/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "bitstream.pio.h"

#include "config.h"

// Differential serial transmit/receive example
// Need to connect a wire from GPIO2 -> GPIO3

void bitstream_test() {
    PIO pio = pio0;
    uint sm_tx = 0;

    uint offset_tx = pio_add_program(pio, &bitstream_tx_program);
    printf("Transmit program loaded at %d\n", offset_tx);

    // Configure state machines, push bits out at 8000 bps (max freq 4000hz)
    bitstream_tx_program_init(pio, sm_tx, offset_tx, GPIO_WRHEAD, 
            125000000UL / 8000);

    pio_sm_set_enabled(pio, sm_tx, true);

    for(;;) {
        putchar('.');
        //pio_sm_put_blocking(pio, sm_tx, 0x55ccaa33);
        pio_sm_put_blocking(pio, sm_tx, 0x55555555);
    }
//    pio_sm_put_blocking(pio, sm_tx, 0);
//    pio_sm_put_blocking(pio, sm_tx, 0x0ff0a55a);
//    pio_sm_put_blocking(pio, sm_tx, 0x12345678);
//    pio_sm_set_enabled(pio, sm_tx, true);
//
//    for (int i = 0; i < 3; ++i)
//        printf("%08x\n", pio_sm_get_blocking(pio, sm_rx));
}
