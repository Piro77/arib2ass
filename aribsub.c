/*****************************************************************************
 * aribsub.c : ARIB subtitle decoder
 *****************************************************************************
 * Copyright (C) 2012 Naohiro KORIYAMA
 * Copyright (C) 2012 the VideoLAN team
 *
 * Authors:  Naohiro KORIYAMA <nkoriyama@gmail.com>
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
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#endif
#include "common.h"

#include "vlc_bits.h"
#include "vlc_md5.h"

#include "png.h"

#define	ARIBSUB_GEN_DRCS_DATA	1

#include "aribb24dec.h"

#define DEBUG_ARIBSUB 1

/****************************************************************************
 * Local structures
 ****************************************************************************/


struct decoder_sys_t
{
    bs_t              bs;

    /* Decoder internal data */
#if 0
    arib_data_group_t data_group;
#endif
    uint32_t          i_data_unit_size;
    int               i_subtitle_data_size;
    unsigned char     *psz_subtitle_data;

    char              *psz_fontfamily;
    bool              b_ignore_ruby;

    arib_decoder_t    arib_decoder;
#ifdef ARIBSUB_GEN_DRCS_DATA
    drcs_data_t       *p_drcs_data;
#endif //ARIBSUB_GEN_DRCS_DATA

    int               i_drcs_num;
    char              drcs_hash_table[10][32 + 1];

    drcs_conversion_t *p_drcs_conv;
    char              *outputfile;
    char              *inputfile;
    FILE              *outputfp;
    FILE              *debugfp;
};

typedef struct ass_region_buf_s
{
    char    *p_buf;
    unsigned char    f_blink;
    mtime_t i_start;
    struct ass_region_buf_s *p_next;
}ass_region_buf_t;

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static void load_drcs_conversion_table( decoder_t * );
static void parse_data_unit( decoder_t * );
static void parse_caption_management_data( decoder_t * );
static void parse_caption_statement_data( decoder_t * );
static void parse_data_group( decoder_t * );
static void parse_arib_pes( decoder_t * );
static bool isallspace(char *,int);
static void dumpheader(decoder_t *);
static void dumpregion(decoder_t *,ass_region_buf_t *,mtime_t);
static void pushregion(decoder_t *,mtime_t,mtime_t);
static void dumparib(decoder_t *,mtime_t);
static void free_all(decoder_t *);
static void free_assregion(ass_region_buf_t *);

static void *Decode( void *dec, block_t **pp_block )
{
    decoder_t *p_dec = (decoder_t *)dec;
    block_t *p_block;

    decoder_sys_t *p_sys = p_dec->p_sys;

    if( ( pp_block == NULL ) || ( *pp_block == NULL ) )
    {
        return NULL;
    }
    p_block = *pp_block;
    bs_init( &p_sys->bs, p_block->p_buffer, p_block->i_buffer );

    parse_arib_pes( p_dec );

    dumparib(p_dec,p_block->i_pts);
}

void *dec_open(void *p_this)
{
    decoder_t     *p_dec = (decoder_t *) p_this;
    decoder_sys_t *p_sys;

    p_dec->pf_decode_sub = Decode;
    p_sys = p_dec->p_sys = (decoder_sys_t*) calloc( 1, sizeof(decoder_sys_t) );
    if (p_sys == NULL) return NULL;

    p_sys->i_subtitle_data_size = 0;
    p_sys->psz_subtitle_data = NULL;
    p_sys->b_ignore_ruby = false;
#ifdef ARIBSUB_GEN_DRCS_DATA
    p_sys->p_drcs_data = NULL;
#endif //ARIBSUB_GEN_DRCS_DATA
    p_sys->i_drcs_num = 0;

    p_sys->p_drcs_conv = NULL;

    load_drcs_conversion_table( p_dec );

    p_sys->outputfile = (char *)getoutputfilename();
    p_sys->inputfile = (char *)getinputfilename();
    if (getdebugflg())
    {
        char *debugfile;
        asprintf(&debugfile,"%s.asslog",p_sys->inputfile);
        p_sys->debugfp = vlc_fopen(debugfile,"w");
        free(debugfile);
    }

}
void *dec_close(void *p_this)
{
    decoder_t *p_dec = (decoder_t *)p_this;
    block_t *p_block;

    decoder_sys_t *p_sys = p_dec->p_sys;

    if (p_sys->outputfp) fclose(p_sys->outputfp);
    if (p_sys->debugfp) fclose(p_sys->debugfp);

    free_all(p_dec);
    free(p_sys);
}
#if 0
static subpicture_t *render( decoder_t *, block_t * );
/*****************************************************************************
 * Open: probe the decoder and return score
 *****************************************************************************
 * Tries to launch a decoder and return score so that the interface is able
 * to chose.
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    decoder_t     *p_dec = (decoder_t *) p_this;
    decoder_sys_t *p_sys;

    if( p_dec->fmt_in.i_codec != VLC_CODEC_ARIB )
    {
        return VLC_EGENERIC;
    }

    p_dec->pf_decode_sub = Decode;
    p_dec->fmt_out.i_cat = SPU_ES;
    p_dec->fmt_out.i_codec = 0;

    p_sys = p_dec->p_sys = (decoder_sys_t*) calloc( 1, sizeof(decoder_sys_t) );
    if( p_sys == NULL )
    {
        return VLC_ENOMEM;
    }

    p_sys->i_subtitle_data_size = 0;
    p_sys->psz_subtitle_data = NULL;
    p_sys->psz_fontfamily = var_InheritString( p_this, "freetype-font" );
    p_sys->b_ignore_ruby =
        var_InheritBool( p_this, ARIBSUB_CFG_PREFIX "ignore_ruby" );
#ifdef ARIBSUB_GEN_DRCS_DATA
    p_sys->p_drcs_data = NULL;
#endif //ARIBSUB_GEN_DRCS_DATA
    p_sys->i_drcs_num = 0;

    p_sys->p_drcs_conv = NULL;
    load_drcs_conversion_table( p_dec );

    return VLC_SUCCESS;
}
#endif
static void free_all( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    free( p_sys->psz_subtitle_data );
    p_sys->psz_subtitle_data = NULL;

    free( p_sys->psz_fontfamily );
    p_sys->psz_fontfamily = NULL;

    drcs_conversion_t *p_drcs_conv = p_sys->p_drcs_conv;
    while( p_drcs_conv != NULL )
    {
        drcs_conversion_t *p_next = p_drcs_conv->p_next;
        free( p_drcs_conv );
        p_drcs_conv = p_next;
    }
    p_sys->p_drcs_conv = NULL;
}
#if 0
/*****************************************************************************
 * Close:
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    decoder_t     *p_dec = (decoder_t*) p_this;
    decoder_sys_t *p_sys = p_dec->p_sys;

    var_Destroy( p_this, ARIBSUB_CFG_PREFIX "ignore_ruby" );

    free_all( p_dec );
    free( p_sys );
}

/*****************************************************************************
 * Decode:
 *****************************************************************************/
static subpicture_t *Decode( decoder_t *p_dec, block_t **pp_block )
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    block_t       *p_block;
    subpicture_t  *p_spu = NULL;

    if( ( pp_block == NULL ) || ( *pp_block == NULL ) )
    {
        return NULL;
    }
    p_block = *pp_block;

    bs_init( &p_sys->bs, p_block->p_buffer, p_block->i_buffer );

    parse_arib_pes( p_dec );

    p_spu = render( p_dec, p_block );

    block_Release( p_block );
    *pp_block = NULL;

    return p_spu;
}
#endif

/* following functions are local */

static char* get_arib_base_dir( decoder_t *p_dec )
{
    VLC_UNUSED(p_dec);

    char *psz_arib_base_dir;
    //current directory
    if( asprintf( &psz_arib_base_dir, "%s", "." ) < 0 )
    {
        psz_arib_base_dir = NULL;
    }

    return psz_arib_base_dir;
}

static char* get_arib_data_dir( decoder_t *p_dec )
{
    char *psz_arib_base_dir = get_arib_base_dir( p_dec );
    if( psz_arib_base_dir == NULL )
    {
        return NULL;
    }

    char *psz_arib_data_dir;
    if( asprintf( &psz_arib_data_dir, "%s"DIR_SEP"data", psz_arib_base_dir ) < 0 )
    {
        psz_arib_data_dir = NULL;
    }
    free( psz_arib_base_dir );

    return psz_arib_data_dir;
}

static void create_arib_basedir( decoder_t *p_dec )
{
    char *psz_arib_base_dir = get_arib_base_dir( p_dec );
    if( psz_arib_base_dir == NULL )
    {
        return;
    }

    struct stat st;
    vlc_stat( psz_arib_base_dir, &st );
    if( !S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode) )
    {
        if( vlc_mkdir( psz_arib_base_dir, 0777) != 0 )
        {
            // ERROR
        }
    }

    free( psz_arib_base_dir );
}

static void create_arib_datadir( decoder_t *p_dec )
{
    create_arib_basedir( p_dec );
    char *psz_arib_data_dir = get_arib_data_dir( p_dec );
    if( psz_arib_data_dir == NULL )
    {
        return;
    }

    struct stat st;
    vlc_stat( psz_arib_data_dir, &st );
    if( !S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode) )
    {
        if( vlc_mkdir( psz_arib_data_dir, 0777) == 0 )
        {
            // ERROR
        }
    }

    free( psz_arib_data_dir );
}

static void load_drcs_conversion_table( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    p_sys->p_drcs_conv = NULL;

    create_arib_basedir( p_dec );

    char *psz_arib_base_dir = get_arib_base_dir( p_dec );
    if( psz_arib_base_dir == NULL )
    {
        return;
    }

    char* psz_conv_file;
    if( asprintf( &psz_conv_file, "%s"DIR_SEP"drcs_conv.ini", psz_arib_base_dir ) < 0 )
    {
        psz_conv_file = NULL;
    }
    free( psz_arib_base_dir );
    if( psz_conv_file == NULL )
    {
        return;
    }

    FILE *fp = vlc_fopen( psz_conv_file, "r" );
    free( psz_conv_file );
    if( fp == NULL )
    {
        return;
    }

    drcs_conversion_t *p_drcs_conv = NULL;
    char buf[256] = { 0 };
    while( fgets( buf, 256, fp ) != 0 )
    {
        if( buf[0] == ';' || buf[0] == '#' ) // comment
        {
            continue;
        }

        char* p_ret = strchr( buf, '\n' );
        if( p_ret != NULL )
        {
            *p_ret = '\0';
        }

        char *p_eq = strchr( buf, '=' );
        if( p_eq == NULL || p_eq - buf != 32 )
        {
            continue;
        }
        char *p_code = strstr( buf, "U+" );
        if( p_code == NULL || strlen( p_code ) < 2 || strlen( p_code ) > 8 )
        {
            continue;
        }

        char hash[32 + 1];
        strncpy( hash, buf, 32 );
        hash[32] = '\0';
        unsigned long code = strtoul( p_code + 2, NULL, 16 );
        if( code > 0x10ffff )
        {
            continue;
        }

        drcs_conversion_t *p_next = (drcs_conversion_t*) malloc(
                sizeof(drcs_conversion_t) );
        if( p_next == NULL )
        {
            continue;
        }
        strncpy( p_next->hash, hash, 32 );
        p_next->hash[32] = '\0';
        p_next->code = code;
        if( p_drcs_conv == NULL )
        {
            p_sys->p_drcs_conv = p_next;
        }
        else
        {
            p_drcs_conv->p_next = p_next;
        }
        p_drcs_conv = p_next;
        p_drcs_conv->p_next = NULL;
    }

    fclose( fp );

}

static FILE* open_image_file( decoder_t* p_dec, const char *psz_hash )
{
    FILE* fp = NULL;
    create_arib_datadir( p_dec );

    char *psz_arib_data_dir = get_arib_data_dir( p_dec );
    if( psz_arib_data_dir == NULL )
    {
        return NULL;
    }

    char* psz_image_file;
    if( asprintf( &psz_image_file, "%s"DIR_SEP"%s.png", psz_arib_data_dir, psz_hash ) < 0 )
    {
        psz_image_file = NULL;
    }
    free( psz_arib_data_dir );
    if( psz_image_file == NULL )
    {
        return NULL;
    }

    struct stat st;
    vlc_stat( psz_image_file, &st );
    if( !S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) )
    {
        fp = vlc_fopen( psz_image_file, "wb" );
        if( fp == NULL )
        {
            // ERROR
        }
    }

    free( psz_image_file );
    return fp;
}

static char* get_drcs_pattern_data_hash(
        decoder_t *p_dec,
        int i_width, int i_height,
        int i_depth, const int8_t* p_patternData )
{
    VLC_UNUSED(p_dec);
    int i_bits_per_pixel = ceil( sqrt( ( i_depth ) ) );
    struct md5_s md5;
    InitMD5( &md5 );
    AddMD5( &md5, p_patternData, i_width * i_height * i_bits_per_pixel / 8 );
    EndMD5( &md5 );
    return psz_md5_hash( &md5 );
}

static void save_drcs_pattern_data_image(
        decoder_t *p_dec,
        const char* psz_hash,
        int i_width, int i_height,
        int i_depth, const int8_t* p_patternData )
{
    FILE *fp = open_image_file( p_dec, psz_hash );
    if( fp == NULL )
    {
        return;
    }

    png_structp png_ptr = png_create_write_struct(
            PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
    if( png_ptr == NULL )
    {
        goto png_create_write_struct_failed;
    }
    png_infop info_ptr = png_create_info_struct( png_ptr );
    if( info_ptr == NULL )
    {
        goto png_create_info_struct_failed;
    }

    if( setjmp( png_jmpbuf( png_ptr ) ) )
    {
        goto png_failure;
    }

    png_set_IHDR( png_ptr,
            info_ptr,
            i_width,
            i_height,
            1,
            PNG_COLOR_TYPE_PALETTE,
            PNG_INTERLACE_NONE,
            PNG_COMPRESSION_TYPE_DEFAULT,
            PNG_FILTER_TYPE_DEFAULT );

    png_bytepp pp_image;
    pp_image = png_malloc( png_ptr, i_height * sizeof(png_bytep) );
    for( int j = 0; j < i_height; j++ )
    {
        pp_image[j] = png_malloc( png_ptr, i_width * sizeof(png_byte) );
    }


    int i_bits_per_pixel = ceil( sqrt( ( i_depth ) ) );

    bs_t bs;
    bs_init( &bs, p_patternData, i_width * i_height * i_bits_per_pixel / 8 );

    for( int j = 0; j < i_height; j++ )
    {
        for( int i = 0; i < i_width; i++ )
        {
            uint8_t i_pxl = bs_read( &bs, i_bits_per_pixel );
            pp_image[j][i] = i_pxl ? 1 : 0;
        }
    }

    png_byte trans_values[1];
    trans_values[0] = (png_byte)0;
    png_set_tRNS( png_ptr, info_ptr, trans_values, 1, NULL );

    int colors[][3] =
    {
        {255, 255, 255}, /* background */
        {  0,   0,   0}, /* foreground */
    };
    png_color palette[2];
    for( int i = 0; i < 2; i++ )
    {
        palette[i].red = colors[i][0];
        palette[i].green = colors[i][1];
        palette[i].blue = colors[i][2];
    }
    png_set_PLTE( png_ptr, info_ptr, palette, 2 );

    png_init_io( png_ptr, fp );
    png_write_info( png_ptr, info_ptr );
    png_set_packing( png_ptr );
    png_write_image( png_ptr, pp_image );
    png_write_end( png_ptr, info_ptr );

    for( int j = 0; j < i_height; j++ )
    {
        png_free( png_ptr, pp_image[j] );
    }
    png_free( png_ptr, pp_image );

png_failure:
png_create_info_struct_failed:
    png_destroy_write_struct( &png_ptr, &info_ptr );
png_create_write_struct_failed:
    fclose( fp );
}

static void save_drcs_pattern(
        decoder_t *p_dec,
        int i_width, int i_height,
        int i_depth, const int8_t* p_patternData )
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    drcs_conversion_t *p_drcs_conv;
    bool found;

/* XXX broken data ? */
if (i_height == 0 || i_width == 0) return;

    char* psz_hash = get_drcs_pattern_data_hash( p_dec,
            i_width, i_height, i_depth, p_patternData );

    found = false;
    // has convert table?
    p_drcs_conv = p_sys->p_drcs_conv;
    while( p_drcs_conv != NULL && p_drcs_conv->hash) 
    {
        if (strcmp( p_drcs_conv->hash,psz_hash) == 0 )
        {
            found = true;
            break;
        }
        p_drcs_conv = p_drcs_conv->p_next;
    }
    // already saved?
    if (!found) {
        for(int i=0;i<10;i++) {
            if (strcmp(p_sys->drcs_hash_table[i],psz_hash) == 0)
            {
                found = true;
                break;
            }
        }
    }

    strncpy( p_sys->drcs_hash_table[p_sys->i_drcs_num], psz_hash, 32 );
    p_sys->drcs_hash_table[p_sys->i_drcs_num][32] = '\0';

    p_sys->i_drcs_num++;

    if (!found)
    {
        save_drcs_pattern_data_image( p_dec, psz_hash,
                i_width, i_height, i_depth, p_patternData );
    }

    free( psz_hash );
}

static void parse_data_unit_staement_body( decoder_t *p_dec,
        uint8_t i_data_unit_parameter,
        uint32_t i_data_unit_size )
{
    VLC_UNUSED(i_data_unit_parameter);
    decoder_sys_t *p_sys = p_dec->p_sys;

    char* p_data_unit_data_byte = (char*) malloc(
            sizeof(char) * (i_data_unit_size + 1) );
    if( p_data_unit_data_byte == NULL )
    {
        return;
    }
    for( uint32_t i = 0; i < i_data_unit_size; i++ )
    {
        p_data_unit_data_byte[i] = bs_read( &p_sys->bs, 8 );
        p_sys->i_data_unit_size += 1;
    }
    p_data_unit_data_byte[i_data_unit_size] = '\0';

    memcpy( p_sys->psz_subtitle_data + p_sys->i_subtitle_data_size,
            p_data_unit_data_byte, i_data_unit_size );
    p_sys->i_subtitle_data_size += i_data_unit_size;

    free( p_data_unit_data_byte );
}

static void parse_data_unit_DRCS( decoder_t *p_dec,
        uint8_t i_data_unit_parameter,
        uint32_t i_data_unit_size )
{
    VLC_UNUSED(i_data_unit_parameter); VLC_UNUSED(i_data_unit_size);
    decoder_sys_t *p_sys = p_dec->p_sys;

#ifdef ARIBSUB_GEN_DRCS_DATA
    if( p_sys->p_drcs_data != NULL )
    {
        for( int i = 0; i < p_sys->p_drcs_data->i_NumberOfCode; i++ )
        {
            drcs_code_t* p_drcs_code = &p_sys->p_drcs_data->p_drcs_code[i];
            for( int j = 0; j < p_drcs_code->i_NumberOfFont ; j++ )
            {
                drcs_font_data_t *p_drcs_font_data =  &p_drcs_code->p_drcs_font_data[j];
                free( p_drcs_font_data->p_drcs_pattern_data );
                free( p_drcs_font_data->p_drcs_geometric_data );
            }
            free( p_drcs_code->p_drcs_font_data );
        }
        free( p_sys->p_drcs_data->p_drcs_code );
        free( p_sys->p_drcs_data );
    }
    p_sys->p_drcs_data = (drcs_data_t*) malloc( sizeof(drcs_data_t) );
    if( p_sys->p_drcs_data == NULL )
    {
        return;
    }
#endif //ARIBSUB_GEN_DRCS_DATA

    int8_t i_NumberOfCode = bs_read( &p_sys->bs, 8 );
    p_sys->i_data_unit_size += 1;

#ifdef ARIBSUB_GEN_DRCS_DATA
    p_sys->p_drcs_data->i_NumberOfCode = i_NumberOfCode;
    p_sys->p_drcs_data->p_drcs_code = (drcs_code_t*) malloc(
            (sizeof(drcs_code_t) * i_NumberOfCode ) );
    if( p_sys->p_drcs_data->p_drcs_code == NULL )
    {
        return;
    }
#endif //ARIBSUB_GEN_DRCS_DATA

    for( int i = 0; i < i_NumberOfCode; i++ )
    {
        uint16_t i_CharacterCode = bs_read( &p_sys->bs, 16 );
        p_sys->i_data_unit_size += 2;
        uint8_t i_NumberOfFont = bs_read( &p_sys->bs, 8 );
        p_sys->i_data_unit_size += 1;

#ifdef ARIBSUB_GEN_DRCS_DATA
        drcs_code_t *p_drcs_code = &p_sys->p_drcs_data->p_drcs_code[i];
        p_drcs_code->i_CharacterCode = i_CharacterCode;
        p_drcs_code->i_NumberOfFont = i_NumberOfFont;
        p_drcs_code->p_drcs_font_data = (drcs_font_data_t*) malloc(
                (sizeof(drcs_font_data_t) * i_NumberOfFont ) );
        if( p_drcs_code->p_drcs_font_data == NULL )
        {
            return;
        }
#else
        VLC_UNUSED(i_CharacterCode);
#endif //ARIBSUB_GEN_DRCS_DATA

        for( int j = 0; j < i_NumberOfFont; j++ )
        {
            uint8_t i_fontId = bs_read( &p_sys->bs, 4 );
            uint8_t i_mode = bs_read( &p_sys->bs, 4 );
            p_sys->i_data_unit_size += 1;

#ifdef ARIBSUB_GEN_DRCS_DATA
            drcs_font_data_t* p_drcs_font_data =
                &p_drcs_code->p_drcs_font_data[j];
            p_drcs_font_data->i_fontId = i_fontId;
            p_drcs_font_data->i_mode = i_mode;
            p_drcs_font_data->p_drcs_pattern_data = NULL;
            p_drcs_font_data->p_drcs_geometric_data = NULL;
#else
            VLC_UNUSED(i_fontId);
#endif //ARIBSUB_GEN_DRCS_DATA

            if( i_mode == 0x00 || i_mode == 0x01 )
            {
                uint8_t i_depth = bs_read( &p_sys->bs, 8 );
                p_sys->i_data_unit_size += 1;
                uint8_t i_width = bs_read( &p_sys->bs, 8 );
                p_sys->i_data_unit_size += 1;
                uint8_t i_height = bs_read( &p_sys->bs, 8 );
                p_sys->i_data_unit_size += 1;

                int i_bits_per_pixel = ceil( sqrt( ( i_depth + 2 ) ) );

#ifdef ARIBSUB_GEN_DRCS_DATA
                drcs_pattern_data_t* p_drcs_pattern_data =
                    p_drcs_font_data->p_drcs_pattern_data =
                    (drcs_pattern_data_t*) malloc(
                            sizeof(drcs_pattern_data_t) );
                if( p_drcs_pattern_data == NULL )
                {
                    return;
                }
                p_drcs_pattern_data->i_depth = i_depth;
                p_drcs_pattern_data->i_width = i_width;
                p_drcs_pattern_data->i_height = i_height;
                p_drcs_pattern_data->p_patternData =
                    (int8_t*) malloc( sizeof(int8_t) *
                            i_width * i_height * i_bits_per_pixel / 8 );
                if( p_drcs_pattern_data->p_patternData == NULL )
                {
                    return;
                }
#else
                int8_t *p_patternData =
                    (int8_t*) malloc( sizeof(int8_t) *
                            i_width * i_height * i_bits_per_pixel / 8 );
                if( p_patternData == NULL )
                {
                    return;
                }
#endif //ARIBSUB_GEN_DRCS_DATA

                for( int k = 0; k < i_width * i_height * i_bits_per_pixel / 8; k++ )
                {
                    uint8_t i_patternData = bs_read( &p_sys->bs, 8 );
                    p_sys->i_data_unit_size += 1;
#ifdef ARIBSUB_GEN_DRCS_DATA
                    p_drcs_pattern_data->p_patternData[k] = i_patternData;
#else
                    p_patternData[k] = i_patternData;
#endif //ARIBSUB_GEN_DRCS_DATA
                }

#ifdef ARIBSUB_GEN_DRCS_DATA
                save_drcs_pattern( p_dec, i_width, i_height, i_depth + 2,
                        p_drcs_pattern_data->p_patternData );
#else
                save_drcs_pattern( p_dec, i_width, i_height, i_depth + 2,
                        p_patternData );
                free( p_patternData );
#endif //ARIBSUB_GEN_DRCS_DATA
            }
            else
            {
                uint8_t i_regionX = bs_read( &p_sys->bs, 8 );
                p_sys->i_data_unit_size += 1;
                uint8_t i_regionY = bs_read( &p_sys->bs, 8 );
                p_sys->i_data_unit_size += 1;
                uint16_t i_geometricData_length = bs_read( &p_sys->bs, 16 );
                p_sys->i_data_unit_size += 2;

#ifdef ARIBSUB_GEN_DRCS_DATA
                drcs_geometric_data_t* p_drcs_geometric_data =
                    p_drcs_font_data->p_drcs_geometric_data =
                    (drcs_geometric_data_t*) malloc(
                            sizeof(drcs_geometric_data_t) );
                if( p_drcs_geometric_data == NULL )
                {
                    return;
                }
                p_drcs_geometric_data->i_regionX = i_regionX;
                p_drcs_geometric_data->i_regionY = i_regionY;
                p_drcs_geometric_data->i_geometricData_length = i_geometricData_length;
                p_drcs_geometric_data->p_geometricData = (int8_t*)
                    malloc( sizeof(int8_t) * i_geometricData_length );
                if( p_drcs_geometric_data->p_geometricData == NULL )
                {
                    return;
                }
#else
                VLC_UNUSED(i_regionX);
                VLC_UNUSED(i_regionY);
#endif //ARIBSUB_GEN_DRCS_DATA

                for( int k = 0; k < i_geometricData_length ; k++ )
                {
                    uint8_t i_geometricData = bs_read( &p_sys->bs, 8 );
                    p_sys->i_data_unit_size += 1;

#ifdef ARIBSUB_GEN_DRCS_DATA
                    p_drcs_geometric_data->p_geometricData[k] = i_geometricData;
#else
                    VLC_UNUSED(i_geometricData);
#endif //ARIBSUB_GEN_DRCS_DATA
                }
            }
        }
    }
}

static void parse_data_unit_others( decoder_t *p_dec,
        uint8_t i_data_unit_parameter,
        uint32_t i_data_unit_size )
{
    VLC_UNUSED(i_data_unit_parameter);
    decoder_sys_t *p_sys = p_dec->p_sys;

    for( uint32_t i = 0; i < i_data_unit_size; i++ )
    {
        bs_skip( &p_sys->bs, 8 );
        p_sys->i_data_unit_size += 1;
    }
}

/*****************************************************************************
 * parse_data_unit
 *****************************************************************************
 * ARIB STD-B24 VOLUME 1 Part 3 Chapter 9.4 Structure of data unit
 *****************************************************************************/
static void parse_data_unit( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    uint8_t i_unit_separator = bs_read( &p_sys->bs, 8 );
    p_sys->i_data_unit_size += 1;
    if( i_unit_separator != 0x1F )
    {
        //msg_Err( p_dec, "i_unit_separator != 0x1F: [%x]", i_unit_separator );
        return;
    }
    uint8_t i_data_unit_parameter = bs_read( &p_sys->bs, 8 );
    p_sys->i_data_unit_size += 1;
    uint32_t i_data_unit_size = bs_read( &p_sys->bs, 24 );
    p_sys->i_data_unit_size += 3;
    if( i_data_unit_parameter == 0x20 )
    {
        parse_data_unit_staement_body( p_dec,
                i_data_unit_parameter,
                i_data_unit_size );
    }
    else if( i_data_unit_parameter == 0x30 ||
            i_data_unit_parameter == 0x31 )
    {
        parse_data_unit_DRCS( p_dec,
                i_data_unit_parameter,
                i_data_unit_size );
    }
    else
    {
        parse_data_unit_others( p_dec,
                i_data_unit_parameter,
                i_data_unit_size );
    }
}

/*****************************************************************************
 * parse_caption_management_data
 *****************************************************************************
 * ARIB STD-B24 VOLUME 1 Part 3 Chapter 9.3.1 Caption management data
 *****************************************************************************/
static void parse_caption_management_data( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    uint8_t i_TMD = bs_read( &p_sys->bs, 2 );
    bs_skip( &p_sys->bs, 6 ); /* Reserved */
    if( i_TMD == 0x02 /* 10 */ )
    {
        uint64_t i_OTM = ((uint64_t)bs_read( &p_sys->bs, 32 ) << 4) & bs_read( &p_sys->bs, 4 );
        VLC_UNUSED(i_OTM);
        bs_skip( &p_sys->bs, 4 ); /* Reserved */
    }
    uint8_t i_num_languages = bs_read( &p_sys->bs, 8 );
    for( int i = 0; i < i_num_languages; i++ )
    {
        uint8_t i_language_tag = bs_read( &p_sys->bs, 3 );
        VLC_UNUSED(i_language_tag);
        bs_skip( &p_sys->bs, 1 ); /* Reserved */
        uint8_t i_DMF = bs_read( &p_sys->bs, 4 );
        if( i_DMF == 0x0C /* 1100 */ ||
                i_DMF == 0x0D /* 1101 */ ||
                i_DMF == 0x0E /* 1110 */ )
        {
            uint8_t i_DC = bs_read( &p_sys->bs, 8 );
            VLC_UNUSED(i_DC);
        }
        uint32_t i_ISO_639_language_code = bs_read( &p_sys->bs, 24 );
        VLC_UNUSED(i_ISO_639_language_code);
        uint8_t i_Format = bs_read( &p_sys->bs, 4 );
        VLC_UNUSED(i_Format);
        uint8_t i_TCS = bs_read( &p_sys->bs, 2 );
        VLC_UNUSED(i_TCS);
        uint8_t i_rollup_mode = bs_read( &p_sys->bs, 2 );
        VLC_UNUSED(i_rollup_mode);
    }
    uint32_t i_data_unit_loop_length = bs_read( &p_sys->bs, 24 );
    free( p_sys->psz_subtitle_data );
    p_sys->i_data_unit_size = 0;
    p_sys->i_subtitle_data_size = 0;
    p_sys->psz_subtitle_data = NULL;
    if( i_data_unit_loop_length > 0 )
    {
        p_sys->psz_subtitle_data = (unsigned char*)
            malloc( sizeof(unsigned char) *
                    (i_data_unit_loop_length + 1) );
    }
    while( p_sys->i_data_unit_size < i_data_unit_loop_length )
    {
        parse_data_unit( p_dec );
    }
}

/*****************************************************************************
 * parse_caption_data
 *****************************************************************************
 * ARIB STD-B24 VOLUME 1 Part 3 Chapter 9.3.2 Caption statement data
 *****************************************************************************/
static void parse_caption_statement_data( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    uint8_t i_TMD = bs_read( &p_sys->bs, 2 );
    bs_skip( &p_sys->bs, 6 ); /* Reserved */
    if( i_TMD == 0x01 /* 01 */ || i_TMD == 0x02 /* 10 */ )
    {
        uint64_t i_STM = ((uint64_t) bs_read( &p_sys->bs, 32 ) << 4) &
            bs_read( &p_sys->bs, 4 );
        VLC_UNUSED(i_STM);
        bs_skip( &p_sys->bs, 4 ); /* Reserved */
    }
    uint32_t i_data_unit_loop_length = bs_read( &p_sys->bs, 24 );
    free( p_sys->psz_subtitle_data );
    p_sys->i_subtitle_data_size = 0;
    p_sys->psz_subtitle_data = NULL;
    if( i_data_unit_loop_length > 0 )
    {
        p_sys->psz_subtitle_data =
            (unsigned char*)malloc( sizeof(unsigned char) *
                    (i_data_unit_loop_length + 1) );
    }
    while( p_sys->i_data_unit_size < i_data_unit_loop_length )
    {
        parse_data_unit( p_dec );
    }
}

/*****************************************************************************
 * parse_data_group
 *****************************************************************************
 * ARIB STD-B24 VOLUME 1 Part 3 Chapter 9.2 Structure of data group 
 *****************************************************************************/
static void parse_data_group( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    uint8_t i_data_group_id = bs_read( &p_sys->bs, 6 );
    uint8_t i_data_group_version = bs_read( &p_sys->bs, 2 );
    VLC_UNUSED(i_data_group_version);
    uint8_t i_data_group_link_number = bs_read( &p_sys->bs, 8 );
    VLC_UNUSED(i_data_group_link_number);
    uint8_t i_last_data_group_link_number = bs_read( &p_sys->bs, 8 );
    VLC_UNUSED(i_last_data_group_link_number);
    uint16_t i_data_group_size = bs_read( &p_sys->bs, 16 );
    VLC_UNUSED(i_data_group_size);

    if( i_data_group_id == 0x00 || i_data_group_id == 0x20 )
    {
        parse_caption_management_data( p_dec );
    }
    else
    {
        parse_caption_statement_data( p_dec );
    }
}

/*****************************************************************************
 * parse_arib_pes
 *****************************************************************************
 * ARIB STD-B24 VOLUME3 Chapter 5 Independent PES transmission protocol 
 *****************************************************************************/
static void parse_arib_pes( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    uint8_t i_data_group_id = bs_read( &p_sys->bs, 8 );
    if( i_data_group_id != 0x80 && i_data_group_id != 0x81 )
    {
        //msg_Err( p_dec, "parse_arib_pes: i_data_group_id is invalid.[%x]", i_data_group_id );
        return;
    }
    uint8_t i_private_stream_id = bs_read( &p_sys->bs, 8 );
    if( i_private_stream_id != 0xFF )
    {
        //msg_Err( p_dec, "parse_arib_pes: i_private_stream_id is invalid.[%x]", i_private_stream_id );
        return;
    }
    uint8_t i_reserved_future_use = bs_read( &p_sys->bs, 4 );
    VLC_UNUSED(i_reserved_future_use);
    uint8_t i_PES_data_packet_header_length= bs_read( &p_sys->bs, 4 );

    /* skip PES_data_private_data_byte */
    bs_skip( &p_sys->bs, i_PES_data_packet_header_length );

    parse_data_group( p_dec );
}

static bool isallspace(char *buf,int len)
{
    int i;
    for(i=0;i<len;i++) {
        if (buf[i]!=0x20) return false;
    }
    return true;
}
static void dumpheader(decoder_t *p_dec)
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    char buf[1024];
    if (p_sys->outputfile) {
        p_sys->outputfp = vlc_fopen(p_sys->outputfile,"w");
        if (p_sys->outputfp == NULL) {
            fprintf(stderr,"output [%s] can't open output to stdout\n",p_sys->outputfile);
            p_sys->outputfp = stdout;
        }
    }
    else {
        char *outputfile;
        asprintf(&outputfile,"%s.ass",p_sys->inputfile);
        p_sys->outputfp = vlc_fopen(outputfile,"w");
        if (p_sys->outputfp == NULL) {
            fprintf(stderr,"output [%s] can't open output to stdout\n",outputfile);
            p_sys->outputfp = stdout;
        }
        free(outputfile);
    }
    FILE *fp = vlc_fopen( "assheader.ini", "r" );
    if (fp == NULL) {
        fprintf(p_sys->outputfp, "[Script Info]\n; Script generated by Aegisub v2.1.2 RELEASE PREVIEW (SVN r1987, amz)\n; http://www.aegisub.net\n;\n");
        fprintf(p_sys->outputfp, "; http://www.aegisub.net\nScriptType: v4.00+\nWrapStyle: 0\nPlayResX: 960\nPlayResY: 540\nScaledBorderAndShadow: yes\nVideo Aspect Ratio: 0\nVideo Zoom: 6\nVideo Position: 0\n\n");
        fprintf(p_sys->outputfp, "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n");
        fprintf(p_sys->outputfp, "Style: Default,和田研中丸ゴシック2004絵文字,36,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,4,0,1,2,2,7,0,0,0,0\n");
        fprintf(p_sys->outputfp, "Style:    Half,和田研中丸ゴシック2004絵文字,36,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,50,100,4,0,1,2,2,7,0,0,0,0\n");
        fprintf(p_sys->outputfp, "Style:    Rubi,和田研中丸ゴシック2004絵文字,18,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,2,0,1,2,2,7,0,0,0,0\n");
        fprintf(p_sys->outputfp, "Style:     HLC,Arial,20,&HFF000000,&H00000000,&H00FFFFFF,&H80000000,0,0,0,0,100,100,0,0.00,1,2,0,2,0,0,0,1\n");
        fprintf(p_sys->outputfp, ";Style: Default,メイリオARIB,55,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,4,0,1,2,2,7,0,0,0,0\n");
        fprintf(p_sys->outputfp, ";Style:    Half,メイリオARIB,55,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,50,100,4,0,1,2,2,7,0,0,0,0\n");
        fprintf(p_sys->outputfp, ";Style:    Rubi,メイリオARIB,28,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,2,0,1,2,2,7,0,0,0,0\n");
        fprintf(p_sys->outputfp, "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        return;
    }
    while( fgets( buf, 1024, fp ) != 0 )
    {
        fprintf(p_sys->outputfp, "%s",buf);
    }
    fclose( fp );
}
static void dumpregion(decoder_t *p_dec,ass_region_buf_t *ass,mtime_t i_stop)
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    static bool dumpstart = true;
    char *p2,*p3;
    char *p_start,*p_stop,*pb_start,*pb_stop;
    mtime_t i,i_dur,i_int,i_off;
    i_int = 135000; // ブリンク間隔(1.5秒)
    i_dur = i_int / 2;  //表示時間
    if (dumpstart) {
        dumpheader(p_dec);
        dumpstart=false;
    }
    p_start =dumpts(ass->i_start);
    p_stop = dumpts(i_stop);
    for(ass_region_buf_t *p = ass;p;p = p->p_next) {
        if (p->f_blink > 0) {
            // f_blink = 2 の場合消灯からスタート
            if (p->f_blink == 1) i_off = 0;
            else i_off = i_dur;
            //表示を複数回に分けてBLINKするように見せかける
            for(i=ass->i_start;i<i_stop-i_off;i=i+i_int) {
                pb_start = dumpts(i+i_off);
                if (i_stop < i+i_dur+i_off) {
                    pb_stop = dumpts(i_stop);
                }
                else {
                    pb_stop  = dumpts(i+i_dur+i_off);
                }
                asprintf(&p2,p->p_buf,pb_start,pb_stop);
                fprintf(p_sys->outputfp, "%s",p2);
                free(p2);
                free(pb_start);
                free(pb_stop);
            }
        }
        else {
            asprintf(&p2,p->p_buf,p_start,p_stop);
            fprintf(p_sys->outputfp, "%s",p2);
            free(p2);
        }
        free(p->p_buf);
        //free(p);
    }
}
static void free_assregion( ass_region_buf_t *p_ass)
{
    ass_region_buf_t *asstmp;
    asstmp = p_ass;
    if (asstmp->p_next == NULL)
    {
        free(asstmp);
    }
    else
    {
        while(asstmp->p_next)
        {
            ass_region_buf_t *p;
            p = asstmp;
            asstmp = asstmp->p_next;
            free(p);
        }
    }
}
static void pushregion(decoder_t  *p_dec,mtime_t i_start,mtime_t i_stop)
{
    arib_buf_region_t *p_region = p_dec->p_sys->arib_decoder.p_region;
    static ass_region_buf_t *ass=NULL;
    ass_region_buf_t *asstmp;
    char *p3,*p4,*style;
    char tmp[512];
    mtime_t tswk;
    if (ass) {
        dumpregion(p_dec,ass,i_start);
        free_assregion(ass);
        ass = NULL;
    }
    asstmp = NULL;
    for(arib_buf_region_t *p_buf_region = p_region;p_buf_region;p_buf_region = p_buf_region->p_next) {
        int i_size = p_buf_region->p_end - p_buf_region->p_start;

        strncpy(tmp,p_buf_region->p_start,i_size);
        tmp[i_size]=0;
        // skip space
        if ((i_size == 0) || (i_size == 3 && strcmp(tmp,"　")==0) || isallspace(tmp,i_size))
        {
            continue;
        }
        if (p_buf_region->i_fontheight == 18) {
            style = "Rubi";
        }
        else {
            if (p_buf_region->i_fontwidth == 36)
                style = "Default";
            else
                style = "Half";
        }
	if (p_buf_region->i_fontheight == 0 && p_buf_region->i_fontheight == 0)
	{
		style = "HLC";
	}
        if (p_buf_region->i_foreground_color == 0xffffff) {
            p4 = NULL;
            asprintf(&p3,"Dialogue: 0,%%s,%%s,%s,,0000,0000,0000,,{\\pos(%d,%d)}%s\n",
                    style,
                    p_buf_region->i_charleft - (p_buf_region->i_fontwidth + p_buf_region->i_horint) ,
                    p_buf_region->i_charbottom - (p_buf_region->i_fontheight + p_buf_region->i_verint),
                    tmp);
        }
        else {
            asprintf(&p3,"Dialogue: 0,%%s,%%s,%s,,0000,0000,0000,,{\\pos(%d,%d)\\c&H%06x&}%s\n",
                    style,
                    p_buf_region->i_charleft - (p_buf_region->i_fontwidth + p_buf_region->i_horint) ,
                    p_buf_region->i_charbottom - (p_buf_region->i_fontheight + p_buf_region->i_verint),
                    p_buf_region->i_foreground_color,tmp);
        }
        if (ass == NULL) {
            asstmp = ass = calloc(1,sizeof(ass_region_buf_t));
        }
        else {
            if (asstmp == NULL) {
                ass_region_buf_t *p;
                for(p = ass;p;p = p->p_next)
                    asstmp = p;
            }
            asstmp->p_next = calloc(1,sizeof(ass_region_buf_t));
            asstmp = asstmp->p_next;
        }
        asstmp->p_buf = strdup(p3);
#ifdef ADD_FLC_SUPPORT
        asstmp->f_blink = p_buf_region->i_blink;
#endif
        asstmp->i_start = i_start;
        free(p3);
        //if (p4) free(p4);
    }
    if (i_start != i_stop && ass) {
        dumpregion(p_dec,ass,i_stop);
        free_assregion(ass);
        ass = NULL;
    }
}
static void dumparib(decoder_t *p_dec,mtime_t i_pts)
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    int retlen;
    char *tostr;
    mtime_t i_stop;
    static mtime_t i_prev_pts;
    static arib_decoder_t *prev_decoder=NULL;

    if (p_sys->psz_subtitle_data == NULL) return;

    tostr = malloc((p_sys->i_subtitle_data_size*3)+1);
    tostr[0]=0;
    arib_initialize_decoder(&p_sys->arib_decoder,1);

    p_sys->arib_decoder.i_drcs_num = p_sys->i_drcs_num;

    for( int i = 0; i < p_sys->i_drcs_num; i++ )
    {
        strncpy( p_sys->arib_decoder.drcs_hash_table[i],
                p_sys->drcs_hash_table[i], 32 );
        p_sys->arib_decoder.drcs_hash_table[i][32] = '\0';
    }
    p_sys->arib_decoder.p_drcs_conv = p_sys->p_drcs_conv;

    retlen = arib_decode_buffer( &p_sys->arib_decoder,
            p_sys->psz_subtitle_data,
            p_sys->i_subtitle_data_size,
            tostr,
            p_sys->i_subtitle_data_size*3 );
    if (retlen > 0) tostr[retlen]=0;
    //i_stop = i_pts + (int64_t)(p_sys->arib_decoder.i_control_time * 1000 * 90 );
    i_stop = i_pts + (int64_t)(p_sys->arib_decoder.i_control_time * 1000 * 9);

    if (p_sys->arib_decoder.p_region && p_sys->debugfp) {
        const unsigned char* start = (const unsigned char*)p_sys->psz_subtitle_data;
        const unsigned char* end = (const unsigned char*)p_sys->psz_subtitle_data + p_sys->i_subtitle_data_size;
        char *dumpdata,*pts,dumpwk[10];
        dumpdata = malloc((p_sys->i_subtitle_data_size * 4)+1);
        dumpdata[0]=NULL;
        while( start < end )
        {
            sprintf(dumpwk,"%02x ",*start++);
            strcat(dumpdata,dumpwk);
        }
        pts = dumpts(i_pts);
        fprintf(p_sys->debugfp,"pts[%s] control_time [%dms] str [%s]\n",pts,p_sys->arib_decoder.i_control_time*100,tostr);
        fprintf(p_sys->debugfp,"dmp [%s]\n",dumpdata);

        free(dumpdata);
        free(pts);
    }

#ifdef ADD_HLC_SUPPORT
    if (p_sys->arib_decoder.p_hlcregion) {
        arib_buf_region_t *p_region = p_dec->p_sys->arib_decoder.p_region;
        while(p_region) {
            if (p_region->p_next == NULL) break;
            p_region = p_region->p_next;
        }
        // insert tail
        p_region->p_next = p_sys->arib_decoder.p_hlcregion;
    }
#endif
    if (p_sys->arib_decoder.p_region) {
        pushregion(p_dec,i_pts,i_stop);
    }

    arib_finalize_decoder(&p_sys->arib_decoder);

#ifdef ADD_HLC_SUPPORT
    if (p_sys->arib_decoder.p_hlcregion) {
	    free(p_sys->arib_decoder.p_hlcbuf);
    }
#endif

    p_sys->i_drcs_num = 0;

    if (tostr) free(tostr);

}

