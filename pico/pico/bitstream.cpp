#include <cstdio>
#include <cstdint>
#include <array>
#include <algorithm>
#include <string>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "bitstream.pio.h"

#include "bitstream.h"
#include "readloop.h"
#include "sectors.h"
#include "tacho.h"

#include "config.h"

#include "correct.h"
#include "crc.h"
#include "mfm.h"
#include "util.h"

const unsigned char * get_plaintext();
size_t get_plaintext_size();
const unsigned char * get_edittext();
size_t get_edittext_size();


// print sector payload as plain text in correct_sector_data()
volatile bool enable_print_sector_text = false;
volatile bool enable_print_sector_info = false;

PIO pio = pio0;
uint sm_tx = 0, sm_rx = 1;

int64_t start_time, end_time;


//std::array<uint8_t, sector_data_sz> sector_buf;
sector_data_t sector_buf;
std::array<uint8_t, sector_payload_sz> decoded_buf;

// will it make it to flash?
constexpr std::array<uint8_t, sector_payload_sz> zero_payload{};

// rs decoded sector payload
uint32_t bitsampler_or = 0;

SectorReader * core1_reader;

uint32_t bitsampler_pio()
{
    return bitsampler_or | pio_sm_get_blocking(pio, sm_rx); // take next sample
}


int count_errors(uint8_t * uncorrected, uint8_t * corrected, size_t sz)
{
    int result = 0;

    for (size_t i = 0; i < sz; ++i) {
        //printf("%c==%c\n", uncorrected[i], corrected[i]);
        result += uncorrected[i] != corrected[i];
    }

    return result;
}


// core1 runs independently and pretends not to know anything
// about received data
void core1_entry()
{
    printf("core1_entry\n");
    readloop_delaylocked(SectorReader::readloop_callback_s, core1_reader);

    multicore_fifo_push_blocking(TS_TERMINATE);
}

void sanity_check()
{
    printf("sanity_check (mfm): ");
    uint8_t cur_level = 0, prev_bit = 0;
    uint32_t mfmbits = mfm_encode_twobyte(0x55, 0xa1, &cur_level, &prev_bit);
    for (int i = 0; i < 32; ++i) {
        putchar('0' + ((mfmbits >> (31-i)) & 1));
    }
    printf(" %08x %08x    ", mfmbits, ~mfmbits);

    uint8_t c1, c2;
    uint8_t prev_level = 0;
    mfm_decode_twobyte(mfmbits, &c1, &c2, &prev_level);
    printf("decoded: %02x %02x\n", c1, c2);
}

void insanity_check()
{
    printf("insanity_check (fm): ");
    uint8_t cur_level = 0, prev_bit = 0;
    uint32_t fmbits = fm_encode_twobyte(0x55, 0xa1, &cur_level, &prev_bit);
    for (int i = 0; i < 32; ++i) {
        putchar('0' + ((fmbits >> (31-i)) & 1));
    }
    printf(" %08x %08x    ", fmbits, ~fmbits);

    uint8_t c1, c2;
    uint8_t prev_level = 0;
    fm_decode_twobyte(fmbits, &c1, &c2, &prev_level);
    printf("decoded: %02x %02x\n", c1, c2);
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

        uint32_t f_cpu = clock_get_hz(clk_sys);

        // calculate clkdiv to match desired MOD_FREQ
        // Fpio = MOD_FREQ * 2 * MOD_HALFPERIOD
        // Fsmp = Fpio / (8 / 2), e.g. 56000 for MOD_FREQ = 7000 for raw import
        const float clkdiv = (float)f_cpu/(MOD_FREQ * 2 * MOD_HALFPERIOD);

        printf("CPU frequency: %d clkdiv=%f\n", f_cpu, clkdiv);

        //printf("TESTING GPIO_WRHEAD\n");
        //gpio_init(this->gpio_wrhead);
        //gpio_set_dir(this->gpio_wrhead, GPIO_OUT);
        //gpio_put(this->gpio_wrhead, 1);
        //sleep_ms(1000);
        //gpio_put(this->gpio_wrhead, 0);
        //printf("DONE\n");


        bitstream_tx_program_init(pio, sm_tx, offset_tx, gpio_wrhead, clkdiv);
        bitstream_rx_program_init(pio, sm_rx, offset_rx, gpio_rdhead, clkdiv);

        gpio_init(this->gpio_wren);
        gpio_put(this->gpio_wren, 0); // 0 = read
        gpio_set_dir(this->gpio_wren, GPIO_OUT);

        gpio_init(this->gpio_write_led);
        gpio_put(this->gpio_write_led, 0);
        gpio_set_dir(this->gpio_write_led, GPIO_OUT);

        gpio_init(this->gpio_read_led);
        gpio_put(this->gpio_read_led, 0);
        gpio_set_dir(this->gpio_read_led, GPIO_OUT);

        initialized = true;
    }

    bitsampler_or = 0;
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
    //printf("write_enable: gpio_wren %d=%d\n", this->gpio_wren, enable ? 1 : 0);
    gpio_put(this->gpio_wren, enable ? 1 : 0);
    gpio_put(this->gpio_write_led, enable ? 1 : 0);
}

void Bitstream::read_led(bool on)
{
    gpio_put(this->gpio_read_led, on ? 1 : 0);
}

void Bitstream::write_bot()
{
    for (size_t i = 0; i < BOT_LEADER_LEN; ++i) {
        pio_sm_put_blocking(pio, sm_tx, LEADER);
        pio_sm_put_blocking(pio, sm_tx, LEADER);
        pio_sm_put_blocking(pio, sm_tx, LEADER);
        pio_sm_put_blocking(pio, sm_tx, LEADER);
    }
}

void Bitstream::write_sector_data(SectorWriter& writer, const uint8_t * data,
        size_t data_sz)
{
    // copy source data to sector buffer and compute parity
    writer.prepare(data, data_sz);

    // data leader
    for (size_t i = 0; i < DATA_LEADER_LEN; ++i) {
        pio_sm_put_blocking(pio, sm_tx, LEADER);
    }
    // data sync E3
    pio_sm_put_blocking(pio, sm_tx, SYNC_DATA);

    // the meat of the sector
    uint8_t mfm_cur_level = 1, mfm_prev_bit = 1;
    for (size_t i = 0; i < writer.size(); i += 2) {
        uint32_t mfm_encoded = modulate(writer[i], writer[i + 1],
                &mfm_cur_level, &mfm_prev_bit);
        pio_sm_put_blocking(pio, sm_tx, mfm_encoded);
    }

    for (size_t i = 0; i < SECTOR_TRAILER_LEN; ++i) {
        pio_sm_put_blocking(pio, sm_tx, LEADER);
    }
}

void Bitstream::llformat()
{
    extern volatile int mainloop_request;

    init();
    pio_sm_set_enabled(pio, sm_tx, true);

    sanity_check();
    insanity_check();

    SectorWriter writer(sector_buf);

    printf("Low-level format. Press shift-y to continue, any other key to abort: ");
    if (getchar() != 'Y') {
        printf("N\n");
        return;
    }

    mainloop_request = 0;
    wheel.rew();
    sleep_ms(500);
    while (1) {
        putchar('>');
        sleep_ms(100);
        tight_loop_contents();
        if (mainloop_request == ' ') {
            wheel.stop();
            sleep_ms(250);
            break;
        }
    }

    printf("Press any key to abort...\n");
    tacho_set_counter(0);
    wheel.play();
    //sleep_ms(5000);

    // 6 seconds of plastic leader
    for (int i = 0; i < 6000; ++i) {
        int c = getchar_timeout_us(1000); 
        if (c != PICO_ERROR_TIMEOUT) {
            printf("aborted\n");
            wheel.stop();
            return;
        }
    }

    write_enable(true);
    write_bot();

    for (uint16_t sector_num = 0; ; ++sector_num) {
        printf("Counter: %d Sector: %d\n", tacho_get_counter(),
                sector_num);

        // sector leader
        for (size_t i = 0; i < SECTOR_LEADER_LEN; ++i) {
            pio_sm_put_blocking(pio, sm_tx, LEADER);
        }
        // sector sync C7
        pio_sm_put_blocking(pio, sm_tx, SYNC_SECTOR);

        // 16-bit sector number repeated SECTOR_NUM_REPEATS times
        uint8_t mfm_cur_level = 1, mfm_prev_bit = 1;
        uint32_t mfm_encoded = modulate(sector_num >> 8, sector_num & 255,
                &mfm_cur_level, &mfm_prev_bit);
        for (size_t i = 0; i < SECTOR_NUM_REPEATS; ++i) {
            pio_sm_put_blocking(pio, sm_tx, mfm_encoded);
        }

        write_sector_data(writer, zero_payload.begin(), zero_payload.size());

        // check end conditions
        if (wheel.get_position() != WP_PLAY) {
            printf("EOT\n");
            break;
        }

        int c = getchar_timeout_us(0); 
        if (c != PICO_ERROR_TIMEOUT) {
            printf("abort\n");
            break;
        }
    }

    write_enable(false);

    deinit();
}

void sizeof_checks()
{
    printf("chunk_payload_t: %d\n", sizeof(chunk_payload_t));
    printf("chunk_data_t: %d\n", sizeof(chunk_data_t));
    printf("full_chunk_t: %d\n", sizeof(full_chunk_t));
    printf("sector_data_t: %d\n", sizeof(sector_data_t));
    printf("sector_payload_sz: %d\n", sector_payload_sz);
}

void Bitstream::sector_scan(uint16_t sector_num)
{
    printf("sector_scan(%d):\n", sector_num);

    sizeof_checks();

    SectorReader reader(sector_buf, decoded_buf.begin());
    init();

    pio_sm_set_enabled(pio, sm_rx, true);
    pio_sm_clear_fifos(pio, sm_rx);
    read_led(true);

    readloop_setparams(
            {
            .bitwidth = MOD_HALFPERIOD,
            .Kp = 0.0333,
            .Ki = 0.000001,
            .alpha = 0.1,
            .sampler = bitsampler_pio
            });

    core1_reader = &reader;
    multicore_launch_core1(core1_entry);
    sleep_ms(10);

    uint32_t out;
    int c;
    for(int i = 0; i < 45*60*10; ++i) {
        if (multicore_fifo_pop_timeout_us(100000ULL, &out)) {
            if (out == TS_TERMINATE) {
                break;
            }
            else if ((out & 0xffff0000) == MSG_SECTOR_FOUND) {
                uint16_t found_num = out & 0xffff;
                printf("found: %d\n", found_num);
            }
            else if ((out & 0xffff0000) == MSG_SECTOR_READ_DONE) {
                uint16_t found_num = out & 0xffff;
                printf("read_done: %d\n", found_num);

                printf("raw:\n");
                for (int n = 0; n < 4; ++n) {
                    printf("chunk %d\n", n);
                    for (size_t i = 0; i < sector_buf.chunks[n].rawbuf.size(); ++i) {
                        printf("%02x ", sector_buf.chunks[n].rawbuf[i]);
                    }
                }
                printf("decoded:\n");
                for (size_t i = 0; i < decoded_buf.size(); ++i) {
                    //putchar(decoded_buf[i]);
                    printf("%02x ", decoded_buf[i]);
                }
                putchar('\n');
            }
        }
        c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            break;
        }        
    }
    if (c == 'd') {
        readloop_dump_debugbuf();
    }

    read_led(false);
    multicore_reset_core1();
    deinit();
}
