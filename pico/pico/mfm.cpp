#include <cstdint>

// 8 data bits -> 16 mfm-bits, updates prev_bit for subsequent calling
uint32_t mfm_encode_twobyte(uint8_t c1, uint8_t c2, uint8_t *prev_bit)
{
    uint16_t o = (c1 << 8) | c2;

    uint8_t prev = *prev_bit;
    uint32_t mfm = 0;

    for (int i = 0; i < 16; ++i) {
        int clock = 0;
        int bit = (o >> (15 - i)) & 1;
        if (bit == 0) {
            clock = 1 - prev;
        }
        prev = bit;

        mfm = (mfm << 1) | clock;
        mfm = (mfm << 1) | bit;
    }

    *prev_bit = prev;

    return mfm;
}

// drop all clock bits from mfm, resulting in two data bytes c1 and c2
void mfm_decode_twobyte(uint32_t mfm, uint8_t * c1, uint8_t * c2)
{
    uint32_t tb = 0;

    for (int i = 0; i < 16; ++i) {
        // just skip clock bits
        uint32_t bit = (mfm & (1 << 30)) >> 30;
        tb = (tb << 1) | bit;
        mfm <<= 2;
    }
    *c1 = (tb >> 8) & 0xff;
    *c2 = tb & 0xff;
}

