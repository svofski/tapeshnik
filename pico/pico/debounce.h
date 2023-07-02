#pragma once

#include <pico/types.h>

void debounce_init();
void debounce_add_gpio(uint gpio);
int debounce_get(uint gpio);
void debounce_add_gpio_direct(uint gpio, gpio_irq_callback_t callback);
