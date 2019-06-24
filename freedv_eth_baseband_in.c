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
#include "freedv_eth_config.h"
#include "io.h"
#include "dtmf.h"
#include "emphasis.h"
#include "interface.h"
#include "eth_ar/alaw.h"
#include "sound.h"
#include "ctcss.h"
#include "eth_ar_codec2.h"

#include <string.h>
#include <speex/speex_preprocess.h>

static uint8_t mac[6];
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static bool cdc = false;
static struct sound_resample *sr = NULL;
static float rx_gain = 1.0;
SpeexPreprocessState *st;

static uint8_t transmission = 0;
static double level_dbm = -10;

bool freedv_eth_baseband_in_cdc(void)
{
	return cdc;
}


void freedv_eth_baseband_in(int16_t *samples, int nr)
{
	int nr_a = sound_resample_nr_out(sr, nr);
	int16_t mod_a[nr_a];

	if (st)
		speex_preprocess_run(st, samples);

	sound_resample_perform_gain_limit(sr, mod_a, samples, nr_a, nr, rx_gain);

	bool new_cdc = io_hl_aux2_get();
	if (!new_cdc && cdc) {
		queue_voice_end(transmission);
		transmission++;
	}
	cdc = new_cdc;

	if (cdc) {
		freedv_eth_voice_rx(bcast, mac, ETH_P_NATIVE16, 
		    (uint8_t *)mod_a, nr_a * sizeof(int16_t), false,
		    transmission, level_dbm);
	}
}

int freedv_eth_bb_in_init(int hw_rate, uint8_t mac_init[6], int nr_hw)
{
	int a_rate = FREEDV_ALAW_RATE;
	
	bool denoise = atoi(freedv_eth_config_value("baseband_in_denoise", NULL, "1"));
	rx_gain = atof(freedv_eth_config_value("baseband_in_gain", NULL, "1.0"));

	memcpy(mac, mac_init, 6);
	printf("Baseband in gain: %f\n", rx_gain);

	cdc = io_hl_aux2_get();

	sound_resample_destroy(sr);
	sr = sound_resample_create(a_rate, hw_rate);

	if (denoise) {
		printf("Denoise and AGC active\n");
		int val;
		float fval;
		st = speex_preprocess_state_init(nr_hw, hw_rate);
		val= denoise;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_DENOISE, &val);
		val = -30;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &val);

		val=1;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC, &val);
		fval=32768 / rx_gain;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_LEVEL, &fval);
		
		val = 40;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &val);
		
 		val=60;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &val);
	} else {
		printf("No denoise and AGC\n");
	}


	return 0;
}
