/*

  Copyright (C) 2002 Rocky Bernstein <rocky@panix.com>

  Program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA

  These are routines that probably should be in xine, but for whatever
  reason aren't - yet.
*/

#ifndef XINE_EXTRA_H
#define XINE_EXTRA_H 1

#define LOG_ERR(s, args...) \
       xine_log_err("%s:  "s"\n", __func__ , ##args)

#define LOG_MSG(s, args...) \
       xine_log_msg("%s:  "s"\n", __func__ , ##args)

#ifdef HAVE_VCDNAV
#include <cdio/types.h>
#else
#include "cdio/types.h"
#endif

/* Xine includes */
#include <xine/xine_internal.h>
#include <xine/input_plugin.h>
#include <xine/xineutils.h>

/*!
  This routine is like xine_log, except it takes a va_list instead of
  a variable number of arguments. It might be useful as a function
  pointer where one wants a specific prototype.

  In short this writes a message to buffer 'buf' and to stdout.
*/
void
xine_vlog_msg(xine_t *this, int buf, const char *format, va_list args) XINE_FORMAT_PRINTF(3, 0);

/*! This routine is like xine_log, except it takes a va_list instead
  of a variable number of arguments and writes to stderr rather than
  stdout. It might be useful as a function pointer where one wants a
  specific prototype.

  In short this writes a message to buffer 'buf' and to stderr.
*/
void xine_vlog_err(xine_t *this, int buf, const char *format, va_list args) XINE_FORMAT_PRINTF(3, 0);

/*! Call this before calling any of the xine_log_msg or xine_log_err
  routines. It sets up the xine buffer that will be used in error
  logging.

  \return true if everything went okay; false is returned if
  logging was already initialized, in which case nothing is done.

 */
bool xine_log_init(xine_t *this);

/*! This routine is like xine_log without any xine-specific paramenters.
  Before calling this routine you should have set up a xine log buffer via
  xine_log_init().

  In short this writes a message to buffer 'buf' and to stdout.

  \return true if everything went okay; false is there was
  an error, such as logging wasn't initialized. On error, nothing is
  logged.
*/
bool xine_log_msg(const char *format, ...) XINE_FORMAT_PRINTF(1, 2);

/*! This routine is like xine_log without any xine-specific paramenters.
  Before calling this routine you should have set up a xine log buffer via
  xine_log_init().

  In short this writes a message to buffer 'buf' and to stdout.

  \return true if everything went okay; false is there was
  an error, such as logging wasn't initialized. On error, nothing is
  logged.
*/
bool xine_log_err(const char *format, ...) XINE_FORMAT_PRINTF(1, 2);

/* Free all (num_mrls) MRLS. */
void xine_free_mrls(int *num_mrls, xine_mrl_t **mrls);

#endif /*XINE_EXTRA_H*/
