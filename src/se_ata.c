/*
 * se_ata.c: ATA Secure Erase
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: hdparm 9.65 - (c) 2007 Mark Lord (BSD-style license)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>

#include "nwipe.h"
#include "context.h"
#include "logging.h"
#include "se_ata.h"

#ifndef SG_DXFER_NONE
#define SG_DXFER_NONE -1
#define SG_DXFER_TO_DEV -2
#define SG_DXFER_FROM_DEV -3
#endif

#define SG_READ 0
#define SG_WRITE 1

#define SG_PIO 0
#define SG_DMA 1

#define SG_CHECK_CONDITION 0x02
#define SG_DRIVER_SENSE 0x08

#define SG_ATA_16 0x85
#define SG_ATA_16_LEN 16

#define SG_ATA_LBA48 1
#define SG_ATA_PROTO_NON_DATA ( 3 << 1 )
#define SG_ATA_PROTO_PIO_IN ( 4 << 1 )
#define SG_ATA_PROTO_PIO_OUT ( 5 << 1 )
#define SG_ATA_PROTO_DMA ( 6 << 1 )

enum {
    ATA_OP_DSM = 0x06,
    ATA_OP_READ_DMA_EXT = 0x25,
    ATA_OP_READ_FPDMA = 0x60,
    ATA_OP_WRITE_DMA_EXT = 0x35,
    ATA_OP_WRITE_FPDMA = 0x61,
    ATA_OP_PIDENTIFY = 0xa1,
    ATA_OP_SANITIZE = 0xb4,
    ATA_OP_READ_DMA = 0xc8,
    ATA_OP_WRITE_DMA = 0xca,
    ATA_OP_IDENTIFY = 0xec,
};

enum {
    ATA_USING_LBA = ( 1 << 6 ),
    ATA_STAT_DRQ = ( 1 << 3 ),
    ATA_STAT_ERR = ( 1 << 0 ),
};

enum {
    SG_CDB2_TLEN_NSECT = 2 << 0,
    SG_CDB2_TLEN_SECTORS = 1 << 2,
    SG_CDB2_TDIR_TO_DEV = 0 << 3,
    SG_CDB2_TDIR_FROM_DEV = 1 << 3,
    SG_CDB2_CHECK_COND = 1 << 5,
};

enum {
    TASKFILE_CMD_REQ_NODATA = 0,
    TASKFILE_CMD_REQ_IN = 2,
    TASKFILE_CMD_REQ_OUT = 3,
    TASKFILE_CMD_REQ_RAW_OUT = 4,
    TASKFILE_DPHASE_NONE = 0,
    TASKFILE_DPHASE_PIO_IN = 1,
    TASKFILE_DPHASE_PIO_OUT = 4,
};

struct ata_lba_regs
{
    __u8 feat;
    __u8 nsect;
    __u8 lbal;
    __u8 lbam;
    __u8 lbah;
}; /* ata_lba_regs */

struct ata_tf
{
    __u8 dev;
    __u8 command;
    __u8 error;
    __u8 status;
    __u8 is_lba48;
    struct ata_lba_regs lob;
    struct ata_lba_regs hob;
}; /* ata_tf */

struct taskfile_regs
{
    __u8 data;
    __u8 feat;
    __u8 nsect;
    __u8 lbal;
    __u8 lbam;
    __u8 lbah;
    __u8 dev;
    __u8 command;
}; /* taskfile_regs */

union reg_flags
{
    unsigned all : 16;
    struct
    {
        union
        {
            unsigned lob_all : 8;
            struct
            {
                unsigned data : 1;
                unsigned feat : 1;
                unsigned lbal : 1;
                unsigned nsect : 1;
                unsigned lbam : 1;
                unsigned lbah : 1;
                unsigned dev : 1;
                unsigned command : 1;
            } lob;
        };
        union
        {
            unsigned hob_all : 8;
            struct
            {
                unsigned data : 1;
                unsigned feat : 1;
                unsigned lbal : 1;
                unsigned nsect : 1;
                unsigned lbam : 1;
                unsigned lbah : 1;
                unsigned dev : 1;
                unsigned command : 1;
            } hob;
        };
    } bits;
} __attribute__( ( packed ) ); /* reg_flags */

struct hdio_taskfile
{
    struct taskfile_regs lob;
    struct taskfile_regs hob;
    union reg_flags oflags;
    union reg_flags iflags;
    int dphase;
    int cmd_req;
    unsigned long obytes;
    unsigned long ibytes;
    __u16 data[0];
}; /* hdio_taskfile */

struct scsi_sg_io_hdr
{
    int interface_id;
    int dxfer_direction;
    unsigned char cmd_len;
    unsigned char mx_sb_len;
    unsigned short iovec_count;
    unsigned int dxfer_len;
    void* dxferp;
    unsigned char* cmdp;
    void* sbp;
    unsigned int timeout;
    unsigned int flags;
    int pack_id;
    void* usr_ptr;
    unsigned char status;
    unsigned char masked_status;
    unsigned char msg_status;
    unsigned char sb_len_wr;
    unsigned short host_status;
    unsigned short driver_status;
    int resid;
    unsigned int duration;
    unsigned int info;
}; /* scsi_sg_io_hdr */

static const unsigned int default_timeout_secs = 15;

static void dump_bytes( const char* f, const char* prefix, unsigned char* p, int len )
{
    char line[128];
    int pos;

    for( int row = 0; row < len; row += 8 )
    {
        int end = ( row + 7 < len - 1 ) ? row + 7 : len - 1;
        pos = snprintf( line, sizeof( line ), "%s[%3d..%3d]:", prefix ? prefix : "", row, end );
        for( int col = 0; col < 8 && ( row + col ) < len; col++ )
        {
            if( pos < (int) sizeof( line ) )
                pos += snprintf( line + pos, sizeof( line ) - (size_t) pos, " %02x", p[row + col] );
        }
        nwipe_log( NWIPE_LOG_DEBUG, "%s: %s", f, line );
    }
} /* dump_bytes */

static inline int needs_lba48( __u8 ata_op, __u64 lba, unsigned int nsect )
{
    switch( ata_op )
    {
        case ATA_OP_SANITIZE:
            return 1;
        case ATA_OP_IDENTIFY:
        case ATA_OP_PIDENTIFY:
            return 0;
    }
    if( lba >= ( ( (__u64) 1 << 28 ) - 1 ) )
        return 1;
    if( nsect )
    {
        if( nsect > 0xff )
            return 1;
        if( ( lba + nsect - 1 ) >= ( ( (__u64) 1 << 28 ) - 1 ) )
            return 1;
    }
    return 0;
} /* needs_lba48 */

static inline int is_dma( __u8 ata_op )
{
    switch( ata_op )
    {
        case ATA_OP_DSM:
        case ATA_OP_READ_DMA_EXT:
        case ATA_OP_READ_FPDMA:
        case ATA_OP_WRITE_DMA_EXT:
        case ATA_OP_WRITE_FPDMA:
        case ATA_OP_READ_DMA:
        case ATA_OP_WRITE_DMA:
            return SG_DMA;
        default:
            return SG_PIO;
    }
} /* is_dma */

static void tf_init( struct ata_tf* tf, __u8 ata_op, __u64 lba, unsigned int nsect )
{
    memset( tf, 0, sizeof( *tf ) );

    tf->command = ata_op;
    tf->dev = ATA_USING_LBA;
    tf->lob.lbal = (__u8) lba;
    tf->lob.lbam = (__u8) ( lba >> 8 );
    tf->lob.lbah = (__u8) ( lba >> 16 );
    tf->lob.nsect = (__u8) nsect;

    if( needs_lba48( ata_op, lba, nsect ) )
    {
        tf->is_lba48 = 1;
        tf->hob.nsect = (__u8) ( nsect >> 8 );
        tf->hob.lbal = (__u8) ( lba >> 24 );
        tf->hob.lbam = (__u8) ( lba >> 32 );
        tf->hob.lbah = (__u8) ( lba >> 40 );
    }
    else
    {
        tf->dev |= (__u8) ( ( lba >> 24 ) & 0x0f );
    }
} /* tf_init */

static __u64 tf_to_lba( struct ata_tf* tf )
{
    __u32 lba24, lbah;
    __u64 lba64;

    lba24 = ( tf->lob.lbah << 16 ) | ( tf->lob.lbam << 8 ) | ( tf->lob.lbal );
    if( tf->is_lba48 )
        lbah = ( tf->hob.lbah << 16 ) | ( tf->hob.lbam << 8 ) | ( tf->hob.lbal );
    else
        lbah = ( tf->dev & 0x0f );
    lba64 = ( ( (__u64) lbah ) << 24 ) | (__u64) lba24;

    return lba64;
} /* tf_to_lba */

static int
sg16( int fd, int rw, int dma, struct ata_tf* tf, void* data, unsigned int data_bytes, unsigned int timeout_secs )
{
    unsigned char cdb[SG_ATA_16_LEN];
    unsigned char sb[32], *desc;
    struct scsi_sg_io_hdr io_hdr;

    memset( cdb, 0, sizeof( cdb ) );
    memset( sb, 0, sizeof( sb ) );
    memset( &io_hdr, 0, sizeof( io_hdr ) );

    if( data && data_bytes && !rw )
        memset( data, 0, data_bytes );

    if( dma )
        cdb[1] = data ? SG_ATA_PROTO_DMA : SG_ATA_PROTO_NON_DATA;
    else
        cdb[1] = data ? ( rw ? SG_ATA_PROTO_PIO_OUT : SG_ATA_PROTO_PIO_IN ) : SG_ATA_PROTO_NON_DATA;

    if( data )
    {
        cdb[2] |= SG_CDB2_TLEN_NSECT | SG_CDB2_TLEN_SECTORS;
        cdb[2] |= rw ? SG_CDB2_TDIR_TO_DEV : SG_CDB2_TDIR_FROM_DEV;
    }
    else
    {
        cdb[2] = SG_CDB2_CHECK_COND;
    }

    cdb[0] = SG_ATA_16;
    cdb[4] = tf->lob.feat;
    cdb[6] = tf->lob.nsect;
    cdb[8] = tf->lob.lbal;
    cdb[10] = tf->lob.lbam;
    cdb[12] = tf->lob.lbah;
    cdb[13] = tf->dev;
    cdb[14] = tf->command;
    if( tf->is_lba48 )
    {
        cdb[1] |= SG_ATA_LBA48;
        cdb[3] = tf->hob.feat;
        cdb[5] = tf->hob.nsect;
        cdb[7] = tf->hob.lbal;
        cdb[9] = tf->hob.lbam;
        cdb[11] = tf->hob.lbah;
    }

    io_hdr.cmd_len = SG_ATA_16_LEN;
    io_hdr.interface_id = 'S';
    io_hdr.mx_sb_len = sizeof( sb );
    io_hdr.dxfer_direction = data ? ( rw ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV ) : SG_DXFER_NONE;
    io_hdr.dxfer_len = data ? data_bytes : 0;
    io_hdr.dxferp = data;
    io_hdr.cmdp = cdb;
    io_hdr.sbp = sb;
    io_hdr.pack_id = (int) tf_to_lba( tf );
    io_hdr.timeout = ( timeout_secs ? timeout_secs : default_timeout_secs ) * 1000;

    dump_bytes( __FUNCTION__, "cdb", cdb, (int) sizeof( cdb ) );
    if( rw && data )
        dump_bytes( __FUNCTION__, "outgoing_data", data, (int) data_bytes );

    if( ioctl( fd, SG_IO, &io_hdr ) == -1 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: ioctl() failed: %s (%d)", __FUNCTION__, strerror( errno ), errno );
        /* errno from ioctl */
        return -1;
    }

    nwipe_log( NWIPE_LOG_DEBUG,
               "%s: ATA_%u status=0x%x, host_status=0x%x, driver_status=0x%x",
               __FUNCTION__,
               io_hdr.cmd_len,
               io_hdr.status,
               io_hdr.host_status,
               io_hdr.driver_status );

    if( io_hdr.status && io_hdr.status != SG_CHECK_CONDITION )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: bad status: 0x%x", __FUNCTION__, io_hdr.status );
        errno = EBADE;
        return -1;
    }

    if( io_hdr.host_status )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: bad host status: 0x%x", __FUNCTION__, io_hdr.host_status );
        errno = EBADE;
        return -1;
    }

    dump_bytes( __FUNCTION__, "sb", sb, sizeof( sb ) );
    if( !rw && data )
        dump_bytes( __FUNCTION__, "incoming_data", data, (int) data_bytes );

    if( io_hdr.driver_status && ( io_hdr.driver_status != SG_DRIVER_SENSE ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: bad driver status: 0x%x", __FUNCTION__, io_hdr.driver_status );
        errno = EBADE;
        return -1;
    }

    if( io_hdr.driver_status == 0 && io_hdr.status == 0 )
    {
        tf->status = 0;
        tf->error = 0;
        return 0;
    }

    desc = sb + 8;

    if( sb[0] != 0x72 || sb[7] < 14 || desc[0] != 0x09 || desc[1] < 0x0c )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: bad or missing sense data (behind RAID/SAS controller?)", __FUNCTION__ );
        errno = EBADE;
        return -1;
    }

    unsigned int len = desc[1] + 2, maxlen = sizeof( sb ) - 8 - 2;
    if( len > maxlen )
        len = maxlen;
    dump_bytes( __FUNCTION__, "desc[]", desc, (int) len );

    tf->is_lba48 = desc[2] & 1;
    tf->error = desc[3];
    tf->lob.nsect = desc[5];
    tf->lob.lbal = desc[7];
    tf->lob.lbam = desc[9];
    tf->lob.lbah = desc[11];
    tf->dev = desc[12];
    tf->status = desc[13];
    tf->hob.feat = 0;

    if( tf->is_lba48 )
    {
        tf->hob.nsect = desc[4];
        tf->hob.lbal = desc[6];
        tf->hob.lbam = desc[8];
        tf->hob.lbah = desc[10];
    }
    else
    {
        tf->hob.nsect = 0;
        tf->hob.lbal = 0;
        tf->hob.lbam = 0;
        tf->hob.lbah = 0;
    }

    nwipe_log( NWIPE_LOG_DEBUG,
               "%s: ATA_%u stat=%02x err=%02x nsect=%02x lbal=%02x lbam=%02x lbah=%02x dev=%02x",
               __FUNCTION__,
               io_hdr.cmd_len,
               tf->status,
               tf->error,
               tf->lob.nsect,
               tf->lob.lbal,
               tf->lob.lbam,
               tf->lob.lbah,
               tf->dev );

    if( tf->status & ( ATA_STAT_ERR | ATA_STAT_DRQ ) )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "%s: I/O error, ata_op=0x%02x ata_status=0x%02x ata_error=0x%02x",
                   __FUNCTION__,
                   tf->command,
                   tf->status,
                   tf->error );
        errno = EIO;
        return -1;
    }

    return 0;
} /* sg16 */

static int do_taskfile_cmd( int fd, struct hdio_taskfile* r, unsigned int timeout_secs )
{
    struct ata_tf tf;
    void* data = NULL;
    unsigned int data_bytes = 0;
    int rw = SG_READ;
    int rc;

    tf_init( &tf, 0, 0, 0 );

    if( r->oflags.bits.lob.feat )
        tf.lob.feat = r->lob.feat;
    if( r->oflags.bits.lob.lbal )
        tf.lob.lbal = r->lob.lbal;
    if( r->oflags.bits.lob.nsect )
        tf.lob.nsect = r->lob.nsect;
    if( r->oflags.bits.lob.lbam )
        tf.lob.lbam = r->lob.lbam;
    if( r->oflags.bits.lob.lbah )
        tf.lob.lbah = r->lob.lbah;
    if( r->oflags.bits.lob.dev )
        tf.dev = r->lob.dev;
    if( r->oflags.bits.lob.command )
        tf.command = r->lob.command;

    if( needs_lba48( tf.command, 0, 0 ) || r->oflags.bits.hob_all || r->iflags.bits.hob_all )
    {
        tf.is_lba48 = 1;
        if( r->oflags.bits.hob.feat )
            tf.hob.feat = r->hob.feat;
        if( r->oflags.bits.hob.lbal )
            tf.hob.lbal = r->hob.lbal;
        if( r->oflags.bits.hob.nsect )
            tf.hob.nsect = r->hob.nsect;
        if( r->oflags.bits.hob.lbam )
            tf.hob.lbam = r->hob.lbam;
        if( r->oflags.bits.hob.lbah )
            tf.hob.lbah = r->hob.lbah;
    }

    switch( r->cmd_req )
    {
        case TASKFILE_CMD_REQ_OUT:
        case TASKFILE_CMD_REQ_RAW_OUT:
            data_bytes = (unsigned int) r->obytes;
            data = r->data;
            rw = SG_WRITE;
            break;
        case TASKFILE_CMD_REQ_IN:
            data_bytes = (unsigned int) r->ibytes;
            data = r->data;
            break;
    }

    rc = sg16( fd, rw, is_dma( tf.command ), &tf, data, data_bytes, timeout_secs );

    if( rc == 0 || errno == EIO )
    {
        if( r->iflags.bits.lob.feat )
            r->lob.feat = tf.error;
        if( r->iflags.bits.lob.lbal )
            r->lob.lbal = tf.lob.lbal;
        if( r->iflags.bits.lob.nsect )
            r->lob.nsect = tf.lob.nsect;
        if( r->iflags.bits.lob.lbam )
            r->lob.lbam = tf.lob.lbam;
        if( r->iflags.bits.lob.lbah )
            r->lob.lbah = tf.lob.lbah;
        if( r->iflags.bits.lob.dev )
            r->lob.dev = tf.dev;
        if( r->iflags.bits.lob.command )
            r->lob.command = tf.status;
        if( r->iflags.bits.hob.feat )
            r->hob.feat = tf.hob.feat;
        if( r->iflags.bits.hob.lbal )
            r->hob.lbal = tf.hob.lbal;
        if( r->iflags.bits.hob.nsect )
            r->hob.nsect = tf.hob.nsect;
        if( r->iflags.bits.hob.lbam )
            r->hob.lbam = tf.hob.lbam;
        if( r->iflags.bits.hob.lbah )
            r->hob.lbah = tf.hob.lbah;
    }

    return rc;
} /* do_taskfile_cmd */

enum {
    SANITIZE_STATUS_EXT = 0x0000,
    SANITIZE_CRYPTO_SCRAMBLE_EXT = 0x0011,
    SANITIZE_BLOCK_ERASE_EXT = 0x0012,
    SANITIZE_OVERWRITE_EXT = 0x0014,
    SANITIZE_FREEZE_LOCK_EXT = 0x0020,
    SANITIZE_ANTIFREEZE_LOCK_EXT = 0x0040,
};

enum {
    SANITIZE_FREEZE_LOCK_KEY = 0x46724C6B, /* "FrLk" */
    SANITIZE_ANTIFREEZE_LOCK_KEY = 0x416E7469, /* "Anti" */
    SANITIZE_CRYPTO_SCRAMBLE_KEY = 0x43727970, /* "Cryp" */
    SANITIZE_BLOCK_ERASE_KEY = 0x426B4572, /* "BkEr" */
    SANITIZE_OVERWRITE_KEY = 0x00004F57, /* "OW"   */
};

enum {
    SANITIZE_FLAG_OPERATION_SUCCEEDED = ( 1 << 7 ),
    SANITIZE_FLAG_OPERATION_IN_PROGRESS = ( 1 << 6 ),
    SANITIZE_FLAG_DEVICE_IN_FROZEN = ( 1 << 5 ),
    SANITIZE_FLAG_ANTIFREEZE_BIT = ( 1 << 4 ),
};

static __u16* ata_identify( int fd )
{
    __u8 buf[4 + 512];
    struct ata_tf tf;
    int i;

    memset( buf, 0, sizeof( buf ) );
    tf_init( &tf, ATA_OP_IDENTIFY, 0, 0 );
    tf.lob.nsect = 1;

    if( sg16( fd, SG_READ, SG_PIO, &tf, buf + 4, 512, 15 ) )
    {
        /* Try ATAPI IDENTIFY instead */
        tf_init( &tf, ATA_OP_PIDENTIFY, 0, 0 );
        tf.lob.nsect = 1;
        if( sg16( fd, SG_READ, SG_PIO, &tf, buf + 4, 512, 15 ) )
            return NULL;
    }

    __u16* id = malloc( 256 * sizeof( __u16 ) );
    if( !id )
        return NULL;

    /* byte-swap LE identify data to host order */
    __u8* raw = buf + 4;
    for( i = 0; i < 256; i++ )
    {
        id[i] = raw[i * 2] | ( (__u16) raw[i * 2 + 1] << 8 );
    }
    return id;
} /* ata_identify */

static int ata_sanitize_taskfile( int fd, __u16 feature, __u64 lba, __u8 nsect, struct hdio_taskfile* r_out )
{
    struct hdio_taskfile r;
    memset( &r, 0, sizeof( r ) );

    r.cmd_req = TASKFILE_CMD_REQ_NODATA;
    r.dphase = TASKFILE_DPHASE_NONE;

    r.oflags.bits.lob.command = 1;
    r.oflags.bits.lob.feat = 1;
    r.oflags.bits.lob.lbal = 1;
    r.oflags.bits.lob.lbam = 1;
    r.oflags.bits.lob.lbah = 1;
    r.oflags.bits.hob.lbal = 1;
    r.oflags.bits.hob.lbam = 1;
    r.oflags.bits.hob.lbah = 1;

    r.lob.command = ATA_OP_SANITIZE;
    r.lob.feat = (__u8) feature;
    r.lob.lbal = (__u8) lba;
    r.lob.lbam = (__u8) ( lba >> 8 );
    r.lob.lbah = (__u8) ( lba >> 16 );
    r.hob.lbal = (__u8) ( lba >> 24 );
    r.hob.lbam = (__u8) ( lba >> 32 );
    r.hob.lbah = (__u8) ( lba >> 40 );

    r.iflags.bits.lob.lbal = 1;
    r.iflags.bits.lob.lbam = 1;
    r.iflags.bits.hob.nsect = 1;

    if( feature == SANITIZE_OVERWRITE_EXT )
    {
        r.oflags.bits.lob.nsect = 1;
        r.lob.nsect = nsect;
    }

    if( do_taskfile_cmd( fd, &r, 10 ) )
    {
        if( r_out )
            memcpy( r_out, &r, sizeof( r ) );
        return -1;
    }

    if( r_out )
        memcpy( r_out, &r, sizeof( r ) );

    return 0;
} /* ata_sanitize_taskfile */

static const char* lbal_to_error_str( __u8 lbal )
{
    switch( lbal )
    {
        case 0:
            return "Reason not reported";
        case 1:
            return "Last sanitize command completed unsuccessfully";
        case 2:
            return "Unsupported command";
        case 3:
            return "Device in frozen state";
        case 4:
            return "Antifreeze lock enabled";
        default:
            return "Unknown error reason";
    }
} /* lbal_to_error_str */

/*
 * Initializes given pre-allocated nwipe_se_ata_ctx pointer.
 * nwipe_se_ata_destroy() should be called when no longer needed.
 */
int nwipe_se_ata_init( const char* device_name, nwipe_se_ata_ctx* san )
{
    memset( san, 0, sizeof( *san ) );
    san->fd = -1;

    san->device_path = strdup( device_name );
    if( !san->device_path )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: strdup() failed", __FUNCTION__, device_name );
        return -1;
    }
    return 0;
} /* nwipe_se_ata_init */

/* Opens the ATA device */
int nwipe_se_ata_open( nwipe_se_ata_ctx* san )
{
    memset( san->error_msg, 0, sizeof( san->error_msg ) ); /* Used in GUI */

    san->fd = open( san->device_path, O_RDONLY | O_NONBLOCK );
    if( san->fd < 0 )
    {
        int eno = errno;
        snprintf( san->error_msg, sizeof( san->error_msg ), "%s (errno=%d)", strerror( eno ), eno );
        nwipe_log( NWIPE_LOG_ERROR,
                   "%s: %s: Failed to open device: %s (%d)",
                   __FUNCTION__,
                   san->device_path,
                   strerror( eno ),
                   eno );
        return -1;
    }

    return 0;
} /* nwipe_se_ata_open */

/* Closes the ATA device */
void nwipe_se_ata_close( nwipe_se_ata_ctx* san )
{
    if( san->fd >= 0 )
    {
        close( san->fd );
        san->fd = -1;
    }
} /* nwipe_se_ata_close */

/*
 * Frees internal allocations of the given nwipe_se_ata_ctx.
 * Do not use the context anymore after this function was called.
 */
void nwipe_se_ata_destroy( nwipe_se_ata_ctx* san )
{
    if( san->device_path )
    {
        free( san->device_path );
        san->device_path = NULL;
    }
} /* nwipe_se_ata_destroy */

/*
 * Probes for ATA Sanitize capabilities using IDENTIFY.
 * Sets san->cap_caps_valid to 1 if san_cap_* values are useable.
 * Returns -1 only on failure sending the IDENTIFY command itself.
 */
int nwipe_se_ata_sancap( nwipe_se_ata_ctx* san )
{
    if( san->fd < 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: FD is not open", __FUNCTION__, san->device_path );
        return -1;
    }

    __u16* id = ata_identify( san->fd );
    if( !id )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: ata_identify() failed", __FUNCTION__, san->device_path );
        san->cap_sanitize = 0;
        san->cap_crypto_erase = 0;
        san->cap_block_erase = 0;
        san->cap_overwrite = 0;
        san->cap_caps_valid = 0;
        return -1;
    }

    san->cap_sanitize = !!( id[59] & 0x1000 );
    san->cap_crypto_erase = !!( id[59] & 0x2000 );
    san->cap_overwrite = !!( id[59] & 0x4000 );
    san->cap_block_erase = !!( id[59] & 0x8000 );
    san->cap_caps_valid = 1;

    free( id );

    return 0;
} /* nwipe_se_ata_sancap */

/*
 * Polls the sanitize status using SANITIZE_STATUS_EXT.
 * Updates san->state and san->progress_* variables of the context.
 * Avoid hammering of device with calls in a tight loop, ensure delays.
 * Success returns 0; errors -1, logs and populates san->error_msg buffer.
 */
int nwipe_se_ata_poll( nwipe_se_ata_ctx* san )
{
    memset( san->error_msg, 0, sizeof( san->error_msg ) ); /* Used in GUI */

    if( san->fd < 0 )
    {
        snprintf( san->error_msg, sizeof( san->error_msg ), "FD is not open" );
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: FD is not open", __FUNCTION__, san->device_path );
        return -1;
    }

    struct hdio_taskfile r;
    memset( &r, 0, sizeof( r ) );

    if( ata_sanitize_taskfile( san->fd, SANITIZE_STATUS_EXT, 0, 0, &r ) != 0 )
    {
        int eno = errno;

        snprintf( san->error_msg,
                  sizeof( san->error_msg ),
                  "%s (errno=%d, device reason: %s)",
                  strerror( eno ),
                  eno,
                  lbal_to_error_str( r.lob.lbal ) );

        nwipe_log( NWIPE_LOG_ERROR,
                   "%s: %s: SANITIZE_STATUS_EXT failed: %s (errno=%d, device reason: %s)",
                   __FUNCTION__,
                   san->device_path,
                   strerror( eno ),
                   eno,
                   lbal_to_error_str( r.lob.lbal ) );

        return -1;
    }

    __u8 nsect_hob = r.hob.nsect;

    if( nsect_hob & SANITIZE_FLAG_DEVICE_IN_FROZEN )
    {
        san->state = NWIPE_SE_ATA_STATE_FROZEN;
        san->progress_raw = 0;
        san->progress_pct = 0;
    }
    else if( nsect_hob & SANITIZE_FLAG_OPERATION_IN_PROGRESS )
    {
        san->state = NWIPE_SE_ATA_STATE_IN_PROGRESS;
        san->progress_raw = ( r.lob.lbam << 8 ) | r.lob.lbal;
        san->progress_pct = ( (int) san->progress_raw * 100 ) / UINT16_MAX;
        if( san->progress_pct > 100 )
            san->progress_pct = 100;
    }
    else if( nsect_hob & SANITIZE_FLAG_OPERATION_SUCCEEDED )
    {
        san->state = NWIPE_SE_ATA_STATE_SUCCESS;
        san->progress_raw = UINT16_MAX;
        san->progress_pct = 100;
    }
    else
    {
        san->state = NWIPE_SE_ATA_STATE_IDLE;
        san->progress_raw = 0;
        san->progress_pct = 0;
    }

    return 0;
} /* nwipe_se_ata_poll */

/*
 * Run the san->planned_sanact sanitize operation.
 * Sends command to device and returns 0 on success.
 * Operation itself runs on the device (as non-blocking).
 * Errors return -1, logs and populates san->error_msg buffer.
 */
int nwipe_se_ata_sanitize( nwipe_se_ata_ctx* san )
{
    __u16 feature;
    __u64 lba = 0;
    __u8 nsect = 0;

    memset( san->error_msg, 0, sizeof( san->error_msg ) ); /* Used in GUI */

    if( san->fd < 0 )
    {
        snprintf( san->error_msg, sizeof( san->error_msg ), "FD is not open" );
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: FD is not open", __FUNCTION__, san->device_path );
        return -1;
    }

    switch( san->planned_sanact )
    {
        case NWIPE_SE_ATA_SANACT_CRYPTO_SCRAMBLE:
            feature = SANITIZE_CRYPTO_SCRAMBLE_EXT;
            lba = SANITIZE_CRYPTO_SCRAMBLE_KEY;
            break;

        case NWIPE_SE_ATA_SANACT_BLOCK_ERASE:
            feature = SANITIZE_BLOCK_ERASE_EXT;
            lba = SANITIZE_BLOCK_ERASE_KEY;
            break;

        case NWIPE_SE_ATA_SANACT_OVERWRITE:
            feature = SANITIZE_OVERWRITE_EXT;
            lba = ( (__u64) SANITIZE_OVERWRITE_KEY << 32 ) | san->ovrpat;
            nsect = san->owpass;
            break;

        case NWIPE_SE_ATA_SANACT_FREEZE_LOCK:
            feature = SANITIZE_FREEZE_LOCK_EXT;
            lba = SANITIZE_FREEZE_LOCK_KEY;
            break;

        case NWIPE_SE_ATA_SANACT_ANTIFREEZE_LOCK:
            feature = SANITIZE_ANTIFREEZE_LOCK_EXT;
            lba = SANITIZE_ANTIFREEZE_LOCK_KEY;
            break;

        default:
            snprintf( san->error_msg, sizeof( san->error_msg ), "Unknown sanitize action" );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: Unknown sanitize action (planned_sanact=%d)",
                       __FUNCTION__,
                       san->device_path,
                       san->planned_sanact );
            return -1;
    }

    nwipe_log( NWIPE_LOG_INFO,
               "%s: issuing SANITIZE feat=0x%04x lba=0x%012llx nsect=%u",
               san->device_path,
               (unsigned) feature,
               (unsigned long long) lba,
               (unsigned) nsect );

    struct hdio_taskfile r;
    memset( &r, 0, sizeof( r ) );

    if( ata_sanitize_taskfile( san->fd, feature, lba, nsect, &r ) != 0 )
    {
        int eno = errno;

        snprintf( san->error_msg,
                  sizeof( san->error_msg ),
                  "%s (errno=%d, device reason: %s)",
                  strerror( eno ),
                  eno,
                  lbal_to_error_str( r.lob.lbal ) );

        nwipe_log( NWIPE_LOG_ERROR,
                   "%s: %s: SANITIZE failed: %s (errno=%d, device reason: %s)",
                   __FUNCTION__,
                   san->device_path,
                   strerror( eno ),
                   eno,
                   lbal_to_error_str( r.lob.lbal ) );

        return -1;
    }

    san->sanact = san->planned_sanact;

    nwipe_log( NWIPE_LOG_INFO, "%s: SANITIZE command accepted (feat=0x%04x)", san->device_path, (unsigned) feature );

    return 0;
} /* nwipe_se_ata_sanitize */
