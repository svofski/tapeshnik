#include <cstdint>
#include <array>
#include <algorithm>
#include <string>

#include "sectors.h"
#include "correct.h"
#include "crc.h"

// reed-solomon instances for writing and reading
static correct_reed_solomon * rs_tx = 0;

// sector buffer without fec bytes
static sector_layout_t tx_sector_buf;

// todo: join with the buffer above, encode can work with that

// tx buffer for feck (+1 pad to 256 for easier encoding)
static std::array<uint8_t, fec_block_length + 1> tx_fec_buf;

// prepare sector for writing
// returns number of bytes taken from data
size_t
SectorWrite::prepare(const uint8_t * data, size_t data_sz, uint16_t sector_num)
{
    tx_sector_buf.sector_num = sector_num;

    // sector payload
    size_t use_bytes = std::min(payload_data_sz, data_sz);
    std::copy(data, data + use_bytes, tx_sector_buf.data);

    // make sure that block remainder is empty
    std::fill(tx_sector_buf.data + data_sz, tx_sector_buf.data + payload_data_sz, 0);

    // update crc
    tx_sector_buf.crc16 = calculate_crc(tx_sector_buf.data, payload_data_sz);

    // add parity and return
    const uint8_t * as_bytes = reinterpret_cast<const uint8_t *>(&tx_sector_buf);
    correct_reed_solomon_encode(rs_tx, as_bytes, fec_message_sz, tx_fec_buf.begin());

    return use_bytes;
}

void 
SectorWrite::set_file_id(const std::string& name_id)
{
    tx_sector_buf = {};
    std::copy_n(name_id.begin(), file_id_sz, &tx_sector_buf.file_id[0]);
}

void 
SectorWrite::set_eof()
{
    tx_sector_buf.reserved0 = SECTOR_FLAG_EOF;
}

SectorWrite::SectorWrite()
{
    rs_tx = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
            1, 1, fec_min_distance);
}

SectorWrite::~SectorWrite()
{
    correct_reed_solomon_destroy(rs_tx);
}

const uint8_t&
SectorWrite::operator[](size_t i)
{
    return tx_fec_buf[i];
}

size_t
SectorWrite::size() const
{
    return tx_fec_buf.size();
}

uint16_t 
SectorWrite::crc16() const
{
    return tx_sector_buf.crc16;
}


uint16_t calculate_crc(uint8_t * data, size_t len)
{
    return MODBUS_CRC16_v3(data, len);
}
