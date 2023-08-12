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

#include "bitstream.h"
#include "readloop.h"
#include "sectors.h"

#include "config.h"

#include "correct.h"
#include "crc.h"
#include "mfm.h"


#define MFM_FREQ 3300 // max frequency at output


#define modulate    fm_encode_twobyte
#define demodulate  fm_decode_twobyte

const uint32_t LEADER = 0xAAAAAAAA;
const uint32_t SYNC   = 0xAAA1A1A1;

//const uint16_t MFM_SYNC_A1 = 0x78f1;
//const uint32_t MFM_SYNC = 0x1e1e78f1; // A1, level = 1, bit = 1

const unsigned char * get_plaintext();
size_t get_plaintext_size();

correct_reed_solomon * rs_rx = 0;

// can we share one PIO between two cores?
PIO pio = pio0;
uint sm_tx = 0, sm_rx = 1;

// mfm read shift register
constexpr int mfm_period = 8;
constexpr int mfm_midperiod = mfm_period / 2;

uint8_t mfm_prev_level;

int64_t start_time, end_time;

uint32_t inverted = 0;

// raw data are read in this buffer
std::array<uint8_t, fec_block_length> rx_fec_buf;
size_t rx_fec_index;

// rs decoded sector payload
sector_layout_t rx_sector_buf;
int32_t rx_prev_sector_num;

uint32_t bitsampler_pio()
{
    return pio_sm_get_blocking(pio, sm_rx); // take next sample
}


///// encode raw payload (223 bytes) to 255-byte output buffer
///// sector_buf points to sector_layout_t with other fields filled in
///// encoded_buf is the output buffer which is 255 bytes long
///size_t encode_block(correct_reed_solomon * rs, const uint8_t * data, size_t data_sz, 
///        sector_layout_t * sector_buf, uint8_t * encoded_buf)
///{
///    std::copy(data, data + std::min(payload_data_sz, data_sz), sector_buf->data);
///
///    // make sure that block remainder is empty
///    std::fill(sector_buf->data + data_sz, sector_buf->data + payload_data_sz, 0);
///
///    sector_buf->crc16 = calculate_crc(sector_buf->data, payload_data_sz);
///
///    const uint8_t * sector_bytes = reinterpret_cast<uint8_t *>(sector_buf);
///    return correct_reed_solomon_encode(rs, sector_bytes, fec_message_sz, encoded_buf);
///}


// basic mfm reader callback: expect MFM sync word, then read and print forever
readloop_state_t simple_mfm_reader(readloop_state_t state, uint32_t bits)
{
    if (state == TS_RESYNC) {
        //printf("%x\n", bits);
        if (bits == SYNC) {
            start_time = time_us_64();
            //printf("SYNC\n");
            inverted = 0;
            return TS_READ;
        }
        if (~bits == SYNC) {
            start_time = time_us_64();
            //printf("SYNC INV\n");
            inverted = 0xffffffff;
            return TS_READ;
        }
    }
    else if (state == TS_READ) {
        uint8_t c1, c2;
        demodulate(bits ^ inverted, &c1, &c2, &mfm_prev_level);
        putchar(c1);
        putchar(c2);
        return TS_READ;
    }
    return state;
}

// ruins raw sector data for testing
// reed-solomon will recover up to fec_min_distance / 2 byte errors
void fuckup_sector_data()
{
    constexpr int maxfuck = fec_min_distance / 2 + /* unrecoverable errors: */ 0;
    for (int i = 0; i < maxfuck; ++i) {
        ssize_t ofs = rand() % rx_fec_buf.size();
        rx_fec_buf[ofs] ^= rand() & 0xff;
    }
}

// decodes and verifies data from rx_sector_buf
int correct_sector_data()
{
    uint8_t * rx_ptr = reinterpret_cast<uint8_t *>(&rx_sector_buf);
    ssize_t decoded_sz = correct_reed_solomon_decode(rs_rx, rx_fec_buf.begin(),
            rx_fec_buf.size(), rx_ptr);
    if (decoded_sz <= 0) {
        std::copy_n(rx_fec_buf.begin(), sizeof(rx_sector_buf), rx_ptr);
        printf("\n\n--- unrecoverable error in sector %d  ---\n", rx_sector_buf.sector_num);
    }
    if (rx_sector_buf.sector_num != rx_prev_sector_num + 1) {
        printf("\n\n--- sector out of sequence; previous: %d current: %d\n\n", 
                rx_prev_sector_num, rx_sector_buf.sector_num);
    }

    uint16_t crc = calculate_crc(&rx_sector_buf.data[0], payload_data_sz);

    std::string filename(reinterpret_cast<const char *>(&rx_sector_buf.file_id[0]), file_id_sz);
    printf("sector %d; file: '%s' crc: actual: %04x expected: %04x %s\n", 
            rx_sector_buf.sector_num,
            filename.c_str(),
            crc, rx_sector_buf.crc16,
            (crc == rx_sector_buf.crc16) ? "OK" : "CRC ERROR"
            );

    //for (size_t i = 0; i < payload_data_sz; ++i) {
    //    putchar(rx_sector_buf.data[i]);
    //}

    rx_prev_sector_num = rx_sector_buf.sector_num;

    if (rx_sector_buf.reserved0 & SECTOR_FLAG_EOF) {
        printf("\nEOF\n");
        return -1;
    }

    return 0;
}

// sector reader callback
readloop_state_t sector_mfm_reader(readloop_state_t state, uint32_t bits)
{
    if (state == TS_RESYNC) {
        if (bits  == SYNC) {
            rx_fec_index = 0;
            //printf("\nSYNC %08x\n", bits);
            inverted = 0x0;
            mfm_prev_level = 1;
            return TS_READ;
        }
        else if (~bits == SYNC) {
            rx_fec_index = 0;
            //printf("\nSYNC INV %08x\n", bits);
            inverted = 0xffffffff;
            mfm_prev_level = 0;
            return TS_READ;
        }

    }
    else if (state == TS_READ) {
        uint8_t c1, c2;
        demodulate(bits ^ inverted, &c1, &c2, &mfm_prev_level);
        rx_fec_buf[rx_fec_index++] = c1;
        if (rx_fec_index < rx_fec_buf.size()) {
            rx_fec_buf[rx_fec_index++] = c2;
        }
        if (rx_fec_index < rx_fec_buf.size()) {
            return TS_READ;
        }
        else {
#if LOOPBACK_TEST
            fuckup_sector_data();
#endif
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

// core1 runs independently and pretends not to know anything
// about received data
void core1_entry()
{
    printf("core1_entry\n");
    rx_prev_sector_num = -1;
    readloop_delaylocked(sector_mfm_reader);
    //readloop_simple(sector_mfm_reader);
    //readloop_naiive(sector_mfm_reader);

    multicore_fifo_push_blocking(TS_TERMINATE);
}

void sanity_check()
{
    uint8_t cur_level = 0, prev_bit = 0;
    uint32_t mfmbits = mfm_encode_twobyte(0x55,0xa1, &cur_level, &prev_bit);
    for (int i = 0; i < 32; ++i) {
        putchar('0' + ((mfmbits >> (31-i)) & 1));
    }
    printf(" %08x %08x\n", mfmbits, ~mfmbits);

    uint8_t c1, c2;
    uint8_t prev_level = 0;
    mfm_decode_twobyte(mfmbits, &c1, &c2, &prev_level);
    printf("decoded: %02x %02x\n", c1, c2);
}

void insanity_check()
{
    uint8_t cur_level = 0, prev_bit = 0;
    uint32_t fmbits = fm_encode_twobyte(0x55,0xa1, &cur_level, &prev_bit);
    for (int i = 0; i < 32; ++i) {
        putchar('0' + ((fmbits >> (31-i)) & 1));
    }
    printf(" %08x %08x\n", fmbits, ~fmbits);

    uint8_t c1, c2;
    uint8_t prev_level = 0;
    fm_decode_twobyte(fmbits, &c1, &c2, &prev_level);
    printf("decoded: %02x %02x\n", c1, c2);
}

void bitstream_test(int mode)
{
    sanity_check();
    insanity_check();

    readloop_setparams(
            {
            .bitwidth = mfm_period,
            .Kp = 0.0333,
            .Ki = 0.000001,
            .alpha = 0.1,
            .sampler = bitsampler_pio
            });

    // sectors
    SectorWrite sectors;

    // load pio programs
    // tx program loads 32-bit words and sends them 1 bit at a time every 8 clock cycles
    uint offset_tx = pio_add_program(pio, &bitstream_tx_program);
    printf("Transmit program loaded at %d\n", offset_tx);

    // rx program samples input on every clock cycle and outputs a word
    uint offset_rx = pio_add_program(pio, &bitstream_rx_program);
    printf("Receive program loaded at %d\n", offset_rx);

    // Configure state machines, push bits out at 8000 bps (max freq 4000hz)
    constexpr float clkdiv = 125e6/(MFM_FREQ * 2 * mfm_period);

    bitstream_tx_program_init(pio, sm_tx, offset_tx, GPIO_WRHEAD, clkdiv);
    bitstream_rx_program_init(pio, sm_rx, offset_rx, GPIO_RDHEAD, clkdiv);

    if (mode & BS_TX) pio_sm_set_enabled(pio, sm_tx, true);
    if (mode & BS_RX) pio_sm_set_enabled(pio, sm_rx, true);

    uint8_t mfm_cur_level = 0, mfm_prev_bit = 0;

    // load plaintext data
    size_t psz = get_plaintext_size();
    const unsigned char * plaintext = get_plaintext();
    printf("plaintext size: %lu bytes\n", psz);

    // initialize forward error correction
    if (mode & BS_RX) {
        rs_rx = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
                1, 1, fec_min_distance);
    }

    pio_sm_clear_fifos(pio, sm_rx);

    // launch the receiver in core1
    if (mode & BS_RX) multicore_launch_core1(core1_entry);

    // wait for the core to launch
    sleep_ms(10);

    //tx_sector_buf = {};
    //std::copy_n(std::string("alicetxt").begin(), file_id_sz, &tx_sector_buf.file_id[0]);
    sectors.set_file_id("alicetxt");

    // mark time to calculate effective payload CPS
    start_time = time_us_64();

    if (mode & BS_TX) {
        // encode the blocks and send them out
        for (size_t input_ofs = 0, sector_num = 0; input_ofs < psz; input_ofs += payload_data_sz) {
            int data_sz = std::min(psz - input_ofs, payload_data_sz);

            if (data_sz + input_ofs >= psz) {
                sectors.set_eof();
            }

            // this takes a moment: prepare all data before writing
            // in writing phase is important so there must be no fifo underruns
            //encode_block(rs_tx, plaintext + input_ofs, data_sz, &tx_sector_buf, tx_fec_buf.begin());
            sectors.prepare(plaintext + input_ofs, data_sz, sector_num);

            // add a looong leader before the first block
            if (sector_num == 0) {
                for (int i = 0; i < 128; ++i) {
                    pio_sm_put_blocking(pio, sm_tx, LEADER);
                    pio_sm_put_blocking(pio, sm_tx, LEADER);
                    pio_sm_put_blocking(pio, sm_tx, LEADER);
                    pio_sm_put_blocking(pio, sm_tx, LEADER);
                }
            }

            // pre-block leader for tuning up the DLL, it also creates a time gap
            // needed by the receiver to decode the block
            for (int i = 0; i < 16; ++i) {
                pio_sm_put_blocking(pio, sm_tx, LEADER);
            }
            // mfm sync word
            pio_sm_put_blocking(pio, sm_tx, SYNC);
            mfm_cur_level = 1;    // adjust line levels after sync word
            mfm_prev_bit = 1;

            //for (size_t i = 0; i < tx_fec_buf.size(); i += 2) {
            //    uint32_t mfm_encoded = modulate(tx_fec_buf[i], 
            //            tx_fec_buf[i + 1], &mfm_cur_level, &mfm_prev_bit);
            //    pio_sm_put_blocking(pio, sm_tx, mfm_encoded);
            //}
            for (size_t i = 0; i < sectors.size(); i += 2) {
                uint32_t mfm_encoded = modulate(sectors[i], 
                        sectors[i + 1], &mfm_cur_level, &mfm_prev_bit);
                pio_sm_put_blocking(pio, sm_tx, mfm_encoded);
            }

            // post-sector gap
            for (int i = 0; i < 8; ++i) {
                pio_sm_put_blocking(pio, sm_tx, LEADER);
            }

            if (!(mode & BS_RX)) {
                printf("wrote sector %d crc %04x\n", sector_num, sectors.crc16());
            }

            // advance sector and block numbers
            //++tx_sector_buf.sector_num;
            ++sector_num; 
#if SINGLE_SECTOR_TEST
            break;// TEST
#endif
        }
    }

    int c;
    if (mode & BS_RX) {
        if (!(mode & BS_TX)) {
            printf("waiting for rx (press d early to print debugbuf)\n");
        }
        // wait for the message from the receiver before shutting down
        //multicore_fifo_pop_blocking();
        uint32_t out;
        for(int i = 0; i < 120*10; ++i) {
            multicore_fifo_pop_timeout_us(100000ULL, &out); // 0.1s
            if (out == TS_TERMINATE) {
                break;
            }
            c = getchar_timeout_us(0); 
            if (c != PICO_ERROR_TIMEOUT) {
                break;
            }
        }
    }

    end_time = time_us_64();

    uint32_t timediff = end_time - start_time;
    uint32_t timediff_ms = timediff / 1000;
    uint32_t timediff_s = timediff_ms / 1000;
    printf("elapsed time=%dms, speed=%dcps\n", timediff_ms,
            psz/timediff_s);

    if (mode & BS_RX) {
        // shut down core1
        multicore_reset_core1();
    }

#if 1
    if (c == 'd') {
        readloop_dump_debugbuf();
    }
#endif

    // shut down PIO
    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_set_enabled(pio, sm_rx, false);
    pio_remove_program(pio, &bitstream_tx_program, offset_tx);
    pio_remove_program(pio, &bitstream_rx_program, offset_rx);
    pio_clear_instruction_memory(pio);

    if (mode & BS_RX) {
        correct_reed_solomon_destroy(rs_rx);
    }
}
