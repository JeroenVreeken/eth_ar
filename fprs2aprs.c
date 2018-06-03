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
#include <ctype.h>
#include <time.h>

struct fprs_frame *aprs2fprs(char *aprs)
{
	int i;
	
	/* skip comments */
	if (aprs[0] == '#')
		return NULL;
	
	struct fprs_frame *frame = fprs_frame_create();
	
	for (i = 0; i < strlen(aprs); i++) {
		if (i > 9)
			goto err_nocall;
		if (aprs[i] == '>') {
			char from_call[10];
			memcpy(from_call, aprs, i);
			from_call[i] = 0;
			uint8_t from_ar[6];
			
//			printf("call: %s\n", from_call);

			if (eth_ar_callssid2mac(from_ar, from_call, false))
				goto err_ar;

			fprs_frame_add_callsign(frame, from_ar);
			break;
		}
	}
	for (i++; i < strlen(aprs); i++) {
		if (aprs[i] == ':') {
			i++;
			break;
		}
	}
	if (i >= strlen(aprs)-1)
		goto err_nopayload;
	int payloadlen = strlen(aprs) - i;
	
	if (payloadlen > 10 && 
	    aprs[i] == ':' && 
	    aprs[i+10] == ':') {
		char dest_call[10];
		memcpy(dest_call, aprs+i+1, 9);
		dest_call[9] = 0;
		uint8_t dest_ar[6];
		
//		printf("to  : %s\n", dest_call);
		
		if (eth_ar_callssid2mac(dest_ar, dest_call, false))
			goto err_ar_dest;
		
		fprs_frame_add_destination(frame, dest_ar);
		
		int msg_start = i + 11;
		for (i = msg_start; i < strlen(aprs); i++) {
			if (aprs[i] == '{')
				break;
		}
		int msg_end = i;
		int msg_len = (msg_end - msg_start);
		
//		printf("MSG len: %d\n", msg_len);
		if (!memcmp(aprs + msg_start, "ack", 3)) {
			fprs_frame_add_messageid(frame, 
			    (uint8_t *)aprs + msg_start + 3, msg_len - 3);
//			printf("ACK size: %d\n", msg_len - 3);
		} else if (!memcmp(aprs + msg_start, "rej", 3)) {
			/* No reject yet */
		} else {
			if (fprs_frame_add_message(frame, 
			    (uint8_t *)aprs + msg_start, msg_len))
				goto err_msg;
		
			if (aprs[msg_end] == '{') {
				int id_size = (strlen(aprs) - msg_end) -1;
//				printf("ID size: %d\n", id_size);
				fprs_frame_add_messageid(frame, 
				    (uint8_t *)aprs +msg_end + 1, id_size);
			}
		}
	}

	return frame;
err_msg:
err_ar_dest:
err_nopayload:
err_ar:
err_nocall:
	fprs_frame_destroy(frame);
	return NULL;
}

int fprs2aprs(char *aprs, size_t *aprs_len, struct fprs_frame *frame, uint8_t *callsign, char *gate_call)
{
	uint8_t origin[6] = { 0 };
	bool have_origin = false;
	double lat, lon;
	bool pos_fixed = true;
	bool have_position = false;
	char symbol1[2] = "";
	char symbol2[2] = "";
	bool have_symbol = false;
	double az, el, speed;
	bool have_vector = false;
	double alt;
	bool have_altitude = false;
	char comment[256] = "";
	time_t timestamp;
	bool have_timestamp = false;
	char dmlassoc[300] = "";
	bool have_dmlassoc = false;
	char dmlstream[300] = "";
	bool have_dmlstream = false;
	
	if (callsign) {
		memcpy(origin, callsign, 6);
		have_origin = true;
	}
	
	struct fprs_element *element = NULL;
	while ((element = fprs_frame_element_get(frame, element))) {
		enum fprs_type type = fprs_element_type(element);
		
		switch (type) {
			case FPRS_REQUEST:
				return -1;
			case FPRS_CALLSIGN:
				memcpy(origin, fprs_element_data(element), 6);
				have_origin = true;
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
				symbol1[0] = fprs_element_data(element)[0];
				symbol2[0] = fprs_element_data(element)[1];
				symbol1[1] = 0;
				symbol2[1] = 0;
				have_symbol = true;
				break;
			case FPRS_COMMENT: {
				size_t comment_len = fprs_element_size(element);
				memcpy(comment, fprs_element_data(element), comment_len);
				comment[comment_len] = 0;
				break;
			}
			case FPRS_TIMESTAMP:
				fprs_timestamp_dec(&timestamp, 
				    fprs_element_data(element), fprs_element_size(element));
				have_timestamp = true;
				break;
			case FPRS_DMLSTREAM: {
				char tmp[256];
				size_t dmlstream_len = fprs_element_size(element);
				memcpy(tmp, fprs_element_data(element), dmlstream_len);
				tmp[dmlstream_len] = 0;
				sprintf(dmlstream, "DMLSTREAM:%s ", tmp);
				have_dmlstream = true;
				break;
			}
			case FPRS_DMLASSOC: {
				char tmp[256];
				size_t dmlassoc_len = fprs_element_size(element);
				memcpy(tmp, fprs_element_data(element), dmlassoc_len);
				tmp[dmlassoc_len] = 0;
				sprintf(dmlassoc, "DMLASSOC:%s ", tmp);
				have_dmlassoc = true;
				break;
			}
				
				break;
			default:
				break;
		}
	}
	if (!have_origin)
		return -1;
	if (!have_position && !have_dmlstream && !have_dmlassoc)
		return -1;

	char sender_call[9];
	int sender_ssid;
	bool sender_mcast;
	uint8_t type = '{';

	eth_ar_mac2call(sender_call, &sender_ssid, &sender_mcast, origin);

	if (!have_symbol) {
		strcpy(symbol1, "F");
		strcpy(symbol2, "A");
	}
	if (!have_position) {
		symbol1[0] = 0;
		symbol2[0] = 0;
	}
	
	char timestampstr[8] = { 0 };
	if (have_timestamp) {
		struct tm tmbd;
		
		gmtime_r(&timestamp, &tmbd);
		sprintf(timestampstr, "%02d%02d%02dz", 
		    tmbd.tm_mday, tmbd.tm_hour, tmbd.tm_min);
	}
	
	char lonstr[10] = { 0 };
	char latstr[9] = { 0 };
	if (have_position) {
		if (have_timestamp)
			type = '/';
		else
			type = '!';
		
		int lond = (int)fabs(lon) % 360;
		double lonmin = fabs(lon - (int)lon) * 60;
		char lons = lon < 0 ? 'W' : 'E';
		snprintf(lonstr, sizeof(lonstr), "%03d%05.02f%c", lond, lonmin, lons);

		int latd = (int)fabs(lat) % 90;
		double latmin = fabs(lat - (int)lat) * 60;
		char lats = lat < 0 ? 'S' : 'N';
		snprintf(latstr, sizeof(latstr), "%02d%05.02f%c", latd, latmin, lats);
	} else {
		type = '>';
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
	
	snprintf(aprs, *aprs_len, "%s-%d>APFPRS,qAR,%s:"
	    "%c"
	    "%s%s%s%s%s"
	    "%s%s"
	    "%s%s%s\r\n", 
	    sender_call, sender_ssid, gate_call, 
	    type, 
	    timestampstr, latstr, symbol1, lonstr, symbol2, 
	    course_speed, altstr,
	    dmlstream, dmlassoc, comment);
	aprs[*aprs_len] = 0;
	*aprs_len = strlen(aprs);
	
	return 0;
}

#define kKey 0x73e2 // This is the seed for the key

int fprs2aprs_login(char *loginline, size_t *loginline_len, char *call)
{
	char rootCall[10]; // need to copy call to remove ssid from parse
	char *p0 = call;
	char *p1 = rootCall;
	short hash;
	short i,len;
	char *ptr = rootCall;

	while ((*p0 != '-') && (*p0 != '\0'))
		*p1++ = toupper(*p0++);
	*p1 = '\0';

	hash = kKey; // Initialize with the key value
	i = 0;
	len = (short)strlen(rootCall);

	while (i<len) {// Loop through the string two bytes at a time
		hash ^= (unsigned char)(*ptr++)<<8; // xor high byte with accumulated hash
		hash ^= (*ptr++); // xor low byte with accumulated hash
		i += 2;
	}
	hash = (short)(hash & 0x7fff); // mask off the high bit so number is always positive
	
	printf("Call: %s\n", rootCall);
	printf("Pass: %d\n", hash);

	snprintf(loginline, *loginline_len, "user %s pass %d vers fprs2aprs_gate 0.1\r\n",
	    call, hash);
	loginline[*loginline_len] = 0;
	*loginline_len = strlen(loginline);

	return 0;
}
