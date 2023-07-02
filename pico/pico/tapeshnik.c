/**
 * Copyright (c) 2023 svofski
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "config.h"

#include "debounce.h"
#include "wheel.h"
#include "mainloop.h"
#include "tacho.h"

#define ML_NO_REQUEST   0
#define ML_STOP_REQUEST ' '
int mainloop_request = ML_NO_REQUEST;

void request_wheel_stop()
{
    mainloop_request = ML_STOP_REQUEST;
}

void blink_on()
{
    printf("\033[5m");
}

void blink_off()
{
    printf("\033[25m");
}

void print_color(int c1, int c2, const char * msg, const char * endl)
{
    printf("\033[%d;%dm%s\033[0m%s", c1, c2, msg, endl);
}

char tacho_counter_str[16];
int last_tacho_counter = -1;
int last_wheel_position = -1;
int last_motor_status = -1;

int main() {
    stdio_init_all();

    printf("tapeshnik\n");

    debounce_init();
    wheel_init(GPIO_MOTOR_CONTROL, GPIO_SOLENOID_CONTROL, GPIO_MODE_ENTRY);
    tacho_init(GPIO_TACHO);

    // Wait forever
    while (1) {
        int c = mainloop_request;
        mainloop_request = ML_NO_REQUEST;
        if (c == ML_NO_REQUEST) {
            c = getchar_timeout_us(100);
            if (mainloop_request != ML_NO_REQUEST) {
                c = mainloop_request;
            }
        }
        switch (c) {
            case 'm': motor_enable(!get_motor_status());
                      printf("motor=%d\n", get_motor_status());
                      break;
            case ' ': wheel_stop();
                      break;
            case 'p': wheel_play();
                      break;
            case 'f': wheel_ff();
                      break;
            case 'r': wheel_rew();
                      break;
            case '0': tacho_set_counter(0);
                      break;
            case 10:
            case 13:
                      printf("\nHelp: m=motor, p=play, f=ff, r=rew, space=stop, 0=zero counter\n");
                      break;
        }
        
        if (tacho_get_counter() != last_tacho_counter
                || get_wheel_position() != last_wheel_position
                || get_motor_status() != last_motor_status) {
            last_tacho_counter = tacho_get_counter();
            last_wheel_position = get_wheel_position();
            last_motor_status = get_motor_status();
            snprintf(tacho_counter_str, 16, "  %+05d    \r", last_tacho_counter);
            
            int bgcolor = last_motor_status ? 44 : 42;
            switch (last_wheel_position) {
                case WP_STOP:
                    blink_off();
                    print_color(bgcolor, 37, "   STOP  ", tacho_counter_str);
                    break;
                case WP_PLAY:
                    blink_on();
                    print_color(bgcolor, 37, " <|PLAY  ", tacho_counter_str);
                    break;
                case WP_FF:
                    blink_on();
                    print_color(bgcolor, 37, "  <<FF   ", tacho_counter_str);
                    break;
                case WP_REW:
                    blink_on();
                    print_color(bgcolor, 37, "  REW>>  ", tacho_counter_str);
                    break;
            }
        }
    };

    return 0;
}

