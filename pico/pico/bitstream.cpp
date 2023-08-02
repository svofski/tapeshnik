/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

//#include <stdio.h>
#include <cstdio>
#include <cstdint>
#include <array>
#include <algorithm>
#include <string>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "bitstream.pio.h"

#include "correct.h"

#include "crc.h"

#include "config.h"

#define MFM_FREQ 10000 // max frequency at output

#define  Kp     0.093
#define  Ki     0.000137
#define  alpha  0.099

//extern const uint8_t plaintext[];
//extern const size_t plaintext_sz;

//MFM_SYNC = [01,00,01,00,10,00,10,01]
const uint16_t MFM_SYNC_A1 = 0x4489;// 0100,0100,1000,1001
const uint32_t MFM_SYNC = 0x55554489;  // prefixed with leader 01010101...

const unsigned char * get_plaintext();
size_t get_plaintext_size();


// can we share one PIO between two cores?
PIO pio = pio0;
uint sm_tx = 0, sm_rx = 1;

// mfm read shift register
uint32_t mfm_bits = 0;
int mfm_period = 8;
int mfm_midperiod = mfm_period / 2;

int64_t start_time, end_time;

// error correction
constexpr size_t block_length = 255;
constexpr size_t min_distance = 32;
constexpr size_t message_length = block_length - min_distance;

//uint8_t input_buf[255];
correct_reed_solomon * rs_tx;
correct_reed_solomon * rs_rx;

constexpr size_t payload_data_sz = 200;

// sector payload: 1 + 1 + 2 + 12 + 4 + 200 + 2 + 1 = 223 bytes
struct sector_payload_t { 
    uint8_t   track_num;
    uint8_t   sector_num_msb;
    uint16_t  sector_num;
    uint8_t   filename[12];
    uint32_t  block_num;
    uint8_t   data[payload_data_sz];
    uint16_t  crc16;
    uint8_t   reserved2;
} __attribute__((packed));

sector_payload_t tx_sector_buf;

// tx buffer for feck (+1 pad to 256 for easier encoding)
std::array<uint8_t, block_length + 1> tx_fec_buf;

// raw data are read in this buffer
std::array<uint8_t, block_length> rx_fec_buf;
size_t rx_fec_index;

// rs decoded sector payload
sector_payload_t rx_sector_buf;
int32_t rx_prev_sector_num;

uint16_t calculate_crc(uint8_t * data, size_t len)
{
    return MODBUS_CRC16_v3(data, len);
}

// encode raw payload (223 bytes) to 255-byte output buffer
// sector_buf points to sector_payload_t with other fields filled in
// encoded_buf is the output buffer which is 255 bytes long
size_t encode_block(correct_reed_solomon * rs, const uint8_t * data, size_t data_sz, 
        sector_payload_t * sector_buf, uint8_t * encoded_buf)
{
    std::copy(data, data + std::min(payload_data_sz, data_sz), sector_buf->data);

    // make sure that block remainder is empty
    std::fill(sector_buf->data + data_sz, sector_buf->data + payload_data_sz, 0);

    sector_buf->crc16 = calculate_crc(sector_buf->data, payload_data_sz);

    const uint8_t * sector_bytes = reinterpret_cast<uint8_t *>(sector_buf);
    return correct_reed_solomon_encode(rs, sector_bytes, message_length, encoded_buf);
}


// --- MFM ----

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

    multicore_fifo_push_blocking(TS_TERMINATE);

    return 0;
}

uint32_t mfm_track_pid(mfm_callback_t cb)
{
    uint32_t lastbit = 0;
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

    multicore_fifo_push_blocking(TS_TERMINATE);

    return 0;
}

// expect MFM sync word, then read and print
mfm_track_state_t simple_mfm_reader(mfm_track_state_t state, uint32_t bits)
{
    if (state == TS_RESYNC) {
        if ((mfm_bits & 0xffff) == MFM_SYNC_A1) {
            start_time = time_us_64();
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

// reed-solomon will recover up to min_distance / 2 byte errors
void fuckup_sector_data()
{
    constexpr int maxfuck = min_distance / 2 + /* unrecoverable errors: */ 0;
    for (int i = 0; i < maxfuck; ++i) {
        ssize_t ofs = rand() % rx_fec_buf.size();
        rx_fec_buf[ofs] ^= rand() & 0xff;
    }
}

int correct_sector_data()
{
    uint8_t * rx_ptr = reinterpret_cast<uint8_t *>(&rx_sector_buf);
    ssize_t decoded_sz = correct_reed_solomon_decode(rs_rx, rx_fec_buf.begin(),
            rx_fec_buf.size(), rx_ptr);
    if (decoded_sz <= 0) {
        std::copy_n(rx_fec_buf.begin(), sizeof(rx_sector_buf), rx_ptr);
        printf("\n\n--- unrecoverable error in sector %d  ---\n", 
                (rx_sector_buf.sector_num_msb << 16) | rx_sector_buf.sector_num);
    }
    if (rx_sector_buf.sector_num != rx_prev_sector_num + 1) {
        printf("\n\n--- sector out of sequence; previous: %d current: %d\n\n", 
                rx_prev_sector_num, rx_sector_buf.sector_num);
    }

    uint16_t crc = calculate_crc(&rx_sector_buf.data[0], payload_data_sz);

    std::string filename(reinterpret_cast<const char *>(&rx_sector_buf.filename[0]), 12);
    printf("sector %d; file: '%s' block: %d crc: actual: %04x expected: %04x %s\n", 
            (rx_sector_buf.sector_num_msb << 16) | rx_sector_buf.sector_num,
            filename.c_str(),
            rx_sector_buf.block_num,
            crc, rx_sector_buf.crc16,
            (crc == rx_sector_buf.crc16) ? "OK" : "CRC ERROR"
            );

    //for (size_t i = 0; i < payload_data_sz; ++i) {
    //    putchar(rx_sector_buf.data[i]);
    //}

    rx_prev_sector_num = rx_sector_buf.sector_num;

    if (rx_sector_buf.reserved2 == '#') {
        printf("\nEOF\n");
        return -1;
    }

    return 0;
}

mfm_track_state_t sector_mfm_reader(mfm_track_state_t state, uint32_t bits)
{
    if (state == TS_RESYNC) {
        if ((mfm_bits & 0xffff) == MFM_SYNC_A1) {
            rx_fec_index = 0;
            return TS_READ;
        }
    }
    else if (state == TS_READ) {
        uint8_t c1, c2;
        mfm_decode_twobyte(bits, &c1, &c2);
        rx_fec_buf[rx_fec_index++] = c1;
        if (rx_fec_index < rx_fec_buf.size()) {
            rx_fec_buf[rx_fec_index++] = c2;
        }
        if (rx_fec_index < rx_fec_buf.size()) {
            return TS_READ;
        }
        else {
            fuckup_sector_data();
            int res = correct_sector_data();
            if (res == -1) {
                return TS_TERMINATE;
            }
            else {
                return TS_RESYNC;
            }
        }
    }
    return state;
}


// try running it on core1?
uint32_t mfm_wait_sync(PIO pio, uint sm_rx, uint16_t sync16, uint32_t max_bits)
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
    rx_prev_sector_num = -1;
    mfm_track_pid(sector_mfm_reader);
}

void bitstream_test() {
    uint offset_tx = pio_add_program(pio, &bitstream_tx_program);
    printf("Transmit program loaded at %d\n", offset_tx);
    uint offset_rx = pio_add_program(pio, &bitstream_rx_program);
    printf("Receive program loaded at %d\n", offset_rx);

    // Configure state machines, push bits out at 8000 bps (max freq 4000hz)
    float clkdiv = 125e6/(MFM_FREQ * 2 * 8);

    bitstream_tx_program_init(pio, sm_tx, offset_tx, GPIO_WRHEAD, clkdiv);
    bitstream_rx_program_init(pio, sm_rx, offset_rx, GPIO_RDHEAD, clkdiv);

    pio_sm_set_enabled(pio, sm_tx, true);
    pio_sm_set_enabled(pio, sm_rx, true);

    uint8_t mfm_prev_bit;

    printf("getting plaintext\n");
    size_t psz = get_plaintext_size();
    const unsigned char * plaintext = get_plaintext();
    printf("got plaintext\n");

    // initialize forward error correction
    rs_tx = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
                1, 1, min_distance);
    rs_rx = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
                1, 1, min_distance);

    pio_sm_clear_fifos(pio, sm_rx);

    // launch the receiver in core1
    multicore_launch_core1(core1_entry);

    // wait for the core to launch
    sleep_ms(10);

    tx_sector_buf = {};
    std::copy_n(std::string("alice.txt  ").begin(), 12, &tx_sector_buf.filename[0]);
    tx_sector_buf.reserved2 = '*';

    start_time = time_us_64();

    // make 200-byte long blocks
    for (size_t input_ofs = 0; input_ofs < psz; input_ofs += payload_data_sz) {
        int data_sz = std::min(psz - input_ofs, payload_data_sz);

        if (data_sz + input_ofs >= psz) {
            tx_sector_buf.reserved2 = '#';
        }

        size_t encoded_len = encode_block(rs_tx, plaintext + input_ofs,
                data_sz, &tx_sector_buf, tx_fec_buf.begin());

        // this also creates a gap for the rx to decode everything
        pio_sm_put_blocking(pio, sm_tx, 0x55555555);
        pio_sm_put_blocking(pio, sm_tx, 0x55555555);
        pio_sm_put_blocking(pio, sm_tx, 0x55555555);
        pio_sm_put_blocking(pio, sm_tx, 0x55555555);
        pio_sm_put_blocking(pio, sm_tx, MFM_SYNC);

        for (size_t i = 0; i < tx_fec_buf.size(); i += 2) {
            uint32_t mfm_encoded = mfm_encode_twobyte(tx_fec_buf[i], 
                    tx_fec_buf[i + 1], &mfm_prev_bit);
            pio_sm_put_blocking(pio, sm_tx, mfm_encoded);
        }


        ++tx_sector_buf.block_num;
        ++tx_sector_buf.sector_num;
    }

    // wait for the message from the receiver before shutting down
    multicore_fifo_pop_blocking();

    end_time = time_us_64();

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
