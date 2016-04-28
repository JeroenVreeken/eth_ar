#include "fprs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


struct fprs_frame {
	uint8_t *data;
	size_t size;
};



struct fprs_frame *fprs_frame_create(void)
{
	return calloc(1, sizeof(struct fprs_frame));
}

void fprs_frame_destroy(struct fprs_frame *frame)
{
	free(frame->data);
	free(frame);
}

static int fprs_frame_data_realloc(struct fprs_frame *frame, size_t size)
{
	if (frame->size < size) {
		uint8_t *newdata = realloc(frame->data, size);
		if (!newdata)
			return -1;
		frame->data = newdata;
		frame->size = size;
	}
	return 0;
}

int fprs_frame_data_set(struct fprs_frame *frame, uint8_t *data, size_t size)
{
	if (fprs_frame_data_realloc(frame, size))
		return -1;
	
	memcpy(frame->data, data, size);
	frame->size = size;
	
	return 0;
}

int fprs_frame_data_get(struct fprs_frame *frame, uint8_t *data, size_t *size)
{
	if (*size < frame->size)
		return -1;
	
	memcpy(data, frame->data, frame->size);
	*size = frame->size;
	
	return 0;
}

size_t fprs_frame_data_size(struct fprs_frame *frame)
{
	return frame->size;
}


static size_t fprs_element_size_total(struct fprs_frame *frame, uint8_t *element)
{
	size_t pos = element - frame->data;
	size_t size = 0;
	
	if (pos >= frame->size)
		return 0;
	
	if ((element[0] & 0x80) == 0) {
		size = (element[0] & 0x07) + 1;
	} else {
		if (pos >= frame->size - 1)
			return 0;
		if ((element[0] & 0xc0) == 0x80) {
			size = element[1] + 2;
		} else {
			if (pos >= frame->size - 2)
				return 0;
			size = element[2] + 3;
		}
	}
	if (pos + size > frame->size)
		return 0;

	return size;
}


uint8_t *fprs_frame_element_get(struct fprs_frame *frame, uint8_t *prev)
{
	size_t pos;
	
	if (prev) {
		pos = prev - frame->data;
		pos += fprs_element_size_total(frame, prev);
	} else {
		pos = 0;
	}
	
	if (fprs_element_size_total(frame, frame->data + pos))
		return frame->data + pos;
	else
		return NULL;
}

uint8_t *fprs_frame_element_add(struct fprs_frame *frame, enum fprs_type type, size_t size)
{
	size_t allocsize = size;

	if (type < 16)
		allocsize += 1;
	else if (type < 64)
		allocsize += 2;
	else
		allocsize += 3;
	
	if (fprs_frame_data_realloc(frame, frame->size + allocsize))
		return NULL;

	uint8_t *element = frame->data + frame->size - allocsize;

	if (type < 16) {
		element[0] = size | (type << 3);
	} else if (type < 64) {
		element[0] = 0x80 | type;
		element[1] = size;
	} else {
		element[0] = 0xc0 | (type >> 8);
		element[1] = type & 0xff;
		element[2] = size;
	}
	return element;
}

static size_t fprs_element_typed_size(uint8_t *element, enum fprs_type type)
{
	if (type < 16) {
		return element[0] & 0x07;
	}
	if (type < 64) {
		return element[1];
	}
	return element[2];
}

size_t fprs_element_size(uint8_t *element)
{
	enum fprs_type type = fprs_element_type(element);
	
	return fprs_element_typed_size(element, type);
}

enum fprs_type fprs_element_type(uint8_t *element)
{
	enum fprs_type ret;
	uint8_t b0 = element[0];
	
	if ((b0 & 0x80) == 0x00) {
		ret = (b0 & 0x78) >> 3;
	} else if ((b0 & 0xc0) == 0x80) {
		ret = b0 & 0x3f;
	} else {
		uint8_t b1 = element[1];
	
		ret = b1 + ((b0 & 0x1f) << 8);
	}
	
	size_t size = fprs_element_typed_size(element, ret);
	switch (ret) {
		case FPRS_POSITION:
			if (size != 7)
				return FPRS_ERROR;
			break;
		case FPRS_CALLSIGN:
			if (size != 6)
				return FPRS_ERROR;
			break;
		case FPRS_SYMBOL:
			if (size != 2)
				return FPRS_ERROR;
			break;
		case FPRS_ALTITUDE:
			if (size != 2)
				return FPRS_ERROR;
			break;
		case FPRS_VECTOR:
			if (size != 4)
				return FPRS_ERROR;
			break;
		default:
			break;
	}
	return ret;
}

uint8_t *fprs_element_data(uint8_t *element)
{
	enum fprs_type type = fprs_element_type(element);
	
	if (type < 16)
		return element + 1;
	if (type < 64)
		return element + 2;
	return element + 3;
}

int fprs_frame_add_position(struct fprs_frame *frame, double lon, double lat, bool fixed)
{
	uint8_t *element;
	
	element = fprs_frame_element_add(frame, FPRS_POSITION, 7);
	if (!element)
		return -1;

	fprs_position_enc(fprs_element_data(element), lon, lat, fixed);

	return 0;
}

int fprs_frame_add_callsign(struct fprs_frame *frame, uint8_t callsign[6])
{
	uint8_t *element;
	
	element = fprs_frame_element_add(frame, FPRS_CALLSIGN, 6);
	if (!element)
		return -1;

	memcpy(fprs_element_data(element), callsign, 6);

	return 0;
}

int fprs_frame_add_objectname(struct fprs_frame *frame, char *obj)
{
	uint8_t *element;
	size_t objsize = strlen(obj);
	
	if (objsize > 255)
		return -1;
	
	element = fprs_frame_element_add(frame, FPRS_OBJECTNAME, objsize);
	if (!element)
		return -1;
	
	memcpy(fprs_element_data(element), obj, objsize);

	return 0;
}

#define FPRS_LON_SCALE 134217728
#define FPRS_LON_MAX 134217727
#define FPRS_LON_MIN -134217728

#define FPRS_LAT_SCALE 67108864
#define FPRS_LAT_MAX 67108863
#define FPRS_LAT_MIN -67108864

/* calculate a 28 bit lon, 27 bit lat and 1 bit stationary/fixed value 
   Result is placed in 7 bytes 
 */
int fprs_position_enc(uint8_t enc[7], double lon, double lat, bool fixed)
{
	int32_t lon32, lat32;
	
	lon32 = FPRS_LON_SCALE * lon / 180.0;
	
	lat32 = FPRS_LAT_SCALE * lat / 90.0;
	
	if (lon32 > FPRS_LON_MAX)
		lon32 = FPRS_LON_MAX;
	if (lon32 < FPRS_LON_MIN)
		lon32 = FPRS_LON_MIN;
	if (lat32 > FPRS_LAT_MAX)
		lat32 = FPRS_LAT_MAX;
	if (lat32 < FPRS_LAT_MIN)
		lat32 = FPRS_LAT_MIN;

	enc[0] = (lon32 >> 20) & 0xff;
	enc[1] = (lon32 >> 12) & 0xff;
	enc[2] = (lon32 >> 4) & 0xff;
	enc[3] = ((lon32 << 4) & 0xf0) | (fixed << 3) | ((lat32 >> 24) & 0x07);
	enc[4] = (lat32 >> 16) & 0xff;
	enc[5] = (lat32 >> 8) & 0xff;
	enc[6] = (lat32) & 0xff;

	return 0;
}

int fprs_position_dec(double *lon, double *lat, bool *fixed, uint8_t dec[7])
{
	*fixed = dec[3] & 0x08;
	
	int32_t lon32, lat32;
	
	lon32 = dec[0] << 20;
	lon32 |= dec[1] << 12;
	lon32 |= dec[2] << 4;
	lon32 |= (dec[3] & 0xf0) >> 4;
	if (lon32 & 0x08000000) {
		lon32 |= 0xf0000000;
	}
	*lon = (lon32 * 180.0) / FPRS_LON_SCALE;
	if (*lon > 180) {
		*lon -= 360.0;
	}

	lat32 = (dec[3] & 0x07) << 24;
	lat32 |= dec[4] << 16;
	lat32 |= dec[5] << 8;
	lat32 |= dec[6];

	if (lat32 & 0x04000000) {
		lat32 |= 0xf8000000;
	}
	*lat = (lat32 * 90.0) / FPRS_LAT_SCALE;

	return 0;
}

int fprs_frame_add_altitude(struct fprs_frame *frame, double altitude)
{
	uint8_t *element;
	
	element = fprs_frame_element_add(frame, FPRS_ALTITUDE, 2);
	if (!element)
		return -1;

	fprs_altitude_enc(fprs_element_data(element), altitude);

	return 0;
}

int fprs_altitude_enc(uint8_t enc[2], double altitude)
{
	altitude += 16384;
	
	if (altitude < 0)
		altitude = 0;
	if (altitude > 65535)
		altitude = 65535;
	
	uint16_t alti = altitude;
	
	enc[0] = alti >> 8;
	enc[1] = alti & 0xff;

	return 0;
}

int fprs_altitude_dec(double *altitude, uint8_t dec[2])
{
	double alt;
	
	alt = (dec[0] << 8) | dec[1];
	alt -= 16384;
	
	*altitude = alt;
	
	return 0;
}

int fprs_frame_add_vector(struct fprs_frame *frame, double az, double el, double speed)
{
	uint8_t *element;
	
	element = fprs_frame_element_add(frame, FPRS_VECTOR, 4);
	if (!element)
		return -1;

	fprs_vector_enc(fprs_element_data(element), az, el, speed);

	return 0;
}

static double pow2(int exp)
{
	switch (exp) {
		case 0:
			return 1;
		case 1:
			return 2;
		case 2:
			return 4;
		case 3:
			return 8;
		case 4:
			return 16;
		case 5:
			return 32;
		case 6:
			return 64;
		case 7:
			return 128;
		case 8:
			return 256;
	}
	return 512;
}

int fprs_vector_enc(uint8_t enc[4], double az, double el, double speed)
{
	int azi = (az / 360.0 * 4096.0) + 0.5;
	int eli = (el / 90 * 1024) + 0.5;
	
	if (eli > 1023)
		eli = 1023;
	if (eli < -1024)
		eli = 1024;
//	azi &= 4095;
//	eli &= 2047;
	
	int speed_exp;
	int speed_man;
	
	if (speed >= 512)
		speed = 511;
	
	if (speed < 4) {
		speed_exp = 0;
		speed_man = (speed * 16);
	} else {
		speed /= 4;
		for (speed_exp = 1; speed >= 2; speed_exp++)
			speed /= 2;
		speed -= 1.0;
		
		speed_man = speed * 64;
	}
	speed_exp &= 0x7;
	speed_man &= 0x3f;
	
	enc[3] = speed_man | ((speed_exp & 0x03) << 6);
	enc[2] = ((speed_exp >> 2) & 1) | ((eli & 0x7f) << 1);
	enc[1] = ((eli >> 7) & 0x0f) | ((azi & 0x0f) << 4);
	enc[0] = (azi & 0xff0) >> 4;
	
	return 0;
}

int fprs_vector_dec(double *az, double *el, double *speed, uint8_t dec[4])
{
	int azi = (dec[0] << 4) | ((dec[1] & 0xf0) >> 4);
	int eli = ((dec[1] & 0x0f) << 7) | ((dec[2] & 0xfe) >> 1);
	int speed_exp = ((dec[2] & 0x01) << 2) | ((dec[3] & 0xc0) >> 6);
	int speed_man = dec[3] & 0x3f;
	
	double speedd;
	
	if (speed_exp) {
		speedd = (1.0 / 64) * speed_man;
		speedd += 1;
		speedd *= pow2(speed_exp + 1);
	} else {
		speedd = (1.0 / 16) * speed_man;
	}
	
	*speed = speedd;
	
	*az = azi * 360.0 / 4096.0;
	
	if (eli >= 1024)
		eli -= 2048;
	*el = eli * 90.0 / 1024.0;
	
	return 0;
}

char *fprs_type2str(enum fprs_type type)
{
	switch(type) {
		case FPRS_ERROR:
			return "FPRS_ERROR";
		case FPRS_POSITION:
			return "FPRS_POSITION";
		case FPRS_CALLSIGN:
			return "FPRS_CALLSIGN";
		case FPRS_ALTITUDE:
			return "FPRS_ALTITUDE";
		case FPRS_VECTOR:
			return "FPRS_VECTOR";
		case FPRS_SYMBOL:
			return "FPRS_SYMBOL";
		case FPRS_OBJECTNAME:
			return "FPRS_OBJECTNAME";
		default:
			return "FPRS_UNKNOWN";
	}
}
