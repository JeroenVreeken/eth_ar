AC_INIT([eth_ar], [0.0], [jeroen@vreeken.net], [https://video.vreeken.net/~dml/])
AM_INIT_AUTOMAKE([foreign dist-xz])
AM_SILENT_RULES([yes])
LT_INIT

AC_PROG_CC

AC_CONFIG_MACRO_DIR([m4])


dnl Search for libcodec2
AC_SEARCH_LIBS([freedv_set_data_header], [codec2], [], [
  AC_MSG_ERROR([unable to find the freedv data channel functions in libcodec2])
])

AC_CHECK_HEADERS([codec2/codec2.h codec2/freedv_api.h],
        [codec2_found_headers=yes; break;])

AS_IF([test "x$codec2_found_headers" != "xyes"],
        [AC_MSG_ERROR([Unable to find the codec2 headers])])


dnl Search for libsamplerate
AC_SEARCH_LIBS([src_process], [samplerate], [], [
  AC_MSG_ERROR([unable to find the src_process() function in libsamplerate])
])

AC_CHECK_HEADERS([samplerate.h],
        [samplerate_found_headers=yes; break;])

AS_IF([test "x$samplerate_found_headers" != "xyes"],
        [AC_MSG_ERROR([Unable to find the samplerate headers])])


dnl Search for hamlib
AC_SEARCH_LIBS([rig_init], [hamlib], [], [
  AC_MSG_ERROR([unable to find the rig_init() function in hamlib])
])

AC_CHECK_HEADERS([hamlib/rig.h],
        [hamlib_found_headers=yes; break;])

AS_IF([test "x$hamlib_found_headers" != "xyes"],
        [AC_MSG_ERROR([Unable to find the hamlib headers])])

AC_CHECK_DECLS([RIG_PTT_GPIO], [], [
	AC_MSG_ERROR([This version of hamlib does not support PTT by GPIO])],
	[[#include <hamlib/rig.h>]])


AC_OUTPUT([Makefile])

