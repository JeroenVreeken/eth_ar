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
#include "io.h"
#include "dtmf.h"
#include "emphasis.h"
#include "interface.h"
#include "alaw.h"
#include "sound.h"

#include <string.h>

static struct emphasis *emphasis_d = NULL;
static uint8_t mac[6];
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static bool dtmf_initialized = false;
static bool cdc;
static struct sound_resample *sr = NULL;

bool freedv_eth_rxa_cdc(void)
{
	return cdc;
}

static void cb_control(char *ctrl)
{
	uint8_t *msg = (uint8_t *)ctrl;
	
	printf("DTMF: %s\n", ctrl);
	
	interface_rx(bcast, mac, ETH_P_AR_CONTROL, msg, strlen(ctrl));
}


void freedv_eth_rxa(int16_t *samples, int nr)
{
	int nr_a = sound_resample_nr_out(sr, nr);
	int16_t mod_a[nr_a];

	sound_resample_perform(sr, mod_a, samples, nr_a, nr);

	cdc = io_hl_dcd_get();

	if (emphasis_d)
		emphasis_de(emphasis_d, mod_a, nr_a);
	dtmf_rx(mod_a, nr_a, cb_control);

	uint8_t alaw[nr_a];
		
	alaw_encode(alaw, mod_a, nr_a);
		
	interface_rx(bcast, mac, ETH_P_ALAW, alaw, nr);
}

int freedv_eth_rxa_init(int hw_rate, uint8_t mac_init[6], bool emphasis)
{
	int a_rate = FREEDV_ALAW_RATE;
	
	memcpy(mac, mac_init, 6);

	cdc = false;

	sound_resample_destroy(sr);
	sr = sound_resample_create(a_rate, hw_rate);

	if (!dtmf_initialized) {
		dtmf_init();
		dtmf_initialized = true;
	}
	
	emphasis_destroy(emphasis_d);
	emphasis_d = NULL;
	if (emphasis)
		emphasis_d = emphasis_init();

	return 0;
}
