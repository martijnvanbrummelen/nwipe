# what flags you want to pass to the C compiler & linker
#CFLAGS = -lncurses -lparted
AM_CFLAGS =
AM_LDFLAGS =

# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
bin_PROGRAMS = nwipe
nwipe_SOURCES = context.h isaac_rand.c logging.h options.h prng.h nwipe.c gui.c isaac_rand.h method.h pass.c device.c gui.h isaac_standard.h mt19937ar-cok.c nwipe.h mt19937ar-cok.h pass.h device.h logging.c method.c options.c prng.c version.c version.h
nwipe_CFLAGS = $(PARTED_CFLAGS)
nwipe_LDADD = $(PARTED_LIBS)
