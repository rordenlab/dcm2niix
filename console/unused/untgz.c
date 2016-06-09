#include "untgz.h"
/* external functions and related types and constants */
#include <stdio.h>          /* fprintf() */
#include <stdlib.h>         /* malloc(), free() */
#include <string.h>         /* strerror(), strcmp(), strlen(), memcpy() */
#include <errno.h>          /* errno */
#include <fcntl.h>          /* open() */
#include <unistd.h>         /* read(), write(), close(), chown(), unlink() */
#include <sys/types.h>
#include <sys/stat.h>       /* stat(), chmod() */
#include <utime.h>          /* utime() */
#include "zlib.h"           /* inflateBackInit(), inflateBack(), */
/* inflateBackEnd(), crc32() */

#if defined(ZLIB_CONST) && !defined(z_const) //for backwards compatibility with zlib 1.2.5 (included with OSX)
#  define z_const const
#else
#  define z_const
#endif

/* function declaration */
#define local static

/* buffer constants */
#define SIZE 32768U         /* input and output buffer sizes */
#define PIECE 16384         /* limits i/o chunks for 16-bit int case */



/* structure for infback() to pass to input function in() -- it maintains the
 input file and a buffer of size SIZE */
struct ind {
    int infile;
    unsigned char *inbuf;
};

/* Load input buffer, assumed to be empty, and return bytes loaded and a
 pointer to them.  read() is called until the buffer is full, or until it
 returns end-of-file or error.  Return 0 on error. */
local unsigned in(void *in_desc, z_const unsigned char **buf)
{
    int ret;
    unsigned len;
    unsigned char *next;
    struct ind *me = (struct ind *)in_desc;
    
    next = me->inbuf;
    *buf = next;
    len = 0;
    do {
        ret = PIECE;
        if ((unsigned)ret > SIZE - len)
            ret = (int)(SIZE - len);
        ret = (int)read(me->infile, next, ret);
        if (ret == -1) {
            len = 0;
            break;
        }
        next += ret;
        len += ret;
    } while (ret != 0 && len < SIZE);
    return len;
}

/* structure for infback() to pass to output function out() -- it maintains the
 output file, a running CRC-32 check on the output and the total number of
 bytes output, both for checking against the gzip trailer.  (The length in
 the gzip trailer is stored modulo 2^32, so it's ok if a long is 32 bits and
 the output is greater than 4 GB.) */
struct outd {
    int outfile;
    int check;                  /* true if checking crc and total */
    unsigned long crc;
    unsigned long total;
};

/* Write output buffer and update the CRC-32 and total bytes written.  write()
 is called until all of the output is written or an error is encountered.
 On success out() returns 0.  For a write failure, out() returns 1.  If the
 output file descriptor is -1, then nothing is written.
 */
local int out(void *out_desc, unsigned char *buf, unsigned len)
{
    int ret;
    struct outd *me = (struct outd *)out_desc;
    
    if (me->check) {
        me->crc = crc32(me->crc, buf, len);
        me->total += len;
    }
    if (me->outfile != -1)
        do {
            ret = PIECE;
            if ((unsigned)ret > len)
                ret = (int)len;
            ret = (int)write(me->outfile, buf, ret);
            if (ret == -1)
                return 1;
            buf += ret;
            len -= ret;
        } while (len != 0);
    return 0;
}

/* next input byte macro for use inside lunpipe() and gunpipe() */
#define NEXT() (have ? 0 : (have = in(indp, &next)), \
last = have ? (have--, (int)(*next++)) : -1)

/* memory for gunpipe() and lunpipe() --
 the first 256 entries of prefix[] and suffix[] are never used, could
 have offset the index, but it's faster to waste the memory */
unsigned char inbuf[SIZE];              /* input buffer */
unsigned char outbuf[SIZE];             /* output buffer */
unsigned short prefix[65536];           /* index to LZW prefix string */
unsigned char suffix[65536];            /* one-character LZW suffix */
unsigned char match[65280 + 2];         /* buffer for reversed match or gzip
                                         32K sliding window */

/* throw out what's left in the current bits byte buffer (this is a vestigial
 aspect of the compressed data format derived from an implementation that
 made use of a special VAX machine instruction!) */
#define FLUSHCODE() \
do { \
left = 0; \
rem = 0; \
if (chunk > have) { \
chunk -= have; \
have = 0; \
if (NEXT() == -1) \
break; \
chunk--; \
if (chunk > have) { \
chunk = have = 0; \
break; \
} \
} \
have -= chunk; \
next += chunk; \
chunk = 0; \
} while (0)

/* Decompress a compress (LZW) file from indp to outfile.  The compress magic
 header (two bytes) has already been read and verified.  There are have bytes
 of buffered input at next.  strm is used for passing error information back
 to gunpipe().
 
 lunpipe() will return Z_OK on success, Z_BUF_ERROR for an unexpected end of
 file, read error, or write error (a write error indicated by strm->next_in
 not equal to Z_NULL), or Z_DATA_ERROR for invalid input.
 */
local int lunpipe(unsigned have, z_const unsigned char *next, struct ind *indp,
                  int outfile, z_stream *strm)
{
    int last;                   /* last byte read by NEXT(), or -1 if EOF */
    unsigned chunk;             /* bytes left in current chunk */
    int left;                   /* bits left in rem */
    unsigned rem;               /* unused bits from input */
    int bits;                   /* current bits per code */
    unsigned code;              /* code, table traversal index */
    unsigned mask;              /* mask for current bits codes */
    int max;                    /* maximum bits per code for this stream */
    unsigned flags;             /* compress flags, then block compress flag */
    unsigned end;               /* last valid entry in prefix/suffix tables */
    unsigned temp;              /* current code */
    unsigned prev;              /* previous code */
    unsigned final;             /* last character written for previous code */
    unsigned stack;             /* next position for reversed string */
    unsigned outcnt;            /* bytes in output buffer */
    struct outd outd;           /* output structure */
    unsigned char *p;
    
    /* set up output */
    outd.outfile = outfile;
    outd.check = 0;
    
    /* process remainder of compress header -- a flags byte */
    flags = NEXT();
    if (last == -1)
        return Z_BUF_ERROR;
    if (flags & 0x60) {
        strm->msg = (char *)"unknown lzw flags set";
        return Z_DATA_ERROR;
    }
    max = flags & 0x1f;
    if (max < 9 || max > 16) {
        strm->msg = (char *)"lzw bits out of range";
        return Z_DATA_ERROR;
    }
    if (max == 9)                           /* 9 doesn't really mean 9 */
        max = 10;
    flags &= 0x80;                          /* true if block compress */
    
    /* clear table */
    bits = 9;
    mask = 0x1ff;
    end = flags ? 256 : 255;
    
    /* set up: get first 9-bit code, which is the first decompressed byte, but
     don't create a table entry until the next code */
    if (NEXT() == -1)                       /* no compressed data is ok */
        return Z_OK;
    final = prev = (unsigned)last;          /* low 8 bits of code */
    if (NEXT() == -1)                       /* missing a bit */
        return Z_BUF_ERROR;
    if (last & 1) {                         /* code must be < 256 */
        strm->msg = (char *)"invalid lzw code";
        return Z_DATA_ERROR;
    }
    rem = (unsigned)last >> 1;              /* remaining 7 bits */
    left = 7;
    chunk = bits - 2;                       /* 7 bytes left in this chunk */
    outbuf[0] = (unsigned char)final;       /* write first decompressed byte */
    outcnt = 1;
    
    /* decode codes */
    stack = 0;
    for (;;) {
        /* if the table will be full after this, increment the code size */
        if (end >= mask && bits < max) {
            FLUSHCODE();
            bits++;
            mask <<= 1;
            mask++;
        }
        
        /* get a code of length bits */
        if (chunk == 0)                     /* decrement chunk modulo bits */
            chunk = bits;
        code = rem;                         /* low bits of code */
        if (NEXT() == -1) {                 /* EOF is end of compressed data */
            /* write remaining buffered output */
            if (outcnt && out(&outd, outbuf, outcnt)) {
                strm->next_in = outbuf;     /* signal write error */
                return Z_BUF_ERROR;
            }
            return Z_OK;
        }
        code += (unsigned)last << left;     /* middle (or high) bits of code */
        left += 8;
        chunk--;
        if (bits > left) {                  /* need more bits */
            if (NEXT() == -1)               /* can't end in middle of code */
                return Z_BUF_ERROR;
            code += (unsigned)last << left; /* high bits of code */
            left += 8;
            chunk--;
        }
        code &= mask;                       /* mask to current code length */
        left -= bits;                       /* number of unused bits */
        rem = (unsigned)last >> (8 - left); /* unused bits from last byte */
        
        /* process clear code (256) */
        if (code == 256 && flags) {
            FLUSHCODE();
            bits = 9;                       /* initialize bits and mask */
            mask = 0x1ff;
            end = 255;                      /* empty table */
            continue;                       /* get next code */
        }
        
        /* special code to reuse last match */
        temp = code;                        /* save the current code */
        if (code > end) {
            /* Be picky on the allowed code here, and make sure that the code
             we drop through (prev) will be a valid index so that random
             input does not cause an exception.  The code != end + 1 check is
             empirically derived, and not checked in the original uncompress
             code.  If this ever causes a problem, that check could be safely
             removed.  Leaving this check in greatly improves gun's ability
             to detect random or corrupted input after a compress header.
             In any case, the prev > end check must be retained. */
            if (code != end + 1 || prev > end) {
                strm->msg = (char *)"invalid lzw code";
                return Z_DATA_ERROR;
            }
            match[stack++] = (unsigned char)final;
            code = prev;
        }
        
        /* walk through linked list to generate output in reverse order */
        p = match + stack;
        while (code >= 256) {
            *p++ = suffix[code];
            code = prefix[code];
        }
        stack = p - match;
        match[stack++] = (unsigned char)code;
        final = code;
        
        /* link new table entry */
        if (end < mask) {
            end++;
            prefix[end] = (unsigned short)prev;
            suffix[end] = (unsigned char)final;
        }
        
        /* set previous code for next iteration */
        prev = temp;
        
        /* write output in forward order */
        while (stack > SIZE - outcnt) {
            while (outcnt < SIZE)
                outbuf[outcnt++] = match[--stack];
            if (out(&outd, outbuf, outcnt)) {
                strm->next_in = outbuf; /* signal write error */
                return Z_BUF_ERROR;
            }
            outcnt = 0;
        }
        p = match + stack;
        do {
            outbuf[outcnt++] = *--p;
        } while (p > match);
        stack = 0;
        
        /* loop for next code with final and prev as the last match, rem and
         left provide the first 0..7 bits of the next code, end is the last
         valid table entry */
    }
}

/* Decompress a gzip file from infile to outfile.  strm is assumed to have been
 successfully initialized with inflateBackInit().  The input file may consist
 of a series of gzip streams, in which case all of them will be decompressed
 to the output file.  If outfile is -1, then the gzip stream(s) integrity is
 checked and nothing is written.
 
 The return value is a zlib error code: Z_MEM_ERROR if out of memory,
 Z_DATA_ERROR if the header or the compressed data is invalid, or if the
 trailer CRC-32 check or length doesn't match, Z_BUF_ERROR if the input ends
 prematurely or a write error occurs, or Z_ERRNO if junk (not a another gzip
 stream) follows a valid gzip stream.
 */
local int gunpipe(z_stream *strm, int infile, int outfile)
{
    int ret, first, last;
    unsigned have, flags, len;
    z_const unsigned char *next = NULL;
    struct ind ind, *indp;
    struct outd outd;
    
    /* setup input buffer */
    ind.infile = infile;
    ind.inbuf = inbuf;
    indp = &ind;
    
    /* decompress concatenated gzip streams */
    have = 0;                               /* no input data read in yet */
    first = 1;                              /* looking for first gzip header */
    strm->next_in = Z_NULL;                 /* so Z_BUF_ERROR means EOF */
    for (;;) {
        /* look for the two magic header bytes for a gzip stream */
        if (NEXT() == -1) {
            ret = Z_OK;
            break;                          /* empty gzip stream is ok */
        }
        if (last != 31 || (NEXT() != 139 && last != 157)) {
            strm->msg = (char *)"incorrect header check";
            ret = first ? Z_DATA_ERROR : Z_ERRNO;
            break;                          /* not a gzip or compress header */
        }
        first = 0;                          /* next non-header is junk */
        
        /* process a compress (LZW) file -- can't be concatenated after this */
        if (last == 157) {
            ret = lunpipe(have, next, indp, outfile, strm);
            break;
        }
        
        /* process remainder of gzip header */
        ret = Z_BUF_ERROR;
        if (NEXT() != 8) {                  /* only deflate method allowed */
            if (last == -1) break;
            strm->msg = (char *)"unknown compression method";
            ret = Z_DATA_ERROR;
            break;
        }
        flags = NEXT();                     /* header flags */
        NEXT();                             /* discard mod time, xflgs, os */
        NEXT();
        NEXT();
        NEXT();
        NEXT();
        NEXT();
        if (last == -1) break;
        if (flags & 0xe0) {
            strm->msg = (char *)"unknown header flags set";
            ret = Z_DATA_ERROR;
            break;
        }
        if (flags & 4) {                    /* extra field */
            len = NEXT();
            len += (unsigned)(NEXT()) << 8;
            if (last == -1) break;
            while (len > have) {
                len -= have;
                have = 0;
                if (NEXT() == -1) break;
                len--;
            }
            if (last == -1) break;
            have -= len;
            next += len;
        }
        if (flags & 8)                      /* file name */
            while (NEXT() != 0 && last != -1)
                ;
        if (flags & 16)                     /* comment */
            while (NEXT() != 0 && last != -1)
                ;
        if (flags & 2) {                    /* header crc */
            NEXT();
            NEXT();
        }
        if (last == -1) break;
        
        /* set up output */
        outd.outfile = outfile;
        outd.check = 1;
        outd.crc = crc32(0L, Z_NULL, 0);
        outd.total = 0;
        
        /* decompress data to output */
        strm->next_in = next;
        strm->avail_in = have;
        ret = inflateBack(strm, in, indp, out, &outd);
        if (ret != Z_STREAM_END) break;
        next = strm->next_in;
        have = strm->avail_in;
        strm->next_in = Z_NULL;             /* so Z_BUF_ERROR means EOF */
        
        /* check trailer */
        ret = Z_BUF_ERROR;
        if (NEXT() != (int)(outd.crc & 0xff) ||
            NEXT() != (int)((outd.crc >> 8) & 0xff) ||
            NEXT() != (int)((outd.crc >> 16) & 0xff) ||
            NEXT() != (int)((outd.crc >> 24) & 0xff)) {
            /* crc error */
            if (last != -1) {
                strm->msg = (char *)"incorrect data check";
                ret = Z_DATA_ERROR;
            }
            break;
        }
        if (NEXT() != (int)(outd.total & 0xff) ||
            NEXT() != (int)((outd.total >> 8) & 0xff) ||
            NEXT() != (int)((outd.total >> 16) & 0xff) ||
            NEXT() != (int)((outd.total >> 24) & 0xff)) {
            /* length error */
            if (last != -1) {
                strm->msg = (char *)"incorrect length check";
                ret = Z_DATA_ERROR;
            }
            break;
        }
        
        /* go back and look for another gzip stream */
    }
    
    /* clean up and return */
    return ret;
}

#if defined(_WIN64) || defined(_WIN32)
local void copymeta(char *from, char *to)
{
 //not for windows computers
}
#else

/* Copy file attributes, from -> to, as best we can.  This is best effort, so
 no errors are reported.  The mode bits, including suid, sgid, and the sticky
 bit are copied (if allowed), the owner's user id and group id are copied
 (again if allowed), and the access and modify times are copied. */
local void copymeta(char *from, char *to)
{
    struct stat was;
    struct utimbuf when;
    
    /* get all of from's Unix meta data, return if not a regular file */
    if (stat(from, &was) != 0 || (was.st_mode & S_IFMT) != S_IFREG)
        return;
    
    /* set to's mode bits, ignore errors */
    (void)chmod(to, was.st_mode & 07777);
    
    /* copy owner's user and group, ignore errors */
    (void)chown(to, was.st_uid, was.st_gid);
    
    /* copy access and modify times, ignore errors */
    when.actime = was.st_atime;
    when.modtime = was.st_mtime;
    (void)utime(to, &when);
}
#endif

/* Decompress the file inname to the file outnname, of if test is true, just
 decompress without writing and check the gzip trailer for integrity.  If
 inname is NULL or an empty string, read from stdin.  If outname is NULL or
 an empty string, write to stdout.  strm is a pre-initialized inflateBack
 structure.  When appropriate, copy the file attributes from inname to
 outname.
 
 gunzip() returns 1 if there is an out-of-memory error or an unexpected
 return code from gunpipe().  Otherwise it returns 0.
 */
local int gunzip(z_stream *strm, char *inname, char *outname, int test, int deletein)
{
    int ret;
    int infile, outfile;
    
    /* open files */
    if (inname == NULL || *inname == 0) {
        inname = "-";
        infile = 0;     /* stdin */
    }
    else {
        infile = open(inname, O_RDONLY, 0);
        if (infile == -1) {
            fprintf(stderr, "gun cannot open %s\n", inname);
            return -1;
        }
    }
    if (test)
        outfile = -1;
    else if (outname == NULL || *outname == 0) {
        outname = "-";
        outfile = 1;    /* stdout */
    }
    else {
        outfile = open(outname, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        if (outfile == -1) {
            close(infile);
            fprintf(stderr, "gun cannot create %s\n", outname);
            return -1;
        }
    }
    errno = 0;
    
    /* decompress */
    ret = gunpipe(strm, infile, outfile);
    if (outfile > 2) close(outfile);
    if (infile > 2) close(infile);
    
    /* interpret result */
    switch (ret) {
        case Z_OK:
        case Z_ERRNO:
            if (infile > 2 && outfile > 2) {
                copymeta(inname, outname);          /* copy attributes */
                if (deletein) unlink(inname);
            }
            if (ret == Z_ERRNO)
                fprintf(stderr, "gun warning: trailing garbage ignored in %s\n",
                        inname);
            break;
        case Z_DATA_ERROR:
            if (outfile > 2) unlink(outname);
            fprintf(stderr, "gun data error on %s: %s\n", inname, strm->msg);
            break;
        case Z_MEM_ERROR:
            if (outfile > 2) unlink(outname);
            fprintf(stderr, "gun out of memory error--aborting\n");
            return 1;
        case Z_BUF_ERROR:
            if (outfile > 2) unlink(outname);
            if (strm->next_in != Z_NULL) {
                fprintf(stderr, "gun write error on %s: %s\n",
                        outname, strerror(errno));
            }
            else if (errno) {
                fprintf(stderr, "gun read error on %s: %s\n",
                        inname, strerror(errno));
            }
            else {
                fprintf(stderr, "gun unexpected end of file on '%s'\n",
                        inname);
            }
            break;
        default:
            if (outfile > 2) unlink(outname);
            fprintf(stderr, "gun internal error--aborting\n");
            return 1;
    }
    return ret;
}


/*
 * "untar" is an extremely simple tar extractor:
 *  * A single C source file, so it should be easy to compile
 *    and run on any system with a C compiler.
 *  * Extremely portable standard C.  The only non-ANSI function
 *    used is mkdir().
 *  * Reads basic ustar tar archives.
 *  * Does not require libarchive or any other special library.
 *
 * To compile: cc -o untar untar.c
 *
 * Usage:  untar <archive>
 *
 * In particular, this program should be sufficient to extract the
 * distribution for libarchive, allowing people to bootstrap
 * libarchive on systems that do not already have a tar program.
 *
 * To unpack libarchive-x.y.z.tar.gz:
 *    * gunzip libarchive-x.y.z.tar.gz
 *    * untar libarchive-x.y.z.tar
 *
 * Written by Tim Kientzle, March 2009.
 *
 * Released into the public domain.
 */

/* These are all highly standard and portable headers. */
/* g++ -O3 -lz untar.c  -s -o untar */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This is for mkdir(); this may need to be changed for some platforms. */
#include <sys/stat.h>  /* For mkdir() */

/* Parse an octal number, ignoring leading and trailing nonsense. */
static int
parseoct(const char *p, size_t n)
{
	int i = 0;
    
	while (*p < '0' || *p > '7') {
		++p;
		--n;
	}
	while (*p >= '0' && *p <= '7' && n > 0) {
		i *= 8;
		i += *p - '0';
		++p;
		--n;
	}
	return (i);
}

/* Returns true if this is 512 zero bytes. */
static int is_end_of_archive(const char *p)
{
	int n;
	for (n = 511; n >= 0; --n)
		if (p[n] != '\0')
			return (0);
	return (1);
}

#if defined(_WIN64) || defined(_WIN32)
const char kPathSeparator ='\\';
//const char kFileSep[2] = "\\";
#else
const char kPathSeparator ='/';
//const char kFileSep[2] = "/";
#endif

void dropFilenameFromPathKeepFinalFileSep(char *path) { 
    const char *dirPath = strrchr(path, kPathSeparator);
    if (dirPath == NULL) {
        strcpy(path,"");
    } else
        path[dirPath - path +1] = 0; // 
}

#if defined(_WIN64) || defined(_WIN32)

bool is_fileexistsWin(const char * filename) {
    FILE * fp = NULL;
    if ((fp = fopen(filename, "r"))) {
        fclose(fp);
        return true;
    }
    return false;
}

int mkdirWin(char *pathname) 
{
	if (is_fileexistsWin(pathname)) return 0;	
	return mkdir(pathname);
}
#endif

/* Create a directory, including parent directories as necessary. */
static void create_dir(char *pathname, int mode)
{
	char *p;
	int r;
    
	/* Strip trailing '/' */
	if (pathname[strlen(pathname) - 1] == kPathSeparator)
		pathname[strlen(pathname) - 1] = '\0';    
	/* Try creating the directory. */
	#if defined(_WIN64) || defined(_WIN32)
	r = mkdirWin(pathname);
    	#else
	r = mkdir(pathname, mode);
    	#endif
    	fprintf(stderr, "attempted  mkdir %s got %d\n", pathname, r);
	if (r != 0) {
		/* On failure, try creating parent directory. */
		p = strrchr(pathname, kPathSeparator);
		if (p != NULL) {
			*p = '\0';
			create_dir(pathname, 0755);
			*p = kPathSeparator;
			
			#if defined(_WIN64) || defined(_WIN32)
			r = mkdirWin(pathname);
    			#else
			r = mkdir(pathname, mode);
    			#endif

		}
	}
	if (r != 0)
		fprintf(stderr, "Could not create directory %s\n", pathname);
}

/* Create a file, including parent directory as necessary. */
static FILE *
create_file(char *pathname, int mode)
{
	FILE *f;
	f = fopen(pathname, "w+");
	if (f == NULL) {
		/* Try creating parent dir and then creating file. */
		char *p = strrchr(pathname, kPathSeparator);
		if (p != NULL) {
			*p = '\0';
			create_dir(pathname, 0755);
			*p = kPathSeparator;
			f = fopen(pathname, "w+");
		}
	}
	return (f);
}

/* Verify the tar checksum. */
static int
verify_checksum(const char *p)
{
	int n, u = 0;
	for (n = 0; n < 512; ++n) {
		if (n < 148 || n > 155)
        /* Standard tar checksum adds unsigned bytes. */
			u += ((unsigned char *)p)[n];
		else
			u += 0x20;
        
	}
	return (u == parseoct(p + 148, 8));
}

/* Extract a tar archive. */
static void untar(FILE *a, const char *path)
{
	char buff[1024];
	FILE *f = NULL;
	size_t bytes_read;
	int filesize;
    char pathDir[1024];
    strcpy(pathDir,path);
    dropFilenameFromPathKeepFinalFileSep(pathDir);
    char newFile[1024] = {""};
	for (;;) {
		bytes_read = fread(buff, 1, 512, a);
		if (bytes_read < 512) {
			fprintf(stderr,
                    "Short read on %s: expected 512, got %zd\n",path, bytes_read);
			return;
		}
		if (is_end_of_archive(buff)) {
			printf("End of %s\n", path);
			return;
		}
		if (!verify_checksum(buff)) {
			fprintf(stderr, "Checksum failure\n");
			return;
		}
		filesize = parseoct(buff + 124, 12);
		switch (buff[156]) {
            case '1':
                printf(" Ignoring hardlink %s\n", buff);
                break;
            case '2':
                printf(" Ignoring symlink %s\n", buff);
                break;
            case '3':
                printf(" Ignoring character device %s\n", buff);
				break;
            case '4':
                printf(" Ignoring block device %s\n", buff);
                break;
            case '5':
                strcpy (newFile,pathDir);
                strcat (newFile,buff);
                //printf(" Extracting dir %s\n", newFile);
                create_dir(buff, parseoct(buff + 100, 8));
                filesize = 0;
                break;
            case '6':
                printf(" Ignoring FIFO %s\n", buff);
                break;
            default:
                strcpy (newFile,pathDir);
                strcat (newFile,buff);
                //printf(" Extracting file %s\n", newFile);
                f = create_file(newFile, parseoct(buff + 100, 8));
                break;
		}
		while (filesize > 0) {
			bytes_read = fread(buff, 1, 512, a);
			if (bytes_read < 512) {
				fprintf(stderr,
                        "Short read on %s: Expected 512, got %zd\n",path, bytes_read);
				return;
			}
			if (filesize < 512)
				bytes_read = filesize;
			if (f != NULL) {
				if (fwrite(buff, 1, bytes_read, f)
				    != bytes_read)
				{
					fprintf(stderr, "Failed write\n");
					fclose(f);
					f = NULL;
				}
			}
			filesize -= bytes_read;
		}
		if (f != NULL) {
			fclose(f);
			f = NULL;
		}
	}
}

void getFileNam( char *pathParent, const char *path) //if path is c:\d1\d2 then filename is 'd2'
{
    const char *filename = strrchr(path, '/'); //UNIX
    if (filename == 0)
        filename = strrchr(path, '\\'); //Windows
    //const char *filename = strrchr(path, kPathSeparator); //x
    if (filename == NULL) {//no path separator
        strcpy(pathParent,path);
        return;
    }
    filename++;
    strcpy(pathParent,filename);
}

void changeDirOfFilename( char *filename, char *newdir) {//set path of filename to be in outdir
    if (strlen(newdir) < 1) return;
    char fname[1024];
    getFileNam( fname, filename);
    char outname[1024];
    strcpy(outname,newdir);
    if  (outname[strlen(outname) - 1] != kPathSeparator)
#if defined(_WIN64) || defined(_WIN32)
    strcat (outname,"\\");
#else
    strcat (outname,"/"); //append name
#endif
    strcat (outname,fname); //append name
    strcpy(filename,outname);
}

//next portion by Chris Rorden, 2014
int untargz( char * fname, char * outdir){
//returns 0 if success in extracting file, -666 if not a tgz/tar.gz file, otherwise error
    //detect type
    char tardir[1024] = "";
    int len = (int)strlen(fname);
    int ext = 0;
    if (len < 4) return - 1;
    if ((len > 7 ) && (strcmp(fname + len - 7, ".tar.gz") == 0))  {
        ext = 7;
    } else if (strcmp(fname + len - 4, ".tgz") == 0)  {
        ext = 4;
    } else if (strcmp(fname + len - 3, ".gz") == 0)  {
            return -666;//ext = 3; //this function only converts tar.gz and tgz
    } else
        return -666;
    len -= ext;
    //set filenames
    char outname[1024];
    //strlcpy not included in all distros http://stackoverflow.com/questions/18547251/when-i-use-strlcpy-function-in-c-the-compilor-give-me-an-error
    strncpy(outname, fname, len);//strlcpy(outname, fname, len+1);
    changeDirOfFilename(outname, outdir);
    if (ext > 3) { //extract tar.gz files to new folder
        strcpy(tardir, outname); //extract ~/dir/f.tar to ~/dir/f
        if( access( tardir, F_OK ) != -1 ) {
            printf("warning folder %s already exists\n",tardir);
            //return -1;
        } else  create_dir(tardir, 0777);
        strcat(outname, ".tar"); // file.tar.gz -> file.tar, file.tgz -> file.tar
        changeDirOfFilename(outname, tardir);
    }
    printf("gun will copy *%s* to *%s*\n",fname,outname);
    if( access( outname, F_OK ) != -1 ) {
        printf("file %s already exists\n",outname);
        return -1;
    }
    //un gzip
    unsigned char *window;
    z_stream strm;
    window = match;  /* reuse LZW match buffer */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    int ret = inflateBackInit(&strm, 15, window);
    ret = gunzip(&strm, fname, outname, 0, 0);
    inflateBackEnd(&strm);
    if (ret != 0) return ret;
    //un tar
    if (ext > 3) { //.tgz or .tar.gz
        FILE *a;
        a = fopen(outname, "r");
        untar(a, outname);
        fclose(a);
        unlink(outname);
        strcpy(fname,tardir);
    }
    return ret;
}