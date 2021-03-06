/* config.h.  Generated by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */


#ifndef _CONFIG_H_
#define _CONFIG_H_

#if defined(WIN32)
#include <windows.h>
#include <stdio.h>
#include <string.h>
#endif

/* Define this if you're running PowerPC architecture */
/* #undef ARCH_PPC */

/* Define this if you're running x86 architecture */
/*define ARCH_X86*/

/* maximum supported data alignment */
#define ATTRIBUTE_ALIGNED_MAX 64

/* Define to one of `_getb67', `GETB67', `getb67' for Cray-2 and Cray-YMP
   systems. This function is required for `alloca.c' support on those systems.
   */
/* #undef CRAY_STACKSEG_END */

/* Define to 1 if using `alloca.c'. */
/* #undef C_ALLOCA */

/* Define this if you have a Motorola 74xx CPU */
/* #undef ENABLE_ALTIVEC */

/* Define to 1 if translation of program messages to the user's native
   language is requested. */
#undef ENABLE_NLS

/* Define this if you have Sun UltraSPARC CPU */
/* #undef ENABLE_VIS */

/* Define to select libmad fixed pointarithmetic implementation */
/* #undef FPM_64BIT */

/* Define to select libmad fixed point arithmetic implementation */
/* #undef FPM_ARM */

/* Define to select libmad fixed point arithmetic implementation */
/* #undef FPM_DEFAULT */

/* Define to select libmad fixed point arithmetic implementation */
#define FPM_INTEL 1

/* Define to select libmad fixed point arithmetic implementation */
/* #undef FPM_MIPS */

/* Define to select libmad fixed point arithmetic implementation */
/* #undef FPM_PPC */

/* Define to select libmad fixed point arithmetic implementation */
/* #undef FPM_SPARC */

/* Define to 1 if you have `alloca', as a function or macro. */
#define HAVE_ALLOCA 1

/* Define to 1 if you have <alloca.h> and it should be used (not on Ultrix).
   */
#define HAVE_ALLOCA_H 1

/* Define this if you have Alsa (libasound) installed */
/* #undef HAVE_ALSA */

/* Define this if you have alsa 0.9.x and more installed */
/* #undef HAVE_ALSA09 */

/* Define this if your asoundlib.h is installed in alsa/ */
/* #undef HAVE_ALSA_ASOUNDLIB_H */

/* Define to 1 if you have the <argz.h> header file. */
/* #undef HAVE_ARGZ_H */

/* Define this if you have ARTS (libartsc) installed */
/* #undef HAVE_ARTS */

/* Define to 1 if you have the <byteswap.h> header file. */
/* #undef HAVE_BYTESWAP_H */

/* Define this if you have CDROM ioctls */
/* #undef HAVE_CDROM_IOCTLS */

/* Define to 1 if you have the `dcgettext' function. */
#define HAVE_DCGETTEXT 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define this if you have a suitable version of libdvdnav */
/* #undef HAVE_DVDNAV */

/* Define this if you have ESD (libesd) installed */
/* #undef HAVE_ESD */

/* Define this if you have linux framebuffer support */
/* #undef HAVE_FB */

/* Define to 1 if you have the `feof_unlocked' function. */
/* #undef HAVE_FEOF_UNLOCKED */

/* Define to 1 if you have the `fgets_unlocked' function. */
/* #undef HAVE_FGETS_UNLOCKED */

/* Define to 1 if you have the `getcwd' function. */
#define HAVE_GETCWD 1

/* Define to 1 if you have the `getegid' function. */
#define HAVE_GETEGID 1

/* Define to 1 if you have the `geteuid' function. */
#define HAVE_GETEUID 1

/* Define to 1 if you have the `getgid' function. */
#define HAVE_GETGID 1

/* Define to 1 if you have the `getpagesize' function. */
#define HAVE_GETPAGESIZE 1

/* Define to 1 if you have the `getpwuid_r' function. */
#define HAVE_GETPWUID_R 1

/* Define if the GNU gettext() function is already present or preinstalled. */
#define HAVE_GETTEXT 1

/* Define to 1 if you have the `getuid' function. */
#define HAVE_GETUID 1

/* Define this if you have GLU support available */
/* #undef HAVE_GLU */

/* Define this if you have GLut support available */
/* #undef HAVE_GLUT */

/* Define this if you have gnome-vfs installed */
/* #undef HAVE_GNOME_VFS */

/* Define if you have the iconv() function. */
/* #undef HAVE_ICONV */

/* Define to 1 if you have the <inttypes.h> header file. */
/* #undef HAVE_INTTYPES_H */

/* Define this if you have ip_mreqn in netinet/in.h */
/* #undef HAVE_IP_MREQN */

/* Define this if you have a usable IRIX al interface available */
/* #undef HAVE_IRIXAL */

/* Define this if you have kernel statistics available via kstat interface */
/* #undef HAVE_KSTAT */

/* Define if you have <langinfo.h> and nl_langinfo(CODESET). */
/* #undef HAVE_LANGINFO_CODESET */

/* Define if your <locale.h> file defines LC_MESSAGES. */
#define HAVE_LC_MESSAGES 1

/* Define this if you have libfame mpeg encoder installed (fame.sf.net) */
/* #undef HAVE_LIBFAME */

/* Define to 1 if you have the `posix4' library (-lposix4). */
/* #undef HAVE_LIBPOSIX4 */

/* Define this if you have librte mpeg encoder installed (zapping.sf.net) */
/* #undef HAVE_LIBRTE */

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the <linux/cdrom.h> header file. */
/* #undef HAVE_LINUX_CDROM_H */

/* Define to 1 if you have the <locale.h> header file. */
#define HAVE_LOCALE_H 1

/* Define to 1 if you have the <malloc.h> header file. */
#define HAVE_MALLOC_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `mempcpy' function. */
/* #undef HAVE_MEMPCPY */

/* Define this if you have mlib installed */
/* #undef HAVE_MLIB */

/* Define to 1 if you have a working `mmap' system call. */
/* #undef HAVE_MMAP */

/* Define to 1 if you have the `munmap' function. */
#define HAVE_MUNMAP 1

/* Define to 1 if you have the `nanosleep' function. */
/* #undef HAVE_NANOSLEEP */

/* Define this if you have libfame 0.8.10 or above */
/* #undef HAVE_NEW_LIBFAME */

/* Define to 1 if you have the <nl_types.h> header file. */
/* #undef HAVE_NL_TYPES_H */

/* Define this if you have OpenGL support available */
/* #undef HAVE_OPENGL */

/* Define to 1 if you have the `putenv' function. */
#define HAVE_PUTENV 1

/* Define this if you have SDL library installed */
/* #undef HAVE_SDL */

/* Define to 1 if you have the `setenv' function. */
#define HAVE_SETENV 1

/* Define to 1 if you have the `setlocale' function. */
#define HAVE_SETLOCALE 1

/* Define to 1 if you have the `sigaction' function. */
#define HAVE_SIGACTION 1

/* Define to 1 if you have the `sigset' function. */
/* #undef HAVE_SIGSET */

/* Define to 1 if you have the <stddef.h> header file. */
#define HAVE_STDDEF_H 1

/* Define to 1 if you have the <stdint.h> header file. */
/* #undef HAVE_STDINT_H */

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `stpcpy' function. */
/* #undef HAVE_STPCPY */

/* Define to 1 if you have the `strcasecmp' function. */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have the `strchr' function. */
#define HAVE_STRCHR 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strpbrk' function. */
#define HAVE_STRPBRK 1

/* Define to 1 if you have the `strsep' function. */
#define HAVE_STRSEP 1

/* Define to 1 if you have the `strtoul' function. */
#define HAVE_STRTOUL 1

/* Define this if your asoundlib.h is installed in sys/ */
/* #undef HAVE_SYS_ASOUNDLIB_H */

/* Define to 1 if you have the <sys/cdio.h> header file. */
/* #undef HAVE_SYS_CDIO_H */

/* Define to 1 if you have the <sys/mixer.h> header file. */
/* #undef HAVE_SYS_MIXER_H */

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 1

/* Define to 1 if you have the <sys/param.h> header file. */
/* #undef HAVE_SYS_PARAM_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the `tsearch' function. */
/* #undef HAVE_TSEARCH */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `vsscanf' function. */
#define HAVE_VSSCANF 1

/* Define this if you have X11R6 installed */
/* #undef HAVE_X11 */

/* Define this if you have libXinerama installed */
/* #undef HAVE_XINERAMA */

/* Define this if you have libXv installed */
/* #undef HAVE_XV */

/* Define this if you have libXv.a */
/* #undef HAVE_XV_STATIC */

/* Define to 1 if you have the `__argz_count' function. */
/* #undef HAVE___ARGZ_COUNT */

/* Define to 1 if you have the `__argz_next' function. */
/* #undef HAVE___ARGZ_NEXT */

/* Define to 1 if you have the `__argz_stringify' function. */
/* #undef HAVE___ARGZ_STRINGIFY */

/* Define as const if the declaration of iconv() needs const. */
/* #undef ICONV_CONST */

/* Define this if you have mlib installed */
/* #undef LIBA52_MLIB */

/* Define this if you have mlib installed */
/* #undef LIBMPEG2_MLIB */

/* Name of package */
#define PACKAGE "xine-lib"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at run-time.
        STACK_DIRECTION > 0 => grows toward higher addresses
        STACK_DIRECTION < 0 => grows toward lower addresses
        STACK_DIRECTION = 0 => direction of growth unknown */
/* #undef STACK_DIRECTION */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Version number of package */
#define VERSION "1-beta12"

/* xine major version number */
#define XINE_MAJOR 1

/* xine minor version number */
#define XINE_MINOR 0

/* xine sub version number */
#define XINE_SUB 0

/* Define to 1 if the X Window System is missing or not being used. */
#define X_DISPLAY_MISSING 1

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
/* #undef WORDS_BIGENDIAN */


/* Define this if you want nvtvd tvmode support */
/* #undef XINE_HAVE_NVTV */

/*#undef HAVE_DVDCSS_DVDCSS_H */

#if defined(WIN32)

#define ssize_t __int64

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

/* Ridiculous hack to return valid xine support
 * directories. These should be read from
 * a registry entry set at install time.
 */

static char tmp_win32_path[ 1024 ];
static char * exec_path_append_subdir( char * string )
{
	char * tmpchar;
	char * cmdline;
	char * back_slash;
	char * fore_slash;
	char * last_slash;

	// get program exec command line
	cmdline = GetCommandLine();

	// check for " at beginning of string
	if( *cmdline == '\"' )
	{
		// copy command line, skip first quote
		strcpy( tmp_win32_path, cmdline + 1 );

		// terminate at second set of quotes
		tmpchar = strchr( tmp_win32_path, '\"' );
		*tmpchar = 0;
	}
	else
	{
		// copy command line
		strcpy( tmp_win32_path, cmdline );

		// terminate at first space
		tmpchar = strchr( tmp_win32_path, ' ' );
		if (tmpchar) *tmpchar = 0;
	}

	// find the last occurance of a back
	// slash or fore slash
	back_slash = strrchr( tmp_win32_path, '\\' );
	fore_slash = strrchr( tmp_win32_path, '/' );

	// make sure the last back slash was not
	// after a drive letter
	if( back_slash > tmp_win32_path )
		if( *( back_slash - 1 ) == ':' )
			back_slash = 0;

	// which slash was the latter slash
	if( back_slash > fore_slash )
		last_slash = back_slash;
	else
		last_slash = fore_slash;

	// detect if we had a relative or
	// distiguished path ( had a slash )
	if( last_slash )
	{
		// we had a slash charachter in our
		// command line
		*( last_slash + 1 ) = 0;

		// if had a string to append to the path
		if( string )
			strcat( tmp_win32_path, string );
	}
	else
	{
		// no slash, assume local directory
		strcpy( tmp_win32_path, "./" );

		// if had a string to append to the path
		if( string )
			strcat( tmp_win32_path, string );
	}

	return tmp_win32_path;
}

#define XINE_PLUGINDIR	exec_path_append_subdir( "plugins" )
#define XINE_FONTDIR	exec_path_append_subdir( "fonts" )
#define XINE_LOCALEDIR	exec_path_append_subdir( "locale" )

#define S_ISDIR(m) ((m) & _S_IFDIR)
#define S_ISREG(m) ((m) & _S_IFREG)
#define S_ISBLK(m) 0
#define S_ISCHR(m) 0

#else

/* Path where catalog files will be. */
#define XINE_LOCALEDIR "/usr/local/share/locale"

/* Define this to plugins directory location */
#define XINE_PLUGINDIR "/usr/local/lib/xine/plugins/1.0.0"

/* Define this if you're running x86 architecture */
#define __i386__ 1

/* Path where aclocal m4 files will be. */
#define XINE_ACFLAGS "-I ${prefix}/share/aclocal"

#endif

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define as `__inline' if that's what the C compiler calls it, or to nothing
   if it is not supported. */
/* #undef inline */

/* Define to `long' if <sys/types.h> does not define. */
/* #undef off_t */

/* Define to `unsigned' if <sys/types.h> does not define. */
/* #undef size_t */


#endif /* defined CONFIG_H */
