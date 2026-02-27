/*
 *  method.c: Method implementations for nwipe.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  Modifications to original dwipe Copyright Andy Beverley <andy@andybev.com>
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* HOWTO:  Add another wipe method.
 *
 *  1.  Create a new function here and add the prototype to the 'method.h' file.
 *  2.  Update nwipe_method_label() appropriately.
 *  3.  Put the passes that you wish to run into a nwipe_pattern_t array.
 *  4.  Call nwipe_runmethod() with your array of patterns.
 *  5.  Copy-and-paste within the 'options.c' file so that the new method can be invoked.
 *  6.  Optionally try to plug your function into 'gui.c'.
 *  7.  Update the function 'calculate_round_size()' with the new method.
 *
 *
 * WARNING: Remember to pad all pattern arrays with { 0, NULL }.
 *
 * WARNING: Never change nwipe_options after calling a method.
 *
 * NOTE: The nwipe_runmethod function appends a user selectable final blanking (zero) pass to all methods.
 *
 */

#include <stdint.h>
#include <string.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "pass.h"
#include "logging.h"
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h> /* SYS_getrandom */
#include <stdlib.h> /* for system() */
#include <errno.h>
#if defined( __linux__ )
/* On glibc/musl with <sys/random.h> available, it's fine (optional). */
/* #include <sys/random.h> */
#endif

/**
 * @brief Fill a buffer with cryptographically secure random bytes using getrandom(2).
 *
 * This wrapper blocks until the kernel CRNG is initialized, then loops until
 * @p len bytes are written (handling short reads and EINTR/EAGAIN).
 *
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Number of bytes to generate.
 * @return On success, returns (ssize_t)len.
 *         On error, returns -errno and leaves errno set.
 */
static ssize_t nwipe_read_entropy( void* buf, size_t len )
{
    unsigned char* p = (unsigned char*) buf;
    size_t n = len;

    while( n > 0 )
    {
        /* Prefer the raw syscall to avoid libc version pitfalls. */
        ssize_t r = syscall( SYS_getrandom, p, n, 0 /* blocking */ );
        if( r < 0 )
        {
            if( errno == EINTR || errno == EAGAIN )
            {
                continue; /* retry */
            }
            return -errno;
        }
        if( r == 0 )
        {
            /* Extremely unlikely: treat as transient and retry. */
            continue;
        }
        p += r;
        n -= (size_t) r;
    }
    return (ssize_t) len;
}

/*
 * Comment Legend
 *
 *   "method"  An ordered set of patterns.
 *   "pattern" The magic bits that will be written to a device.
 *   "pass"    Reading or writing one pattern to an entire device.
 *   "rounds"  The number of times that a method will be applied to a device.
 *
 */

const char* nwipe_dod522022m_label = "DoD 5220.22-M";
const char* nwipe_dodshort_label = "DoD Short";
const char* nwipe_gutmann_label = "Gutmann Wipe";
const char* nwipe_ops2_label = "RCMP TSSIT OPS-II";
const char* nwipe_random_label = "PRNG Stream";
const char* nwipe_zero_label = "Fill With Zeros";
const char* nwipe_one_label = "Fill With Ones";
const char* nwipe_verify_zero_label = "Verify Zeros (0x00)";
const char* nwipe_verify_one_label = "Verify Ones  (0xFF)";
const char* nwipe_is5enh_label = "HMG IS5 Enhanced";
const char* nwipe_bruce7_label = "Bruce Schneier 7-Pass";
const char* nwipe_bmb_label = "BMB21-2019";
const char* nwipe_secure_erase_label = "Secure Erase (ATA/NVMe)";
const char* nwipe_secure_erase_prng_verify_label = "Secure Erase + PRNG + Verify";
const char* nwipe_sanitize_crypto_erase_label = "Sanitize Crypto Erase (ATA/SCSI/NVMe)";
const char* nwipe_sanitize_block_erase_label = "Sanitize Block Erase (ATA/SCSI/NVMe)";
const char* nwipe_sanitize_overwrite_label = "Sanitize Overwrite (ATA/SCSI/NVMe)";

const char* nwipe_unknown_label = "Unknown Method (FIXME)";

const char* nwipe_method_label( void* method )
{
    /**
     *  Returns a pointer to the name of the method function.
     *
     */

    if( method == &nwipe_dod522022m )
    {
        return nwipe_dod522022m_label;
    }
    if( method == &nwipe_dodshort )
    {
        return nwipe_dodshort_label;
    }
    if( method == &nwipe_gutmann )
    {
        return nwipe_gutmann_label;
    }
    if( method == &nwipe_ops2 )
    {
        return nwipe_ops2_label;
    }
    if( method == &nwipe_random )
    {
        return nwipe_random_label;
    }
    if( method == &nwipe_zero )
    {
        return nwipe_zero_label;
    }
    if( method == &nwipe_one )
    {
        return nwipe_one_label;
    }
    if( method == &nwipe_verify_zero )
    {
        return nwipe_verify_zero_label;
    }
    if( method == &nwipe_verify_one )
    {
        return nwipe_verify_one_label;
    }
    if( method == &nwipe_is5enh )
    {
        return nwipe_is5enh_label;
    }
    if( method == &nwipe_bruce7 )
    {
        return nwipe_bruce7_label;
    }
    if( method == &nwipe_bmb )
    {
        return nwipe_bmb_label;
    }
    if( method == &nwipe_secure_erase )
    {
        return nwipe_secure_erase_label;
    }
    if( method == &nwipe_secure_erase_prng_verify )
    {
        return nwipe_secure_erase_prng_verify_label;
    }

    if( method == &nwipe_sanitize_crypto_erase )
    {
        return nwipe_sanitize_crypto_erase_label;
    }
    if( method == &nwipe_sanitize_block_erase )
    {
        return nwipe_sanitize_block_erase_label;
    }
    if( method == &nwipe_sanitize_overwrite )
    {
        return nwipe_sanitize_overwrite_label;
    }

    /* else */
    return nwipe_unknown_label;

} /* nwipe_method_label */

/*
 * Execute ATA Secure Erase using the external hdparm(8) utility.
 *
 * This implementation assumes:
 *   - Linux with glibc
 *   - hdparm is installed and available in the system PATH
 *
 * We deliberately do not re-implement the low-level ATA SECURITY
 * commands here (SECURITY_SET_PASSWORD, SECURITY_ERASE_UNIT, etc.)
 * because that logic is complex, hardware-specific and already
 * well-tested in hdparm.
 *
 * Returns 0 on success, -1 on failure.
 */
static int nwipe_execute_ata_secure_erase( nwipe_context_t* c )
{
    char cmd[512];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Secure Erase: invalid context or device name." );
        return -1;
    }

    /*
     * Build a hdparm command line:
     *
     *   hdparm --user-master u --security-erase NULL /dev/sdX
     *
     * Notes:
     *   - We redirect all output to /dev/null to keep the GUI clean.
     *   - The password "NULL" is arbitrary but consistent with common
     *     hdparm usage for temporary erase passwords.
     *   - The user is expected to run nwipe as root, so hdparm will
     *     have the necessary privileges.
     */
    rc = snprintf( cmd,
                   sizeof( cmd ),
                   "hdparm --yes-i-know-what-i-am-doing "
                   "--user-master u --security-erase NULL '%s' >/dev/null 2>&1",
                   c->device_name );

    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Secure Erase: command line for device %s is too long.", c->device_name );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting ATA Secure Erase via hdparm on %s.", c->device_name );

    errno = 0;
    rc = system( cmd );

    if( rc == -1 )
    {
        /*
         * system() itself failed (fork/exec or similar). This usually
         * indicates a serious problem (e.g. no memory, no /bin/sh).
         */
        nwipe_perror( errno, __FUNCTION__, "system" );
        nwipe_log( NWIPE_LOG_ERROR, "ATA Secure Erase: failed to invoke hdparm for %s.", c->device_name );
        return -1;
    }

    /*
     * We treat any non-zero return code from system() as a failure of
     * the secure erase operation. Detailed diagnostics are available
     * by running hdparm manually without redirection.
     */
    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Secure Erase: hdparm reported failure on %s (rc=%d).", c->device_name, rc );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "ATA Secure Erase completed successfully on %s.", c->device_name );

    return 0;
}

/*
 * Execute NVMe Secure Erase using the external nvme(1) utility.
 *
 * This implementation assumes:
 *   - Linux with glibc
 *   - nvme-cli is installed and provides the "nvme" binary
 *
 * We use the "nvme format" command with a secure erase setting.
 * The exact sanitize/format behaviour is controller-specific; for
 * most consumer devices, "nvme format -s1" triggers a user-data
 * erase suitable for secure wipe purposes.
 *
 * Returns 0 on success, -1 on failure.
 */

/* -------------------- Sanitize (Crypto Erase) helpers -------------------- */

/* Best-effort: map /dev/nvme0n1 -> /dev/nvme0 (controller char device) */
static int nwipe_nvme_controller_from_node( const char* in, char* out, size_t out_sz )
{
    if( in == NULL || out == NULL || out_sz < 8 )
    {
        return -1;
    }

    /* Copy first, then strip partition suffix (p\d+) and namespace suffix (n\d+). */
    if( snprintf( out, out_sz, "%s", in ) >= (int) out_sz )
    {
        return -1;
    }

    /* Strip partition suffix (e.g. /dev/nvme0n1p2 -> /dev/nvme0n1). */
    {
        size_t len = strlen( out );
        while( len > 0 && out[len - 1] >= '0' && out[len - 1] <= '9' )
        {
            len--;
        }
        if( len > 0 && out[len - 1] == 'p' )
        {
            out[len - 1] = '\0';
        }
    }

    /* Strip namespace suffix (e.g. /dev/nvme0n1 -> /dev/nvme0). */
    {
        size_t len = strlen( out );
        while( len > 0 && out[len - 1] >= '0' && out[len - 1] <= '9' )
        {
            len--;
        }
        if( len > 0 && out[len - 1] == 'n' )
        {
            out[len - 1] = '\0';
        }
    }

    return 0;
}

/* Best-effort: map /dev/sda1 -> /dev/sda (for sg_sanitize/hdparm) */
static int nwipe_strip_partition_suffix( const char* in, char* out, size_t out_sz )
{
    if( in == NULL || out == NULL || out_sz < 8 )
    {
        return -1;
    }

    if( snprintf( out, out_sz, "%s", in ) >= (int) out_sz )
    {
        return -1;
    }

    /* NVMe partitions handled elsewhere; for sdX style: strip trailing digits. */
    {
        size_t len = strlen( out );
        while( len > 0 && out[len - 1] >= '0' && out[len - 1] <= '9' )
        {
            len--;
        }
        if( len > 0 && out[len - 1] != '/' )
        {
            out[len] = '\0';
        }
    }

    return 0;
}

static int nwipe_execute_nvme_sanitize_crypto_erase( nwipe_context_t* c )
{
    char cmd[512];
    char ctrl[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Crypto Erase: invalid context or device name." );
        return -1;
    }

    if( nwipe_nvme_controller_from_node( c->device_name, ctrl, sizeof( ctrl ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "NVMe Sanitize Crypto Erase: unable to derive controller node from %s.", c->device_name );
        return -1;
    }

    /*
     * nvme-sanitize expects the controller character device (e.g. /dev/nvme0).
     * We request the Crypto Erase Sanitize operation (action 4 / start-crypto-erase).
     */
    rc = snprintf( cmd, sizeof( cmd ), "nvme sanitize '%s' -a 4 >/dev/null 2>&1", ctrl );
    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Crypto Erase: command line for device %s is too long.", ctrl );
        return -1;
    }

    nwipe_log(
        NWIPE_LOG_NOTICE, "Starting NVMe Sanitize Crypto Erase via nvme on %s (from %s).", ctrl, c->device_name );

    errno = 0;
    rc = system( cmd );
    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Crypto Erase failed on %s (rc=%d).", ctrl, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_ata_sanitize_crypto_scramble( nwipe_context_t* c )
{
    char cmd[512];
    char dev[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Crypto Scramble: invalid context or device name." );
        return -1;
    }

    if( nwipe_strip_partition_suffix( c->device_name, dev, sizeof( dev ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "ATA Sanitize Crypto Scramble: unable to normalize device node from %s.", c->device_name );
        return -1;
    }

    /*
     * ATA Sanitize Feature Set crypto operation (aka "crypto scramble") via hdparm.
     * Note: Many distros require the explicit acknowledgement flag.
     */
    rc = snprintf( cmd,
                   sizeof( cmd ),
                   "hdparm --yes-i-know-what-i-am-doing --sanitize-crypto-scramble '%s' >/dev/null 2>&1",
                   dev );

    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Crypto Scramble: command line for device %s is too long.", dev );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting ATA Sanitize Crypto Scramble via hdparm on %s.", dev );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Crypto Scramble failed on %s (rc=%d).", dev, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_scsi_sanitize_crypto_erase( nwipe_context_t* c )
{
    char cmd[512];
    char dev[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Crypto Erase: invalid context or device name." );
        return -1;
    }

    if( nwipe_strip_partition_suffix( c->device_name, dev, sizeof( dev ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "SCSI Sanitize Crypto Erase: unable to normalize device node from %s.", c->device_name );
        return -1;
    }

    /*
     * SCSI SANITIZE crypto erase via sg3_utils.
     * Without --quick, sg_sanitize inserts a short safety delay.
     */
    rc = snprintf( cmd, sizeof( cmd ), "sg_sanitize --crypto '%s' >/dev/null 2>&1", dev );

    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Crypto Erase: command line for device %s is too long.", dev );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting SCSI Sanitize Crypto Erase via sg_sanitize on %s.", dev );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Crypto Erase failed on %s (rc=%d).", dev, rc );
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------------ */

static int nwipe_execute_nvme_sanitize_block_erase( nwipe_context_t* c )
{
    char cmd[512];
    char ctrl[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Block Erase: invalid context or device name." );
        return -1;
    }

    if( nwipe_nvme_controller_from_node( c->device_name, ctrl, sizeof( ctrl ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "NVMe Sanitize Block Erase: unable to derive controller node from %s.", c->device_name );
        return -1;
    }

    /* nvme sanitize expects the controller character device (e.g. /dev/nvme0). Action 2 = Block Erase. */
    rc = snprintf( cmd, sizeof( cmd ), "nvme sanitize '%s' -a 2 >/dev/null 2>&1", ctrl );
    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Block Erase: command line for device %s is too long.", ctrl );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting NVMe Sanitize Block Erase via nvme on %s (from %s).", ctrl, c->device_name );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Block Erase failed on %s (rc=%d).", ctrl, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_nvme_sanitize_overwrite( nwipe_context_t* c )
{
    char cmd[512];
    char ctrl[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Overwrite: invalid context or device name." );
        return -1;
    }

    if( nwipe_nvme_controller_from_node( c->device_name, ctrl, sizeof( ctrl ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "NVMe Sanitize Overwrite: unable to derive controller node from %s.", c->device_name );
        return -1;
    }

    /* Action 3 = Overwrite. */
    rc = snprintf( cmd, sizeof( cmd ), "nvme sanitize '%s' -a 3 >/dev/null 2>&1", ctrl );
    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Overwrite: command line for device %s is too long.", ctrl );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting NVMe Sanitize Overwrite via nvme on %s (from %s).", ctrl, c->device_name );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Sanitize Overwrite failed on %s (rc=%d).", ctrl, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_ata_sanitize_block_erase( nwipe_context_t* c )
{
    char cmd[512];
    char dev[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Block Erase: invalid context or device name." );
        return -1;
    }

    if( nwipe_strip_partition_suffix( c->device_name, dev, sizeof( dev ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "ATA Sanitize Block Erase: unable to normalize device node from %s.", c->device_name );
        return -1;
    }

    rc = snprintf(
        cmd, sizeof( cmd ), "hdparm --yes-i-know-what-i-am-doing --sanitize-block-erase '%s' >/dev/null 2>&1", dev );
    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Block Erase: command line for device %s is too long.", dev );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting ATA Sanitize Block Erase via hdparm on %s.", dev );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Block Erase failed on %s (rc=%d).", dev, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_ata_sanitize_overwrite( nwipe_context_t* c )
{
    char cmd[512];
    char dev[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Overwrite: invalid context or device name." );
        return -1;
    }

    if( nwipe_strip_partition_suffix( c->device_name, dev, sizeof( dev ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "ATA Sanitize Overwrite: unable to normalize device node from %s.", c->device_name );
        return -1;
    }

    /*
     * Use 1 pass and specify a deterministic overwrite pattern.
     * hdparm requires the acknowledgement flag for sanitize commands.
     */
    rc = snprintf(
        cmd,
        sizeof( cmd ),
        "hdparm --yes-i-know-what-i-am-doing --sanitize-overwrite-passes 1 --sanitize-overwrite hex:00000000 '%s' "
        ">/dev/null 2>&1",
        dev );
    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Overwrite: command line for device %s is too long.", dev );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting ATA Sanitize Overwrite via hdparm on %s.", dev );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "ATA Sanitize Overwrite failed on %s (rc=%d).", dev, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_scsi_sanitize_block_erase( nwipe_context_t* c )
{
    char cmd[512];
    char dev[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Block Erase: invalid context or device name." );
        return -1;
    }

    if( nwipe_strip_partition_suffix( c->device_name, dev, sizeof( dev ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "SCSI Sanitize Block Erase: unable to normalize device node from %s.", c->device_name );
        return -1;
    }

    rc = snprintf( cmd, sizeof( cmd ), "sg_sanitize --block '%s' >/dev/null 2>&1", dev );
    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Block Erase: command line for device %s is too long.", dev );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting SCSI Sanitize Block Erase via sg_sanitize on %s.", dev );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Block Erase failed on %s (rc=%d).", dev, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_scsi_sanitize_overwrite( nwipe_context_t* c )
{
    char cmd[512];
    char dev[256];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Overwrite: invalid context or device name." );
        return -1;
    }

    if( nwipe_strip_partition_suffix( c->device_name, dev, sizeof( dev ) ) != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "SCSI Sanitize Overwrite: unable to normalize device node from %s.", c->device_name );
        return -1;
    }

    /* Use --zero to request zero-filled overwrite where supported. */
    rc = snprintf( cmd, sizeof( cmd ), "sg_sanitize --overwrite --zero '%s' >/dev/null 2>&1", dev );
    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Overwrite: command line for device %s is too long.", dev );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting SCSI Sanitize Overwrite via sg_sanitize on %s.", dev );

    errno = 0;
    rc = system( cmd );

    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "SCSI Sanitize Overwrite failed on %s (rc=%d).", dev, rc );
        return -1;
    }

    return 0;
}

static int nwipe_execute_nvme_secure_erase( nwipe_context_t* c )
{
    char cmd[512];
    int rc;

    if( c == NULL || c->device_name == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Secure Erase: invalid context or device name." );
        return -1;
    }

    /*
     * Build an nvme command line:
     *
     *   nvme format /dev/nvmeXnY -s1
     *
     * Notes:
     *   - We assume c->device_name points at the namespace device node
     *     (e.g. /dev/nvme0n1). For controller nodes the syntax is
     *     slightly different; that can be extended later if needed.
     *   - "-s1" requests a secure erase of user data where supported.
     *   - Output is redirected to /dev/null to keep the GUI clean.
     */
    rc = snprintf( cmd, sizeof( cmd ), "nvme format '%s' -s1 >/dev/null 2>&1", c->device_name );

    if( rc < 0 || (size_t) rc >= sizeof( cmd ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Secure Erase: command line for device %s is too long.", c->device_name );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Starting NVMe Secure Erase via nvme format on %s.", c->device_name );

    errno = 0;
    rc = system( cmd );

    if( rc == -1 )
    {
        /*
         * system() itself failed (fork/exec or similar). This usually
         * indicates a serious problem (e.g. no memory, no /bin/sh).
         */
        nwipe_perror( errno, __FUNCTION__, "system" );
        nwipe_log( NWIPE_LOG_ERROR, "NVMe Secure Erase: failed to invoke nvme for %s.", c->device_name );
        return -1;
    }

    /*
     * As with the ATA case, any non-zero return code is treated as a
     * failure of the secure erase operation.
     */
    if( rc != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "NVMe Secure Erase: nvme format reported failure on %s (rc=%d).", c->device_name, rc );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "NVMe Secure Erase completed successfully on %s.", c->device_name );

    return 0;
}

void* nwipe_zero( void* ptr )
{
    /**
     * Fill the device with zeroes.
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* setup for a zero-fill. */

    char zerofill[1] = { '\x00' };
    nwipe_pattern_t patterns[] = { { 1, &zerofill[0] },  // pass 1: 0s
                                   { 0, NULL } };

    /* Run the method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_zero */

void* nwipe_one( void* ptr )
{
    /**
     * Fill the device with ones.
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* setup for a one-fill. */

    char onefill[1] = { '\xFF' };
    nwipe_pattern_t patterns[] = { { 1, &onefill[0] },  // pass 1: 1s
                                   { 0, NULL } };

    /* Run the method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_one */

void* nwipe_verify_zero( void* ptr )
{
    /**
     * Verify the device is full of zeros.
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* Do nothing because nwipe_runmethod appends a zero-fill. */
    nwipe_pattern_t patterns[] = { { 0, NULL } };

    /* Run the method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_verify zeros */

void* nwipe_verify_one( void* ptr )
{
    /**
     * Verify the device is full of ones.
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* Do nothing because nwipe_runmethod appends a zero-fill. */
    nwipe_pattern_t patterns[] = { { 0, NULL } };

    /* Run the method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_verify */

void* nwipe_dod522022m( void* ptr )
{
    /**
     * United States Department of Defense 5220.22-M standard wipe.
     *
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* A result holder. */
    int r;

    /* Random characters. (Elements 2 and 6 are unused.) */
    char dod[7];

    nwipe_pattern_t patterns[] = { { 1, &dod[0] },  // Pass 1: A random character.
                                   { 1, &dod[1] },  // Pass 2: The bitwise complement of pass 1.
                                   { -1, "" },  // Pass 3: A random stream.
                                   { 1, &dod[3] },  // Pass 4: A random character.
                                   { 1, &dod[4] },  // Pass 5: A random character.
                                   { 1, &dod[5] },  // Pass 6: The bitwise complement of pass 5.
                                   { -1, "" },  // Pass 7: A random stream.
                                   { 0, NULL } };

    /* Load the array with random characters. */
    r = nwipe_read_entropy( &dod, sizeof( dod ) );

    /* NOTE: Only the random data in dod[0], dod[3], and dod[4] is actually used. */

    /* Check the result. */
    if( r != sizeof( dod ) )
    {
        r = errno;
        nwipe_perror( r, __FUNCTION__, "read" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to seed the %s method.", nwipe_dod522022m_label );

        /* Ensure a negative return. */
        if( r < 0 )
        {
            c->result = r;
            return NULL;
        }
        else
        {
            c->result = -1;
            return NULL;
        }
    }

    /* Pass 2 is the bitwise complement of Pass 1. */
    dod[1] = ~dod[0];

    /* Pass 4 is the bitwise complement of Pass 3. */
    dod[5] = ~dod[4];

    /* Run the DoD 5220.22-M method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_dod522022m */

void* nwipe_dodshort( void* ptr )
{
    /**
     * United States Department of Defense 5220.22-M short wipe.
     * This method is comprised of passes 1,2,7 from the standard wipe.
     *
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* A result holder. */
    int r;

    /* Random characters. (Element 3 is unused.) */
    char dod[3];

    nwipe_pattern_t patterns[] = { { 1, &dod[0] },  // Pass 1: A random character.
                                   { 1, &dod[1] },  // Pass 2: The bitwise complement of pass 1.
                                   { -1, "" },  // Pass 3: A random stream.
                                   { 0, NULL } };

    /* Load the array with random characters. */
    r = nwipe_read_entropy( &dod, sizeof( dod ) );

    /* NOTE: Only the random data in dod[0] is actually used. */

    /* Check the result. */
    if( r != sizeof( dod ) )
    {
        r = errno;
        nwipe_perror( r, __FUNCTION__, "read" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to seed the %s method.", nwipe_dodshort_label );

        /* Ensure a negative return. */
        if( r < 0 )
        {
            c->result = r;
            return NULL;
        }
        else
        {
            c->result = -1;
            return NULL;
        }
    }

    /* Pass 2 is the bitwise complement of Pass 1. */
    dod[1] = ~dod[0];

    /* Run the DoD 5220.022-M short method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_dodshort */

void* nwipe_gutmann( void* ptr )
{
    /**
     * Peter Gutmann's wipe.
     *
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* Define the Gutmann method. */
    nwipe_pattern_t book[] = { { -1, "" },  // Random pass.
                               { -1, "" },  // Random pass.
                               { -1, "" },  // Random pass.
                               { -1, "" },  // Random pass.
                               { 3, "\x55\x55\x55" },  // Static pass: 0x555555  01010101 01010101 01010101
                               { 3, "\xAA\xAA\xAA" },  // Static pass: 0XAAAAAA  10101010 10101010 10101010
                               { 3, "\x92\x49\x24" },  // Static pass: 0x924924  10010010 01001001 00100100
                               { 3, "\x49\x24\x92" },  // Static pass: 0x492492  01001001 00100100 10010010
                               { 3, "\x24\x92\x49" },  // Static pass: 0x249249  00100100 10010010 01001001
                               { 3, "\x00\x00\x00" },  // Static pass: 0x000000  00000000 00000000 00000000
                               { 3, "\x11\x11\x11" },  // Static pass: 0x111111  00010001 00010001 00010001
                               { 3, "\x22\x22\x22" },  // Static pass: 0x222222  00100010 00100010 00100010
                               { 3, "\x33\x33\x33" },  // Static pass: 0x333333  00110011 00110011 00110011
                               { 3, "\x44\x44\x44" },  // Static pass: 0x444444  01000100 01000100 01000100
                               { 3, "\x55\x55\x55" },  // Static pass: 0x555555  01010101 01010101 01010101
                               { 3, "\x66\x66\x66" },  // Static pass: 0x666666  01100110 01100110 01100110
                               { 3, "\x77\x77\x77" },  // Static pass: 0x777777  01110111 01110111 01110111
                               { 3, "\x88\x88\x88" },  // Static pass: 0x888888  10001000 10001000 10001000
                               { 3, "\x99\x99\x99" },  // Static pass: 0x999999  10011001 10011001 10011001
                               { 3, "\xAA\xAA\xAA" },  // Static pass: 0xAAAAAA  10101010 10101010 10101010
                               { 3, "\xBB\xBB\xBB" },  // Static pass: 0xBBBBBB  10111011 10111011 10111011
                               { 3, "\xCC\xCC\xCC" },  // Static pass: 0xCCCCCC  11001100 11001100 11001100
                               { 3, "\xDD\xDD\xDD" },  // Static pass: 0xDDDDDD  11011101 11011101 11011101
                               { 3, "\xEE\xEE\xEE" },  // Static pass: 0xEEEEEE  11101110 11101110 11101110
                               { 3, "\xFF\xFF\xFF" },  // Static pass: 0xFFFFFF  11111111 11111111 11111111
                               { 3, "\x92\x49\x24" },  // Static pass: 0x924924  10010010 01001001 00100100
                               { 3, "\x49\x24\x92" },  // Static pass: 0x492492  01001001 00100100 10010010
                               { 3, "\x24\x92\x49" },  // Static pass: 0x249249  00100100 10010010 01001001
                               { 3, "\x6D\xB6\xDB" },  // Static pass: 0x6DB6DB  01101101 10110110 11011011
                               { 3, "\xB6\xDB\x6D" },  // Static pass: 0xB6DB6D  10110110 11011011 01101101
                               { 3, "\xDB\x6D\xB6" },  // Static pass: 0XDB6DB6  11011011 01101101 10110110
                               { -1, "" },  // Random pass.
                               { -1, "" },  // Random pass.
                               { -1, "" },  // Random pass.
                               { -1, "" },  // Random pass.
                               { 0, NULL } };

    /* Put the book array into this array in random order. */
    nwipe_pattern_t patterns[36];

    /* An entropy buffer. */
    u16 s[27];

    /* Load the array with random characters. */
    ssize_t r = nwipe_read_entropy( &s, sizeof( s ) );
    if( r != sizeof( s ) )
    {
        r = errno;
        nwipe_perror( r, __FUNCTION__, "read" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to seed the %s method.", nwipe_gutmann_label );

        /* Ensure a negative return. */
        if( r < 0 )
        {
            c->result = r;
            return NULL;
        }
        else
        {
            c->result = -1;
            return NULL;
        }
    }

    // First 4 random passes
    for( int i = 0; i <= 3; ++i )
    {
        patterns[i] = book[i];
    }
    // Middle 27 passes in random order
    for( int i = 26; i >= 0; --i )
    {
        /* Get a random integer that is less than the first index 'i'. */
        int n = (int) ( (double) ( s[i] ) / (double) ( 0x0000FFFF + 1 ) * (double) ( i + 1 ) );

        /* Initialize the secondary index. */
        int j = 3;

        while( n-- >= 0 )
        {
            /* Advance 'j' by 'n' positions... */
            j += 1;

            /* ... but don't count 'book' elements that have already been copied. */
            while( book[j].length == 0 )
            {
                j += 1;
            }
        }

        /* Copy the element. */
        patterns[i + 4] = book[j];

        /* Mark this element as having been used. */
        book[j].length = 0;
    }
    // Last 4 random passes
    for( int i = 31; i <= 34; ++i )
    {
        patterns[i] = book[i];
    }

    /* Ensure that the array is terminated. */
    patterns[35].length = 0;
    patterns[35].s = NULL;

    /* Run the Gutmann method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_gutmann */

void* nwipe_ops2( void* ptr )
{
    /**
     *  Royal Canadian Mounted Police
     *  Technical Security Standard for Information Technology
     *  Appendix OPS-II: Media Sanitization
     *
     *  NOTE: The last pass of this method is specially handled by nwipe_runmethod.
     *
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* A generic array index. */
    int i;

    /* A generic result buffer. */
    int r;

    /* A buffer for random characters. */
    char* s;

    /* A buffer for the bitwise complements of 's'. */
    char* t;

    /* The element count of 's' and 't'. */
    u32 u;

    /* The pattern array for this method is dynamically allocated. */
    nwipe_pattern_t* patterns;

    /* The element count of 'patterns'. */
    u32 q;

    /* We need one random character per round. */
    u = 1 * nwipe_options.rounds;

    /* Allocate the array of random characters. */
    s = malloc( sizeof( char ) * u );

    if( s == NULL )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate the random character array." );
        c->result = -1;
        return NULL;
    }

    /* Allocate the array of complement characters. */
    t = malloc( sizeof( char ) * u );

    if( t == NULL )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate the complement character array." );
        c->result = -1;
        free( s );
        return NULL;
    }

    /* We need eight pattern elements per round, plus one for padding. */
    q = 8 * u + 1;

    /* Allocate the pattern array. */
    patterns = malloc( sizeof( nwipe_pattern_t ) * q );

    if( patterns == NULL )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate the pattern array." );
        c->result = -1;
        free( s );
        free( t );
        return NULL;
    }

    /* Load the array of random characters. */
    r = nwipe_read_entropy( s, u );

    if( r != u )
    {
        r = errno;
        nwipe_perror( r, __FUNCTION__, "read" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to seed the %s method.", nwipe_ops2_label );

        if( r < 0 )
        {
            c->result = r;
            free( s );
            free( t );
            free( patterns );
            return NULL;
        }
        else
        {
            c->result = -1;
            free( s );
            free( t );
            free( patterns );
            return NULL;
        }
    }

    for( i = 0; i < u; i += 1 )
    {
        /* Populate the array of complements. */
        t[i] = ~s[i];
    }

    for( i = 0; i < u; i += 8 )
    {
        /* Populate the array of patterns. */

        /* Even elements point to the random characters. */
        patterns[i * 4 + 0].length = 1;
        patterns[i * 4 + 0].s = &s[i];
        patterns[i * 4 + 2].length = 1;
        patterns[i * 4 + 2].s = &s[i];
        patterns[i * 4 + 4].length = 1;
        patterns[i * 4 + 4].s = &s[i];
        patterns[i * 4 + 6].length = 1;
        patterns[i * 4 + 6].s = &s[i];

        /* Odd elements point to the complement characters. */
        patterns[i * 4 + 1].length = 1;
        patterns[i * 4 + 1].s = &t[i];
        patterns[i * 4 + 3].length = 1;
        patterns[i * 4 + 3].s = &t[i];
        patterns[i * 4 + 5].length = 1;
        patterns[i * 4 + 5].s = &t[i];
        patterns[i * 4 + 7].length = 1;
        patterns[i * 4 + 7].s = &t[i];
    }

    /* Ensure that the array is terminated. */
    patterns[q - 1].length = 0;
    patterns[q - 1].s = NULL;

    /* Run the TSSIT OPS-II method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Release the random character buffer. */
    free( s );

    /* Release the complement character buffer */
    free( t );

    /* Release the pattern buffer. */
    free( patterns );

    /* We're done. */

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_ops2 */

void* nwipe_is5enh( void* ptr )
{
    nwipe_context_t* c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    c->wipe_status = 1;

    char is5enh[3] = { '\x00', '\xFF', '\x00' };
    nwipe_pattern_t patterns[] = { { 1, &is5enh[0] },  // Pass 1: 0s
                                   { 1, &is5enh[1] },  // Pass 2: 1s
                                   { -1, &is5enh[2] },  // Pass 3: random bytes with verification
                                   { 0, NULL } };
    c->result = nwipe_runmethod( c, patterns );

    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_is5enh */

void* nwipe_random( void* ptr )
{
    /**
     * Fill the device with a stream from the PRNG.
     *
     */

    nwipe_context_t* c;
    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* Define the random method. */
    nwipe_pattern_t patterns[] = { { -1, "" }, { 0, NULL } };

    /* Run the method. */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_random */

void* nwipe_bruce7( void* ptr )
{
    /**
     * Bruce Schneier 7-Pass wiping method.
     *
     * Pass 1: Overwrite the drive with all ones (0xFF).
     * Pass 2: Overwrite the drive with all zeroes (0x00).
     * Pass 3-7: Overwrite the drive with five passes of random data.
     */

    nwipe_context_t* c = (nwipe_context_t*) ptr;

    /* Get current time at the start of the wipe */
    time( &c->start_time );

    /* Set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* Setup for Bruce Schneier 7-Pass method */
    char onefill[1] = { '\xFF' };
    char zerofill[1] = { '\x00' };
    nwipe_pattern_t patterns[] = {
        { 1, &onefill[0] },  // Pass 1: Overwrite with ones
        { 1, &zerofill[0] },  // Pass 2: Overwrite with zeroes
        { -1, "" },  // Pass 3: Random data
        { -1, "" },  // Pass 4: Random data
        { -1, "" },  // Pass 5: Random data
        { -1, "" },  // Pass 6: Random data
        { -1, "" },  // Pass 7: Random data
        { 0, NULL }  // Terminate pattern array
    };

    /* Run the Bruce Schneier 7-Pass method */
    c->result = nwipe_runmethod( c, patterns );

    /* Finished. Set the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* Get current time at the end of the wipe */
    time( &c->end_time );

    return NULL;
}

void* nwipe_bmb( void* ptr )
{
    /**
     * BMB Secure Wipe Method:
     * Pass 1: 0xFF
     * Pass 2: 0x00
     * Pass 3-5: 3× Random
     * Pass 6: 0xFF
     */

    nwipe_context_t* c = (nwipe_context_t*) ptr;

    time( &c->start_time );
    c->wipe_status = 1;

    char onefill[1] = { '\xFF' };
    char zerofill[1] = { '\x00' };

    nwipe_pattern_t patterns[] = {
        { 1, &onefill[0] },  // 0xFF
        { 1, &zerofill[0] },  // 0x00
        { -1, "" },  // RANDOM
        { -1, "" },  // RANDOM
        { -1, "" },  // RANDOM
        { 1, &onefill[0] },  // 0xFF
        { 0, NULL }  // 0X00
    };

    c->result = nwipe_runmethod( c, patterns );

    c->wipe_status = 0;
    time( &c->end_time );

    return NULL;
}
void* nwipe_secure_erase( void* ptr )
{
    /**
     * Perform a drive-internal secure erase (ATA/NVMe) and then verify
     * that the device reads back as all zeros.
     *
     * This method does NOT write patterns itself. Instead it:
     *   1) issues the appropriate secure-erase operation for the bus type
     *   2) performs a full-device zero verification pass using nwipe_static_verify()
     */

    nwipe_context_t* c;
    int op_result = -1;

    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe  */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /*
     * Step 1: Execute the drive's internal secure erase, depending on bus type.
     */
    switch( c->device_type )
    {
        case NWIPE_DEVICE_NVME:
            nwipe_log(
                NWIPE_LOG_NOTICE, "Attempting NVMe Secure Erase on %s (%s).", c->device_name, c->device_type_str );
            op_result = nwipe_execute_nvme_secure_erase( c );
            break;

        case NWIPE_DEVICE_ATA:
        case NWIPE_DEVICE_SAS:
            nwipe_log(
                NWIPE_LOG_NOTICE, "Attempting ATA Secure Erase on %s (%s).", c->device_name, c->device_type_str );
            op_result = nwipe_execute_ata_secure_erase( c );
            break;

        default:
            /*
             * For other bus types we currently do not support a drive-internal
             * secure erase operation.
             */
            nwipe_log( NWIPE_LOG_WARNING,
                       "Secure Erase method is not supported on bus type %s (device %s).",
                       c->device_type_str,
                       c->device_name );
            op_result = -1;
            break;
    }

    /*
     * If the secure-erase operation failed, we do not attempt verification.
     */
    if( op_result != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Secure Erase could not be executed on %s. Aborting method.", c->device_name );
        c->result = -1;

        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /*
     * Step 2: Verify that the device now reads back as all zeros.
     * We reuse nwipe_static_verify() with a single 0x00 pattern and
     * set up the progress counters so that the GUI shows a single
     * full-device verify pass.
     */

    /* Single-byte zero pattern. */
    nwipe_pattern_t pattern_zero = { 1, "\x00" };

    /* Initialize progress accounting for a single verify pass. */
    c->round_count = 1;
    c->round_working = 1;
    c->round_done = 0;
    c->round_size = c->device_size;

    c->pass_count = 1;
    c->pass_working = 1;
    c->pass_done = 0;
    c->pass_size = c->device_size;

    /* Mark this pass as a verification pass. */
    c->pass_type = NWIPE_PASS_VERIFY;

    nwipe_log( NWIPE_LOG_NOTICE, "Secure erase completed for %s, starting zero verification pass.", c->device_name );

    /* Perform the verification. */
    c->result = nwipe_static_verify( c, &pattern_zero );

    /* Reset pass type. */
    c->pass_type = NWIPE_PASS_NONE;

    /* Finished. Clear the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe  */
    time( &c->end_time );

    return NULL;
} /* nwipe_secure_erase */

void* nwipe_secure_erase_prng_verify( void* ptr )
{
    /**
     * Perform a drive-internal secure erase (ATA/NVMe), then overwrite the
     * whole device with a single PRNG stream pass and finally verify that
     * PRNG pass.
     *
     * Rationale: Some users want to combine the device-internal sanitize with
     * an additional host-driven overwrite and deterministic verification.
     */

    nwipe_context_t* c;
    int op_result = -1;
    ssize_t r;

    c = (nwipe_context_t*) ptr;

    /* get current time at the start of the wipe */
    time( &c->start_time );

    /* set wipe in progress flag for GUI */
    c->wipe_status = 1;

    /* Step 1: Execute the drive's internal secure erase, depending on bus type. */
    switch( c->device_type )
    {
        case NWIPE_DEVICE_NVME:
            nwipe_log(
                NWIPE_LOG_NOTICE, "Attempting NVMe Secure Erase on %s (%s).", c->device_name, c->device_type_str );
            op_result = nwipe_execute_nvme_secure_erase( c );
            break;

        case NWIPE_DEVICE_ATA:
        case NWIPE_DEVICE_SAS:
            nwipe_log(
                NWIPE_LOG_NOTICE, "Attempting ATA Secure Erase on %s (%s).", c->device_name, c->device_type_str );
            op_result = nwipe_execute_ata_secure_erase( c );
            break;

        default:
            nwipe_log( NWIPE_LOG_WARNING,
                       "Secure Erase method is not supported on bus type %s (device %s).",
                       c->device_type_str,
                       c->device_name );
            op_result = -1;
            break;
    }

    if( op_result != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Secure Erase could not be executed on %s. Aborting method.", c->device_name );
        c->result = -1;

        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /* Step 2: Seed PRNG and run a single PRNG write + verify pass. */

    /* Allocate the PRNG seed buffer (mirrors nwipe_runmethod()). */
    c->prng_seed.length = NWIPE_KNOB_PRNG_STATE_LENGTH;
    c->prng_seed.s = malloc( c->prng_seed.length );

    if( !c->prng_seed.s )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the prng seed buffer." );
        c->result = -1;
        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /* Fill the seed with entropy (mirrors random passes in nwipe_runmethod()). */
    r = nwipe_read_entropy( c->prng_seed.s, c->prng_seed.length );

    if( r < 0 || r != c->prng_seed.length )
    {
        nwipe_perror( errno, __FUNCTION__, "getrandom" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to seed the PRNG (insufficient entropy?)." );
        c->result = -1;

        c->prng_seed.length = 0;
        free( c->prng_seed.s );
        c->prng_seed.s = NULL;

        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /* Progress accounting: one write pass + one verify pass. */
    c->round_count = 1;
    c->round_working = 1;
    c->round_done = 0;
    c->round_size = c->device_size * 2;

    c->pass_count = 2;
    c->pass_size = c->device_size;
    c->pass_done = 0;
    c->pass_errors = 0;
    c->verify_errors = 0;

    /* Pass 1: PRNG write. */
    c->pass_working = 1;
    c->pass_type = NWIPE_PASS_WRITE;
    nwipe_log( NWIPE_LOG_NOTICE, "Secure erase completed for %s, starting PRNG overwrite pass.", c->device_name );

    c->result = nwipe_random_pass( c );

    if( c->result < 0 )
    {
        c->pass_type = NWIPE_PASS_NONE;
        c->prng_seed.length = 0;
        free( c->prng_seed.s );
        c->prng_seed.s = NULL;

        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /* Pass 2: PRNG verify. */
    c->pass_working = 2;
    c->pass_type = NWIPE_PASS_VERIFY;
    nwipe_log( NWIPE_LOG_NOTICE, "Starting PRNG verification pass for %s.", c->device_name );

    c->result = nwipe_random_verify( c );

    /* Reset pass type. */
    c->pass_type = NWIPE_PASS_NONE;

    /* Release the seed buffer. */
    c->prng_seed.length = 0;
    free( c->prng_seed.s );
    c->prng_seed.s = NULL;

    /* Finished. Clear the wipe_status flag so that the GUI knows */
    c->wipe_status = 0;

    /* get current time at the end of the wipe */
    time( &c->end_time );

    return NULL;
}

void* nwipe_sanitize_crypto_erase( void* ptr )
{
    /**
     * Perform a Sanitize "Crypto Erase" operation where supported:
     *
     *   - NVMe:  nvme sanitize <controller> -a 4
     *   - ATA:   hdparm --sanitize-crypto-scramble
     *   - SCSI:  sg_sanitize --crypto
     *
     * IMPORTANT: Crypto erase sanitize does not guarantee the media reads back
     * as zeros. Some devices return random data after key regeneration. So we
     * intentionally do NOT run a zero-verify pass here.
     */

    nwipe_context_t* c;
    int op_result = -1;

    c = (nwipe_context_t*) ptr;

    if( c == NULL )
    {
        return NULL;
    }

    /* Mark as running. */
    c->wipe_status = 1;
    time( &c->start_time );

    switch( c->device_type )
    {
        case NWIPE_DEVICE_NVME:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting NVMe Sanitize Crypto Erase on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_nvme_sanitize_crypto_erase( c );
            break;

        case NWIPE_DEVICE_ATA:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting ATA Sanitize Crypto Scramble on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_ata_sanitize_crypto_scramble( c );
            break;

        case NWIPE_DEVICE_SAS:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting SCSI Sanitize Crypto Erase on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_scsi_sanitize_crypto_erase( c );
            break;

        default:
            nwipe_log( NWIPE_LOG_WARNING,
                       "Sanitize Crypto Erase method is not supported on bus type %s (device %s).",
                       c->device_type_str,
                       c->device_name );
            op_result = -1;
            break;
    }

    if( op_result != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Sanitize Crypto Erase failed on %s.", c->device_name );
        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /* Successful completion. */
    nwipe_log( NWIPE_LOG_NOTICE, "Sanitize Crypto Erase completed on %s.", c->device_name );

    c->wipe_status = 0;
    time( &c->end_time );
    return NULL;
}

void* nwipe_sanitize_block_erase( void* ptr )
{
    /**
     * Perform a Sanitize "Block Erase" operation where supported:
     *
     *   - NVMe:  nvme sanitize <controller> -a 2
     *   - ATA:   hdparm --sanitize-block-erase
     *   - SCSI:  sg_sanitize --block
     *
     * NOTE: After block erase sanitize, readback content may be indeterminate
     * on some devices. We therefore do NOT run a verification pass here.
     */

    nwipe_context_t* c;
    int op_result = -1;

    c = (nwipe_context_t*) ptr;

    if( c == NULL )
    {
        return NULL;
    }

    /* Mark as running. */
    c->wipe_status = 1;
    time( &c->start_time );

    switch( c->device_type )
    {
        case NWIPE_DEVICE_NVME:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting NVMe Sanitize Block Erase on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_nvme_sanitize_block_erase( c );
            break;

        case NWIPE_DEVICE_ATA:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting ATA Sanitize Block Erase on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_ata_sanitize_block_erase( c );
            break;

        case NWIPE_DEVICE_SAS:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting SCSI Sanitize Block Erase on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_scsi_sanitize_block_erase( c );
            break;

        default:
            nwipe_log( NWIPE_LOG_WARNING,
                       "Sanitize Block Erase method is not supported on bus type %s (device %s).",
                       c->device_type_str,
                       c->device_name );
            op_result = -1;
            break;
    }

    if( op_result != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Sanitize Block Erase failed on %s.", c->device_name );
        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /* Successful completion. */
    nwipe_log( NWIPE_LOG_NOTICE, "Sanitize Block Erase completed on %s.", c->device_name );

    c->wipe_status = 0;
    time( &c->end_time );
    return NULL;
}

void* nwipe_sanitize_overwrite( void* ptr )
{
    /**
     * Perform a Sanitize "Overwrite" operation where supported:
     *
     *   - NVMe:  nvme sanitize <controller> -a 3
     *   - ATA:   hdparm --sanitize-overwrite-passes 1 --sanitize-overwrite hex:00000000
     *   - SCSI:  sg_sanitize --overwrite --zero
     *
     * NOTE: Similar to other sanitize operations, readback verification may not be reliable
     * across all devices/firmware combinations. We therefore do NOT run a verify pass here.
     */

    nwipe_context_t* c;
    int op_result = -1;

    c = (nwipe_context_t*) ptr;

    if( c == NULL )
    {
        return NULL;
    }

    /* Mark as running. */
    c->wipe_status = 1;
    time( &c->start_time );

    switch( c->device_type )
    {
        case NWIPE_DEVICE_NVME:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting NVMe Sanitize Overwrite on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_nvme_sanitize_overwrite( c );
            break;

        case NWIPE_DEVICE_ATA:
            nwipe_log(
                NWIPE_LOG_NOTICE, "Attempting ATA Sanitize Overwrite on %s (%s).", c->device_name, c->device_type_str );
            op_result = nwipe_execute_ata_sanitize_overwrite( c );
            break;

        case NWIPE_DEVICE_SAS:
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Attempting SCSI Sanitize Overwrite on %s (%s).",
                       c->device_name,
                       c->device_type_str );
            op_result = nwipe_execute_scsi_sanitize_overwrite( c );
            break;

        default:
            nwipe_log( NWIPE_LOG_WARNING,
                       "Sanitize Overwrite method is not supported on bus type %s (device %s).",
                       c->device_type_str,
                       c->device_name );
            op_result = -1;
            break;
    }

    if( op_result != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Sanitize Overwrite failed on %s.", c->device_name );
        c->wipe_status = 0;
        time( &c->end_time );
        return NULL;
    }

    /* Successful completion. */
    nwipe_log( NWIPE_LOG_NOTICE, "Sanitize Overwrite completed on %s.", c->device_name );

    c->wipe_status = 0;
    time( &c->end_time );
    return NULL;
}

/**
 * Check whether verification should be skipped because earlier
 * write passes recorded errors (making verification meaningless).
 *
 * If skipped, logs a warning and advances round_done by device_size
 * so that the progress percentage remains accurate.
 *
 * Returns 1 if verification should be skipped, 0 otherwise.
 */
static int nwipe_should_skip_verify( nwipe_context_t* c )
{
    if( c->pass_errors == 0 )
        return 0;

    nwipe_log( NWIPE_LOG_WARNING,
               "Skipping verification of pass %i/%i on %s due to %llu pass errors.",
               c->pass_working,
               c->pass_count,
               c->device_name,
               c->pass_errors );

    c->round_done += c->device_size;
    return 1;
}

int nwipe_runmethod( nwipe_context_t* c, nwipe_pattern_t* patterns )
{
    /**
     * Writes patterns to the device.
     *
     */

    /* The result holder. */
    int r;

    /* An index variable. */
    int i = 0;

    /* Variable to track if it is the last pass */
    int lastpass = 0;

    i = 0;

    /* The zero-fill pattern for the final pass of most methods. */
    nwipe_pattern_t pattern_zero = { 1, "\x00" };

    /* The one-fill pattern for verification of the ones fill */
    nwipe_pattern_t pattern_one = { 1, "\xFF" };

    /* Create the PRNG state buffer. */
    c->prng_seed.length = NWIPE_KNOB_PRNG_STATE_LENGTH;
    c->prng_seed.s = malloc( c->prng_seed.length );

    /* Check the memory allocation. */
    if( !c->prng_seed.s )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the prng seed buffer." );
        return -1;
    }

    /* Count the number of patterns in the array. */
    while( patterns[i].length )
    {
        i += 1;
    }

    /* Tell the parent the number of device passes that will be run in one round. */
    c->pass_count = i;

    /* Set the number of bytes that will be written across all passes in one round. */
    c->pass_size = c->pass_count * c->device_size;

    /* For the selected method, calculate the correct round_size value (for correct percentage calculation) */
    calculate_round_size( c );

    /* If only verifying then the round size is the device size */
    if( nwipe_options.method == &nwipe_verify_zero || nwipe_options.method == &nwipe_verify_one )
    {
        c->round_size = c->device_size;
    }

    /* Initialize the working round counter. */
    c->round_working = 0;

    nwipe_log(
        NWIPE_LOG_NOTICE, "Invoking method '%s' on %s", nwipe_method_label( nwipe_options.method ), c->device_name );

    while( c->round_working < c->round_count )
    {
        /* Increment the round counter. */
        c->round_working += 1;

        nwipe_log(
            NWIPE_LOG_NOTICE, "Starting round %i of %i on %s", c->round_working, c->round_count, c->device_name );

        /* Initialize the working pass counter. */
        c->pass_working = 0;

        for( i = 0; i < c->pass_count; i++ )
        {
            /* Increment the working pass. */
            c->pass_working += 1;

            /* Check if this is the last pass. */
            if( nwipe_options.verify == NWIPE_VERIFY_LAST && nwipe_options.method != &nwipe_ops2 )
            {
                if( nwipe_options.noblank == 1 && c->round_working == c->round_count
                    && c->pass_working == c->pass_count )
                {
                    lastpass = 1;
                }
            }

            nwipe_log( NWIPE_LOG_NOTICE,
                       "Starting pass %i/%i, round %i/%i, on %s",
                       c->pass_working,
                       c->pass_count,
                       c->round_working,
                       c->round_count,
                       c->device_name );

            if( patterns[i].length == 0 )
            {
                /* Caught insanity. */
                nwipe_log( NWIPE_LOG_SANITY, "nwipe_runmethod: A non-terminating pattern element has zero length." );
                return -1;
            }

            if( patterns[i].length > 0 )
            {

                /* Write a static pass. */
                c->pass_type = NWIPE_PASS_WRITE;
                r = nwipe_static_pass( c, &patterns[i] );
                c->pass_type = NWIPE_PASS_NONE;

                /* Log number of bytes written to disk */
                nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes written to %s", c->pass_done, c->device_name );

                /* Check for a fatal error. */
                if( r < 0 )
                {
                    return r;
                }

                if( nwipe_options.verify == NWIPE_VERIFY_ALL || lastpass == 1 )
                {
                    if( !nwipe_should_skip_verify( c ) )
                    {
                        nwipe_log( NWIPE_LOG_NOTICE,
                                   "Verifying pass %i of %i, round %i of %i, on %s",
                                   c->pass_working,
                                   c->pass_count,
                                   c->round_working,
                                   c->round_count,
                                   c->device_name );

                        /* Verify this pass. */
                        c->pass_type = NWIPE_PASS_VERIFY;
                        r = nwipe_static_verify( c, &patterns[i] );
                        c->pass_type = NWIPE_PASS_NONE;

                        nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes read from %s", c->pass_done, c->device_name );

                        /* Check for a fatal error. */
                        if( r < 0 )
                        {
                            return r;
                        }

                        nwipe_log( NWIPE_LOG_NOTICE,
                                   "Verified pass %i of %i, round %i of %i, on '%s'.",
                                   c->pass_working,
                                   c->pass_count,
                                   c->round_working,
                                   c->round_count,
                                   c->device_name );
                    }
                }

            } /* static pass */

            else
            {
                c->pass_type = NWIPE_PASS_WRITE;

                /* Seed the PRNG. */
                r = nwipe_read_entropy( c->prng_seed.s, c->prng_seed.length );

                /* Check the result. */
                if( r < 0 )
                {
                    c->pass_type = NWIPE_PASS_NONE;
                    nwipe_perror( errno, __FUNCTION__, "getrandom" );
                    nwipe_log( NWIPE_LOG_FATAL, "Unable to seed the PRNG." );
                    return -1;
                }

                /* Check for a partial read. */
                if( r != c->prng_seed.length )
                {
                    /* TODO: Handle partial reads. */
                    nwipe_log( NWIPE_LOG_FATAL, "Insufficient entropy is available." );
                    return -1;
                }

                /* Write the random pass. */
                r = nwipe_random_pass( c );
                c->pass_type = NWIPE_PASS_NONE;

                /* Log number of bytes written to disk */
                nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes written to %s", c->pass_done, c->device_name );

                /* Check for a fatal error. */
                if( r < 0 )
                {
                    return r;
                }

                /* Make sure IS5 enhanced always verifies its PRNG pass regardless */
                /* of the current combination of the --noblank (which influences   */
                /* the lastpass variable) and --verify options.                    */
                if( nwipe_options.verify == NWIPE_VERIFY_ALL || lastpass == 1 || nwipe_options.method == &nwipe_is5enh )
                {
                    if( !nwipe_should_skip_verify( c ) )
                    {
                        nwipe_log( NWIPE_LOG_NOTICE,
                                   "Verifying pass %i of %i, round %i of %i, on %s",
                                   c->pass_working,
                                   c->pass_count,
                                   c->round_working,
                                   c->round_count,
                                   c->device_name );

                        /* Verify this pass. */
                        c->pass_type = NWIPE_PASS_VERIFY;
                        r = nwipe_random_verify( c );
                        c->pass_type = NWIPE_PASS_NONE;

                        nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes read from %s", c->pass_done, c->device_name );

                        /* Check for a fatal error. */
                        if( r < 0 )
                        {
                            return r;
                        }

                        nwipe_log( NWIPE_LOG_NOTICE,
                                   "Verified pass %i of %i, round %i of %i, on '%s'.",
                                   c->pass_working,
                                   c->pass_count,
                                   c->round_working,
                                   c->round_count,
                                   c->device_name );
                    }
                }

            } /* random pass */

            nwipe_log( NWIPE_LOG_NOTICE,
                       "Finished pass %i/%i, round %i/%i, on %s",
                       c->pass_working,
                       c->pass_count,
                       c->round_working,
                       c->round_count,
                       c->device_name );

        } /* for passes */

        if( c->round_working < c->round_count )
        {
            nwipe_log(
                NWIPE_LOG_NOTICE, "Finished round %i of %i on %s", c->round_working, c->round_count, c->device_name );
        }
        else
        {
            nwipe_log( NWIPE_LOG_NOTICE,
                       "Finished final round %i of %i on %s",
                       c->round_working,
                       c->round_count,
                       c->device_name );
        }

    } /* while rounds */

    if( nwipe_options.method == &nwipe_ops2 )
    {
        /* NOTE: The OPS-II method specifically requires that a random pattern be left on the device. */

        /* Tell the parent that we are running the final pass. */
        c->pass_type = NWIPE_PASS_FINAL_OPS2;

        /* Seed the PRNG. */
        r = nwipe_read_entropy( c->prng_seed.s, c->prng_seed.length );

        /* Check the result. */
        if( r < 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "getrandom" );
            nwipe_log( NWIPE_LOG_FATAL, "Unable to seed the PRNG." );
            return -1;
        }

        /* Check for a partial read. */
        if( r != c->prng_seed.length )
        {
            /* TODO: Handle partial reads. */
            nwipe_log( NWIPE_LOG_FATAL, "Insufficient entropy is available." );
            return -1;
        }

        nwipe_log( NWIPE_LOG_NOTICE, "Writing final random pattern to '%s'.", c->device_name );

        /* The final ops2 pass. */
        r = nwipe_random_pass( c );

        nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes written to %s", c->pass_done, c->device_name );

        /* Check for a fatal error. */
        if( r < 0 )
        {
            return r;
        }

        if( nwipe_options.verify == NWIPE_VERIFY_LAST || nwipe_options.verify == NWIPE_VERIFY_ALL )
        {
            if( !nwipe_should_skip_verify( c ) )
            {
                nwipe_log( NWIPE_LOG_NOTICE, "Verifying final random pattern FRP on %s", c->device_name );

                /* Verify the final zero pass. */
                r = nwipe_random_verify( c );

                nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes read from %s", c->pass_done, c->device_name );

                /* Check for a fatal error. */
                if( r < 0 )
                {
                    return r;
                }

                nwipe_log( NWIPE_LOG_NOTICE, "[SUCCESS] Verified FRP on '%s' matches", c->device_name );
            }
        }

    } /* final ops2 */

    else if( nwipe_options.method == &nwipe_verify_zero )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "Verifying that %s is zeroed", c->device_name );

        /* Verify the final zero pass. */
        c->pass_type = NWIPE_PASS_VERIFY;
        r = nwipe_static_verify( c, &pattern_zero );
        c->pass_type = NWIPE_PASS_NONE;

        /* Check for a fatal error. */
        if( r < 0 )
        {
            return r;
        }
        if( c->verify_errors == 0 )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "[SUCCESS] Verified that %s is Zeroed.", c->device_name );
        }
        else
        {
            nwipe_log( NWIPE_LOG_ERROR, "[FAILURE] %s has not been Zeroed .", c->device_name );
        }

    } /* verify */

    else if( nwipe_options.method == &nwipe_verify_one )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "Verifying that %s is Ones (0xFF)", c->device_name );

        /* Verify the final ones pass. */
        c->pass_type = NWIPE_PASS_VERIFY;
        r = nwipe_static_verify( c, &pattern_one );
        c->pass_type = NWIPE_PASS_NONE;

        /* Check for a fatal error. */
        if( r < 0 )
        {
            return r;
        }
        if( c->verify_errors == 0 )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "[SUCCESS] Verified that %s is full of ones (0xFF).", c->device_name );
        }
        else
        {
            nwipe_log( NWIPE_LOG_ERROR, "[FAILURE] %s is not full of ones (0xFF).", c->device_name );
        }

    } /* verify */

    else if( nwipe_options.noblank == 0 )
    {
        /* Tell the user that we are on the final pass. */
        c->pass_type = NWIPE_PASS_FINAL_BLANK;

        nwipe_log( NWIPE_LOG_NOTICE, "Blanking device %s", c->device_name );

        /* The final zero pass. */
        r = nwipe_static_pass( c, &pattern_zero );

        /* Log number of bytes written to disk */
        nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes written to %s", c->pass_done, c->device_name );

        /* Check for a fatal error. */
        if( r < 0 )
        {
            return r;
        }

        if( nwipe_options.verify == NWIPE_VERIFY_LAST || nwipe_options.verify == NWIPE_VERIFY_ALL )
        {
            if( !nwipe_should_skip_verify( c ) )
            {
                nwipe_log( NWIPE_LOG_NOTICE, "Verifying that %s is empty.", c->device_name );

                /* Verify the final zero pass. */
                c->pass_type = NWIPE_PASS_VERIFY;
                r = nwipe_static_verify( c, &pattern_zero );
                c->pass_type = NWIPE_PASS_NONE;

                /* Log number of bytes read from disk */
                nwipe_log( NWIPE_LOG_NOTICE, "%llu bytes read from %s", c->pass_done, c->device_name );

                /* Check for a fatal error. */
                if( r < 0 )
                {
                    return r;
                }

                if( c->verify_errors == 0 )
                {
                    nwipe_log( NWIPE_LOG_NOTICE, "[SUCCESS] Verified that %s is empty.", c->device_name );
                }
                else
                {
                    nwipe_log( NWIPE_LOG_NOTICE, "[FAILURE] %s Verification errors, not empty", c->device_name );
                }
            }
        }

        if( c->verify_errors == 0 && c->pass_errors == 0 && c->fsyncdata_errors == 0 )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "[SUCCESS] Blanked device %s", c->device_name );
        }
        else
        {
            nwipe_log( NWIPE_LOG_NOTICE, "[FAILURE] %s may not be blanked", c->device_name );
        }

    } /* final blank */

    /* Release the state buffer. */
    c->prng_seed.length = 0;
    free( c->prng_seed.s );

    /* Tell the parent that we have fininshed the final pass. */
    c->pass_type = NWIPE_PASS_NONE;

    if( c->verify_errors > 0 )
    {
        /* We finished, but with non-fatal verification errors. */
        nwipe_log( NWIPE_LOG_ERROR, "%llu verification errors on '%s'.", c->verify_errors, c->device_name );
    }

    if( c->pass_errors > 0 )
    {
        /* We finished, but with non-fatal wipe errors. */
        nwipe_log( NWIPE_LOG_ERROR, "%llu wipe errors on '%s'.", c->pass_errors, c->device_name );
    }

    if( c->fsyncdata_errors > 0 )
    {
        /* We finished, but with non-fatal sync errors. */
        nwipe_log( NWIPE_LOG_ERROR, "%llu sync errors on '%s'.", c->fsyncdata_errors, c->device_name );
    }

    if( c->pass_errors > 0 || c->verify_errors > 0 || c->fsyncdata_errors > 0 )
    {
        /* We finished, but with non-fatal errors. */
        return 1;
    }

    /* We finished successfully. */
    return 0;

} /* nwipe_runmethod */

void calculate_round_size( nwipe_context_t* c )
{
    /* This is where the round size is calculated. round_size is used in the running percentage completion
     * calculation. round size is calculated based on pass_size, pass_count, number of rounds, blanking
     * on/off and verification All/Last/None
     *
     * To hopefully make this calculation more understandable, I have separated the calculations that apply to
     * all methods and processed first then created a switch statement that contains method specific changes if any
     */

    /* Don't change the order of these values as the case statements use their index in the array, New methods
     * don't need to be added to this array unless they have complicated calculations like Ops2 and IS5. If you do
     * add a method, just add it to the bottom of the array_methods array and also to the bottom of the switch
     * statement.
     */
    void* array_methods[] = { &nwipe_zero,
                              &nwipe_ops2,
                              &nwipe_dodshort,
                              &nwipe_dod522022m,
                              &nwipe_gutmann,
                              &nwipe_random,
                              &nwipe_is5enh,
                              NULL };
    int i;

    /* This while loop allows us to effectively create a const that represents a method so we can use a case statement
     * rather than if statements.
     *
     * Using a switch statement looks better than if statments as more methods may get added in the future expanding the
     * list. The code could be condensed as some methods have identical adjustments, however as there are only a few
     * methods I felt it was easier to understand as it is, however this could be changed if necessary.
     */

    /* Initialise, -1 = no additional calculation required */
    int selected_method = -1;

    i = 0;
    while( array_methods[i] != NULL )
    {
        if( nwipe_options.method == array_methods[i] )
        {
            selected_method = i;
        }
        i++;
    }

    /* On exit from the while loop the selected method either equals an index to a method
     * or it equals -1 which means no extra calculations are required that are method specific
     */

    if( nwipe_options.verify == NWIPE_VERIFY_ALL )
    {
        /* We must read back all passes, so double the byte count. */
        c->pass_size *= 2;
    }

    /* Tell the parent the number of rounds that will be run. */
    c->round_count = nwipe_options.rounds;

    /* Set the initial number of bytes that will be written across all rounds.
       c->pass_size includes write AND verification passes if 'verify_all' is selected
       but does not include the final blanking pass or the verify_last option */
    c->round_size = c->pass_size;

    /* Multiple the round_size by the number of rounds (times) the user wants to wipe the drive with this method. */
    c->round_size *= c->round_count;

    /* Now increase size based on whether blanking is enabled and verification */
    if( nwipe_options.noblank == 0 )
    {
        /* Blanking enabled so increase round size */
        c->round_size += c->device_size;

        if( nwipe_options.verify == NWIPE_VERIFY_LAST || nwipe_options.verify == NWIPE_VERIFY_ALL )
        {
            c->round_size += c->device_size;
        }
    }
    else
    {
        /* Blanking not enabled, check for 'Verify_last', increase round size if enabled. */
        if( nwipe_options.verify == NWIPE_VERIFY_LAST )
        {
            c->round_size += c->device_size;
        }
    }

    /* Additional method specific round_size adjustments go in this switch statement */

    switch( selected_method )
    {
        case 0:
            /* NWIPE_ZERO - No additional calculation required
             * ---------- */
            break;

        case 1:
            /* NWIPE_OPS2
             * ---------- */

            /* Required for mandatory 9th and final random pass */
            c->round_size += c->device_size;

            /* Required for selectable 9th and final random verification */
            if( nwipe_options.verify == NWIPE_VERIFY_ALL || nwipe_options.verify == NWIPE_VERIFY_LAST )
            {
                c->round_size += c->device_size;
            }

            /* As no final zero blanking pass is permitted by this standard reduce round size if it's selected */
            if( nwipe_options.noblank == 0 )
            {
                /* Reduce for blanking pass */
                c->round_size -= c->device_size;

                /* Reduce for blanking pass verification */
                if( nwipe_options.verify == NWIPE_VERIFY_ALL || nwipe_options.verify == NWIPE_VERIFY_LAST )
                {
                    c->round_size -= c->device_size;
                }
            }
            else
            {
                if( nwipe_options.verify == NWIPE_VERIFY_LAST )
                {
                    /* If blanking off & verification on reduce round size */
                    c->round_size -= c->device_size;
                }
            }

            break;

        case 2:
            /* DoD Short - No additional calculation required
             * --------- */

            break;

        case 3:
            /* DOD 522022m - No additional calculation required
             * ----------- */

            break;

        case 4:
            /* GutMann - No additional calculation required
             * ------- */

            break;

        case 5:
            /* PRNG (random) - No additional calculation required
             * ------------- */

            break;

        case 6:
            /* NWIPE_IS5ENH
             * ------------ */

            /* This method ALWAYS verifies the 3rd pass so increase by device size,
             * but NOT if VERIFY_ALL has been selected, but first .. */

            /* Reduce as Verify_Last already included previously if blanking was off */
            if( nwipe_options.verify == NWIPE_VERIFY_LAST && nwipe_options.noblank == 1 )
            {
                c->round_size -= c->device_size;
            }

            /* Adjusts for verify on every third pass multiplied by number of rounds */
            if( nwipe_options.verify != NWIPE_VERIFY_ALL )
            {
                c->round_size += ( c->device_size * c->round_count );
            }

            break;

        case -1:
            /* Method not listed so don't do any extra calculations
             * ---------------------------------------------------- */
            break;
    }
}

/* eof */
