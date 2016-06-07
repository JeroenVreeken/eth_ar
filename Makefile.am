ACLOCAL_AMFLAGS=-I m4

CFLAGS+= -Wall -Werror -O3

lib_LTLIBRARIES=libeth_ar.la

libeth_ar_la_SOURCES= eth_ar.c fprs.c fprs2aprs.c
libeth_ar_la_CFLAGS=-fPIC 
libeth_ar_la_LDFLAGS= -shared -fPIC -version-info 0:0:0 -lm

nobase_include_HEADERS = eth_ar/eth_ar.h eth_ar/fprs.h


bin_PROGRAMS = analog_trx freedv_eth fprs2aprs_gate
noinst_PROGRAMS = fprs_test

analog_trx_SOURCES = sound.c alaw.c dsp.c input.c interface.c beacon.c analog_trx.c
analog_trx_LDADD = libeth_ar.la
analog_trx_LDFLAGS = -lcodec2 -lsamplerate -lasound -lhamlib

freedv_eth_SOURCES = sound.c alaw.c dsp.c input.c interface.c nmea.c freedv_eth.c
freedv_eth_LDADD = libeth_ar.la
freedv_eth_LDFLAGS = -lcodec2 -lsamplerate -lasound -lhamlib

fprs_test_SOURCES = nmea.c fprs_test.c
fprs_test_LDADD = libeth_ar.la

fprs2aprs_gate_SOURCES = fprs2aprs_gate.c interface.c
fprs2aprs_gate_LDADD = libeth_ar.la


