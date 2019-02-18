/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2015

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
#include <eth_ar/eth_ar.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

/*
	8 character callsign, 4 bit ssid
	|call(42-40) ssid(3-0) lm|
	|call(39-32)             |
	|call(31-24)             |
	|call(23-16)             |
	|call(15-8)              |
	|call(7-0)               |
 */

static char alnum2code[37] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
	'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 
	0
};

int eth_ar_call2mac(uint8_t mac[6], char *callsign, int ssid, bool multicast)
{
	uint64_t add = 0;
	int i;
	
	if (ssid > 15 || ssid < 0)
		return -1;
	
	for (i = 7; i >= 0; i--) {
		char c;
		
		if (i >= strlen(callsign)) {
			c = 0;
		} else {
			c = toupper(callsign[i]);
		}
		
		int j;
		
		for (j = 0; j < sizeof(alnum2code); j++) {
			if (alnum2code[j] == c)
				break;
		}
		if (j == sizeof(alnum2code))
			return -1;
		
		add *= 37;
		add += j;
	}
	
	mac[0] = ((add >> (40 - 6)) & 0xc0) | (ssid << 2) | 0x02 | multicast;
	mac[1] = (add >> 32) & 0xff;
	mac[2] = (add >> 24) & 0xff;
	mac[3] = (add >> 16) & 0xff;
	mac[4] = (add >> 8) & 0xff;
	mac[5] = add & 0xff;

	return 0;
}

int eth_ar_callssid2mac(uint8_t mac[6], char *callsign, bool multicast)
{
	int ssid = 0;
	char call[9];
	int i;
	
	for (i = 0; i < 8; i++) {
		if (callsign[i] == '-')
			break;
		if (callsign[i] == 0)
			break;
		if (callsign[i] == ' ')
			break;
		call[i] = callsign[i];
	}
	call[i] = 0;
	
	if (callsign[i] == '-') {
		ssid = atoi(callsign + i + 1);
	}
	
	return eth_ar_call2mac(mac, call, ssid, multicast);
}

int eth_ar_mac2call(char *callsign, int *ssid, bool *multicast, uint8_t mac[6])
{
	uint64_t add;
	int i;

	if (!memcmp(mac, (uint8_t[6]){ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 6)) {
		*ssid = 0;
		*multicast = true;
		strcpy(callsign, "*");
		return 0;
	}
	*multicast = mac[0] & 0x01;
	*ssid = (mac[0] & 0x3c) >> 2;
	add = (uint64_t)(mac[0] & 0xc0) << (40 - 6);
	add |= (uint64_t)mac[1] << 32;
	add |= (uint64_t)mac[2] << 24;
	add |= (uint64_t)mac[3] << 16;
	add |= (uint64_t)mac[4] << 8;
	add |= (uint64_t)mac[5];

	for (i = 0; i < 8; i++) {
		int c = add % 37;
		callsign[i] = alnum2code[c];
		add /= 37;
	}
	callsign[i] = 0;

	return 0;
}

int eth_ar_mac_ssid_mask(uint8_t masked_mac[6], const uint8_t mac[6])
{
	masked_mac[0] = mac[0] & 0xc3;
	memcpy(masked_mac + 1, mac + 1, 5);

	return 0;
}



uint8_t eth_ar_dbm_encode(double dbm)
{
	double enc = (dbm * 2.0) - 128.0;
	
	if (enc < 0)
		return 0;
	if (enc > 255)
		return 255;
	return (uint8_t)(enc + 0.5);
}

double eth_ar_dbm_decode(uint8_t enc)
{
	if (enc) {
		return -128.0 + (enc / 2.0);
	} else {
		return 0.0;
	}
}

