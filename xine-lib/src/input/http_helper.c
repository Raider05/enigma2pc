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
 *
 * URL helper functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <xine/xine_internal.h>
#include "http_helper.h"


const char *_x_url_user_agent (const char *url)
{
  if (!strncasecmp (url, "qthttp://", 9))
    return "QuickTime"; /* needed for Apple trailers */
  return NULL;
}

int _x_parse_url (char *url, char **proto, char** host, int *port,
                  char **user, char **password, char **uri,
                  const char **user_agent)
{
  char   *start      = NULL;
  char   *authcolon  = NULL;
  char	 *at         = NULL;
  char	 *portcolon  = NULL;
  char   *slash      = NULL;
  char   *semicolon  = NULL;
  char   *end        = NULL;
  char   *strtol_err = NULL;

  if (!url)      _x_abort();
  if (!proto)    _x_abort();
  if (!user)     _x_abort();
  if (!password) _x_abort();
  if (!host)     _x_abort();
  if (!port)     _x_abort();
  if (!uri)      _x_abort();

  *proto    = NULL;
  *port     = 0;
  *user     = NULL;
  *host     = NULL;
  *password = NULL;
  *uri      = NULL;

  /* proto */
  start = strstr(url, "://");
  if (!start || (start == url))
    goto error;

  end  = start + strlen(start) - 1;
  *proto = strndup(url, start - url);

  if (user_agent)
    *user_agent = _x_url_user_agent (url);

  /* user:password */
  start += 3;
  at = strchr(start, '@');
  slash = strchr(start, '/');

  /* stupid Nullsoft URL scheme */
  semicolon = strchr(start, ';');
  if (semicolon && (!slash || (semicolon < slash)))
    slash = semicolon;

  if (at && slash && (at > slash))
    at = NULL;

  if (at) {
    authcolon = strchr(start, ':');
    if(authcolon && authcolon < at) {
      *user = strndup(start, authcolon - start);
      *password = strndup(authcolon + 1, at - authcolon - 1);
      if ((authcolon == start) || (at == (authcolon + 1))) goto error;
    } else {
      /* no password */
      *user = strndup(start, at - start);
      if (at == start) goto error;
    }
    start = at + 1;
  }

  /* host:port (ipv4) */
  /* [host]:port (ipv6) */
  if (*start != '[')
  {
    /* ipv4*/
    portcolon = strchr(start, ':');
    if (slash) {
      if (portcolon && portcolon < slash) {
        *host = strndup(start, portcolon - start);
        if (portcolon == start) goto error;
        *port = strtol(portcolon + 1, &strtol_err, 10);
        if ((strtol_err != slash) || (strtol_err == portcolon + 1))
          goto error;
      } else {
        *host = strndup(start, slash - start);
        if (slash == start) goto error;
      }
    } else {
      if (portcolon) {
        *host = strndup(start, portcolon - start);
        if (portcolon < end) {
          *port = strtol(portcolon + 1, &strtol_err, 10);
          if (*strtol_err != '\0') goto error;
        } else {
          goto error;
        }
      } else {
        if (*start == '\0') goto error;
        *host = strdup(start);
      }
    }
  } else {
    /* ipv6*/
    char *hostendbracket;

    hostendbracket = strchr(start, ']');
    if (hostendbracket != NULL) {
      if (hostendbracket == start + 1) goto error;
      *host = strndup(start + 1, hostendbracket - start - 1);

      if (hostendbracket < end) {
        /* Might have a trailing port */
        if (*(hostendbracket + 1) == ':') {
          portcolon = hostendbracket + 1;
          if (portcolon < end) {
            *port = strtol(portcolon + 1, &strtol_err, 10);
            if ((*strtol_err != '\0') && (*strtol_err != '/')) goto error;
          } else {
            goto error;
          }
        }
      }
    } else {
      goto error;
    }
  }

  /* uri */
  start = slash;
  if (start) {
    /* handle crazy Nullsoft URL scheme */
    if (*start == ';') {
      /* ";stream.nsv" => "/;stream.nsv" */
      *uri = malloc(strlen(start) + 2);
      *uri[0] = '/';
      strcpy(*uri + 1, start);
    } else {
      static const char toescape[] = " #";
      char *it = start;
      unsigned int escapechars = 0;

      while( it && *it ) {
	if ( strchr(toescape, *it) != NULL )
	  escapechars++;
	it++;
      }

      if ( escapechars == 0 )
	*uri = strdup(start);
      else {
	const size_t len = strlen(start);
	size_t i;

	*uri = malloc(len + 1 + escapechars*2);
	it = *uri;

	for(i = 0; i < len; i++, it++) {
	  if ( strchr(toescape, start[i]) != NULL ) {
	    it[0] = '%';
	    it[1] = ( (start[i] >> 4) > 9 ) ? 'A' + ((start[i] >> 4)-10) : '0' + (start[i] >> 4);
	    it[2] = ( (start[i] & 0x0f) > 9 ) ? 'A' + ((start[i] & 0x0f)-10) : '0' + (start[i] & 0x0f);
	    it += 2;
	  } else
	    *it = start[i];
	}
	*it = '\0';
      }
    }
  } else {
    *uri = strdup("/");
  }

  return 1;

error:
  if (*proto) {
    free (*proto);
    *proto = NULL;
  }
  if (*user) {
    free (*user);
    *user = NULL;
  }
  if (*password) {
    free (*password);
    *password = NULL;
  }
  if (*host) {
    free (*host);
    *host = NULL;
  }
  if (*port) {
    *port = 0;
  }
  if (*uri) {
    free (*uri);
    *uri = NULL;
  }
  return 0;
}


#ifdef TEST_URL
/*
 * url parser test program
 */

static int check_url(char *url, int ok) {
  char *proto, *host, *user, *password, *uri;
  int port;
  int res;

  printf("--------------------------------\n");
  printf("url=%s\n", url);
  res = _x_parse_url (url,
                      &proto, &host, &port, &user, &password, &uri, NULL);
  if (res) {
    printf("proto=%s, host=%s, port=%d, user=%s, password=%s, uri=%s\n",
           proto, host, port, user, password, uri);
    free(proto);
    free(host);
    free(user);
    free(password);
    free(uri);
  } else {
    printf("bad url\n");
  }
  if (res == ok) {
    printf("test OK\n", url);
    return 1;
  } else {
    printf("test KO\n", url);
    return 0;
  }
}

static int check_paste(const char *base, const char *url, const char *ok) {
  char *res;
  int ret;

  printf("--------------------------------\n");
  printf("base url=%s\n", base);
  printf(" new url=%s\n", url);
  res = _x_canonicalise_url (base, url);
  printf("  result=%s\n", res);
  ret = !strcmp (res, ok);
  free (res);
  puts (ret ? "test OK" : "test KO");
  return ret;
}

int main(int argc, char** argv) {
  char *proto, host, port, user, password, uri;
  int res = 0;

  res += check_url("http://www.toto.com/test1.asx", 1);
  res += check_url("http://www.toto.com:8080/test2.asx", 1);
  res += check_url("http://titi:pass@www.toto.com:8080/test3.asx", 1);
  res += check_url("http://www.toto.com", 1);
  res += check_url("http://www.toto.com/", 1);
  res += check_url("http://www.toto.com:80", 1);
  res += check_url("http://www.toto.com:80/", 1);
  res += check_url("http://www.toto.com:", 0);
  res += check_url("http://www.toto.com:/", 0);
  res += check_url("http://www.toto.com:abc", 0);
  res += check_url("http://www.toto.com:abc/", 0);
  res += check_url("http://titi@www.toto.com:8080/test4.asx", 1);
  res += check_url("http://@www.toto.com:8080/test5.asx", 0);
  res += check_url("http://:@www.toto.com:8080/test6.asx", 0);
  res += check_url("http:///test6.asx", 0);
  res += check_url("http://:/test7.asx", 0);
  res += check_url("http://", 0);
  res += check_url("http://:", 0);
  res += check_url("http://@", 0);
  res += check_url("http://:@", 0);
  res += check_url("http://:/@", 0);
  res += check_url("http://www.toto.com:80a/", 0);
  res += check_url("http://[www.toto.com]", 1);
  res += check_url("http://[www.toto.com]/", 1);
  res += check_url("http://[www.toto.com]:80", 1);
  res += check_url("http://[www.toto.com]:80/", 1);
  res += check_url("http://[12:12]:80/", 1);
  res += check_url("http://user:pass@[12:12]:80/", 1);
  res += check_paste("http://www.toto.com/foo/test.asx", "http://www2.toto.com/www/foo/test1.asx", "http://www2.toto.com/www/foo/test1.asx");
  res += check_paste("http://www.toto.com/foo/test.asx", "/bar/test2.asx", "http://www.toto.com/bar/test2.asx");
  res += check_paste("http://www.toto.com/foo/test.asx", "test3.asx", "http://www.toto.com/foo/test3.asx");
  printf("================================\n");
  if (res != 31) {
    printf("result: KO\n");
  } else {
    printf("result: OK\n");
  }
}
#endif
