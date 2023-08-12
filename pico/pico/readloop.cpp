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

// fixed timing reader with callback
uint32_t readloop_simple(readloop_callback_t cb)
{
    uint32_t prev = 0;
    int sample_t = 0;
    int bitcount = 0;

    readloop_state_t state = TS_RESYNC;

    for (; state != TS_TERMINATE;) {
        uint32_t cur = sample_one_bit();
        ++sample_t;
        if (sample_t == bitwidth) {
            sample_t = 0;
        }

        if (state == TS_RESYNC) {
            if (cur != prev) {
                int dist_mid = abs(sample_t) - halfwidth;
                int dist_0 = abs(sample_t);
                int dist_p = abs(bitwidth - sample_t);

                if (dist_0 < dist_mid || dist_p < dist_mid) {
                    sample_t = 0;
                }
                else {
                    sample_t = halfwidth;
                }
            }
        }
        else {
            if (cur != prev) {
                int dist_mid = abs(sample_t) - halfwidth;
                int dist_0 = abs(sample_t);
                int dist_p = abs(bitwidth - sample_t);

                if (dist_0 < dist_mid) {
                    sample_t = std::max(0, sample_t - 1);
                }
                if (dist_p < dist_mid) {
                    sample_t += 1;
                    if (sample_t >= bitwidth) {
                        sample_t -= bitwidth;
                    }
                }
            }
        }

        if (sample_t == halfwidth) {
            // take sample hopefully in the middle 
            mfm_bits = (mfm_bits << 1) | cur;

            switch (state) {
                case TS_RESYNC:
                    state = cb(state, mfm_bits);
                    bitcount = 0;
                    break;
                case TS_READ:
                    if (++bitcount == 32) {
                        bitcount = 0;
                        state = cb(state, mfm_bits);
                    }
                    break;
                case TS_TERMINATE:
                    break;
            }
        }

        prev = cur;
    }

    return 0;
}

// fixed timing reader with callback
uint32_t readloop_naiive(readloop_callback_t cb)
{
    uint32_t prev = 0;
    int sample_t = 0;
    int bitcount = 0;

    readloop_state_t state = TS_RESYNC;

    const int shortpulse = bitwidth;
    for (; state != TS_TERMINATE;) {
        uint32_t cur = sample_one_bit();
        if (cur & 0x80000000) {
            break;
        }

        ++sample_t;

        // flip
        if (cur != prev) {
            // the long pulses are only present in sync
            if (sample_t > 7 * shortpulse / 2) {// 3.5 pulse
                mfm_bits = (mfm_bits << 1) | prev; ++bitcount;
                mfm_bits = (mfm_bits << 1) | prev; ++bitcount;
                mfm_bits = (mfm_bits << 1) | prev; ++bitcount;
            }                                                
            else if (sample_t > 5 * shortpulse / 2) { // 2.5 pulse
                mfm_bits = (mfm_bits << 1) | prev; ++bitcount;
                mfm_bits = (mfm_bits << 1) | prev; ++bitcount;
            }
            else if (sample_t > 3 * shortpulse / 2) { // 1.5 pulse
                mfm_bits = (mfm_bits << 1) | prev; ++bitcount;
            }

            mfm_bits = (mfm_bits << 1) | cur; ++bitcount;

            sample_t = 0;
        }

        switch (state) {
            case TS_RESYNC:
                state = cb(state, mfm_bits);
                if (state == TS_READ) putchar('#');
                bitcount = 0;
                break;
            case TS_READ:
                if (bitcount >= 32) {
                    int s = bitcount - 32;
                    state = cb(state, mfm_bits >> s);
                    bitcount -= 32;
                }
                break;
            case TS_TERMINATE:
                break;
        }

        prev = cur;
    }

    return 0;
}

// delay-locked loop tracker with PI-tuning
// borrows from https://github.com/carrotIndustries/redbook/ by Lukas K.
uint32_t readloop_delaylocked(readloop_callback_t cb)
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
    readloop_state_t state = TS_RESYNC;

    for (; state != TS_TERMINATE;) {
        uint32_t bit = sample_one_bit();

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
                case TS_RESYNC:
                    state = cb(state, mfm_bits);
                    if (state == TS_READ) {
                        bitcount = 0;
                    }
                    break;
                case TS_READ:
                    if (++bitcount == 32) {
                        bitcount = 0;
                        state = cb(state, mfm_bits);
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
