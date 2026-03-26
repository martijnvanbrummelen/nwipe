/*
 * se_ata.h: ATA Secure Erase
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: hdparm 9.65 - (c) 2007 Mark Lord (BSD-style license)
 */

#ifndef SE_ATA_H_
#define SE_ATA_H_

#include <linux/types.h> /* __u8, __u16, __u32, __u64 */
#include <stdbool.h>

typedef enum {
    NWIPE_SE_ATA_STATE_UNKNOWN = 0,
    NWIPE_SE_ATA_STATE_FROZEN,
    NWIPE_SE_ATA_STATE_IDLE,
    NWIPE_SE_ATA_STATE_IN_PROGRESS,
    NWIPE_SE_ATA_STATE_SUCCESS,
    NWIPE_SE_ATA_STATE_FAILURE,
} nwipe_se_ata_state_e;

typedef enum {
    NWIPE_SE_ATA_SANACT_UNKNOWN = 0,
    NWIPE_SE_ATA_SANACT_STATUS,
    NWIPE_SE_ATA_SANACT_CRYPTO_SCRAMBLE,
    NWIPE_SE_ATA_SANACT_BLOCK_ERASE,
    NWIPE_SE_ATA_SANACT_OVERWRITE,
    NWIPE_SE_ATA_SANACT_FREEZE_LOCK,
    NWIPE_SE_ATA_SANACT_ANTIFREEZE_LOCK,
    NWIPE_SE_ATA_SANACT_EXIT_FAILURE /* NWIPE_SE_ATA_SANACT_STATUS + CSOF */
} nwipe_se_ata_sanact_e;

typedef struct
{
    char* device_path; /* /dev/sda */
    int fd; /* -1 when closed */

    /* Error message buffer for GUI */
    char error_msg[256];

    /* Supported erasing methods */
    int cap_sanitize; /* feature set */
    int cap_crypto_erase; /* Crypto Scramble */
    int cap_block_erase; /* Block Erase */
    int cap_overwrite; /* Overwrite */
    int cap_caps_valid; /* 0 = No, 1 = Yes */

    /* Last or current running operation */
    nwipe_se_ata_state_e state;
    nwipe_se_ata_sanact_e sanact;
    int progress_pct; /* 0-100 */
    __u16 progress_raw; /* 0xFFFF */

    /* Options (set before nwipe_se_ata_sanitize) */
    nwipe_se_ata_sanact_e planned_sanact;
    int destructive_sanact; /* 0 = No, 1 = Yes */
    __u8 owpass; /* 0-15 overwrite pass count */
    __u32 ovrpat; /* 32-bit overwrite pattern */
} nwipe_se_ata_ctx;

int nwipe_se_ata_init( const char* device_name, nwipe_se_ata_ctx* san );
int nwipe_se_ata_open( nwipe_se_ata_ctx* san );
void nwipe_se_ata_close( nwipe_se_ata_ctx* san );
void nwipe_se_ata_destroy( nwipe_se_ata_ctx* san );
int nwipe_se_ata_sancap( nwipe_se_ata_ctx* san );
int nwipe_se_ata_poll( nwipe_se_ata_ctx* san );
int nwipe_se_ata_sanitize( nwipe_se_ata_ctx* san );

#endif /* SE_ATA_H_ */
