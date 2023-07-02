#include <stdio.h>
#include "pico/stdlib.h"
#include "debounce.h"

typedef struct {
    uint gpio;
    uint32_t value;

    uint32_t state;

    uint32_t direct_enabled;
    gpio_irq_callback_t direct_cb[32];
} debounce_t;

debounce_t debob;
alarm_id_t alarm_id;

int64_t alarm_callback(alarm_id_t id, void * user_data)
{
    uint32_t gpio = (uint32_t)user_data;
    uint32_t mask = ~(1 << gpio);

    debob.value = (debob.value & mask) | (gpio_get(gpio) << gpio);
    debob.state &= ~(1 << gpio); // back to state 0 for this pin

    //printf("gpio%d=%d\n", gpio, 1 & (debob.value >> gpio));
    return 0;
}

void debounce_gpio_cb(uint gpio, uint32_t events)
{
    //printf("gpio_cb gpio=%d\n", gpio);

    if (debob.direct_enabled & (1 << gpio)) {
        debob.direct_cb[gpio](gpio, events);
        return;
    }

    // see gpio_irq_level
    switch (1 & (debob.state >> gpio)) {
        case 0:
            alarm_id = add_alarm_in_ms(10, alarm_callback, (void *)gpio, false);
            debob.state |= 1 << gpio;
            break;
        case 1:
            // obviously a state change means bounce, reset everything
            debob.state &= ~(1 << gpio);
            break;
    }
}

int debounce_get(uint gpio)
{
    return 1 & (debob.value >> gpio);
}

void debounce_init()
{
    debob.state = 0;
    debob.direct_enabled = 0;
    for (int i = 0; i < sizeof(debob.direct_cb)/sizeof(debob.direct_cb[0]); ++i) {
      debob.direct_cb[i] = NULL;
    }
}

void debounce_add_gpio(uint gpio)
{
    uint32_t mask = ~(1 << gpio);

    debob.value = (debob.value & mask) | (gpio_get(gpio) << gpio);
    debob.state &= mask;

    gpio_set_irq_enabled_with_callback(gpio, 
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &debounce_gpio_cb);
}

void debounce_add_gpio_direct(uint gpio, gpio_irq_callback_t callback)
{
    debob.direct_enabled |= 1 << gpio;
    debob.direct_cb[gpio] = callback;
    gpio_set_irq_enabled_with_callback(gpio, 
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &debounce_gpio_cb);
}

