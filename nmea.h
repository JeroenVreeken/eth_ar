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
#ifndef _INCLUDE_NMEA_H_
#define _INCLUDE_NMEA_H_

#include <stdlib.h>
#include <stdbool.h>

#define NMEA_LEN 83
#define NMEA_SIZE 84

struct nmea_state {
	/* Parsed state */
	bool position_valid;
	double latitude;
	double longitude;
	
	bool altitude_valid;
	double altitude;
	
	bool speed_valid;
	double speed;
	
	bool course_valid;
	double course;
	
	/* Internal state */
	char line[NMEA_SIZE];
	int pos;
};

struct nmea_state *nmea_state_create(void);
void nmea_state_destroy(struct nmea_state *nmea);

int nmea_parse_line(struct nmea_state *state, char *line);

int nmea_parse(struct nmea_state *nmea, char *data, size_t size);

#endif /* _INCLUDE_NMEA_H_ */
