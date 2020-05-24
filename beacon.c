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

#include "beacon.h"
#include <stdio.h>

#include <string.h>
#include <math.h>
#include <ctype.h>

/* Table used to produce callsign beacon */
static char *beacon_morsecode[][2] = {
	{ "a", ".-" },
	{ "b", "-..." },
	{ "c", "-.-." },
	{ "d", "-.." },
	{ "e", "." },
	{ "f", "..-." },
	{ "g", "--." },
	{ "h", "...." },
	{ "i", ".." },
	{ "j", ".---" },
	{ "k", "-.-" },
	{ "l", ".-.." },
	{ "m", "--" },
	{ "n", "-." },
	{ "o", "---" },
	{ "p", ".--." },
	{ "q", "--.-" },
	{ "r", ".-." },
	{ "s", "..." },
	{ "t", "-" },
	{ "u", "..-" },
	{ "v", "...-" },
	{ "w", ".--" },
	{ "x", "-..-" },
	{ "y", "-.--" },
	{ "z", "--.." },
	{ "0", "-----" },
	{ "1", ".----" },
	{ "2", "..---" },
	{ "3", "...--" },
	{ "4", "....-" },
	{ "5", "....." },
	{ "6", "-...." },
	{ "7", "--..." },
	{ "8", "---.." },
	{ "9", "----." },
	{ ".", ".-.-.-" },
	{ ",", "--..--" },
	{ ":", "---..." },
	{ ";", "-.-.-." },
	{ "$", "...-..-" },
	{ "?", "..--.." },
	{ "'", ".----." },
	{ "-", "-...-" },
	{ "+", ".-.-." },
	{ "_", "..--.-" },
	{ "/", "-..-." },
	{ "(", "-.--." },
	{ ")", "-.--.-" },
	{ "\"", ".-..-." },
	{ "@", ".--.-." },
	{ " ", " " }, /* must be last */
};

#define MORSE_WPM	20
#define MORSE_PARIS_DOTS	50
#define MORSE_SINE_FREQ		500
#define MORSE_SINE_AMP		4096
#define MORSE_SINE_MUL_SILENCE	4
#define MORSE_FACTOR_DASH	3
#define MORSE_FACTOR_IGAP	1
#define MORSE_FACTOR_LGAP	2 /* I+L = 3 */
#define MORSE_FACTOR_WGAP	3 /* I+L+W+I = 7 */
static int16_t *morse_dot;
static int morse_dot_size;
static int16_t *morse_dash;
static int morse_dash_size;
static int16_t *morse_igap;
static int morse_igap_size;
static int16_t *morse_lgap;
static int morse_lgap_size;
static int16_t *morse_wgap;
static int morse_wgap_size;

enum morse_state {
	MORSE_STATE_NONE,
	MORSE_STATE_WGAP,
	MORSE_STATE_DOT,
	MORSE_STATE_DASH,
	MORSE_STATE_IGAP,
	MORSE_STATE_LGAP,
};


struct beacon {
	enum morse_state state;
	int pos;
	int lpos;
	int lindex;
	int16_t *element;
	int element_pos;
	int element_size;
	
	int rate;
	int state_interval;
	long long int interval;
	char *msg;

	int cnt;
};

int beacon_morsecode_index(char c)
{
	int i = 0;
	
	c = tolower(c);
	while (beacon_morsecode[i][0][0] != c) {
		i++;
		if (beacon_morsecode[i][0][0] == ' ')
			return i;
	}
	return i;
}

static int16_t double2int16(double v)
{
	if (v > 32767)
		return 32767;
	if (v < -32768)
		return -32768;
	return v;
}

struct beacon *beacon_init(int rate, int state_interval, int beacon_interval, char *message)
{
	struct beacon *beacon;
	int i;
	
	beacon = calloc(1, sizeof(struct beacon));
	if (!beacon)
		return NULL;

	beacon->rate = rate;
	beacon->state_interval = state_interval;
	beacon->interval = (long long)beacon_interval * rate;
	beacon->msg = strdup(message);
	/* Trigger the first after 1 second */
	beacon->cnt = beacon->interval - rate;

	if (!morse_dot) {
		morse_dot_size = (60 * rate) / (MORSE_PARIS_DOTS * MORSE_WPM);
		morse_dash_size = morse_dot_size * MORSE_FACTOR_DASH;
		morse_igap_size = morse_dot_size * MORSE_FACTOR_IGAP;
		morse_lgap_size = morse_dot_size * MORSE_FACTOR_LGAP;
		morse_wgap_size = morse_dot_size * MORSE_FACTOR_WGAP;
	
		morse_dot = malloc(morse_dot_size * sizeof(int16_t));
		morse_dash = malloc(morse_dash_size * sizeof(int16_t));
		morse_igap = calloc(sizeof(int16_t), morse_igap_size);
		morse_lgap = calloc(sizeof(int16_t), morse_lgap_size);
		morse_wgap = calloc(sizeof(int16_t), morse_wgap_size);
		for (i = 0; i < morse_dot_size; i++) {
			morse_dot[i] = double2int16(sin((M_PI*2*i)/(rate/MORSE_SINE_FREQ))*MORSE_SINE_AMP);
		}
		for (i = 0; i < morse_dash_size; i++) {
			morse_dash[i] = double2int16(sin((M_PI*2*i)/(rate/MORSE_SINE_FREQ))*MORSE_SINE_AMP);
		}

	}

	return beacon;
}

void beacon_destroy(struct beacon *beacon)
{
	if (!beacon)
		return;
	free(morse_igap);
	free(morse_lgap);
	free(morse_wgap);
	free(morse_dash);
	free(morse_dot);
	morse_dot = NULL;
	free(beacon);
}

bool beacon_state_check(struct beacon *beacon)
{
	beacon->cnt += beacon->state_interval;

	return beacon->cnt >= beacon->interval;
}

int beacon_generate(struct beacon *beacon, int16_t *sound, int nr)
{
	int i;
	
	memset(sound, 0, nr * sizeof(int16_t));
	beacon_generate_add(beacon, sound, nr);
	
	for (i = 0; i < nr; i++) {
		int val = sound[i] * MORSE_SINE_MUL_SILENCE;
		if (val > 32767)
			val = 32767;
		if (val < -32768)
			val = -32768;
		sound[i] = val;
	}
	
	return 0;
}

int beacon_generate_add(struct beacon *beacon, int16_t *sound, int nr)
{
	if (beacon->state == MORSE_STATE_NONE) {
		if (beacon->cnt < beacon->interval)
			return 0;
		beacon->state = MORSE_STATE_WGAP;
		beacon->element = morse_wgap;
		beacon->element_size = morse_wgap_size;
		beacon->element_pos = 0;
		beacon->pos = -1;
		beacon->lpos = 0;
		beacon->lindex = beacon_morsecode_index(' ');
	}

	while (nr) {
		char c;
		int copy = nr;
		if (beacon->element_size - beacon->element_pos < nr) {
			copy = beacon->element_size - beacon->element_pos;
		}
		
		int i;
		for (i = 0; i < copy; i++) {
			int sample = sound[i];
			sample += beacon->element[beacon->element_pos + i];
			if (sample > 32767)
				sample = 32767;
			if (sample < -32768)
				sample = -32768;
			sound[i] = sample;
		}
		sound += copy;
		nr -= copy;
		beacon->element_pos += copy;

		if (beacon->element_pos >= beacon->element_size) {
			beacon->element_pos = 0;
			switch(beacon->state) {
				case MORSE_STATE_WGAP:
				case MORSE_STATE_DOT:
				case MORSE_STATE_DASH:
					beacon->state = MORSE_STATE_IGAP;
					beacon->element = morse_igap;
					beacon->element_size = morse_igap_size;
					break;
				case MORSE_STATE_IGAP:
					beacon->lpos++;
					c = beacon_morsecode[beacon->lindex][1][beacon->lpos];
					if (!c) {
						beacon->state = MORSE_STATE_LGAP;
						beacon->element = morse_lgap;
						beacon->element_size = morse_lgap_size;
					} else {
						if (c == '.') {
							beacon->state = MORSE_STATE_DOT;
							beacon->element = morse_dot;
							beacon->element_size = morse_dot_size;	
						} else if (c == '-') {
							beacon->state = MORSE_STATE_DASH;
							beacon->element = morse_dash;
							beacon->element_size = morse_dash_size;			
						} else {
							beacon->state = MORSE_STATE_WGAP;
							beacon->element = morse_wgap;
							beacon->element_size = morse_wgap_size;			
						}
					}
					break;
				case MORSE_STATE_LGAP:
					beacon->pos++;
					if (beacon->pos >= strlen(beacon->msg)) {
						beacon->state = MORSE_STATE_NONE;
						beacon->cnt = 0;
						break;
					} else {
						beacon->lindex = beacon_morsecode_index(
						    beacon->msg[beacon->pos]);
						beacon->lpos = 0;
					}
					c = beacon_morsecode[beacon->lindex][1][beacon->lpos];
					if (c == '.') {
						beacon->state = MORSE_STATE_DOT;
						beacon->element = morse_dot;
						beacon->element_size = morse_dot_size;	
					} else if (c == '-') {
						beacon->state = MORSE_STATE_DASH;
						beacon->element = morse_dash;
						beacon->element_size = morse_dash_size;			
					} else {
						beacon->state = MORSE_STATE_WGAP;
						beacon->element = morse_wgap;
						beacon->element_size = morse_wgap_size;			
					}

					break;
				case MORSE_STATE_NONE:
					nr = 0;
					break;
			}
		}
	}
	
	return 0;
}


struct beacon_sample *beacon_beep_create(int rate, double f, double t_off, double t_on, double amp)
{
	size_t nr_off = rate * t_off;
	size_t nr_on = rate * t_on;
	size_t nr = nr_on + nr_off;
	int i;
	
	struct beacon_sample *bs = calloc(sizeof(struct beacon_sample), 1);
	if (!bs)
		return NULL;
	
	bs->samples = calloc(sizeof(int16_t), nr);
	bs->nr = nr;
	if (!bs->samples) {
		free(bs);
		return NULL;
	}
	
	for (i = 0; i < nr_on; i++) {
		bs->samples[nr_off + i] = double2int16(sin((M_PI*2*i)/(rate/f))*(amp * 16384));
	}
	
	return bs;
}
