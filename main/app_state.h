#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app_board.h"

typedef struct {
    float motor_temp_off_c;
    float motor_temp_max_c;
    float relay_cutoff_c;
    float relay_resume_c;
    float buzzer_warning_c;
    float buzzer_disarm_c;
    uint8_t motor_pwm_min_pct;
    uint8_t buzzer_tone_type;
    bool buzzer_enabled;
} app_config_t;

extern const char *TAG;

extern const uint8_t EDGE_AGENT_MAC_1[6];
extern const uint8_t EDGE_AGENT_MAC_2[6];
extern const uint8_t ESPNOW_CHANNEL;
extern const char *WIFI_STA_SSID;
extern const char *WIFI_STA_PASSWORD;
extern const uint8_t DS18B20_SENSOR1_ROM[8];

extern bool relay_state;
extern bool relay_overheat_lockout;
extern bool buzzer_warning_armed;
extern uint32_t motor_pwm_duty;
extern size_t ds18b20_sensor_count;
extern uint8_t ds18b20_roms[DS18B20_MAX_SENSORS][8];
extern bool temperature_valid[DS18B20_MAX_SENSORS];
extern float last_temperature_c[DS18B20_MAX_SENSORS];
extern bool status_snapshot_valid;
extern float last_reported_temperature_c[DS18B20_MAX_SENSORS];
extern bool last_reported_relay_state;
extern uint32_t last_status_report_tick;
extern app_config_t app_config;
extern bool system_enabled;
extern bool motor_enabled;
