/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General 
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** types.h
**
** Data type definitions
** $Id: types.h,v 1.4 2004/08/27 19:33:37 valtri Exp $
*/

#ifndef _NOSEFART_TYPES_H_
#define _NOSEFART_TYPES_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Define this if running on little-endian (x86) systems */

#ifndef DCPLAYA
# define  HOST_LITTLE_ENDIAN
#endif

#ifdef __GNUC__
#define  INLINE      static inline
#elif defined(WIN32)
#define  INLINE      static __inline
#else /* crapintosh? */
#define  INLINE      static
#endif

/* These should be changed depending on the platform */



#ifdef __BEOS__		/* added by Eli Dayan (for compiling under BeOS) */
	
	/* use types in the BeOS Support Kit instead */
	#include <be/support/SupportDefs.h>
#elif defined (DCPLAYA) /* $$$ added by ben (for compiling with dcplaya) */
# include <arch/types.h>
#else	
	typedef  char     int8;
	typedef  short    int16;
	typedef  int      int32;

	typedef  unsigned char  uint8;
	typedef  unsigned short uint16;
	typedef  unsigned int   uint32;

#endif

typedef  uint8    boolean;

#ifndef  TRUE
#define  TRUE     1
#endif
#ifndef  FALSE
#define  FALSE    0
#endif

#ifndef  NULL
#define  NULL     ((void *) 0)
#endif

#ifdef NOFRENDO_DEBUG
#include <stdlib.h>
#include "memguard.h"
#include "log.h"
#define  ASSERT(expr)      if (FALSE == (expr))\
                           {\
                             log_printf("ASSERT: line %d of %s\n", __LINE__, __FILE__);\
                             log_shutdown();\
                             exit(1);\
                           }
#define  ASSERT_MSG(msg)   {\
                             log_printf("ASSERT: %s\n", msg);\
                             log_shutdown();\
                             exit(1);\
                           }
#else /* Not debugging */
#include "memguard.h"
#define  ASSERT(expr)
#define  ASSERT_MSG(msg)
#endif

#endif /* _NOSEFART_TYPES_H_ */

/*
** $Log: types.h,v $
** Revision 1.4  2004/08/27 19:33:37  valtri
** MINGW32 port. Engine library and most of plugins compiles now.
**
** List of some changes:
**  - replaced some _MSC_VER by more common WIN32
**  - define INTLDIR, remove -static flag for included intl
**  - shared more common CFLAGS with DEBUG_CFLAGS
**  - use WIN32_CFLAGS for all building
**  - separate some flags into THREAD_CFLAGS_CONFIG,
**    THREAD_CFLAGS_CONFIG and ZLIB_LIB_CONFIG for public xine-config,
**    automatically use internal libs if necessary
**  - don't warn about missing X for mingw and cygwin
**  - libw32dll disabled for WIN32 (making native loader would be
**    interesting, or porting wine code to Windows? :->)
**  - DVB and RTP disabled for WIN32, not ported yet
**  - fix build and fix a warning in cdda
**  - fix build for nosefart and libfaad
**  - implement configure option --disable-freetype
**  - sync libxine.pc and xine-config.in
**  - add -liberty to goom under WIN32
**  - move original build files from included phread and zlib into archives
**    and replace them by autotools
**
** Revision 1.3  2003/01/11 15:53:53  tmmm
** make the Nosefart engine aware of the config's WORDS_BIGENDIAN #define
**
** Revision 1.2  2003/01/09 19:50:04  jkeil
** NSF audio files were crashing on SPARC.
**
** - Define the correct HOST_ENDIAN for SPARC
** - remove unaligned memory accesses
**
** Revision 1.1  2003/01/08 07:04:36  tmmm
** initial import of Nosefart sources
**
** Revision 1.7  2000/07/04 04:46:44  matt
** moved INLINE define from osd.h
**
** Revision 1.6  2000/06/09 15:12:25  matt
** initial revision
**
*/
