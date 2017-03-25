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

#ifndef _INCLUDE_FREEDV_ETH_H_
#define _INCLUDE_FREEDV_ETH_H_

#include "nmea.h"

#include <codec2/codec2.h>
#include <codec2/freedv_api.h>
#include <eth_ar/eth_ar.h>
#include <stdint.h>
#include <stdbool.h>

static inline uint16_t freedv_eth_mode2type(int mode)
{
	uint16_t type = ETH_P_CODEC2_700;

	switch(mode) {
		case FREEDV_MODE_700:
			type = ETH_P_CODEC2_700;
			break;
		case FREEDV_MODE_700B:
			type = ETH_P_CODEC2_700B;
			break;
		case FREEDV_MODE_1600:
			type = ETH_P_CODEC2_1300;
			break;
		case FREEDV_MODE_2400A:
		case FREEDV_MODE_2400B:
			type = ETH_P_CODEC2_1300C;
			break;
		default:
			break;
	}

	return type;
}

static inline int freedv_eth_type2codecmode(uint16_t type)
{
	switch(type) {
		case ETH_P_CODEC2_3200:
			return CODEC2_MODE_3200;
		case ETH_P_CODEC2_2400:
			return CODEC2_MODE_2400;
		case ETH_P_CODEC2_1600:
			return CODEC2_MODE_1600;
		case ETH_P_CODEC2_1400:
			return CODEC2_MODE_1400;
		case ETH_P_CODEC2_1300:
			return CODEC2_MODE_1300;
		case ETH_P_CODEC2_1200:
			return CODEC2_MODE_1200;
		case ETH_P_CODEC2_700:
			return CODEC2_MODE_700;
		case ETH_P_CODEC2_700B:
			return CODEC2_MODE_700B;
		case ETH_P_CODEC2_700C:
			return CODEC2_MODE_700C;
		case ETH_P_CODEC2_1300C:
			return CODEC2_MODE_1300C;
		case ETH_P_ALAW:
			return 'A';
		default:
			break;
	}
	return -1;
}

static inline bool freedv_eth_type_isvoice(uint16_t type)
{
	switch(type) {
		case ETH_P_CODEC2_3200:
		case ETH_P_CODEC2_2400:
		case ETH_P_CODEC2_1600:
		case ETH_P_CODEC2_1400:
		case ETH_P_CODEC2_1300:
		case ETH_P_CODEC2_1200:
		case ETH_P_CODEC2_700:
		case ETH_P_CODEC2_700B:
		case ETH_P_CODEC2_700C:
		case ETH_P_CODEC2_1300C:
		case ETH_P_ALAW:
			return true;
		default:
			break;
	}
	return false;
}


struct tx_packet {
	uint8_t from[6];
	uint8_t data[2048];
	size_t len;
	size_t off;
	
	struct tx_packet *next;
	struct tx_packet *prev;
};

struct tx_packet *tx_packet_alloc(void);
void tx_packet_free(struct tx_packet *packet);

struct tx_packet *dequeue_voice(void);
struct tx_packet *peek_voice(void);
void enqueue_voice(struct tx_packet *packet);
bool queue_voice_filled(void);

struct tx_packet *dequeue_data(void);
struct tx_packet *peek_data(void);
void enqueue_data(struct tx_packet *packet);
bool queue_data_filled(void);

struct tx_packet *dequeue_control(void);
struct tx_packet *peek_control(void);
void enqueue_control(struct tx_packet *packet);
bool queue_control_filled(void);

bool freedv_eth_cdc(void);
int freedv_eth_transcode(struct tx_packet *packet, int to_codecmode, uint16_t from_type);

int freedv_eth_tx_init(struct freedv *init_freedv, uint8_t init_mac[6], 
    struct nmea_state *init_nmea, bool init_fullduplex,
    int hw_rate,
    int tx_tail_msec, int tx_delay_msec,
    int tx_header_msec, int tx_header_max_msec,
    int tx_fprs_msec);
char freedv_eth_tx_vc_callback(void *arg);
void freedv_eth_tx_state_machine(void);
bool freedv_eth_tx_ptt(void);
void freedv_eth_tx_cb_datatx(void *arg, unsigned char *packet, size_t *size);

#define FREEDV_ALAW_NR_SAMPLES 160
#define FREEDV_ALAW_RATE 8000

int freedv_eth_txa_init(bool init_fullduplex, int hw_rate, 
    int tx_tail_msec, 
    double ctcss_f, double ctcss_amp,
    int beacon_interval, char *beacon_msg,
    bool emphasis);
void freedv_eth_txa_state_machine(void);
bool freedv_eth_txa_ptt(void);

bool freedv_eth_rxa_cdc(void);
int freedv_eth_rxa_init(int hw_rate, uint8_t mac_init[6], bool emphasis, double ctcss_freq);
void freedv_eth_rxa(int16_t *samples, int nr);

#endif /* _INCLUDE_FREEDV_ETH_H_ */
