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
#define	_POSIX_PTHREAD_SEMANTICS 1	/* for 5-arg getpwuid_r on solaris */

/*
#define LOG
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xineutils.h>
#include <xine/xineintl.h>
#ifdef _MSC_VER
#include <xine/xine_internal.h>
#endif
#include "xine_private.h"

#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/socket.h>

#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#if HAVE_UCONTEXT_H
#include <ucontext.h>
#endif

#ifdef HAVE_NL_LANGINFO
#include <langinfo.h>
#endif

#if defined(WIN32)
#include <windows.h>
#endif

#ifndef O_CLOEXEC
#  define O_CLOEXEC  0
#endif

typedef struct {
  const char    language[16];     /* name of the locale */
  const char    encoding[16];     /* typical encoding */
  const char    spu_encoding[16]; /* default spu encoding */
  const char    modifier[8];
} lang_locale_t;


/*
 * information about locales used in xine
 */
static const lang_locale_t lang_locales[] = {
  { "af_ZA",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "ar_AE",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_BH",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_DZ",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_EG",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_IN",    "utf-8",       "utf-8",       ""         },
  { "ar_IQ",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_JO",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_KW",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_LB",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_LY",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_MA",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_OM",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_QA",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_SA",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_SD",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_SY",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_TN",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "ar_YE",    "iso-8859-6",  "iso-8859-6",  ""         },
  { "be_BY",    "cp1251",      "cp1251",      ""         },
  { "bg_BG",    "cp1251",      "cp1251",      ""         },
  { "br_FR",    "iso-8859-1",  "iso-88591",   ""         },
  { "bs_BA",    "iso-8859-2",  "cp1250",      ""         },
  { "ca_ES",    "iso-8859-1",  "iso-88591",   ""         },
  { "ca_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "cs_CZ",    "iso-8859-2",  "cp1250",      ""         },
  { "cy_GB",    "iso-8859-14", "iso-8859-14", ""         },
  { "da_DK",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "de_AT",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "de_AT",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "de_BE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "de_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "de_CH",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "de_DE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "de_DE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "de_LU",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "de_LU",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "el_GR",    "iso-8859-7",  "iso-8859-7",  ""         },
  { "en_AU",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_BW",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_CA",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_DK",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_GB",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_HK",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_IE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_IE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "en_IN",    "utf-8",       "utf-8",       ""         },
  { "en_NZ",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_PH",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_SG",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_US",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_ZA",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "en_ZW",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_AR",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_BO",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_CL",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_CO",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_CR",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_DO",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_EC",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_ES",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "es_GT",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_HN",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_MX",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_NI",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_PA",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_PE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_PR",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_PY",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_SV",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_US",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_UY",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "es_VE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "et_EE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "eu_ES",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "eu_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fa_IR",    "utf-8",       "utf-8",       ""         },
  { "fi_FI",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "fi_FI",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fo_FO",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "fr_BE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "fr_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fr_CA",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "fr_CH",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "fr_FR",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "fr_FR",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fr_LU",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "fr_LU",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "ga_IE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "ga_IE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "gl_ES",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "gl_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "gv_GB",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "he_IL",    "iso-8859-8",  "iso-8859-8",  ""         },
  { "hi_IN",    "utf-8",       "utf-8",       ""         },
  { "hr_HR",    "iso-8859-2",  "cp1250",      ""         },
  { "hu_HU",    "iso-8859-2",  "cp1250",      ""         },
  { "id_ID",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "is_IS",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "it_CH",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "it_IT",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "it_IT",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "iw_IL",    "iso-8859-8",  "iso-8859-8",  ""         },
  { "ja_JP",    "euc-jp",      "euc-jp",      ""         },
  { "ja_JP",    "ujis",        "ujis",        ""         },
  { "japanese", "euc",         "euc",         ""         },
  { "ka_GE",    "georgian-ps", "georgian-ps", ""         },
  { "kl_GL",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "ko_KR",    "euc-kr",      "euc-kr",      ""         },
  { "ko_KR",    "utf-8",       "utf-8",       ""         },
  { "korean",   "euc",         "euc",         ""         },
  { "kw_GB",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "lt_LT",    "iso-8859-13", "iso-8859-13", ""         },
  { "lv_LV",    "iso-8859-13", "iso-8859-13", ""         },
  { "mi_NZ",    "iso-8859-13", "iso-8859-13", ""         },
  { "mk_MK",    "iso-8859-5",  "cp1251",      ""         },
  { "mr_IN",    "utf-8",       "utf-8",       ""         },
  { "ms_MY",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "mt_MT",    "iso-8859-3",  "iso-8859-3",  ""         },
  { "nb_NO",    "ISO-8859-1",  "ISO-8859-1",  ""         },
  { "nl_BE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "nl_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "nl_NL",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "nl_NL",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "nn_NO",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "no_NO",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "oc_FR",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "pl_PL",    "iso-8859-2",  "cp1250",      ""         },
  { "pt_BR",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "pt_PT",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "pt_PT",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "ro_RO",    "iso-8859-2",  "cp1250",      ""         },
  { "ru_RU",    "iso-8859-5",  "cp1251",      ""         },
  { "ru_RU",    "koi8-r",      "cp1251",      ""         },
  { "ru_UA",    "koi8-u",      "cp1251",      ""         },
  { "se_NO",    "utf-8",       "utf-8",       ""         },
  { "sk_SK",    "iso-8859-2",  "cp1250",      ""         },
  { "sl_SI",    "iso-8859-2",  "cp1250",      ""         },
  { "sq_AL",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "sr_YU",    "iso-8859-2",  "cp1250",      ""         },
  { "sr_YU",    "iso-8859-5",  "cp1251",      "cyrillic" },
  { "sv_FI",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "sv_FI",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "sv_SE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "ta_IN",    "utf-8",       "utf-8",       ""         },
  { "te_IN",    "utf-8",       "utf-8",       ""         },
  { "tg_TJ",    "koi8-t",      "cp1251",      ""         },
  { "th_TH",    "tis-620",     "tis-620",     ""         },
  { "tl_PH",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "tr_TR",    "iso-8859-9",  "iso-8859-9",  ""         },
  { "uk_UA",    "koi8-u",      "cp1251",      ""         },
  { "ur_PK",    "utf-8",       "utf-8",       ""         },
  { "uz_UZ",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "vi_VN",    "tcvn",        "tcvn",        ""         },
  { "vi_VN",    "utf-8",       "utf-8",       ""         },
  { "wa_BE",    "iso-8859-1",  "iso-8859-1",  ""         },
  { "wa_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "yi_US",    "cp1255",      "cp1255",      ""         },
  { "zh_CN",    "gb18030",     "gb18030",     ""         },
  { "zh_CN",    "gb2312",      "gb2312",      ""         },
  { "zh_CN",    "gbk",         "gbk",         ""         },
  { "zh_HK",    "big5-hkscs",  "big5-hkscs",  ""         },
  { "zh_TW",    "big-5",       "big-5",       ""         },
  { "zh_TW",    "euc-tw",      "euc-tw",      ""         },
  { "" }
};

/**
 * @brief Allocate and clean memory size_t 'size', then return the
 *        pointer to the allocated memory.
 * @param size Size of the memory area to allocate.
 *
 * @return A pointer to the allocated memory area, or NULL in case of
 *         error.
 *
 * The behaviour of this function differs from standard malloc() as
 * xine_xmalloc(0) will not return a NULL pointer, but rather a
 * pointer to a memory area of size 1 byte.
 *
 * The NULL value is only ever returned in case of an error in
 * malloc(), and is reported to stderr stream.
 *
 * @deprecated This function has been deprecated, as the behaviour of
 *             allocating a 1 byte memory area on zero size is almost
 *             never desired, and the function is thus mostly misused.
 */
void *xine_xmalloc(size_t size) {
  void *ptr;

  /* prevent xine_xmalloc(0) of possibly returning NULL */
  if( !size )
    size++;

  if((ptr = calloc(1, size)) == NULL) {
    fprintf(stderr, "%s: malloc() failed: %s.\n",
	    __XINE_FUNCTION__, strerror(errno));
    return NULL;
  }

  return ptr;
}

/**
 * @brief Wrapper around calloc() function.
 * @param nmemb Number of elements to allocate
 * @param size Size of each element to allocate
 *
 * This is a simple wrapper around calloc(), the only thing
 * it does more than calloc() is outputting an error if
 * the calloc fails (returning NULL).
 */
void *xine_xcalloc(size_t nmemb, size_t size) {
  void *ptr;

  if((ptr = calloc(nmemb, size)) == NULL) {
    fprintf(stderr, "%s: calloc() failed: %s.\n",
	    __XINE_FUNCTION__, strerror(errno));
    return NULL;
  }

  return ptr;
}

void *xine_memdup (const void *src, size_t length)
{
  void *dst = malloc (length);
  return xine_fast_memcpy (dst, src, length);
}

void *xine_memdup0 (const void *src, size_t length)
{
  char *dst = xine_xmalloc (length + 1);
  dst[length] = 0;
  return xine_fast_memcpy (dst, src, length);
}

#ifdef WIN32
/*
 * Parse command line with Windows XP syntax and copy the command (argv[0]).
 */
static size_t xine_strcpy_command(const char *cmdline, char *cmd, size_t maxlen) {
  size_t i, j;

  i = 0;
  j = 0;
  while (cmdline[i] && isspace(cmdline[i])) i++;

  while (cmdline[i] && !isspace(cmdline[i]) && j + 2 < maxlen) {
    switch (cmdline[i]) {
    case '\"':
      i++;
      while (cmdline[i] && cmdline[i] != '\"') {
        if (cmdline[i] == '\\') {
          i++;
          if (cmdline[i] == '\"') cmd[j++] = '\"';
          else {
            cmd[j++] = '\\';
            cmd[j++] = cmdline[i];
          }
        } else cmd[j++] = cmdline[i];
        if (cmdline[i]) i++;
      }
      break;

    case '\\':
      i++;
      if (cmdline[i] == '\"') cmd[j++] = '\"';
      else {
        cmd[j++] = '\\';
        i--;
      }
      break;

    default:
      cmd[j++] = cmdline[i];
    }

    i++;
  }
  cmd[j] = '\0';

  return j;
}
#endif

#ifndef BUFSIZ
#define BUFSIZ 256
#endif

const char *xine_get_homedir(void) {
#ifdef WIN32
  static char homedir[1024] = {0, };
  char *s;
  int len;

  len = xine_strcpy_command(GetCommandLine(), homedir, sizeof(homedir));
  s = strdup(homedir);
  GetFullPathName(s, sizeof(homedir), homedir, NULL);
  free(s);
  if ((s = strrchr(homedir, '\\')))
    *s = '\0';

  return homedir;
#else
  struct passwd pwd, *pw = NULL;
  static char homedir[BUFSIZ] = {0,};

#ifdef HAVE_GETPWUID_R
  if(getpwuid_r(getuid(), &pwd, homedir, sizeof(homedir), &pw) != 0 || pw == NULL) {
#else
  if((pw = getpwuid(getuid())) == NULL) {
#endif
    char *tmp = getenv("HOME");
    if(tmp) {
      strncpy(homedir, tmp, sizeof(homedir));
      homedir[sizeof(homedir) - 1] = '\0';
    }
  } else {
    char *s = strdup(pw->pw_dir);
    strncpy(homedir, s, sizeof(homedir));
    homedir[sizeof(homedir) - 1] = '\0';
    free(s);
  }

  if(!homedir[0]) {
    printf("xine_get_homedir: Unable to get home directory, set it to /tmp.\n");
    strcpy(homedir, "/tmp");
  }

  return homedir;
#endif /* WIN32 */
}

#if defined(WIN32) || defined(__CYGWIN__)
static void xine_get_rootdir(char *rootdir, size_t maxlen) {
  char *s;

  strncpy(rootdir, xine_get_homedir(), maxlen - 1);
  rootdir[maxlen - 1] = '\0';
  if ((s = strrchr(rootdir, XINE_DIRECTORY_SEPARATOR_CHAR))) *s = '\0';
}

const char *xine_get_pluginroot(void) {
  static char pluginroot[1024] = {0, };

  if (!pluginroot[0]) {
    xine_get_rootdir(pluginroot, sizeof(pluginroot) - strlen(XINE_REL_PLUGINROOT) - 1);
    strcat(pluginroot, XINE_DIRECTORY_SEPARATOR_STRING XINE_REL_PLUGINROOT);
  }

  return pluginroot;
}

const char *xine_get_plugindir(void) {
  static char plugindir[1024] = {0, };

  if (!plugindir[0]) {
    xine_get_rootdir(plugindir, sizeof(plugindir) - strlen(XINE_REL_PLUGINDIR) - 1);
    strcat(plugindir, XINE_DIRECTORY_SEPARATOR_STRING XINE_REL_PLUGINDIR);
  }

  return plugindir;
}

const char *xine_get_fontdir(void) {
  static char fontdir[1024] = {0, };

  if (!fontdir[0]) {
    xine_get_rootdir(fontdir, sizeof(fontdir) - strlen(XINE_REL_FONTDIR) - 1);
    strcat(fontdir, XINE_DIRECTORY_SEPARATOR_STRING XINE_REL_FONTDIR);
  }

  return fontdir;
}

const char *xine_get_localedir(void) {
  static char localedir[1024] = {0, };

  if (!localedir[0]) {
    xine_get_rootdir(localedir, sizeof(localedir) - strlen(XINE_REL_LOCALEDIR) - 1);
    strcat(localedir, XINE_DIRECTORY_SEPARATOR_STRING XINE_REL_LOCALEDIR);
  }

  return localedir;
}
#endif

char *xine_chomp(char *str) {
  char *pbuf;

  pbuf = str;

  while(*pbuf != '\0') pbuf++;

  while(pbuf > str) {
    if(*pbuf == '\r' || *pbuf == '\n' || *pbuf == '"') *pbuf = '\0';
    pbuf--;
  }

  while(*pbuf == '=') pbuf++;

  return pbuf;
}


/*
 * a thread-safe usecond sleep
 */
void xine_usec_sleep(unsigned usec) {
#ifdef WIN32
  /* select does not work on win32 */
  Sleep(usec / 1000);
#else
#  if 0
#    if HAVE_NANOSLEEP
  /* nanosleep is prefered on solaris, because it's mt-safe */
  struct timespec ts, remaining;
  ts.tv_sec =   usec / 1000000;
  ts.tv_nsec = (usec % 1000000) * 1000;
  while (nanosleep (&ts, &remaining) == -1 && errno == EINTR)
    ts = remaining;
#    else
  usleep(usec);
#    endif
#  else
  if (usec < 10000) {
      usec = 10000;
  }
  struct timeval tm;
  tm.tv_sec  = usec / 1000000;
  tm.tv_usec = usec % 1000000;
  select(0, 0, 0, 0, &tm); /* FIXME: EINTR? */
#  endif
#endif
}


/* print a hexdump of length bytes from the data given in buf */
void xine_hexdump (const void *buf_gen, int length) {
  static const char separator[70] = "---------------------------------------------------------------------";

  const uint8_t *const buf = (const uint8_t*)buf_gen;
  int j = 0;

  /* printf ("Hexdump: %i Bytes\n", length);*/
  puts(separator);

  while(j<length) {
    int i;
    const int imax = (j+16 < length) ? (j+16) : length;

    printf ("%04X ",j);
    for (i=j; i<j+16; i++) {
      if( i<length )
        printf ("%02X ", buf[i]);
      else
        printf("   ");
    }

    for (i=j; i < imax; i++) {
      fputc ((buf[i] >= 32 && buf[i] <= 126) ? buf[i] : '.', stdout);
    }
    j=i;

    fputc('\n', stdout);
  }

  puts(separator);
}


static const lang_locale_t *_get_first_lang_locale(const char *lcal) {
  const lang_locale_t *llocale;
  size_t lang_len;
  char *mod;

  if(lcal && *lcal) {
    llocale = &*lang_locales;

    if ((mod = strchr(lcal, '@')))
      lang_len = mod++ - lcal;
    else
      lang_len = strlen(lcal);

    while(*(llocale->language)) {
      if(!strncmp(lcal, llocale->language, lang_len)) {
        if ((!mod && !llocale->modifier) || (mod && llocale->modifier && !strcmp(mod, llocale->modifier)))
	  return llocale;
      }

      llocale++;
    }
  }
  return NULL;
}


static char *_get_lang(void) {
    char *lang;

    if(!(lang = getenv("LC_ALL")))
      if(!(lang = getenv("LC_MESSAGES")))
        lang = getenv("LANG");

  return lang;
}


/*
 * get encoding of current locale
 */
char *xine_get_system_encoding(void) {
  char *codeset = NULL;

#ifdef HAVE_NL_LANGINFO
  setlocale(LC_CTYPE, "");
  codeset = nl_langinfo(CODESET);
#endif
  /*
   * guess locale codeset according to shell variables
   * when nl_langinfo(CODESET) isn't available or workig
   */
  if (!codeset || strstr(codeset, "ANSI") != 0) {
    char *lang = _get_lang();

    codeset = NULL;

    if(lang) {
      char *lg, *enc, *mod;

      lg = strdup(lang);

      if((enc = strchr(lg, '.')) && (strlen(enc) > 1)) {
        enc++;

        if((mod = strchr(enc, '@')))
          *mod = '\0';

        codeset = strdup(enc);
      }
      else {
        const lang_locale_t *llocale = _get_first_lang_locale(lg);

        if(llocale && llocale->encoding)
          codeset = strdup(llocale->encoding);
      }

      free(lg);
    }
  } else
    codeset = strdup(codeset);

  return codeset;
}


/*
 * guess default encoding of subtitles
 */
const char *xine_guess_spu_encoding(void) {
  char *lang = _get_lang();

  if (lang) {
    const lang_locale_t *llocale;
    char *lg, *enc;

    lg = strdup(lang);

    if ((enc = strchr(lg, '.'))) *enc = '\0';
    llocale = _get_first_lang_locale(lg);
    free(lg);
    if (llocale && llocale->spu_encoding) return llocale->spu_encoding;
  }

  return "iso-8859-1";
}


#ifdef _MSC_VER
void xine_xprintf(xine_t *xine, int verbose, const char *fmt, ...) {
  char message[256];
  va_list ap;

  if (xine && xine->verbosity >= verbose) {
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    xine_log(xine, XINE_LOG_TRACE, "%s", message);
  }
}
#endif

int xine_monotonic_clock(struct timeval *tv, struct timezone *tz)
{
#if _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK) && defined(HAVE_POSIX_TIMERS)
  static int initialized = 0;
  static int use_clock_monotonic = 0;

  struct timespec tp;

  if( !initialized ) {
    struct timespec res;
    int ret;

    ret = clock_getres(CLOCK_MONOTONIC, &res);

    if( ret != 0 ) {
      lprintf("get resolution of monotonic clock failed\n");
    } else {
      /* require at least milisecond resolution */
      if( res.tv_sec > 0 ||
          res.tv_nsec > 1000000 ) {
        lprintf("monotonic clock resolution (%d:%d) too bad\n",
                 (int)res.tv_sec, (int)res.tv_nsec);
      } else {
        if( clock_gettime(CLOCK_MONOTONIC, &tp) != 0 ) {
          lprintf("get monotonic clock failed\n");
        } else {
          lprintf("using monotonic clock\n");
          use_clock_monotonic = 1;
        }
      }
    }
    initialized = 1;
  }

  if(use_clock_monotonic && !clock_gettime(CLOCK_MONOTONIC, &tp)) {
    tv->tv_sec = tp.tv_sec;
    tv->tv_usec = tp.tv_nsec / 1000;
    return 0;
  } else {
    return gettimeofday(tv, tz);
  }

#else

  return gettimeofday(tv, tz);

#endif
}

char *xine_strcat_realloc (char **dest, char *append)
{
  char *newstr = realloc (*dest, (*dest ? strlen (*dest) : 0) + strlen (append) + 1);
  if (newstr)
    strcat (*dest = newstr, append);
  return newstr;
}

char *_x_asprintf(const char *format, ...)
{
  va_list ap;
  char *buf = NULL;

  va_start (ap, format);
  if (vasprintf (&buf, format, ap) < 0)
    buf = NULL;
  va_end (ap);

  return buf;
}

int _x_set_file_close_on_exec(int fd)
{
#ifndef WIN32
  return fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
  return SetHandleInformation((HANDLE)_get_osfhandle(fd), HANDLE_FLAG_INHERIT, 0);
#endif
}

int _x_set_socket_close_on_exec(int s)
{
#ifndef WIN32
  return fcntl(s, F_SETFD, FD_CLOEXEC);
#else
  return SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
#endif
}


int xine_open_cloexec(const char *name, int flags)
{
  int fd = open(name, (flags | O_CLOEXEC));

  if (fd >= 0) {
    _x_set_file_close_on_exec(fd);
  }

  return fd;
}

int xine_create_cloexec(const char *name, int flags, mode_t mode)
{
  int fd = open(name, (flags | O_CREAT | O_CLOEXEC), mode);

  if (fd >= 0) {
    _x_set_file_close_on_exec(fd);
  }

  return fd;
}

int xine_socket_cloexec(int domain, int type, int protocol)
{
  int s = socket(domain, type, protocol);

  if (s >= 0) {
    _x_set_socket_close_on_exec(s);
  }

  return s;
}

