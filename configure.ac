#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.64])
AC_INIT([nwipe], [0.26], [git@brumit.nl])
AM_INIT_AUTOMAKE(foreign subdir-objects)
AC_OUTPUT(Makefile src/Makefile man/Makefile)
AC_CONFIG_SRCDIR([src/nwipe.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
PKG_PROG_PKG_CONFIG

# Checks for libraries.

PKG_CHECK_MODULES(
	[PANEL],
	[panel],
	[
		CFLAGS="${CFLAGS} ${PANEL_CFLAGS}"
		LIBS="${LIBS} ${PANEL_LIBS}"
	],
	[AC_CHECK_LIB([panel], [main], [
		LIBS="-lpanel $LIBS"
		AC_CHECK_HEADERS(panel.h,, [
		    AC_CHECK_HEADERS(ncurses/panel.h, [
			AC_DEFINE([PANEL_IN_SUBDIR], [ncurses/], [Look for ncurses headers in subdir])
			], [AC_MSG_ERROR([ncurses panel headers not found])])
		])
	    ], [AC_MSG_ERROR([ncurses panel library not found])])]
)

PKG_CHECK_MODULES(
	[NCURSES],
	[ncurses],
	[
		CFLAGS="${CFLAGS} ${NCURSES_CFLAGS}"
		LIBS="${LIBS} ${NCURSES_LIBS}"
	],
	[AC_CHECK_LIB([ncurses], [delscreen], [
		LIBS="-lncurses $LIBS"
		AC_CHECK_HEADERS(ncurses.h,, [
		    AC_CHECK_HEADERS(ncurses/ncurses.h, [
			AC_DEFINE([NCURSES_IN_SUBDIR], [ncurses/], [Look for ncurses headers in subdir])
			], [AC_MSG_ERROR([ncurses headers not found])])
		])
	    ], [AC_MSG_ERROR([ncurses development library not found])]
	)]
)

AC_CHECK_LIB([intl], [libintl_dgettext]) # needed to statically link libparted, but not given in its pkgconfig file
AC_CHECK_LIB([uuid], [uuid_generate])    # needed to statically link libparted, but not given in its pkgconfig file
PKG_CHECK_MODULES([PARTED], [libparted])
AC_CHECK_LIB([pthread], [main], ,[AC_MSG_ERROR([pthread development library not found])])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/file.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([fdatasync memset regcomp strdup strerror])

AC_OUTPUT
