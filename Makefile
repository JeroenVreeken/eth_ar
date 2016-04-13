include build.mk

CFLAGS += -Wall -Werror -O3
LDFLAGS += -lm -lasound -lhamlib -lcodec2 -lsamplerate

SRCS = \
	eth_ar.c \
	interface.c \
	sound.c \
	alaw.c \
	dsp.c \
	input.c

OBJS = $(SRCS:.c=.o)

ANALOG_TRX_OBJS = $(OBJS) analog_trx.o
FREEDV_ETH_OBJS = $(OBJS) freedv_eth.o

all: analog_trx freedv_eth

analog_trx: $(ANALOG_TRX_OBJS)

freedv_eth: $(FREEDV_ETH_OBJS)


DEPS:=$(SRCS:.c=.d) freedv_eth.d eth_ar.d
-include $(DEPS)

$(OBJS): Makefile

clean:
	rm -rf $(OBJS) $(DEPS) analog_trx freedv_eth analog_trx.o freedv_eth.o \

