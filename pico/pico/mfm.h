#pragma once

#include <cstdint>

// 8 data bits -> 16 mfm-bits, updates prev_bit for subsequent calling
uint32_t mfm_encode_twobyte(uint8_t c1, uint8_t c2, uint8_t *prev_bit);

// take only data bits from mfm encoding two bytes, put in c1, c2
void mfm_decode_twobyte(uint32_t mfm, uint8_t * c1, uint8_t * c2);
