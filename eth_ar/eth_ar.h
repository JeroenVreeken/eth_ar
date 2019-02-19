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

#ifdef __cplusplus
extern "C" {
#endif

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
#define ETH_P_CODEC2_700C	0x7308
#define ETH_P_CODEC2_WB		0x7309
#define ETH_P_CODEC2_450 	0x730a
#define ETH_P_CODEC2_450PWB	0x730b

#define ETH_P_AR_CONTROL	0x7342

#define ETH_P_ALAW		0x7365
#define ETH_P_FPRS		0x7370
#define ETH_P_ULAW		0x7355
#define ETH_P_LE16		0x7373
#define ETH_P_BE16		0x7353

#define ETH_AR_CALL_LEN_MAX	8
#define ETH_AR_CALL_SIZE	9

int eth_ar_call2mac(uint8_t mac[6], char *callsign, int ssid, bool multicast);
int eth_ar_callssid2mac(uint8_t mac[6], char *callsign, bool multicast);
int eth_ar_mac2call(char *callsign, int *ssid, bool *multicast, uint8_t mac[6]);
int eth_ar_mac_ssid_mask(uint8_t masked_mac[6], const uint8_t mac[6]);

uint8_t eth_ar_dbm_encode(double dbm);
double eth_ar_dbm_decode(uint8_t enc);

#ifdef __cplusplus
}
#endif

#endif /* _INCLUDE_ETH_AR_H_ */
