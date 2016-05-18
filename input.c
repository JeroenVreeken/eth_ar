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
#include "input.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <linux/input.h>

static bool toggle;
static bool state = false;

int input_init(char *device, bool inputtoggle)
{
	int fd = open(device, O_RDONLY);
	
	printf("open %s: %d\n", device, fd);
	
	if (fd >= 0)
		ioctl(fd, EVIOCGRAB, (void*)1);

	toggle = inputtoggle;

	return fd;
}

int input_handle(int fd, void (*cb)(bool state))
{
	struct input_event ev;
	ssize_t r;
	
	r = read(fd, &ev, sizeof(ev));
	if (r == sizeof(ev)) {
		if (ev.type == EV_KEY) {
			if (!toggle) {
				cb(ev.value);
			} else {
				if (ev.value) {
					state = !state;
					cb(state);
				}
			}
		}
	} else {
		printf("input r: %zd\n", r);
	}

	return 0;
}


