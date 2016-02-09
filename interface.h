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

int interface_rx(uint8_t *data, size_t len);
int interface_tx(int (*cb)(uint8_t *data, size_t len));
int interface_init(char *name, uint8_t mac[6], uint16_t type);

#endif /* _INCLUDE_INTERFACE_H_ */
