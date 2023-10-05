#pragma once

#include <cstdint>
#include "wheel.h"
#include "correct.h"
#include "sectors.h"

// for testing
constexpr int BS_TX = 1;
constexpr int BS_RX = 2;

void bitstream_test(int mode = BS_TX | BS_RX);

class Bitstream {
private:
    Wheel wheel;
    int gpio_rdhead;
    int gpio_wrhead;
    int gpio_wren;
    int gpio_read_led;
    int gpio_write_led;
    bool initialized;
    uint offset_tx, offset_rx;

    // switch to write mode
    void write_enable(bool enable);
    void read_led(bool on);

    void write_bot();

    // write the inner part of a sector: LEADER, DATA SYNC, DATA + PARITY
    void write_sector_data(SectorWriter & writer, const uint8_t * data, size_t data_sz);

public:
    Bitstream(Wheel & wheel, int gpio_rdhead, int gpio_wrhead, int gpio_wren,
            int gpio_read_led, int gpio_write_led)
      : wheel(wheel), gpio_rdhead(gpio_rdhead), gpio_wrhead(gpio_wrhead),
        gpio_wren(gpio_wren), gpio_read_led(gpio_read_led),
        gpio_write_led(gpio_write_led), initialized(false) {}
    ~Bitstream();

    void init();   // prepare hardware and algorithms
    void deinit(); 


    //void test(int mode = BS_TX | BS_RX); 
    //void test_sector_rewrite();
    void llformat();

    void sector_scan(uint16_t sector_num);
};
