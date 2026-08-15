#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([nwipe],[0.42],[git@brumit.nl])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_FILES([Makefile src/Makefile man/Makefile])
AC_CONFIG_SRCDIR([src/nwipe.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
PKG_PROG_PKG_CONFIG

# Checks for libraries.
PKG_CHECK_MODULES([NCURSES],   [ncurses])
PKG_CHECK_MODULES([PANEL],     [panel])
PKG_CHECK_MODULES([LIBCONFIG], [libconfig])
PKG_CHECK_MODULES([PARTED],    [libparted])

AC_SEARCH_LIBS([pthread_create], [pthread], [], [AC_MSG_ERROR([pthread development library not found])])
AC_SEARCH_LIBS([clock_gettime], [rt])

# Optional NVMe secure erase support.
AC_ARG_WITH([libnvme],
  [AS_HELP_STRING([--without-libnvme], [Build without NVMe secure erase support])],
  [], [with_libnvme=check])
have_libnvme=no
AS_IF([test "x$with_libnvme" != xno],
  [PKG_CHECK_MODULES([LIBNVME], [libnvme >= 1.0],
    [
      AC_DEFINE([HAVE_LIBNVME], [1], [Define if libnvme is available])
      have_libnvme=yes
    ],
    [AS_IF([test "x$with_libnvme" = xyes],
      [AC_MSG_ERROR([libnvme >= 1.0 required but not found])],
      [AC_MSG_WARN([libnvme not found, NVMe secure erase support disabled])])])])
AM_CONDITIONAL([HAVE_LIBNVME], [test "x$have_libnvme" = xyes])

have_sanact_verif=no
have_nvme_version_project=no
libnvme_version=
AS_IF([test "x$have_libnvme" = xyes], [
  save_CPPFLAGS=$CPPFLAGS
  CPPFLAGS="$CPPFLAGS $LIBNVME_CFLAGS"
  libnvme_version=$($PKG_CONFIG --modversion libnvme)
  AC_CHECK_DECL([NVME_SANITIZE_SANACT_EXIT_MEDIA_VERIF],
    [AC_DEFINE([HAVE_NVME_SANITIZE_SANACT_EXIT_MEDIA_VERIF], [1],
       [Define if libnvme has NVME_SANITIZE_SANACT_EXIT_MEDIA_VERIF])
     have_sanact_verif=yes],
    [],
    [[#include <libnvme.h>]])
  AC_CHECK_DECL([NVME_VERSION_PROJECT],
    [AC_DEFINE([HAVE_NVME_VERSION_PROJECT], [1],
       [Define if libnvme has NVME_VERSION_PROJECT])
     have_nvme_version_project=yes],
    [AC_DEFINE_UNQUOTED([NWIPE_LIBNVME_VERSION], ["$libnvme_version"],
       [libnvme version from pkg-config, used when NVME_VERSION_PROJECT is missing])],
    [[#include <libnvme.h>]])
  CPPFLAGS=$save_CPPFLAGS
])

# Libraries needed for static linking with parted.
AS_CASE([" $LDFLAGS "], [*" -static "*|*" -all-static "*], [
  save_LIBS=$LIBS; LIBS=
  AC_CHECK_LIB([uuid], [uuid_generate], [LIBS="-luuid $LIBS"],
    [AC_MSG_ERROR([libuuid is required for static linking but not found])])
  AC_CHECK_LIB([devmapper], [dm_task_create], [LIBS="-ldevmapper $LIBS"],
    [AC_MSG_ERROR([libdevmapper is required for static linking but not found])])
  AC_CHECK_LIB([blkid], [blkid_new_probe], [LIBS="-lblkid $LIBS"],
    [AC_MSG_ERROR([libblkid is required for static linking but not found])])
  PARTED_STATIC_LIBS=$LIBS; LIBS=$save_LIBS
])
AC_SUBST([PARTED_STATIC_LIBS])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h netinet/in.h stddef.h stdint.h stdlib.h \
                  string.h sys/file.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])

# Checks for library functions.
AC_CHECK_FUNCS([malloc fdatasync memset regcomp strdup strerror])

AC_OUTPUT

# Configuration summary.
AS_ECHO([""])
AS_ECHO(["  ---------------------------------------------"])
AS_ECHO(["   nwipe $VERSION"])
AS_ECHO(["  ---------------------------------------------"])
AS_ECHO(["    prefix ........................ $prefix"])
AS_ECHO(["    CC ............................ $CC"])
AS_ECHO(["    CXX ........................... $CXX"])
AS_ECHO(["    CFLAGS ........................ $CFLAGS"])
AS_ECHO(["    CXXFLAGS ...................... $CXXFLAGS"])
AS_ECHO(["    CPPFLAGS ...................... $CPPFLAGS"])
AS_ECHO(["    LDFLAGS ....................... $LDFLAGS"])
AS_ECHO([""])
AS_ECHO(["    NVMe secure erase ............. $have_libnvme"])
AS_IF([test "x$have_libnvme" = xyes], [
  AS_ECHO(["      libnvme version ............. $libnvme_version"])
  AS_ECHO(["      NVME_VERSION_PROJECT ........ $have_nvme_version_project"])
  AS_ECHO(["      SANACT_EXIT_MEDIA_VERIF ..... $have_sanact_verif"])
])
AS_ECHO(["  ---------------------------------------------"])
AS_ECHO([""])