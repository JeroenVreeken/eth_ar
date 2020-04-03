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
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

static _Atomic(struct tx_packet *) tx_packet_pool = NULL;

static uint8_t voice_transmission = 0;
static double voice_level = -INFINITY;

struct tx_packet *tx_packet_alloc(void)
{
	struct tx_packet *packet;
	
	do {
		if (tx_packet_pool) {
			packet = tx_packet_pool;
		} else {
			return malloc(sizeof(struct tx_packet));
		}
	} while (!atomic_compare_exchange_weak(&tx_packet_pool, &packet, packet->next));

	packet->len = 0;
	packet->off = 0;
	packet->local_rx = false;
		
	return packet;
}

void tx_packet_free(struct tx_packet *packet)
{
	struct tx_packet *next;

	do {
		next = tx_packet_pool;
		packet->next = next;
	} while (!atomic_compare_exchange_weak(&tx_packet_pool, &next, packet));
}

static struct tx_packet *queue_voice = NULL;
static struct tx_packet **queue_voice_tail = &queue_voice;

struct tx_packet *dequeue_voice(void)
{
	struct tx_packet *packet;
	
	packet = queue_voice;
	queue_voice = packet->next;
	if (&packet->next == queue_voice_tail) {
		queue_voice_tail = &queue_voice;
	}
	return packet;
}

struct tx_packet *peek_voice(void)
{
	return queue_voice;
}

int enqueue_voice(struct tx_packet *packet, uint8_t transmission, double level_dbm)
{
	if (queue_voice && transmission != voice_transmission) {
		if (level_dbm < voice_level) {
			tx_packet_free(packet);
			return 0;
		}
	}
	voice_transmission = transmission;
	voice_level = level_dbm;

	packet->next = NULL;
	*queue_voice_tail = packet;
	queue_voice_tail = &packet->next;

	return 1;
}

bool queue_voice_filled(size_t min_len)
{
	size_t len = 0;
	struct tx_packet *entry;
	
	for (entry = queue_voice; entry; entry = entry->next) {
		len += entry->len;
		if (len >= min_len) {
			return true;
		}
	}
	
	return false;
}

void queue_voice_end(uint8_t transmission)
{
	if (transmission == voice_transmission)
		voice_level = -INFINITY;
}

static struct tx_packet *queue_baseband = NULL;
static struct tx_packet **queue_baseband_tail = &queue_baseband;

struct tx_packet *dequeue_baseband(void)
{
	struct tx_packet *packet;
	
	packet = queue_baseband;
	queue_baseband = packet->next;
	if (&packet->next == queue_baseband_tail) {
		queue_baseband_tail = &queue_baseband;
	}
	return packet;
}

struct tx_packet *peek_baseband(void)
{
	return queue_baseband;
}

void enqueue_baseband(struct tx_packet *packet)
{
	packet->next = NULL;
	*queue_baseband_tail = packet;
	queue_baseband_tail = &packet->next;
}

bool queue_baseband_filled(void)
{
	return queue_baseband;
}

void ensure_baseband(size_t nr)
{
	struct tx_packet *packet = queue_baseband;
	
	if (packet->len == nr)
		return;
	
	if (packet->len > nr) {
		struct tx_packet *p2 = tx_packet_alloc();
		size_t new_nr = packet->len - nr;
		
		p2->local_rx = packet->local_rx;
		memcpy(p2->data, packet->data + nr, new_nr);
		p2->len = new_nr;
		
		packet->len = nr;
		p2->next = packet->next;
		packet->next = p2->next;
	} else {
		while (packet->next) {
			struct tx_packet *p2 = packet->next;
			size_t nr_extra = nr - packet->len;

			if (nr_extra > p2->len)
				nr_extra = p2->len;
			memcpy(packet->data + packet->len, p2->data, nr_extra);
			packet->len += nr_extra;
			
			size_t nr_off = p2->len - nr_extra;
			if (!nr_off) {
				packet->next = p2->next;
				tx_packet_free(p2);
			} else {
				memmove(p2->data, p2->data + nr_extra, nr_off);
				p2->len = nr_off;
			}
			
			if (packet->len == nr)
				return;
		}
		size_t nr_zero = nr - packet->len;
		memset(packet->data + packet->len, 0, nr_zero);
		packet->len = nr;
	}
}


static struct tx_packet *queue_data = NULL;
static struct tx_packet **queue_data_tail = &queue_data;

struct tx_packet *dequeue_data(void)
{
	struct tx_packet *packet;
	
	packet = queue_data;
	queue_data = packet->next;
	if (&packet->next == queue_data_tail) {
		queue_data_tail = &queue_data;
	}
	return packet;
}

struct tx_packet *peek_data(void)
{
	return queue_data;
}

void enqueue_data(struct tx_packet *packet)
{
	packet->next = NULL;
	*queue_data_tail = packet;
	queue_data_tail = &packet->next;
}

bool queue_data_filled(void)
{
	return queue_data;
}


static struct tx_packet *queue_control = NULL;
static struct tx_packet **queue_control_tail = &queue_control;

struct tx_packet *dequeue_control(void)
{
	struct tx_packet *packet;
	
	packet = queue_control;
	queue_control = packet->next;
	if (&packet->next == queue_control_tail) {
		queue_control_tail = &queue_control;
	}
	return packet;
}

struct tx_packet *peek_control(void)
{
	return queue_control;
}

void enqueue_control(struct tx_packet *packet)
{
	packet->next = NULL;
	*queue_control_tail = packet;
	queue_control_tail = &packet->next;
}

bool queue_control_filled(void)
{
	return queue_control;
}
