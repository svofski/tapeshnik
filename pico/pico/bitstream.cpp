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

// current working version is FM
#define modulate    fm_encode_twobyte
#define demodulate  fm_decode_twobyte

// not modulated, these words are written bit by bit
const uint32_t LEADER = 0xAAAAAAAA;
const uint32_t SYNC   = 0xAAA1A1A1;

const unsigned char * get_plaintext();
size_t get_plaintext_size();
const unsigned char * get_edittext();
size_t get_edittext_size();

correct_reed_solomon * rs_rx = 0;

PIO pio = pio0;
uint sm_tx = 0, sm_rx = 1;

uint8_t prev_level;

int64_t start_time, end_time;

uint32_t inverted = 0;

// raw data are read in this buffer
std::array<uint8_t, fec_block_length> rx_fec_buf;
size_t rx_fec_index;

// rs decoded sector payload
sector_layout_t rx_sector_buf;
int32_t rx_prev_sector_num;

uint32_t bitsampler_or = 0;

uint32_t bitsampler_pio()
{
    return bitsampler_or | pio_sm_get_blocking(pio, sm_rx); // take next sample
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

    multicore_fifo_push_blocking(MSG_SECTOR_READ_DONE);

    std::string filename(reinterpret_cast<const char *>(&rx_sector_buf.file_id[0]), file_id_sz);
    printf("sector %d; file: '%s' crc: actual: %04x expected: %04x %s\n", 
            rx_sector_buf.sector_num,
            filename.c_str(),
            crc, rx_sector_buf.crc16,
            (crc == rx_sector_buf.crc16) ? "OK" : "CRC ERROR"
            );


    for (size_t i = 0; i < payload_data_sz; ++i) {
        putchar(rx_sector_buf.data[i]);
    }

    rx_prev_sector_num = rx_sector_buf.sector_num;

    if (rx_sector_buf.reserved0 & SECTOR_FLAG_EOF) {
        printf("\nEOF\n");
        return -1;
    }

    return 0;
}

// sector reader callback
readloop_state_t sector_demod_reader(readloop_state_t state, uint32_t bits)
{
    if (state == TS_RESYNC) {
        if (bits  == SYNC) {
            rx_fec_index = 0;
            //printf("\nSYNC %08x\n", bits); // printf is slow, can ruin sync
            inverted = 0x0;
            prev_level = 1;
            return TS_READ;
        }
        else if (~bits == SYNC) {
            rx_fec_index = 0;
            //printf("\nSYNC INV %08x\n", bits);
            inverted = 0xffffffff;
            prev_level = 0;
            return TS_READ;
        }

    }
    else if (state == TS_READ) {
        uint8_t c1, c2;
        demodulate(bits ^ inverted, &c1, &c2, &prev_level);
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
    readloop_delaylocked(sector_demod_reader);
    //readloop_simple(sector_demod_reader);
    //readloop_naiive(sector_demod_reader);

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

Bitstream::Bitstream(int gpio_rdhead, int gpio_wrhead, int gpio_wren)
    : gpio_rdhead(gpio_rdhead), gpio_wrhead(gpio_wrhead), gpio_wren(gpio_wren),
    initialized(false)
{
}

void Bitstream::init()
{
    if (!initialized) {
        // load pio programs
        // tx program loads 32-bit words and sends them 1 bit at a time every 8 clock cycles
        this->offset_tx = pio_add_program(pio, &bitstream_tx_program);
        printf("Transmit program loaded at %d\n", offset_tx);

        // rx program samples input on every clock cycle and outputs a word
        this->offset_rx = pio_add_program(pio, &bitstream_rx_program);
        printf("Receive program loaded at %d\n", offset_rx);

        // calculate clkdiv to match desired MOD_FREQ
        constexpr float clkdiv = 125e6/(MOD_FREQ * 2 * MOD_HALFPERIOD);

        bitstream_tx_program_init(pio, sm_tx, offset_tx, gpio_wrhead, clkdiv);
        bitstream_rx_program_init(pio, sm_rx, offset_rx, gpio_rdhead, clkdiv);

        gpio_init(this->gpio_wren);
        gpio_put(this->gpio_wren, 0); // 0 = read
        gpio_set_dir(this->gpio_wren, GPIO_OUT);

        initialized = true;
    }
}

void Bitstream::deinit()
{
    if (initialized) {
        // shut down PIO
        pio_sm_set_enabled(pio, sm_tx, false);
        pio_sm_set_enabled(pio, sm_rx, false);
        pio_remove_program(pio, &bitstream_tx_program, offset_tx);
        pio_remove_program(pio, &bitstream_rx_program, offset_rx);
        pio_clear_instruction_memory(pio);

        initialized = false;
    }
}

// switch on write head
void Bitstream::write_enable(bool enable)
{
    printf("write_enable: gpio_wren %d=%d\n", this->gpio_wren, enable ? 1 : 0);
    gpio_put(this->gpio_wren, enable ? 1 : 0);
}

void Bitstream::test(int mode)
{
    sanity_check();
    insanity_check();

    readloop_setparams(
            {
            .bitwidth = MOD_HALFPERIOD,
            .Kp = 0.0333,
            .Ki = 0.000001,
            .alpha = 0.1,
            .sampler = bitsampler_pio
            });

    // sectors
    SectorWrite sectors;

    init(); // hardware up

    if (mode & BS_TX) pio_sm_set_enabled(pio, sm_tx, true);
    if (mode & BS_RX) pio_sm_set_enabled(pio, sm_rx, true);

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
        write_enable(true);
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
                for (int i = 0; i < 256; ++i) {
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
            uint8_t mfm_cur_level = 1, mfm_prev_bit = 1;

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

    write_enable(false);

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

    if (mode & BS_RX) {
        correct_reed_solomon_destroy(rs_rx);
    }
}

void
Bitstream::test_sector_rewrite()
{
    init();
    write_enable(true);
    sleep_ms(1000);
    write_enable(false);
    return;

    sanity_check();
    insanity_check();

    constexpr int edit_sector_num = 5;

    printf("will attempt to rewrite sector %d, tape should be rewound and moving\n",
            edit_sector_num);
    sleep_ms(100);

    readloop_setparams(
            {
            .bitwidth = MOD_HALFPERIOD,
            .Kp = 0.0333,
            .Ki = 0.000001,
            .alpha = 0.1,
            .sampler = bitsampler_pio
            });

    // sectors
    SectorWrite sectors;
    init();

    pio_sm_set_enabled(pio, sm_tx, true);
    pio_sm_set_enabled(pio, sm_rx, true);

    printf("Will edit/rewrite sector 5\n");

    // load updated data (1 sector)
    size_t edittext_sz = get_edittext_size();
    const unsigned char * edittext = get_edittext();

    rs_rx = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
            1, 1, fec_min_distance);

    pio_sm_clear_fifos(pio, sm_rx);
    pio_sm_clear_fifos(pio, sm_tx);

    multicore_launch_core1(core1_entry);
    sleep_ms(10);

    // prepare write data
    sectors.set_file_id("alicetxt");
    printf("edittext (%d):\n%s\n", edittext_sz, edittext);
    sleep_ms(100);
    sectors.prepare(edittext, 210, edit_sector_num);

    printf("entering seek loop\n");

    uint32_t out;
    for(;;) {
        out = 0;
        multicore_fifo_pop_timeout_us(100000, &out); 
        if (out == TS_TERMINATE) {
            printf("reader requested termination\n");
            break;
        }
        if (out == MSG_SECTOR_READ_DONE) {
            printf("SECTOR_READ_DONE %d\n", rx_sector_buf.sector_num);
            if (rx_sector_buf.sector_num == edit_sector_num - 1) {
                bitsampler_or = 0x80000000;
                printf("found sector %d, will replace the next one\n", rx_sector_buf.sector_num);
                break;
            }
        }
        int c = getchar_timeout_us(0); 
        if (c != PICO_ERROR_TIMEOUT) {
            printf("abort\n");
            return;
        }
    }

    // no time to lose, switch like fuck into write-mode
    write_enable(true);

    // pre-block leader for tuning up the DLL, it also creates a time gap
    // needed by the receiver to decode the block
    for (int i = 0; i < 16; ++i) {
        pio_sm_put_blocking(pio, sm_tx, LEADER);
    }
    // mfm sync word
    pio_sm_put_blocking(pio, sm_tx, SYNC);
    uint8_t mfm_cur_level = 1, mfm_prev_bit = 1;

    for (size_t i = 0; i < sectors.size(); i += 2) {
        uint32_t mfm_encoded = modulate(sectors[i], 
                sectors[i + 1], &mfm_cur_level, &mfm_prev_bit);
        pio_sm_put_blocking(pio, sm_tx, mfm_encoded);
    }

    while (!pio_sm_is_tx_fifo_empty(pio, sm_tx)) putchar('.');
    sleep_ms(4);

    write_enable(false);

    printf("Replaced sector %f\n", edit_sector_num);
    
}
