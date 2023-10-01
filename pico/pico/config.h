#pragma once

/* time in milliseconds before motor turns off after STOP */
#define MOTOR_OFF_DELAY 1500


#define GPIO_EBOOST 6 // 1 = enable 12V boost converter

//     ___ 
// ___/ 0 \_____
#define GPIO_MODE_ENTRY 22
#define GPIO_MOTOR_CONTROL 21
#define GPIO_SOLENOID_CONTROL 20

#define GPIO_WRHEAD 16  // pi pico pin 21, green wire
#define GPIO_RDHEAD 17  // pi pico pin 22, yellow wire
#define GPIO_WREN   18  // 1 = write, 0 = read

#define GPIO_TACHO 19

#define MOD_HALFPERIOD  8     // number of clocks per half-period in modulation
#define MOD_FREQ        3300  // max bit flipping frequency
                              //
#define SOLENOID_PULSE_MS 25
