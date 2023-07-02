#include <stdio.h>
#include "pico/stdlib.h"
#include "wheel.h"
#include "debounce.h"
#include "tacho.h"
#include "config.h"

enum tape_error_t {
    ERR_OK = 0,
    ERR_WHEEL_CANNOT_RESET = -1,
};

int tape_error = 0;
int motor_status = 0;
int wheel_position = WP_UNKNOWN;

int motor_gpio = 0;
int solenoid_gpio = 0;
int mode_gpio = 0;
int motor_off_alarm_id = -1;

void motor_init(int pin)
{
    motor_gpio = pin;

    gpio_init(motor_gpio);
    gpio_put(motor_gpio, 0);
    gpio_set_dir(motor_gpio, GPIO_OUT);
    motor_status = 0;
    motor_off_alarm_id = -1;
}

void motor_enable(bool ena)
{
    cancel_alarm(motor_off_alarm_id);
    motor_off_alarm_id = -1;
    motor_status = ena ? 1 : 0;
    gpio_put(motor_gpio, motor_status);
}

void motor_on()
{
    cancel_alarm(motor_off_alarm_id);
    motor_off_alarm_id = -1;

    if (!motor_status) {
        motor_enable(1);
        sleep_ms(250);
    }
}

int64_t motor_off_cb(alarm_id_t id, void * user_data)
{
    motor_enable(false);
    motor_off_alarm_id = -1;
    return 0;
}

void motor_stop_later()
{
    if (!motor_status) {
        return;
    }

    cancel_alarm(motor_off_alarm_id);
    motor_off_alarm_id = add_alarm_in_ms(MOTOR_OFF_DELAY, motor_off_cb, NULL, false);
}

int get_motor_status()
{
    return motor_status;
}

void solenoid_init(int gpo_pin)
{
    solenoid_gpio = gpo_pin;
    gpio_init(solenoid_gpio);
    gpio_put(solenoid_gpio, 0);
    gpio_set_dir(solenoid_gpio, GPIO_OUT);
}

void solenoid_enable(bool enable)
{
    gpio_put(solenoid_gpio, enable ? 1 : 0); 
}

void solenoid_pulse_ms(int ms)
{
    solenoid_enable(1);
    sleep_ms(ms);
    solenoid_enable(0);
}

int wheel_stop()
{
    tape_error = ERR_OK;

    autostop_alarm_stop();
    tacho_set_dir(0);
    
    if (debounce_get(mode_gpio) != 0) {
        wheel_position = WP_STOP;
        motor_stop_later();
        return ERR_OK;
    }

    motor_on();
    motor_stop_later();

    // faster step out from PLAY position
    if (wheel_position == WP_PLAY) {
        solenoid_pulse_ms(10);
        sleep_ms(200);
        solenoid_pulse_ms(10);
        sleep_ms(200);
    }

    if (debounce_get(mode_gpio) != 0) {
        wheel_position = WP_STOP;
        return ERR_OK;
    }
    //

    for (int retry = 0; (retry < 3) && (debounce_get(mode_gpio) == 0); ++retry) {
        solenoid_pulse_ms(10);
        for (int i = 0; i < 10; ++i) {
            sleep_ms(50);
            if (debounce_get(mode_gpio)) {
                break;
            }
        }
    }

    if (!debounce_get(mode_gpio)) {
        tape_error = ERR_WHEEL_CANNOT_RESET;
    }
    else {
        sleep_ms(25);
        wheel_position = WP_STOP;
    }
    
    return tape_error;
}

int wheel_play()
{
    tape_error = ERR_OK;

    motor_on();

    if (wheel_position == WP_PLAY)
        return ERR_OK;

    autostop_alarm_stop();

    if (wheel_position != WP_STOP) {
        wheel_stop();
        motor_on(); // cancel motor_stop_later
    }

    tacho_set_dir(+1);

    if (tape_error != ERR_OK) 
        return tape_error;

    solenoid_pulse_ms(10);
    sleep_ms(425);  // half-turn +
    wheel_position = WP_PLAY;

    autostop_alarm_start(AUTOSTOP_SLOW);

    return ERR_OK;
}

int wheel_ff()
{
    if (wheel_position == WP_FF && motor_status) {
        return ERR_OK;
    }

    wheel_play();
    autostop_alarm_stop();

    if (tape_error != ERR_OK)
        return tape_error;

    solenoid_pulse_ms(10);
    sleep_ms(325);
    wheel_position = WP_FF;

    autostop_alarm_start(AUTOSTOP_QUICK);

    return ERR_OK;
}

int wheel_rew()
{
    if (wheel_position == WP_REW && motor_status) {
        return ERR_OK;
    }

    wheel_play();
    //autostop_alarm_stop();

    if (tape_error != ERR_OK)
        return tape_error;

    solenoid_pulse_ms(10);
    sleep_ms(50);
    solenoid_pulse_ms(10);
    sleep_ms(100);
    tacho_set_dir(-1);
    sleep_ms(200);
    wheel_position = WP_REW;
    
    autostop_alarm_start(AUTOSTOP_QUICK);

    return ERR_OK;
}

void wheel_init(int gpo_motor, int gpo_solenoid_control, int gpi_mode_entry)
{
    motor_init(gpo_motor);
    solenoid_init(gpo_solenoid_control);
    mode_gpio = gpi_mode_entry;
    gpio_pull_up(mode_gpio);
    debounce_add_gpio(mode_gpio);
}

wheel_position_t get_wheel_position()
{
    return wheel_position;
}
