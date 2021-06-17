AC_INIT([eth_ar], [0.2], [jeroen@vreeken.net], [], [https://video.vreeken.net/~dml/])
AM_INIT_AUTOMAKE([foreign dist-xz])
AM_SILENT_RULES([yes])
LT_INIT

AC_PROG_CC

AC_CONFIG_MACRO_DIR([m4])


dnl Search for libcodec2
PKG_CHECK_MODULES([CODEC2], [codec2], [], [
  AC_MSG_ERROR([unable to find libcodec2])
])

AC_CHECK_HEADERS([codec2/codec2.h codec2/freedv_api.h],
        [codec2_found_headers=yes; break;])

AS_IF([test "x$codec2_found_headers" != "xyes"],
        [AC_MSG_ERROR([Unable to find the codec2 headers])])



dnl Search for libsamplerate
PKG_CHECK_MODULES([SAMPLERATE], [samplerate], [samplerate_found_lib=yes], [samplerate_found_lib=no])

AC_CHECK_HEADERS([samplerate.h],
        [samplerate_found_headers=yes; break;])

AM_CONDITIONAL(ENABLE_SAMPLERATE, test "x$samplerate_found_headers" = "xyes")



dnl Search for libspeexdsp
PKG_CHECK_MODULES([SPEEXDSP], [speexdsp], [speexdsp_found_lib=yes], [speexdsp_found_lib=no])

AC_CHECK_HEADERS([speex/speex_preprocess.h],
        [speexdsp_found_headers=yes; break;])





dnl Search for hamlib
PKG_CHECK_MODULES([HAMLIB], [hamlib], [hamlib_found_lib=yes], [hamlib_found_lib=no])

AC_CHECK_HEADERS([hamlib/rig.h],
        [hamlib_found_headers=yes; break;])

AM_CONDITIONAL(ENABLE_HAMLIB, test "x$hamlib_found_headers" = "xyes")


AS_IF([test "x$hamlib_found_headers" == "xyes"],
        [AC_CHECK_DECLS([RIG_PTT_GPIO], [], [
	AC_MSG_ERROR([This version of hamlib does not support PTT by GPIO])],
	[[#include <hamlib/rig.h>]])]
)

AS_IF([test "x$hamlib_found_headers" == "xyes"],
        [AC_CHECK_DECLS([RIG_DCD_GPIO], [], [
	AC_MSG_ERROR([This version of hamlib does not support DCD by GPIO])],
	[[#include <hamlib/rig.h>]])]
)


AC_CHECK_DECLS([IFF_UP], [linux_if_found_headers=yes], [],
	[[#include <linux/if.h>]])

AM_CONDITIONAL(ENABLE_INTERFACE, test "x$linux_if_found_headers" = "xyes")


# libgpps
PKG_CHECK_MODULES([LIBGPS], [libgps], [libgps_found=yes], [libgps_found=no])

AM_CONDITIONAL(ENABLE_GPS, test "x$libgps_found" = "xyes")



AC_SEARCH_LIBS([cos], [m])


AC_CONFIG_FILES([
Makefile
eth_ar.pc
])

AC_OUTPUT


echo "Optional libs:"
echo "    hamlib          : " $hamlib_found_headers
echo "    libsamplerate   : " $samplerate_found_headers
echo "    libspeexdsp     : " $speexdsp_found_headers
echo "    linux/if header : " $linux_if_found_headers
echo "    libgps          : " $libgps_found
