/*
 * Copyright (C) 2000-2013 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */
#ifndef XINEUTILS_H
#define XINEUTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#ifdef WIN32
#  include <winsock.h>
#else
#  include <sys/time.h>
#endif
#include <xine/os_types.h>
#include <xine/attributes.h>
#include <xine/compat.h>
#include <xine/xmlparser.h>
#include <xine/xine_buffer.h>
#include <xine/configfile.h>
#include <xine/list.h>
#include <xine/array.h>
#include <xine/sorted_array.h>

#include <stdio.h>
#include <string.h>

/*
 * Mark exported data symbols for link engine library clients with older
 * Win32 compilers
 */
#if defined(WIN32) && !defined(XINE_LIBRARY_COMPILE)
#  define DL_IMPORT __declspec(dllimport)
#  define extern DL_IMPORT extern
#endif

  /*
   * debugable mutexes
   */

  typedef struct {
    pthread_mutex_t  mutex;
    char             id[80];
    char            *locked_by;
  } xine_mutex_t;

  int xine_mutex_init    (xine_mutex_t *mutex, const pthread_mutexattr_t *mutexattr,
			  const char *id) XINE_PROTECTED;

  int xine_mutex_lock    (xine_mutex_t *mutex, const char *who) XINE_PROTECTED;
  int xine_mutex_unlock  (xine_mutex_t *mutex, const char *who) XINE_PROTECTED;
  int xine_mutex_destroy (xine_mutex_t *mutex) XINE_PROTECTED;



			/* CPU Acceleration */

/*
 * The type of an value that fits in an MMX register (note that long
 * long constant values MUST be suffixed by LL and unsigned long long
 * values by ULL, lest they be truncated by the compiler)
 */

/* generic accelerations */
#define MM_ACCEL_MLIB           0x00000001

/* x86 accelerations */
#define MM_ACCEL_X86_MMX        0x80000000
#define MM_ACCEL_X86_3DNOW      0x40000000
#define MM_ACCEL_X86_MMXEXT     0x20000000
#define MM_ACCEL_X86_SSE        0x10000000
#define MM_ACCEL_X86_SSE2       0x08000000
#define MM_ACCEL_X86_SSE3       0x04000000
#define MM_ACCEL_X86_SSSE3      0x02000000
#define MM_ACCEL_X86_SSE4       0x01000000
#define MM_ACCEL_X86_SSE42      0x00800000
#define MM_ACCEL_X86_AVX        0x00400000

/* powerpc accelerations and features */
#define MM_ACCEL_PPC_ALTIVEC    0x04000000
#define MM_ACCEL_PPC_CACHE32    0x02000000

/* SPARC accelerations */

#define MM_ACCEL_SPARC_VIS      0x01000000
#define MM_ACCEL_SPARC_VIS2     0x00800000

/* x86 compat defines */
#define MM_MMX                  MM_ACCEL_X86_MMX
#define MM_3DNOW                MM_ACCEL_X86_3DNOW
#define MM_MMXEXT               MM_ACCEL_X86_MMXEXT
#define MM_SSE                  MM_ACCEL_X86_SSE
#define MM_SSE2                 MM_ACCEL_X86_SSE2

uint32_t xine_mm_accel (void) XINE_CONST XINE_PROTECTED;



		     /* Optimized/fast memcpy */

extern void *(* xine_fast_memcpy)(void *to, const void *from, size_t len) XINE_PROTECTED;

/*
 * Debug stuff
 */
/*
 * profiling (unworkable in non DEBUG isn't defined)
 */
void xine_profiler_init (void) XINE_PROTECTED;
int xine_profiler_allocate_slot (const char *label) XINE_PROTECTED;
void xine_profiler_start_count (int id) XINE_PROTECTED;
void xine_profiler_stop_count (int id) XINE_PROTECTED;
void xine_profiler_print_results (void) XINE_PROTECTED;

/*
 * Allocate and clean memory size_t 'size', then return the pointer
 * to the allocated memory.
 */
void *xine_xmalloc(size_t size) XINE_MALLOC XINE_DEPRECATED XINE_PROTECTED;

void *xine_xcalloc(size_t nmemb, size_t size) XINE_MALLOC XINE_PROTECTED;

/*
 * Free allocated memory and set pointer to NULL
 * @param ptr Pointer to the pointer to the memory block which should be freed.
 */
static inline void _x_freep(void *ptr) {
  void **p = (void **)ptr;
  free (*p);
  *p = NULL;
}

/*
 * Copy blocks of memory.
 */
void *xine_memdup (const void *src, size_t length) XINE_MALLOC XINE_PROTECTED;
void *xine_memdup0 (const void *src, size_t length) XINE_MALLOC XINE_PROTECTED;

/*
 * Get user home directory.
 */
const char *xine_get_homedir(void) XINE_PROTECTED;

#if defined(WIN32) || defined(__CYGWIN__)
/*
 * Get other xine directories.
 */
const char *xine_get_pluginroot(void) XINE_PROTECTED;
const char *xine_get_plugindir(void) XINE_PROTECTED;
const char *xine_get_fontdir(void) XINE_PROTECTED;
const char *xine_get_localedir(void) XINE_PROTECTED;
#endif

/*
 * Clean a string (remove spaces and '=' at the begin,
 * and '\n', '\r' and spaces at the end.
 */
char *xine_chomp (char *str) XINE_PROTECTED;

/*
 * A thread-safe usecond sleep
 */
void xine_usec_sleep(unsigned usec) XINE_PROTECTED;

/* compatibility macros */
#define xine_strpbrk(S, ACCEPT) strpbrk((S), (ACCEPT))
#define xine_strsep(STRINGP, DELIM) strsep((STRINGP), (DELIM))
#define xine_setenv(NAME, VAL, XX) setenv((NAME), (VAL), (XX))

/**
 * append to a string, reallocating
 * normally, updates & returns *dest
 * on error, *dest is unchanged & NULL is returned.
 */
char *xine_strcat_realloc (char **dest, char *append) XINE_PROTECTED;

/**
 * asprintf wrapper
 * allocate a string large enough to hold the output, and return a pointer to
 * it. This pointer should be passed to free when it is no longer needed.
 * return NULL on error.
 */
char *_x_asprintf(const char *format, ...) XINE_PROTECTED XINE_MALLOC XINE_FORMAT_PRINTF(1, 2);

/**
 * opens a file, ensuring that the descriptor will be closed
 * automatically after a fork/execute.
 */
int xine_open_cloexec(const char *name, int flags) XINE_PROTECTED;

/**
 * creates a file, ensuring that the descriptor will be closed
 * automatically after a fork/execute.
 */
int xine_create_cloexec(const char *name, int flags, mode_t mode) XINE_PROTECTED;

/**
 * creates a socket, ensuring that the descriptor will be closed
 * automatically after a fork/execute.
 */
int xine_socket_cloexec(int domain, int type, int protocol) XINE_PROTECTED;

/*
 * Color Conversion Utility Functions
 * The following data structures and functions facilitate the conversion
 * of RGB images to packed YUV (YUY2) images. There are also functions to
 * convert from YUV9 -> YV12. All of the meaty details are written in
 * color.c.
 */

typedef struct yuv_planes_s {

  unsigned char *y;
  unsigned char *u;
  unsigned char *v;
  unsigned int row_width;    /* frame width */
  unsigned int row_count;    /* frame height */

} yuv_planes_t;

void init_yuv_conversion(void) XINE_PROTECTED;
void init_yuv_planes(yuv_planes_t *yuv_planes, int width, int height) XINE_PROTECTED;
void free_yuv_planes(yuv_planes_t *yuv_planes) XINE_PROTECTED;

extern void (*yuv444_to_yuy2)
  (const yuv_planes_t *yuv_planes, unsigned char *yuy2_map, int pitch) XINE_PROTECTED;
extern void (*yuv9_to_yv12)
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dest, int y_dest_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dest, int u_dest_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dest, int v_dest_pitch,
   int width, int height) XINE_PROTECTED;
extern void (*yuv411_to_yv12)
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dest, int y_dest_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dest, int u_dest_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dest, int v_dest_pitch,
   int width, int height) XINE_PROTECTED;
extern void (*yv12_to_yuy2)
  (const unsigned char *y_src, int y_src_pitch,
   const unsigned char *u_src, int u_src_pitch,
   const unsigned char *v_src, int v_src_pitch,
   unsigned char *yuy2_map, int yuy2_pitch,
   int width, int height, int progressive) XINE_PROTECTED;
extern void (*yuy2_to_yv12)
  (const unsigned char *yuy2_map, int yuy2_pitch,
   unsigned char *y_dst, int y_dst_pitch,
   unsigned char *u_dst, int u_dst_pitch,
   unsigned char *v_dst, int v_dst_pitch,
   int width, int height) XINE_PROTECTED;

#define SCALEFACTOR 65536
#define CENTERSAMPLE 128

/* These conversions are normalised for the MPEG Y'CbCr colourspace.
 * (Yes, we know that we call it YUV elsewhere.)
 */
#define COMPUTE_Y(r, g, b) \
  (unsigned char) \
  ((y_r_table[r] + y_g_table[g] + y_b_table[b]) / SCALEFACTOR)
#define COMPUTE_U(r, g, b) \
  (unsigned char) \
  ((u_r_table[r] + u_g_table[g] + u_b_table[b]) / SCALEFACTOR + CENTERSAMPLE)
#define COMPUTE_V(r, g, b) \
  (unsigned char) \
  ((v_r_table[r] + v_g_table[g] + v_b_table[b]) / SCALEFACTOR + CENTERSAMPLE)

#define UNPACK_BGR15(packed_pixel, r, g, b) \
  b = (packed_pixel & 0x7C00) >> 7; \
  g = (packed_pixel & 0x03E0) >> 2; \
  r = (packed_pixel & 0x001F) << 3;

#define UNPACK_BGR16(packed_pixel, r, g, b) \
  b = (packed_pixel & 0xF800) >> 8; \
  g = (packed_pixel & 0x07E0) >> 3; \
  r = (packed_pixel & 0x001F) << 3;

#define UNPACK_RGB15(packed_pixel, r, g, b) \
  r = (packed_pixel & 0x7C00) >> 7; \
  g = (packed_pixel & 0x03E0) >> 2; \
  b = (packed_pixel & 0x001F) << 3;

#define UNPACK_RGB16(packed_pixel, r, g, b) \
  r = (packed_pixel & 0xF800) >> 8; \
  g = (packed_pixel & 0x07E0) >> 3; \
  b = (packed_pixel & 0x001F) << 3;

extern int y_r_table[256] XINE_PROTECTED;
extern int y_g_table[256] XINE_PROTECTED;
extern int y_b_table[256] XINE_PROTECTED;

extern int u_r_table[256] XINE_PROTECTED;
extern int u_g_table[256] XINE_PROTECTED;
extern int u_b_table[256] XINE_PROTECTED;

extern int v_r_table[256] XINE_PROTECTED;
extern int v_g_table[256] XINE_PROTECTED;
extern int v_b_table[256] XINE_PROTECTED;

/* TJ. direct sliced rgb -> yuy2 conversion */
extern void *rgb2yuy2_alloc (int color_matrix, const char *format) XINE_PROTECTED;
extern void  rgb2yuy2_free (void *rgb2yuy2) XINE_PROTECTED;
extern void  rgb2yuy2_slice (void *rgb2yuy2, const uint8_t *in, int ipitch, uint8_t *out, int opitch,
  int width, int height) XINE_PROTECTED;
extern void  rgb2yuy2_palette (void *rgb2yuy2, const uint8_t *pal, int num_colors, int bits_per_pixel)
  XINE_PROTECTED;


/* frame copying functions */
extern void yv12_to_yv12
  (const unsigned char *y_src, int y_src_pitch, unsigned char *y_dst, int y_dst_pitch,
   const unsigned char *u_src, int u_src_pitch, unsigned char *u_dst, int u_dst_pitch,
   const unsigned char *v_src, int v_src_pitch, unsigned char *v_dst, int v_dst_pitch,
   int width, int height) XINE_PROTECTED;
extern void yuy2_to_yuy2
  (const unsigned char *src, int src_pitch,
   unsigned char *dst, int dst_pitch,
   int width, int height) XINE_PROTECTED;

/* print a hexdump of the given data */
void xine_hexdump (const void *buf, int length) XINE_PROTECTED;

/*
 * Optimization macros for conditions
 * Taken from the FIASCO L4 microkernel sources
 */
#if !defined(__GNUC__) || __GNUC__ < 3
#  define EXPECT_TRUE(x)  (x)
#  define EXPECT_FALSE(x) (x)
#else
#  define EXPECT_TRUE(x)  __builtin_expect((x),1)
#  define EXPECT_FALSE(x) __builtin_expect((x),0)
#endif

#ifdef NDEBUG
#define _x_assert(exp) \
  do {                                                                \
    if (!(exp))                                                       \
      fprintf(stderr, "assert: %s:%d: %s: Assertion `%s' failed.\n",  \
              __FILE__, __LINE__, __XINE_FUNCTION__, #exp);           \
  } while(0)
#else
#define _x_assert(exp) \
  do {                                                                \
    if (!(exp)) {                                                     \
      fprintf(stderr, "assert: %s:%d: %s: Assertion `%s' failed.\n",  \
              __FILE__, __LINE__, __XINE_FUNCTION__, #exp);           \
      abort();                                                        \
    }                                                                 \
  } while(0)
#endif

#define _x_abort()                                                    \
  do {                                                                \
    fprintf(stderr, "abort: %s:%d: %s: Aborting.\n",                  \
            __FILE__, __LINE__, __XINE_FUNCTION__);                   \
    abort();                                                          \
  } while(0)


/****** logging with xine **********************************/

#ifndef LOG_MODULE
  #define LOG_MODULE __FILE__
#endif /* LOG_MODULE */

#define LOG_MODULE_STRING printf("%s: ", LOG_MODULE );

#ifdef LOG_VERBOSE
  #define LONG_LOG_MODULE_STRING                                            \
    printf("%s: (%s:%d) ", LOG_MODULE, __XINE_FUNCTION__, __LINE__ );
#else
  #define LONG_LOG_MODULE_STRING  LOG_MODULE_STRING
#endif /* LOG_VERBOSE */

#ifdef LOG
  #ifdef __GNUC__
    #define lprintf(fmt, args...)                                           \
      do {                                                                  \
        LONG_LOG_MODULE_STRING                                              \
        printf(fmt, ##args);                                                \
        fflush(stdout);                                                     \
      } while(0)
  #else /* __GNUC__ */
    #ifdef _MSC_VER
      #define lprintf(fmtargs)                                              \
        do {                                                                \
          LONG_LOG_MODULE_STRING                                            \
          printf("%s", fmtargs);                                            \
          fflush(stdout);                                                   \
        } while(0)
    #else /* _MSC_VER */
      #define lprintf(...)                                                  \
        do {                                                                \
          LONG_LOG_MODULE_STRING                                            \
          printf(__VA_ARGS__);                                              \
          fflush(stdout);                                                   \
        } while(0)
    #endif  /* _MSC_VER */
  #endif /* __GNUC__ */
#else /* LOG */
  #ifdef __GNUC__
    #define lprintf(fmt, args...)     do {} while(0)
  #else
  #ifdef _MSC_VER
void __inline lprintf(const char * fmt, ...) {}
  #else
    #define lprintf(...)              do {} while(0)
  #endif /* _MSC_VER */
  #endif /* __GNUC__ */
#endif /* LOG */

#ifdef __GNUC__
  #define llprintf(cat, fmt, args...)                                       \
    do{                                                                     \
      if(cat){                                                              \
        LONG_LOG_MODULE_STRING                                              \
        printf( fmt, ##args );                                              \
      }                                                                     \
    }while(0)
#else
#ifdef _MSC_VER
  #define llprintf(cat, fmtargs)                                            \
    do{                                                                     \
      if(cat){                                                              \
        LONG_LOG_MODULE_STRING                                              \
        printf( "%s", fmtargs );                                            \
      }                                                                     \
    }while(0)
#else
  #define llprintf(cat, ...)                                                \
    do{                                                                     \
      if(cat){                                                              \
        LONG_LOG_MODULE_STRING                                              \
        printf( __VA_ARGS__ );                                              \
      }                                                                     \
    }while(0)
#endif /* _MSC_VER */
#endif /* __GNUC__ */

#ifdef  __GNUC__
  #define xprintf(xine, verbose, fmt, args...)                              \
    do {                                                                    \
      if((xine) && (xine)->verbosity >= verbose){                           \
        xine_log(xine, XINE_LOG_TRACE, fmt, ##args);                        \
      }                                                                     \
    } while(0)
#else
#ifdef _MSC_VER
void xine_xprintf(xine_t *xine, int verbose, const char *fmt, ...);
  #define xprintf xine_xprintf
#else
  #define xprintf(xine, verbose, ...)                                       \
    do {                                                                    \
      if((xine) && (xine)->verbosity >= verbose){                           \
        xine_log(xine, XINE_LOG_TRACE, __VA_ARGS__);                        \
      }                                                                     \
    } while(0)
#endif /* _MSC_VER */
#endif /* __GNUC__ */

/* time measuring macros for profiling tasks */

#ifdef DEBUG
#  define XINE_PROFILE(function)                                            \
     do {                                                                   \
       struct timeval current_time;                                         \
       double dtime;                                                        \
       gettimeofday(&current_time, NULL);                                   \
       dtime = -(current_time.tv_sec + (current_time.tv_usec / 1000000.0)); \
       function;                                                            \
       gettimeofday(&current_time, NULL);                                   \
       dtime += current_time.tv_sec + (current_time.tv_usec / 1000000.0);   \
       printf("%s: (%s:%d) took %lf seconds\n",                             \
              LOG_MODULE, __XINE_FUNCTION__, __LINE__, dtime);              \
     } while(0)
#  define XINE_PROFILE_ACCUMULATE(function)                                 \
     do {                                                                   \
       struct timeval current_time;                                         \
       static double dtime = 0;                                             \
       gettimeofday(&current_time, NULL);                                   \
       dtime -= current_time.tv_sec + (current_time.tv_usec / 1000000.0);   \
       function;                                                            \
       gettimeofday(&current_time, NULL);                                   \
       dtime += current_time.tv_sec + (current_time.tv_usec / 1000000.0);   \
       printf("%s: (%s:%d) took %lf seconds\n",                             \
              LOG_MODULE, __XINE_FUNCTION__, __LINE__, dtime);              \
     } while(0)
#else
#  define XINE_PROFILE(function) function
#  define XINE_PROFILE_ACCUMULATE(function) function
#endif /* DEBUG */

/**
 * get encoding of current locale
 */
char *xine_get_system_encoding(void) XINE_MALLOC XINE_PROTECTED;

/*
 * guess default encoding for the subtitles
 */
const char *xine_guess_spu_encoding(void) XINE_PROTECTED;

/*
 * use the best clock reference (API compatible with gettimeofday)
 * note: it will be a monotonic clock, if available.
 */
int xine_monotonic_clock(struct timeval *tv, struct timezone *tz) XINE_PROTECTED;

/**
 * Unknown FourCC reporting functions
 */
void _x_report_video_fourcc (xine_t *, const char *module, uint32_t) XINE_PROTECTED;
void _x_report_audio_format_tag (xine_t *, const char *module, uint32_t) XINE_PROTECTED;

/* don't harm following code */
#ifdef extern
#  undef extern
#endif

#ifdef __cplusplus
}
#endif

#endif
