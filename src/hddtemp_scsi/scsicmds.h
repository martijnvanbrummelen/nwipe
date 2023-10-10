/*
 * Copyright (C) 2002  Emmanuel VARAGNAT <hddtemp@guzu.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#ifndef SCSICMDS_H_
#define SCSICMDS_H_

#define SUPPORT_LOG_PAGES	0x00
#define TEMPERATURE_PAGE	0x0d
#define EXCEPTIONS_CONTROL_PAGE 0x1c
#define LOGPAGEHDRSIZE  	4

int scsi_SG_IO(int device, unsigned char *cdb, int cdb_len, unsigned char *buffer, int buffer_len, unsigned char *sense, unsigned char sense_len, int dxfer_direction);
int scsi_inquiry(int device, unsigned char *buffer);
int scsi_modesense(int device, unsigned char pagenum, unsigned char *buffer, int buffer_len);
int scsi_modeselect(int device, char *buffer);
int scsi_logsense(int device, int pagenum, unsigned char *buffer, int buffer_len);
int scsi_smartsupport(int device);
int scsi_smartDEXCPTdisable(int device);

#endif
