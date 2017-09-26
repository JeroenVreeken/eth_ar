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
	TX_STATE_BEEP1,
	TX_STATE_BEEP2,
	TX_STATE_BEEPD,
};


static bool fullduplex;
static enum tx_state tx_state;
static bool tx_hadvoice;
static int tx_tail;
static int tx_state_cnt;
static struct sound_resample *sr_l, *sr_r = NULL;
static struct ctcss *ctcss = NULL;
static struct beacon *beacon = NULL;
static struct emphasis *emphasis_p = NULL;
static int nr_samples = FREEDV_ALAW_NR_SAMPLES;
static bool output_bb = false;
static bool output_tone = false;
static enum io_hl_ptt ptt = IO_HL_PTT_OFF;

struct beacon_sample *beep_1k;
struct beacon_sample *beep_1k2;
struct beacon_sample *beep_2k;

static int tx_sound_out(int16_t *samples, int16_t *samples_bb, int nr)
{
	if (!sr_l) {
		sound_out_lr(samples, samples_bb, nr);
	} else {
		int nr_out = sound_resample_nr_out(sr_l, nr);
		int16_t hw_mod_out_l[nr_out];
		int16_t hw_mod_out_r[nr_out];
					
		sound_resample_perform(sr_l, hw_mod_out_l, samples, nr_out, nr);
		sound_resample_perform(sr_r, hw_mod_out_r, samples_bb, nr_out, nr);
		sound_out_lr(hw_mod_out_l, hw_mod_out_r, nr_out);
	}
	return 0;
}

static void tx_silence(void)
{
	int16_t buffer[nr_samples];
	int16_t buffer_bb[nr_samples];
	memset(buffer, 0, sizeof(int16_t)*nr_samples);
	memset(buffer_bb, 0, sizeof(int16_t)*nr_samples);
	
	if (tx_hadvoice) {
		if (ctcss) {
			if (output_tone)
				ctcss_add(ctcss, buffer_bb, nr_samples);
			else
				ctcss_add(ctcss, buffer, nr_samples);
		}
		if (emphasis_p)
			emphasis_pre(emphasis_p, buffer, nr_samples);
	}
	tx_sound_out(buffer, buffer_bb, nr_samples);
}

static void tx_beep(void)
{
	struct beacon_sample *bs;
	if (tx_state == TX_STATE_BEEP1) {
		bs = beep_1k;
	} else if (tx_state == TX_STATE_BEEP2) {
		bs = beep_1k2;
	} else {
		bs = beep_2k;
	}
	int16_t buffer[nr_samples];
	int16_t buffer_bb[nr_samples];
	memcpy(buffer, bs->samples + tx_state_cnt * nr_samples, sizeof(int16_t)*nr_samples);
	memset(buffer_bb, 0, sizeof(int16_t)*nr_samples);
	
	if (ctcss) {
		if (output_tone)
			ctcss_add(ctcss, buffer_bb, nr_samples);
		else
			ctcss_add(ctcss, buffer, nr_samples);
	}
	if (emphasis_p)
		emphasis_pre(emphasis_p, buffer, nr_samples);
	tx_sound_out(buffer, buffer_bb, nr_samples);
}

static void tx_voice(void)
{
	struct tx_packet *packet = peek_voice();

	if (!packet) {
		if (!beacon) {
			tx_silence();
		} else {
			int16_t buffer[nr_samples];
			int16_t buffer_bb[nr_samples];
			memset(buffer_bb, 0, sizeof(int16_t)*nr_samples);
			
			beacon_generate(beacon, buffer, nr_samples);
			if (tx_hadvoice) {
				if (ctcss) {
					if (output_tone)
						ctcss_add(ctcss, buffer_bb, nr_samples);
					else
						ctcss_add(ctcss, buffer, nr_samples);
				}
			}
			if (emphasis_p)
				emphasis_pre(emphasis_p, buffer, nr_samples);
			tx_sound_out(buffer, buffer_bb, nr_samples);
		}
	} else {
		int nr = packet->len / sizeof(short);
		int16_t buffer[nr];
		int16_t buffer_bb[nr];

		tx_hadvoice = true;
		memcpy(buffer, packet->data, packet->len);
		if (output_bb)
			memcpy(buffer_bb, packet->data, packet->len);
		else
			memset(buffer_bb, 0, packet->len);
		
		if (beacon)
			beacon_generate_add(beacon, buffer, nr);
		if (emphasis_p)
			emphasis_pre(emphasis_p, buffer, nr);
		if (ctcss) {
			if (output_tone)
				ctcss_add(ctcss, buffer_bb, nr);
			else
				ctcss_add(ctcss, buffer, nr);
		}
		tx_sound_out(buffer, buffer_bb, nr);

		packet = dequeue_voice();
		tx_packet_free(packet);
	}
}

void freedv_eth_txa_state_machine(void)
{
	enum io_hl_ptt new_ptt = ptt;
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
				new_ptt = IO_HL_PTT_OTHER;
				tx_state = TX_STATE_ON;
				tx_state_cnt = 0;
				if (ctcss)
					ctcss_reset(ctcss);
			} else {
				tx_silence();
				break;
			}
		case TX_STATE_ON:
			if (!queue_voice_filled() && !bcn) {
				tx_state_cnt = 0;
				if (io_hl_aux2_get()) {
					tx_state = TX_STATE_BEEP1;
				} else if (io_dmlassoc_get()) {
					tx_state = TX_STATE_BEEPD;
				} else {
					tx_state = TX_STATE_TAIL;
				}
			} else {
				tx_voice();
				break;
			}
		case TX_STATE_BEEP1:
		case TX_STATE_BEEP2:
		case TX_STATE_BEEPD:
			if (queue_voice_filled() || bcn) {
					tx_state = TX_STATE_ON;
					tx_state_cnt = 0;
					
					tx_voice();
					break;
			}
			if (tx_state == TX_STATE_BEEP1) {
				if (tx_state_cnt >= beep_1k->nr / nr_samples) {
					tx_state_cnt = 0;
					if (io_hl_aux3_get()) {
						tx_state = TX_STATE_BEEP2;
					} else {
						tx_state = TX_STATE_TAIL;
					}
				} else {
					tx_beep();
					break;
				}
			}
			if (tx_state == TX_STATE_BEEP2) {
				if (tx_state_cnt >= beep_1k2->nr / nr_samples) {
					tx_state_cnt = 0;
					tx_state = TX_STATE_TAIL;
				} else {
					tx_beep();
					break;
				}
			}
			if (tx_state == TX_STATE_BEEPD) {
				if (tx_state_cnt >= beep_2k->nr / nr_samples) {
					tx_state_cnt = 0;
					tx_state = TX_STATE_TAIL;
				} else {
					tx_beep();
					break;
				}
			}
		case TX_STATE_TAIL:
			if (tx_state_cnt >= tx_tail) {
				tx_state = TX_STATE_OFF;
				tx_state_cnt = 0;
				tx_hadvoice = false;
				new_ptt = IO_HL_PTT_OFF;
				
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
			break;
	}

	if (new_ptt != IO_HL_PTT_OFF && tx_hadvoice)
		new_ptt = IO_HL_PTT_AUDIO;
	if (new_ptt != ptt) {
		io_hl_ptt_set(new_ptt);
		ptt = new_ptt;
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
    bool emphasis,
    bool init_output_bb,
    bool init_output_tone)
{
	int a_rate = FREEDV_ALAW_RATE;

	beep_1k = beacon_beep_create(a_rate, 1000.0, 0.15, 0.25, 0.25);
	beep_1k2 = beacon_beep_create(a_rate, 1200.0, 0.05, 0.15, 0.25);
	beep_2k = beacon_beep_create(a_rate, 2000.0, 0.15, 0.25, 0.25);

	fullduplex = init_fullduplex;
	output_bb = init_output_bb;
	output_tone = init_output_tone;
	
	tx_state = TX_STATE_OFF;
	io_hl_ptt_set(false);

	tx_state_cnt = 0;
	tx_hadvoice = false;

	int period_msec = 1000 / (FREEDV_ALAW_RATE / nr_samples);
	printf("TX period: %d msec\n", period_msec);

	tx_tail = tx_tail_msec / period_msec;
	printf("TX tail: %d periods\n", tx_tail);

	sound_resample_destroy(sr_l);
	sound_resample_destroy(sr_r);
	if (a_rate != hw_rate) {
		sr_l = sound_resample_create(hw_rate, a_rate);
		sr_r = sound_resample_create(hw_rate, a_rate);
	} else {
		sr_l = NULL;
		sr_r = NULL;
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

