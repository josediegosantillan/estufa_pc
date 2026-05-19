#pragma once

#include <stdint.h>

void motor_pwm_init(void);
void motor_pwm_set_duty(uint32_t duty);
void motor_apply_for_temperature(float temperature_c);
void buzzer_init(void);
void buzzer_task_init(void);
void buzzer_play_warning_async(void);
