#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "app_state.h"

void config_set_defaults(app_config_t *config);
esp_err_t config_load(void);
esp_err_t config_save(void);
esp_err_t config_validate(const app_config_t *config);
bool wifi_sta_credentials_present(void);
