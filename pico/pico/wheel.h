#pragma once

typedef enum wheel_position_e {
    WP_UNKNOWN = -1,
    WP_STOP = 0,
    WP_PLAY = 1,
    WP_FF = 2,
    WP_REW = 3,
} wheel_position_t;

void motor_init();
void motor_enable(bool ena);
int get_motor_status();

void wheel_init(int gpo_motor, int gpo_solenoid_control, int gpi_mode_entry);
int wheel_stop();
int wheel_play();
int wheel_ff();
int wheel_rew();

wheel_position_t get_wheel_position();


