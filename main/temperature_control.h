#pragma once

#include <stddef.h>
#include "esp_err.h"

const char *temperature_sensor_label(size_t sensor_index);
void ds18b20_init(void);
void ds18b20_task_init(void);
esp_err_t ds18b20_read_temperature_c(size_t sensor_index, float *temperature_c);
