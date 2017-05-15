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
#include "eth_ar_codec2.h"
#include "alaw.h"
#include "ulaw.h"
#include "stdio.h"

#include <stdlib.h>
#include <string.h>

static struct CODEC2 *trans_enc;
static struct CODEC2 *trans_dec;
static short *trans_speech;
static int trans_speech_size;
static int trans_speech_pos;
static int trans_dec_mode = -1;
static int trans_enc_mode = -1;
static int trans_enc_samples_frame;
static int trans_enc_bytes_frame;

int freedv_eth_transcode(struct tx_packet *packet, int to_codecmode, uint16_t from_type)
{
	int from_codecmode = eth_ar_eth_p_codecmode(from_type);
	int samples;

	if (to_codecmode == from_codecmode)
		return 0;

	if (from_codecmode != CODEC2_MODE_ALAW &&
	    from_codecmode != CODEC2_MODE_ULAW) {
		if (from_codecmode != trans_dec_mode) {
			if (trans_dec)
				codec2_destroy(trans_dec);
			trans_dec_mode = from_codecmode;
			trans_dec = codec2_create(trans_dec_mode);
		}
		samples = codec2_samples_per_frame(trans_dec);
	} else {
		samples = packet->len;
	}

	if (trans_speech_pos + samples > trans_speech_size) {
		short *tmp = realloc(trans_speech, sizeof(short)*(trans_speech_pos + samples));
		if (!tmp)
			return -1;
		trans_speech = tmp;
		trans_speech_size = trans_speech_pos + samples;
	}
	short *speech = trans_speech + trans_speech_pos;
	
	switch (from_codecmode) {
		case CODEC2_MODE_ALAW:
			alaw_decode(speech, packet->data, samples);
			break;
		case CODEC2_MODE_ULAW:
			ulaw_decode(speech, packet->data, samples);
			break;
		default:
			codec2_decode(trans_dec, speech, packet->data);
			break;
	}
	trans_speech_pos += samples;


	switch(to_codecmode) {
		case CODEC2_MODE_ALAW: {
			alaw_encode(packet->data, trans_speech, trans_speech_pos);
			packet->len = trans_speech_pos;
			trans_speech_pos = 0;
		
			break;
		}
		case CODEC2_MODE_ULAW: {
			ulaw_encode(packet->data, trans_speech, trans_speech_pos);
			packet->len = trans_speech_pos;
			trans_speech_pos = 0;
		
			break;
		}
		case 'S': {
			/* Fill packet with native short samples */
			packet->len = trans_speech_pos * sizeof(short);
			memcpy(packet->data, trans_speech, packet->len);
			trans_speech_pos = 0;
			
			break;
		}
		default: 
			if (to_codecmode != trans_enc_mode) {
				if (trans_enc)
					codec2_destroy(trans_enc);
				trans_enc_mode = to_codecmode;
				trans_enc = codec2_create(trans_enc_mode);
				trans_enc_samples_frame = codec2_samples_per_frame(trans_enc);
				trans_enc_bytes_frame = codec2_bits_per_frame(trans_enc);
				trans_enc_bytes_frame += 7;
				trans_enc_bytes_frame /= 8;
			}
			packet->len = 0;
		
			int off = 0;
			while (trans_speech_pos - off >= trans_enc_samples_frame) {
				codec2_encode(trans_enc, packet->data + packet->len, trans_speech + off);
				off += trans_enc_samples_frame;
				packet->len += trans_enc_bytes_frame;
			}
			if (off && off != trans_speech_pos) {
				memmove(trans_speech, trans_speech + off, sizeof(short)*(trans_speech_pos - off));
			}
			trans_speech_pos -= off;
			break;
	}

	return 0;
}
