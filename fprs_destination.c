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
static bool done = false;

static int cb(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	struct fprs_frame *frame = fprs_frame_create();
	if (!frame)
		return -1;
	
	fprs_frame_data_set(frame, data, len);
	
	struct fprs_element *el_callsign;
	struct fprs_element *el_destination;
	
	el_callsign = fprs_frame_element_by_type(frame, FPRS_CALLSIGN);
	el_destination = fprs_frame_element_by_type(frame, FPRS_DESTINATION);
	
	if (!el_callsign || !el_destination)
		goto skip;

	struct fprs_element *el = NULL;
	int i;
	
	printf("\"elements\": [");
	for (i = 0; (el = fprs_frame_element_get(frame, el)); i++) {
		char *el_str = fprs_element2stra(el);
		printf("%s { %s }", i ? "," : "", el_str);
		free(el_str);
	}
	printf(" ]\n");

	done = true;
skip:
	fprs_frame_destroy(frame);

	return 0;
}

static void usage(void)
{
	printf("Options:\n");
	printf("-m [msg]\tMessage");
	printf("-c [call]\town callsign\n");
	printf("-d [call]\tdestinationcallsign\n");
	printf("-n [dev]\tNetwork device name (default: \"%s\")\n", netname);
}


int main(int argc, char **argv)
{
	int opt;
	int fd_int;
	uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t myaddr[6];
	uint8_t daddr[6];
	char *call = NULL;
	char *dst = NULL;
	char *msg = NULL;
	time_t stop_listen = 60;
	
	while ((opt = getopt(argc, argv, "c:d:n:m:")) != -1) {
		switch(opt) {
			case 'c':
				call = optarg;
				break;
			case 'd':
				dst = optarg;
				break;
			case 'm':
				msg = optarg;
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

	if (!call || eth_ar_callssid2mac(myaddr, call, false)) {
		printf("From callsign could not be converted to a valid MAC address\n");
		goto err_usage;
	}
	if (!dst || eth_ar_callssid2mac(daddr, dst, false)) {
		printf("Destination callsign could not be converted to a valid MAC address\n");
		goto err_usage;
	}

	if (call && dst && msg) {
		struct fprs_frame *frame = fprs_frame_create();
	
		fprs_frame_add_callsign(frame, myaddr);
		fprs_frame_add_destination(frame, daddr);
		fprs_frame_add_comment(frame, msg);
	
		uint8_t *data;
		size_t size;
				
		size = fprs_frame_data_size(frame);
		data = calloc(size, sizeof(uint8_t));
		if (!data)
			goto err_fatal;
		fprs_frame_data_get(frame, data, &size);
		interface_rx_raw(bcast, myaddr, ETH_P_FPRS, data, size);
		free(data);
		
		return 0;
	}

	stop_listen += time(NULL);

	do {
		struct timeval timeout;
		
		timeout.tv_usec = 0;
		timeout.tv_sec = stop_listen - time(NULL);
		if (timeout.tv_sec < 0)
			timeout.tv_sec = 0;
		
		fd_set fdr;
		
		FD_ZERO(&fdr);
		FD_SET(fd_int, &fdr);
		
		select(fd_int + 1, &fdr, NULL, NULL, &timeout);

		if (FD_ISSET(fd_int, &fdr)) {
			interface_tx_raw(cb);
		}
	} while (!done && time(NULL) < stop_listen);

	return 0;

err_fatal:
	printf("FATAL ERROR\n");
err_usage:
	usage();
	return -1;
}
