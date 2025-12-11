#include "cpu_features.h"

/*
 * Executes the CPUID instruction and fills out the provided variables with the results.
 * eax: The function/subfunction number to query with CPUID.
 * *eax_out, *ebx_out, *ecx_out, *edx_out: Pointers to variables where the CPUID output will be stored.
 */
void cpuid( uint32_t eax, uint32_t* eax_out, uint32_t* ebx_out, uint32_t* ecx_out, uint32_t* edx_out )
{
#if defined( __i386__ ) || defined( __x86_64__ ) /* only on x86 */
#if defined( _MSC_VER ) /* MSVC */
    int r[4];
    __cpuid( r, eax );
    *eax_out = r[0];
    *ebx_out = r[1];
    *ecx_out = r[2];
    *edx_out = r[3];
#elif defined( __GNUC__ ) /* GCC/Clang */
    __asm__ __volatile__( "cpuid"
                          : "=a"( *eax_out ), "=b"( *ebx_out ), "=c"( *ecx_out ), "=d"( *edx_out )
                          : "a"( eax ) );
#else
#error "Unsupported compiler"
#endif
#else /* not-x86  */
    (void) eax;
    *eax_out = *ebx_out = *ecx_out = *edx_out = 0; /* CPUID = 0  */
#endif
}

/*
 * Checks if the AES-NI instruction set is supported by the processor.
 * Returns 1 (true) if supported, 0 (false) otherwise.
 */
int has_aes_ni( void )
{
#if defined( __i386__ ) || defined( __x86_64__ ) /* only for x86 */
    uint32_t eax, ebx, ecx, edx;
    cpuid( 1, &eax, &ebx, &ecx, &edx );
    return ( ecx & ( 1u << 25 ) ) != 0; /* Bit 25 = AES-NI */
#else /* ARM, RISC-V … */
    return 0; /* no AES-NI    */
#endif
}
