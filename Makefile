include build.mk

CFLAGS += -Wall -Werror -O3
LDFLAGS += -lm -lasound -lhamlib -lcodec2 -lsamplerate

SRCS = \
	analog_trx.c \
	eth_ar.c \
	interface.c \
	sound.c \
	alaw.c \
	dsp.c

OBJS = $(SRCS:.c=.o)

all: analog_trx

analog_trx: $(OBJS)

DEPS:=$(SRCS:.c=.d)
-include $(DEPS)

$(OBJS): Makefile

clean:
	rm -rf $(OBJS) $(DEPS)\
		analog_trx

