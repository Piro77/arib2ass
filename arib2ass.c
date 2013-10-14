/*****************************************************************************
 * decode_mpeg.c: MPEG decoder example
 *----------------------------------------------------------------------------
 * Copyright (C) 2001-2010 VideoLAN
 * $Id: decode_mpeg.c 104 2005-03-21 13:38:56Z massiot $
 *
 * Authors: Jean-Paul Saman <jpsaman #_at_# m2x dot nl>
 *          Arnaud de Bossoreille de Ribou <bozo@via.ecp.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *----------------------------------------------------------------------------
 *
 *****************************************************************************/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#if defined(HAVE_STDBOOL_H)
#include <stdbool.h>
#endif

#include <getopt.h>

#if defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#elif defined(HAVE_STDINT_H)
#include <stdint.h>
#endif

/* The libdvbpsi distribution defines DVBPSI_DIST */
#ifdef DVBPSI_DIST
#include "../src/dvbpsi.h"
#include "../src/psi.h"
#include "../src/tables/pat.h"
#include "../src/descriptor.h"
#include "../src/tables/pmt.h"
#include "../src/descriptors/dr.h"
#else
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/descriptor.h>
#include <dvbpsi/pmt.h>
#include <dvbpsi/dr.h>
#endif

#include "common.h"

#define SYSTEM_CLOCK_DR 0x0B
#define MAX_BITRATE_DR 0x0E
#define STREAM_IDENTIFIER_DR 0x52
#define SUBTITLING_DR 0x59

#define READBUFSZ 8192

#ifndef _WIN32
#define O_BINARY 0
#endif
/*****************************************************************************
 * General typdefs
 *****************************************************************************/
typedef bool vlc_bool_t;

/*****************************************************************************
 * TS stream structures
 *----------------------------------------------------------------------------
 * PAT pid=0
 * - refers to N PMT's with pids known PAT
 *  PMT 0 pid=X stream_type
 *  PMT 1 pid=Y stream_type
 *  PMT 2 pid=Z stream_type
 *  - a PMT refers to N program_streams with pids known from PMT
 *   PID A type audio
 *   PID B type audio
 *   PID C type audio .. etc
 *   PID D type video
 *   PID E type teletext
 *****************************************************************************/

typedef struct
{
    dvbpsi_handle handle;

    int i_pat_version;
    int i_ts_id;
} ts_pat_t;

typedef struct ts_pid_s
{
    int         i_pid;

    vlc_bool_t  b_seen;
    int         i_cc;   /* countinuity counter */

    vlc_bool_t  b_pcr;  /* this PID is the PCR_PID */
    mtime_t     i_pcr;  /* last know PCR value */

    dvbpsi_pmt_es_t* p_es;
    block_t     *p_block;
    mtime_t     i_pts;
    int         i_pes_size;
    int         i_pes_gathered;
    decoder_t   *decoder;
} ts_pid_t;

typedef struct ts_pmt_s
{
    dvbpsi_handle handle;

    int         i_number; /* i_number = 0 is actually a NIT */
    int         i_pmt_version;
    ts_pid_t    *pid_pmt;
    ts_pid_t    *pid_pcr;
} ts_pmt_t;

typedef struct
{
    ts_pat_t    pat;

    int         i_pmt;
    ts_pmt_t    pmt;

    ts_pid_t    pid[8192];

    /* to determine length and time */
    int         i_pid_ref_pcr;
    mtime_t     i_first_pcr;
    mtime_t     i_current_pcr;
    mtime_t     i_last_pcr;
    int         i_pcrs_num;
    mtime_t     *p_pcrs;
    int64_t     *p_pos;
    uint16_t    i_packet_size;
    int         i_fd;

} ts_stream_t;

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static void DumpPMT(void* p_data, dvbpsi_pmt_t* p_pmt);

/*****************************************************************************
 * ReadPacket
 *****************************************************************************/
static int ReadPacket( int i_fd, uint8_t* p_dst )
{
    static char *buf = NULL;
    static int  left = 0;
    int i = 187;
    int i_rc = 1;
    int i_skip = 0;

    p_dst[0] = 0;

    while(1) {
        if (left < 188) {
            if (!buf) {
                buf = malloc(READBUFSZ);
            }
            else {
                if (left > 0) {
                    memmove(buf,buf+(READBUFSZ-left),left);
                }
            }
            i_rc = read(i_fd,buf+left,READBUFSZ-left);
            if (i_rc <= 0) return i_rc;
            left += i_rc;
        }
        i_skip = 0;
        for(i=READBUFSZ-left;i<READBUFSZ;i++) {
            if (buf[i] == 0x47) {
                if ((left - i_skip) >= 188) {
                    memcpy(p_dst,buf+i,188);
                    left -= 188 + i_skip;
                    return 188;
                }
                else {
                    left -= i_skip;
                    i = 0;
                    break;
                }
            }
            i_skip++;
        }
        if (i==READBUFSZ) left=0;
    }
}

static dvbpsi_descriptor_t *PMTEsFindDescriptor(dvbpsi_pmt_es_t *,int);
static int PMTEsHasComponentTag( dvbpsi_pmt_es_t *, int, int );

/*****************************************************************************
 * DumpPAT
 *****************************************************************************/
static void DumpPAT(void* p_data, dvbpsi_pat_t* p_pat)
{
    dvbpsi_pat_program_t* p_program = p_pat->p_first_program;
    ts_stream_t* p_stream = (ts_stream_t*) p_data;

    p_stream->pat.i_pat_version = p_pat->i_version;
    p_stream->pat.i_ts_id = p_pat->i_ts_id;

#if 0
    fprintf( stderr, "\n");
    fprintf( stderr, "New PAT\n");
    fprintf( stderr, "  transport_stream_id : %d\n", p_pat->i_ts_id);
    fprintf( stderr, "  version_number      : %d\n", p_pat->i_version);
    fprintf( stderr, "    | program_number @ [NIT|PMT]_PID\n");
#endif
    while( p_program )
    {
        if (p_program->i_number == 0) {
            p_program = p_program->p_next;
            continue;
        }

        p_stream->i_pmt++;
        p_stream->pmt.i_number = p_program->i_number;
        p_stream->pmt.pid_pmt = &p_stream->pid[p_program->i_pid];
        p_stream->pmt.pid_pmt->i_pid = p_program->i_pid;
        p_stream->pmt.handle = dvbpsi_AttachPMT( p_program->i_number, DumpPMT, p_stream );

#if 0
        fprintf( stderr, "    | %14d @ 0x%x (%d)\n",
                p_program->i_number, p_program->i_pid, p_program->i_pid);
#endif
        p_program = p_program->p_next;

        break; // only first program
    }
#if 0
    fprintf( stderr, "  active              : %d\n", p_pat->b_current_next);
#endif
    dvbpsi_DeletePAT(p_pat);
}

/*****************************************************************************
 * GetTypeName
 *****************************************************************************/
static char const* GetTypeName(uint8_t type)
{
    switch (type)
    {
        case 0x00:
            return "Reserved";
        case 0x01:
            return "ISO/IEC 11172 Video";
        case 0x02:
            return "ISO/IEC 13818-2 Video";
        case 0x03:
            return "ISO/IEC 11172 Audio";
        case 0x04:
            return "ISO/IEC 13818-3 Audio";
        case 0x05:
            return "ISO/IEC 13818-1 Private Section";
        case 0x06:
            return "ISO/IEC 13818-1 Private PES data packets";
        case 0x07:
            return "ISO/IEC 13522 MHEG";
        case 0x08:
            return "ISO/IEC 13818-1 Annex A DSM CC";
        case 0x09:
            return "H222.1";
        case 0x0A:
            return "ISO/IEC 13818-6 type A";
        case 0x0B:
            return "ISO/IEC 13818-6 type B";
        case 0x0C:
            return "ISO/IEC 13818-6 type C";
        case 0x0D:
            return "ISO/IEC 13818-6 type D";
        case 0x0E:
            return "ISO/IEC 13818-1 auxillary";
        default:
            if (type < 0x80)
                return "ISO/IEC 13818-1 reserved";
            else
                return "User Private";
    }
}

/*****************************************************************************
 * DumpMaxBitrateDescriptor
 *****************************************************************************/
static void DumpMaxBitrateDescriptor(dvbpsi_max_bitrate_dr_t* bitrate_descriptor)
{
    fprintf( stderr, "Bitrate: %d\n", bitrate_descriptor->i_max_bitrate);
}

/*****************************************************************************
 * DumpSystemClockDescriptor
 *****************************************************************************/
static void DumpSystemClockDescriptor(dvbpsi_system_clock_dr_t* p_clock_descriptor)
{
    fprintf( stderr, "External clock: %s, Accuracy: %E\n",
            p_clock_descriptor->b_external_clock_ref ? "Yes" : "No",
            p_clock_descriptor->i_clock_accuracy_integer *
            pow(10.0, -(double)p_clock_descriptor->i_clock_accuracy_exponent));
}

/*****************************************************************************
 * DumpStreamIdentifierDescriptor
 *****************************************************************************/
static void DumpStreamIdentifierDescriptor(dvbpsi_stream_identifier_dr_t* p_si_descriptor)
{
    fprintf( stderr, "Component tag: 0x%x\n",
            p_si_descriptor->i_component_tag);
}

/*****************************************************************************
 * DumpSubtitleDescriptor
 *****************************************************************************/
static void DumpSubtitleDescriptor(dvbpsi_subtitling_dr_t* p_subtitle_descriptor)
{
    int a;

    fprintf( stderr, "%d subtitles,\n", p_subtitle_descriptor->i_subtitles_number);
    for (a = 0; a < p_subtitle_descriptor->i_subtitles_number; ++a)
    {
        fprintf( stderr, "       | %d - lang: %c%c%c, type: %d, cpid: %d, apid: %d\n", a,
                p_subtitle_descriptor->p_subtitle[a].i_iso6392_language_code[0],
                p_subtitle_descriptor->p_subtitle[a].i_iso6392_language_code[1],
                p_subtitle_descriptor->p_subtitle[a].i_iso6392_language_code[2],
                p_subtitle_descriptor->p_subtitle[a].i_subtitling_type,
                p_subtitle_descriptor->p_subtitle[a].i_composition_page_id,
                p_subtitle_descriptor->p_subtitle[a].i_ancillary_page_id);
    }
}

/*****************************************************************************
 * DumpDescriptors
 *****************************************************************************/
static void DumpDescriptors(const char* str, dvbpsi_descriptor_t* p_descriptor)
{
    int i;

    while(p_descriptor)
    {
        fprintf( stderr, "%s 0x%02x : ", str, p_descriptor->i_tag);
        switch (p_descriptor->i_tag)
        {
            case SYSTEM_CLOCK_DR:
                DumpSystemClockDescriptor(dvbpsi_DecodeSystemClockDr(p_descriptor));
                break;
            case MAX_BITRATE_DR:
                DumpMaxBitrateDescriptor(dvbpsi_DecodeMaxBitrateDr(p_descriptor));
                break;
            case STREAM_IDENTIFIER_DR:
                DumpStreamIdentifierDescriptor(dvbpsi_DecodeStreamIdentifierDr(p_descriptor));
                break;
            case SUBTITLING_DR:
                DumpSubtitleDescriptor(dvbpsi_DecodeSubtitlingDr(p_descriptor));
                break;
            default:
                fprintf( stderr, "\"");
                for(i = 0; i < p_descriptor->i_length; i++)
                    fprintf( stderr, "%c", p_descriptor->p_data[i]);
                fprintf( stderr, "\"\n");
        }
        p_descriptor = p_descriptor->p_next;
    }
}

/*****************************************************************************
 * DumpPMT
 *****************************************************************************/
static void DumpPMT(void* p_data, dvbpsi_pmt_t* p_pmt)
{
    dvbpsi_pmt_es_t* p_es = p_pmt->p_first_es;
    ts_stream_t* p_stream = (ts_stream_t*) p_data;

    p_stream->pmt.i_pmt_version = p_pmt->i_version;
    p_stream->pmt.pid_pcr = &p_stream->pid[p_pmt->i_pcr_pid];
    p_stream->pid[p_pmt->i_pcr_pid].b_pcr = VLC_TRUE;
    p_stream->pid[p_pmt->i_pcr_pid].i_pid = p_pmt->i_pcr_pid;

#if 0
    fprintf( stderr, "\n" );
    fprintf( stderr, "New active PMT\n" );
    fprintf( stderr, "  program_number : %d\n", p_pmt->i_program_number );
    fprintf( stderr, "  version_number : %d\n", p_pmt->i_version );
    fprintf( stderr, "  PCR_PID        : 0x%x (%d)\n", p_pmt->i_pcr_pid, p_pmt->i_pcr_pid);
    DumpDescriptors("    ]", p_pmt->p_first_descriptor);
    fprintf( stderr, "    | type @ elementary_PID\n");
#endif
    while(p_es)
    {
        if (p_es->i_type == 0x06) { //
            if (PMTEsHasComponentTag(p_es,0x30,0x37)) {
                p_stream->pid[p_es->i_pid].p_es = p_es;
                if (p_stream->pid[p_es->i_pid].b_seen && !p_stream->pid[p_es->i_pid].decoder) {
                    p_stream->pid[p_es->i_pid].b_seen = VLC_FALSE;
                }
            }
        }
#if 0
        fprintf( stderr, "    | 0x%02x (%s) @ 0x%x (%d)\n", p_es->i_type, GetTypeName(p_es->i_type),
                p_es->i_pid, p_es->i_pid);
        DumpDescriptors("    |  ]", p_es->p_first_descriptor);
#endif
        p_es = p_es->p_next;
    }
    dvbpsi_DeletePMT(p_pmt);
}

static mtime_t AdjustPCRWrapAround( ts_stream_t *p_stream, mtime_t i_pcr )
{
    /*
     * PCR is 33bit. If PCR reaches to 0x1FFFFFFFF (26:30:43.717), ressets from 0.
     * So, need to add 0x1FFFFFFFF, for calculating duration or current position.
     */
    mtime_t i_adjust = 0;
    int64_t i_pos = lseek( p_stream->i_fd ,0,SEEK_CUR);
    int i;
    for( i = 1; i < p_stream->i_pcrs_num && p_stream->p_pos[i] <= i_pos; ++i )
    {
        if( p_stream->p_pcrs[i-1] > p_stream->p_pcrs[i] )
            i_adjust += 0x1FFFFFFFF;
    }
    if( p_stream->p_pcrs[i-1] > i_pcr )
        i_adjust += 0x1FFFFFFFF;

    return i_pcr + i_adjust;
}

static mtime_t GetPCR( uint8_t *p )

{
    mtime_t i_pcr = -1;

    if( ( p[3]&0x20 ) && /* adaptation */
            ( p[5]&0x10 ) &&
            ( p[4] >= 7 ) )
    {
        /* PCR is 33 bits */
        i_pcr = ( (mtime_t)p[6] << 25 ) |
            ( (mtime_t)p[7] << 17 ) |
            ( (mtime_t)p[8] << 9 ) |
            ( (mtime_t)p[9] << 1 ) |
            ( (mtime_t)p[10] >> 7 );
    }
    return i_pcr;
}


/*****************************************************************************
 * usage
 *****************************************************************************/
static void usage( char *name )
{
    printf( "Usage: %s [--file <filename>|--help|--version|--debug|--output <ofilename>]\n", name );
    printf( "       %s [-f <filename>|-h|-v|-d|-o <ofilename>]\n", name );
    printf( "\n" );
    printf( "       %s --help\n", name );
    printf( "       %s --file <filename> --output <ofilename>\n", name );
    printf( "Arguments:\n" );
    printf( "file   : read MPEG2-TS stream from file\n" );
    printf( "         default output ASS file <filename>.ass \n" );
    printf( "output : output ASS filename \n" );
    printf( "help   : print this help message\n" );
    printf( "debug  : output debug info to <filename>.asslog \n" );
}
static void printversion( char *name )
{
    printf( "%s version %s\n", name ,VERSION);
}
static char *outputfilename = NULL;
static char *filename = NULL;
static int  debugflg = 0;
char *getoutputfilename()
{
    return outputfilename;
}
char *getinputfilename()
{
    return filename;
}
int getdebugflg()
{
    return debugflg;
}

/*****************************************************************************
 * main
 *****************************************************************************/
int main(int i_argc, char* pa_argv[])
{
    const char* const short_options = "hdf:vo:";
    const struct option long_options[] =
    {
        { "help",       0, NULL, 'h' },
        { "debug",      0, NULL, 'd' },
        { "file",       1, NULL, 'f' },
        { "output",     1, NULL, 'o' },
        { "version",    0, NULL, 'v' },
        { NULL,         0, NULL, 0 }
    };
    int next_option = 0;

    int i_fd = -1;
    int i_mtu = 1316; /* (7 * 188) = 1316 < 1500 network MTU */
#ifdef HAVE_GETTIMEOFDAY
    mtime_t  time_prev = 0;
    mtime_t  time_base = 0;
#endif
    mtime_t  i_prev_pcr = 0;  /* 33 bits */
    int      i_old_cc = -1;
    uint32_t i_bytes = 0; /* bytes transmitted between PCR's */

    uint8_t *p_data = NULL;
    ts_stream_t *p_stream = NULL;
    int i_len = 0;
    int b_verbose = 0;
    int i = 0;

    /* parser commandline arguments */
    do {
        next_option = getopt_long( i_argc, pa_argv, short_options, long_options, NULL );
        switch( next_option )
        {
            case 'f':
                filename = strdup( optarg );
                break;
            case 'o':
                outputfilename = strdup( optarg );
                break;
            case 'h':
                usage( pa_argv[0] );
                goto error;
                break;
            case 'v':
                printversion( pa_argv[0] );
                goto error;
                break;
            case 'd':
                debugflg = 1;
                break;
            case -1:
                break;
            default:
                usage( pa_argv[0] );
                goto error;
        }
    } while( next_option != -1 );

    /* initialize */
    if( filename )
    {
        i_fd = open( filename, O_BINARY );
        p_data = (uint8_t *) malloc( sizeof( uint8_t ) * 188 );
        if( !p_data )
            goto out_of_memory;
    }
    else
    {
        usage( pa_argv[0] );
        goto error;
    }

    p_stream = (ts_stream_t *) malloc( sizeof(ts_stream_t) );
    if( !p_stream )
        goto out_of_memory;
    memset( p_stream, 0, sizeof(ts_stream_t) );

    p_stream->i_pid_ref_pcr = -1;
    p_stream->i_first_pcr = -1;
    p_stream->i_current_pcr = -1;
    p_stream->i_last_pcr = -1;
    p_stream->i_pcrs_num = 10;
    p_stream->p_pcrs = (mtime_t *)calloc( p_stream->i_pcrs_num, sizeof( mtime_t ) );
    p_stream->p_pos = (int64_t *)calloc( p_stream->i_pcrs_num, sizeof( int64_t ) );
    p_stream->i_packet_size = 188;
    p_stream->i_fd = i_fd;

    /* Read first packet */
    if( filename )
        i_len = ReadPacket( i_fd, p_data );

    /* Enter infinite loop */
    p_stream->pat.handle = dvbpsi_AttachPAT( DumpPAT, p_stream );
    while( i_len > 0 )
    {
        vlc_bool_t b_first = VLC_FALSE;

        i_bytes += i_len;
        for( i = 0; i < i_len; i += 188 )
        {
            uint8_t    i_skip = 0;
            uint8_t   *p_tmp = &p_data[i];
            uint16_t   i_pid = ((uint16_t)(p_tmp[1] & 0x1f) << 8) + p_tmp[2];
            int        i_cc = (p_tmp[3] & 0x0f);
            vlc_bool_t b_adaptation = (p_tmp[3] & 0x20); /* adaptation field */
            vlc_bool_t b_discontinuity = VLC_FALSE;
            vlc_bool_t b_payload = (p_tmp[3] & 0x10);
            vlc_bool_t b_unit_start = p_tmp[1]&0x40;


            /* Get the PID */
            ts_pid_t *p_pid = &p_stream->pid[ ((p_tmp[i+1]&0x1f)<<8)|p_tmp[i+2] ];




            if( i_pid == 0x0 )
                dvbpsi_PushPacket(p_stream->pat.handle, p_tmp);
            else if( p_stream->pmt.pid_pmt && i_pid == p_stream->pmt.pid_pmt->i_pid )
                dvbpsi_PushPacket(p_stream->pmt.handle, p_tmp);

            /* Remember PID */
            if(( !p_stream->pid[i_pid].b_seen ) && (p_stream->pmt.pid_pcr))
            {
                p_stream->pid[i_pid].b_seen = VLC_TRUE;
                p_stream->pid[i_pid].i_cc = 0xff;
                if (p_stream->pid[i_pid].p_es) {
                    p_stream->pid[i_pid].p_block = calloc(1,sizeof(block_t));
                    p_stream->pid[i_pid].decoder = calloc(1,sizeof(decoder_t));
                    dec_open(p_stream->pid[i_pid].decoder);
                    fprintf(stderr,"Target pid  0x%x PMT 0x%x \n",i_pid,p_stream->pmt.pid_pmt->i_pid);
                }
            }

            /* Handle discontinuities if they occurred,
             * according to ISO/IEC 13818-1: DIS pages 20-22 */
            if( b_adaptation )
            {
                vlc_bool_t b_discontinuity_indicator = (p_tmp[5]&0x80);
                vlc_bool_t b_random_access_indicator = (p_tmp[5]&0x40);
                vlc_bool_t b_pcr = (p_tmp[5]&0x10);  /* PCR flag */

                i_skip = 5 + p_tmp[4];

                b_discontinuity = (p_tmp[5]&0x80) ? true : false;

            }
            else
            {
                i_skip = 4;
            }

            /* Test continuity counter */
            /* continuous when (one of this):
             * diff == 1
             * diff == 0 and payload == 0
             * diff == 0 and duplicate packet (playload != 0) <- should we
             *   test the content ?
             */
            const int i_diff = ( i_cc - p_pid->i_cc )&0x0f;
            if( b_payload && i_diff == 1 )
            {
                p_pid->i_cc = ( p_pid->i_cc + 1 ) & 0xf;
            }
            else 
            {
                if( p_pid->i_cc == 0xff )
                {
                    p_pid->i_cc = i_cc;
                }
                else if( i_diff != 0 && !b_discontinuity )
                {
                    p_pid->i_cc = i_cc;
                    if( p_pid->p_es /* && p_pid->es->fmt.i_cat != VIDEO_ES */ )
                    {
                        /* Small video artifacts are usually better than
                         * dropping full frames 
                         p_pid->p_es->i_flags |= BLOCK_FLAG_CORRUPTED;
                         */
                    }
                }
            }
            mtime_t i_pcr = GetPCR( p_tmp );
            if( i_pcr >= 0  && (p_stream->pmt.pid_pcr) && (i_pid == p_stream->pmt.pid_pcr->i_pid))
            {
                if (p_stream->i_pid_ref_pcr == -1) {
                    p_stream->i_pid_ref_pcr = i_pid;
                    p_stream->i_first_pcr = i_pcr;
                    p_stream->i_current_pcr = i_pcr;
                    fprintf(stderr,"refpcr %s pid 0x%x\n",dumpts(i_pcr),i_pid);
                }
                if( p_stream->i_pid_ref_pcr == p_pid->i_pid )
                {
                    p_stream->i_current_pcr = AdjustPCRWrapAround( p_stream, i_pcr );
                }


            }
            // payload 

            uint8_t *header;
            int     i_size;
            header = p_tmp + i_skip;
            i_size = i_len - i_skip;

            // Invalid ?
            if (i_size < 0) break;

            if( b_unit_start )
            {
                if (p_pid->p_es && p_pid->p_block) {
                    p_pid->i_pes_size = 0;
                    p_pid->i_pes_gathered = 0;
                    if (p_pid->p_block->p_buffer) {
                        free(p_pid->p_block->p_buffer);
                        p_pid->p_block->i_buffer = 0;
                        p_pid->p_block->p_buffer = 0;
                    }
                }
                if (b_payload && p_pid->p_es && p_pid->p_block) {
                    //printf("have payload %d offset %d\n",i_pid,p_tmp[i_skip]);
                    int skip2;
                    mtime_t i_pts,i_dts;
                    if( header[0] != 0 || header[1] != 0 || header[2] != 1 ) {
                        fprintf(stderr,"Invalid header\n");
                    }
                    else {
                        skip2=header[8]+9;
                        if(( ( header[6]&0xC0 ) == 0x80 ) && (i_size - skip2 > 0)){
                            i_skip = i_skip + skip2;
                            if( header[7]&0x80 )    /* has pts */
                            {
                                i_pts = ((mtime_t)(header[ 9]&0x0e ) << 29)|
                                    (mtime_t)(header[10] << 22)|
                                    ((mtime_t)(header[11]&0xfe) << 14)|
                                    (mtime_t)(header[12] << 7)|
                                    (mtime_t)(header[13] >> 1);

                                if( header[7]&0x40 )    /* has dts */
                                {
                                    i_dts = ((mtime_t)(header[14]&0x0e ) << 29)|
                                        (mtime_t)(header[15] << 22)|
                                        ((mtime_t)(header[16]&0xfe) << 14)|
                                        (mtime_t)(header[17] << 7)|
                                        (mtime_t)(header[18] >> 1);
                                }
                            }
                            if (i_size > 6) {
                                p_pid->i_pes_size = (header[4]<<8 | header[5]);
                                if (p_pid->i_pes_size > 0) p_pid->i_pes_size += 6;
                            }
                            p_pid->p_block->p_buffer  = calloc(1,i_size);
                            memcpy(p_pid->p_block->p_buffer,p_tmp+i_skip,i_size - skip2);
                            p_pid->p_block->i_buffer = i_size - skip2;
                            // first pcr - pts diff time
                            if (i_pts < p_stream->i_first_pcr)
                                i_pts += 0x1FFFFFFFF;
                            p_pid->p_block->i_pts = i_pts - p_stream->i_first_pcr;
                            p_pid->i_pts = i_pts;
                            p_pid->i_pes_gathered += i_size;
                            if( p_pid->i_pes_size > 0 &&
                                    p_pid->i_pes_gathered >= p_pid->i_pes_size )
                            {
                                /* XXX overflow??? */
                                p_pid->p_block->p_buffer = realloc(p_pid->p_block->p_buffer,p_pid->p_block->i_buffer * 2);
                                p_pid->p_block->p_buffer[p_pid->p_block->i_buffer+1]=0;
                                p_pid->p_block->i_buffer += 1;
                                if (p_pid->decoder) {
                                    p_pid->decoder->pf_decode_sub(p_pid->decoder,&p_pid->p_block);
                                }
                            }
                        }
                        else {

                        }
                    }
                }
            } // unit_start
            else
            {
                if (b_payload && p_pid->p_es && p_pid->p_block) {
                    if( p_pid->p_es == NULL )
                    {

                    }
                    else
                    {
                        p_pid->p_block->p_buffer = realloc(p_pid->p_block->p_buffer,p_pid->p_block->i_buffer+i_size);
                        memcpy(p_pid->p_block->p_buffer+p_pid->p_block->i_buffer,p_tmp+i_skip,i_size);
                        p_pid->p_block->i_buffer += i_size;
                        p_pid->i_pes_gathered += i_size; 
                        if( p_pid->i_pes_size > 0 &&
                                p_pid->i_pes_gathered >= p_pid->i_pes_size )
                        {
                            /* XXX overflow??? */
                            p_pid->p_block->p_buffer = realloc(p_pid->p_block->p_buffer,p_pid->p_block->i_buffer * 2);
                            p_pid->p_block->p_buffer[p_pid->p_block->i_buffer+1]=0;
                            p_pid->p_block->i_buffer += 1;
                            if (p_pid->decoder) {
                                p_pid->decoder->pf_decode_sub(p_pid->decoder,&p_pid->p_block);
                            }
                        }
                    }
                }
            }

        }

        i_len = ReadPacket( i_fd, p_data );
    }
    if( p_stream->pmt.handle )
        dvbpsi_DetachPMT( p_stream->pmt.handle );
    if( p_stream->pat.handle )
        dvbpsi_DetachPAT( p_stream->pat.handle );

    /* clean up */
    if( filename )
        close( i_fd );

    if( p_data )    free( p_data );
    if( filename )  free( filename );

    for(i=0;i<8192;i++) {
        ts_pid_t *p_pid = &p_stream->pid[i];
        if (p_pid && p_pid->decoder) {
            dec_close(p_pid->decoder);
            free(p_pid->decoder);
        }
        if (p_pid && p_pid->p_block)
            free(p_pid->p_block);
    }
    /* free other stuff first ;-)*/
    if( p_stream )  {
        free( p_stream->p_pcrs );
        free( p_stream->p_pos );
        free( p_stream );
    }
    return EXIT_SUCCESS;

out_of_memory:
    fprintf( stderr, "Out of memory\n" );

error:
    if( p_data )    free( p_data );
    if( filename )  free( filename );

    /* free other stuff first ;-)*/
    if( p_stream )  free( p_stream );
    return EXIT_FAILURE;
}
static dvbpsi_descriptor_t *PMTEsFindDescriptor( dvbpsi_pmt_es_t *p_es, int i_tag )
{
    dvbpsi_descriptor_t *p_dr = p_es->p_first_descriptor;;
    while( p_dr && ( p_dr->i_tag != i_tag ) )
        p_dr = p_dr->p_next;
    return p_dr;
}
static int PMTEsHasComponentTag( dvbpsi_pmt_es_t *p_es, int i_component_tag , int i_component_tag2 )
{
    dvbpsi_descriptor_t *p_dr = PMTEsFindDescriptor( p_es, 0x52 );
    if( !p_dr )
        return 0;
    dvbpsi_stream_identifier_dr_t *p_si = dvbpsi_DecodeStreamIdentifierDr( p_dr );
    if( !p_si )
        return 0;

    if ((p_si->i_component_tag >= i_component_tag) && (p_si->i_component_tag <= i_component_tag2)) return 1;
    else return 0;
}

#if 0
static void GetFirstPCR( ts_stream_t *p_stream )
{
    int64_t i_initial_pos = stream_Tell( p_stream->i_fd );

    if( stream_Seek( p_stream->i_fd, 0 ) )
        return;

    while( vlc_object_alive (p_demux) )
    {
        block_t     *p_pkt;
        if( !( p_pkt = ReadTSPacket( p_demux ) ) )
        {
            break;
        }
        mtime_t i_pcr = GetPCR( p_pkt );
        if( i_pcr >= 0 )
        {
            p_stream->i_pid_ref_pcr = PIDGet( p_pkt );
            p_stream->i_first_pcr = i_pcr;
            p_stream->i_current_pcr = i_pcr;
        }
        block_Release( p_pkt );
        if( p_stream->i_first_pcr >= 0 )
            break;
    }
    stream_Seek( p_stream->i_fd, i_initial_pos );
}

static void GetLastPCR( ts_stream_t *p_stream )
{
    int64_t i_initial_pos = stream_Tell( p_stream->i_fd );
    mtime_t i_initial_pcr = p_stream->i_current_pcr;

    int64_t i_last_pos = stream_Size( p_stream->i_fd ) - p_stream->i_packet_size;
    int64_t i_pos = i_last_pos - p_stream->i_packet_size * 4500; /* FIXME if the value is not reasonable, please change it. */
    if( i_pos < 0 )
        return;

    while( vlc_object_alive( p_demux ) )
    {
        if( SeekToPCR( p_demux, i_pos ) )
            break;
        p_stream->i_last_pcr = AdjustPCRWrapAround( p_demux, p_stream->i_current_pcr );
        if( ( i_pos = stream_Tell( p_stream->i_fd ) ) >= i_last_pos )
            break;
    }
    if( p_stream->i_last_pcr >= 0 )
    {
        int64_t i_size = stream_Size( p_stream->i_fd );
        mtime_t i_duration_msec = ( p_stream->i_last_pcr - p_stream->i_first_pcr ) * 100 / 9 / 1000;
        int64_t i_rate = ( i_size < 0 || i_duration_msec <= 0 ) ? 0 : i_size * 1000 * 8 / i_duration_msec;
        const int64_t TS_SUPPOSED_MAXRATE = 55 * 1000 * 1000; //FIXME I think it's enough.
        const int64_t TS_SUPPOSED_MINRATE = 0.5 * 1000 * 1000; //FIXME
        if( i_rate < TS_SUPPOSED_MINRATE || i_rate > TS_SUPPOSED_MAXRATE )
        {
            msg_Dbg( p_demux, "calculated bitrate (%"PRId64"bit/s) is too low or too high. min bitrate (%lldbit/s) max bitrate (%lldbit/s)",
                    i_rate, TS_SUPPOSED_MINRATE, TS_SUPPOSED_MAXRATE );
            p_stream->i_last_pcr = -1;
        }
    }
    stream_Seek( p_stream->i_fd, i_initial_pos );
    p_stream->i_current_pcr = i_initial_pcr;
}

static void CheckPCR( ts_stream_t *p_stream )
{
    int64_t i_initial_pos = stream_Tell( p_stream->i_fd );
    mtime_t i_initial_pcr = p_stream->i_current_pcr;

    int64_t i_size = stream_Size( p_stream->i_fd );

    int i = 0;
    p_stream->p_pcrs[0] = p_stream->i_first_pcr;
    p_stream->p_pos[0] = i_initial_pos;

    for( i = 1; i < p_stream->i_pcrs_num && ( p_demux ); ++i )
    {
        int64_t i_pos = i_size / p_stream->i_pcrs_num * i;
        if( SeekToPCR( p_stream, i_pos ) )
            break;
        p_stream->p_pcrs[i] = p_stream->i_current_pcr;
        p_stream->p_pos[i] = stream_Tell( p_stream->i_fd );
        if( p_stream->p_pcrs[i-1] > p_stream->p_pcrs[i] )
        {
            //    msg_Dbg( p_demux, "PCR Wrap Around found between %d%% and %d%% (pcr:%lld(0x%09llx) pcr:%lld(0x%09llx))", (int)((i-1)*100/p_stream->i_pcrs_num), (int)(i*100/p_stream->i_pcrs_num), p_stream->p_pcrs[i-1], p_stream->p_pcrs[i-1], p_stream->p_pcrs[i], p_stream->p_pcrs[i] );
        }
    }
    if( i < p_stream->i_pcrs_num )
    {
        //msg_Dbg( p_demux, "Force Seek Per Percent: Seeking failed at %d%%.", (int)(i*100/p_stream->i_pcrs_num) );
        //p_stream->b_force_seek_per_percent = true;
    }

    stream_Seek( p_stream->i_fd, i_initial_pos );
    p_stream->i_current_pcr = i_initial_pcr;
}

static void PCRHandle( ts_stream_t *p_stream, ts_pid_t *pid, block_t *p_bk )
{
    if( p_stream->i_pmt <= 0 )
        return;

    mtime_t i_pcr = GetPCR( p_bk );
    if( i_pcr >= 0 )
    {

        if( p_stream->i_pid_ref_pcr == pid->i_pid )
        {
            p_stream->i_current_pcr = AdjustPCRWrapAround( p_stream, i_pcr );
        }

        /* Search program and set the PCR */
        for( int i = 0; i < p_stream->i_pmt; i++ )
        {
            for( int i_prg = 0; i_prg < p_stream->pmt[i]->psi->i_prg; i_prg++ )
            {
                if( pid->i_pid == p_stream->pmt[i]->psi->prg[i_prg]->i_pid_pcr )
                {
                    es_out_Control( p_demux->out, ES_OUT_SET_GROUP_PCR,
                            (int)p_stream->pmt[i]->psi->prg[i_prg]->i_number,
                            (int64_t)(VLC_TS_0 + i_pcr * 100 / 9) );
                }
            }
        }
    }
}
#endif
