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

static _Atomic(struct tx_packet *) tx_packet_pool = NULL;

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

void enqueue_voice(struct tx_packet *packet)
{
	packet->next = NULL;
	*queue_voice_tail = packet;
	queue_voice_tail = &packet->next;
}

bool queue_voice_filled(void)
{
	return queue_voice;
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
