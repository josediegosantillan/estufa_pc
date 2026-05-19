#include "app_state.h"

const char *TAG = "espnow_peer";

const uint8_t EDGE_AGENT_MAC_1[6] = { 0xDC, 0xB4, 0xD9, 0x17, 0x91, 0x04 };
const uint8_t EDGE_AGENT_MAC_2[6] = { 0x80, 0xB5, 0x4E, 0xDE, 0x45, 0xAC };

const uint8_t ESPNOW_CHANNEL = 11;
const char *WIFI_STA_SSID = "MERCUSYS_6A69";
const char *WIFI_STA_PASSWORD = "93429856";
const uint8_t DS18B20_SENSOR1_ROM[8] = {0x28, 0xBB, 0x88, 0xAF, 0x00, 0x00, 0x00, 0x27};

bool relay_state = false;
bool relay_overheat_lockout = false;
bool buzzer_warning_armed = true;
uint32_t motor_pwm_duty = 0;
size_t ds18b20_sensor_count = 0;
uint8_t ds18b20_roms[DS18B20_MAX_SENSORS][8] = {0};
bool temperature_valid[DS18B20_MAX_SENSORS] = {false};
float last_temperature_c[DS18B20_MAX_SENSORS] = {0.0f};
bool status_snapshot_valid = false;
float last_reported_temperature_c[DS18B20_MAX_SENSORS] = {0.0f};
bool last_reported_relay_state = false;
uint32_t last_status_report_tick = 0;
app_config_t app_config = {0};
bool system_enabled = false;
bool motor_enabled = true;
