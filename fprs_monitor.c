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
#include "interface.h"
#include <eth_ar/fprs.h>
#include <eth_ar/eth_ar.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>

static char *netname = "freedv";

static int cb(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	struct fprs_frame *frame = fprs_frame_create();
	if (!frame)
		return -1;
	
	fprs_frame_data_set(frame, data, len);
	
	struct fprs_element *el = NULL;
	int i;
	
	printf("\"elements\": [");
	for (i = 0; (el = fprs_frame_element_get(frame, el)); i++) {
		char *el_str = fprs_element2stra(el);
		printf("%s { %s }", i ? "," : "", el_str);
		free(el_str);
	}
	printf(" ]\n");
	
	fprs_frame_destroy(frame);

	return 0;
}

static void usage(void)
{
	printf("Options:\n");
	printf("-n [dev]\tNetwork device name (default: \"%s\")\n", netname);
}


int main(int argc, char **argv)
{
	int opt;
	int fd_int;
	
	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch(opt) {
			case 'n':
				netname = optarg;
				break;
			default:
				goto err_usage;
		}
	}

	fd_int = interface_init(netname, NULL, false, ETH_P_FPRS);
	if (fd_int < 0) {
		printf("Could not open interface: %s\n", strerror(errno));
		return -1;
	}

	do {
		fd_set fdr;
		
		FD_ZERO(&fdr);
		FD_SET(fd_int, &fdr);
		
		select(fd_int + 1, &fdr, NULL, NULL, NULL);

		if (FD_ISSET(fd_int, &fdr)) {
			interface_tx(cb);
		}
	} while (1);

	return 0;

err_usage:
	usage();
	return -1;
}
