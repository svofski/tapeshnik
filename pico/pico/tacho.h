#pragma once

#define AUTOSTOP_COUNTER_LIMIT  2
#define AUTOSTOP_QUICK          50
#define AUTOSTOP_SLOW           150 // 100 is ok at reel start, but near the end it is slower

void autostop_alarm_start(int check_interval);
void autostop_alarm_stop();
void autostop_alarm_reset();
void tacho_init(int pin);
void tacho_set_dir(int dir);
void tacho_set_counter(int value);
int tacho_get_counter();
