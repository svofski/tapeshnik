#include <cstdint>
#include <cstdio>

// fm doesn't need prev_bit, but nice to have it for uniformity
uint32_t fm_encode_twobyte(uint8_t c1, uint8_t c2, uint8_t *cur_level, uint8_t *prev_bit)
{
    uint16_t o = (c1 << 8) | c2;
    uint8_t level = *cur_level;
    uint32_t fm = 0;

    for (int i = 0; i < 16; ++i) {
        int clock = 1 - level;          // R (clock always flips)
        int bit = (o >> (15 - i)) & 1;  // 1: R, 0: N
        if (!bit) {
            level = 1 - level;          // flip in recording for easier decoding
        }
        fm = (fm << 1) | clock;
        fm = (fm << 1) | level;
    }
    *cur_level = level;
    return fm;
}

// input is levels, translate into flux reversals
// drop all clock bits from mfm, resulting in two data bytes c1 and c2
void fm_decode_twobyte(uint32_t fm, uint8_t * c1, uint8_t * c2, uint8_t *prev_level)
{
    uint32_t tb = 0;

    uint8_t plevel = *prev_level;

    //printf("FM: %08x: ", fm);
    for (int i = 0; i < 32; ++i) {
        uint32_t newlevel = (fm & (1 << 31)) >> 31;
        uint32_t reversal = newlevel != plevel;
        //printf("%d->%d[%c] ", plevel, newlevel, reversal ? 'R':'N');
        plevel = newlevel;

        // drop clock bits, shift in data bits
        if (i & 1) {
            tb = (tb << 1) | reversal;
        }

        fm <<= 1;
    }
    *prev_level = plevel;

    *c1 = (tb >> 8) & 0xff;
    *c2 = tb & 0xff;
}



// 8 data bits -> 16 mfm-bits, updates cur_level and prev_bit for subsequent calling
// the result is in terms of absolute values, flux reversals implied
uint32_t mfm_encode_twobyte(uint8_t c1, uint8_t c2, uint8_t *cur_level, uint8_t *prev_bit)
{
    uint16_t o = (c1 << 8) | c2;

    uint8_t level = *cur_level;
    uint8_t lastbit = *prev_bit;
    uint32_t mfm = 0;

    for (int i = 0; i < 16; ++i) {
        int clock;
        int bit = (o >> (15 - i)) & 1;

        // 0: RN, 1: NR,  0101: RN NR NN NR
        if (bit == 0) {
            if (lastbit == 1) {
                clock = level;
            }
            else {
                clock = 1 - level;
            }
            level = clock;
        }
        if (bit == 1) { // NR
            clock = level;
            level = 1 - level;
        }

        mfm = (mfm << 1) | clock;
        mfm = (mfm << 1) | level;

        lastbit = bit;
    }

    *prev_bit = lastbit;
    *cur_level = level;

    return mfm;
}


// input is levels, translate into flux reversals
// drop all clock bits from mfm, resulting in two data bytes c1 and c2
void mfm_decode_twobyte(uint32_t mfm, uint8_t * c1, uint8_t * c2, uint8_t *prev_level)
{
    uint32_t tb = 0;

    uint8_t plevel = *prev_level;

    //printf("MFM: %08x: ", mfm);
    for (int i = 0; i < 32; ++i) {
        uint32_t newlevel = (mfm & (1 << 31)) >> 31;
        uint32_t reversal = newlevel != plevel;
        //printf("%d->%d[%c] ", plevel, newlevel, reversal ? 'R':'N');
        plevel = newlevel;

        // drop clock bits
        if (i & 1) {
            tb = (tb << 1) | reversal;
        }

        mfm <<= 1;
    }
    *prev_level = plevel;

    *c1 = (tb >> 8) & 0xff;
    *c2 = tb & 0xff;
}

