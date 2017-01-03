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

#include "freedv_eth_rx.h"
#include "freedv_eth.h"
#include "interface.h"

#include <string.h>
#include <stdio.h>
#include <codec2/codec2.h>

static bool cdc_voice = false;
static int nr_rx;
static float rx_sync = 0;
static bool cdc;

static int16_t *samples_rx = NULL;

static int bytes_per_codec_frame;
static int bytes_per_eth_frame;
static uint16_t eth_type_rx;
static void *silence_packet = NULL;

static uint8_t rx_add[6], mac[6];

#define RX_SYNC_ZERO -3.0
#define RX_SYNC_THRESHOLD 10.0

static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

bool freedv_eth_rx_cdc(void)
{
	return cdc;
}


void freedv_eth_rx(struct freedv *freedv, int16_t *samples, int nr)
{
	while (nr) {
		int nin = freedv_nin(freedv);
		int copy = nin - nr_rx;
		if (copy > nr)
			copy = nr;

		memcpy(samples_rx + nr_rx, samples, copy * sizeof(int16_t));
		samples += copy;
		nr -= copy;
		nr_rx += copy;
		
		if (nr_rx == nin) {
			unsigned char packed_codec_bits[bytes_per_codec_frame];
			
			bool old_cdc = cdc;
			cdc = false;
			
			int ret = freedv_codecrx(freedv, packed_codec_bits, samples_rx);

			/* Don't 'detect' a voice signal to soon. 
			   Might be nice to have some SNR number from the
			   2400B mode so we can take it into account */
			int sync;
			float snr_est;
			freedv_get_modem_stats(freedv, &sync, &snr_est);
			if (!sync) {
				if (old_cdc)
					printf("RX sync lost\n");
				rx_sync = RX_SYNC_ZERO;
			} else {
				rx_sync += snr_est - RX_SYNC_ZERO;
			}
			
			cdc = (ret && rx_sync > RX_SYNC_THRESHOLD);
			if (ret && cdc) {
				int i;
				for (i = 0; i < bytes_per_codec_frame/bytes_per_eth_frame; i++) {
					interface_rx(
					    bcast, rx_add, eth_type_rx,
					    packed_codec_bits + i * bytes_per_eth_frame,
					    bytes_per_eth_frame);
				}
				printf(".");
				fflush(NULL);
				cdc_voice = true;
			} else if (cdc) {
				int i;
				/* Data frame between voice data? */
				printf("*");
				fflush(NULL);
				if (cdc_voice) {
					for (i = 0; i < bytes_per_codec_frame/bytes_per_eth_frame; i++) {
						interface_rx(
						    bcast, rx_add, eth_type_rx,
						    silence_packet,
						    bytes_per_eth_frame);
					}
				}
			}
			if (sync)
				printf(" %f\t%f\t%f\n", snr_est, rx_sync, snr_est-RX_SYNC_ZERO);

			/* Reset rx address for voice to our own mac */
			if (!cdc && cdc != old_cdc) {
				printf("Reset RX add\n");
				memcpy(rx_add, mac, 6);
				cdc_voice = false;
			}


			nr_rx = 0;
		}
	}
}

void freedv_eth_rx_cb_datarx(void *arg, unsigned char *packet, size_t size)
{
	if (size == 12) {
		if (memcmp(rx_add, packet + 6, 6)) {
			char callstr[9];
			int ssid;
			bool multicast;
		
			memcpy(rx_add, packet + 6, 6);

			eth_ar_mac2call(callstr, &ssid, &multicast, rx_add);
			printf("Voice RX add: %s-%d%s\n", callstr, ssid, multicast ? "" : "*");
		}
	} else if (size > 14) {
		/* Filter out our own packets if they come back */
		if (memcmp(packet+6, mac, 6)) {
			uint16_t type = (packet[12] << 8) | packet[13];
			interface_rx(packet, packet+6, type, packet + 14, size - 14);
		}
	}
}


void freedv_eth_rx_vc_callback(void *arg, char c)
{
	uint8_t msg[2];
	
	/* Ignore if not receiving */
	if (!freedv_eth_rx_cdc())
		return;
	
	if (c)
		printf("VC RX: 0x%x %c\n", c, c);
	msg[0] = c;
	msg[1] = 0;
	interface_rx(bcast, rx_add, ETH_P_AR_CONTROL, msg, 1);
}

static void create_silence_packet(struct CODEC2 *c2)
{
	int nr = codec2_samples_per_frame(c2);
	int16_t samples[nr];

	free(silence_packet);
	silence_packet = calloc(1, bytes_per_eth_frame);

	memset(samples, 0, nr * sizeof(int16_t));
	
	int i;
	unsigned char *sp = silence_packet;

	for (i = 0; i < bytes_per_codec_frame/bytes_per_eth_frame; i++) {
		codec2_encode(c2, sp, samples);
		sp += bytes_per_codec_frame;
	}
}


int freedv_eth_rx_init(struct freedv *freedv, uint8_t init_mac[6])
{
	int nr_samples;

	cdc = false;

        bytes_per_eth_frame = codec2_bits_per_frame(freedv_get_codec2(freedv));
	bytes_per_eth_frame += 7;
	bytes_per_eth_frame /= 8;
	int rat = freedv_get_n_codec_bits(freedv) / codec2_bits_per_frame(freedv_get_codec2(freedv));
	bytes_per_codec_frame = bytes_per_eth_frame * rat;

	nr_samples = freedv_get_n_max_modem_samples(freedv);
	create_silence_packet(freedv_get_codec2(freedv));
	free(samples_rx);
	samples_rx = calloc(nr_samples, sizeof(samples_rx[0]));

	int mode = freedv_get_mode(freedv);
	eth_type_rx = freedv_eth_mode2type(mode);

	memcpy(mac, init_mac, 6);
	memcpy(rx_add, init_mac, 6);

	return 0;
}

