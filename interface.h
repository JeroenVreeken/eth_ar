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
#ifndef _INCLUDE_INTERFACE_H_
#define _INCLUDE_INTERFACE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "eth_ar/eth_ar.h"

int interface_rx_raw(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len);
int interface_rx(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level);
int interface_tx_raw(int (*cb)(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len));
int interface_tx(int (*cb)(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level));
int interface_init(char *name, uint8_t mac[ETH_AR_MAC_SIZE], bool tap, uint16_t filter_type);
int interface_tx_outgoing(bool enable);

#endif /* _INCLUDE_INTERFACE_H_ */
