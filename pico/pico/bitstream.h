#pragma once

#include "correct.h"

// for testing
constexpr int BS_TX = 1;
constexpr int BS_RX = 2;

void bitstream_test(int mode = BS_TX | BS_RX);
