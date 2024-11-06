# Makefile for PRNG Benchmark

# Compiler
CC = gcc

# Compiler Flags
CFLAGS = -O2

# Libraries to Link Against
LIBS = -lssl -lcrypto -lm

# Source Files
SOURCES = prng_benchmark.c \
          mt19937ar-cok/mt19937ar-cok.c \
          isaac_rand/isaac_rand.c \
          isaac_rand/isaac64.c \
          alfg/add_lagg_fibonacci_prng.c \
          xor/xoroshiro256_prng.c \
          aes/aes_ctr_prng.c

# Object Files
OBJECTS = $(SOURCES:.c=.o)

# Executable Name
TARGET = prng_benchmark

# Default Target
all: $(TARGET)

# Link Object Files to Create Executable
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LIBS)

# Compile .c Files to .o Object Files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean Build Artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Optional: Run the Benchmark
run: $(TARGET)
	./$(TARGET)

# Phony Targets
.PHONY: all clean run
