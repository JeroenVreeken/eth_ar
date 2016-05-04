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
#include "fprs.h"
#include "eth_ar.h"

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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#ifdef __linux__
#include <linux/sockios.h>
#endif


bool fullduplex = false;
char *call = NULL;
uint8_t mac[6];
uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
int fd_is;
int fd_int;

int info_t;
int info_timeout = 60 * 30;
double info_longitude = 0.0;
double info_latitude = 0.0;
char *info_text = "FPRS gate";
struct fprs_frame *info_frame;

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

#ifndef __FreeBSD__
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

static void aprs_is_in(void)
{
	char buffer[256];
	ssize_t r;
	
	r = read(fd_is, buffer, 256);
	if (r > 0) {
		write(2, buffer, r);
	}
}

static int fprs2aprs_is(struct fprs_frame *frame, uint8_t *from)
{
	char aprs[256] = { 0 };
	size_t aprs_size = 255;
	
	if (fprs2aprs(aprs, &aprs_size, frame, from, call))
		return -1;

	printf("%s", aprs);
	write(fd_is, aprs, strlen(aprs));

	return 0;
}

static int int_out(struct fprs_frame *frame)
{
	uint8_t data[256];
	size_t size = 256;
	
	fprs_frame_data_get(frame, data, &size);
	interface_rx(bcast, mac, ETH_P_FPRS, data, size);

	return 0;
}

static int cb_int_tx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	struct fprs_frame *frame = fprs_frame_create();

	fprs_frame_data_set(frame, data, len);
	
	fprs2aprs_is(frame, from);
	
	if (fullduplex) {
		fprs_frame_add_callsign(frame, from);
		int_out(frame);
	}
	
	fprs_frame_destroy(frame);

	return 0;
}

void info_tx(void)
{
	fprs2aprs_is(info_frame, mac);
	int_out(info_frame);
}

void info_create(void)
{
	info_frame = fprs_frame_create();
	
	fprs_frame_add_position(info_frame, 
	    info_longitude, info_latitude, true);

	fprs_frame_add_symbol(info_frame, (uint8_t[2]){'F','&'});

	if (strlen(info_text))
		fprs_frame_add_comment(info_frame, info_text);
}


#define kKey 0x73e2 // This is the seed for the key

static void login(void)
{
	char loginline[256];
	char rootCall[10]; // need to copy call to remove ssid from parse
	char *p0 = call;
	char *p1 = rootCall;
	short hash;
	short i,len;
	char *ptr = rootCall;

	while ((*p0 != '-') && (*p0 != '\0'))
		*p1++ = *p0++;
	*p1 = '\0';

	hash = kKey; // Initialize with the key value
	i = 0;
	len = (short)strlen(rootCall);

	while (i<len) {// Loop through the string two bytes at a time
		hash ^= (unsigned char)(*ptr++)<<8; // xor high byte with accumulated hash
		hash ^= (*ptr++); // xor low byte with accumulated hash
		i += 2;
	}
	hash = (short)(hash & 0x7fff); // mask off the high bit so number is always positive
	
	printf("Call: %s\n", rootCall);
	printf("Pass: %d\n", hash);

	sprintf(loginline, "user %s pass %d vers fprs2aprs_gate 0.1\r\n",
	    call, hash);

	write(fd_is, loginline, strlen(loginline));
}

char *netname = "freedv";
char *host = "euro.aprs2.net";
int port = 14580;

static void usage(void)
{
	printf("Options:\n");
	printf("-v\tverbose\n");
	printf("-c [call]\town callsign\n");
	printf("-f\tFullduplex (digipeat received frames)\n");
	printf("-h [host]\tAPRS-IS server (default: \"%s\")\n", host);
	printf("-n [dev]\tNetwork device name (default: \"%s\")\n", netname);
	printf("-p [port]\tAPRS-IS port (default: %d)\n", port);
	printf("-t [seconds]\tInfo timeout (default: %d)\n", info_timeout);
	printf("-i [text]\tInfo text (default: \"%s\")\n", info_text);
	printf("-o [longitude]\tInfo longitude (default: %f)\n", info_longitude);
	printf("-a [latitude]\tInfo latitude (default: %f)\n", info_latitude);
}

int main(int argc, char **argv)
{
	struct pollfd *fds;
	int nfds;
	int poll_int, poll_is;
	int opt;
	
	while ((opt = getopt(argc, argv, "a:vc:n:fh:i:p:o:t:")) != -1) {
		switch(opt) {
			case 'a':
				info_latitude = atof(optarg);
				break;
			case 'c':
				call = optarg;
				break;
			case 'f':
				fullduplex = true;
			case 'h':
				host = optarg;
				break;
			case 'i':
				info_text = optarg;
				break;
			case 'o':
				info_longitude = atof(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'n':
				netname = optarg;
				break;
			case 't':
				info_timeout = atoi(optarg);
				break;
			default:
				goto err_usage;
		}
	}
	
	info_t = info_timeout;
	if (info_longitude != 0.0 || info_latitude != 0.0) {
		info_create();
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

	if (eth_ar_callssid2mac(mac, call, false)) {
		printf("Callsign could not be converted to a valid MAC address\n");
		goto err_usage;
	}

	fd_int = interface_init(netname, mac, false, ETH_P_FPRS);
	if (fd_int < 0) {
		printf("Could not open interface: %s\n", strerror(errno));
		goto err_usage;
	}

	fd_is = tcp_connect(host, port);
	if (fd_is <= 0) {
		printf("Failed to connect to server %s:%d: %s\n",
		    host, port, strerror(errno));
		goto err_usage;
	}
	login();

	nfds = 1 + 1;
	fds = calloc(sizeof(struct pollfd), nfds);

	poll_int = 0;
	fds[poll_int].fd = fd_int;
	fds[poll_int].events = POLLIN;
	poll_is = 1;
	fds[poll_is].fd = fd_is;
	fds[poll_is].events = POLLIN;
	

	do {
		poll(fds, nfds, 1000);
		if (fds[poll_int].revents & POLLIN) {
			interface_tx(cb_int_tx);
		}
		if (fds[poll_is].revents & POLLIN) {
			aprs_is_in();
		}
		
		info_t++;
		if (info_t >= info_timeout) {
			info_t = 0;
			if (info_frame)
				info_tx();
		}
	} while (1);

	return 0;

err_usage:
	usage();
	return -1;
}
