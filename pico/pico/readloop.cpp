#include <cstdint>
#include <cmath>
#include <cstdio>
#include <array>
#include "readloop.h"

static readloop_bit_sampler_t sample_one_bit = 0;

static uint64_t mfm_bits = 0;

static int bitwidth = 0;
static int halfwidth = 0;

static float Kp = 0;
static float Ki = 0;
static float alpha = 0;

static size_t debugbuf_index = 0;
static std::array<uint8_t, 100000> debugbuf;

void readloop_setparams(readloop_params_t args)
{
    bitwidth = args.bitwidth;
    halfwidth = args.bitwidth / 2;

    Kp = args.Kp;
    Ki = args.Ki;
    alpha = args.alpha;

    sample_one_bit = args.sampler;

    debugbuf_index = 0;
}

// delay-locked loop tracker with PI-tuning
// borrows from https://github.com/carrotIndustries/redbook/ by Lukas K.
uint32_t readloop_delaylocked(readloop_callback_t cb, void * user)
{
    uint32_t lastbit = 0;
    int phase_delta = 0;
    int phase_delta_filtered = 0;
    int integ = 0;

    int nscale = 20;
    int scale = 1 << nscale;
    int one = scale;
    int iKp = (int)(Kp * scale);
    int iKi = (int)(Ki * scale);
    int ialpha = (int)(alpha * scale);

    int integ_max = 512 * scale;

    int acc_size = 512;
    int ftw0 = acc_size / bitwidth * scale;
    int ftw = 0;
    int iacc_size = acc_size * scale;
    int iacc = iacc_size / 2;

    int bitcount = 0;
    int rawcnt = 0;   // raw sample count for debugbuffa
    uint32_t rawsample = 0;

    printf("%s, collecting debugbuf\n", __FUNCTION__);
    readloop_state_t state = TS_RESYNC_SECTOR;

    for (; state != TS_TERMINATE;) {
        uint32_t bit = sample_one_bit();
        if (bit & RL_BREAK) {
            break;
        }

        if (debugbuf_index < debugbuf.size()) {
            rawsample = (rawsample << 1) | bit;
            if (++rawcnt == 8) {
                rawcnt = 0;
                debugbuf[debugbuf_index++] = 0xff & rawsample;
            }
        }

        if (bit != lastbit) {                   // input transition
            phase_delta = iacc_size / 2 - iacc; // 180 deg off transition point
        }

        int64_t tmp64 = (int64_t)phase_delta * ialpha;
        tmp64 += (int64_t)phase_delta_filtered * (one - ialpha);
        phase_delta_filtered = tmp64 >> nscale;

        integ += (int64_t)(phase_delta_filtered * iKi) >> nscale;

        if (integ > integ_max) {
            integ = integ_max;
        }
        else if (integ < -integ_max) {
            integ = -integ_max;
        }
        
        ftw = ftw0 + (((int64_t)phase_delta_filtered * iKp) >> nscale) + integ;
        lastbit = bit;
        iacc = iacc + ftw;
        if (iacc >= iacc_size) {
            iacc -= iacc_size;
            mfm_bits = (mfm_bits << 1) | bit;   // sample bit

            switch (state) {
                case TS_RESYNC_SECTOR:
                case TS_RESYNC_DATA:
                    state = cb(state, mfm_bits, user);
                    if (state == TS_READ_SECTOR || state == TS_READ_DATA) {
                        bitcount = 0;
                    }
                    break;
                case TS_READ_SECTOR:
                case TS_READ_DATA:
                    if (++bitcount == 32) {
                        bitcount = 0;
                        state = cb(state, mfm_bits, user);
                    }
                    break;
                case TS_TERMINATE:
                    break;
            }
        }
    }

    return 0;
}


void readloop_dump_debugbuf()
{
    printf("---debug sample begin---\n");
    for (size_t i = 0; i < debugbuf.size(); ++i) {
        printf("%02x ", debugbuf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("---debug sample end---\n");
}
