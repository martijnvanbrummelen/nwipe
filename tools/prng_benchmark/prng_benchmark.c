// prng_benchmark.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>  // For va_start and va_end
#include <stdbool.h> // For bool type
#include <errno.h>   // For error handling

// Simple logging functions as a substitute for nwipe_log
#define NWIPE_LOG_NOTICE 1
#define NWIPE_LOG_FATAL 2
#define NWIPE_LOG_ERROR 3

/**
 * @brief Logs messages with different severity levels.
 *
 * @param level The severity level of the log (NOTICE, FATAL, ERROR).
 * @param format The format string (similar to printf).
 * @param ... Additional arguments for the format string.
 */
void nwipe_log(int level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (level == NWIPE_LOG_NOTICE) {
        printf("NOTICE: ");
    } else if (level == NWIPE_LOG_FATAL) {
        printf("FATAL: ");
    } else if (level == NWIPE_LOG_ERROR) {
        printf("ERROR: ");
    }
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

/**
 * @brief Prints an error message based on the error number.
 *
 * @param errnum The error number (errno).
 * @param function The name of the function where the error occurred.
 * @param message A descriptive message about the error.
 */
void nwipe_perror(int errnum, const char* function, const char* message) {
    fprintf(stderr, "%s: %s: %s\n", function, message, strerror(errnum));
}

// Definition of the u8 type and other necessary types
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

// Structure for entropy seed
typedef struct {
    size_t length;  // Length of the entropy string in bytes
    u8* s;          // The actual bytes of the entropy string
} nwipe_entropy_t;

// Function pointer types for PRNG initialization and reading
typedef int (*nwipe_prng_init_t)(void **state, nwipe_entropy_t *seed);
typedef int (*nwipe_prng_read_t)(void **state, void *buffer, size_t count);

// Structure to define a generic PRNG
typedef struct {
    const char* label;          // Name of the pseudo-random number generator
    nwipe_prng_init_t init;     // Function to initialize the PRNG state
    nwipe_prng_read_t read;     // Function to read data from the PRNG
} nwipe_prng_t;

// Forward declarations of PRNG functions
int nwipe_twister_init(void **state, nwipe_entropy_t *seed);
int nwipe_twister_read(void **state, void *buffer, size_t count);

int nwipe_isaac_init(void **state, nwipe_entropy_t *seed);
int nwipe_isaac_read(void **state, void *buffer, size_t count);

int nwipe_isaac64_init(void **state, nwipe_entropy_t *seed);
int nwipe_isaac64_read(void **state, void *buffer, size_t count);

int nwipe_add_lagg_fibonacci_prng_init(void **state, nwipe_entropy_t *seed);
int nwipe_add_lagg_fibonacci_prng_read(void **state, void *buffer, size_t count);

int nwipe_xoroshiro256_prng_init(void **state, nwipe_entropy_t *seed);
int nwipe_xoroshiro256_prng_read(void **state, void *buffer, size_t count);

int nwipe_aes_ctr_prng_init(void **state, nwipe_entropy_t *seed);
int nwipe_aes_ctr_prng_read(void **state, void *buffer, size_t count);

// Include PRNG-specific header files
#include "mt19937ar-cok/mt19937ar-cok.h"
#include "isaac_rand/isaac_rand.h"
#include "isaac_rand/isaac64.h"
#include "alfg/add_lagg_fibonacci_prng.h"
#include "xor/xoroshiro256_prng.h"
#include "aes/aes_ctr_prng.h"

// Definition of PRNG instances
nwipe_prng_t prngs[] = {
    { "Mersenne Twister (mt19937ar-cok)", nwipe_twister_init, nwipe_twister_read },
    { "ISAAC (rand.c 20010626)", nwipe_isaac_init, nwipe_isaac_read },
    { "ISAAC-64 (isaac64.c)", nwipe_isaac64_init, nwipe_isaac64_read },
    { "Lagged Fibonacci Generator", nwipe_add_lagg_fibonacci_prng_init, nwipe_add_lagg_fibonacci_prng_read },
    { "XORoshiro-256", nwipe_xoroshiro256_prng_init, nwipe_xoroshiro256_prng_read },
    { "AES-256-CTR (OpenSSL)", nwipe_aes_ctr_prng_init, nwipe_aes_ctr_prng_read }
};

// Definition of PRNG block sizes
#define SIZE_OF_TWISTER 4
#define SIZE_OF_ISAAC 4
#define SIZE_OF_ISAAC64 8
#define SIZE_OF_ADD_LAGG_FIBONACCI_PRNG 32
#define SIZE_OF_XOROSHIRO256_PRNG 32
#define SIZE_OF_AES_CTR_PRNG 32

// Number of bytes to generate for the benchmark (default: 100 MB)
#define DEFAULT_BENCHMARK_SIZE_MB 100

// Seed data (can be adjusted or randomly generated)
#define SEED_LENGTH 32
unsigned char seed_data[SEED_LENGTH] = {
    0x00, 0x01, 0x02, 0x03, 
    0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B,
    0x1C, 0x1D, 0x1E, 0x1F
};

// Structure to store benchmark results
typedef struct {
    const char* label;
    double time_seconds;
    double throughput_mb_per_sec;  // Throughput in MB/s
} benchmark_result_t;

// Function to calculate the elapsed time in seconds
/**
 * @brief Calculates the difference between two timespec structures.
 *
 * @param start The start time.
 * @param end The end time.
 * @return The elapsed time in seconds as a double.
 */
double time_diff(struct timespec start, struct timespec end) {
    double start_sec = start.tv_sec + start.tv_nsec / 1e9;
    double end_sec = end.tv_sec + end.tv_nsec / 1e9;
    return end_sec - start_sec;
}

// Helper functions from prng.c

/**
 * @brief Converts a 32-bit unsigned integer to a byte buffer.
 *
 * @param buffer The destination buffer.
 * @param val The 32-bit unsigned integer value.
 * @param len The number of bytes to write.
 */
static inline void u32_to_buffer(u8* buffer, u32 val, const int len) {
    for(int i = 0; i < len; ++i) {
        buffer[i] = (u8)(val & 0xFFUL);
        val >>= 8;
    }
}

/**
 * @brief Converts a 64-bit unsigned integer to a byte buffer.
 *
 * @param buffer The destination buffer.
 * @param val The 64-bit unsigned integer value.
 * @param len The number of bytes to write.
 */
static inline void u64_to_buffer(u8* buffer, u64 val, const int len) {
    for(int i = 0; i < len; ++i) {
        buffer[i] = (u8)(val & 0xFFULL);
        val >>= 8;
    }
}

/**
 * @brief Retrieves the next value from the ISAAC PRNG.
 *
 * @param ctx The ISAAC context.
 * @return The next 32-bit unsigned integer from the PRNG.
 */
static inline u32 isaac_nextval(randctx* ctx) {
    if(ctx->randcnt == 0) {
        isaac(ctx);
        ctx->randcnt = RANDSIZ;
    }
    ctx->randcnt--;
    return ctx->randrsl[ctx->randcnt];
}

/**
 * @brief Retrieves the next value from the ISAAC-64 PRNG.
 *
 * @param ctx The ISAAC-64 context.
 * @return The next 64-bit unsigned integer from the PRNG.
 */
static inline u64 isaac64_nextval(rand64ctx* ctx) {
    if(ctx->randcnt == 0) {
        isaac64(ctx);
        ctx->randcnt = RANDSIZ;
    }
    ctx->randcnt--;
    return ctx->randrsl[ctx->randcnt];
}

// Implementation of PRNG init and read functions

// Mersenne Twister
/**
 * @brief Initializes the Mersenne Twister PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param seed The entropy seed.
 * @return 0 on success, -1 on failure.
 */
int nwipe_twister_init(void **state, nwipe_entropy_t *seed) {
    printf("Initializing Mersenne Twister PRNG\n");

    if(*state == NULL) {
        *state = malloc(sizeof(twister_state_t));
        if(*state == NULL) {
            fprintf(stderr, "Failed to allocate memory for Mersenne Twister state.\n");
            return -1;
        }
    }
    twister_init((twister_state_t*) *state, (unsigned long*) (seed->s), seed->length / sizeof(unsigned long));
    return 0;
}

/**
 * @brief Reads random data from the Mersenne Twister PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param buffer The buffer to store random data.
 * @param count The number of bytes to generate.
 * @return 0 on success.
 */
int nwipe_twister_read(void **state, void *buffer, size_t count) {
    u8* bufpos = buffer;
    size_t words = count / SIZE_OF_TWISTER;

    for(size_t ii = 0; ii < words; ii++) {
        u32_to_buffer(bufpos, twister_genrand_int32((twister_state_t*) *state), SIZE_OF_TWISTER);
        bufpos += SIZE_OF_TWISTER;
    }

    size_t remain = count % SIZE_OF_TWISTER;
    if(remain > 0) {
        u32_to_buffer(bufpos, twister_genrand_int32((twister_state_t*) *state), remain);
    }

    return 0;
}

// ISAAC
/**
 * @brief Initializes the ISAAC PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param seed The entropy seed.
 * @return 0 on success, -1 on failure.
 */
int nwipe_isaac_init(void **state, nwipe_entropy_t *seed) {
    int count;
    randctx* isaac_state = *state;

    printf("Initializing ISAAC PRNG\n");

    if(*state == NULL) {
        *state = malloc(sizeof(randctx));
        isaac_state = *state;
        if(isaac_state == NULL) {
            fprintf(stderr, "Failed to allocate memory for ISAAC state.\n");
            return -1;
        }
    }

    if(sizeof(isaac_state->randrsl) < seed->length) {
        count = sizeof(isaac_state->randrsl);
    } else {
        memset(isaac_state->randrsl, 0, sizeof(isaac_state->randrsl));
        count = seed->length;
    }

    if(count == 0) {
        randinit(isaac_state, 0);
    } else {
        memcpy(isaac_state->randrsl, seed->s, count);
        randinit(isaac_state, 1);
    }

    return 0;
}

/**
 * @brief Reads random data from the ISAAC PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param buffer The buffer to store random data.
 * @param count The number of bytes to generate.
 * @return 0 on success.
 */
int nwipe_isaac_read(void **state, void *buffer, size_t count) {
    randctx* isaac_state = *state;
    u8* bufpos = buffer;
    size_t words = count / SIZE_OF_ISAAC;

    for(size_t ii = 0; ii < words; ii++) {
        u32_to_buffer(bufpos, isaac_nextval(isaac_state), SIZE_OF_ISAAC);
        bufpos += SIZE_OF_ISAAC;
    }

    size_t remain = count % SIZE_OF_ISAAC;
    if(remain > 0) {
        u32_to_buffer(bufpos, isaac_nextval(isaac_state), remain);
    }

    return 0;
}

// ISAAC-64
/**
 * @brief Initializes the ISAAC-64 PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param seed The entropy seed.
 * @return 0 on success, -1 on failure.
 */
int nwipe_isaac64_init(void **state, nwipe_entropy_t *seed) {
    int count;
    rand64ctx* isaac_state = *state;

    printf("Initializing ISAAC-64 PRNG\n");

    if(*state == NULL) {
        *state = malloc(sizeof(rand64ctx));
        isaac_state = *state;
        if(isaac_state == NULL) {
            fprintf(stderr, "Failed to allocate memory for ISAAC-64 state.\n");
            return -1;
        }
    }

    if(sizeof(isaac_state->randrsl) < seed->length) {
        count = sizeof(isaac_state->randrsl);
    } else {
        memset(isaac_state->randrsl, 0, sizeof(isaac_state->randrsl));
        count = seed->length;
    }

    if(count == 0) {
        rand64init(isaac_state, 0);
    } else {
        memcpy(isaac_state->randrsl, seed->s, count);
        rand64init(isaac_state, 1);
    }

    return 0;
}

/**
 * @brief Reads random data from the ISAAC-64 PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param buffer The buffer to store random data.
 * @param count The number of bytes to generate.
 * @return 0 on success.
 */
int nwipe_isaac64_read(void **state, void *buffer, size_t count) {
    rand64ctx* isaac_state = *state;
    u8* bufpos = buffer;
    size_t words = count / SIZE_OF_ISAAC64;

    for(size_t ii = 0; ii < words; ii++) {
        u64_to_buffer(bufpos, isaac64_nextval(isaac_state), SIZE_OF_ISAAC64);
        bufpos += SIZE_OF_ISAAC64;
    }

    size_t remain = count % SIZE_OF_ISAAC64;
    if(remain > 0) {
        u64_to_buffer(bufpos, isaac64_nextval(isaac_state), remain);
    }

    return 0;
}

// Lagged Fibonacci Generator
/**
 * @brief Initializes the Lagged Fibonacci Generator PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param seed The entropy seed.
 * @return 0 on success, -1 on failure.
 */
int nwipe_add_lagg_fibonacci_prng_init(void **state, nwipe_entropy_t *seed) {
    if(*state == NULL) {
        printf("Initializing Lagged Fibonacci Generator PRNG\n");
        *state = malloc(sizeof(add_lagg_fibonacci_state_t));
        if(*state == NULL) {
            fprintf(stderr, "Failed to allocate memory for Lagged Fibonacci state.\n");
            return -1;
        }
    }
    add_lagg_fibonacci_init((add_lagg_fibonacci_state_t*) *state, (uint64_t*) (seed->s), seed->length / sizeof(uint64_t));
    return 0;
}

/**
 * @brief Reads random data from the Lagged Fibonacci Generator PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param buffer The buffer to store random data.
 * @param count The number of bytes to generate.
 * @return 0 on success.
 */
int nwipe_add_lagg_fibonacci_prng_read(void **state, void *buffer, size_t count) {
    u8* bufpos = buffer;
    size_t words = count / SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;

    for(size_t ii = 0; ii < words; ii++) {
        add_lagg_fibonacci_genrand_uint256_to_buf((add_lagg_fibonacci_state_t*) *state, bufpos);
        bufpos += SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;
    }

    size_t remain = count % SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;
    if(remain > 0) {
        unsigned char temp_output[SIZE_OF_ADD_LAGG_FIBONACCI_PRNG];
        add_lagg_fibonacci_genrand_uint256_to_buf((add_lagg_fibonacci_state_t*) *state, temp_output);
        memcpy(bufpos, temp_output, remain);
    }

    return 0;
}

// XORoshiro-256
/**
 * @brief Initializes the XORoshiro-256 PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param seed The entropy seed.
 * @return 0 on success, -1 on failure.
 */
int nwipe_xoroshiro256_prng_init(void **state, nwipe_entropy_t *seed) {
    printf("Initializing XORoshiro-256 PRNG\n");

    if(*state == NULL) {
        *state = malloc(sizeof(xoroshiro256_state_t));
        if(*state == NULL) {
            fprintf(stderr, "Failed to allocate memory for XORoshiro-256 state.\n");
            return -1;
        }
    }
    xoroshiro256_init((xoroshiro256_state_t*) *state, (unsigned long*) (seed->s), seed->length / sizeof(unsigned long));
    return 0;
}

/**
 * @brief Reads random data from the XORoshiro-256 PRNG.
 *
 * @param state Pointer to the PRNG state.
 * @param buffer The buffer to store random data.
 * @param count The number of bytes to generate.
 * @return 0 on success.
 */
int nwipe_xoroshiro256_prng_read(void **state, void *buffer, size_t count) {
    u8* bufpos = buffer;
    size_t words = count / SIZE_OF_XOROSHIRO256_PRNG;

    for(size_t ii = 0; ii < words; ii++) {
        xoroshiro256_genrand_uint256_to_buf((xoroshiro256_state_t*) *state, bufpos);
        bufpos += SIZE_OF_XOROSHIRO256_PRNG;
    }

    size_t remain = count % SIZE_OF_XOROSHIRO256_PRNG;
    if(remain > 0) {
        unsigned char temp_output[SIZE_OF_XOROSHIRO256_PRNG];
        xoroshiro256_genrand_uint256_to_buf((xoroshiro256_state_t*) *state, temp_output);
        memcpy(bufpos, temp_output, remain);
    }

    return 0;
}

// AES-256-CTR (OpenSSL)
/**
 * @brief Initializes the AES-256-CTR PRNG using OpenSSL.
 *
 * @param state Pointer to the PRNG state.
 * @param seed The entropy seed.
 * @return 0 on success, -1 on failure.
 */
int nwipe_aes_ctr_prng_init(void **state, nwipe_entropy_t *seed) {
    printf("Initializing AES-256-CTR PRNG\n");

    if(*state == NULL) {
        *state = calloc(1, sizeof(aes_ctr_state_t));
        if(*state == NULL) {
            fprintf(stderr, "Failed to allocate memory for AES CTR PRNG state.\n");
            return -1;
        }
    }

    if(aes_ctr_prng_init((aes_ctr_state_t*) *state, (unsigned long*) (seed->s), seed->length / sizeof(unsigned long)) != 0) {
        fprintf(stderr, "Error occurred during PRNG initialization in OpenSSL.\n");
        aes_ctr_prng_general_cleanup((aes_ctr_state_t*) *state);
        free(*state);
        *state = NULL;
        return -1;
    }

    // Optional: Insert validation if necessary
    // ...

    return 0;
}

/**
 * @brief Reads random data from the AES-256-CTR PRNG using OpenSSL.
 *
 * @param state Pointer to the PRNG state.
 * @param buffer The buffer to store random data.
 * @param count The number of bytes to generate.
 * @return 0 on success, -1 on failure.
 */
int nwipe_aes_ctr_prng_read(void **state, void *buffer, size_t count) {
    u8* bufpos = buffer;
    size_t words = count / SIZE_OF_AES_CTR_PRNG;

    for(size_t ii = 0; ii < words; ii++) {
        if(aes_ctr_prng_genrand_uint256_to_buf((aes_ctr_state_t*) *state, bufpos) != 0) {
            fprintf(stderr, "Error occurred during RNG generation in OpenSSL.\n");
            return -1;
        }
        bufpos += SIZE_OF_AES_CTR_PRNG;
    }

    size_t remain = count % SIZE_OF_AES_CTR_PRNG;
    if(remain > 0) {
        unsigned char temp_output[SIZE_OF_AES_CTR_PRNG];
        memset(temp_output, 0, sizeof(temp_output));
        if(aes_ctr_prng_genrand_uint256_to_buf((aes_ctr_state_t*) *state, temp_output) != 0) {
            fprintf(stderr, "Error occurred during RNG generation in OpenSSL.\n");
            return -1;
        }
        memcpy(bufpos, temp_output, remain);
    }

    return 0;
}

int main(int argc, char* argv[]) {
    const int num_prngs = sizeof(prngs) / sizeof(prngs[0]);
    benchmark_result_t results[num_prngs];
    bool memonly = false;      // Flag for --memonly parameter
    size_t benchmark_size_mb = DEFAULT_BENCHMARK_SIZE_MB; // Default benchmark size in MB

    // Parse command-line arguments to check for --memonly and --size
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--memonly") == 0) {
            memonly = true;
        }
        else if(strcmp(argv[i], "--size") == 0) {
            if(i + 1 < argc) {
                errno = 0;
                long size_input = strtol(argv[i + 1], NULL, 10);
                if(errno != 0 || size_input <= 0) {
                    fprintf(stderr, "Error: Invalid size value '%s'. Please provide a positive integer.\n", argv[i + 1]);
                    return EXIT_FAILURE;
                }
                benchmark_size_mb = (size_t)size_input;
                i++; // Skip the next argument as it's the size value
            }
            else {
                fprintf(stderr, "Error: --size option requires an argument.\n");
                return EXIT_FAILURE;
            }
        }
        else {
            fprintf(stderr, "Warning: Unknown argument '%s' ignored.\n", argv[i]);
        }
    }

    // Calculate benchmark size in bytes
    size_t benchmark_size = benchmark_size_mb * 1024 * 1024;

    if(memonly) {
        printf("Benchmarking in memory-only mode. Random data will not be written to disk.\n\n");
    }

    // Create a common seed
    nwipe_entropy_t seed;
    seed.length = SEED_LENGTH;
    seed.s = seed_data;

    // Iterate over all PRNGs
    for(int i = 0; i < num_prngs; i++) {
        nwipe_prng_t current_prng = prngs[i];
        void* state = NULL;
        void* buffer = malloc(benchmark_size);
        if(!buffer) {
            fprintf(stderr, "Error: Unable to allocate memory for the buffer.\n");
            return EXIT_FAILURE;
        }

        // Initialize the PRNG
        printf("Initializing PRNG: %s\n", current_prng.label);
        if(current_prng.init(&state, &seed) != 0) {
            fprintf(stderr, "Error: PRNG %s could not be initialized.\n", current_prng.label);
            free(buffer);
            continue;
        }

        // Start timing
        struct timespec start_time, end_time;
        if(clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
            perror("clock_gettime");
            fprintf(stderr, "Error: Failed to get start time.\n");
            free(buffer);
            continue;
        }

        // Generate random data
        if(current_prng.read(&state, buffer, benchmark_size) != 0) {
            fprintf(stderr, "Error: PRNG %s could not generate random data.\n", current_prng.label);
            free(buffer);
            continue;
        }

        // Stop timing
        if(clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
            perror("clock_gettime");
            fprintf(stderr, "Error: Failed to get end time.\n");
            free(buffer);
            continue;
        }

        // Calculate elapsed time
        double elapsed = time_diff(start_time, end_time);
        // Calculate throughput in MB/s
        double throughput = (double)benchmark_size / (1024.0 * 1024.0) / elapsed;

        results[i].label = current_prng.label;
        results[i].time_seconds = elapsed;
        results[i].throughput_mb_per_sec = throughput;  // Store throughput

        // Conditionally write the random data to a binary file
        if(!memonly) {
            char filename[256];
            snprintf(filename, sizeof(filename), "%s.bin", current_prng.label);
            // Replace spaces, parentheses, and slashes in the filename with underscores
            for(char* p = filename; *p; p++) {
                if(*p == ' ' || *p == '(' || *p == ')' || *p == '/') {
                    *p = '_';
                }
            }

            FILE* fp = fopen(filename, "wb");
            if(fp) {
                size_t written = fwrite(buffer, 1, benchmark_size, fp);
                if(written != benchmark_size) {
                    fprintf(stderr, "Warning: Could not write all data to file %s.\n", filename);
                }
                fclose(fp);
                printf("Random data written to %s.\n", filename);
            } else {
                fprintf(stderr, "Warning: Could not open file %s.\n", filename);
            }
        }

        // Clean up
        free(buffer);
        // Optional: Call cleanup function for the PRNG if available
        // Example:
        // if(current_prng.cleanup) {
        //     current_prng.cleanup(state);
        // }
    }

    // Determine the fastest PRNG
    double min_time = -1;
    int fastest_index = -1;
    for(int i = 0; i < num_prngs; i++) {
        if(results[i].time_seconds > 0) { // Ensure the result is valid
            if(min_time < 0 || results[i].time_seconds < min_time) {
                min_time = results[i].time_seconds;
                fastest_index = i;
            }
        }
    }

    // Output the results
    printf("\nBenchmark Results for PRNGs (generated %zu MB):\n", benchmark_size_mb);
    printf("%-40s : %-10s : %-10s\n", "PRNG", "Seconds", "MB/s");
    printf("---------------------------------------------------------------------\n");
    for(int i = 0; i < num_prngs; i++) {
        if(results[i].time_seconds > 0) {
            printf("%-40s : %-10.6f : %-10.2f\n", results[i].label, results[i].time_seconds, results[i].throughput_mb_per_sec);
        } else {
            printf("%-40s : %-10s : %-10s\n", prngs[i].label, "Error", "Error");
        }
    }

    if(fastest_index >= 0) {
        printf("\nThe fastest PRNG is: %s with %.6f seconds (%.2f MB/s).\n", 
               results[fastest_index].label, 
               results[fastest_index].time_seconds, 
               results[fastest_index].throughput_mb_per_sec);
    } else {
        printf("\nNo PRNG could be successfully benchmarked.\n");
    }

    return EXIT_SUCCESS;
}
