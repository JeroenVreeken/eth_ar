include build.mk

CFLAGS += -Wall -Werror -O3
LDFLAGS += -lm -lasound -lhamlib -lcodec2 -lsamplerate

SRCS = \
	sound.c \
	alaw.c \
	dsp.c \
	input.c

OBJS = $(SRCS:.c=.o)

ETH_AR_SRCS = \
	eth_ar.c

ETH_AR_OBJS = $(ETH_AR_SRCS:.c=.o)

INTERFACE_SRCS = \
	interface.c

INTERFACE_OBJS = $(INTERFACE_SRCS:.c=.o)

FPRS_SRCS = \
	fprs.c \
	fprs2aprs.c \
	nmea.c

FPRS_OBJS = $(FPRS_SRCS:.c=.o)

ANALOG_TRX_OBJS = $(OBJS) $(ETH_AR_OBJS) $(INTERFACE_OBJS) analog_trx.o
FREEDV_ETH_OBJS = $(OBJS) $(ETH_AR_OBJS) $(INTERFACE_OBJS) $(FPRS_OBJS) freedv_eth.o

all: analog_trx freedv_eth fprs_test fprs2aprs_gate

analog_trx: $(ANALOG_TRX_OBJS)

freedv_eth: $(FREEDV_ETH_OBJS)

fprs_test: $(FPRS_OBJS) $(ETH_AR_OBJS) fprs_test.o

fprs2aprs_gate: $(FPRS_OBJS) $(ETH_AR_OBJS) $(INTERFACE_OBJS) fprs2aprs_gate.o


DEPS:=$(SRCS:.c=.d) $(ETH_AR_SRCS:.c=.d) $(FPRS_SRCS:.c=.d) $(INTERFACES_SRCS:.c=.d) \
	freedv_eth.d eth_ar.d fprs_test.d fprs2aprs_gate.d

-include $(DEPS)

$(OBJS): Makefile

clean:
	rm -rf $(OBJS) $(ETH_AR_OBJS) $(FPRS_OBJS) $(INTERFACE_OBJS) $(DEPS) \
		analog_trx analog_trx.o \
		freedv_eth freedv_eth.o \
		fprs_test fprs_test.o \
		fprs2aprs_gate fprs2aprs_gate.o

