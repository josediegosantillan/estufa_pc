#include "actuators.h"

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_board.h"
#include "app_config.h"
#include "app_state.h"

static TaskHandle_t buzzer_task_handle = NULL;

static void buzzer_set_tone(uint32_t freq_hz, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq_hz));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

static void buzzer_stop(void)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

static void buzzer_warning_task(void *arg)
{
    (void)arg;
    const uint32_t half_duty = 512;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        switch (app_config.buzzer_tone_type) {
        case 1:
            for (int i = 0; i < 3; ++i) {
                buzzer_set_tone(2000, half_duty);
                vTaskDelay(pdMS_TO_TICKS(500));
                buzzer_stop();
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            break;
        case 2:
            for (int i = 0; i < 6; ++i) {
                buzzer_set_tone(800, half_duty);
                vTaskDelay(pdMS_TO_TICKS(200));
                buzzer_set_tone(1600, half_duty);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            buzzer_stop();
            break;
        default:
            for (int i = 0; i < 3; ++i) {
                buzzer_set_tone(2200, half_duty);
                vTaskDelay(pdMS_TO_TICKS(120));
                buzzer_stop();
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            break;
        }
    }
}

void motor_pwm_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_channel_config_t channel_cfg = {
        .gpio_num = MOTOR_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
    motor_pwm_set_duty(0);
}

void motor_pwm_set_duty(uint32_t duty)
{
    if (duty > MOTOR_PWM_DUTY_MAX) {
        duty = MOTOR_PWM_DUTY_MAX;
    }

    if (motor_pwm_duty == duty) {
        return;
    }

    motor_pwm_duty = duty;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    ESP_LOGI(TAG,
             "motor pwm duty=%" PRIu32 " (%" PRIu32 "%%) gpio=%d",
             duty,
             (duty * 100U) / MOTOR_PWM_DUTY_MAX,
             MOTOR_PWM_GPIO);
}

void motor_apply_for_temperature(float temperature_c)
{
    if (temperature_c < app_config.motor_temp_off_c) {
        motor_pwm_set_duty(0);
        return;
    }

    if (temperature_c < app_config.motor_temp_max_c) {
        float span = app_config.motor_temp_max_c - app_config.motor_temp_off_c;
        uint32_t min_running_duty = (MOTOR_PWM_DUTY_MAX * app_config.motor_pwm_min_pct) / 100U;
        if (span <= 0.0f) {
            motor_pwm_set_duty(0);
            return;
        }
        float ratio = (temperature_c - app_config.motor_temp_off_c) / span;
        uint32_t duty = min_running_duty +
                        (uint32_t)((MOTOR_PWM_DUTY_MAX - min_running_duty) * ratio);
        motor_pwm_set_duty(duty);
        return;
    }

    motor_pwm_set_duty(MOTOR_PWM_DUTY_MAX);
}

void buzzer_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_channel_config_t channel_cfg = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
    buzzer_stop();
}

void buzzer_task_init(void)
{
    BaseType_t ok = xTaskCreate(buzzer_warning_task, "buzzer_warn", 2048, NULL, 3, &buzzer_task_handle);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

void buzzer_play_warning_async(void)
{
    if (buzzer_task_handle) {
        xTaskNotifyGive(buzzer_task_handle);
    }
}
