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

#include "emphasis.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define NR 8000
#define AMP (16384)
#define STEP 1.1394832
#define FREF 3000


double amp(int16_t *buf_out, int nr)
{
	int i;
	int amin = 0;
	int amax = 0;
		
	for (i = 0; i < nr; i++) {
		if (buf_out[i] > amax)
			amax = buf_out[i];
		if (buf_out[i] < amin)
			amin = buf_out[i];
	}
	double amp = 20*log10((amax - amin)/65536.0);
	return amp;
}

int main(int argc, char **argv)
{
	struct emphasis *emphasis = emphasis_init();
	double f;
	int16_t buf_in[NR];
	int16_t buf_out[NR];
	int i;
	
	for (f = STEP; f < NR/2; f+= STEP) {
		printf("%f %f", f, -20*log10((FREF/f)*(32768.0/AMP)));
		for(i = 0; i < NR; i++) {
			buf_in[i] = AMP * sin(((f * M_PI * 2.0) / NR) * i);
		}
		memcpy(buf_out, buf_in, sizeof(buf_out));

		printf(" %f", amp(buf_out, NR));

		emphasis_reset(emphasis);
		emphasis_pre(emphasis, buf_out, NR);
		
		printf(" %f", amp(buf_out, NR));

		emphasis_reset(emphasis);
		emphasis_de(emphasis, buf_out, NR);

		printf(" %f", amp(buf_out, NR));

		printf("\n");
	}

	return 0;
}
