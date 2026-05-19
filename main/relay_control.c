#include "relay_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_board.h"
#include "app_state.h"
#include "network_services.h"

void relay_apply(bool enabled)
{
    relay_state = enabled;
    ESP_ERROR_CHECK(gpio_set_level(RELAY_GPIO, enabled ? 1 : 0));
    ESP_LOGI(TAG, "relay=%s gpio=%d", enabled ? "on" : "off", RELAY_GPIO);
}

void relay_init(void)
{
    const gpio_config_t relay_cfg = {
        .pin_bit_mask = 1ULL << RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&relay_cfg));
    relay_apply(false);
}

void unused_gpio_init(void)
{
    if (UNUSED_GPIO_PULLDOWN_MASK == 0) {
        ESP_LOGI(TAG, "unused gpio pulldown skipped");
        return;
    }

    const gpio_config_t unused_cfg = {
        .pin_bit_mask = UNUSED_GPIO_PULLDOWN_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&unused_cfg));
}

static void relay_button_task(void *arg)
{
    (void)arg;

    bool last_sample = gpio_get_level(RELAY_BUTTON_GPIO) == 0;
    bool stable_state = last_sample;
    TickType_t last_change_tick = xTaskGetTickCount();

    while (1) {
        bool pressed = gpio_get_level(RELAY_BUTTON_GPIO) == 0;
        TickType_t now = xTaskGetTickCount();

        if (pressed != last_sample) {
            last_sample = pressed;
            last_change_tick = now;
        }

        if (pressed != stable_state && (now - last_change_tick) >= BUTTON_DEBOUNCE_TIME) {
            stable_state = pressed;
            if (stable_state) {
                relay_apply(!relay_state);
                notify_relay_state();
                ESP_LOGI(TAG, "relay button pressed gpio=%d", RELAY_BUTTON_GPIO);
            }
        }

        vTaskDelay(BUTTON_POLL_INTERVAL);
    }
}

void relay_button_init(void)
{
    const gpio_config_t button_cfg = {
        .pin_bit_mask = 1ULL << RELAY_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_cfg));
    BaseType_t ok = xTaskCreate(relay_button_task, "relay_btn", 2048, NULL, 5, NULL);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
