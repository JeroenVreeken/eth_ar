/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2016, 2017

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
#include "freedv_eth_rx.h"
#include "io.h"

#include <string.h>
#include <stdio.h>
#include <eth_ar/fprs.h>

enum tx_state {
	TX_STATE_OFF,
	TX_STATE_DELAY,
	TX_STATE_ON,
	TX_STATE_TAIL,
};

static enum tx_state tx_state;
static int tx_state_cnt;
static int tx_state_data_header_cnt;
static int tx_state_fprs_cnt;
static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static uint8_t mac[6];
static uint8_t tx_add[6];
static int bytes_per_codec_frame;
static int bytes_per_eth_frame;
static bool vc_busy = false;
static bool fullduplex;
static int tx_delay;
static int tx_tail;
static int tx_header;
static int tx_header_max;
static int tx_fprs;

static struct nmea_state *nmea;

static struct freedv *freedv = NULL;

static int nom_modem_samples;
static int16_t *mod_out;
static struct sound_resample *sr = NULL;

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

static void check_tx_add(void)
{
	char callstr[9];
	int ssid;
	bool multicast;
	uint8_t *add;
	struct tx_packet *packet = peek_voice();

	if (packet) {
		add = packet->from;
	} else {
		add = mac;
	}

	if (memcmp(add, tx_add, 6)) {
		memcpy(tx_add, add, 6);
		freedv_set_data_header(freedv, tx_add);

		eth_ar_mac2call(callstr, &ssid, &multicast, tx_add);
		printf("Voice TX add: %s-%d%s\n", callstr, ssid, multicast ? "" : "*");
	}
}

static void data_tx(void)
{
	freedv_datatx(freedv, mod_out);
	
	tx_sound_out(mod_out, nom_modem_samples);
	
	if (tx_state == TX_STATE_ON) {
		printf("+");
	} else {
		printf("~");
	}
	fflush(NULL);
}

static void tx_voice(void)
{
	check_tx_add();
	struct tx_packet *packet = dequeue_voice();
	uint8_t *data = packet->data;
	size_t len = packet->len;
	
	while (len) {
		if (len < bytes_per_codec_frame) {
			len = 0;
		} else {
			double energy = codec2_get_energy(freedv_get_codec2(freedv), data);
			bool fprs_late = nmea && tx_state_fprs_cnt >= tx_fprs && nmea->position_valid;
			bool header_late = tx_state_data_header_cnt >= tx_header;
			bool have_data = fprs_late || queue_data_filled() || header_late;
			
//			printf("e: %f %d\n", energy, vc_busy);

			if (tx_state_data_header_cnt >= tx_header_max ||
			    (have_data && energy < 15.0) ||
			    (!vc_busy && energy < 1.0)) {
				data_tx();
			} else {
				freedv_codectx(freedv, mod_out, data);
			
				tx_sound_out(mod_out, nom_modem_samples);

				printf("-");
				fflush(NULL);
			}
			len -= bytes_per_codec_frame;
		}
	}
		
	tx_packet_free(packet);
}


char freedv_eth_tx_vc_callback(void *arg)
{
	char c;
	
	if (queue_control_filled() && tx_state == TX_STATE_ON) {
		struct tx_packet *qp = peek_control();
		
		c = qp->data[qp->off++];
		if (c < 0)
			c = 0;
		if (qp->off >= qp->len) {
			dequeue_control();
		
			tx_packet_free(qp);
		}
		vc_busy = true;
		printf("VC TX: 0x%x %c\n", c, c);
	} else {
		c = 0;
		vc_busy = false;
	}
	
	return c;
}


void freedv_eth_tx_state_machine(void)
{
	bool ptt;
	bool set_ptt = false;

	tx_state_cnt++;
	switch (tx_state) {
		case TX_STATE_OFF:
			if ((queue_voice_filled() || queue_data_filled()) && (!freedv_eth_cdc() || fullduplex)) {
//				printf("OFF -> DELAY\n");
				tx_state = TX_STATE_DELAY;
				tx_state_cnt = 0;
				set_ptt = true;
				ptt = true;

				check_tx_add();
			} else {
				sound_silence();
				break;
			}
		case TX_STATE_DELAY:
//			printf("%d %d\n", tx_state_cnt, tx_delay);
			if (tx_state_cnt >= tx_delay) {
//				printf("DELAY -> ON\n");
				tx_state = TX_STATE_ON;
				tx_state_cnt = 0;
				tx_state_data_header_cnt = 0;
				tx_state_fprs_cnt = tx_fprs - tx_header - 1;
			}
			if (queue_voice_filled()) {
				tx_voice();
			} else {
				data_tx();
			}
			break;
		case TX_STATE_ON:
			if (!queue_voice_filled() &&
			    !queue_data_filled() && freedv_data_ntxframes(freedv) <= 1 &&
			    !vc_busy) {
//				printf("ON -> TAIL\n");
				tx_state = TX_STATE_TAIL;
				tx_state_cnt = 0;
			}
			tx_state_data_header_cnt++;
			tx_state_fprs_cnt++;
			if (queue_voice_filled()) {
				tx_voice();
			} else {
				data_tx();
			}
			break;
		case TX_STATE_TAIL:
			if (tx_state_cnt >= tx_tail) {
//				printf("TAIL -> OFF\n");
				tx_state = TX_STATE_OFF;
				tx_state_cnt = 0;
				set_ptt = true;
				ptt = false;
			} else {
				if (queue_voice_filled() || queue_data_filled()) {
//					printf("TAIL -> ON\n");
					tx_state = TX_STATE_ON;
					tx_state_cnt = 0;
					
					check_tx_add();
				}
				if (queue_voice_filled()) {
					tx_voice();
				} else {
					data_tx();
				}
				break;
			}
	}

	if (set_ptt)
		io_hl_ptt_set(ptt);
}

bool freedv_eth_tx_ptt(void)
{
	return tx_state != TX_STATE_OFF;
}

void freedv_eth_tx_cb_datatx(void *arg, unsigned char *packet, size_t *size)
{
	if (tx_state == TX_STATE_ON) {
		bool fprs_late = nmea && tx_state_fprs_cnt >= tx_fprs && nmea->position_valid;
//		printf("data %d %d %d\n", tx_state_fprs_cnt, fprs_late, tx_state_data_header_cnt);
		
		if ((!queue_data_filled() && !fprs_late) || 
		    tx_state_data_header_cnt >= tx_header) {
			tx_state_data_header_cnt = 0;
			*size = 0;
		} else if (fprs_late) {
//			printf("fprs\n");
			/* Send fprs frame */
			struct fprs_frame *frame = fprs_frame_create();
			fprs_frame_add_position(frame, 
			    nmea->longitude, nmea->latitude, false);
			if (nmea->altitude_valid)
				fprs_frame_add_altitude(frame, nmea->altitude);
			if (nmea->speed_valid && nmea->course_valid &&
			    nmea->speed >= FPRS_VECTOR_SPEED_EPSILON)
				fprs_frame_add_vector(frame, nmea->course, 0.0, nmea->speed);
			
			fprs_frame_add_symbol(frame, (uint8_t[2]){'F','#'});
			
			size_t fprs_size = *size - 14;
			uint16_t eth_type = ETH_P_FPRS;
			
			fprs_frame_data_get(frame, packet + 14, &fprs_size);
			memcpy(packet + 0, bcast, 6);
			memcpy(packet + 6, mac, 6);
			packet[12] = eth_type >> 8;
			packet[13] = eth_type & 0xff;
			*size = fprs_size + 14;
			
			fprs_frame_destroy(frame);
			tx_state_fprs_cnt = 0;
		} else {
			struct tx_packet *qp = dequeue_data();
		
			memcpy(packet, qp->data, qp->len);
			*size = qp->len;
		
			tx_packet_free(qp);
		}
	} else {
		/* TX not on, just send header frames as filler */
		*size = 0;
	}
}

int freedv_eth_tx_init(struct freedv *init_freedv, uint8_t init_mac[6], 
    struct nmea_state *init_nmea, bool init_fullduplex,
    int hw_rate,
    int tx_tail_msec, int tx_delay_msec,
    int tx_header_msec, int tx_header_max_msec,
    int tx_fprs_msec)
{
	freedv = init_freedv;
	nmea = init_nmea;
	fullduplex = init_fullduplex;
	memcpy(mac, init_mac, 6);
	memcpy(tx_add, mac, 6);
	
	tx_state = TX_STATE_OFF;
	io_hl_ptt_set(false);

        bytes_per_eth_frame = codec2_bits_per_frame(freedv_get_codec2(freedv));
	bytes_per_eth_frame += 7;
	bytes_per_eth_frame /= 8;
	printf("TX bytes per ethernet frame: %d\n", bytes_per_eth_frame);
	int rat = freedv_get_n_codec_bits(freedv) / codec2_bits_per_frame(freedv_get_codec2(freedv));
	printf("TX ethernet frames per freedv frame: %d\n", rat);
	bytes_per_codec_frame = bytes_per_eth_frame * rat;

	int freedv_rate = freedv_get_modem_sample_rate(freedv);
	printf("TX freedv rate: %d\n", freedv_rate);
	sound_resample_destroy(sr);
	if (freedv_rate != hw_rate) {
		sr = sound_resample_create(hw_rate, freedv_rate);
	} else {
		sr = NULL;
	}
	int nr_samples = freedv_get_n_max_modem_samples(freedv);
	int period_msec = 1000 / (freedv_rate / nr_samples);
	printf("TX period: %d msec\n", period_msec);

	tx_tail = tx_tail_msec / period_msec;
	tx_delay = tx_delay_msec / period_msec;
	tx_header = tx_header_msec / period_msec;
	tx_header_max = tx_header_max_msec / period_msec;
	tx_fprs = tx_fprs_msec / period_msec;
	
	printf("TX delay: %d periods\n", tx_delay);
	printf("TX tail: %d periods\n", tx_tail);
	printf("TX header: %d periods\n", tx_header);
	printf("TX header max: %d periods\n", tx_header_max);

	nom_modem_samples = freedv_get_n_nom_modem_samples(freedv);
	
	free(mod_out);
	mod_out = calloc(sizeof(int16_t), nom_modem_samples);

	return 0;
}
