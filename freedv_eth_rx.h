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

#ifndef _INCLUDE_FREEDV_ETH_RX_H_
#define _INCLUDE_FREEDV_ETH_RX_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <codec2/freedv_api.h>
#include <eth_ar/eth_ar.h>

int freedv_eth_rx_init(struct freedv *freedv, uint8_t mac[6], int hw_rate);
void freedv_eth_rx(int16_t *samples, int nr);
bool freedv_eth_rx_cdc(void);

void freedv_eth_rx_vc_callback(void *arg, char c);
void freedv_eth_rx_cb_datarx(void *arg, unsigned char *packet, size_t size);

void freedv_eth_symrx(signed char *rxsym);


#endif /* _INCLUDE_FREEDV_ETH_RX_H_ */
