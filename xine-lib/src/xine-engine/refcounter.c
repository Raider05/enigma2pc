/*
 * Copyright (C) 2000-2004 the xine project
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define LOG_MODULE "refcounter"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/refcounter.h>

refcounter_t* _x_new_refcounter(void *object, void (*destructor)(void *))
{
  refcounter_t *new_refcounter;

  new_refcounter = (refcounter_t *) calloc(1, sizeof(refcounter_t));
  new_refcounter->count      = 1;
  new_refcounter->object     = object;
  new_refcounter->destructor = destructor;
  pthread_mutex_init (&new_refcounter->lock, NULL);
  lprintf("new referenced object %p\n", object);
  return new_refcounter;
}

int _x_refcounter_inc(refcounter_t *refcounter)
{
  int res;

  pthread_mutex_lock(&refcounter->lock);
  if (!refcounter->count)
    _x_abort();
  res = ++refcounter->count;
  pthread_mutex_unlock(&refcounter->lock);

  return res;
}

int _x_refcounter_dec(refcounter_t *refcounter)
{
  int res;

  pthread_mutex_lock(&refcounter->lock);
  res = --refcounter->count;
  pthread_mutex_unlock(&refcounter->lock);
  if (!res) {
    lprintf("calling destructor of object %p\n", refcounter->object);
    refcounter->destructor(refcounter->object);
  }

  return res;
}

void _x_refcounter_dispose(refcounter_t *refcounter)
{
  pthread_mutex_destroy (&refcounter->lock);
  free(refcounter);
}
