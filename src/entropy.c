// entropy.c

#include "entropy.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>

#define N 64  // Number of bits in a uint64_t

typedef enum {
    NWIPE_LOG_NONE = 0,
    NWIPE_LOG_DEBUG,  // Debugging messages, detailed for troubleshooting
    NWIPE_LOG_INFO,  // Informative logs, for regular operation insights
    NWIPE_LOG_NOTICE,  // Notices for significant but non-critical events
    NWIPE_LOG_WARNING,  // Warnings about potential errors
    NWIPE_LOG_ERROR,  // Error messages, significant issues that affect operation
    NWIPE_LOG_FATAL,  // Fatal errors, require immediate termination of the program
    NWIPE_LOG_SANITY,  // Sanity checks, used primarily in debugging phases
    NWIPE_LOG_NOTIMESTAMP  // Log entries without timestamp information
} nwipe_log_t;

extern void nwipe_log( nwipe_log_t level, const char* format, ... );

/*
 * Function to calculate Shannon entropy based on bit distribution
 * It counts the number of 0s and 1s, calculates their probability,
 * and then uses the entropy formula to determine randomness.
 */
double shannon_entropy( uint64_t num )
{
    int bit_count[2] = { 0, 0 };

    // Count the number of 0s and 1s
    for( int i = 0; i < N; i++ )
    {
        bit_count[( num >> i ) & 1]++;
    }

    // Probability of 0s and 1s
    double p0 = (double) bit_count[0] / N;
    double p1 = (double) bit_count[1] / N;

    // Ensure there is no division by zero
    if( p0 == 0.0 || p1 == 0.0 )
    {
        return 0.0;  // Minimum entropy if all bits are the same
    }

    // Shannon entropy calculation
    return -p0 * log2( p0 ) - p1 * log2( p1 );
}

/*
 * Function to perform a bit frequency test
 * This checks the proportion of 1s in the number.
 * For high entropy, this proportion should be close to 0.5,
 * indicating an even distribution of 0s and 1s.
 */
double bit_frequency_test( uint64_t num )
{
    int count_ones = 0;

    // Count the number of 1s
    for( int i = 0; i < N; i++ )
    {
        if( ( num >> i ) & 1 )
        {
            count_ones++;
        }
    }

    // Calculate the proportion of 1s
    return (double) count_ones / N;
}

/*
 * Function to perform a runs test
 * This checks for sequences of identical bits.
 * A random sequence should have a moderate number of runs,
 * which indicates that the bits alternate regularly.
 */
int runs_test( uint64_t num )
{
    int runs = 1;  // Initialize number of runs
    int prev_bit = num & 1;  // Check the least significant bit

    // Count the number of runs
    for( int i = 1; i < N; i++ )
    {
        int current_bit = ( num >> i ) & 1;
        if( current_bit != prev_bit )
        {
            runs++;
            prev_bit = current_bit;
        }
    }

    // Returns the number of runs found
    return runs;
}

/*
 * Function to perform an auto-correlation test
 * This checks how often consecutive bits are the same.
 * A low correlation implies higher randomness.
 */
double auto_correlation_test( uint64_t num )
{
    int matches = 0;

    // Compare each bit with the next one in the sequence
    for( int i = 0; i < N - 1; i++ )
    {
        if( ( ( num >> i ) & 1 ) == ( ( num >> ( i + 1 ) ) & 1 ) )
        {
            matches++;
        }
    }

    // Returns the proportion of matches
    return (double) matches / ( N - 1 );
}

/*
 * Main function to check the entropy using various methods
 * This function aggregates the results of multiple entropy-checking methods:
 * - Shannon Entropy
 * - Bit Frequency Test
 * - Runs Test
 * - Auto-Correlation Test
 * The function returns 1 if the entropy is sufficient, otherwise it returns 0.
 */
int nwipe_check_entropy( uint64_t num )
{
    // Calculate Shannon entropy
    double entropy = shannon_entropy( num );
    if( entropy == 0.0 )
    {
        nwipe_log( NWIPE_LOG_FATAL, "Entropy calculation failed. All bits are identical." );
        return 0;  // Insufficient entropy
    }

    // Perform bit frequency test
    double frequency = bit_frequency_test( num );

    // Perform runs test (number of runs should ideally be in the middle of a range)
    int runs = runs_test( num );

    // Perform auto-correlation test (low correlation implies high randomness)
    double correlation = auto_correlation_test( num );

    // Log the results for debugging
    nwipe_log( NWIPE_LOG_DEBUG, "Shannon Entropy: %f", entropy );
    nwipe_log( NWIPE_LOG_DEBUG, "Bit Frequency (proportion of 1s): %f", frequency );
    nwipe_log( NWIPE_LOG_DEBUG, "Number of Runs: %d", runs );
    nwipe_log( NWIPE_LOG_DEBUG, "Auto-correlation: %f", correlation );

    /*
     * Entropy evaluation criteria
     * Criteria are chosen based on expected randomness for simplicity:
     * - High Shannon entropy (>0.9) indicates high unpredictability
     * - Bit frequency around 0.5 indicates a balanced number of 0s and 1s
     * - Runs count close to N/2 indicates randomness
     * - Low auto-correlation (<0.5) suggests randomness
     */
    if( entropy > 0.9 && frequency > 0.4 && frequency < 0.6 && runs > 20 && runs < 44 && correlation < 0.5 )
    {
        nwipe_log( NWIPE_LOG_INFO, "Entropy check passed. Sufficient randomness detected." );
        return 1;  // Sufficient entropy
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Entropy check failed. Insufficient randomness." );
        return 0;  // Insufficient entropy
    }
}
