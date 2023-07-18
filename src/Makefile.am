# what flags you want to pass to the C compiler & linker
#CFLAGS = -lncurses -lparted
AM_CFLAGS =
AM_LDFLAGS =

# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
bin_PROGRAMS = nwipe
nwipe_SOURCES = context.h logging.h options.h prng.h version.h temperature.h nwipe.c gui.c method.h pass.c device.c gui.h isaac_rand/isaac_standard.h isaac_rand/isaac_rand.h isaac_rand/isaac_rand.c isaac_rand/isaac64.h isaac_rand/isaac64.c mt19937ar-cok/mt19937ar-cok.c nwipe.h mt19937ar-cok/mt19937ar-cok.h pass.h device.h logging.c method.c options.c prng.c version.c temperature.c PDFGen/pdfgen.h PDFGen/pdfgen.c create_pdf.c create_pdf.h embedded_images/shred_db.jpg.c embedded_images/shred_db.jpg.h  embedded_images/tick_erased.jpg.c embedded_images/tick_erased.jpg.h embedded_images/redcross.c embedded_images/redcross.h hpa_dco.h hpa_dco.c miscellaneous.h miscellaneous.c embedded_images/nwipe_exclamation.jpg.h embedded_images/nwipe_exclamation.jpg.c conf.h conf.c customers.h customers.c
nwipe_LDADD = $(PARTED_LIBS) $(LIBCONFIG)
