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
** log.h
**
** Error logging header file
** $Id: log.h,v 1.3 2006/04/21 23:15:45 dsalt Exp $
*/

#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>
#include <xine/attributes.h>

extern int log_init(void);
extern void log_shutdown(void);
extern void log_print(const char *string);
extern void log_printf(const char *format, ...) XINE_FORMAT_PRINTF(1, 2);

#endif /* _LOG_H_ */

/*
** $Log: log.h,v $
** Revision 1.3  2006/04/21 23:15:45  dsalt
** Add printf format attributes.
**
** Revision 1.2  2003/12/05 15:55:01  f1rmb
** cleanup phase II. use xprintf when it's relevant, use xine_xmalloc when it's relevant too. Small other little fix (can't remember). Change few internal function prototype because it xine_t pointer need to be used if some xine's internal sections. NOTE: libdvd{nav,read} is still too noisy, i will take a look to made it quit, without invasive changes. To be continued...
**
** Revision 1.1  2003/01/08 07:04:35  tmmm
** initial import of Nosefart sources
**
** Revision 1.4  2000/06/09 15:12:25  matt
** initial revision
**
*/
