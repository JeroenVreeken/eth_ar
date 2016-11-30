/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2016

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#ifndef _INCLUDE_FPRS_H_
#define _INCLUDE_FPRS_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>


/*
	Frame:
	
	A frame consists of a number of concatenated elements.
	Each starting with a header followed by a number of data bytes.
	A header can be 1, 2 or 3 bytes.
	Data can be up to 255 bytes (depending on element type)
	
	|header|data|header|data|header|data|
 */
 
struct fprs_frame;
struct fprs_element;

struct fprs_frame *fprs_frame_create(void);
void fprs_frame_destroy(struct fprs_frame *);

/* Set frame data to given byte array. Used when receiving frames */
int fprs_frame_data_set(struct fprs_frame *, uint8_t *data, size_t size);

/* Get frame data. Used when sending a frame 
 	size:	In: available space
		Out: used size
 */
int fprs_frame_data_get(struct fprs_frame *, uint8_t *data, size_t *size);

size_t fprs_frame_data_size(struct fprs_frame *);

struct fprs_element *fprs_frame_element_get(struct fprs_frame *, struct fprs_element *prev);

/*
	Element header:
	
	e: Element number
	l: Element length (excluding header)
	
	Element 0  - 15  : 0eeeelll

	Element 16 - 63  : 10eeeeee llllllll
	
	Element 64 - 8191: 110eeeee eeeeeeee llllllll
 */

/* FPRS element types */
enum fprs_type {
	FPRS_ERROR = -1,
	
	/* length limited to 7 */
	FPRS_POSITION = 0, /* 7 byte 2D position (lat + lon) */
	FPRS_CALLSIGN = 1, /* 6 byte callsign in ETH_AR format (base 37) */
	FPRS_SYMBOL = 2,   /* 2 byte symbol */
	FPRS_ALTITUDE = 3, /* 2 byte altitude */
	FPRS_VECTOR = 4,   /* 4 byte movement vector (12bit az, 11bit el, 9bit FP speed)*/

	/* length limited to 255 */
	FPRS_OBJECTNAME = 16, 	/* Object name (variable length) */
	FPRS_COMMENT = 17, 	/* Generic comment (do not use it for objects, altitude etc) */
	FPRS_REQUEST = 18,	/* Request elements about a station */
	FPRS_DESTINATION = 19,	/* Reply target */
	FPRS_TIMESTAMP = 20,	/* variable length timestamp */

	FPRS_DMLSTREAM = 21,	/* DML stream name (variable length) */
	FPRS_DMLASSOC = 22,	/* DML stream name (variable length) */
};

char *fprs_type2str(enum fprs_type);

struct fprs_element *fprs_frame_element_by_type(struct fprs_frame *, enum fprs_type);

size_t fprs_element_size(struct fprs_element *element);
enum fprs_type fprs_element_type(struct fprs_element *element);
uint8_t *fprs_element_data(struct fprs_element *element);

char *fprs_element2stra(struct fprs_element *el);

struct fprs_element *fprs_frame_element_add(struct fprs_frame *frame, enum fprs_type type, size_t size);

int fprs_frame_add_position(struct fprs_frame *, double lon, double lat, bool fixed);
int fprs_position_enc(uint8_t enc[7], double lon, double lat, bool fixed);
int fprs_position_dec(double *lon, double *lat, bool *fixed, uint8_t dec[7]);

int fprs_frame_add_callsign(struct fprs_frame *, uint8_t callsign[6]);
int fprs_frame_add_destination(struct fprs_frame *, uint8_t callsign[6]);

int fprs_frame_add_request(struct fprs_frame *, uint8_t callsign[6], enum fprs_type *elements, int nr_elements);
int fprs_request_dec(uint8_t callsign[6], enum fprs_type *elements, int *nr_elements, uint8_t *el_data, size_t el_size);

int fprs_frame_add_symbol(struct fprs_frame *frame, uint8_t symbol[2]);

int fprs_frame_add_altitude(struct fprs_frame *, double altitude);
int fprs_altitude_enc(uint8_t enc[2], double altitude);
int fprs_altitude_dec(double *altitude, uint8_t dec[2]);

int fprs_frame_add_vector(struct fprs_frame *, double az, double el, double speed);
int fprs_vector_enc(uint8_t enc[4], double az, double el, double speed);
int fprs_vector_dec(double *az, double *el, double *speed, uint8_t dec[4]);

int fprs_frame_add_timestamp(struct fprs_frame *, time_t time);
int fprs_timestamp_dec(time_t *timestamp, uint8_t *el_data, size_t el_size);

#define FPRS_VECTOR_SPEED_EPSILON (1.0/16.0)

int fprs_frame_add_objectname(struct fprs_frame *, char *);
int fprs_frame_add_comment(struct fprs_frame *, char *);
int fprs_frame_add_dmlassoc(struct fprs_frame *, char *);
int fprs_frame_add_dmlstream(struct fprs_frame *, char *);

/* Conversion of a fprs frame to aprs ASCII format */
int fprs2aprs(char *aprs, size_t *aprs_len, struct fprs_frame *frame, uint8_t *callsign, char *gate_call);


#endif /* _INCLUDE_FPRS_H_ */
