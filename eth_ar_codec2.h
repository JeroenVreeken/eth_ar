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
#ifndef _INCLUDE_ETH_AR_CODEC2_H_
#define _INCLUDE_ETH_AR_CODEC2_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "eth_ar/eth_ar.h"
#include <codec2/codec2.h>

/* Not really codec2 modes */
#define CODEC2_MODE_ALAW	'A'
#define CODEC2_MODE_ULAW	'U'
#define CODEC2_MODE_LE16	's'
#define CODEC2_MODE_BE16	'S'

#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define CODEC2_MODE_NATIVE16	CODEC2_MODE_LE16
#define ETH_P_NATIVE16		ETH_P_LE16
#else
#define CODEC2_MODE_NATIVE16	CODEC2_MODE_BE16
#define ETH_P_NATIVE16		ETH_P_BE16
#endif

static inline int eth_ar_eth_p_codecmode(uint16_t type)
{
	switch(type) {
		case ETH_P_CODEC2_3200:
			return CODEC2_MODE_3200;
		case ETH_P_CODEC2_2400:
			return CODEC2_MODE_2400;
		case ETH_P_CODEC2_1600:
			return CODEC2_MODE_1600;
		case ETH_P_CODEC2_1400:
			return CODEC2_MODE_1400;
		case ETH_P_CODEC2_1300:
			return CODEC2_MODE_1300;
		case ETH_P_CODEC2_1200:
			return CODEC2_MODE_1200;
		case ETH_P_CODEC2_700:
			return CODEC2_MODE_700;
		case ETH_P_CODEC2_700B:
			return CODEC2_MODE_700B;
		case ETH_P_CODEC2_700C:
			return CODEC2_MODE_700C;
		case ETH_P_ALAW:
			return CODEC2_MODE_ALAW;
		case ETH_P_ULAW:
			return CODEC2_MODE_ULAW;
		case ETH_P_LE16:
			return CODEC2_MODE_LE16;
		case ETH_P_BE16:
			return CODEC2_MODE_BE16;
		default:
			break;
	}
	return -1;
}


static inline bool eth_ar_eth_p_iscodec2(uint16_t type)
{
	switch(type) {
		case ETH_P_CODEC2_3200:
		case ETH_P_CODEC2_2400:
		case ETH_P_CODEC2_1600:
		case ETH_P_CODEC2_1400:
		case ETH_P_CODEC2_1300:
		case ETH_P_CODEC2_1200:
		case ETH_P_CODEC2_700:
		case ETH_P_CODEC2_700B:
		case ETH_P_CODEC2_700C:
			return true;
		default:
			break;
	}
	return false;
}


static inline bool eth_ar_eth_p_isvoice(uint16_t type)
{
	if (eth_ar_eth_p_iscodec2(type))
		return true;

	switch(type) {
		case ETH_P_ALAW:
		case ETH_P_ULAW:
		case ETH_P_LE16:
		case ETH_P_BE16:
			return true;
		default:
			break;
	}
	return false;
}


#ifdef __cplusplus
}
#endif

#endif /* _INCLUDE_ETH_AR_CODEC2_H_ */
