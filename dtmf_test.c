
#include "dtmf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

struct dtmf_key {
	char key;
	double f1;
	double f2;
};

struct dtmf_key keys[] = {
	{ '1', 697, 1209 },
	{ '2', 697, 1336 },
	{ '3', 697, 1477 },
	{ 'A', 697, 1633 },
	{ '4', 770, 1209 },
	{ '5', 770, 1336 },
	{ '6', 770, 1477 },
	{ 'B', 770, 1633 },
	{ '7', 852, 1209 },
	{ '8', 852, 1336 },
	{ '9', 852, 1477 },
	{ 'C', 852, 1633 },
	{ '*', 941, 1209 },
	{ '0', 941, 1336 },
	{ '#', 941, 1477 },
	{ 'D', 941, 1633 },
};

char dtmf_test_cb_key = 0;
static void dtmf_test_cb(char *data)
{
	printf("dtmf_test_cb: %zd, 0x%02x, %c\n", strlen(data), (unsigned char)data[0], data[0]);
	dtmf_test_cb_key = data[0];
}

void dtmf_test_gen(int rate, short *samples, int nr, struct dtmf_key *key)
{
	int i;
	double amp1 = 8192;
	double amp2 = 8192;
	
	for (i = 0; i < nr; i++) {
		double v1 = sin(i * M_PI * 2 * key->f1 / (double)rate);
		double v2 = sin(i * M_PI * 2 * key->f2 / (double)rate);
		
		samples[i] = v1 * amp1 + v2 * amp2;
	}
}

int test(int rate)
{
	int r;
	r = dtmf_init(rate);
	printf("dtmf_init(%d): %d\n", rate, r);
	
	
	int nr = rate * 0.04;
	short samples[nr];
	memset(samples, 0, nr * sizeof(short));
	bool detected;
	
	int k;
	for (k = 0; k < sizeof(keys)/sizeof(keys[k]); k++) {
		dtmf_test_gen(rate, samples, nr, &keys[k]);
	
		dtmf_rx(samples, nr, dtmf_test_cb, &detected);
		printf("dmtf_rx(): %d, detected=%d\n", r, detected);
		
		if (!detected || dtmf_test_cb_key != keys[k].key) {
			printf("Failed\n");
			exit(1);
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
