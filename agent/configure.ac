#                                               -*- Autoconf -*-
# PACKTER Agent 3.0 — autoconf/automake build.
# Replaces the GNU-make-only Makefile so the agent builds portably on Linux,
# *BSD and macOS: ./configure detects libpcap, libm and (optionally)
# libmaxminddb, and links each tool against only what it needs.
AC_PREREQ([2.69])
AC_INIT([packter-agent],[3.0.0-beta.1],[https://github.com/daisu-mi/packter/issues])
AC_CONFIG_SRCDIR([src/pt_agent.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Wno-portability])

AC_PROG_CC
AM_PROG_AR
AC_PROG_RANLIB

# Optional ASan/UBSan build (replaces the old `make SANITIZE=1`).
AC_ARG_ENABLE([sanitizer],
  [AS_HELP_STRING([--enable-sanitizer],[build with AddressSanitizer/UBSan])],
  [AS_IF([test "x$enableval" = xyes],
     [SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
      CFLAGS="$CFLAGS $SAN"
      LDFLAGS="$LDFLAGS $SAN"])])

# Some platforms split these out (e.g. Solaris: -lsocket -lnsl).
AC_SEARCH_LIBS([socket],[socket])
AC_SEARCH_LIBS([gethostbyname],[nsl])

# libpcap — required by the live-capture tools (pt_agent, pt_thmon). Captured
# into PCAP_LIBS and linked ONLY into those, so the pure-UDP collectors and
# pt_replay run on hosts without libpcap.
AC_CHECK_HEADERS([pcap.h],[],[AC_MSG_ERROR([pcap.h not found — install the libpcap headers])])
ac_save_LIBS=$LIBS; LIBS=
AC_SEARCH_LIBS([pcap_open_live],[pcap],[PCAP_LIBS=$LIBS],
  [AC_MSG_ERROR([libpcap is required])])
LIBS=$ac_save_LIBS
AC_SUBST([PCAP_LIBS])

# libm for sqrtf() (pt_thmon's CUSUM/EWMA detectors).
ac_save_LIBS=$LIBS; LIBS=
AC_SEARCH_LIBS([sqrtf],[m],[MATH_LIBS=$LIBS])
LIBS=$ac_save_LIBS
AC_SUBST([MATH_LIBS])

# Optional libmaxminddb — enables PACKTEARTH (pt_agent -G). DB-IP "IP to City
# Lite" MMDB recommended (CC BY). --without-geoip disables; --with-geoip
# requires it.
AC_ARG_WITH([geoip],
  [AS_HELP_STRING([--with-geoip],[PACKTEARTH GeoIP via libmaxminddb @<:@default=check@:>@])],
  [],[with_geoip=check])
MMDB_LIBS=
AS_IF([test "x$with_geoip" != xno],[
  ac_save_LIBS=$LIBS; LIBS=
  AC_SEARCH_LIBS([MMDB_open],[maxminddb],
    [AC_CHECK_HEADER([maxminddb.h],
       [MMDB_LIBS=$LIBS
        AC_DEFINE([PACKTER_HAVE_GEOIP],[1],[Define to 1 if libmaxminddb is available])],
       [AS_IF([test "x$with_geoip" = xyes],[AC_MSG_ERROR([maxminddb.h not found])])])],
    [AS_IF([test "x$with_geoip" = xyes],[AC_MSG_ERROR([libmaxminddb not found])])])
  LIBS=$ac_save_LIBS
])
AC_SUBST([MMDB_LIBS])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_NOTICE([
  packter-agent $PACKAGE_VERSION configured:
    CC           : $CC
    libpcap      : ${PCAP_LIBS:-(none)}
    libm         : ${MATH_LIBS:-(none)}
    GeoIP (mmdb) : ${MMDB_LIBS:-disabled (-G unavailable)}
])
