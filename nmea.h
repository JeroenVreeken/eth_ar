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

#include <stdbool.h>

struct nmea_state {
	bool position_valid;
	double latitude;
	double longitude;
	
	bool altitude_valid;
	double altitude;
	
	bool speed_valid;
	double speed;
	
	bool course_valid;
	double course;
};

int nmea_parse(struct nmea_state *state, char *line);

#endif /* _INCLUDE_NMEA_H_ */
