/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2016, 2017, 2020

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
static int bytes_per_freedv_frame;
static int bytes_per_codec2_frame;
static bool vc_busy = false;
static bool fullduplex;
static int tx_delay;
static int tx_tail;
static int tx_header;
static int tx_header_max;
static int tx_fprs;
static int tx_channel = 0;
static double tx_amp;

static struct nmea_state *nmea;

static struct freedv *freedv = NULL;

static int nom_modem_samples;
static int16_t *mod_out;
static struct sound_resample *sr0 = NULL;
static struct sound_resample *sr1 = NULL;

static int tx_sound_out(int16_t *samples, int nr)
{
	int16_t *samples1 = NULL;

	struct tx_packet *packet = peek_baseband();
	if (packet) {
		ensure_baseband(nr*sizeof(int16_t));
		packet = dequeue_baseband();
		samples1 = (int16_t*)packet->data;
	}
	
	if (!sr0) {
		sound_out_lr(samples, samples1, nr);
	} else {
		int nr_out = sound_resample_nr_out(sr0, nr);
		int16_t hw_mod_out0[nr_out];
		int16_t hw_mod_out1[nr_out];
		
		sound_resample_perform(sr0, hw_mod_out0, samples, nr_out, nr);
		if (samples1) {
			sound_resample_perform(sr1, hw_mod_out1, samples1, nr_out, nr);
		}
		int16_t *hw0;
		int16_t *hw1;
		if (tx_channel == 0) {
			hw0 = hw_mod_out0;
			hw1 = samples1 ? hw_mod_out1 : NULL;
		} else {
			hw1 = hw_mod_out0;
			hw0 = samples1 ? hw_mod_out1 : NULL;
		}
		sound_out_lr(hw0, hw1, nr_out);
	}

	if (packet) {
		tx_packet_free(packet);
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
	
	sound_gain(mod_out, nom_modem_samples, tx_amp);
	
	tx_sound_out(mod_out, nom_modem_samples);
	
	if (tx_state == TX_STATE_ON) {
		if (queue_voice_filled(bytes_per_freedv_frame))
			printf("x");
		else
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
	size_t len = 0;

	unsigned char frame_data[bytes_per_freedv_frame];
	size_t frame_len = 0;
	
	while (len < bytes_per_freedv_frame) {
		size_t copy = bytes_per_freedv_frame - frame_len;
		struct tx_packet *packet = peek_voice();
		
		if (packet->len < copy)
			copy = packet->len;
		
		memcpy(frame_data + frame_len, packet->data, copy);
		frame_len += copy;
		
		if (packet->len > copy) {
			memmove(packet->data, packet->data + copy, packet->len - copy);
		} else {
			dequeue_voice();
			tx_packet_free(packet);	
		}
	}
	
	bool fprs_late = nmea && tx_state_fprs_cnt >= tx_fprs && nmea->position_valid;
	bool header_late = tx_state_data_header_cnt >= tx_header;
	bool have_data = fprs_late || queue_data_filled() || header_late;

	bool send_data_frame = tx_state_data_header_cnt >= tx_header_max;
	struct CODEC2 *codec2 = freedv_get_codec2(freedv);
	if (codec2) {
		double energy = codec2_get_energy(codec2, data);
		send_data_frame |= have_data && energy < 15.0;
		send_data_frame |= !vc_busy && energy < 1.0;
	}
			
//	printf("e: %f %d %d\n", energy, vc_busy, tx_state_data_header_cnt);

	if (send_data_frame) {
		data_tx();
	} else {
		freedv_rawdatatx(freedv, mod_out, data);
			
		sound_gain(mod_out, nom_modem_samples, tx_amp);
		tx_sound_out(mod_out, nom_modem_samples);

		printf("-");
		fflush(NULL);
	}
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
	enum io_hl_ptt ptt;
	bool set_ptt = false;

	tx_state_cnt++;
	switch (tx_state) {
		case TX_STATE_OFF:
			if ((queue_voice_filled(bytes_per_freedv_frame) || queue_data_filled()) && (!freedv_eth_cdc() || fullduplex)) {
//				printf("OFF -> DELAY\n");
				tx_state = TX_STATE_DELAY;
				tx_state_cnt = 0;
				set_ptt = IO_HL_PTT_OTHER;
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
			if (queue_voice_filled(bytes_per_freedv_frame)) {
				tx_voice();
			} else {
				data_tx();
			}
			break;
		case TX_STATE_ON:
			if (!queue_voice_filled(bytes_per_freedv_frame) &&
			    !queue_data_filled() && freedv_data_ntxframes(freedv) <= 1 &&
			    !vc_busy) {
//				printf("ON -> TAIL\n");
				tx_state = TX_STATE_TAIL;
				tx_state_cnt = 0;
			}
			tx_state_data_header_cnt++;
			tx_state_fprs_cnt++;
			if (queue_voice_filled(bytes_per_freedv_frame)) {
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
				ptt = IO_HL_PTT_OFF;
			} else {
				if (queue_voice_filled(bytes_per_freedv_frame) || queue_data_filled()) {
//					printf("TAIL -> ON\n");
					tx_state = TX_STATE_ON;
					tx_state_cnt = 0;
					
					check_tx_add();
				}
				if (queue_voice_filled(bytes_per_freedv_frame)) {
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
    int tx_fprs_msec,
    int tx_channel_init,
    double tx_amp_init)
{
	freedv = init_freedv;
	nmea = init_nmea;
	fullduplex = init_fullduplex;
	memcpy(mac, init_mac, 6);
	memcpy(tx_add, mac, 6);
	tx_amp = tx_amp_init;
	
	tx_state = TX_STATE_OFF;
	io_hl_ptt_set(IO_HL_PTT_OFF);

	tx_channel = tx_channel_init;
	printf("TX channel %d\n", tx_channel);

        bytes_per_codec2_frame = freedv_get_bits_per_codec_frame(freedv);
	bytes_per_codec2_frame += 7;
	bytes_per_codec2_frame /= 8;
	printf("TX bytes per codec2 frame: %d\n", bytes_per_codec2_frame);
	int rat = freedv_get_bits_per_modem_frame(freedv) / freedv_get_bits_per_codec_frame(freedv);
	printf("TX codec2 frames per freedv frame: %d\n", rat);
	bytes_per_freedv_frame = bytes_per_codec2_frame * rat;
	printf("TX bytes per freedv frame: %d\n", bytes_per_freedv_frame);

	int freedv_rate = freedv_get_modem_sample_rate(freedv);
	printf("TX freedv rate: %d\n", freedv_rate);
	sound_resample_destroy(sr0);
	sound_resample_destroy(sr1);
	if (freedv_rate != hw_rate) {
		sr0 = sound_resample_create(hw_rate, freedv_rate);
		sr1 = sound_resample_create(hw_rate, freedv_rate);
	} else {
		sr0 = NULL;
		sr1 = NULL;
	}
	int period_msec = 1000 / (freedv_rate / freedv_get_n_nom_modem_samples(freedv));
	printf("TX period: %d msec\n", period_msec);

	tx_tail = (tx_tail_msec + period_msec - 1) / period_msec;
	tx_delay = (tx_delay_msec + period_msec -1)/ period_msec;
	tx_header = (tx_header_msec + period_msec -1) / period_msec;
	tx_header_max = tx_header_max_msec / period_msec;
	tx_fprs = (tx_fprs_msec + period_msec -1) / period_msec;
	
	printf("TX delay: %d periods\n", tx_delay);
	printf("TX tail: %d periods\n", tx_tail);
	printf("TX header: %d periods\n", tx_header);
	printf("TX header max: %d periods\n", tx_header_max);

	nom_modem_samples = freedv_get_n_nom_modem_samples(freedv);
	
	free(mod_out);
	mod_out = calloc(sizeof(int16_t), nom_modem_samples);

	return 0;
}
