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
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "bitstream.pio.h"

#include "config.h"

#define  Kp     0.093
#define  Ki     0.000137
#define  alpha  0.099

//extern const uint8_t plaintext[];
//extern const size_t plaintext_sz;

//MFM_SYNC = [01,00,01,00,10,00,10,01]
const uint16_t MFM_SYNC_A1 = 0x4489;// 0100,0100,1000,1001
const uint32_t MFM_SYNC = 0x55554489;  // prefixed with leader 01010101...

const char * get_plaintext();
size_t get_plaintext_size();


// can we share one PIO between two cores?
PIO pio = pio0;
uint sm_tx = 0, sm_rx = 1;

// mfm read shift register
uint32_t mfm_bits = 0;
int mfm_period = 8;
int mfm_midperiod = mfm_period / 2;


uint32_t start_time, end_time;

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

typedef enum _mfm_track_state
{
    TS_RESYNC = 0,
    TS_READ,
    TS_TERMINATE,
} mfm_track_state_t;

typedef mfm_track_state_t (*mfm_callback_t)(mfm_track_state_t, uint32_t);

uint32_t mfm_track_simple(mfm_callback_t cb)
{
    uint32_t prev = 0;
    int sample_t = 0;
    int bitcount = 0;

    mfm_track_state_t state = TS_RESYNC;

    for (; state != TS_TERMINATE;) {
        uint32_t cur = pio_sm_get_blocking(pio, sm_rx);
        if (cur != prev) {
            sample_t = 0;
        }
        else {
            ++sample_t;
            if (sample_t == mfm_period) {
                sample_t = 0;
            }
        }

        if (sample_t == mfm_midperiod) {
            // take sample hopefully in the middle 
            mfm_bits = (mfm_bits << 1) | cur;

            switch (state) {
                case TS_RESYNC:
                    state = cb(state, mfm_bits);
                    bitcount = 0;
                    break;
                case TS_READ:
                    if (++bitcount == 32) {
                        bitcount = 0;
                        state = cb(state, mfm_bits);
                    }
                    break;
                case TS_TERMINATE:
                    break;
            }
        }

        prev = cur;
    }

    return 0;
}

uint32_t mfm_track_pid(mfm_callback_t cb)
{
    int last_acc = 0;
    int lastbit = 0;
    int phase_delta = 0;
    int phase_delta_filtered = 0;
    int integ = 0;

    int nscale = 20;
    int scale = 1 << nscale;
    int one = scale;
    int iKp = (int)(Kp * scale);
    int iKi = (int)(Ki * scale);
    int ialpha = (int)(alpha * scale);

    int integ_max = 512 * scale;

    int acc_size = 512;
    int ftw0 = acc_size / mfm_period * scale;
    int ftw = 0;
    int iacc_size = acc_size * scale;
    int iacc = iacc_size / 2;


    int bitcount = 0;

    mfm_track_state_t state = TS_RESYNC;

    for (; state != TS_TERMINATE;) {
        uint32_t bit = pio_sm_get_blocking(pio, sm_rx); // bit
        if (bit != lastbit) {  // input transition
            phase_delta = iacc_size / 2 - iacc; // 180 deg off transition point
        }

        int64_t tmp64 = (int64_t)phase_delta * ialpha;
        tmp64 += (int64_t)phase_delta_filtered * (one - ialpha);
        phase_delta_filtered = tmp64 >> nscale;

        integ += (int64_t)(phase_delta_filtered * iKi) >> nscale;

        if (integ > integ_max) {
            integ = integ_max;
        }
        else if (integ < -integ_max) {
            integ = -integ_max;
        }
        
        ftw = ftw0 + (((int64_t)phase_delta_filtered * iKp) >> nscale) + integ;
        lastbit = bit;
        iacc = iacc + ftw;
        if (iacc >= iacc_size) {
            iacc -= iacc_size;
            //sampled_bits[(*sampled_bits_sz)++] = bit;
            mfm_bits = (mfm_bits << 1) | bit;

            switch (state) {
                case TS_RESYNC:
                    state = cb(state, mfm_bits);
                    bitcount = 0;
                    break;
                case TS_READ:
                    if (++bitcount == 32) {
                        bitcount = 0;
                        state = cb(state, mfm_bits);
                    }
                    break;
                case TS_TERMINATE:
                    break;
            }
        }
    }

    return 0;
}

// expect MFM sync word, then read and print
mfm_track_state_t simple_mfm_reader(mfm_track_state_t state, uint32_t bits)
{
    if (state == TS_RESYNC) {
        if ((mfm_bits & 0xffff) == MFM_SYNC_A1) {
            start_time = time_us_32();
            return TS_READ;
        }
    }
    else if (state == TS_READ) {
        uint8_t c1, c2;
        mfm_decode_twobyte(bits, &c1, &c2);
        putchar(c1);
        putchar(c2);
        return TS_READ;
    }
    return state;
}

// try running it on core1?
uint32_t mfm_wait_sync(PIO pio, uint sm_rx, uint16_t sync16, int max_bits)
{
    uint32_t prev = 0;
    int sample_t = 0;

    for (uint i = 0; i < max_bits; ++i) {
        uint32_t cur = pio_sm_get_blocking(pio, sm_rx);
        if (cur != prev) {
            sample_t = 0;
        }
        else {
            ++sample_t;
            if (sample_t == mfm_period) {
                sample_t = 0;
            }
        }

        if (sample_t == mfm_midperiod) {
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

// sample and return 32 mfm-bits
// return value suitable for decoding with mfm_decode_twobyte()
uint32_t mfm_recv32(PIO pio, uint sm_rx)
{
    uint32_t prev = 0;
    int sample_t = 0;

    for (uint n = 0; n < 32;) {
        uint32_t cur = pio_sm_get_blocking(pio, sm_rx);
        if (cur != prev) {
            sample_t = 0;
        }
        else {
            ++sample_t;
            if (sample_t == mfm_period) {
                sample_t = 0;
            }
        }

        // take sample hopefully in the middle 
        if (sample_t == mfm_midperiod) {
            mfm_bits = (mfm_bits << 1) | cur;
            ++n;
        }

        prev = cur;
    }

    return mfm_bits;
}

void core1_entry()
{
//    uint32_t rxsync;
//    do {
//        rxsync = mfm_wait_sync(pio, sm_rx, MFM_SYNC_A1, 1024);
//    } while ((rxsync & 0xffff) != MFM_SYNC_A1);
//
//    uint8_t c1, c2;
//    mfm_decode_twobyte(rxsync, &c1, &c2);
//    printf("rxsync=$%x (%02x %02x)\n", rxsync, c1, c2);
//    for(;;) {
//        uint32_t tb = mfm_recv32(pio, sm_rx);
//        mfm_decode_twobyte(tb, &c1, &c2);
//        putchar(c1);
//        putchar(c2);
//    }
    mfm_track_pid(simple_mfm_reader);
}

void bitstream_test() {
    uint offset_tx = pio_add_program(pio, &bitstream_tx_program);
    printf("Transmit program loaded at %d\n", offset_tx);
    uint offset_rx = pio_add_program(pio, &bitstream_rx_program);
    printf("Receive program loaded at %d\n", offset_rx);

    // Configure state machines, push bits out at 8000 bps (max freq 4000hz)
    float clkdiv_max = 125e6/((8000 - 1200) * 8);
    float clkdiv_min = 125e6/((8000 + 1200) * 8);
    float clkdiv = 125e6/(8000 * 8);
    float dclkdiv = 100;

    bitstream_tx_program_init(pio, sm_tx, offset_tx, GPIO_WRHEAD, clkdiv);
    bitstream_rx_program_init(pio, sm_rx, offset_rx, GPIO_RDHEAD, 125e6/(8000 * 8));

    pio_sm_set_enabled(pio, sm_tx, true);
    pio_sm_set_enabled(pio, sm_rx, true);

    uint8_t mfm_prev_bit;

    printf("getting plaintext\n");
    size_t psz = get_plaintext_size();
    const char * plaintext = get_plaintext();
    printf("got plaintext\n");

    pio_sm_clear_fifos(pio, sm_rx);

    // launch the receiver in core1
    multicore_launch_core1(core1_entry);

    // wait for the core to launch
    sleep_ms(10);

    printf("SEND MFM_SYNC %u\n", time_us_32());
    pio_sm_put_blocking(pio, sm_tx, 0x55555555);
    pio_sm_put_blocking(pio, sm_tx, 0x55555555);
    pio_sm_put_blocking(pio, sm_tx, MFM_SYNC);
    for(size_t i = 0; i < psz; i += 2) {
        uint32_t mfm_encoded = mfm_encode_twobyte(plaintext[i], plaintext[i + 1],
                &mfm_prev_bit);

        clkdiv = clkdiv + dclkdiv;
        if (clkdiv >= clkdiv_max) {
            clkdiv = clkdiv_max;
            dclkdiv = -dclkdiv;
            //printf("\nMAX\n");
        }
        else if (clkdiv <= clkdiv_min) {
            clkdiv = clkdiv_min;
            dclkdiv = -dclkdiv;
            //printf("\nMIN\n");
        }
        pio_sm_set_clkdiv(pio, sm_tx, clkdiv);

        pio_sm_put_blocking(pio, sm_tx, mfm_encoded); // 32*8 = 256 cycles
    }

    // let the receiver finish before shutting down
    sleep_ms(250);

    end_time = time_us_32();

    uint32_t timediff = end_time - start_time;
    uint32_t timediff_ms = timediff / 1000;
    uint32_t timediff_s = timediff_ms / 1000;
    printf("elapsed time=%dms, speed=%dcps\n", timediff_ms,
            psz/timediff_s);

    // shut down core1
    multicore_reset_core1();

    // shut down PIO
    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_set_enabled(pio, sm_rx, false);
    pio_remove_program(pio, &bitstream_tx_program, offset_tx);
    pio_remove_program(pio, &bitstream_rx_program, offset_rx);
    pio_clear_instruction_memory(pio);
}
