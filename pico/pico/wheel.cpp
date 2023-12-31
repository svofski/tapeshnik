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

void Motor::init()
{
    gpio_init(this->gpio);
    gpio_put(this->gpio, 0);
    gpio_set_dir(this->gpio, GPIO_OUT);
    this->status = 0;
    this->motor_off_alarm_id = -1;

    boost.init();
}

void Motor::enable(bool ena)
{
    if (ena && !boost.state()) {
        boost.power_on();
        sleep_ms(100);
    }
    cancel_alarm(motor_off_alarm_id);
    motor_off_alarm_id = -1;
    this->status = ena ? 1 : 0;
    gpio_put(this->gpio, this->status);
    if (!ena) {
        boost.power_off();
    }
}

void Motor::motor_on()
{
    cancel_alarm(motor_off_alarm_id);
    motor_off_alarm_id = -1;

    if (!this->status) {
        this->enable(1);
        sleep_ms(250);
    }
}

int64_t motor_off_cb(alarm_id_t id, void * user_data)
{
    Motor * that = static_cast<Motor *>(user_data);
    that->enable(false);
    that->motor_off_alarm_id = -1;
    return 0;
}

void Motor::stop_later()
{
    if (!this->status) {
        return;
    }

    cancel_alarm(this->motor_off_alarm_id);
    motor_off_alarm_id = add_alarm_in_ms(MOTOR_OFF_DELAY, motor_off_cb, this, false);
}

void Solenoid::init()
{
    gpio_init(this->gpio);
    gpio_put(this->gpio, 0);
    gpio_set_dir(this->gpio, GPIO_OUT);
}

void Solenoid::enable(bool enable)
{
    gpio_put(this->gpio, enable ? 1 : 0); 
}

void Solenoid::pulse_ms(int ms)
{
    this->enable(1);
    sleep_ms(ms);
    this->enable(0);
}

int Wheel::stop()
{
    this->tape_error = ERR_OK;

    autostop_alarm_stop();
    
    if (debounce_get(this->i_mode_entry) != 0) {
        this->wheel_position = WP_STOP;
        motor.stop_later();
        tacho_set_dir(0);
        return ERR_OK;
    }

    motor.motor_on();
    motor.stop_later();

    // faster step out from PLAY position
    if (this->wheel_position == WP_PLAY) {
        this->solenoid.pulse_ms(SOLENOID_PULSE_MS);
        ::sleep_ms(200);
        this->solenoid.pulse_ms(SOLENOID_PULSE_MS);
        ::sleep_ms(200);
    }

    if (debounce_get(this->i_mode_entry) != 0) {
        wheel_position = WP_STOP;
        tacho_set_dir(0);
        return ERR_OK;
    }
    //

    for (int retry = 0; (retry < 3) && (debounce_get(this->i_mode_entry) == 0); ++retry) {
        solenoid.pulse_ms(SOLENOID_PULSE_MS);
        for (int i = 0; i < 10; ++i) {
            sleep_ms(50);
            if (debounce_get(this->i_mode_entry)) {
                break;
            }
        }
    }

    if (!debounce_get(this->i_mode_entry)) {
        tape_error = ERR_WHEEL_CANNOT_RESET;
    }
    else {
        sleep_ms(25);
        wheel_position = WP_STOP;
    }
    tacho_set_dir(0);

    return tape_error;
}

int Wheel::play()
{
    this->tape_error = ERR_OK;

    this->motor.motor_on();

    if (this->wheel_position == WP_PLAY)
        return ERR_OK;

    autostop_alarm_stop();

    if (this->wheel_position != WP_STOP) {
        this->stop();
        this->motor.motor_on(); // cancel motor_stop_later
    }

    tacho_set_dir(+1);

    if (this->tape_error != ERR_OK) 
        return tape_error;

    this->solenoid.pulse_ms(SOLENOID_PULSE_MS);
    sleep_ms(425);  // half-turn +
    this->wheel_position = WP_PLAY;

    autostop_alarm_start(AUTOSTOP_SLOW);
    //tacho_set_dir(+1);

    return ERR_OK;
}

int Wheel::ff()
{
    if (this->wheel_position == WP_FF && motor.get_status()) {
        return ERR_OK;
    }

    this->play();
    autostop_alarm_stop();

    if (this->tape_error != ERR_OK)
        return this->tape_error;

    this->solenoid.pulse_ms(SOLENOID_PULSE_MS);
    sleep_ms(325);
    this->wheel_position = WP_FF;

    autostop_alarm_start(AUTOSTOP_QUICK);

    return ERR_OK;
}

int Wheel::rew()
{
    if (this->wheel_position == WP_REW && motor.get_status()) {
        return ERR_OK;
    }

    this->play();
    autostop_alarm_stop();

    if (this->tape_error != ERR_OK)
        return this->tape_error;

    this->solenoid.pulse_ms(SOLENOID_PULSE_MS);
    sleep_ms(50);
    this->solenoid.pulse_ms(SOLENOID_PULSE_MS);
    sleep_ms(122);
    tacho_set_dir(-1); // start counting backwards precisely at this moment

    // it seems that it should be ~178 here, but for quick short rewinds
    // like during retries it winds back too much, so decrease the delay to 50
    //sleep_ms(178);
    sleep_ms(50);
    this->wheel_position = WP_REW;
    
    autostop_alarm_start(AUTOSTOP_QUICK);

    return ERR_OK;
}

void Wheel::init()
{
    motor.init();
    solenoid.init();
    gpio_pull_up(this->i_mode_entry);
    debounce_add_gpio(this->i_mode_entry);
    this->wheel_position = WP_UNKNOWN;
}

void Boost::init()
{
    gpio_init(this->gpio);
    gpio_pull_down(this->gpio);
    gpio_put(this->gpio, 0);
    gpio_set_dir(this->gpio, GPIO_OUT);
}

void Boost::power_on()
{
    gpio_put(this->gpio, 1);
}

void Boost::power_off()
{
    gpio_put(this->gpio, 0);
}

bool Boost::state() const
{
    return gpio_get(this->gpio);
}
