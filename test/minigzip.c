/* minigzip.c -- simulate gzip using the zlib compression library
 * Copyright (C) 1995-2006, 2010, 2011, 2016 Jean-loup Gailly
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/*
 * minigzip is a minimal implementation of the gzip utility. This is
 * only an example of using zlib and isn't meant to replace the
 * full-featured gzip. No attempt is made to deal with file systems
 * limiting names to 14 or 8+3 characters, etc... Error checking is
 * very limited. So use minigzip only for testing; use gzip for the
 * real thing.
 */

/* @(#) $Id$ */

#define _POSIX_SOURCE 1  /* This file needs POSIX for fdopen(). */
#define _POSIX_C_SOURCE 200112  /* For snprintf(). */

#include "zbuild.h"
#ifdef ZLIB_COMPAT
# include "zlib.h"
#else
# include "zlib-ng.h"
#endif
#include <stdio.h>

#include <string.h>
#include <stdlib.h>

#ifdef USE_MMAP
#  include <sys/types.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#endif

#ifndef UNALIGNED_OK
#  include <malloc.h>
#endif

#if defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#  define snprintf _snprintf
#endif

#if !defined(Z_HAVE_UNISTD_H) && !defined(_LARGEFILE64_SOURCE)
#ifndef WIN32 /* unlink already in stdio.h for WIN32 */
  extern int unlink (const char *);
#endif
#endif

#ifndef GZ_SUFFIX
#  define GZ_SUFFIX ".gz"
#endif
#define SUFFIX_LEN (sizeof(GZ_SUFFIX)-1)

#if defined(S390_DFLTCC_DEFLATE) || defined(S390_DFLTCC_INFLATE)
#define BUFLEN      262144       /* DFLTCC works faster with larger buffers */
#else
#define BUFLEN      16384        /* read buffer size */
#endif
#define BUFLENW     (BUFLEN * 3) /* write buffer size */
#define MAX_NAME_LEN 1024

static char *prog;

void error            (const char *msg);
void gz_compress      (FILE   *in, gzFile out);
#ifdef USE_MMAP
int  gz_compress_mmap (FILE   *in, gzFile out);
#endif
void gz_uncompress    (gzFile in, FILE   *out);
void file_compress    (char  *file, char *mode);
void file_uncompress  (char  *file);
int  main             (int argc, char *argv[]);

/* ===========================================================================
 * Display error message and exit
 */
void error(const char *msg)
{
    fprintf(stderr, "%s: %s\n", prog, msg);
    exit(1);
}

/* ===========================================================================
 * Compress input to output then close both files.
 */

void gz_compress(FILE   *in, gzFile out)
{
    char buf[BUFLEN];
    int len;
    int err;

#ifdef USE_MMAP
    /* Try first compressing with mmap. If mmap fails (minigzip used in a
     * pipe), use the normal fread loop.
     */
    if (gz_compress_mmap(in, out) == Z_OK) return;
#endif
    /* Clear out the contents of buf before reading from the file to avoid
       MemorySanitizer: use-of-uninitialized-value warnings. */
    memset(buf, 0, sizeof(buf));
    for (;;) {
        len = (int)fread(buf, 1, sizeof(buf), in);
        if (ferror(in)) {
            perror("fread");
            exit(1);
        }
        if (len == 0) break;

        if (PREFIX(gzwrite)(out, buf, (unsigned)len) != len) error(PREFIX(gzerror)(out, &err));
    }
    fclose(in);
    if (PREFIX(gzclose)(out) != Z_OK) error("failed gzclose");
}

#ifdef USE_MMAP /* MMAP version, Miguel Albrecht <malbrech@eso.org> */

/* Try compressing the input file at once using mmap. Return Z_OK if
 * if success, Z_ERRNO otherwise.
 */
int gz_compress_mmap(FILE   *in, gzFile out)
{
    int len;
    int err;
    int ifd = fileno(in);
    caddr_t buf;    /* mmap'ed buffer for the entire input file */
    off_t buf_len;  /* length of the input file */
    struct stat sb;

    /* Determine the size of the file, needed for mmap: */
    if (fstat(ifd, &sb) < 0) return Z_ERRNO;
    buf_len = sb.st_size;
    if (buf_len <= 0) return Z_ERRNO;

    /* Now do the actual mmap: */
    buf = mmap((caddr_t) 0, buf_len, PROT_READ, MAP_SHARED, ifd, (off_t)0);
    if (buf == (caddr_t)(-1)) return Z_ERRNO;

    /* Compress the whole file at once: */
    len = PREFIX(gzwrite)(out, (char *)buf, (unsigned)buf_len);

    if (len != (int)buf_len) error(PREFIX(gzerror)(out, &err));

    munmap(buf, buf_len);
    fclose(in);
    if (PREFIX(gzclose)(out) != Z_OK) error("failed gzclose");
    return Z_OK;
}
#endif /* USE_MMAP */

/* ===========================================================================
 * Uncompress input to output then close both files.
 */
void gz_uncompress(gzFile in, FILE   *out)
{
    char buf[BUFLENW];
    int len;
    int err;

    for (;;) {
        len = PREFIX(gzread)(in, buf, sizeof(buf));
        if (len < 0) error (PREFIX(gzerror)(in, &err));
        if (len == 0) break;

        if ((int)fwrite(buf, 1, (unsigned)len, out) != len) {
            error("failed fwrite");
        }
    }
    if (fclose(out)) error("failed fclose");

    if (PREFIX(gzclose)(in) != Z_OK) error("failed gzclose");
}


/* ===========================================================================
 * Compress the given file: create a corresponding .gz file and remove the
 * original.
 */
void file_compress(char  *file, char  *mode)
{
    char outfile[MAX_NAME_LEN];
    FILE  *in;
    gzFile out;

    if (strlen(file) + strlen(GZ_SUFFIX) >= sizeof(outfile)) {
        fprintf(stderr, "%s: filename too long\n", prog);
        exit(1);
    }

    snprintf(outfile, sizeof(outfile), "%s%s", file, GZ_SUFFIX);

    in = fopen(file, "rb");
    if (in == NULL) {
        perror(file);
        exit(1);
    }
    out = PREFIX(gzopen)(outfile, mode);
    if (out == NULL) {
        fprintf(stderr, "%s: can't gzopen %s\n", prog, outfile);
        exit(1);
    }
    gz_compress(in, out);

    unlink(file);
}


/* ===========================================================================
 * Uncompress the given file and remove the original.
 */
void file_uncompress(char  *file)
{
    char buf[MAX_NAME_LEN];
    char *infile, *outfile;
    FILE  *out;
    gzFile in;
    size_t len = strlen(file);

    if (len + strlen(GZ_SUFFIX) >= sizeof(buf)) {
        fprintf(stderr, "%s: filename too long\n", prog);
        exit(1);
    }

    snprintf(buf, sizeof(buf), "%s", file);

    if (len > SUFFIX_LEN && strcmp(file+len-SUFFIX_LEN, GZ_SUFFIX) == 0) {
        infile = file;
        outfile = buf;
        outfile[len-3] = '\0';
    } else {
        outfile = file;
        infile = buf;
        snprintf(buf + len, sizeof(buf) - len, "%s", GZ_SUFFIX);
    }
    in = PREFIX(gzopen)(infile, "rb");
    if (in == NULL) {
        fprintf(stderr, "%s: can't gzopen %s\n", prog, infile);
        exit(1);
    }
    out = fopen(outfile, "wb");
    if (out == NULL) {
        perror(file);
        exit(1);
    }

    gz_uncompress(in, out);

    unlink(infile);
}


/* ===========================================================================
 * Usage:  minigzip [-c] [-d] [-f] [-h] [-r] [-1 to -9] [files...]
 *   -c : write to standard output
 *   -d : decompress
 *   -f : compress with Z_FILTERED
 *   -h : compress with Z_HUFFMAN_ONLY
 *   -r : compress with Z_RLE
 *   -0 to -9 : compression level
 */

int main(int argc, char *argv[])
{
    int copyout = 0;
    int uncompr = 0;
    int i = 0;
    gzFile file;
    char *bname, outmode[20];

    snprintf(outmode, sizeof(outmode), "%s", "wb6 ");

    prog = argv[i];
    bname = strrchr(argv[i], '/');
    if (bname)
      bname++;
    else
      bname = argv[i];

    if (!strcmp(bname, "gunzip"))
      uncompr = 1;
    else if (!strcmp(bname, "zcat"))
      copyout = uncompr = 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0)
            copyout = 1;
        else if (strcmp(argv[i], "-d") == 0)
            uncompr = 1;
        else if (strcmp(argv[i], "-f") == 0)
            outmode[3] = 'f';
        else if (strcmp(argv[i], "-h") == 0)
            outmode[3] = 'h';
        else if (strcmp(argv[i], "-r") == 0)
            outmode[3] = 'R';
        else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9' && argv[i][2] == 0)
            outmode[2] = argv[i][1];
        else
            break;
    }
    if (outmode[3] == ' ')
        outmode[3] = 0;
    if (i == argc) {
        SET_BINARY_MODE(stdin);
        SET_BINARY_MODE(stdout);
        if (uncompr) {
            file = PREFIX(gzdopen)(fileno(stdin), "rb");
            if (file == NULL) error("can't gzdopen stdin");
            gz_uncompress(file, stdout);
        } else {
            file = PREFIX(gzdopen)(fileno(stdout), outmode);
            if (file == NULL) error("can't gzdopen stdout");
            gz_compress(stdin, file);
        }
    } else {
        if (copyout) {
            SET_BINARY_MODE(stdout);
        }
        do {
            if (uncompr) {
                if (copyout) {
                    file = PREFIX(gzopen)(argv[i], "rb");
                    if (file == NULL)
                        fprintf(stderr, "%s: can't gzopen %s\n", prog, argv[i]);
                    else
                        gz_uncompress(file, stdout);
                } else {
                    file_uncompress(argv[i]);
                }
            } else {
                if (copyout) {
                    FILE * in = fopen(argv[i], "rb");

                    if (in == NULL) {
                        perror(argv[i]);
                    } else {
                        file = PREFIX(gzdopen)(fileno(stdout), outmode);
                        if (file == NULL) error("can't gzdopen stdout");

                        gz_compress(in, file);
                    }

                } else {
                    file_compress(argv[i], outmode);
                }
            }
        } while (++i < argc);
    }
    return 0;
}
