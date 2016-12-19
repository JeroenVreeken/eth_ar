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

#ifndef _INCLUDE_FREEDV_ETH_H_
#define _INCLUDE_FREEDV_ETH_H_

#include <codec2/freedv_api.h>

static inline uint16_t freedv_eth_mode2type(int mode)
{
	uint16_t type = ETH_P_CODEC2_700;

	switch(mode) {
		case FREEDV_MODE_700:
			type = ETH_P_CODEC2_700;
			break;
		case FREEDV_MODE_700B:
			type = ETH_P_CODEC2_700B;
			break;
		case FREEDV_MODE_2400A:
		case FREEDV_MODE_2400B:
		case FREEDV_MODE_1600:
			type = ETH_P_CODEC2_1300;
			break;
		default:
			break;
	}

	return type;
}

#endif /* _INCLUDE_FREEDV_ETH_H_ */
