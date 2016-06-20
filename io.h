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

#ifndef _INCLUDE_INPUT_H_
#define _INCLUDE_INPUT_H_

#include <stdint.h>
#include <stdbool.h>
#include <poll.h>

int io_init_tty(void);
int io_init_input(char *device, bool inputtoggle);
int io_fs_nr(void);
int io_poll_fill(struct pollfd *fds, int count);

bool io_state_rx_get(void);

int io_handle(struct pollfd *fds, int count, void (*cb_control)(char *));

#endif /* _INCLUDE_INPUT_H_ */
