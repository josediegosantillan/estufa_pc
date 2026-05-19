#include "temperature_control.h"

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "actuators.h"
#include "app_board.h"
#include "app_config.h"
#include "app_state.h"
#include "network_services.h"
#include "relay_control.h"

static portMUX_TYPE ds18b20_mux = portMUX_INITIALIZER_UNLOCKED;

const char *temperature_sensor_label(size_t sensor_index)
{
    if (sensor_index == 0) {
        return DS18B20_SENSOR1_LABEL;
    }
    if (sensor_index == 1) {
        return DS18B20_SENSOR2_LABEL;
    }
    return "ds18b20";
}

static bool ds18b20_rom_equals(const uint8_t a[8], const uint8_t b[8])
{
    return memcmp(a, b, 8) == 0;
}

static bool is_motor_control_sensor(size_t sensor_index)
{
    if (ds18b20_sensor_count <= 1) {
        return sensor_index == 0;
    }

    return sensor_index == 1;
}

static inline void ds18b20_drive_low(void)
{
    ESP_ERROR_CHECK(gpio_set_direction(DS18B20_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(DS18B20_GPIO, 0));
}

static inline void ds18b20_release_bus(void)
{
    ESP_ERROR_CHECK(gpio_set_direction(DS18B20_GPIO, GPIO_MODE_INPUT));
}

static inline int ds18b20_read_bus(void)
{
    return gpio_get_level(DS18B20_GPIO);
}

static uint8_t ds18b20_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; ++i) {
        uint8_t inbyte = data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }

    return crc;
}

static bool ds18b20_reset_pulse(void)
{
    bool present = false;

    taskENTER_CRITICAL(&ds18b20_mux);
    ds18b20_drive_low();
    esp_rom_delay_us(480);
    ds18b20_release_bus();
    esp_rom_delay_us(70);
    present = ds18b20_read_bus() == 0;
    esp_rom_delay_us(410);
    taskEXIT_CRITICAL(&ds18b20_mux);

    return present;
}

static void ds18b20_write_bit(int bit)
{
    taskENTER_CRITICAL(&ds18b20_mux);
    ds18b20_drive_low();
    if (bit) {
        esp_rom_delay_us(6);
        ds18b20_release_bus();
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        ds18b20_release_bus();
        esp_rom_delay_us(10);
    }
    taskEXIT_CRITICAL(&ds18b20_mux);
}

static int ds18b20_read_bit(void)
{
    int bit = 0;

    taskENTER_CRITICAL(&ds18b20_mux);
    ds18b20_drive_low();
    esp_rom_delay_us(6);
    ds18b20_release_bus();
    esp_rom_delay_us(9);
    bit = ds18b20_read_bus();
    esp_rom_delay_us(55);
    taskEXIT_CRITICAL(&ds18b20_mux);

    return bit;
}

static void ds18b20_write_byte(uint8_t value)
{
    for (int i = 0; i < 8; ++i) {
        ds18b20_write_bit((value >> i) & 0x01);
    }
}

static uint8_t ds18b20_read_byte(void)
{
    uint8_t value = 0;

    for (int i = 0; i < 8; ++i) {
        value |= ds18b20_read_bit() << i;
    }

    return value;
}

static void ds18b20_write_rom(const uint8_t rom[8])
{
    for (int i = 0; i < 8; ++i) {
        ds18b20_write_byte(rom[i]);
    }
}

static bool ds18b20_search_next(uint8_t rom[8], uint8_t *last_discrepancy, bool *last_device_flag)
{
    int id_bit_number = 1;
    int last_zero = 0;
    int rom_byte_number = 0;
    uint8_t rom_byte_mask = 1;

    if (!rom || !last_discrepancy || !last_device_flag) {
        return false;
    }

    if (*last_device_flag) {
        return false;
    }

    if (!ds18b20_reset_pulse()) {
        *last_discrepancy = 0;
        *last_device_flag = false;
        return false;
    }

    memset(rom, 0, 8);
    ds18b20_write_byte(0xF0);

    while (rom_byte_number < 8) {
        int id_bit = ds18b20_read_bit();
        int cmp_id_bit = ds18b20_read_bit();
        int search_direction = 0;

        if (id_bit == 1 && cmp_id_bit == 1) {
            return false;
        }

        if (id_bit != cmp_id_bit) {
            search_direction = id_bit;
        } else {
            if (id_bit_number < *last_discrepancy) {
                search_direction = (rom[rom_byte_number] & rom_byte_mask) != 0;
            } else {
                search_direction = (id_bit_number == *last_discrepancy);
            }

            if (search_direction == 0) {
                last_zero = id_bit_number;
            }
        }

        if (search_direction) {
            rom[rom_byte_number] |= rom_byte_mask;
        }

        ds18b20_write_bit(search_direction);

        ++id_bit_number;
        rom_byte_mask <<= 1;

        if (rom_byte_mask == 0) {
            ++rom_byte_number;
            rom_byte_mask = 1;
        }
    }

    if (ds18b20_crc8(rom, 7) != rom[7]) {
        return false;
    }

    *last_discrepancy = (uint8_t)last_zero;
    if (*last_discrepancy == 0) {
        *last_device_flag = true;
    }

    return true;
}

static esp_err_t ds18b20_discover_sensors(void)
{
    uint8_t rom[8] = {0};
    uint8_t discovered_roms[DS18B20_MAX_SENSORS][8] = {0};
    uint8_t last_discrepancy = 0;
    bool last_device_flag = false;
    size_t discovered_count = 0;

    ds18b20_sensor_count = 0;
    memset(ds18b20_roms, 0, sizeof(ds18b20_roms));
    memset(temperature_valid, 0, sizeof(temperature_valid));

    while (discovered_count < DS18B20_MAX_SENSORS &&
           ds18b20_search_next(rom, &last_discrepancy, &last_device_flag)) {
        memcpy(discovered_roms[discovered_count], rom, sizeof(rom));
        ++discovered_count;
        if (last_device_flag) {
            break;
        }
    }

    if (discovered_count == 0) {
        ESP_LOGW(TAG, "no se detectaron sensores ds18b20 en gpio=%d", DS18B20_GPIO);
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < discovered_count; ++i) {
        if (ds18b20_rom_equals(discovered_roms[i], DS18B20_SENSOR1_ROM)) {
            memcpy(ds18b20_roms[0], discovered_roms[i], 8);
            break;
        }
    }

    for (size_t i = 0; i < discovered_count; ++i) {
        if (ds18b20_rom_equals(discovered_roms[i], DS18B20_SENSOR1_ROM)) {
            continue;
        }

        if (memcmp(ds18b20_roms[0], discovered_roms[i], 8) != 0 && ds18b20_roms[1][0] == 0) {
            memcpy(ds18b20_roms[1], discovered_roms[i], 8);
            break;
        }
    }

    if (ds18b20_roms[0][0] == 0 && discovered_count > 0) {
        memcpy(ds18b20_roms[0], discovered_roms[0], 8);
    }

    if (ds18b20_roms[1][0] == 0 && discovered_count > 1) {
        for (size_t i = 0; i < discovered_count; ++i) {
            if (!ds18b20_rom_equals(discovered_roms[i], ds18b20_roms[0])) {
                memcpy(ds18b20_roms[1], discovered_roms[i], 8);
                break;
            }
        }
    }

    for (size_t i = 0; i < DS18B20_MAX_SENSORS; ++i) {
        if (ds18b20_roms[i][0] != 0) {
            ds18b20_sensor_count = i + 1;
        }
    }

    for (size_t i = 0; i < ds18b20_sensor_count; ++i) {
        ESP_LOGI(TAG,
                 "%s rom=" MACSTR ":%02X:%02X",
                 temperature_sensor_label(i),
                 MAC2STR(ds18b20_roms[i]),
                 ds18b20_roms[i][6],
                 ds18b20_roms[i][7]);
    }

    return ESP_OK;
}

void ds18b20_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << DS18B20_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI(TAG, "ds18b20 gpio=%d ready", DS18B20_GPIO);
    (void)ds18b20_discover_sensors();
}

esp_err_t ds18b20_read_temperature_c(size_t sensor_index, float *temperature_c)
{
    uint8_t scratchpad[9] = {0};

    if (!temperature_c || sensor_index >= ds18b20_sensor_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ds18b20_reset_pulse()) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(0x55);
    ds18b20_write_rom(ds18b20_roms[sensor_index]);
    ds18b20_write_byte(0x44);
    vTaskDelay(pdMS_TO_TICKS(750));

    if (!ds18b20_reset_pulse()) {
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(0x55);
    ds18b20_write_rom(ds18b20_roms[sensor_index]);
    ds18b20_write_byte(0xBE);

    for (size_t i = 0; i < sizeof(scratchpad); ++i) {
        scratchpad[i] = ds18b20_read_byte();
    }

    if (ds18b20_crc8(scratchpad, 8) != scratchpad[8]) {
        return ESP_ERR_INVALID_CRC;
    }

    int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
    *temperature_c = raw / 16.0f;
    return ESP_OK;
}

static void ds18b20_task(void *arg)
{
    (void)arg;

    while (1) {
        bool status_updated = false;

        if (ds18b20_sensor_count == 0) {
            (void)ds18b20_discover_sensors();
        }

        for (size_t i = 0; i < ds18b20_sensor_count; ++i) {
            float temperature_c = 0.0f;
            esp_err_t err = ds18b20_read_temperature_c(i, &temperature_c);

            if (err == ESP_OK) {
                temperature_valid[i] = true;
                last_temperature_c[i] = temperature_c;
                ESP_LOGI(TAG, "%s = %.2f C", temperature_sensor_label(i), temperature_c);

                if (is_motor_control_sensor(i)) {
                    motor_apply_for_temperature(temperature_c);
                    ESP_LOGI(TAG,
                             "motor control sensor=%s temp=%.2f C pwm=%" PRIu32 " (%" PRIu32 "%%)",
                             temperature_sensor_label(i),
                             temperature_c,
                             motor_pwm_duty,
                             (motor_pwm_duty * 100U) / MOTOR_PWM_DUTY_MAX);

                    if (app_config.buzzer_enabled && temperature_c >= app_config.buzzer_warning_c) {
                        if (buzzer_warning_armed) {
                            buzzer_warning_armed = false;
                            buzzer_play_warning_async();
                            ESP_LOGW(TAG,
                                     "advertencia sonora: %s llego a %.2f C (umbral %.2f C)",
                                     temperature_sensor_label(i),
                                     temperature_c,
                                     app_config.buzzer_warning_c);
                        }
                    } else {
                        buzzer_warning_armed = true;
                    }

                    if (temperature_c >= app_config.relay_cutoff_c) {
                        relay_overheat_lockout = true;
                        if (relay_state) {
                            relay_apply(false);
                            notify_relay_state();
                        }
                        ESP_LOGW(TAG,
                                 "proteccion termica: rele=off por %s %.2f C (corte %.2f C)",
                                 temperature_sensor_label(i),
                                 temperature_c,
                                 app_config.relay_cutoff_c);
                    } else if (relay_overheat_lockout && temperature_c <= app_config.relay_resume_c) {
                        relay_overheat_lockout = false;
                        ESP_LOGI(TAG,
                                 "proteccion termica liberada por %s %.2f C (rearme %.2f C)",
                                 temperature_sensor_label(i),
                                 temperature_c,
                                 app_config.relay_resume_c);
                        if (!relay_state) {
                            relay_apply(true);
                            notify_relay_state();
                            ESP_LOGI(TAG,
                                     "control automatico local: rele=on por %s %.2f C (rearme <= %.2f C)",
                                     temperature_sensor_label(i),
                                     temperature_c,
                                     app_config.relay_resume_c);
                        }
                    } else if (!relay_overheat_lockout &&
                               temperature_c <= app_config.relay_resume_c &&
                               !relay_state) {
                        relay_apply(true);
                        notify_relay_state();
                        ESP_LOGI(TAG,
                                 "control automatico local: rele=on por %s %.2f C (encendido <= %.2f C)",
                                 temperature_sensor_label(i),
                                 temperature_c,
                                 app_config.relay_resume_c);
                    }
                }

                status_updated = true;
            } else if (err == ESP_ERR_NOT_FOUND) {
                temperature_valid[i] = false;
                if (is_motor_control_sensor(i)) {
                    motor_pwm_set_duty(0);
                }
                ESP_LOGW(TAG, "%s no detectado en gpio=%d", temperature_sensor_label(i), DS18B20_GPIO);
            } else if (err == ESP_ERR_INVALID_CRC) {
                temperature_valid[i] = false;
                if (is_motor_control_sensor(i)) {
                    motor_pwm_set_duty(0);
                }
                ESP_LOGW(TAG, "%s crc invalido", temperature_sensor_label(i));
            } else {
                temperature_valid[i] = false;
                if (is_motor_control_sensor(i)) {
                    motor_pwm_set_duty(0);
                }
                ESP_LOGW(TAG, "%s read failed: %s", temperature_sensor_label(i), esp_err_to_name(err));
            }
        }

        if (status_updated) {
            maybe_report_status(false);
        }

        vTaskDelay(DS18B20_POLL_INTERVAL);
    }
}

void ds18b20_task_init(void)
{
    BaseType_t ok = xTaskCreate(ds18b20_task, "ds18b20", 4096, NULL, 4, NULL);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
