SUBDIRS = external/openssl src man

# Set of files to be formatted.
FORMATSOURCES = src/*.c src/*.h
format:
	clang-format -i -style=file $(FORMATSOURCES)

check-format:
	clang-format -i -style=file $(FORMATSOURCES) && git diff --exit-code

# Add a rule to build OpenSSL first
all: build-openssl $(SUBDIRS)

build-openssl:
	cd external/openssl && ./config no-shared --prefix=$(abs_top_srcdir)/external/openssl/build && make && make install

