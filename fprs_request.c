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

char *netname = "freedv";

static void usage(void)
{
	printf("Options:\n");
	printf("-e [element]\telement type\n");
	printf("-c [call]\town callsign\n");
	printf("-t [call]\ttarget callsign\n");
	printf("-n [dev]\tNetwork device name (default: \"%s\")\n", netname);
}

int main(int argc, char **argv)
{
	int opt;
	int fd_int;
	uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t myaddr[6];
	uint8_t taddr[6];
	char *call = NULL;
	char *target = NULL;
	enum fprs_type el = FPRS_ERROR;
	
	while ((opt = getopt(argc, argv, "c:n:t:e:")) != -1) {
		switch(opt) {
			case 'c':
				call = optarg;
				break;
			case 'e':
				el = atoi(optarg);
				break;
			case 't':
				target = optarg;
				break;
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
	if (!target || eth_ar_callssid2mac(taddr, target, false)) {
		printf("Target callsign could not be converted to a valid MAC address\n");
		goto err_usage;
	}

	struct fprs_frame *frame = fprs_frame_create();
	
	fprs_frame_add_callsign(frame, myaddr);
	fprs_frame_add_request(frame, taddr, &el, 1);
	
	uint8_t *data;
	size_t size;
				
	size = fprs_frame_data_size(frame);
	data = calloc(size, sizeof(uint8_t));
	if (data) {
		fprs_frame_data_get(frame, data, &size);
		interface_rx(bcast, myaddr, ETH_P_FPRS, data, size);
		free(data);
	}

	return 0;

err_usage:
	usage();
	return -1;
}
