#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([nwipe], [0.37], [git@brumit.nl])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/nwipe.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
PKG_PROG_PKG_CONFIG

# Checks for libraries.

PKG_CHECK_MODULES([PANEL], [panel],
    [CFLAGS="${CFLAGS} ${PANEL_CFLAGS}"
     LIBS="${LIBS} ${PANEL_LIBS}"],
    [AC_CHECK_LIB([panel], [main],
        [LIBS="-lpanel $LIBS"
         AC_CHECK_HEADERS([panel.h],
             [],
             [AC_CHECK_HEADERS([ncurses/panel.h],
                 [AC_DEFINE([PANEL_IN_SUBDIR], ["ncurses/"], [Look for ncurses headers in subdir])],
                 [AC_MSG_ERROR([ncurses panel headers not found])])])],
        [AC_MSG_ERROR([ncurses panel library not found])])])

PKG_CHECK_MODULES([NCURSES], [ncurses],
    [CFLAGS="${CFLAGS} ${NCURSES_CFLAGS}"
     LIBS="${LIBS} ${NCURSES_LIBS}"],
    [AC_CHECK_LIB([ncurses], [delscreen],
        [LIBS="-lncurses $LIBS"
         AC_CHECK_HEADERS([ncurses.h],
             [],
             [AC_CHECK_HEADERS([ncurses/ncurses.h],
                 [AC_DEFINE([NCURSES_IN_SUBDIR], ["ncurses/"], [Look for ncurses headers in subdir])],
                 [AC_MSG_ERROR([ncurses headers not found])])])],
        [AC_MSG_ERROR([ncurses development library not found])])])

PKG_CHECK_MODULES([OPENSSL], [openssl],
    [CFLAGS="${CFLAGS} ${OPENSSL_CFLAGS}"
     LIBS="${LIBS} ${OPENSSL_LIBS}"],
    [AC_CHECK_LIB([ssl], [SSL_library_init],
        [LIBS="-lssl -lcrypto $LIBS"
         AC_CHECK_HEADERS([openssl/ssl.h],
             [],
             [AC_CHECK_HEADERS([openssl/crypto.h],
                 [AC_DEFINE([OPENSSL_IN_SUBDIR], ["openssl/"], [Look for openssl headers in subdir])],
                 [AC_MSG_ERROR([openssl headers not found])])])],
        [AC_MSG_ERROR([OpenSSL development library not found])])])

PKG_CHECK_MODULES([LIBCONFIG], [libconfig],
    [CFLAGS="${CFLAGS} ${LIBCONFIG_CFLAGS}"
     LIBS="${LIBS} ${LIBCONFIG_LIBS}"],
    [AC_CHECK_LIB([config], [main],
        [LIBS="-lconfig $LIBS"
         AC_CHECK_HEADERS([libconfig.h],
             [],
             [AC_MSG_ERROR([libconfig headers not found])])],
        [AC_MSG_ERROR([libconfig library not found])])])

PKG_CHECK_MODULES([QRENCODE], [qrencode],
    [CFLAGS="${CFLAGS} ${QRENCODE_CFLAGS}"
     LIBS="${LIBS} ${QRENCODE_LIBS}"],
    [AC_CHECK_LIB([qrencode], [QRcode_encodeString],
        [LIBS="-lqrencode $LIBS"
         AC_CHECK_HEADERS([qrencode.h],
             [],
             [AC_MSG_ERROR([QRencode headers not found])])],
        [AC_MSG_ERROR([QRencode library not found])])])

AC_CHECK_LIB([intl], [libintl_dgettext], [], [])
AC_CHECK_LIB([uuid], [uuid_generate], [], [])
PKG_CHECK_MODULES([PARTED], [libparted], [], [AC_MSG_ERROR([libparted not found])])
AC_CHECK_LIB([pthread], [main], [], [AC_MSG_ERROR([pthread development library not found])])

# Checks for header files.
AC_CHECK_HEADERS([libconfig.h fcntl.h inttypes.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/file.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([fdatasync memset regcomp strdup strerror])

# Initialize and update git submodules if necessary.
AC_MSG_CHECKING([whether git submodules need to be initialized])
AS_IF([test -d ".git" && test -f ".gitmodules"],
    [git submodule status 2>/dev/null | grep -q "^[-+]" && (
        AC_MSG_RESULT([yes])
        AC_MSG_NOTICE([Initializing and updating git submodules...])
        git submodule init && git submodule update || AC_MSG_ERROR([Failed to initialize or update git submodules])
    ) || AC_MSG_RESULT([no])],
    [AC_MSG_NOTICE([No git repository or submodules found. Skipping submodule initialization.])])

AC_CONFIG_FILES([Makefile src/Makefile man/Makefile])
AC_OUTPUT

