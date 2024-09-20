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
	cd external/openssl && ./Configure linux-x86_64 no-shared no-comp no-engine no-md4 no-md2 no-ripemd no-whirlpool \
  no-dsa no-dh no-ec no-ecdsa no-ecdh no-sm2 no-sm3 no-sm4 no-aria \
  no-cast no-camellia no-seed no-rc2 no-rc4 no-rc5 no-des no-blake2 no-poly1305 \
  no-chacha no-scrypt no-siphash no-ssl no-tls no-tls1 no-tls1_1 no-tls1_2 no-tls1_3 \
  no-dtls no-dtls1 no-dtls1_2 no-nextprotoneg no-ocsp no-srp no-srtp no-psk \
  no-deprecated no-legacy no-tests no-shared no-dso no-async no-atexit no-autoalginit \
  no-autoload-config no-static-engine no-ui-console no-filenames \
  --prefix=$(abs_top_srcdir)/external/openssl/build && make -j$(shell nproc) && make install_sw



