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
 
#include <eth_ar/eth_ar.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/if.h>

void usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("eth_ar_if <dev>\n");
	printf("eth_ar_if <dev> <call>\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	int sock;
	bool set_call = false;
	char *call = "";
	char *dev = "";
	struct ifreq ifr = {};

	if (argc < 2) {
		usage();
		goto err_usage;
	}
	dev = argv[1];
	if (argc > 2) {
		set_call = true;
		call = argv[2];
	}
	
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("Could not open socket");
		goto err_socket;
	}
	
	strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);
	
	if (!set_call) {
		char callsign[ETH_AR_CALL_SIZE];
		int ssid;
		bool multicast;
		uint8_t *mac;
		
		if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
			perror("Could not get HW address");
			goto err_gethw;
		}
		mac = (uint8_t*)ifr.ifr_hwaddr.sa_data;
		if (eth_ar_mac2call(callsign, &ssid, &multicast, mac)) {
			perror("Could not extract callsign\n");
			goto err_getmac;
		}
		printf("%s: Call: %s-%d\n", dev, call, ssid);
	} else {
		uint8_t mac[6];
		
		if (eth_ar_callssid2mac(mac, call, false)) {
			perror("Could not convert callsign\n");
			goto err_setmac;
		}
		
		memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
		if (ioctl(sock, SIOCSIFHWADDR, &ifr) < 0) {
			perror("Could not set HW address");
			goto err_sethw;
		}
	}
	
	close(sock);

	return 0;

err_setmac:
err_getmac:
err_gethw:
err_sethw:
	close(sock);
err_socket:
err_usage:
	return -1;
}
