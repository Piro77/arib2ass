#ifndef COMMON_H
# define COMMON_H

#include "config.h"

#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#elif defined(HAVE_STDINT_H)
#include <stdint.h>
#endif

#if defined(HAVE_STRINGS_H)
#include <string.h>
#endif

#define VLC_UNUSED(x) (void)(x)
#define PRIx8	"x"
#define	vlc_fopen	fopen
#define	vlc_stat	stat
#define	vlc_mkdir	mkdir

#if defined( WIN32 ) || defined( UNDER_CE ) || defined( __SYMBIAN32__ ) || defined( __OS2__ )
#   define DIR_SEP_CHAR '\\'
#   define DIR_SEP "\\"
#   define PATH_SEP_CHAR ';'
#   define PATH_SEP ";"
#else
#   define DIR_SEP_CHAR '/'
#   define DIR_SEP "/"
#   define PATH_SEP_CHAR ':'
#   define PATH_SEP ":"
#endif

typedef int64_t mtime_t;

#define VLC_FALSE 0
#define VLC_TRUE  1

typedef struct block_t      block_t;

struct block_t
{
    uint8_t     *p_buffer;
    size_t      i_buffer;
    mtime_t     i_pts;
    mtime_t     i_dts;
    mtime_t     i_length;
};

typedef struct decoder_sys_t     decoder_sys_t;

typedef struct {
  void  *      ( * pf_decode_sub)   ( void  *, block_t **);
  decoder_sys_t *p_sys;
}decoder_t;

static char * dumpts(mtime_t ts)
{
        int sec,min,hour;
        char *buf;
        sec = ts / 90000;
        ts -= sec * 90000;
        min = sec / 60;
        sec -= min * 60;
        hour = min / 60;
        min -= hour * 60;
        asprintf(&buf,"%02d:%02d:%02d.%03d", hour, min, sec, ts/90);
        return buf;
}


#endif
