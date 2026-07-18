/*
 * se_nvme.h: NVMe Secure Erase
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: nvme-cli 2.16 - (c) 2015 NVMe-CLI Authors (GPL v2.0)
 */
#ifdef HAVE_CONFIG_H
#include <config.h> /* HAVE_LIBNVME */
#endif

#ifndef SE_NVME_H_
#define SE_NVME_H_
#ifdef HAVE_LIBNVME

#include <linux/types.h> /* __u8, __u16, __u32 */
#include <stdbool.h>
#include <libnvme.h>

/*
 * While the device internal state machine has an "Idle" state,
 * it is not exposed over the wire. Instead, devices that are idle
 * report either the "Never Sanitized" or "Sanitize Success" states.
 * A successful sanitize can therefore only be derived when a prior
 * sanitize command was accepted or an "In Progress" state was seen
 * which then transitioned to a "Sanitize Success" state afterwards.
 */
typedef enum {
    NWIPE_SE_NVME_STATE_UNKNOWN = 0,
    NWIPE_SE_NVME_STATE_NEVER_SANITIZED,
    NWIPE_SE_NVME_STATE_IN_PROGRESS,
    NWIPE_SE_NVME_STATE_SUCCESS,
    NWIPE_SE_NVME_STATE_FAILURE,
} nwipe_se_nvme_state_e;

typedef struct
{
    nvme_root_t root; /* Root of topology tree */
} nwipe_se_nvme_topo;

typedef struct
{
    /* Controller information */
    char* ctrl_path; /* /dev/nvme0 */
    int fd; /* -1 when closed */

    /* Namespaces belonging to this controller */
    char** ns_names; /* Basenames; NULL-terminated */
    int ns_count;

    /* Error message buffer for GUI */
    char error_msg[256];

    /* Supported erasing methods */
    int cap_crypto_erase; /* Crypto Erase */
    int cap_block_erase; /* Block Erase */
    int cap_overwrite; /* Overwrite */
    int cap_caps_valid; /* 0 = No, 1 = Yes */

    /* Last or current running operation */
    nwipe_se_nvme_state_e state;
    __u8 state_raw; /* 0xFF */
    enum nvme_sanitize_sanact sanact;
    int progress_pct; /* 0-100 */
    __u16 progress_raw; /* 0xFFFF */

    /* Estimated times in seconds (0 = unavailable) */
    __u32 est_overwrite;
    __u32 est_block_erase;
    __u32 est_crypto_erase;

    /* Options (set before nwipe_se_nvme_sanitize) */
    enum nvme_sanitize_sanact planned_sanact;
    int destructive_sanact; /* 0 = No, 1 = Yes */
    __u8 owpass; /* 0-15 (0-based, sent directly) */
    bool oipbp; /* invert pattern between passes */
    __u32 ovrpat; /* 32-bit overwrite pattern */
    bool nodas; /* no deallocate after sanitize */
    bool ause; /* allow unrestricted sanitize exit */
    bool emvs; /* enter media verification state */
} nwipe_se_nvme_ctx;

int nwipe_se_nvme_topo_init( nwipe_se_nvme_topo* topo );
void nwipe_se_nvme_topo_destroy( nwipe_se_nvme_topo* topo );

int nwipe_se_nvme_init( const char* device_name, nwipe_se_nvme_ctx* san, nwipe_se_nvme_topo* topo );
int nwipe_se_nvme_open( nwipe_se_nvme_ctx* san );
void nwipe_se_nvme_close( nwipe_se_nvme_ctx* san );
void nwipe_se_nvme_destroy( nwipe_se_nvme_ctx* san );
int nwipe_se_nvme_sancap( nwipe_se_nvme_ctx* san );
int nwipe_se_nvme_poll( nwipe_se_nvme_ctx* san );
int nwipe_se_nvme_sanitize( nwipe_se_nvme_ctx* san );

#endif /* HAVE_LIBNVME */
#endif /* SE_NVME_H_ */
