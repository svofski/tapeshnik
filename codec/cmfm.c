// a prototype DLL and MFM decoder
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "tinywav.h"

#define  Kp     0.093
#define  Ki     0.000137
#define  alpha  0.099

#define TIMEBASE 8

const uint8_t mfm_sync[] = {1,0,0,0,1,0,0,1,0,0,0,1,0,0,1};
const size_t mfm_sync_sz = sizeof(mfm_sync)/sizeof(mfm_sync[0]);

typedef struct _mfmcodec
{
    int prev_bit;
} mfm_codec_t;

void mfmcodec_init(mfm_codec_t * codec)
{
    codec->prev_bit = 0;
}

void mfmcodec_decode(mfm_codec_t * codec, uint8_t * mfm_bits, size_t mfm_sz,
        uint8_t * bytes)
{
    // discard all clock bits (even)
    for (unsigned i = 1, nbyte = 0; i < mfm_sz;) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; ++j, i += 2) {
            //if (i < 100) printf("%d", mfm_bits[i]);
            byte = (byte << 1) | mfm_bits[i];
        }
        bytes[nbyte++] = byte;
    }
}

void from_nrzi(uint8_t * nrzi, size_t sz, uint8_t * bstream)
{
    for (int i = 1; i < sz; ++i) {
        bstream[i - 1] = nrzi[i - 1] != nrzi[i];
    }
    bstream[sz - 1] = 0;
}

// return index of the first bit after sync
size_t mfmcodec_scan_sync(uint8_t * bstream, size_t sz, size_t first)
{
    for (size_t n = first; n < sz; ++n) {
        if (n >= 5134 - mfm_sync_sz && n < 5134) printf("%d", bstream[n]);
        unsigned i;
        for (i = 0; i < mfm_sync_sz && i + n < sz && bstream[n + i] == mfm_sync[i];
                ++i);
        //if (n > 5110 && n < 5150) {
        //    printf("n=%d i=%d\n", n, i);
        //}
        if (i == mfm_sync_sz) {
            return n + mfm_sync_sz;
        }
    }

    return sz;
}

void sample_bits(uint8_t * bits, size_t bits_sz, uint8_t * sampled_bits,
        size_t * sampled_bits_sz)
{
    int last_acc = 0;
    int lastbit = 0;
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
    int ftw0 = acc_size / (TIMEBASE/2) * scale;
    int ftw = 0;
    int iacc_size = acc_size * scale;
    int iacc = iacc_size / 2;

    *sampled_bits_sz = 0;
    
    for (int i = 0; i < bits_sz; ++i) {
        int bit = bits[i];
        if (bit != lastbit) { // input transition
            phase_delta = iacc_size / 2 - iacc;  // 180 deg off transition point
        }
        //if (iacc < last_acc) { // phase accumulator has wrapped around
        //    sampled_bits[(*sampled_bits_sz)++] = bit;
        //}
        //last_acc = iacc;

        int64_t tmp64 = (int64_t)phase_delta * ialpha;
        //printf("%ld\n", tmp64>>32);
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
        //iacc = (iacc + ftw) % iacc_size;
        iacc = iacc + ftw;
        if (iacc >= iacc_size) {
            iacc -= iacc_size;
            sampled_bits[(*sampled_bits_sz)++] = bit;
        }
    }
}

void compare_message(uint8_t * message, size_t sz, const char * ref_file)
{
    FILE * fref = fopen(ref_file, "r");
    if (fref == NULL) {
        fprintf(stderr, "could not open %s, no BER\n", ref_file);
    }
    int nerrors = 0;
    for (int i = 0; i < sz; ++i) { 
        int c = fgetc(fref);
        if (c < 0) {
            printf("breaking out early\n");
            break;
        }
        if (c != message[i]) {
            ++nerrors;
            printf("%d: %c-%c\n", i, c, message[i]);
        }
    }
    fclose(fref);
    printf("nerrors=%d BER: %f\n", nerrors, (float)nerrors / sz * 100.0);
}

int main(int argc, char *argv[])
{
    TinyWav tw;
    if (argc > 1) {
        if (tinywav_open_read(&tw, argv[1], TW_INTERLEAVED) != 0) {
            fprintf(stderr, "bad wav arg\n");
            return 1;
        }

        size_t sz = tw.numChannels * tw.numFramesInHeader;
        float  * samples = (float *)malloc(sz * sizeof(float));
        int frames_read = tinywav_read_f(&tw, samples, tw.numFramesInHeader);
        printf("read %d samples from wav\n", frames_read);

        uint8_t *bytes = (uint8_t *)malloc(sz);
        for (int i = 0; i < sz; ++i) {
            bytes[i] = samples[i] < 0 ? 0 : 1;
        }
        free(samples);

        uint8_t * sampled_bits = (uint8_t *)malloc(sz);
        size_t nsampled_bits;
        sample_bits(bytes, sz, sampled_bits, &nsampled_bits);
        free(bytes);

        printf("sampled %lu bits\n", nsampled_bits);
        for (int i = 0; i < 100; ++i) printf("%d", sampled_bits[i]); printf("\n---\n");
        uint8_t * mfmbits = (uint8_t *)malloc(nsampled_bits);
        from_nrzi(sampled_bits, nsampled_bits, mfmbits);
        free(sampled_bits);

        for (int i = 0; i < 100; ++i) printf("%d", mfmbits[i]); printf("\n");

        size_t first = mfmcodec_scan_sync(mfmbits, nsampled_bits, 0);
        printf("found sync at %lu\n", first);

        size_t nbytes = nsampled_bits/16;
        uint8_t * decoded_bytes = (uint8_t *)malloc(nbytes);
        mfmcodec_decode(0, mfmbits + first, nsampled_bits - first, decoded_bytes);

        printf("DECODED MESSAGE:\n%s\n", decoded_bytes);
        compare_message(decoded_bytes, nbytes, "test.txt");

        free(mfmbits);
    } 
    else {
        fprintf(stderr, "usage: %s input.wav\n", argv[0]);
        return 1;
    }
}
