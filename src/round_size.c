#include "round_size.h"

uint64_t nwipe_calculate_round_size_bytes( uint64_t base_pass_size,
                                           uint64_t device_size,
                                           int rounds,
                                           int noblank,
                                           nwipe_round_verify_t verify,
                                           nwipe_round_method_class_t method_class,
                                           uint64_t* effective_pass_size_out )
{
    uint64_t pass_size = base_pass_size;
    uint64_t round_size;
    uint64_t rounds_u64 = ( rounds > 0 ) ? (uint64_t) rounds : 1ULL;

    if( verify == NWIPE_ROUND_VERIFY_ALL )
    {
        pass_size *= 2ULL;
    }

    if( effective_pass_size_out != 0 )
    {
        *effective_pass_size_out = pass_size;
    }

    round_size = pass_size * rounds_u64;

    if( noblank == 0 )
    {
        round_size += device_size;
        if( verify == NWIPE_ROUND_VERIFY_LAST || verify == NWIPE_ROUND_VERIFY_ALL )
        {
            round_size += device_size;
        }
    }
    else if( verify == NWIPE_ROUND_VERIFY_LAST )
    {
        round_size += device_size;
    }

    if( method_class == NWIPE_ROUND_METHOD_OPS2 )
    {
        round_size += device_size;

        if( verify == NWIPE_ROUND_VERIFY_LAST || verify == NWIPE_ROUND_VERIFY_ALL )
        {
            round_size += device_size;
        }

        if( noblank == 0 )
        {
            round_size -= device_size;

            if( verify == NWIPE_ROUND_VERIFY_LAST || verify == NWIPE_ROUND_VERIFY_ALL )
            {
                round_size -= device_size;
            }
        }
        else if( verify == NWIPE_ROUND_VERIFY_LAST )
        {
            round_size -= device_size;
        }
    }
    else if( method_class == NWIPE_ROUND_METHOD_IS5ENH )
    {
        if( verify == NWIPE_ROUND_VERIFY_LAST && noblank == 1 )
        {
            round_size -= device_size;
        }

        if( verify != NWIPE_ROUND_VERIFY_ALL )
        {
            round_size += ( device_size * rounds_u64 );
        }
    }

    return round_size;
}
