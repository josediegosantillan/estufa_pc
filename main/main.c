#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_log.h"
#include "actuators.h"
#include "app_board.h"
#include "app_config.h"
#include "app_state.h"
#include "network_services.h"
#include "relay_control.h"
#include "temperature_control.h"

void app_main(void)
{
    ESP_LOGI(TAG, "app_main start");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "nvs ok");
    ESP_ERROR_CHECK(config_load());
    if (config_validate(&app_config) != ESP_OK) {
        ESP_LOGW(TAG, "config invalida en nvs, restaurando defaults");
        config_set_defaults(&app_config);
        ESP_ERROR_CHECK(config_save());
    }
    ESP_LOGI(TAG,
             "config: motor %.1f-%.1fC min=%u%% relay %.1f/%.1fC buzzer %.1fC %s",
             app_config.motor_temp_off_c,
             app_config.motor_temp_max_c,
             app_config.motor_pwm_min_pct,
             app_config.relay_resume_c,
             app_config.relay_cutoff_c,
             app_config.buzzer_warning_c,
             app_config.buzzer_enabled ? "on" : "off");

    unused_gpio_init();
    ESP_LOGI(TAG, "unused gpio init ok");
    relay_init();
    ESP_LOGI(TAG, "relay init ok");
    relay_button_init();
    ESP_LOGI(TAG, "relay button init ok");
    motor_pwm_init();
    ESP_LOGI(TAG, "motor pwm init ok gpio=%d", MOTOR_PWM_GPIO);
    buzzer_init();
    ESP_LOGI(TAG, "buzzer init ok gpio=%d", BUZZER_GPIO);
    buzzer_task_init();
    ESP_LOGI(TAG, "buzzer task init ok");
    ds18b20_init();
    ESP_LOGI(TAG, "ds18b20 init ok");
    ds18b20_task_init();
    ESP_LOGI(TAG, "ds18b20 task init ok");
    wifi_init();
    ESP_LOGI(TAG, "wifi init ok");
    espnow_init_peer();
    ESP_LOGI(TAG, "espnow init ok");
    if (wifi_sta_credentials_present()) {
        ESP_LOGI(TAG, "web: cuando conecte a wifi entra por la IP que aparezca en el serial");
    } else {
        ESP_LOGI(TAG, "para habilitar web local, completa WIFI_STA_SSID y WIFI_STA_PASSWORD en app_state.c");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
