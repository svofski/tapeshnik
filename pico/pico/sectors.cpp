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

    uint8_t * dst = decoded_buf;
    for (size_t n = 0; n < FEC_BLOCKS_PER_SECTOR; ++n) {
        ssize_t decoded_sz = correct_reed_solomon_decode(rs_rx, 
            /* encoded */         rxbuf.chunks[n].rawbuf.begin(),
            /* encoded_length */  fec_block_length,
            /* msg */             dst);
        if (decoded_sz <= 0) {
            std::copy_n(rxbuf.chunks[n].rawbuf.begin(), payload_data_sz, dst);
            printf("\n\n--- error ---\n");
        }
        uint16_t crc = calculate_crc(dst, payload_data_sz);
        if (crc != rxbuf.chunks[n].p.payload.crc16) {
            printf("\n\n--- crc error ---\n");
        }

        dst += sizeof(chunk_payload_t);
    }



#if 0
    // entire rx_sector_buf layout as flat array
    uint8_t * rx_ptr = reinterpret_cast<uint8_t *>(&rx_sector_buf);
    ssize_t decoded_sz = correct_reed_solomon_decode(rs_rx, rx_fec_buf.begin(),
            rx_fec_buf.size(), rx_ptr);
    if (decoded_sz <= 0) {
        std::copy_n(rx_fec_buf.begin(), sizeof(rx_sector_buf), rx_ptr);
        if (enable_print_sector_info) {
            printf("\n\n--- unrecoverable error in sector %d  ---\n", rx_sector_buf.sector_num);
        }
    }
    if (rx_sector_buf.sector_num != rx_prev_sector_num + 1) {
        if (enable_print_sector_info) {
            printf("\n\n--- sector out of sequence; previous: %d current: %d\n\n", 
                    rx_prev_sector_num, rx_sector_buf.sector_num);
        }
    }

    uint16_t crc = calculate_crc(&rx_sector_buf.data[0], payload_data_sz);

    multicore_fifo_push_blocking(MSG_SECTOR_READ_DONE);

    if (enable_print_sector_info) {
        int nerrors = count_errors(&rx_fec_buf[0], rx_ptr, sizeof(rx_sector_buf));
        float ber = 100.0 * nerrors / sizeof(rx_sector_buf);
        if (crc != rx_sector_buf.crc16) {
            ber = 100;
        }

        std::string filename(reinterpret_cast<const char *>(&rx_sector_buf.file_id[0]), file_id_sz);
        set_color(40, 33); // 40 = black bg, 33 = brown fg
        printf("\nsector %d; file: '%s' crc actual: %04x expected: %04x nerrors=%d BER=%3.1f ", 
                rx_sector_buf.sector_num,
                filename.c_str(),
                crc, rx_sector_buf.crc16,
                nerrors, ber);
        if (crc == rx_sector_buf.crc16) {
            printf("OK");
        }
        else {
            set_color(41, 37);
            printf("ERROR");
        }
        reset_color();
        putchar('\n');
    }

    if (enable_print_sector_text) {
        for (size_t i = 0; i < payload_data_sz; ++i) {
            putchar(rx_sector_buf.data[i]);
        }
    }

    rx_prev_sector_num = rx_sector_buf.sector_num;

    if (rx_sector_buf.reserved0 & SECTOR_FLAG_EOF) {
        print_color(45, 33, "EOF", "\n");
        return -1;
    }
#endif
    return 0;
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
                    int res = correct_sector_data();
                    multicore_fifo_push_blocking(MSG_SECTOR_READ_DONE + sector_number);
                    if (res == -1) {
                        return TS_TERMINATE;
                    }
                    else {
                        return TS_RESYNC_SECTOR;  // seek next sector
                    }
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
