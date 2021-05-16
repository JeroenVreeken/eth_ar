// modified by Luigi Auriemma to work also with damaged audio and allow customized parameters

/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Goertzel routines are borrowed from Steve Underwood's tremendous work on the
 * DTMF detector.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Convenience Signal Processing routines
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Steve Underwood <steveu@coppice.org>
 */

/* Some routines from tone_detect.c by Steven Underwood as published under the zapata library */
/*
	tone_detect.c - General telephony tone detection, and specific
					detection of DTMF.

	Copyright (C) 2001  Steve Underwood <steveu@coppice.org>

	Despite my general liking of the GPL, I place this code in the
	public domain for the benefit of all mankind - even the slimy
	ones who might try to proprietize my work and use it to my
	detriment.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "dtmf.h"
#include "ctcss.h"

#define	DSP_DIGITMODE_DTMF			0				/*!< Detect DTMF digits */
#define DSP_DIGITMODE_MF			1				/*!< Detect MF digits */

#define DSP_DIGITMODE_NOQUELCH		(1 << 8)		/*!< Do not quelch DTMF from in-band */
#define DSP_DIGITMODE_RELAXDTMF		(1 << 11)		/*!< "Radio" mode (relaxed DTMF) */

#define	MAX_DTMF_DIGITS		1024

/* Basic DTMF specs:
 *
 * Minimum tone on = 40ms
 * Minimum tone off = 50ms
 * Maximum digit rate = 10 per second
 * Normal twist <= 8dB accepted
 * Reverse twist <= 4dB accepted
 * S/N >= 15dB will detect OK
 * Attenuation <= 26dB will detect OK
 * Frequency tolerance +- 1.5% will detect, +-3.5% will reject
 */




/* PARAMETERS */ 
 


static double DTMF_OPTIMIZED_VALUE_8000 =    102;
 
//#define DTMF_THRESHOLD		8.0e7
static double DTMF_THRESHOLD =     8000000000.0; // aluigi work-around
static double DTMF_NORMAL_TWIST	= 6.3;     /* 8dB */

#if 0
#ifdef	RADIO_RELAX
static double DTMF_REVERSE_TWIST1 = 6.5;
static double DTMF_REVERSE_TWIST2 = 2.5;
#else
static double DTMF_REVERSE_TWIST1 = 4.0;
static double DTMF_REVERSE_TWIST2 = 2.5;
#endif
#define DTMF_REVERSE_TWIST          ((digitmode & DSP_DIGITMODE_RELAXDTMF) ? DTMF_REVERSE_TWIST1 : DTMF_REVERSE_TWIST2)     /* 4dB normal */
static double DTMF_2ND_HARMONIC_ROW1 = 1.7;
static double DTMF_2ND_HARMONIC_ROW2 = 2.5;
#define DTMF_2ND_HARMONIC_ROW       ((digitmode & DSP_DIGITMODE_RELAXDTMF) ? DTMF_2ND_HARMONIC_ROW1 : DTMF_2ND_HARMONIC_ROW2)     /* 4dB normal */
static double DTMF_2ND_HARMONIC_COL =	63.1;    /* 18dB */
static double DTMF_TO_TOTAL_ENERGY =	42.0;
#endif

static double DTMF_RELATIVE_PEAK_ROW =	6.3;     /* 8dB */
static double DTMF_RELATIVE_PEAK_COL =	6.3;     /* 8dB */




typedef struct {
	int v2;
	int v3;
	int chunky;
	int fac;
	int samples;
} goertzel_state_t;

typedef struct {
	int value;
	int power;
} goertzel_result_t;

typedef struct
{
	goertzel_state_t row_out[4];
	goertzel_state_t col_out[4];
	int lasthit;
	int current_hit;
	double energy;
	int current_sample;
	int samples;
	double threshold;
} dtmf_detect_state_t;


typedef struct
{
	char digits[MAX_DTMF_DIGITS + 1];
	int current_digits;
	int detected_digits;
	int lost_digits;

	dtmf_detect_state_t dtmf;
} digit_detect_state_t;

static double dtmf_row[] =
{
	697.0,  770.0,  852.0,  941.0
};
static double dtmf_col[] =
{
	1209.0, 1336.0, 1477.0, 1633.0
};


static char dtmf_positions[] = "123A" "456B" "789C" "*0#D";


static inline void goertzel_sample(goertzel_state_t *s, short sample)
{
	int v1;
	
	v1 = s->v2;
	s->v2 = s->v3;
	
	s->v3 = (s->fac * s->v2) >> 15;
	s->v3 = s->v3 - v1 + (sample >> s->chunky);
	if (abs(s->v3) > 32768) {
		s->chunky++;
		s->v3 = s->v3 >> 1;
		s->v2 = s->v2 >> 1;
		v1 = v1 >> 1;
	}
}

static inline void goertzel_update(goertzel_state_t *s, short *samps, int count)
{
	int i;
	
	for (i=0;i<count;i++) 
		goertzel_sample(s, samps[i]);
}

static inline double goertzel_result(goertzel_state_t *s)
{
	goertzel_result_t r;
	r.value = (s->v3 * s->v3) + (s->v2 * s->v2);
	r.value -= ((s->v2 * s->v3) >> 15) * s->fac;
	r.power = s->chunky * 2;
//	printf("val %d power %d\n", r.value, r.power);
	return (double)r.value * (double)(1 << r.power);
}

static inline void goertzel_init(goertzel_state_t *s, double freq, int samples, int rate)
{
	s->v2 = s->v3 = s->chunky = 0.0;
	s->fac = (int)(32768.0 * 2.0 * cos(2.0 * M_PI * freq / rate));
	s->samples = samples;
}

static inline void goertzel_reset(goertzel_state_t *s)
{
	s->v2 = s->v3 = s->chunky = 0.0;
}

static void ast_dtmf_detect_init (dtmf_detect_state_t *s, int rate)
{
	int i;

	s->lasthit = 0;
	s->current_hit = 0;
	for (i = 0;  i < 4;  i++) {
		int opt_samples = DTMF_OPTIMIZED_VALUE_8000 * rate / 8000;
		goertzel_init (&s->row_out[i], dtmf_row[i], opt_samples, rate);
		goertzel_init (&s->col_out[i], dtmf_col[i], opt_samples, rate);
		s->energy = 0.0;
		s->samples = opt_samples;
	}
	s->current_sample = 0;
	int rsq = rate / 8000;
	rsq = rsq * rsq;
	s->threshold = DTMF_THRESHOLD * rsq;
}


static void ast_digit_detect_init(digit_detect_state_t *s, int rate)
{
/*	s->current_digits = 0;
	s->detected_digits = 0;
	s->lost_digits = 0;
	s->digits[0] = '\0';
*/
	ast_dtmf_detect_init(&s->dtmf, rate);
}
/*
static void store_digit(digit_detect_state_t *s, char digit)
{
	s->detected_digits++;
	if (s->current_digits < MAX_DTMF_DIGITS) {
		s->digits[s->current_digits++] = digit;
		s->digits[s->current_digits] = '\0';
	} else {
		//ast_log(LOG_WARNING, "Digit lost due to full buffer\n");
		s->lost_digits++;
	}
}
*/

static int dtmf_detect(digit_detect_state_t *s, int16_t amp[], int samples, 
		 int digitmode, int *writeback, void (*cb)(char *), bool *detected)
{
	double row_energy[4];
	double col_energy[4];
	double famp;
	int i;
	int j;
	int sample;
	int best_row;
	int best_col;
	int hit;
	int limit;

	*detected = false;
	hit = 0;
	for (sample = 0;  sample < samples;  sample = limit) {
		/* s->dtmf.samples is optimised to meet the DTMF specs. */
		if ((samples - sample) >= (s->dtmf.samples - s->dtmf.current_sample))
			limit = sample + (s->dtmf.samples - s->dtmf.current_sample);
		else
			limit = samples;
		/* The following unrolled loop takes only 35% (rough estimate) of the 
		   time of a rolled loop on the machine on which it was developed */
		for (j = sample; j < limit; j++) {
			famp = amp[j];
			s->dtmf.energy += famp*famp;
			/* With GCC 2.95, the following unrolled code seems to take about 35%
			   (rough estimate) as long as a neat little 0-3 loop */
			goertzel_sample(s->dtmf.row_out, amp[j]);
			goertzel_sample(s->dtmf.col_out, amp[j]);
			goertzel_sample(s->dtmf.row_out + 1, amp[j]);
			goertzel_sample(s->dtmf.col_out + 1, amp[j]);
			goertzel_sample(s->dtmf.row_out + 2, amp[j]);
			goertzel_sample(s->dtmf.col_out + 2, amp[j]);
			goertzel_sample(s->dtmf.row_out + 3, amp[j]);
			goertzel_sample(s->dtmf.col_out + 3, amp[j]);
		}
		s->dtmf.current_sample += (limit - sample);
		if (s->dtmf.current_sample < s->dtmf.samples) {
			if (hit && !((digitmode & DSP_DIGITMODE_NOQUELCH))) {
				/* If we had a hit last time, go ahead and clear this out since likely it
				   will be another hit */
				for (i=sample;i<limit;i++) 
					amp[i] = 0;
				*writeback = 1;
			}
			continue;
		}
		/* We are at the end of a DTMF detection block */
		/* Find the peak row and the peak column */
		row_energy[0] = goertzel_result (&s->dtmf.row_out[0]);
		col_energy[0] = goertzel_result (&s->dtmf.col_out[0]);

		for (best_row = best_col = 0, i = 1;  i < 4;  i++) {
			row_energy[i] = goertzel_result (&s->dtmf.row_out[i]);
			if (row_energy[i] > row_energy[best_row])
				best_row = i;
			col_energy[i] = goertzel_result (&s->dtmf.col_out[i]);
			if (col_energy[i] > col_energy[best_col])
				best_col = i;
		}
		hit = 0;

		/* Basic signal level test and the twist test */
		if (row_energy[best_row] >= s->dtmf.threshold && 
		    col_energy[best_col] >= s->dtmf.threshold &&
//		    col_energy[best_col] < row_energy[best_row] *DTMF_REVERSE_TWIST &&          // aluigi work-around
		    col_energy[best_col]*DTMF_NORMAL_TWIST > row_energy[best_row]) {
			/* Relative peak test */
			for (i = 0;  i < 4;  i++) {
				if ((i != best_col &&
				    col_energy[i]*DTMF_RELATIVE_PEAK_COL > col_energy[best_col]) ||
				    (i != best_row 
				     && row_energy[i]*DTMF_RELATIVE_PEAK_ROW > row_energy[best_row])) {
					break;
				}
			}
			/* ... and fraction of total energy test */
			if (i >= 4 /*&&
			    (row_energy[best_row] + col_energy[best_col]) > DTMF_TO_TOTAL_ENERGY*s->dtmf.energy*/) {     // aluigi work-around
				/* Got a hit */

if(0) printf("%e\t%e\t%e\t%f %f\t-"
"%e %e %e %e | "
"%e %e %e %e"
"\n", s->dtmf.threshold, col_energy[best_col], row_energy[best_row],
col_energy[best_col]/s->dtmf.threshold, row_energy[best_row]/s->dtmf.threshold,
col_energy[0],col_energy[1],col_energy[2],col_energy[3],
row_energy[0],row_energy[1],row_energy[2],row_energy[3]
);
				hit = dtmf_positions[(best_row << 2) + best_col];
				if (!(digitmode & DSP_DIGITMODE_NOQUELCH)) {
					/* Zero out frame data if this is part DTMF */
					for (i=sample;i<limit;i++) 
						amp[i] = 0;
					*writeback = 1;
				}
			}
		}
		
		/* The logic in the next test is:
		   For digits we need two successive identical clean detects, with
		   something different preceeding it. This can work with
		   back to back differing digits. More importantly, it
		   can work with nasty phones that give a very wobbly start
		   to a digit */
		if (hit != s->dtmf.current_hit) {
			if (hit && s->dtmf.lasthit == hit) {
				s->dtmf.current_hit = hit;
				char cbv[2] = { hit, 0 };
				cb(cbv);
			} else if (s->dtmf.lasthit != s->dtmf.current_hit) {
				s->dtmf.current_hit = 0;
			}
		}
		if (hit && s->dtmf.lasthit) {
			*detected |= hit;
		}
		s->dtmf.lasthit = hit;

		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 4;  i++) {
			goertzel_reset(&s->dtmf.row_out[i]);
			goertzel_reset(&s->dtmf.col_out[i]);
		}
		s->dtmf.energy = 0.0;
		s->dtmf.current_sample = 0;
	}
	return samples - sample;
}


static digit_detect_state_t dtmf;

int dtmf_rx(short *smp, int nr, void (*cb)(char *), bool *detected)
{
	int writeback;
	
	dtmf_detect(&dtmf, smp, nr, DSP_DIGITMODE_NOQUELCH, &writeback, cb, detected);

	return 0;
}

int dtmf_init(int rate)
{
    
    ast_digit_detect_init(&dtmf, rate);

	return 0;
}


static goertzel_state_t ctcss_gs;
static int ctcss_samples;
static int ctcss_samples_cur;
static unsigned ctcss_result;
static double ctcss_threshold_level = 4e10;
static unsigned ctcss_threshold_mask = 0xf;
static double ctcss_rate_fac = 1;

bool ctcss_detect_rx(short *smp, int nr)
{
	while (nr) {
		int nr_up = nr;
		if ((ctcss_samples - ctcss_samples_cur) < nr_up) {
			nr_up = ctcss_samples - ctcss_samples_cur;
		}
		
		goertzel_update(&ctcss_gs, smp, nr_up);

		ctcss_samples_cur += nr_up;
		nr -= nr_up;
		
		if (ctcss_samples_cur == ctcss_samples) {
			double r = goertzel_result(&ctcss_gs);
			
			r *= ctcss_rate_fac;
			
			ctcss_result <<= 1;
			ctcss_result |= r > ctcss_threshold_level;
			
			printf("r: %e\t0x%08x %d\n", r, ctcss_result,
			    (ctcss_result & ctcss_threshold_mask) == ctcss_threshold_mask);
		
			goertzel_reset(&ctcss_gs);
			ctcss_samples_cur = 0;
		}
	}
	
	return (ctcss_result & ctcss_threshold_mask) == ctcss_threshold_mask;
}

int ctcss_detect_init(double freq, int rate)
{
	ctcss_samples = rate / freq * 1;
	printf("RX CTCSS: %fHz, %d samples in bin (%dms)\n", freq, ctcss_samples, 1000*ctcss_samples/rate);
	goertzel_init(&ctcss_gs, freq, ctcss_samples, rate);
	goertzel_reset(&ctcss_gs);
	ctcss_samples_cur = 0;
	ctcss_result = 0;
	
	double tsq = rate / 8000.0;
	ctcss_rate_fac = 1 / (tsq * tsq);
	
	return 0;
}


struct iir {
	float z[3];
	float den[4];
	float num[4];
};

struct iir *filter_iir_create_8k_hp_300hz()
{
	struct iir *iir = calloc(1, sizeof(struct iir));

	// Butterworth 2nd
/*	iir->den[0] = 1;
	iir->den[1] = -1.6692;
	iir->den[2] = 0.716634;
	
	iir->num[0] = 0.846459;
	iir->num[1] = -1.69292;
	iir->num[2] = 0.846459;
*/

	// Chebyshev 2nd
/*	iir->den[0] = 1;
	iir->den[1] = -1.7384;
	iir->den[2] = 0.808669;
	
	iir->num[0] = 0.627783;
	iir->num[1] = -1.25557;
	iir->num[2] = 0.627783;
*/
	// Butterworth 3nd
	iir->den[0] = 1;
	iir->den[1] = -2.52981;
	iir->den[2] = 2.16382;
	iir->den[3] = -0.623539;
	
	iir->num[0] = 0.789646;
	iir->num[1] = -2.36894;
	iir->num[2] = 2.36894;
	iir->num[3] = -0.789646;

	return iir;
}

int filter_iir_2nd(struct iir *iir, short *smp, int nr)
{
	int i;
	
	for (i = 0; i < nr; i++) {
		float newz = smp[i] * iir->den[0] 
		    - iir->z[0] * iir->den[1] 
		    - iir->z[1] * iir->den[2]
		    - iir->z[2] * iir->den[3];
		
		float newy = newz * iir->num[0]
		     + iir->z[0] * iir->num[1] 
		     + iir->z[1] * iir->num[2]
		     + iir->z[2] * iir->num[3];
		
		iir->z[2] = iir->z[1];
		iir->z[1] = iir->z[0];
		iir->z[0] = newz;

		if (newy > 32767)
			newy = 32767;
		if (newy < -32768)
			newy = -32768;
		smp[i] = newy;
	}
	
	return 0;
}
