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
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdbool.h>

static inline uint16_t freedv_eth_mode2type(int mode)
{
	uint16_t type = ETH_P_CODEC2_700;

	switch(mode) {
#if defined(CODEC2_MODE_700)
		case FREEDV_MODE_700:
			type = ETH_P_CODEC2_700;
			break;
#endif
#if defined(CODEC2_MODE_700B)
		case FREEDV_MODE_700B:
			type = ETH_P_CODEC2_700B;
			break;
#endif
		case FREEDV_MODE_1600:
			type = ETH_P_CODEC2_1300;
			break;
		case FREEDV_MODE_2400A:
		case FREEDV_MODE_2400B:
			type = ETH_P_CODEC2_1300;
			break;
#if defined(FREEDV_MODE_2020)
		case FREEDV_MODE_2020:
			type = ETH_P_LPCNET_1733;
			break;
#endif
		case FREEDV_MODE_700C:
		case FREEDV_MODE_700D:
			type = ETH_P_CODEC2_700C;
			break;
#if defined(FREEDV_MODE_6000)
		case FREEDV_MODE_6000:
			type = ETH_P_CODEC2_3200;
			break;
#endif
		default:
			break;
	}

	return type;
}

#define TX_PACKET_LEN_MAX 4096
struct tx_packet {
	uint8_t from[6];
	uint8_t data[TX_PACKET_LEN_MAX];
	size_t len;
	size_t off;
	bool local_rx;
	
	struct tx_packet *next;
	struct tx_packet *prev;
};

static inline size_t tx_packet_max(void)
{
	return TX_PACKET_LEN_MAX;
}

struct tx_packet *tx_packet_alloc(void);
void tx_packet_free(struct tx_packet *packet);

struct tx_packet *dequeue_voice(void);
struct tx_packet *peek_voice(void);
int enqueue_voice(struct tx_packet *packet, uint8_t transmission, double level_dbm);
bool queue_voice_filled(size_t min_len);
void queue_voice_end(uint8_t transmission);

struct tx_packet *dequeue_baseband(void);
struct tx_packet *peek_baseband(void);
void enqueue_baseband(struct tx_packet *packet);
bool queue_baseband_filled(void);
void ensure_baseband(size_t nr);

struct tx_packet *dequeue_data(void);
struct tx_packet *peek_data(void);
void enqueue_data(struct tx_packet *packet);
bool queue_data_filled(void);

struct tx_packet *dequeue_control(void);
struct tx_packet *peek_control(void);
void enqueue_control(struct tx_packet *packet);
bool queue_control_filled(void);

void freedv_eth_voice_rx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len, bool local_rx, uint8_t transmission, double level);

bool freedv_eth_cdc(void);

struct freedv_eth_transcode;

struct freedv_eth_transcode * freedv_eth_transcode_init(int native_rate);
int freedv_eth_transcode(struct freedv_eth_transcode *tc, struct tx_packet *packet, int to_codecmode, uint16_t from_type);

int freedv_eth_tx_init(struct freedv *init_freedv, uint8_t init_mac[6], 
    struct nmea_state *init_nmea, bool init_fullduplex,
    int hw_rate,
    int tx_tail_msec, int tx_delay_msec,
    int tx_channel,
    bool modem);
char freedv_eth_tx_vc_callback(void *arg);
void freedv_eth_tx_state_machine(void);
bool freedv_eth_tx_ptt(void);
void freedv_eth_tx_cb_datatx(void *arg, unsigned char *packet, size_t *size);

#define FREEDV_ALAW_NR_SAMPLES 160
#define FREEDV_ALAW_RATE 8000

int freedv_eth_txa_init(bool init_fullduplex, int hw_rate, 
    int tx_tail_msec);
void freedv_eth_txa_state_machine(void);
bool freedv_eth_txa_ptt(void);

bool freedv_eth_rxa_cdc(void);
int freedv_eth_rxa_init(int hw_rate, uint8_t mac_init[6], int hw_nr);
void freedv_eth_rxa(int16_t *samples, int nr);

int freedv_eth_bb_in_init(int hw_rate, uint8_t mac_init[6], int nr_hw);
bool freedv_eth_baseband_in_cdc(void);

int freedv_eth_modem_init(char *modem_file, struct freedv *init_freedv);
void freedv_eth_modem_poll(short *events);
void freedv_eth_modem_rx(int fd_modem);
void freedv_eth_modem_tx(int fd_modem);
void freedv_eth_modem_tx_add(signed char *tx_sym, size_t nr);
bool freedv_eth_modem_tx_empty(int fd_modem);

#endif /* _INCLUDE_FREEDV_ETH_H_ */
