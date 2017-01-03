/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2017

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

#include "freedv_eth.h"
#include "sound.h"
#include "io.h"
#include "ctcss.h"
#include "beacon.h"
#include "emphasis.h"

#include <stdio.h>
#include <string.h>

enum tx_state {
	TX_STATE_OFF,
	TX_STATE_ON,
	TX_STATE_TAIL,
};


static bool fullduplex;
static enum tx_state tx_state;
static bool tx_hadvoice;
static int tx_tail;
static int tx_state_cnt;
static struct sound_resample *sr = NULL;
static struct ctcss *ctcss = NULL;
static struct beacon *beacon = NULL;
static struct emphasis *emphasis_p = NULL;
static int nr_samples = FREEDV_ALAW_NR_SAMPLES;

static int tx_sound_out(int16_t *samples, int nr)
{
	if (!sr) {
		sound_out(samples, nr, true, true);
	} else {
		int nr_out = sound_resample_nr_out(sr, nr);
		int16_t hw_mod_out[nr_out];
					
		sound_resample_perform(sr, hw_mod_out, samples, nr_out, nr);
		sound_out(hw_mod_out, nr_out, true, true);
	}
	return 0;
}

static void tx_silence(void)
{
	int16_t buffer[nr_samples];
	memset(buffer, 0, sizeof(int16_t)*nr_samples);
	
	if (tx_hadvoice) {
		if (ctcss)
			ctcss_add(ctcss, buffer, nr_samples);
		if (emphasis_p)
			emphasis_pre(emphasis_p, buffer, nr_samples);
	}
	tx_sound_out(buffer, nr_samples);
}

static void tx_voice(void)
{
	struct tx_packet *packet = peek_voice();

	if (!packet) {
		if (!beacon) {
			tx_silence();
		} else {
			int16_t buffer[nr_samples];
			
			beacon_generate(beacon, buffer, nr_samples);
			if (tx_hadvoice) {
				if (ctcss)
					ctcss_add(ctcss, buffer, nr_samples);
			}
			if (emphasis_p)
				emphasis_pre(emphasis_p, buffer, nr_samples);
			tx_sound_out(buffer, nr_samples);
		}
	} else {
		int nr = packet->len / sizeof(short);
		int16_t buffer[nr];
		
		tx_hadvoice = true;
		memcpy(buffer, packet->data, packet->len);
		
		if (beacon)
			beacon_generate_add(beacon, buffer, nr);
		if (ctcss)
			ctcss_add(ctcss, buffer, nr);
		if (emphasis_p)
			emphasis_pre(emphasis_p, buffer, nr);
		tx_sound_out(buffer, nr);

		packet = dequeue_voice();
		tx_packet_free(packet);
	}
}

void freedv_eth_txa_state_machine(void)
{
	tx_state_cnt++;
	bool bcn;
	if (beacon) {
		bcn = beacon_state_check(beacon);
		bcn = bcn && (!freedv_eth_cdc() || fullduplex);
	} else {
		bcn = false;
	}
	
	switch (tx_state) {
		case TX_STATE_OFF:
			if (queue_voice_filled() || bcn) {
				tx_state = TX_STATE_ON;
				io_hl_ptt_set(true);
				tx_state_cnt = 0;
				if (ctcss)
					ctcss_reset(ctcss);
			} else {
				tx_silence();
				break;
			}
		case TX_STATE_ON:
			if (!queue_voice_filled() && !bcn) {
				tx_state = TX_STATE_TAIL;
				tx_state_cnt = 0;
			} else {
				tx_voice();
				break;
			}
		case TX_STATE_TAIL:
			if (tx_state_cnt >= tx_tail) {
				tx_state = TX_STATE_OFF;
				tx_state_cnt = 0;
				tx_hadvoice = false;
				io_hl_ptt_set(false);
				
				tx_silence();
			} else {
				if (queue_voice_filled() || bcn) {
					tx_state = TX_STATE_ON;
					tx_state_cnt = 0;
					
					tx_voice();
				} else {
					tx_silence();
				}
			}
	}
}

bool freedv_eth_txa_ptt(void)
{
	return tx_state != TX_STATE_OFF;
}

int freedv_eth_txa_init(bool init_fullduplex, int hw_rate, 
    int tx_tail_msec, 
    double ctcss_f, double ctcss_amp,
    int beacon_interval, char *beacon_msg,
    bool emphasis)
{
	int a_rate = FREEDV_ALAW_RATE;

	fullduplex = init_fullduplex;
	
	tx_state = TX_STATE_OFF;
	io_hl_ptt_set(false);

	tx_state_cnt = 0;
	tx_hadvoice = false;

	int period_msec = 1000 / (FREEDV_ALAW_RATE / nr_samples);
	printf("TX period: %d msec\n", period_msec);

	tx_tail = tx_tail_msec / period_msec;
	printf("TX tail: %d periods\n", tx_tail);

	sound_resample_destroy(sr);
	if (a_rate != hw_rate) {
		sr = sound_resample_create(hw_rate, a_rate);
	} else {
		sr = NULL;
	}
	
	ctcss_destroy(ctcss);
	ctcss = NULL;
	if (ctcss_f != 0.0) {
		ctcss = ctcss_init(a_rate, ctcss_f, ctcss_amp);
	}

	beacon_destroy(beacon);
	beacon = NULL;
	if (beacon_interval) {
		beacon = beacon_init(a_rate, nr_samples, beacon_interval, beacon_msg);
	}

	emphasis_destroy(emphasis_p);
	emphasis_p = NULL;
	if (emphasis)
		emphasis_p = emphasis_init();

	return 0;
}
