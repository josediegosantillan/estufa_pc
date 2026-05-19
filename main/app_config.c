#include "app_config.h"

#include "nvs.h"

void config_set_defaults(app_config_t *config)
{
    if (!config) {
        return;
    }

    config->motor_temp_off_c = 35.0f;
    config->motor_temp_max_c = 60.0f;
    config->relay_cutoff_c = 48.0f;
    config->relay_resume_c = 47.0f;
    config->buzzer_warning_c = 47.0f;
    config->motor_pwm_min_pct = 35;
    config->buzzer_enabled = true;
    config->buzzer_disarm_c = 44.0f;
    config->buzzer_tone_type = 0;
}

esp_err_t config_load(void)
{
    nvs_handle_t handle = 0;
    size_t size = sizeof(app_config);

    config_set_defaults(&app_config);

    esp_err_t err = nvs_open("appcfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, "config", &app_config, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND || size != sizeof(app_config)) {
        config_set_defaults(&app_config);
        return ESP_OK;
    }

    return err;
}

esp_err_t config_save(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("appcfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, "config", &app_config, sizeof(app_config));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t config_validate(const app_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->motor_temp_max_c <= config->motor_temp_off_c) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->relay_cutoff_c > 60.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->relay_resume_c >= config->relay_cutoff_c) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->motor_pwm_min_pct > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->buzzer_disarm_c >= config->buzzer_warning_c) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

bool wifi_sta_credentials_present(void)
{
    return WIFI_STA_SSID[0] != '\0';
}
