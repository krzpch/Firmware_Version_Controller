#ifndef FVC_HASH_H
#define FVC_HASH_H

#include <stdint.h>
#include <stdio.h>

uint32_t fvc_calc_crc(uint32_t hash_in, uint8_t *data, size_t data_len);

#endif
