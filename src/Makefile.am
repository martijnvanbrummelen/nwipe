# what flags you want to pass to the C compiler & linker
#CFLAGS = -lncurses -lparted
AM_CFLAGS =
AM_LDFLAGS =

# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
bin_PROGRAMS = nwipe
nwipe_SOURCES = context.h logging.h options.h prng.h version.h temperature.h nwipe.c gui.c method.h pass.c device.c gui.h isaac_rand/isaac_standard.h isaac_rand/isaac_rand.h isaac_rand/isaac_rand.c isaac_rand/isaac64.h isaac_rand/isaac64.c mt19937ar-cok/mt19937ar-cok.c nwipe.h mt19937ar-cok/mt19937ar-cok.h pass.h device.h logging.c method.c options.c prng.c version.c temperature.c
nwipe_LDADD = $(PARTED_LIBS)
