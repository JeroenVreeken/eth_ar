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

static struct emphasis *emphasis_d = NULL;
static uint8_t mac[ETH_AR_MAC_SIZE];
static uint8_t bcast[ETH_AR_MAC_SIZE] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static bool dtmf_initialized = false;
static bool cdc;
static bool ctcss_sql;
static int dtmf_mute = 1;
static float rx_gain = 1.0;
static float limit = 1.0;
static char dtmf_control_start = '*';
static char dtmf_control_stop = '#';
static int rxa_dcd_threshold = 0;
static int rxa_dcd_cnt = 0;

static uint8_t transmission = 0;
static double level_dbm = -60;

enum dtmf_state {
	DTMF_IDLE,
	DTMF_CONTROL,
	DTMF_CONTROL_TAIL,
};

static enum dtmf_state dtmf_state = DTMF_IDLE;

static SpeexPreprocessState *st = NULL;

bool freedv_eth_rxa_cdc(void)
{
	return cdc;
}



static void cb_control(char *ctrl)
{
	uint8_t *msg = (uint8_t *)ctrl;
	
	if (ctrl[0] == dtmf_control_start) {
		dtmf_state = DTMF_CONTROL;
	} else if (ctrl[0] == dtmf_control_stop) {
		dtmf_state = DTMF_CONTROL_TAIL;
	} else {
		if (dtmf_state == DTMF_CONTROL_TAIL)
			dtmf_state = DTMF_IDLE;
	}
	printf("DTMF: %s\n", ctrl);
	
	interface_rx(bcast, mac, ETH_P_AR_CONTROL, msg, strlen(ctrl), 0, 1);
}


void freedv_eth_rxa(int16_t *samples, int nr)
{
	bool detected;
	bool new_cdc = false;
	bool skip_prep = false;
	
	if (!ctcss_sql) {
		new_cdc = io_hl_dcd_get();
		skip_prep = !new_cdc;
	}

	if (cdc) {
		dtmf_rx(samples, nr, cb_control, &detected);
		if (detected) {
			if ((dtmf_mute == 1) ||
			    (dtmf_mute == 2 && dtmf_state == DTMF_CONTROL) ||
			    (dtmf_mute == 2 && dtmf_state == DTMF_CONTROL_TAIL))
				memset(samples, 0, nr * sizeof(int16_t)); 
		}
	}

	if (st && !skip_prep)
		speex_preprocess_run(st, samples);

	sound_gain_limit(samples, nr, rx_gain, &limit);

	if (emphasis_d)
		emphasis_de(emphasis_d, samples, nr);
	if (ctcss_sql) {
		new_cdc = ctcss_detect_rx(samples, nr);
	}
	if (cdc && !new_cdc) {
		queue_voice_end(transmission);
		transmission++;
	}

	if (!new_cdc) {
		rxa_dcd_cnt = -rxa_dcd_threshold;
		cdc = false;
	} else {
		if (rxa_dcd_cnt >= 0) {
			cdc = true;
		}
		rxa_dcd_cnt += nr;
	}
	cdc = new_cdc;

	if (cdc) {
		freedv_eth_voice_rx(bcast, mac, ETH_P_NATIVE16, (uint8_t *)samples, nr * sizeof(int16_t), true, transmission, level_dbm);
	} else {
		dtmf_state = DTMF_IDLE;
	}
}

int freedv_eth_rxa_init(int hw_rate, uint8_t mac_init[ETH_AR_MAC_SIZE], int hw_nr)
{
	double ctcss_freq = atof(freedv_eth_config_value("analog_rx_ctcss_frequency", NULL, "0.0"));
	bool emphasis = atoi(freedv_eth_config_value("analog_rx_emphasis", NULL, "0"));
	float rx_gain_init = atof(freedv_eth_config_value("analog_rx_gain", NULL, "1.0"));
	int dtmf_mute_init = atoi(freedv_eth_config_value("analog_dtmf_mute", NULL, "1"));

	rx_gain = rx_gain_init;
	memcpy(mac, mac_init, 6);
	printf("RXA rx_gain: %f\n", rx_gain);

	int msec_samples = hw_rate / 1000;
	printf("RXA samples per msec: %d\n", msec_samples);

	int rxa_delay_msec = atoi(freedv_eth_config_value("rx_delay", NULL, "0"));
	rxa_dcd_threshold = rxa_delay_msec * msec_samples;
	printf("RXA rx delay samples: %d\n", rxa_dcd_threshold);

	rxa_dcd_cnt = -rxa_dcd_threshold;
	cdc = false;

	if (!dtmf_initialized) {
		dtmf_init(hw_rate);
		dtmf_initialized = true;
	}
	dtmf_mute = dtmf_mute_init;
	
	emphasis_destroy(emphasis_d);
	emphasis_d = NULL;
	if (emphasis)
		emphasis_d = emphasis_init();

	if (ctcss_freq > 0.0) {
		ctcss_detect_init(ctcss_freq, hw_rate);
		ctcss_sql = true;
	} else {
		ctcss_sql = false;
	}

	bool denoise = atoi(freedv_eth_config_value("analog_rx_denoise", NULL, "1"));
	if (denoise) {
		printf("RXA Analog denoise and AGC active\n");
		spx_int32_t val;
		float fval;
		st = speex_preprocess_state_init(hw_nr, hw_rate);
		val= denoise;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_DENOISE, &val);
		val = -15; //default -15
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &val);

		val=1;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC, &val);
		// Add factor 2.0 since speex seems to aim on half the value
		fval=32768.0 / rx_gain;
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_LEVEL, &fval);
		
		val = 40; // default 12
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &val);
		val = -40; // default -40
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_DECREMENT, &val);
		
 		val=60; // default 30
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &val);

		val= -15; // default -15
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &val);

		/* preprocess info */
		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_DENOISE, &val);
		printf("RXA DENOISE enabled: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_AGC, &val);
		printf("RXA AGC enabled: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_VAD, &val);
		printf("RXA VAD enabled: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_DEREVERB, &val);
		printf("RXA DEREVERB enabled: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_NOISE_SUPPRESS, &val);
		printf("RXA NOISE_SUPPRESS: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_ECHO_SUPPRESS, &val);
		printf("RXA ECHO_SUPPRESS: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_ECHO_SUPPRESS_ACTIVE, &val);
		printf("RXA ECHO_SUPPRESS_ACTIVE: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_AGC_LEVEL, &fval);
		printf("RXA AGC_LEVEL: %f\n", fval);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_AGC_INCREMENT, &val);
		printf("RXA AGC_INCREMENT: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_AGC_DECREMENT, &val);
		printf("RXA AGC_DECREMENT:: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_AGC_MAX_GAIN, &val);
		printf("RXA AGC_MAX_GAIN: %d\n", val);

		speex_preprocess_ctl(st, SPEEX_PREPROCESS_GET_AGC_LEVEL, &fval);
		printf("RXA AGC_LEVEL:: %f\n", fval);
	} else {
		printf("RXA No analog denoise and AGC\n");
	}

	return 0;
}
