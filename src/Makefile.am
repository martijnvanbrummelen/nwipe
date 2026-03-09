# what flags you want to pass to the C compiler & linker
#CFLAGS = -lncurses -lparted
AM_CFLAGS =
AM_LDFLAGS =
check_PROGRAMS = test_round_size
TESTS = test_round_size

# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
bin_PROGRAMS = nwipe

# Core sources
NWIPE_CORE = \
    context.h \
    logging.c logging.h \
    options.c options.h \
    prng.c prng.h \
    version.c version.h \
    temperature.c temperature.h \
    nwipe.c nwipe.h \
    gui.c gui.h \
    method.c method.h \
    pass.c pass.h \
    pass_internal.c pass_internal.h \
    pass_static.c pass_random.c \
    device.c device.h \
    hpa_dco.c hpa_dco.h \
    miscellaneous.c miscellaneous.h \
    conf.c conf.h \
    customers.c customers.h \
    cpu_features.c cpu_features.h \
    round_size.c round_size.h \
    ANSI-color-codes.h \
    display_help.c

# PRNG implementations
NWIPE_PRNG = \
    isaac_rand/isaac_standard.h \
    isaac_rand/isaac_rand.c isaac_rand/isaac_rand.h \
    isaac_rand/isaac64.c isaac_rand/isaac64.h \
    mt19937ar-cok/mt19937ar-cok.c mt19937ar-cok/mt19937ar-cok.h \
    alfg/add_lagg_fibonacci_prng.c alfg/add_lagg_fibonacci_prng.h \
    xor/xoroshiro256_prng.c xor/xoroshiro256_prng.h \
    splitmix64/splitmix64.c splitmix64/splitmix64.h \
    aes/aes_ctr_prng.cpp aes/aes_ctr_prng.h \
    chacha20/chacha20.c chacha20/chacha20.h \
    chacha20/chacha20_test.c chacha20/chacha20_test_vectors.c chacha20/chacha20_test.h

# PDF generation and reporting
NWIPE_PDF = \
    PDFGen/pdfgen.c PDFGen/pdfgen.h \
    create_pdf.c create_pdf.h \
    create_single_disc_pdf.c \
    create_system_multi_disc_pdf.c \
    embedded_images/shred_db.jpg.c embedded_images/shred_db.jpg.h \
    embedded_images/tick_erased.jpg.c embedded_images/tick_erased.jpg.h \
    embedded_images/redcross.c embedded_images/redcross.h \
    embedded_images/nwipe_exclamation.jpg.c embedded_images/nwipe_exclamation.jpg.h

# HDD temperature (SCSI)
NWIPE_HDDTEMP = \
    hddtemp_scsi/hddtemp.h hddtemp_scsi/scsi.h hddtemp_scsi/scsicmds.h \
    hddtemp_scsi/get_scsi_temp.c hddtemp_scsi/scsi.c hddtemp_scsi/scsicmds.c

# Combine all sources
nwipe_SOURCES = $(NWIPE_CORE) $(NWIPE_PRNG) $(NWIPE_PDF) $(NWIPE_HDDTEMP)
nwipe_LDADD = $(PARTED_LIBS) $(LIBCONFIG)

test_round_size_SOURCES = ../tests/unit/test_round_size.c round_size.c round_size.h
test_round_size_CPPFLAGS = -I$(top_srcdir)/src -I$(top_builddir)
