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
#include <pthread.h>
#include "freedv_eth_config.h"

static RIG *rig;
static int dcd_level = 0;
static int dcd_threshold = 1;
static ptt_type_t ptt_type = RIG_PTT_NONE;
static dcd_type_t dcd_type = RIG_DCD_NONE;
static long token_aux1 = -1;
static long token_aux2 = -1;
static long token_aux3 = -1;

static volatile ptt_t rig_thread_ptt = RIG_PTT_OFF;
static volatile dcd_t rig_thread_dcd = RIG_DCD_OFF;
static volatile bool rig_thread_aux1 = false;
static volatile bool rig_thread_aux2 = false;
static volatile bool rig_thread_aux3 = false;

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

	dcd = rig_thread_dcd;
	if (dcd == RIG_DCD_ON)
		dcd_level++;
	else
		dcd_level = 0;

	return dcd_level >= dcd_threshold;
}

bool io_hl_aux1_get(void)
{
	return rig_thread_aux1;
}
bool io_hl_aux2_get(void)
{
	return rig_thread_aux2;
}
bool io_hl_aux3_get(void)
{
	return rig_thread_aux3;
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

	rig_thread_ptt = pstate;
	__sync_synchronize();

	if (pstate == RIG_PTT_OFF && dcd_level <= 0) {
		/* make dcd insensitive for a little while */
		dcd_level = -dcd_threshold;
	}
}

#define CYCLE_NS (20*1000*1000)
void *io_hl_rig_thread(void *arg)
{
	dcd_t cur_dcd = rig_thread_dcd;
	ptt_t cur_ptt = rig_thread_ptt;
	value_t cur_value;
	struct timespec t_cycle_start;
	struct timespec t_cycle_end;
	struct timespec t_wait;
	
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &t_cycle_start);
		__sync_synchronize();
//		printf("%d %d\n", rig_thread_ptt, cur_ptt);
		if (rig_thread_ptt != cur_ptt) {
			cur_ptt = rig_thread_ptt;
			rig_set_ptt(rig, RIG_VFO_CURR, cur_ptt);
		}
		rig_get_dcd(rig, RIG_VFO_CURR, &cur_dcd);
		if (token_aux1 > 0) {
			if (rig_get_ext_parm(rig, token_aux1, &cur_value) == RIG_OK)
				rig_thread_aux1 = cur_value.i;
		}
		if (token_aux2 > 0) {
			if (rig_get_ext_parm(rig, token_aux2, &cur_value) == RIG_OK)
				rig_thread_aux2 = cur_value.i;
		}
		if (token_aux3 > 0) {
			if (rig_get_ext_parm(rig, token_aux3, &cur_value) == RIG_OK)
				rig_thread_aux3 = cur_value.i;
		}
		rig_thread_dcd = cur_dcd;
		__sync_synchronize();
		clock_gettime(CLOCK_MONOTONIC, &t_cycle_end);

		t_wait.tv_sec = 0;
		t_wait.tv_nsec = CYCLE_NS;
		
		t_wait.tv_nsec -= t_cycle_end.tv_nsec - t_cycle_start.tv_nsec;
		t_wait.tv_nsec -= (t_cycle_end.tv_sec - t_cycle_start.tv_sec) * 1000000000;
		if (t_wait.tv_nsec > CYCLE_NS) {
			/* Clock not monotonic? */
			t_wait.tv_nsec = CYCLE_NS;
		}
		if (t_wait.tv_nsec > 0) {
			nanosleep(&t_wait, NULL);
		}
	}
}

int io_hl_init(rig_model_t rig_model, int dcd_th, ptt_type_t ptt, char *ptt_file, dcd_type_t dcd, char *dcd_file, char *rig_file)
{
	int retcode;
	ptt_type = ptt;
	dcd_type = dcd;

	dcd_threshold = dcd_th;
	
	rig_set_debug(RIG_DEBUG_WARN);

	rig = rig_init(rig_model);
	if (!rig) {
		printf("Could not init rig\n");
		return -1;
	}

	rig->state.pttport.type.ptt = ptt_type;

	rig->state.dcdport.type.dcd = dcd_type;

	if (ptt_file)
		strncpy(rig->state.pttport.pathname, ptt_file, HAMLIB_FILPATHLEN - 1);
	if (dcd_file)
		strncpy(rig->state.dcdport.pathname, dcd_file, HAMLIB_FILPATHLEN - 1);
	if (rig_file)
		strncpy(rig->state.rigport.pathname, rig_file, HAMLIB_FILPATHLEN - 1);

	char *conf_set = NULL;
	while ((conf_set = freedv_eth_config_value("rig_conf_set", conf_set, NULL))) {
		char conf[81];
		strncpy(conf, conf_set, 80);
		conf_set[80] = 0;
		
		char *conf_tok;
		char *conf_val;
		char *svp;
		
		conf_tok = strtok_r(conf, " \t=:", &svp);
		conf_val = strtok_r(NULL, " \t=:", &svp);
		if (conf_tok && conf_val) {
			printf("Conf param: '%s' will be set to: '%s'\n", conf_tok, conf_val);
			long token = rig_token_lookup(rig, conf_tok);
			if (token > 0) {
				if (rig_set_conf(rig, token, conf_val)) {
					printf("Conf param with token %ld could not be set\n", token);
				}
			} else {
				printf("Could not get token for '%s'\n", conf_tok);
			}
		}
	}

	retcode = rig_open(rig);
	if (retcode != RIG_OK) {
	  	fprintf(stderr,"rig_open: error = %s \n", rigerror(retcode));
		return -2;
	}

	char *rig_ctcss_sql = freedv_eth_config_value("rig_ctcss_sql", NULL, NULL);
	if (rig_ctcss_sql) {
		printf("rig ctcss squelch: %s\n", rig_ctcss_sql);
		tone_t tone = atoi(rig_ctcss_sql);
		rig_set_ctcss_sql(rig, RIG_VFO_CURR, tone);
	}
	char *rig_ctcss_tone = freedv_eth_config_value("rig_ctcss_tone", NULL, NULL);
	if (rig_ctcss_tone) {
		printf("rig ctcss tone: %s\n", rig_ctcss_tone);
		tone_t tone = atoi(rig_ctcss_tone);
		rig_set_ctcss_tone(rig, RIG_VFO_CURR, tone);
	}
	char *rig_freq_rx = freedv_eth_config_value("rig_freq_rx", NULL, NULL);
	if (rig_freq_rx) {
		printf("rig frequency for rx: %s\n", rig_freq_rx);
		freq_t freq = atof(rig_freq_rx);
		int r = rig_set_freq(rig, RIG_VFO_RX, freq);
		if (r != RIG_OK) {
			printf("error from rig: %d\n", r);
		}
	}
	char *rig_freq_tx = freedv_eth_config_value("rig_freq_tx", NULL, NULL);
	if (rig_freq_tx) {
		printf("rig frequency for tx: %s\n", rig_freq_tx);
		freq_t freq = atof(rig_freq_tx);
		
		int r = rig_set_split_vfo(rig, RIG_VFO_CURR, RIG_SPLIT_ON, RIG_VFO_TX);
		if (r != RIG_OK) {
			printf("error from rig: %d\n", r);
		}
		
		r = rig_set_freq(rig, RIG_VFO_TX, freq);
		if (r != RIG_OK) {
			printf("error from rig: %d\n", r);
		}
	}

	/* Init to sane status */
	rig_set_ptt(rig, RIG_VFO_CURR, RIG_PTT_OFF);
	rig_get_dcd(rig, RIG_VFO_CURR, (dcd_t*)&rig_thread_dcd);
	
	token_aux1 = rig_ext_token_lookup(rig, "AUX1");
	token_aux2 = rig_ext_token_lookup(rig, "AUX2");
	token_aux3 = rig_ext_token_lookup(rig, "AUX3");
	printf("rig AUX tokens: %ld %ld %ld\n", token_aux1, token_aux2, token_aux3);

	pthread_t rig_thread;
	pthread_create(&rig_thread, NULL, io_hl_rig_thread, NULL);

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

static bool io_dmlassoc = false;

bool io_dmlassoc_get(void)
{
	return io_dmlassoc;	
}

void io_dmlassoc_set(bool val)
{
	if (val != io_dmlassoc) {
		printf("DMLASSOC state: %d -> %d\n", io_dmlassoc, val);
	}
	io_dmlassoc = val;
}

