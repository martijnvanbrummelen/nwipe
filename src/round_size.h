#ifndef ROUND_SIZE_H_
#define ROUND_SIZE_H_

#include <stdint.h>

typedef enum {
    NWIPE_ROUND_VERIFY_NONE = 0,
    NWIPE_ROUND_VERIFY_LAST = 1,
    NWIPE_ROUND_VERIFY_ALL = 2,
} nwipe_round_verify_t;

typedef enum {
    NWIPE_ROUND_METHOD_DEFAULT = 0,
    NWIPE_ROUND_METHOD_OPS2 = 1,
    NWIPE_ROUND_METHOD_IS5ENH = 2,
} nwipe_round_method_class_t;

uint64_t nwipe_calculate_round_size_bytes( uint64_t base_pass_size,
                                           uint64_t device_size,
                                           int rounds,
                                           int noblank,
                                           nwipe_round_verify_t verify,
                                           nwipe_round_method_class_t method_class,
                                           uint64_t* effective_pass_size_out );

#endif /* ROUND_SIZE_H_ */
