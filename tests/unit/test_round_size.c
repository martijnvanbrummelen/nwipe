#include <assert.h>
#include <stdint.h>

#include "round_size.h"

static void test_default_verify_last_blanking_on( void )
{
    uint64_t pass_size = 0;
    uint64_t round_size = nwipe_calculate_round_size_bytes( 100,
                                                            100,
                                                            1,
                                                            0,
                                                            NWIPE_ROUND_VERIFY_LAST,
                                                            NWIPE_ROUND_METHOD_DEFAULT,
                                                            &pass_size );

    assert( pass_size == 100 );
    assert( round_size == 300 );
}

static void test_default_verify_all_two_rounds( void )
{
    uint64_t pass_size = 0;
    uint64_t round_size = nwipe_calculate_round_size_bytes( 100,
                                                            100,
                                                            2,
                                                            0,
                                                            NWIPE_ROUND_VERIFY_ALL,
                                                            NWIPE_ROUND_METHOD_DEFAULT,
                                                            &pass_size );

    assert( pass_size == 200 );
    assert( round_size == 600 );
}

static void test_ops2_verify_last_no_blank( void )
{
    uint64_t pass_size = 0;
    uint64_t round_size = nwipe_calculate_round_size_bytes( 100,
                                                            100,
                                                            1,
                                                            1,
                                                            NWIPE_ROUND_VERIFY_LAST,
                                                            NWIPE_ROUND_METHOD_OPS2,
                                                            &pass_size );

    assert( pass_size == 100 );
    assert( round_size == 300 );
}

static void test_is5enh_verify_last_no_blank_two_rounds( void )
{
    uint64_t pass_size = 0;
    uint64_t round_size = nwipe_calculate_round_size_bytes( 100,
                                                            100,
                                                            2,
                                                            1,
                                                            NWIPE_ROUND_VERIFY_LAST,
                                                            NWIPE_ROUND_METHOD_IS5ENH,
                                                            &pass_size );

    assert( pass_size == 100 );
    assert( round_size == 400 );
}

static void test_is5enh_verify_all_two_rounds( void )
{
    uint64_t pass_size = 0;
    uint64_t round_size = nwipe_calculate_round_size_bytes( 100,
                                                            100,
                                                            2,
                                                            0,
                                                            NWIPE_ROUND_VERIFY_ALL,
                                                            NWIPE_ROUND_METHOD_IS5ENH,
                                                            &pass_size );

    assert( pass_size == 200 );
    assert( round_size == 600 );
}

int main( void )
{
    test_default_verify_last_blanking_on();
    test_default_verify_all_two_rounds();
    test_ops2_verify_last_no_blank();
    test_is5enh_verify_last_no_blank_two_rounds();
    test_is5enh_verify_all_two_rounds();
    return 0;
}
