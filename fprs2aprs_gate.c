#include "interface.h"
#include "fprs.h"
#include "eth_ar.h"

#include <unistd.h>
#include <stdio.h>
#include <poll.h>
#include <errno.h>
#include <string.h>


char *call = "FPRSGATE";

static int cb_int_tx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	char aprs[256] = { 0 };
	size_t aprs_size = 255;
	
	struct fprs_frame *frame = fprs_frame_create();

	fprs_frame_data_set(frame, data, len);
	
	if (fprs2aprs(aprs, &aprs_size, frame, from, call))
		return -1;

	printf("%s", aprs);

	fprs_frame_destroy(frame);

	return 0;
}

static void usage(void)
{
	printf("Options:\n");
	printf("-v\tverbose\n");
	printf("-c [call]\town callsign\n");
	printf("-n [dev]\tNetwork device name (default: \"freedv\")\n");
}

int main(int argc, char **argv)
{
	char *call = "FPRSGATE";
	char *netname = "freedv";
	int fd_int;
	struct pollfd *fds;
	int nfds;
	int poll_int;
	uint8_t mac[6];
	int opt;
	
	while ((opt = getopt(argc, argv, "c:n:")) != -1) {
		switch(opt) {
			case 'c':
				call = optarg;
				break;
			case 'n':
				netname = optarg;
				break;
			default:
				usage();
				return -1;
		}
	}

	if (eth_ar_callssid2mac(mac, call, false)) {
		printf("Callsign could not be converted to a valid MAC address\n");
		return -1;
	}

	fd_int = interface_init(netname, mac, false, ETH_P_FPRS);
	if (fd_int < 0) {
		printf("Could not open interface: %s\n", strerror(errno));
		return -1;
	}

	nfds = 1;
	fds = calloc(sizeof(struct pollfd), nfds);

	poll_int = 0;
	fds[poll_int].fd = fd_int;
	fds[poll_int].events = POLLIN;

	do {
		poll(fds, nfds, -1);
		if (fds[poll_int].revents & POLLIN) {
			interface_tx(cb_int_tx);
		}
	} while (1);

	return 0;
}
