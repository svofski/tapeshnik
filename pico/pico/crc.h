#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t MODBUS_CRC16_v3( const unsigned char *buf, unsigned int len);

#ifdef __cplusplus
}
#endif
