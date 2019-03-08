/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2019

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
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "eth_ar/eth_ar.h"
#include "eth_ar_codec2.h"
#include "interface.h"
#include "beacon.h"

#define RATE 8000
#define NR_SAMPLES 160

uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
uint8_t mac[6];

uint8_t transmission = 42;
uint8_t level = 30;

int main(int argc, char **argv)
{
	char *netname = "test";
	char *call = "test";

	if (eth_ar_callssid2mac(mac, call, false)) {
		printf("Callsign could not be converted to a valid MAC address\n");
		return -1;
	}
	int fd_int = interface_init(netname, mac, true, 0);
	if (fd_int < 0) {
		printf("Could not create interface\n");
		return -1;
	}

	struct beacon_sample *beep_1k, *beep_2k, *b;
	
	beep_1k = beacon_beep_create(RATE, 1.0, 0, 1.0, 0.25);
	beep_2k = beacon_beep_create(RATE, 2.0, 0, 1.0, 0.25);

	int beacon = 0;
	while (1) {
		sleep(5);
		
		if (beacon & 1) {
			b = beep_1k;
		} else {
			b = beep_2k;
		}
		
		printf("Sending beep\n");
		
		int nr;
		for (nr = 0; nr < b->nr; nr += NR_SAMPLES) {
			interface_rx(bcast, mac, ETH_P_NATIVE16, 
			    (uint8_t*)(b->samples+nr), 2*NR_SAMPLES,
			    transmission, level);
		}
	}

	return 0;
}
