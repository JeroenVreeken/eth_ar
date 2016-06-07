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

#include <eth_ar/fprs.h>
#include <eth_ar/eth_ar.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

int fprs2aprs(char *aprs, size_t *aprs_len, struct fprs_frame *frame, uint8_t *callsign, char *gate_call)
{
	uint8_t origin[6] = { 0 };
	double lat, lon;
	bool pos_fixed = true;
	bool have_position = false;
	uint8_t symbol[2];
	bool have_symbol = false;
	double az, el, speed;
	bool have_vector = false;
	double alt;
	bool have_altitude = false;
	char comment[256] = "";
	
	if (callsign) {
		memcpy(origin, callsign, 6);
	}
	
	uint8_t *element = NULL;
	while ((element = fprs_frame_element_get(frame, element))) {
		enum fprs_type type = fprs_element_type(element);
		
		switch (type) {
			case FPRS_CALLSIGN:
				memcpy(origin, fprs_element_data(element), 6);
				break;
			case FPRS_POSITION:
				fprs_position_dec(&lon, &lat, &pos_fixed, fprs_element_data(element));
				have_position = true;
				break;
			case FPRS_ALTITUDE:
				fprs_altitude_dec(&alt, fprs_element_data(element));
				have_altitude = true;
				break;
			case FPRS_VECTOR:
				fprs_vector_dec(&az, &el, &speed, fprs_element_data(element));
				have_vector = true;
				break;
			case FPRS_SYMBOL:
				memcpy(symbol, fprs_element_data(element), 2);
				have_symbol = true;
				break;
			case FPRS_COMMENT: {
				size_t comment_len = fprs_element_size(element);
				memcpy(comment, fprs_element_data(element), comment_len);
				comment[comment_len] = 0;
				break;
			}
			default:
				break;
		}
	}

	char sender_call[9];
	int sender_ssid;
	bool sender_mcast;
	uint8_t type = '{';

	eth_ar_mac2call(sender_call, &sender_ssid, &sender_mcast, origin);

	if (!have_symbol) {
		symbol[0] = 'F';
		symbol[1] = 'A';
	}
	
	char lonstr[10] = { 0 };
	char latstr[9] = { 0 };
	if (have_position) {
		type = '!';
		
		int lond = fabs(lon);
		double lonmin = fabs(lon - (int)lon) * 60;
		char lons = lon < 0 ? 'W' : 'E';
		sprintf(lonstr, "%03d%05.02f%c", lond, lonmin, lons);

		int latd = fabs(lat);
		double latmin = fabs(lat - (int)lat) * 60;
		char lats = lat < 0 ? 'S' : 'N';
		sprintf(latstr, "%02d%05.02f%c", latd, latmin, lats);
	}
	
	char course_speed[8] = { 0 };
	if (have_vector) {
		speed *= cos(el / 180.0 * M_PI);
		
		sprintf(course_speed, "%03d/%03d", (int)(az + 0.5), (int)((speed * 1.943844492) + 0.5));
	}
	
	char altstr[10] = { 0 };
	if (have_altitude) {
		if (alt < 0)
			alt = 0;
		alt *= 3.2808399;
		sprintf(altstr, "/A=%06d", (int)alt);
	}
	
	snprintf(aprs, *aprs_len, "%s-%d>APFPRS,qAR,%s:%c%s%c%s%c%s%s%s\r\n", 
	    sender_call, sender_ssid, gate_call, 
	    type, latstr, symbol[0], lonstr, symbol[1], course_speed, altstr,
	    comment);
	aprs[*aprs_len] = 0;
	*aprs_len = strlen(aprs);
	
	return 0;
}
