#pragma once

#include <cstdint>
#include <string>

// error correction
constexpr size_t fec_block_length = 255;    // libcorrect requirement
constexpr size_t fec_min_distance = 32;     // recommended number of parity bytes
constexpr size_t fec_message_sz = fec_block_length - fec_min_distance; // message payload size = 223 bytes

constexpr size_t file_id_sz = 8;

// sector payload size = fec payload size - sector info
constexpr size_t payload_data_sz = fec_message_sz - (1 + 2 + file_id_sz + 2);

constexpr uint8_t SECTOR_FLAG_EOF = 0x80;

// sector layout: 1 + 2 + 8 + 210 + 2 = 223 bytes === "fec_message_sz"
struct sector_layout_t {
    uint8_t   reserved0;    // format version and flags
    uint16_t  sector_num;   // physical sector in the track
    uint8_t   file_id[file_id_sz];   // short name or guid
    uint8_t   data[payload_data_sz];
    uint16_t  crc16;
} __attribute__((packed));

uint16_t calculate_crc(uint8_t * data, size_t len);

// singletonize or make it a proper class
class SectorWrite {
public:
    SectorWrite();
    ~SectorWrite();

    void set_eof();
    void set_file_id(const std::string& name_id);
    size_t prepare(const uint8_t * data, size_t data_sz, uint16_t sector_num);

    const uint8_t& operator[](size_t);
    size_t size() const;
    uint16_t crc16() const;
};
