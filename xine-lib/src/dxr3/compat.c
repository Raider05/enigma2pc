/*
 * Copyright (C) 2010 the xine project
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

/* For compatibility with em8300 < 0.18.0 */
int dxr3_compat_ioctl (int fd, int rq, void *arg)
{
  int ret = ioctl (fd, rq, arg);
  if (ret < 0 && errno == EINVAL || errno == ENOTTY)
    ret = ioctl (fd, rq & 0xFF, arg);
  return ret;
}
