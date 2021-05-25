ACLOCAL_AMFLAGS=-I m4

CFLAGS+= -Wall -Werror -O3

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = eth_ar.pc

lib_LTLIBRARIES=libeth_ar.la

libeth_ar_la_SOURCES= eth_ar.c fprs.c fprs2aprs.c alaw.c ulaw.c 
libeth_ar_la_CFLAGS=-fPIC 
libeth_ar_la_LDFLAGS= -fPIC -version-info 2:0:2 -lm

nobase_include_HEADERS = eth_ar/eth_ar.h eth_ar/fprs.h eth_ar/alaw.h eth_ar/ulaw.h

bin_PROGRAMS = eth_ar_callssid2mac
noinst_PROGRAMS = eth_ar_if fprs_test emphasis_test eth_ar_test test_eth dtmf_test ctcss_test
TESTS = fprs_test eth_ar_test dtmf_test ctcss_test

if ENABLE_HAMLIB
if ENABLE_SAMPLERATE
bin_PROGRAMS += analog_trx freedv_eth fprs2aprs_gate eth_ar_if fprs_request fprs_destination fprs_monitor eth_ar_callssid2mac

analog_trx_SOURCES = sound.c dsp.c io.c interface.c analog_trx.c freedv_eth_config.c
analog_trx_LDADD = libeth_ar.la
analog_trx_LDFLAGS = $(CODEC2_LIBS) -lsamplerate -lasound -lhamlib -lpthread -lm $(SPEEXDSP_LIBS)

freedv_eth_SOURCES = sound.c dsp.c io.c interface.c nmea.c freedv_eth.c freedv_eth_modem.c freedv_eth_rx.c freedv_eth_config.c freedv_eth_transcode.c freedv_eth_queue.c freedv_eth_tx.c freedv_eth_txa.c ctcss.c beacon.c emphasis.c freedv_eth_rxa.c freedv_eth_baseband_in.c
freedv_eth_LDADD = libeth_ar.la
freedv_eth_LDFLAGS = $(CODEC2_LIBS) -lsamplerate -lasound -lhamlib -lpthread -lm $(SPEEXDSP_LIBS)

endif
endif

eth_ar_test_SOURCES = eth_ar_test.c
eth_ar_test_LDADD = libeth_ar.la

fprs_test_SOURCES = nmea.c fprs_test.c
fprs_test_LDADD = libeth_ar.la

dtmf_test_SOURCES = dsp.c dtmf_test.c

ctcss_test_SOURCES = dsp.c ctcss_test.c

if ENABLE_INTERFACE
bin_PROGRAMS += fprs2aprs_gate fprs_request fprs_destination fprs_monitor

test_eth_SOURCES = interface.c beacon.c test_eth.c freedv_eth_config.c
test_eth_LDADD = libeth_ar.la
test_eth_LDFLAGS = -lm

fprs2aprs_gate_SOURCES = fprs2aprs_gate.c interface.c
fprs2aprs_gate_LDADD = libeth_ar.la

fprs_request_SOURCES = fprs_request.c interface.c
fprs_request_LDADD = libeth_ar.la

fprs_destination_SOURCES = fprs_destination.c interface.c
fprs_destination_LDADD = libeth_ar.la

fprs_monitor_SOURCES = fprs_monitor.c interface.c
fprs_monitor_LDADD = libeth_ar.la

eth_ar_if_SOURCES = eth_ar_if.c
eth_ar_if_LDADD = libeth_ar.la


if ENABLE_GPS

bin_PROGRAMS += fprs_gps

fprs_gps_SOURCES = fprs_gps.c interface.c
fprs_gps_LDADD = libeth_ar.la
fprs_gps_LDFLAGS = -lgps -lm

endif

endif

emphasis_test_SOURCES = emphasis_test.c emphasis.c
emphasis_test_LDFLAGS = -lm

eth_ar_callssid2mac_SOURCES = eth_ar_callssid2mac.c
eth_ar_callssid2mac_LDADD = libeth_ar.la
