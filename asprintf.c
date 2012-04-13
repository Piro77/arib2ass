#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef _WIN32

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

int asprintf( char **, char *, ... );
int vasprintf( char **, char *, va_list );

int vasprintf( char **sptr, char *fmt, va_list argv )
{
    int wanted = vsnprintf( *sptr = NULL, 0, fmt, argv );
    if( (wanted < 0) || ((*sptr = malloc( 1 + wanted )) == NULL) )
        return -1;

    return vsprintf( *sptr, fmt, argv );
}

int asprintf( char **sptr, char *fmt, ... )
{
    int retval;
    va_list argv;
    va_start( argv, fmt );
    retval = vasprintf( sptr, fmt, argv );
    va_end( argv );
    return retval;
}
#endif
