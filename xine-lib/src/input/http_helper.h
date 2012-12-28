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
 *
 * URL helper functions
 */

#ifndef HTTP_HELPER_H
#define HTTP_HELPER_H

#include <xine/attributes.h>
#include <xine/xine_internal.h>

/*
 * user agent finder, using modified protcol names
 * {proto}://...
 * e.g. "qthttp://example.com/foo.mov" → "QuickTime"
 *
 * return:
 *   NULL or user agent prefix
 */
const char *_x_url_user_agent (const char *url);

/*
 * url parser
 * {proto}://{user}:{password}@{host}:{port}{uri}
 * {proto}://{user}:{password}@{[host]}:{port}{uri}
 *
 * return:
 *   0  invalid url
 *   1  valid url
 */
int _x_parse_url (char *url, char **proto, char** host, int *port,
                  char **user, char **password, char **uri,
                  const char **user_agent);

/*
 * canonicalise url, given base
 * base must be valid according to _x_parse_url
 * url may only contain "://" if it's absolute
 *
 * return:
 *   the canonicalised URL (caller must free() it)
 *   NULL if error
 */
static inline XINE_MALLOC char *_x_canonicalise_url (const char *base, const char *url) {

  size_t base_length;
  char *cut;

  if ((cut = strstr (url, "://")))
    return strdup (url);

  cut = strstr (base, "://");
  if (url[0] == '/') {
    /* absolute - base up to first '/' after "://", then url */
    cut = strchr (cut + 3, '/');
  }
  else {
    /* relative - base up to & inc. last '/', then url */
    cut = strrchr (cut, '/');
    if (cut)
      ++cut;
  }
  base_length = cut ? (size_t)(cut - base) : strlen (base);

  return _x_asprintf ("%.*s%s", (int)base_length, base, url);
}

#endif /* HTTP_HELPER_H */
