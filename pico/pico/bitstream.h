#pragma once

#include <cstdint>
#include "correct.h"

// for testing
constexpr int BS_TX = 1;
constexpr int BS_RX = 2;

void bitstream_test(int mode = BS_TX | BS_RX);


class Bitstream {
private:
    int gpio_rdhead;
    int gpio_wrhead;
    int gpio_wren;
    int gpio_read_led;
    int gpio_write_led;
    bool initialized;
    uint offset_tx, offset_rx;

    // switch to write mode
    void write_enable(bool enable);
public:
    Bitstream(int gpio_rdhead, int gpio_wrhead, int gpio_wren, int gpio_read_led,
            int gpio_write_led)
      : gpio_rdhead(gpio_rdhead), gpio_wrhead(gpio_wrhead), gpio_wren(gpio_wren), 
        gpio_read_led(gpio_read_led), gpio_write_led(gpio_write_led),
        initialized(false) {}
    ~Bitstream();

    void init();   // prepare hardware and algorithms
    void deinit(); 


    void test(int mode = BS_TX | BS_RX); 
    void test_sector_rewrite();
};
