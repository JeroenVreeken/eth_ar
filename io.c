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
#include <termios.h>
#include <string.h>

static RIG *rig;
static int dcd_level = 0;
static int dcd_threshold = 1;
static ptt_type_t ptt_type = RIG_PTT_NONE;
static dcd_type_t dcd_type = RIG_DCD_NONE;

static bool toggle;
static bool input_state = false;
static int fd_input = -1;

int io_init_input(char *device, bool inputtoggle)
{
	fd_input = open(device, O_RDONLY);
	
	printf("open %s: %d\n", device, fd_input);
	
	if (fd_input >= 0)
		ioctl(fd_input, EVIOCGRAB, (void*)1);

	toggle = inputtoggle;

	return fd_input;
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
	
	struct termios attribs;

	if(tcgetattr(STDIN_FILENO, &attribs) < 0) {
		perror("stdin");
		return -1;
	}

	attribs.c_lflag &= ~(ICANON | ECHO | ECHONL);

	if(tcsetattr(STDIN_FILENO, TCSANOW, &attribs) < 0) {
		perror("stdin");
		return -1;
	}
 
	return 0;
}

static int tty_rx = false;

bool io_state_rx_get(void)
{
	return input_state || tty_rx;
}

int io_handle(struct pollfd *fds, int count, void (*cb_control)(char *))
{
	int nr = 0;

	if (fd_input >= 0 && nr < count) {
		if (fds[nr].revents == POLLIN) {
		printf("io input\n");
			input_handle(fd_input);
		}
		nr++;
	}
	if (fd_tty >= 0 && nr < count) {
		if (fds[nr].revents == POLLIN) {
			ssize_t r;
			char buffer[2];
	
			r = read(0, buffer, 1);
			if (r == 1) {
				if (buffer[0] == '\n') {
					tty_rx = ! tty_rx;
					printf("tty DCD input: %d\n", tty_rx);
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


bool io_hl_dcd_get(void)
{
	dcd_t dcd;

	if (dcd_type == RIG_DCD_NONE)
		return false;

	rig_get_dcd(rig, RIG_VFO_CURR, &dcd);
	if (dcd == RIG_DCD_ON)
		dcd_level++;
	else
		dcd_level = 0;

	return dcd_level >= dcd_threshold;
}

void io_hl_ptt_set(enum io_hl_ptt state)
{
	ptt_t pstate;
	
	switch (state) {
		case IO_HL_PTT_AUDIO:
			pstate = RIG_PTT_ON_MIC;
			break;
		case IO_HL_PTT_OTHER:
			pstate = RIG_PTT_ON_DATA;
			break;
		case IO_HL_PTT_OFF:
		default:
			pstate = RIG_PTT_OFF;
			break;
	}

	rig_set_ptt(rig, RIG_VFO_CURR, pstate);

	if (pstate == RIG_PTT_OFF && dcd_level <= 0) {
		/* make dcd insensitive for a little while */
		dcd_level = -dcd_threshold;
	}
}

int io_hl_init(rig_model_t rig_model, int dcd_th, ptt_type_t ptt, char *ptt_file, dcd_type_t dcd)
{
	int retcode;
	ptt_type = ptt;
	dcd_type = dcd;

	dcd_threshold = dcd_th;
	
	rig = rig_init(rig_model);
	if (!rig) {
		printf("Could not init rig\n");
		return -1;
	}

	if (ptt_type != RIG_PTT_NONE)
		rig->state.pttport.type.ptt = ptt_type;

	if (dcd_type != RIG_DCD_NONE)
		rig->state.dcdport.type.dcd = dcd_type;

	if (ptt_file)
		strncpy(rig->state.pttport.pathname, ptt_file, FILPATHLEN - 1);
	if (ptt_file)
		strncpy(rig->state.dcdport.pathname, ptt_file, FILPATHLEN - 1);

	retcode = rig_open(rig);
	if (retcode != RIG_OK) {
	  	fprintf(stderr,"rig_open: error = %s \n", rigerror(retcode));
		return -2;
	}

	rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_OFF);

	return 0;
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
		fds[nr].events = POLLIN;
		nr++;
	}
	if (fd_tty >= 0 && nr < count) {
		fds[nr].fd = fd_tty;
		fds[nr].events = POLLIN;
		nr++;
	}
	return 0;
}

