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

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>

static int fd;
static uint16_t eth_mac[6];

int interface_rx(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len)
{
	uint8_t packet[len + 14];
	
	memcpy(packet, to, 6);
	memcpy(packet + 6, from, 6);
	packet[12] = eth_type >> 8;
	packet[13] = eth_type & 0xff;
	
	memcpy(packet + 14, data, len);
	
//	printf("Packet to interface %zd\n", sizeof(packet));
	write(fd, packet, sizeof(packet));
	return 0;
}

int interface_tx(int (*cb)(uint8_t to[6], uint8_t from[6], uint16_t eth_type, uint8_t *data, size_t len))
{
	uint8_t data[2048];
	size_t len;
	
	len = read(fd, data, 2048);
	if (len > 14) {
//		int i;
		uint16_t eth_type = (data[12] << 8) | data[13];
		
		return cb(data, data + 6, eth_type, data + 14, len - 14);
	}
	
	return 0;
}

/* Create a socket on an existing device */
static int sock_alloc(char *dev)
{
	short protocol = htons(ETH_P_ALL);
	int sock;
	
	sock = socket(AF_PACKET, SOCK_RAW, protocol);
	if (sock < 0)
		goto err_socket;

	struct ifreq ifr;

	size_t if_name_len = strlen(dev);
	if (if_name_len >= sizeof(ifr.ifr_name))
		goto err_len;
	strcpy(ifr.ifr_name, dev);

	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0)
		goto err_ioctl;
	
	int ifindex = ifr.ifr_ifindex;

	struct sockaddr_ll sll = { 0 };
	
	sll.sll_family = AF_PACKET; 
	sll.sll_ifindex = ifindex;
	sll.sll_protocol = protocol;
	if(bind(sock, (struct sockaddr *)&sll , sizeof(sll)) < 0)
		goto err_bind;

	return sock;
err_bind:
err_ioctl:
err_len:
	close(sock);
err_socket:

	return -1;
}

/* Create a TAP device */
static int tap_alloc(char *dev, uint8_t mac[6])
{
	struct ifreq ifr = { };
	int fd;
	char *tundev = "/dev/net/tun";

	/* open the tun device */
	if((fd = open(tundev, O_RDWR)) < 0 ) {
		return -1;
	}

	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

	if (*dev) {
		/* if a device name was specified, put it in the structure; otherwise,
		 * the kernel will try to allocate the "next" device of the
		 * specified type */
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	/* try to create the device */
	if(ioctl(fd, TUNSETIFF, &ifr) < 0 ) {
		printf("Creating tap device failed\n");
		close(fd);
		return -1;
	}
	
	ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
	memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);

	if (ioctl(fd, SIOCSIFHWADDR, &ifr) < 0) {
		printf("Setting HWADDR %02x:%02x:%02x:%02x:%02x:%02x failed\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

		close(fd);
		return -1;
	}

	return fd;
}

int interface_init(char *name, uint8_t mac[6], bool tap)
{
	if (name == NULL)
		name = "freedv";
	if (tap)
		fd = tap_alloc(name, mac);
	else
		fd = sock_alloc(name);
	memcpy(eth_mac, mac, 6);
	
	return fd;
}
