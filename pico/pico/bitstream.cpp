/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

//#include <stdio.h>
#include <cstdio>
#include <cstdint>
#include <array>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "bitstream.pio.h"

#include "config.h"

//extern const uint8_t plaintext[];
//extern const size_t plaintext_sz;

//MFM_SYNC = [01,00,01,00,10,00,10,01]
const uint16_t MFM_SYNC_A1 = 0x4489;// 0100,0100,1000,1001
const uint32_t MFM_SYNC = 0x55554489;  // prefixed with leader 01010101...

const char * get_plaintext();
size_t get_plaintext_size();

// 8 bits -> 16 mfm-bits, returns last bit 
uint32_t mfm_encode_twobyte(uint8_t c1, uint8_t c2, uint8_t *prev_bit)
{
    uint16_t o = (c1 << 8) | c2;

    uint8_t prev = *prev_bit;
    uint32_t mfm = 0;

    for (int i = 0; i < 16; ++i) {
        int clock = 0;
        int bit = (o >> (15 - i)) & 1;
        if (bit == 0) {
            clock = 1 - prev;
        }
        prev = bit;

        mfm = (mfm << 1) | clock;
        mfm = (mfm << 1) | bit;
    }

    *prev_bit = prev;

    return mfm;
}

// mfm read shift register
uint32_t mfm_bits = 0;

void mfm_decode_twobyte(uint32_t mfm, uint8_t * c1, uint8_t * c2)
{
    uint tb = 0;

    for (int i = 0; i < 16; ++i) {
        // just skip clock bits
        uint bit = (mfm & (1 << 30)) >> 30;
        tb = (tb << 1) | bit;
        mfm <<= 2;
    }
    *c1 = (tb >> 8) & 0xff;
    *c2 = tb & 0xff;
}

std::array<uint32_t, 64> buffer;
//uint32_t buffer[64];


// try running it on core1?
uint32_t mfm_wait_sync(PIO pio, uint sm_rx, uint16_t sync16, uint max_bits)
{
    uint32_t prev = 0;
    int sample_t = 0;

    for(uint i = 0; i < max_bits; ++i) {
        uint32_t cur = pio_sm_get_blocking(pio, sm_rx);
        if (cur != prev) {
            sample_t = 0;
        }
        else {
            ++sample_t;
            if (sample_t == 8) {
                sample_t = 0;
            }
        }

        if (sample_t == 4) {
            // take sample hopefully in the middle 
            mfm_bits = (mfm_bits << 1) | cur;
            //printf("%x ", mfm_bits);
            if ((mfm_bits & 0xffff) == sync16) {
                //printf("HOORAY\n");
                return mfm_bits;
            }
        }

        prev = cur;
    }

    return 0;
}

#if 0
uint32_t mfm_recv(PIO pio, uint sm_rx)
{
    uint32_t prev = 0;
    int sample_t = 0;

    for(uint i = 0; i < max_bits; ++i) {
        uint32_t cur = pio_sm_get_blocking(pio, sm_rx);
        if (cur != prev) {
            sample_t = 0;
        }
        else {
            ++sample_t;
            if (sample_t == 8) {
                sample_t = 0;
            }
        }

        if (sample_t == 4) {
            // take sample hopefully in the middle 
            mfm_bits = (mfm_bits << 1) | cur;
            //printf("%x ", mfm_bits);
            if ((mfm_bits & 0xffff) == sync16) {
                //printf("HOORAY\n");
                return mfm_bits;
            }
        }

        prev = cur;
    }

    return 0;
}
#endif

void bitstream_test() {
    PIO pio = pio0;
    uint sm_tx = 0, sm_rx = 1;

    uint offset_tx = pio_add_program(pio, &bitstream_tx_program);
    printf("Transmit program loaded at %d\n", offset_tx);
    uint offset_rx = pio_add_program(pio, &bitstream_rx_program);
    printf("Receive program loaded at %d\n", offset_rx);

    // Configure state machines, push bits out at 8000 bps (max freq 4000hz)
    bitstream_tx_program_init(pio, sm_tx, offset_tx, GPIO_WRHEAD, 125e6/(8000 * 8));

    bitstream_rx_program_init(pio, sm_rx, offset_rx, GPIO_RDHEAD, 125e6/(8000 * 8));

    pio_sm_set_enabled(pio, sm_tx, true);
    pio_sm_set_enabled(pio, sm_rx, true);

    uint8_t mfm_prev_bit;

    printf("SEND MFM_SYNC\n");
    pio_sm_clear_fifos(pio, sm_rx);
    pio_sm_put_blocking(pio, sm_tx, MFM_SYNC);

    uint32_t rxsync = mfm_wait_sync(pio, sm_rx, MFM_SYNC_A1, 1024);

    uint8_t c1, c2;
    mfm_decode_twobyte(rxsync, &c1, &c2);
    printf("rxsync=$%x (%02x %02x)\n", rxsync, c1, c2);

    //printf("\n--- buffor ---\n");
    //for(uint i = 0; i < 64; ++i) {
    //    printf("%x ", buffer[i]);
    //}
    //printf("\n--- buffor ---\n");

    return;

    mfm_prev_bit = 1; // because the last bit in A1 sync is "1"

    printf("getting plaintext\n");
    size_t psz = get_plaintext_size();
    const char * plaintext = get_plaintext();

    printf("got plaintext\n");

    pio_sm_clear_fifos(pio, sm_rx);

    for(size_t i = 0; i < psz; i += 2) {
        putchar(plaintext[i]);
        putchar(plaintext[i + 1]);
        uint32_t mfm_encoded = mfm_encode_twobyte(plaintext[i], plaintext[i + 1],
                &mfm_prev_bit);

        pio_sm_put_blocking(pio, sm_tx, mfm_encoded); // 32*8 = 256 cycles
    }
    pio_sm_set_enabled(pio, sm_tx, false);

    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_set_enabled(pio, sm_rx, false);
    //void pio_remove_program(PIO pio, const pio_program_t *program, uint loaded_offset);
    pio_remove_program(pio, &bitstream_tx_program, offset_tx);
    pio_remove_program(pio, &bitstream_rx_program, offset_rx);
    pio_clear_instruction_memory(pio);
}
