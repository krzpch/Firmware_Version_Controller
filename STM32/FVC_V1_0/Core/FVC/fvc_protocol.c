#include "fvc_protocol.h"
#include "fvc.h"
#include "bsp.h"

#include <string.h>

#define IS_PROTOCOL_DEBUG_ENABLED(_debug_config) (_debug_config & (1 << 0)) ? true : false
#define IS_INTERFACE_DEBUG_ENABLED(_debug_config) (_debug_config & (1 << 1)) ? true : false

struct fvc_protocol_ctx
{
	// values read from EEPROM
	uint8_t debug_conf;
	uint8_t board_id;
};

static struct fvc_protocol_ctx ctx;

#define MAX_CLI_MSG		256
#define PACKET_OVERHEAD 4

/**
 * Table with calculated crc table.
 */
static uint8_t const crc8x_table[] = { 0x00, 0x31, 0x62, 0x53, 0xc4, 0xf5,
		0xa6, 0x97, 0xb9, 0x88, 0xdb, 0xea, 0x7d, 0x4c, 0x1f, 0x2e, 0x43, 0x72,
		0x21, 0x10, 0x87, 0xb6, 0xe5, 0xd4, 0xfa, 0xcb, 0x98, 0xa9, 0x3e, 0x0f,
		0x5c, 0x6d, 0x86, 0xb7, 0xe4, 0xd5, 0x42, 0x73, 0x20, 0x11, 0x3f, 0x0e,
		0x5d, 0x6c, 0xfb, 0xca, 0x99, 0xa8, 0xc5, 0xf4, 0xa7, 0x96, 0x01, 0x30,
		0x63, 0x52, 0x7c, 0x4d, 0x1e, 0x2f, 0xb8, 0x89, 0xda, 0xeb, 0x3d, 0x0c,
		0x5f, 0x6e, 0xf9, 0xc8, 0x9b, 0xaa, 0x84, 0xb5, 0xe6, 0xd7, 0x40, 0x71,
		0x22, 0x13, 0x7e, 0x4f, 0x1c, 0x2d, 0xba, 0x8b, 0xd8, 0xe9, 0xc7, 0xf6,
		0xa5, 0x94, 0x03, 0x32, 0x61, 0x50, 0xbb, 0x8a, 0xd9, 0xe8, 0x7f, 0x4e,
		0x1d, 0x2c, 0x02, 0x33, 0x60, 0x51, 0xc6, 0xf7, 0xa4, 0x95, 0xf8, 0xc9,
		0x9a, 0xab, 0x3c, 0x0d, 0x5e, 0x6f, 0x41, 0x70, 0x23, 0x12, 0x85, 0xb4,
		0xe7, 0xd6, 0x7a, 0x4b, 0x18, 0x29, 0xbe, 0x8f, 0xdc, 0xed, 0xc3, 0xf2,
		0xa1, 0x90, 0x07, 0x36, 0x65, 0x54, 0x39, 0x08, 0x5b, 0x6a, 0xfd, 0xcc,
		0x9f, 0xae, 0x80, 0xb1, 0xe2, 0xd3, 0x44, 0x75, 0x26, 0x17, 0xfc, 0xcd,
		0x9e, 0xaf, 0x38, 0x09, 0x5a, 0x6b, 0x45, 0x74, 0x27, 0x16, 0x81, 0xb0,
		0xe3, 0xd2, 0xbf, 0x8e, 0xdd, 0xec, 0x7b, 0x4a, 0x19, 0x28, 0x06, 0x37,
		0x64, 0x55, 0xc2, 0xf3, 0xa0, 0x91, 0x47, 0x76, 0x25, 0x14, 0x83, 0xb2,
		0xe1, 0xd0, 0xfe, 0xcf, 0x9c, 0xad, 0x3a, 0x0b, 0x58, 0x69, 0x04, 0x35,
		0x66, 0x57, 0xc0, 0xf1, 0xa2, 0x93, 0xbd, 0x8c, 0xdf, 0xee, 0x79, 0x48,
		0x1b, 0x2a, 0xc1, 0xf0, 0xa3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49,
		0x1a, 0x2b, 0xbc, 0x8d, 0xde, 0xef, 0x82, 0xb3, 0xe0, 0xd1, 0x46, 0x77,
		0x24, 0x15, 0x3b, 0x0a, 0x59, 0x68, 0xff, 0xce, 0x9d, 0xac };


static uint8_t _calculate_hash(uint8_t *data, size_t data_len)
{
	uint8_t crc = 0xFF;
	if (data == NULL) {
		return 0xff;
	}
	crc &= 0xff;
 	while (data_len--) {
		crc = crc8x_table[crc ^ *data++];
	}
	return crc;
}

static size_t _calculate_packet_len(struct protocol_frame * structure)
{
	size_t packet_len = PROTOCOL_COMMON_LEN + PROTOCOL_HASH_LEN;

	switch (structure->data_type) {
		case TYPE_NACK:
		case TYPE_ACK:
		case TYPE_ID_REQ:
			packet_len += 1;
			break;
		case TYPE_CLI_DATA:
		case TYPE_PROGRAM_DATA:
			packet_len += (structure->payload_len + 2);
			break;
		default:
			break;
	}

	return packet_len;
}

void fvc_protocol_init(uint8_t board_id, uint8_t debug_conf)
{
	ctx.board_id = board_id;
	ctx.debug_conf = debug_conf;
}

size_t frame_serialize (struct protocol_frame * structure, uint8_t * packet, size_t max_packet_len)
{
	if ((structure == NULL) || (packet == NULL) || (max_packet_len < _calculate_packet_len(structure))) {
		return 0;
	}

	size_t iterator = 0;

	packet[iterator++] = structure->source_id;
	packet[iterator++] = structure->destination_id;
	packet[iterator++] = (uint8_t) structure->data_type;

	switch (structure->data_type) {
		case TYPE_NACK:
		case TYPE_ACK:
		case TYPE_ID_REQ:
			break;
		case TYPE_ID_RESP:
			packet[iterator++] = *structure->payload_ptr;
			break;
		case TYPE_PROGRAM_DATA:
		case TYPE_CLI_DATA:
			packet[iterator++] = (uint8_t) structure->payload_len >> 8;
			packet[iterator++] = (uint8_t) (structure->payload_len);
			memcpy(&packet[iterator], structure->payload_ptr, structure->payload_len);
			iterator += structure->payload_len;
			break;
		default:
			return 0;
	}

	packet[iterator] = _calculate_hash(packet, iterator);

	return iterator + 1;
}

bool frame_deserialize (struct protocol_frame * structure, uint8_t * packet, size_t max_packet_len)
{
	if ((structure == NULL) || (packet == NULL) || (max_packet_len == 0)) {
		return 0;
	}
	uint16_t iterator = 0;
	uint8_t calculated_hash = 0;
	uint8_t packet_hash = 0;

	structure->source_id = packet[iterator++];
	structure->destination_id = packet[iterator++];
	structure->data_type = (enum payload_type) packet[iterator++];

	switch (structure->data_type) {
		case TYPE_NACK:
		case TYPE_ACK:
		case TYPE_ID_REQ:
			break;
		case TYPE_ID_RESP:
			*structure->payload_ptr = packet[iterator++];
			break;
		case TYPE_PROGRAM_UPDATE_REQUEST:
			structure->payload_len = 12;
			memcpy(structure->payload_ptr, &packet[iterator], structure->payload_len);
			iterator += structure->payload_len;
			break;
		case TYPE_PROGRAM_DATA:
		case TYPE_CLI_DATA:
			structure->payload_len = (((uint16_t) packet[iterator]) << 8) | ((uint16_t) packet[iterator+1]);
			memcpy(structure->payload_ptr, &packet[iterator+2], structure->payload_len);
			iterator += 2 + structure->payload_len;
			break;
		default:
			return false;
	}

	calculated_hash = _calculate_hash(packet, iterator);
	packet_hash = packet[iterator];

	if (packet_hash != calculated_hash) {
		return false;
	}

	return true;
}

bool debug_transmit(const char* format, ...)
{

	bool status = false;
	uint8_t temp_buff[MAX_CLI_MSG] = {0};
	uint8_t serialized_packet[MAX_CLI_MSG + PACKET_OVERHEAD] = {0};
	struct protocol_frame frame = {
			.source_id = ctx.board_id,
			.destination_id = 0,
			.data_type = TYPE_CLI_DATA,
			.payload_ptr = temp_buff
	};
	va_list ap;
	va_start(ap, format);
	size_t len = vsnprintf((char*)temp_buff, MAX_CLI_MSG, (const char*)format, ap);\
	if(len == 0)
		return status;

	if (IS_INTERFACE_DEBUG_ENABLED(ctx.debug_conf))
		status = bsp_debug_interface_transmit((uint8_t *)temp_buff, len);

	if (IS_PROTOCOL_DEBUG_ENABLED(ctx.debug_conf))
	{
		frame.payload_len = len;
		len = frame_serialize(&frame,(uint8_t *) serialized_packet, MAX_CLI_MSG + PACKET_OVERHEAD);
		status &= bsp_interface_transmit((uint8_t *)serialized_packet, len);
	}

	va_end(ap);
	return status;
}

bool send_response(bool response)
{
	bool status = false;
	uint8_t serialized_packet[MAX_CLI_MSG + PACKET_OVERHEAD] = {0};
	struct protocol_frame frame = {
			.source_id = ctx.board_id,
			.destination_id = 0,
	};
	frame.data_type = response ? TYPE_ACK : TYPE_NACK;

	size_t len = frame_serialize(&frame,(uint8_t *) serialized_packet, MAX_CLI_MSG + PACKET_OVERHEAD);
	if (len > 0) {
		status = bsp_interface_transmit((uint8_t *)serialized_packet, len);
	}
	return status;
}
