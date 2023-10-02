/**
 * Copyright (c) 2023 svofski
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "config.h"

#include "debounce.h"
#include "wheel.h"
#include "mainloop.h"
#include "tacho.h"
#include "bitstream.h"
#include "util.h"

#define ML_NO_REQUEST   0
#define ML_STOP_REQUEST ' '
int mainloop_request = ML_NO_REQUEST;

Boost boost(GPIO_EBOOST);
Motor motor(boost, GPIO_MOTOR_CONTROL);
Solenoid solenoid(GPIO_SOLENOID_CONTROL);
Wheel wheel(motor, solenoid, GPIO_MODE_ENTRY);
Bitstream bstream(GPIO_RDHEAD, GPIO_WRHEAD, GPIO_WREN);

void request_wheel_stop()
{
    mainloop_request = ML_STOP_REQUEST;
}

char tacho_counter_str[16];
int last_tacho_counter = -1;
int last_wheel_position = -1;
int last_motor_status = -1;

int slice_num;

void freq_sweep()
{
    gpio_set_function(GPIO_WRHEAD, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(GPIO_WRHEAD);   // GP16, pin 21
    pwm_set_clkdiv_int_frac(slice_num, 62, 8);   // pwm slice frequency 125MHz/62.5 = 2MHz
    // useful sweep range: 100Hz..20000Hz
    // wrap = [2e6/100..2e6/20000] = [20000..100]
    pwm_set_enabled(slice_num, true);
    
    printf("linear sweep %d..%d Hz... on pin 21 ", 2000000/20000, 2000000/100);
    for (int i = 20000; i >= 100; --i) {
        pwm_set_wrap(slice_num, i);
        pwm_set_chan_level(slice_num, PWM_CHAN_A, i >> 1);
        sleep_ms(2);
    }
    pwm_set_enabled(slice_num, false);
    printf("done\n");
}

void freq_8(int hz)
{
    gpio_set_function(GPIO_WRHEAD, GPIO_FUNC_PWM);
    auto slice_num = pwm_gpio_to_slice_num(GPIO_WRHEAD);   // GP16, pin 21
    pwm_set_clkdiv_int_frac(slice_num, 62, 8);   // pwm slice frequency 125MHz/62.5 = 2MHz
    // useful sweep range: 100Hz..20000Hz
    // wrap = [2e6/100..2e6/20000] = [20000..100]
    pwm_set_enabled(slice_num, true);
    
    printf("20sec of %d Hz... on pin 21 ", hz);
    int div = static_cast<int>(125000000/62.5/hz);
    pwm_set_wrap(slice_num, div);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, div >> 1);
    //sleep_ms(20000);
    //pwm_set_enabled(slice_num, false);
    //gpio_set_function(GPIO_WRHEAD, GPIO_FUNC_NULL);
    //printf("done\n");
}

void freq_stop()
{
    pwm_set_enabled(slice_num, false);
    gpio_set_function(GPIO_WRHEAD, GPIO_FUNC_NULL);
}

int main() {
    stdio_init_all();

    printf("tapeshnik\n");

    debounce_init();
    //wheel_init(GPIO_MOTOR_CONTROL, GPIO_SOLENOID_CONTROL, GPIO_MODE_ENTRY);

    tacho_init(GPIO_TACHO);

    wheel.init();

    //gpio_disable_pulls(GPIO_RDHEAD);
    //gpio_pull_up(GPIO_RDHEAD);
    //gpio_set_dir(GPIO_RDHEAD, /* out */ true);
    //gpio_put(GPIO_RDHEAD, 1);

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
            case 'z': solenoid.pulse_ms(50);
                      break;
            case 'm': motor.enable(!motor.get_status());
                      printf("motor=%d\n", motor.get_status());
                      break;
            case ' ': wheel.stop();
                      freq_stop();
                      break;
            case 'p': wheel.play();
                      break;
            case 'f': wheel.ff();
                      break;
            case 'r': wheel.rew();
                      break;
            case '0': tacho_set_counter(0);
                      break;
            case 's': freq_sweep();
                      break;
            case '4': freq_8(4000);
                      break;
            case '8': freq_8(8000);
                      break;
            case '1': freq_8(10000);
                      break;
            case '2': freq_8(20000);
                      break;
            case '3': freq_8(30000);
                      break;
            case '5': bstream.test();
                      break;
            case 'w': bstream.test(BS_TX);
                      break;
            case 'l': bstream.test(BS_RX);
                      break;
            case 'e': bstream.test_sector_rewrite();
                      break;
            case 10:
            case 13:
                      printf("\nHelp: m=motor, p=play, f=ff, r=rew, space=stop, 0=zero counter\n");
                      set_color(41, 37);
                      printf("ERROR TEST");
                      reset_color();
                      putchar('\n');
                      break;
        }

        if (tacho_get_counter() != last_tacho_counter
                || wheel.get_position() != last_wheel_position
                || motor.get_status() != last_motor_status) {
            last_tacho_counter = tacho_get_counter();
            last_wheel_position = wheel.get_position();
            last_motor_status = motor.get_status();
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

