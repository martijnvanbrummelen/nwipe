/*
 * se_ata_gui.h: ATA Secure Erase (GUI)
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: hdparm 9.65 - (c) 2007 Mark Lord (BSD-style license)
 */

#ifndef SE_ATA_GUI_H_
#define SE_ATA_GUI_H_

#include "context.h" /* nwipe_context_t */
#include "se_ata.h" /* nwipe_se_ata_ctx */

void nwipe_gui_se_ata_sanitize( nwipe_context_t* ctx, nwipe_se_ata_ctx* san );

#endif /* SE_ATA_GUI_H_ */
