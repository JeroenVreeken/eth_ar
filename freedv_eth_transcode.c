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
#include "eth_ar/alaw.h"
#include "eth_ar/ulaw.h"
#include <stdio.h>
#include "sound.h"

#include <stdlib.h>
#include <string.h>

struct freedv_eth_transcode {
	struct CODEC2 *trans_enc;
	struct CODEC2 *trans_dec;
	short *trans_speech;
	int trans_speech_size;
	int trans_speech_pos;
	int trans_dec_mode;
	int trans_enc_mode;
	int trans_enc_samples_frame;
	int trans_enc_bytes_frame;
	int trans_dec_bytes_frame;
	int trans_rate_native;
	struct sound_resample *sr;
	int sr_rate_in;
	int sr_rate_out;
};

struct freedv_eth_transcode *freedv_eth_transcode_init(int native_rate)
{
	printf("Transcode native audio rate: %d\n", native_rate);

	struct freedv_eth_transcode *tc = calloc(1, sizeof(struct freedv_eth_transcode));
	
	tc->trans_dec_mode = -1;
	tc->trans_enc_mode = -1;

	tc->trans_rate_native = native_rate;
	
	return tc;
}

static inline int eth_ar_codec_rate(struct freedv_eth_transcode *tc, int mode)
{
	switch(mode) {
		case CODEC_MODE_LE16:
		case CODEC_MODE_BE16:
			return tc->trans_rate_native;	
		case CODEC_MODE_LPCNET_1733:
			return 16000;
		case CODEC_MODE_ALAW:
		case CODEC_MODE_ULAW:
		case CODEC2_MODE_3200:
		case CODEC2_MODE_2400:
		case CODEC2_MODE_1600:
		case CODEC2_MODE_1400:
		case CODEC2_MODE_1300:
		case CODEC2_MODE_1200:
		case CODEC2_MODE_700C:
		case CODEC2_MODE_450:
		case CODEC2_MODE_450PWB:
		default:
			return 8000;
	}
}


int freedv_eth_transcode(struct freedv_eth_transcode *tc, struct tx_packet *packet, int to_codecmode, uint16_t from_type)
{
	int from_codecmode = eth_ar_eth_p_codecmode(from_type);
	int samples_in;
	int from_rate = eth_ar_codec_rate(tc, from_codecmode);
	int to_rate = eth_ar_codec_rate(tc, to_codecmode);

	if (to_codecmode == from_codecmode)
		return 0;

	switch(from_codecmode) {
		case CODEC_MODE_ALAW:
		case CODEC_MODE_ULAW:
			samples_in = packet->len;
			break;
		case CODEC_MODE_LE16:
		case CODEC_MODE_BE16:
			samples_in = packet->len / 2;
			break;
		default: {
			if (from_codecmode != tc->trans_dec_mode) {
				if (tc->trans_dec)
					codec2_destroy(tc->trans_dec);
				tc->trans_dec_mode = from_codecmode;
				tc->trans_dec = codec2_create(tc->trans_dec_mode);
				tc->trans_dec_bytes_frame = codec2_bits_per_frame(tc->trans_dec);
				tc->trans_dec_bytes_frame += 7;
				tc->trans_dec_bytes_frame /= 8;
			}
			int frames = packet->len / tc->trans_dec_bytes_frame;
			samples_in = codec2_samples_per_frame(tc->trans_dec) * frames;
			break;
		}
	}

	short *speech_in = malloc(sizeof(short)*(samples_in));
	
	switch (from_codecmode) {
		case CODEC_MODE_ALAW:
			alaw_decode(speech_in, packet->data, samples_in);
			break;
		case CODEC_MODE_ULAW:
			ulaw_decode(speech_in, packet->data, samples_in);
			break;
		case CODEC_MODE_LE16: {
			/* Fill packet with native short samples */
			union {
				uint8_t b[2];
				uint16_t s;
			} b2s;
			int i;
			for (i = 0; i < samples_in; i++) {
				b2s.b[0] = packet->data[i * 2 + 0];
				b2s.b[1] = packet->data[i * 2 + 1];
				speech_in[i] = le16toh(b2s.s);
			}
			break;
		}
		case CODEC_MODE_BE16: {
			/* Fill packet with native short samples */
			union {
				uint8_t b[2];
				uint16_t s;
			} b2s;
			int i;
			for (i = 0; i < samples_in; i++) {
				b2s.b[0] = packet->data[i * 2 + 0];
				b2s.b[1] = packet->data[i * 2 + 1];
				speech_in[i] = be16toh(b2s.s);
			}
			break;
		}
		default: {
			int cbytes = 0;
			int i;
			for (i = 0; i < samples_in; i+= codec2_samples_per_frame(tc->trans_dec)) {
				codec2_decode(tc->trans_dec, speech_in + i, packet->data + cbytes);
				cbytes += tc->trans_dec_bytes_frame;
			}
			break;
		}
	}

	int samples_out = 0;
	if (to_rate == from_rate) {
		samples_out = samples_in;
		if (tc->sr) {
			sound_resample_destroy(tc->sr);
			tc->sr = NULL;
		}
	} else {
		if (tc->sr_rate_in != from_rate || tc->sr_rate_out != to_rate) {
			if (tc->sr) {
				sound_resample_destroy(tc->sr);
				tc->sr = NULL;
			}
		}
		if (!tc->sr) {
			printf("Transcode with resample: %d -> %d\n", from_rate, to_rate);
			tc->sr_rate_in = from_rate;
			tc->sr_rate_out = to_rate;
			tc->sr = sound_resample_create(tc->sr_rate_out, tc->sr_rate_in);
		}
		if (tc->sr) {
			samples_out = sound_resample_nr_out(tc->sr, samples_in);
		}
	}

	if (tc->trans_speech_pos + samples_out > tc->trans_speech_size) {
		short *tmp = realloc(tc->trans_speech, sizeof(short)*(tc->trans_speech_pos + samples_out));
		if (!tmp)
			return -1;
		tc->trans_speech = tmp;
		tc->trans_speech_size = tc->trans_speech_pos + samples_out;
	}
	short *speech_out = tc->trans_speech + tc->trans_speech_pos;
	if (!tc->sr) {
		memcpy(speech_out, speech_in, samples_out);
	} else {
		sound_resample_perform(tc->sr, speech_out, speech_in, samples_out, samples_in);
	}
	tc->trans_speech_pos += samples_out;


	free(speech_in);

	switch(to_codecmode) {
		case CODEC_MODE_ALAW: {
			if (tc->trans_speech_pos > tx_packet_max())
				tc->trans_speech_pos = tx_packet_max();
			alaw_encode(packet->data, tc->trans_speech, tc->trans_speech_pos);
			packet->len = tc->trans_speech_pos;
			tc->trans_speech_pos = 0;
		
			break;
		}
		case CODEC_MODE_ULAW: {
			if (tc->trans_speech_pos > tx_packet_max())
				tc->trans_speech_pos = tx_packet_max();
			ulaw_encode(packet->data, tc->trans_speech, tc->trans_speech_pos);
			packet->len = tc->trans_speech_pos;
			tc->trans_speech_pos = 0;
		
			break;
		}
		case CODEC_MODE_NATIVE16: {
			/* Fill packet with native short samples */
			if (tc->trans_speech_pos > tx_packet_max())
				tc->trans_speech_pos = tx_packet_max();
			memcpy(packet->data, tc->trans_speech, tc->trans_speech_pos * sizeof(short));
			packet->len = tc->trans_speech_pos * sizeof(short);
			tc->trans_speech_pos = 0;
			break;
		}
		default: 
			if (to_codecmode != tc->trans_enc_mode) {
				if (tc->trans_enc)
					codec2_destroy(tc->trans_enc);
				tc->trans_enc_mode = to_codecmode;
				tc->trans_enc = codec2_create(tc->trans_enc_mode);
				tc->trans_enc_samples_frame = codec2_samples_per_frame(tc->trans_enc);
				tc->trans_enc_bytes_frame = codec2_bits_per_frame(tc->trans_enc);
				tc->trans_enc_bytes_frame += 7;
				tc->trans_enc_bytes_frame /= 8;
			}
			packet->len = 0;
		
			int off = 0;
			while (tc->trans_speech_pos - off >= tc->trans_enc_samples_frame) {
				codec2_encode(tc->trans_enc, packet->data + packet->len, tc->trans_speech + off);
				off += tc->trans_enc_samples_frame;
				packet->len += tc->trans_enc_bytes_frame;
			}
			if (off && off != tc->trans_speech_pos) {
				memmove(tc->trans_speech, tc->trans_speech + off, sizeof(short)*(tc->trans_speech_pos - off));
			}
			tc->trans_speech_pos -= off;
			break;
	}

	return 0;
}
