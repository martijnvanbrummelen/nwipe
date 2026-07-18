/*
 * se_nvme_gui.h: NVMe Secure Erase (GUI)
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: nvme-cli 2.16 - (c) 2015 NVMe-CLI Authors (GPL v2.0)
 */
#ifdef HAVE_CONFIG_H
#include <config.h> /* HAVE_LIBNVME */
#endif

#ifndef SE_NVME_GUI_H_
#define SE_NVME_GUI_H_
#ifdef HAVE_LIBNVME

#include "context.h" /* nwipe_context_t */
#include "se_nvme.h" /* nwipe_se_nvme_ctx */

void nwipe_gui_se_nvme_sanitize( nwipe_context_t* ctx, nwipe_se_nvme_ctx* san );

#endif /* HAVE_LIBNVME */
#endif /* SE_NVME_GUI_H_ */
