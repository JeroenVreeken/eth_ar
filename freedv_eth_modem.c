#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "freedv_eth.h"
#include "freedv_eth_rx.h"

static bool modem_tx;
static int nr_sym;
static signed char *rx_sym = NULL;
static int rx_sym_cur;

struct txbuffer {
	signed char *buffer;
	size_t size;
	
	struct txbuffer *next;
};

static struct txbuffer *txq = NULL;


int freedv_eth_modem_init(char *modem_file, struct freedv *freedv)
{
	modem_tx = false;

#if defined(FREEDV_MODE_6000)
	int fd_modem = open(modem_file, O_RDWR);
	if (fd_modem < 0) {
		printf("Could not open modem: %s\n", modem_file);
		return -1;
	}
	fcntl(fd_modem, F_SETFL, O_NONBLOCK);
	//set: TIOCMBIS 
	//clear: TIOCMBIC
	ioctl(fd_modem, TIOCMBIC, &(int){TIOCM_DTR});
	ioctl(fd_modem, TIOCMBIC, &(int){TIOCM_RTS});

	nr_sym = freedv_get_n_modem_symbols(freedv);
	rx_sym = realloc(rx_sym, nr_sym);
	
	return fd_modem;
#else
	return -1;
#endif
}

void freedv_eth_modem_poll(short *events)
{
	*events = POLLIN | (txq ? POLLOUT : 0);
}

void freedv_eth_modem_rx(int fd_modem)
{
	ssize_t r = read(fd_modem, rx_sym + rx_sym_cur, nr_sym - rx_sym_cur);
	if (r > 0) {
		rx_sym_cur += r;
	}
	if (rx_sym_cur == nr_sym) {
		freedv_eth_symrx(rx_sym);
		rx_sym_cur = 0;
	}
}

void freedv_eth_modem_tx(int fd_modem)
{
	ssize_t r = write(fd_modem, txq->buffer, txq->size);
	if (r > 0) {
		if (r < txq->size) {
			size_t newsize = txq->size - r;
			memmove(txq->buffer, &txq->buffer[r], newsize);
			txq->buffer = realloc(txq->buffer, newsize);
		} else {
			struct txbuffer *old = txq;
			txq = txq->next;
			free(old->buffer);
			free(old);
		}
	}
}

void freedv_eth_modem_tx_add(signed char *tx_sym, size_t nr)
{
	struct txbuffer *entry = malloc(sizeof(struct txbuffer));
	
	if (entry) {
		entry->buffer = malloc(nr);
		if (entry->buffer) {
			entry->next = NULL;
			entry->size = nr;
			memcpy(entry->buffer, tx_sym, nr);
			
			struct txbuffer **q = &txq;
			while(*q)
				q = &(*q)->next;
			*q = entry;
		} else {
			free(entry);
		}
	}
}

bool freedv_eth_modem_tx_empty(int fd_modem)
{
	int outq = 0;
	bool empty = false;
	
	if (!ioctl(fd_modem, TIOCOUTQ, &outq))
	{
		empty = (outq == 0);
	}

	return empty;
}
