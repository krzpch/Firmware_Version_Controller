#ifndef FVC_HASH_H
#define FVC_HASH_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/**
 * @brief Calcualtes 32 bit crc
 * @param [in] hash_in - starting value of crc
 * @param [in] data - data for crc calculations
 * @param [in] data_len - length of input data
 * @return new value of crc based on input crc and data
 */
uint32_t fvc_calc_crc(uint32_t hash_in, uint8_t *data, size_t data_len);

/**
 * @brief Calculates HMAC-SHA256 hash
 * @param [in] data - pointer to data to be hashed
 * @param [in] data_len - length of data
 * @param [in] key - pointer to key
 * @param [in] key_len - length of key
 * @param [out] hash_out - pointer to caLculated hash buffer (must be 32 bytes)
 * @note this function can only be used on buffered data
 */
void fvc_calc_hmac_sha256(uint8_t *data, size_t data_len, uint8_t *key, size_t key_len, uint8_t *hash_out);

/**
 * @brief Initialize HMAC-SHA256 hash calculator
 * @param [in] key - pointer to key
 * @param [in] key_len - length of key
 * @note this functions set can only be used on streamed data
 */
void fvc_calc_hmac_sha256_init(uint8_t *key, size_t key_len);

/**
 * @brief Updates current HMAC-SHA256 hash calculation
 * @param [in] data - pointer to data to be hashed
 * @param [in] data_len - length of data
 * @note this functions set can only be used on streamed data
 */
void fvc_calc_hmac_sha256_write_data(uint8_t *data, size_t data_len);

/**
 * @brief Finishes calculations of HMAC-SHA256 and puts it to output buffer
 * @param [out] hash_out - pointer to caLculated hash buffer (must be 32 bytes) 
 * @note this functions set can only be used on streamed data
 */
void fvc_calc_hmac_sha256_end_calc(uint8_t *hash_out);

#endif
