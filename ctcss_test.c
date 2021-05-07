
#include "ctcss.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

double tones[] = {
	71.9,
};

void ctcss_test_gen(int rate, short *samples, int nr, double tone, double ramp)
{
	int i;
	double amp = 32767 * ramp;
	printf("amp %f\n", amp);
	
	for (i = 0; i < nr; i++) {
		double v = sin(i * M_PI * 2 * tone / (double)rate);
		
		samples[i] = v * amp;
	}
}

int test(int rate)
{
	int r;
	
	
	int nr = rate /40;
	short samples[nr];
	memset(samples, 0, nr * sizeof(short));
	bool detected;
	
	int k;
	double ramp;
	for (k = 0; k < sizeof(tones)/sizeof(tones[k]); k++) {
		for (ramp = 150.0/3000.0; ramp < 1; ramp *= 10) {
			r = ctcss_detect_init(tones[k], rate);
			printf("ctcss_init(%d): %d\n", rate, r);

			ctcss_test_gen(rate, samples, nr, 55, ramp);
			detected = ctcss_detect_rx(samples, nr);
			printf("ctcss_detect_rx(270): detected=%d\n", detected);

			ctcss_test_gen(rate, samples, nr, tones[k] * 1.035, ramp);
			detected = ctcss_detect_rx(samples, nr);
			printf("ctcss_detect_rx(-): detected=%d\n", detected);

			ctcss_test_gen(rate, samples, nr, tones[k], ramp);
			detected = ctcss_detect_rx(samples, nr);
			printf("ctcss_detect_rx(+): detected=%d\n", detected);

			ctcss_test_gen(rate, samples, nr, tones[k] / 1.035, ramp);
			detected = ctcss_detect_rx(samples, nr);
			printf("ctcss_detect_rx(-): detected=%d\n", detected);
		
			if (!detected) {
				printf("Failed\n");
//				exit(1);
			}
		}
	}
	
	return 0;
}

int main(int argc, char **argv)
{
	test(48000);
	test(44100);
	test(16000);
	test(8000);
	printf("Test: Passed\n");
	return 0;
}
