/*
	Copyright Jeroen Vreeken (jeroen@vreeken.net), 2019

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
#include <string.h>

void usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("eth_ar_if <call>\n");
	printf("eth_ar_if <call>-<ssid>\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	char *call = "";

	if (argc < 2) {
		usage();
		goto err_usage;
	}
	call = argv[1];
	
	uint8_t mac[6];

	if (eth_ar_callssid2mac(mac, call, false)) {
		perror("Could not convert callsign\n");
		goto err_2mac;
	}

	char callsign[ETH_AR_CALL_SIZE];
	int ssid = 0;
	bool multicast;
	if (eth_ar_mac2call(callsign, &ssid, &multicast, mac)) {
		goto err_2call;
	}

	printf("%s-%d\n", callsign, ssid);
	printf("%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return 0;

err_2call:
err_2mac:
err_usage:
	return -1;
}
