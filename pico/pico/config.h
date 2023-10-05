#pragma once

/* time in milliseconds before motor turns off after STOP */
#define MOTOR_OFF_DELAY 1500


#define GPIO_EBOOST 6 // 1 = enable 12V boost converter

//     ___ 
// ___/ 0 \_____
#define GPIO_MODE_ENTRY 22
#define GPIO_MOTOR_CONTROL 21
#define GPIO_SOLENOID_CONTROL 20

#define GPIO_WRHEAD 16  // pi pico pin 21, green wire
#define GPIO_RDHEAD 17  // pi pico pin 22, yellow wire
#define GPIO_WREN   18  // 1 = write, 0 = read

#define GPIO_TACHO 19

#define MOD_HALFPERIOD  8     // number of clocks per half-period in modulation
                              // also hardcoded in bitstream.pio !
#define MOD_FREQ        7000  // max bit flipping frequency
                              // mfm: 9600 almost works but has trouble syncing
                              //      3300 is rock solid
                              //      6600 seems solid
                              //      7000 is solid with A1 sync
                              //      8000 feels good until it isn't
                              // fm:  6600 ok
#define SOLENOID_PULSE_MS 25

#define GPIO_READ_LED   8
#define GPIO_WRITE_LED  7
#define GPIO_ACT_LED    9

#define BOT_LEADER_LEN      256
#define SECTOR_LEADER_LEN   8
#define DATA_LEADER_LEN     8
#define SECTOR_TRAILER_LEN  8

#define CODEC_MFM 1

#ifdef CODEC_MFM
#define modulate    mfm_encode_twobyte
#define demodulate  mfm_decode_twobyte
#endif

#ifdef CODEC_FM
#define modulate    fm_encode_twobyte
#define demodulate  fm_decode_twobyte
#endif

// not modulated, these words are written bit by bit
#ifdef CODEC_FM
constexpr uint32_t LEADER = 0xAAAAAAAA;
constexpr uint32_t SYNC   = 0xAAA1A1A1;
#endif

#ifdef CODEC_MFM
constexpr uint32_t LEADER       = 0xCCCCCCCC;
constexpr uint32_t SYNC_SECTOR  = 0xCCCCCCC7;
constexpr uint32_t SYNC_DATA    = 0xCCCCCCE3;
#endif

#define SECTOR_NUM_REPEATS  4

constexpr uint32_t FEC_BLOCKS_PER_SECTOR = 4;
