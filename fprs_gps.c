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
#include <math.h>
#include <time.h>
#include <sys/select.h>
#include <gps.h>


static char *netname = "freedv";


static void usage(void)
{
	printf("Options:\n");
	printf("-c [call]\town callsign\n");
	printf("-i [dev]\tNetwork device name (default: \"%s\")\n", netname);
}



static uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static uint8_t myaddr[6];
static char *symbol = "0F";

static struct gps_data_t gps = {0};
static time_t last_update;
#define UPDATE_INTERVAL	60
static time_t update_interval = UPDATE_INTERVAL;

void handle_gps(void)
{
	struct fprs_frame *frame = fprs_frame_create();
	double speed = 0;

	if (!(gps.set & TIME_SET))
		return;
	if (!(gps.set & STATUS_SET))
		return;
	if (!(gps.set & MODE_SET))
		return;
	
	// Not needed on eth_ar interfaces
	//fprs_frame_add_callsign(frame, myaddr);

	uint8_t sym[2] = { symbol[0], symbol[1] };
	fprs_frame_add_symbol(frame, sym);

	if (gps.status) {
		fprs_frame_add_timestamp(frame, gps.fix.time);
		printf("%f ", gps.fix.time);
	
		if (gps.fix.mode >= MODE_2D) {
			fprs_frame_add_position(frame, gps.fix.longitude, gps.fix.latitude, false);
			printf("lat %f lon %f ", gps.fix.latitude, gps.fix.longitude);
		}
		if (gps.fix.mode >= MODE_3D) {
			fprs_frame_add_altitude(frame, gps.fix.altitude);
			printf("atl %f ", gps.fix.altitude);

			double az = gps.fix.track;
			double el = atan(gps.fix.climb / gps.fix.speed) / M_PI * 180.0;
			speed = sqrt(gps.fix.speed * gps.fix.speed + gps.fix.climb * gps.fix.climb);
			fprs_frame_add_vector(frame, az, el, speed);
			printf("az %f el %f speed %f ", az, el, speed);
		}
	}

	update_interval = UPDATE_INTERVAL - speed * 4;
	if (update_interval < 10)
		update_interval = 10;

	gps.set = 0;

	if (time(NULL) < last_update + update_interval)
		goto no_update;

	printf("transmit ");

	uint8_t *data;
	size_t size;

	size = fprs_frame_data_size(frame);
	data = calloc(size, sizeof(uint8_t));
	if (!data)
		goto err_fatal;
	fprs_frame_data_get(frame, data, &size);

	interface_rx_raw(bcast, myaddr, ETH_P_FPRS, data, size);

	free(data);

	last_update = time(NULL);

err_fatal:
no_update:
	fprs_frame_destroy(frame);
	
	printf("\n");
}


int main(int argc, char **argv)
{
	int opt;
	int fd_int;
	char *call = NULL;

	while ((opt = getopt(argc, argv, "c:i:e:s:")) != -1) {
		switch(opt) {
			case 'c':
				call = optarg;
				break;
			case 'i':
				netname = optarg;
				break;
			case 's':
				if (strlen(optarg) >= 2)
					symbol = optarg;
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


	//if (gps_open(GPSD_SHARED_MEMORY, NULL, &gps)) {
	if (gps_open("localhost", DEFAULT_GPSD_PORT, &gps)) {
		printf("gps_open failed\n");
		goto err;
	}
	gps_stream(&gps, WATCH_ENABLE, NULL);

	do {
		if (gps_waiting (&gps, 1000000)) {
			int r;
			if ((r = gps_read(&gps, NULL, 0)) < 0) {
				printf("Error reading gps data\n");
			} else {
				handle_gps();
			}
		}
	} while (1);

	gps_close(&gps);

	return 0;

err:
err_usage:
	usage();
	return -1;
}
