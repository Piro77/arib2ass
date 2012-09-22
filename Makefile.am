## Process this file with automake to produce Makefile.in

bin_PROGRAMS = arib2ass

arib2ass_SOURCES = arib2ass.c aribsub.c md5.c asprintf.c
arib2ass_LDFLAGS = -L../src -ldvbpsi -lm -lpng
arib2ass_CFLAGS = -std=c99

noinst_HEADERS = common.h aribb24dec.h vlc_bits.h vlc_md5.h
