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

#include "nmea.h"

#include <string.h>
#include <stdio.h>

static bool nmea_check(char *line)
{
	int i;
	bool start = false;
	int sum = 0;
	
	for (i = 0; i < strlen(line); i++) {
		if (!start) {
			if (line[i] == '$')
				start = true;
		} else {
			if (line[i] == '*') {
				int check;
				
				sscanf(line + i + 1, "%02x", &check);

				if (check == sum)
					return true;
				else
					return false;
			} else {
				sum ^= line[i];
			}
		}
	}
	
	return false;
}

int nmea_parse(struct nmea_state *state, char *line)
{
	if (!nmea_check(line))
		return -1;
	
	if (!strncmp(line + 1, "GPGGA", 5)) {
		int t;
		int lat, latd, lon, lond;
		char ns, ew;
		int fix;
		int nrsat;
		float dilution;
		float alt;
		char at;
		
		sscanf(line, "$GPGGA,%d,%d.%d,%c,%d.%d,%c,%d,%d,%g,%g,%c",
		    &t, &lat, &latd, &ns, &lon, &lond, &ew, &fix, &nrsat, &dilution,
		    &alt, &at);
		
		state->position_valid = fix;
		state->altitude_valid = fix;
		
		state->longitude = lon / 100;
		state->longitude += ((lon % 100) + lond * 0.001) / 60.0;
		if (ew == 'W')
			state->longitude *= -1;

		state->latitude = lat / 100;
		state->latitude += ((lat % 100) + latd * 0.001) / 60.0;
		if (ns == 'S')
			state->latitude *= -1;
		
		state->altitude = alt;
	}
	if (!strncmp(line + 1, "GPRMC", 5)) {
		int t;
		char status;
		int lat, latd, lon, lond;
		char ns, ew;
		float sknot;
		float tt;
		
		sscanf(line, "$GPRMC,%d,%c,%d.%d,%c,%d.%d,%c,%f,%f",
		    &t, &status, &lat, &latd, &ns, &lon, &lond, &ew, 
		    &sknot, &tt);
		
		state->position_valid = status == 'A';
		state->course_valid = status == 'A';
		state->speed_valid = status == 'A';
		
		state->longitude = lon / 100;
		state->longitude += ((lon % 100) + lond * 0.001) / 60.0;
		if (ew == 'W')
			state->longitude *= -1;

		state->latitude = lat / 100;
		state->latitude += ((lat % 100) + latd * 0.001) / 60.0;
		if (ns == 'S')
			state->latitude *= -1;
		
		state->course = tt;
		state->speed = sknot * 0.514444444;
	}
	if (!strncmp(line + 1, "GPVTG", 5)) {
		float tt;
		float m;
		float sknot;
		float skm;
		
		sscanf(line, "$GPVTG,%f,T,%f,M,%f,N,%f,K",
		    &tt, &m, &sknot, &skm);
		
		state->course = tt;
		state->course_valid = true;
		
		state->speed = sknot * 0.514444444;
		state->speed_valid = true;
	}
	
	return 0;
}


