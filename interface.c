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
#include <errno.h>
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
static bool outgoing = false;

int interface_rx(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level)
{
	size_t packet_size = len + sizeof(struct eth_ar_voice_header);
	uint8_t packet[packet_size];
	
	struct eth_ar_voice_header *header = (void*)packet;
	
	memcpy(header->to, to, 6);
	memcpy(header->from, from, 6);
	header->type = htons(eth_type);
	header->nr = transmission;
	header->level = level;
	
	memcpy(packet + sizeof(struct eth_ar_voice_header), data, len);
	
//	printf("Packet to interface %zd\n", packet_size);
	return write(fd, packet, packet_size) <= 0;
}
int interface_rx_raw(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len)
{
	uint8_t packet[len + 14];
	
	memcpy(packet, to, 6);
	memcpy(packet + 6, from, 6);
	packet[12] = eth_type >> 8;
	packet[13] = eth_type & 0xff;
	
	memcpy(packet + 14, data, len);
	
//	printf("Packet to interface %zd\n", sizeof(packet));
	return write(fd, packet, sizeof(packet)) <= 0;
}

static int interface_tx_tap(size_t doff, int (*cb)(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level))
{
	uint8_t data[2048];
	struct eth_ar_voice_header *header = (void*)data;
	size_t len;
	
	len = read(fd, data, 2048);
	if (len > doff) {
//		int i;
		uint16_t eth_type = ntohs(header->type);
		
		return cb(header->to, header->from, eth_type, data + doff, len - doff, header->nr, header->level);
	}
	
	return 0;
}


static int interface_tx_sock(size_t doff, int (*cb)(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level))
{
	uint8_t data[2048];
	struct eth_ar_voice_header *header = (void*)data;
	ssize_t len;
	
	struct sockaddr_ll addr;
        socklen_t addr_len = sizeof(addr);
        
	len = recvfrom(fd, data, 2048, 0, (struct sockaddr*)&addr, &addr_len);

//	len = read(fd, data, 2048);
	if (len > doff) {
//		int i;
		
	        if (addr.sll_pkttype != PACKET_OUTGOING || outgoing) {
			uint16_t eth_type = ntohs(header->type);
		
			return cb(header->to, header->from, eth_type, data + doff, len - doff, header->nr, header->level);
		}
	}
	
	if (len <= 0)
		return -1;
	return 0;
}

static int (*interface_tx_func)(size_t doff, int (*cb)(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level));
int interface_tx(int (*cb)(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len, uint8_t transmission, uint8_t level))
{
	return interface_tx_func(sizeof(struct eth_ar_voice_header), cb);
}
int interface_tx_raw(int (*cb)(uint8_t to[ETH_AR_MAC_SIZE], uint8_t from[ETH_AR_MAC_SIZE], uint16_t eth_type, uint8_t *data, size_t len))
{
	return interface_tx_func(14, (void*)cb);
}


/* Create a socket on an existing device */
static int sock_alloc(char *dev, uint16_t filter_type)
{
	short protocol;
	int sock;
	
	if (!filter_type)
		protocol = htons(ETH_P_ALL);
	else
		protocol = htons(filter_type);
	
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
	interface_tx_func = interface_tx_sock;

	return sock;
err_bind:
err_ioctl:
err_len:
	close(sock);
err_socket:
	printf("Could not open socket for dev '%s': %s\n", dev, strerror(errno));

	return -1;
}

/* Create a TAP device */
static int tap_alloc(char *dev, uint8_t mac[ETH_AR_MAC_SIZE])
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
		strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);
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

	interface_tx_func = interface_tx_tap;

	int sock;
	
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return -1;

	if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
		perror("Getting flags failed");
		close(sock);
		close(fd);
		return -1;
	}
	ifr.ifr_flags |= IFF_UP || IFF_RUNNING;
	if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
		perror("Setting flags failed");
		close(sock);
		close(fd);
		return -1;
	}

	close(sock);

	return fd;
}

int interface_init(char *name, uint8_t mac[ETH_AR_MAC_SIZE], bool tap, uint16_t filter_type)
{
	if (name == NULL)
		name = "freedv";
	if (tap)
		fd = tap_alloc(name, mac);
	else
		fd = sock_alloc(name, filter_type);
	
	return fd;
}

int interface_tx_outgoing(bool enable)
{
	outgoing = enable;
	return 0;
}
