include build.mk

CFLAGS += -Wall -Werror -O3
LDFLAGS += -lm -lasound -lhamlib -lcodec2

SRCS = \
	freedv_eth.c \
	eth_ar.c \
	interface.c \
	sound.c

OBJS = $(SRCS:.c=.o)

all: freedv_eth

freedv_eth: $(OBJS)

DEPS:=$(SRCS:.c=.d)
-include $(DEPS)

$(OBJS): Makefile

clean:
	rm -rf $(OBJS) $(DEPS)\
		freedv_eth

