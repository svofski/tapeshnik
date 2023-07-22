#pragma once

/* time in milliseconds before motor turns off after STOP */
#define MOTOR_OFF_DELAY 1500

//     ___ 
// ___/ 0 \_____
#define GPIO_MODE_ENTRY 22
#define GPIO_MOTOR_CONTROL 21
#define GPIO_SOLENOID_CONTROL 20

#define GPIO_WRHEAD 16 // pi pico pin 21, green wire
#define GPIO_RDHEAD 17 // pi pico pin 22, yellow wire

#define GPIO_TACHO 19
