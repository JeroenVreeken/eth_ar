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
#ifndef _INCLUDE_ETH_AR_H_
#define _INCLUDE_ETH_AR_H_

#include <stdint.h>
#include <stdbool.h>

#define ETH_P_CODEC2_3200	0x7300
#define ETH_P_CODEC2_2400	0x7301
#define ETH_P_CODEC2_1600	0x7302
#define ETH_P_CODEC2_1400	0x7303
#define ETH_P_CODEC2_1300	0x7304
#define ETH_P_CODEC2_1200	0x7305
#define ETH_P_CODEC2_700	0x7306
#define ETH_P_CODEC2_700B	0x7307

#define ETH_P_AR_CONTROL	0x7342

#define ETH_AR_CALL_LEN_MAX	8
#define ETH_AR_CALL_SIZE	9

int eth_ar_call2mac(uint8_t mac[6], char *callsign, int ssid, bool multicast);
int eth_ar_callssid2mac(uint8_t mac[6], char *callsign, bool multicast);
int eth_ar_mac2call(char *callsign, int *ssid, bool *multicast, uint8_t mac[6]);


#endif /* _INCLUDE_ETH_AR_H_ */
