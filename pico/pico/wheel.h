#pragma once

typedef enum wheel_position_e {
    WP_UNKNOWN = -1,
    WP_STOP = 0,
    WP_PLAY = 1,
    WP_FF = 2,
    WP_REW = 3,
} wheel_position_t;

struct Motor
{
    int status;
    int gpio;
    int motor_off_alarm_id;

    Motor(int pin) : gpio(pin) {}
    void init();
    void enable(bool ena);
    void motor_on();
    int get_status() const { return this->status; }

    void stop_later();
};

struct Solenoid
{
    int gpio;

    Solenoid(int pin): gpio(pin) {}
    void init();
    void enable(bool enable);
    void pulse_ms(int ms);
};

struct Wheel
{
    Motor& motor;
    Solenoid& solenoid;
    int i_mode_entry;

    int tape_error;
    wheel_position_t wheel_position;

    Wheel(Motor& _motor, Solenoid& _solenoid, int mode_entry_gpio):
        motor(_motor), solenoid(_solenoid), i_mode_entry(mode_entry_gpio) {}
    void init();

    wheel_position_t get_position() const { return wheel_position; }

    int play();
    int ff();
    int rew();
    int stop();
};

