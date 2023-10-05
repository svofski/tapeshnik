#pragma once

#include <cstdint>
#include <string>
#include <array>

#include "config.h"
#include "correct.h"
#include "readloop.h"

// error correction
constexpr size_t fec_block_length = 255;    // libcorrect requirement
constexpr size_t fec_min_distance = 32;     // recommended number of parity bytes
constexpr size_t fec_message_sz = fec_block_length - fec_min_distance; // message payload size = 223 bytes

// sector payload size = fec payload size - sector info
constexpr size_t payload_data_sz = fec_message_sz - /* crc16 */ 2;

struct chunk_payload_t {
    uint8_t   data[payload_data_sz];
    uint16_t  crc16;
} __attribute__((packed));;

union full_chunk_t {
    struct _ {
        chunk_payload_t payload;
        std::array<uint8_t, fec_min_distance + 1> parity;
    } p;
    std::array<uint8_t, fec_block_length + 1> rawbuf;
} __attribute__((packed));

union sector_data_t {
    std::array<full_chunk_t, FEC_BLOCKS_PER_SECTOR> chunks;
    std::array<uint8_t, sizeof(full_chunk_t) * FEC_BLOCKS_PER_SECTOR> raw;
} __attribute__((packed));

//constexpr size_t sector_data_sz = payload_data_sz * 4;

constexpr size_t sector_payload_sz = payload_data_sz * FEC_BLOCKS_PER_SECTOR;

uint16_t calculate_crc(uint8_t * data, size_t len);

// singletonize or make it a proper class
class SectorWriter {
private:
    // reed-solomon instances for writing and reading
    correct_reed_solomon * rs_tx = 0;
    
    // sector buffer without fec bytes
    sector_data_t& txbuf;
public:
    SectorWriter(sector_data_t& txbuf);
    ~SectorWriter();

    size_t prepare(const uint8_t * data, size_t data_sz);

    const uint8_t& operator[](size_t);
    size_t size() const;
    //uint16_t crc16() const;
};

class SectorReader {
private:
    correct_reed_solomon * rs_rx = 0;
    sector_data_t& rxbuf;
    size_t rxbuf_index;
    uint8_t prev_level;
    uint32_t inverted = 0;


    std::array<uint16_t, 3> sector_nums;
    size_t sector_nums_index;

    int pick_sector_num();
public:
    int sector_number;

    SectorReader(sector_data_t& rxbuf);
    ~SectorReader();

    int correct_sector_data();
    readloop_state_t readloop_callback(readloop_state_t state, uint32_t bits);
    static readloop_state_t readloop_callback_s(readloop_state_t state, 
            uint32_t bits, void * instance);
};
