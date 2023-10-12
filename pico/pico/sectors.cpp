#include <cstdint>
#include <array>
#include <algorithm>
#include <string>

#include "config.h"
#include "sectors.h"
#include "correct.h"
#include "crc.h"
#include "mfm.h"

#include "pico/multicore.h"

// prepare sector for writing
// returns number of bytes taken from data
size_t
SectorWriter::prepare(const uint8_t * data, size_t data_sz)
{
    size_t use_bytes;
    for (size_t i = 0; i < txbuf.chunks.size(); ++i) {
        use_bytes = std::min(payload_data_sz, data_sz);
        auto & chunk = txbuf.chunks[i];
        std::copy(data, data + use_bytes, chunk.rawbuf.begin());
        if (use_bytes < payload_data_sz) {
            std::fill(chunk.rawbuf.begin() + use_bytes,
                    chunk.rawbuf.begin() + payload_data_sz, 0);
        }
        chunk.p.payload.crc16 = calculate_crc(chunk.p.payload.data, payload_data_sz);

        const uint8_t * as_bytes = reinterpret_cast<const uint8_t *>(&chunk);
        correct_reed_solomon_encode(rs_tx, as_bytes, fec_message_sz,
                chunk.rawbuf.begin());

        data_sz -= use_bytes;
        data += use_bytes;
    }

    return use_bytes;
}

SectorWriter::SectorWriter(sector_data_t& txbuf) : txbuf(txbuf)
{
    rs_tx = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
            1, 1, fec_min_distance);
}

SectorWriter::~SectorWriter()
{
    correct_reed_solomon_destroy(rs_tx);
}

const uint8_t&
SectorWriter::operator[](size_t i)
{
    return txbuf.raw[i];
}

size_t
SectorWriter::size() const
{
    return txbuf.raw.size();
}

uint16_t calculate_crc(uint8_t * data, size_t len)
{
    return MODBUS_CRC16_v3(data, len);
}

SectorReader::SectorReader(sector_data_t& rxbuf, uint8_t * decoded_buf)
    : rxbuf(rxbuf), decoded_buf(decoded_buf)
{
    rs_rx = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
            1, 1, fec_min_distance);
}

SectorReader::~SectorReader()
{
    correct_reed_solomon_destroy(rs_rx);
}

// decodes and verifies data from rx_sector_buf
int SectorReader::correct_sector_data()
{
    // decode all blocks in sector

    int nerrors = 0;

    uint8_t * dst = decoded_buf;
    for (size_t n = 0; n < FEC_BLOCKS_PER_SECTOR; ++n) {
        ssize_t decoded_sz = correct_reed_solomon_decode(rs_rx, 
            /* encoded */         rxbuf.chunks[n].rawbuf.begin(),
            /* encoded_length */  fec_block_length,
            /* msg */             dst);
        if (decoded_sz <= 0) {
            std::copy_n(rxbuf.chunks[n].rawbuf.begin(), payload_data_sz, dst);
            //printf("\n\n--- error ---\n");
            ++nerrors;
        }
        else {
            uint16_t crc = calculate_crc(dst, payload_data_sz);
            if (crc != rxbuf.chunks[n].p.payload.crc16) {
                //printf("\n\n--- crc error ---\n");
                ++nerrors;
            }
        }

        dst += sizeof(chunk_payload_t);
    }

    return nerrors;
}

// Boyer-Moore majority vote
int
SectorReader::pick_sector_num()
{
    int votes = 0, candidate = -1;

    for (size_t i = 0; i < sector_nums.size(); ++i) {
        if (votes == 0) {
            candidate = sector_nums[i];
            votes = 1;
        }
        else {
            if (sector_nums[i] == candidate) {
                ++votes;
            }
            else {
                --votes;
            }
        }
    }

    size_t count = 0;
    for (size_t i = 0; i < sector_nums.size(); ++i) {
        if (sector_nums[i] == candidate) ++count;
    }

    if (count > sector_nums.size() / 2) {
        return candidate;
    }

    return -1;
}

// sector reader callback (core1)
readloop_state_t
SectorReader::readloop_callback(readloop_state_t state, uint32_t bits)
{
    switch (state) {
        // seek sector start
        case TS_RESYNC_SECTOR:
            {
                sector_nums_index = 0;
                if (bits == SYNC_SECTOR) {
                    inverted = 0x0;
                    prev_level = 1;
                    return TS_READ_SECTOR;
                }
                else if (~bits == SYNC_SECTOR) {
                    inverted = 0xffffffff;
                    prev_level = 0;
                    return TS_READ_SECTOR;
                }
            }
            break;
        // read sector number
        case TS_READ_SECTOR:
            {
                uint8_t c1, c2;
                demodulate(bits ^ inverted, &c1, &c2, &prev_level);
                sector_nums[sector_nums_index] = (c1 << 8) | c2;
                if (++sector_nums_index == 3) {
                    sector_number = pick_sector_num();

                    // for (size_t i = 0; i < sector_nums.size(); ++i) {
                    //     printf("X %04x", sector_nums[i]);
                    // }
                    // putchar('\n');

                    multicore_fifo_push_blocking(MSG_SECTOR_FOUND + sector_number);
                    return TS_RESYNC_DATA;
                }
            }
            break;
        // seek data / sector payload start
        case TS_RESYNC_DATA:
            {
                rxbuf_index = 0;
                if (bits  == SYNC_DATA) {
                    inverted = 0x0;
                    prev_level = 1;
                    return TS_READ_DATA;
                }
                else if (~bits == SYNC_DATA) {
                    inverted = 0xffffffff;
                    prev_level = 0;
                    return TS_READ_DATA;
                }
            }
            break;
        // read the meat of the sector
        case TS_READ_DATA:
            {
                uint8_t c1, c2;
                demodulate(bits ^ inverted, &c1, &c2, &prev_level);
                rxbuf.raw[rxbuf_index++] = c1;
                if (rxbuf_index < rxbuf.raw.size()) {
                    rxbuf.raw[rxbuf_index++] = c2;
                }
                if (rxbuf_index < rxbuf.raw.size()) {
                    return TS_READ_DATA;
                }
                else {
#if LOOPBACK_TEST
                    fuckup_sector_data();
#endif
                    int nerrors = correct_sector_data();
                    if (nerrors == 0) {
                        multicore_fifo_push_blocking(MSG_SECTOR_READ_DONE + sector_number);
                    }
                    else {
                        multicore_fifo_push_blocking(MSG_SECTOR_READ_ERROR + sector_number);
                    }
                    return TS_RESYNC_SECTOR;  // seek next sector
                }
            }
            break;
        default:
            break;
    }
    return state;
}

readloop_state_t
SectorReader::readloop_callback_s(readloop_state_t state, uint32_t bits,
        void * instance)
{
    return reinterpret_cast<SectorReader *>(instance)->
        readloop_callback(state, bits);
}
