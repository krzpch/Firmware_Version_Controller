#ifndef FVC_PROTOCOL_H
#define FVC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#define PROTOCOL_COMMON_LEN		3		// Common data in frame (source_id, destination_id, data_type)
#define PROTOCOL_HASH_LEN		2
#define PROTOCOL_MAX_DATA_LEN	16*1024
#define PROTOCOL_FRAM_MAX_LEN	(3+PROTOCOL_MAX_DATA_LEN+PROTOCOL_HASH_LEN)

enum payload_type
{
	TYPE_NACK = 0,
	TYPE_ACK,
	TYPE_FATAL_ERROR,
	TYPE_ID_REQ,
	TYPE_ID_RESP,
	TYPE_CLI_DATA,
	TYPE_PROGRAM_UPDATE_REQUEST,
	TYPE_PROGRAM_DATA,
	TYPE_PROGRAM_UPDATE_FINISHED,
	TYPE_EEPROM_DATA_READ,
	TYPE_EEPROM_DATA_WRITE,

	TYPE_TOP
};

struct protocol_frame
{
	uint8_t source_id;
	uint8_t destination_id;
	enum payload_type data_type;
	uint16_t payload_len;
	uint8_t *payload_ptr;
};

void fvc_protocol_init(uint8_t board_id, uint8_t debug_conf);

size_t frame_serialize (struct protocol_frame * structure, uint8_t * packet, size_t max_packet_len);
bool frame_deserialize (struct protocol_frame * structure, uint8_t * packet, size_t max_packet_len);

bool debug_transmit(const char* format, ...);
bool send_response(enum payload_type response);

#endif
