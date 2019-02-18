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

#include <poll.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <resolv.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#ifdef __linux__
#include <linux/sockios.h>
#endif


static char *call = NULL;
static int fd_is = -1;
static int fd_int = -1;

int tcp_connect(char *host, int port)
{
	struct addrinfo *result;
	struct addrinfo *entry;
	struct addrinfo hints = { 0 };
	int error;
	int sock = -1;
	char port_str[10];
	int tcp_connect_timeout = 1;
	
	sprintf(port_str, "%d", port);

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	error = getaddrinfo(host, port_str, &hints, &result);
	if (error) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
		
		res_init();
		
		return -1;
	}
	
	for (entry = result; entry; entry = entry->ai_next) {
		int flags;
		
		sock = socket(entry->ai_family, entry->ai_socktype,
		    entry->ai_protocol);
		flags = fcntl(sock, F_GETFL, 0);
		fcntl(sock, F_SETFL, flags | O_NONBLOCK);
		if (sock >= 0) {
			fd_set fdset_tx, fdset_err;
			struct timeval tv;
			
			tv.tv_sec = tcp_connect_timeout;
			tv.tv_usec = 0;
			
			if (connect(sock, entry->ai_addr, entry->ai_addrlen)) {
				int ret;
				do {
				    errno = 0;
				    FD_ZERO(&fdset_tx);
				    FD_ZERO(&fdset_err);
				    FD_SET(sock, &fdset_tx);
				    FD_SET(sock, &fdset_err);
				    tv.tv_sec = tcp_connect_timeout;
				    tv.tv_usec = 0;
				    ret = select(sock+1, NULL, &fdset_tx, NULL,
				    &tv);
				} while (
				    ret < 0 
				    &&
				    (errno == EAGAIN || errno == EINTR ||
				     errno == EINPROGRESS));
				
				int error = 0;
				socklen_t len = sizeof (error);
				int retval = getsockopt (sock, SOL_SOCKET, SO_ERROR,
				    &error, &len );
				if (!retval && error) {
					close(sock);
					sock = -1;
				}
			}

			if (sock >= 0) {
				flags = fcntl(sock, F_GETFL, 0);
				fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
				
				setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
				    &(int){1}, sizeof(int));

#ifdef __linux__
				/* number of probes which may fail */
				setsockopt(sock, SOL_TCP, TCP_KEEPCNT,
				    &(int){5}, sizeof(int));
				/* Idle time before starting with probes */
				setsockopt(sock, SOL_TCP, TCP_KEEPIDLE,
				    &(int){10}, sizeof(int));
				/* interval between probes */
				setsockopt(sock, SOL_TCP, TCP_KEEPINTVL,
				    &(int){2}, sizeof(int));
#endif
				
				break;
			}
		}
	}
	freeaddrinfo(result);
	
	return sock;
}

static int aprs_is_in(void)
{
	char buffer[256];
	ssize_t r;
	
	r = read(fd_is, buffer, 256);
	if (r > 0) {
		return write(2, buffer, r) != r;
	} else {
		return -1;
	}
}

static int fprs2aprs_is(struct fprs_frame *frame, uint8_t *from)
{
	char aprs[256] = { 0 };
	size_t aprs_size = 255;
	
	if (fprs2aprs(aprs, &aprs_size, frame, from, call))
		return -1;

	printf("%s", aprs);
	if (write(fd_is, aprs, strlen(aprs)) <= 0)
		return -1;

	return 0;
}

static int cb_int_tx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	struct fprs_frame *frame = fprs_frame_create();

	fprs_frame_data_set(frame, data, len);
	
	fprs2aprs_is(frame, from);
	
	fprs_frame_destroy(frame);

	return 0;
}




char *netname = "freedv";
char *host = "euro.aprs2.net";
int port = 14580;

static void usage(void)
{
	printf("Options:\n");
	printf("-v\tverbose\n");
	printf("-c [call]\town callsign\n");
	printf("-h [host]\tAPRS-IS server (default: \"%s\")\n", host);
	printf("-n [dev]\tNetwork device name (default: \"%s\")\n", netname);
	printf("-p [port]\tAPRS-IS port (default: %d)\n", port);
}

int main(int argc, char **argv)
{
	struct pollfd *fds;
	int nfds;
	int poll_int, poll_is;
	int opt;
	
	while ((opt = getopt(argc, argv, "c:n:h:i:p:")) != -1) {
		switch(opt) {
			case 'c':
				call = optarg;
				break;
			case 'h':
				host = optarg;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'n':
				netname = optarg;
				break;
			default:
				goto err_usage;
		}
	}
	
	if (!call) {
		printf("No callsign given\n");
		goto err_usage;
	} else {
		int i;
		
		for (i = 0; i < strlen(call); i++) {
			call[i] = toupper(call[i]);
		}
	}

	nfds = 1 + 1;
	fds = calloc(sizeof(struct pollfd), nfds);

	poll_int = 0;
	fds[poll_int].fd = -1;
	fds[poll_int].events = POLLIN;
	poll_is = 1;
	fds[poll_is].fd = -1;
	fds[poll_is].events = POLLIN;
	

	do {
		if (fd_int < 0) {
			fd_int = interface_init(netname, NULL, false, ETH_P_FPRS);
			if (fd_int < 0) {
				printf("Could not open interface: %s\n", strerror(errno));
			} else {
				fds[poll_int].fd = fd_int;
			}
		}
		if (fd_is < 0) {
			fd_is = tcp_connect(host, port);
			if (fd_is <= 0) {
			printf("Failed to connect to server %s:%d: %s\n",
			    host, port, strerror(errno));
			} else {
				char loginline[256];
				size_t loginline_len = sizeof(loginline) - 1;
				fprs2aprs_login(loginline, &loginline_len, call);
				if (write(fd_is, loginline, strlen(loginline)) < 0) {
					close(fd_is);
				} else {
					fds[poll_is].fd = fd_is;
				}
			}
		}
		poll(fds, nfds, 1000);
		if (fds[poll_int].revents & (POLLIN | POLLERR)) {
			if (interface_tx_raw(cb_int_tx)) {
				printf("Interface lost\n");
				close(fd_int);
				fd_int = -1;
				fds[poll_int].fd = -1;
			}
		}
		if (fds[poll_is].revents & (POLLIN | POLLERR)) {
			if (aprs_is_in()) {
				printf("APRS-IS connection lost\n");
				close(fd_is);
				fd_is = -1;
				fds[poll_is].fd = -1;
			}
		}
	} while (1);

	return 0;

err_usage:
	usage();
	return -1;
}
