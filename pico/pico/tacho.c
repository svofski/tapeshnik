#include <stdio.h>
#include "pico/stdlib.h"
#include "tacho.h"
#include "wheel.h"
#include "mainloop.h"
#include "debounce.h"

repeating_timer_t autostop_timer;
int32_t tacho_counter = 0;
int32_t tacho_dir = 0;
int32_t autostop_alarm_counter = 0;

void interruptor_cb(uint gpio, uint32_t events)
{
    tacho_counter += tacho_dir;
    autostop_alarm_reset();
}

void tacho_init(int pin)
{
    gpio_pull_down(pin);
    gpio_set_input_hysteresis_enabled(pin, true);
    debounce_add_gpio_direct(pin, interruptor_cb);
}

void tacho_set_dir(int dir)
{
    tacho_dir = dir;
}

void tacho_set_counter(int cnt)
{
    tacho_counter = cnt;
}

int tacho_get_counter()
{
    return tacho_counter;
}

bool autostop_alarm_callback(repeating_timer_t *rt)
{
    ++autostop_alarm_counter;
    if (autostop_alarm_counter > AUTOSTOP_COUNTER_LIMIT) {
        //printf("AUTOSTOP\n");
        request_wheel_stop();
        return false; // stop
    }

    return true;  // keep on repeating
}


void autostop_alarm_start(int check_interval)
{
    autostop_alarm_counter = 0;
    autostop_alarm_stop();
    add_repeating_timer_ms(check_interval, autostop_alarm_callback, 
            /*userdata*/NULL, &autostop_timer);
}

void autostop_alarm_reset()
{
    autostop_alarm_counter = 0;
}

void autostop_alarm_stop()
{
    cancel_repeating_timer(&autostop_timer);
}

