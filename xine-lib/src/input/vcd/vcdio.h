/*
  $Id: vcdio.h,v 1.3 2005/01/08 15:12:42 rockyb Exp $

  Copyright (C) 2002, 2004, 2005 Rocky Bernstein <rocky@panix.com>

  This program is free software; you can redistribute it and/or modify
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

*/

#ifndef _VCDIO_H_
#define _VCDIO_H_

/*!
  From xine plugin spec:

  read nlen bytes, return number of bytes read.
*/
off_t
vcdio_read (vcdplayer_t *p_vcdplayer, char *psz_buf, const off_t nlen);

/*! Opens VCD device and initializes things.

   - do nothing if the device had already been open and is the same device.
   - if the device had been open and is a different, close it before trying
     to open new device.
*/
bool
vcdio_open(vcdplayer_t *p_vcdplayer, char *psz_device);

/*! Closes VCD device specified via "this", and also wipes memory of it
   from it inside "this". */
/* FIXME Move player stuff to player. */
int
vcdio_close(vcdplayer_t *p_vcdplayer);

/*!
  From xine plugin spec:

  seek position, return new position

  if seeking failed, -1 is returned
*/
off_t
vcdio_seek (vcdplayer_t *p_vcdplayer, off_t offset, int origin);

#endif /* _VCDIO_H_ */

/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
