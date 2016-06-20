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
#include "io.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <linux/input.h>

static bool toggle;
static bool input_state = false;
static int fd_input = -1;

int io_init_input(char *device, bool inputtoggle)
{
	int fd = open(device, O_RDONLY);
	
	printf("open %s: %d\n", device, fd);
	
	if (fd >= 0)
		ioctl(fd, EVIOCGRAB, (void*)1);

	toggle = inputtoggle;

	return fd;
}

static int input_handle(int fd)
{
	struct input_event ev;
	ssize_t r;
	
	r = read(fd, &ev, sizeof(ev));
	if (r == sizeof(ev)) {
		if (ev.type == EV_KEY) {
			if (!toggle) {
				input_state = ev.value;
			} else {
				if (ev.value) {
					input_state = !input_state;
				}
			}
			printf("DCD input: %d\n", input_state);
		}
	} else {
		printf("input r: %zd\n", r);
	}

	return 0;
}


static int fd_tty = -1;

int io_init_tty(void)
{
	fd_tty = 0;
	
	return 0;
}

static int tty_rx = false;

bool io_state_rx_get(void)
{
	return tty_rx;
}

int io_handle(struct pollfd *fds, int count, void (*cb_control)(char *))
{
	int nr = 0;
	
	if (fd_input >= 0 && nr < count) {
		if (fds[nr].events == POLLIN) {
			input_handle(fd_input);
		}
		nr++;
	}
	if (fd_tty >= 0 && nr < count) {
		if (fds[nr].events == POLLIN) {
			ssize_t r;
			char buffer[2];
	
			r = read(0, buffer, 1);
			if (r == 1) {
				if (buffer[0] == '\n') {
					tty_rx = ! tty_rx;
				} else {
					buffer[1] = 0;
					cb_control(buffer);
				}
			}
		}
		nr++;
	}
	
	return nr;
}


int io_fs_nr(void)
{
	int nr = 0;
	
	if (fd_input >= 0)
		nr++;
	if (fd_tty >= 0)
		nr++;
	
	return nr;
}

int io_poll_fill(struct pollfd *fds, int count)
{
	int nr = 0;
	
	if (fd_input >= 0 && nr < count) {
		fds[nr].fd = fd_input;
		nr++;
	}
	if (fd_tty && nr < count) {
		fds[nr].fd = fd_tty;
		nr++;
	}
	return 0;
}
