/*
 * Copyright (C) 2000-2003 the xine project
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

#include <stdio.h>
#include <pthread.h>
#include <xine/xineutils.h>

/*
#define DBG_MUTEX
*/

int xine_mutex_init (xine_mutex_t *mutex, const pthread_mutexattr_t *mutexattr,
		     const char *id) {

#ifdef DBG_MUTEX
  strncpy (mutex->id, id, sizeof (mutex->id));
  mutex->id[sizeof (mutex->id) - 1] = 0;
#endif

  return pthread_mutex_init (&mutex->mutex, mutexattr);
}

int xine_mutex_lock (xine_mutex_t *mutex, const char *who) {

#ifndef DBG_MUTEX

  return pthread_mutex_lock (&mutex->mutex);

#else

  if (pthread_mutex_trylock (&mutex->mutex)) {
    printf ("xine_mutex: BLOCK when %s tried to lock mutex %s because it is locked by %s. continue trying...)\n",
	    who, mutex->id, mutex->locked_by);

    pthread_mutex_lock (&mutex->mutex);
  }

  printf ("xine_mutex: %s has now locked mutex %s\n",
	  who, mutex->id);
  mutex->locked_by = who;

  return 1;

#endif
}

int xine_mutex_unlock  (xine_mutex_t *mutex, const char *who) {

  printf ("xine_mutex: mutex %s unlocked by %s\n",
	  mutex->id, who);
  return pthread_mutex_unlock (&mutex->mutex);
}

int xine_mutex_destroy (xine_mutex_t *mutex) {
  return pthread_mutex_destroy (&mutex->mutex);
}



