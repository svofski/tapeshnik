#pragma once

#include <cstdint>

// read loop states
enum readloop_state_t
{
    TS_RESYNC_SECTOR = 0, // sync seeking sector
    TS_READ_SECTOR,       // read sector number
    TS_RESYNC_DATA,       // sync seeking sector data
    TS_READ_DATA,         // read sector data
    TS_TERMINATE,
};

enum readloop_special_t
{
    RL_BREAK = 0x80000000,  // request from bitsampler for read loop to break
};

const int MSG_SECTOR_FOUND =      0x10000; // MSG_SECTOR_FOUND + sector no
const int MSG_SECTOR_READ_DONE =  0x20000; // MSG_SECTOR_FOUND + sector no


// reader callback: receives tracking state and 32 sampled bits, returns new state
// its job is to decode the bits (fm, mfm, whatever) store and decode
typedef readloop_state_t (*readloop_callback_t)(readloop_state_t, uint32_t, void *);

// bit sampler function: return next value 0 or 1
// 0x80000000 for loop termination
typedef uint32_t (*readloop_bit_sampler_t)(void);

struct readloop_params_t {
    int bitwidth;
    float Kp;
    float Ki;
    float alpha;
    readloop_bit_sampler_t sampler;
};

void readloop_setparams(readloop_params_t args);

uint32_t readloop_simple(readloop_callback_t cb, void * user);
uint32_t readloop_naiive(readloop_callback_t cb, void * user);
uint32_t readloop_delaylocked(readloop_callback_t cb, void * user);

void readloop_dump_debugbuf();
