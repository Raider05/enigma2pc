/*
 * Copyright (C) 2008-2012 the xine-project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <xine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define XINE_LIST_VERSION_N(x,y) #x"."#y
#define XINE_LIST_VERSION XINE_LIST_VERSION_N(XINE_MAJOR_VERSION,XINE_MINOR_VERSION)

int main (int argc, char *argv[])
{
  int optstate = 0;
  int which = 'm';
  int lf = 0;

  for (;;)
  {
#define OPTS "hvaemp"
#ifdef HAVE_GETOPT_LONG
    static const struct option longopts[] = {
      { "help", no_argument, NULL, 'h' },
      { "version", no_argument, NULL, 'v' },
      { "mime-types", no_argument, NULL, 'm' },
      { "extensions", no_argument, NULL, 'e' },
      { "all", no_argument, NULL, 'a' },
      { NULL }
    };
    int index = 0;
    int opt = getopt_long (argc, argv, OPTS, longopts, &index);
#else
    int opt = getopt(argc, argv, OPTS);
#endif
    if (opt == -1)
      break;

    switch (opt)
    {
    case 'h':
      optstate |= 1;
      break;
    case 'v':
      optstate |= 4;
      break;
    case 'a':
    case 'e':
    case 'm':
      which = opt;
      break;
    case 'p':
      lf = 1;
      break;
    default:
      optstate |= 2;
      break;
    }
  }

  if (optstate & 1)
    printf ("\
xine-list-"XINE_LIST_VERSION" %s\n\
using xine-lib %s\n\
usage: %s [options]\n\
options:\n\
  -h, --help		this help text\n\
  -m, --mime-types	list just the supported MIME types\n\
  -e, --extensions	list just the recognised filename extensions\n\
  -a, --all		list everything\n\
  -p, --pretty-print	add line feeds\n\
\n", XINE_VERSION, xine_get_version_string (), argv[0]);
  else if (optstate & 4)
    printf ("\
xine-list %s\n\
using xine-lib %s\n\
(c) 2008 the xine project team\n\
This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE,\n\
to the extent permitted by law.\n",
	     XINE_VERSION, xine_get_version_string ());

  if (optstate & 2)
  {
    fputs ("xine-list: invalid option (try -h or --help)\n", stderr);
    return 1;
  }

  if (optstate)
    return 0;

  xine_t *xine = xine_new ();

  /* Avoid writing catalog.cache if possible */
  int major, minor, sub;
  xine_get_version (&major, &minor, &sub);
  if ((major == 1 && minor == 1 && sub > 20) ||
      (major == 1 && minor == 2 && sub > 0) ||
      (major == 1 && minor > 2) ||
      (major > 1))
    xine_set_flags (xine, XINE_FLAG_NO_WRITE_CACHE);

  xine_init (xine);

  char *text = NULL;
  char *sep, *sep2;
  switch (which)
  {
  case 'a':
  case 'm':
    text = xine_get_mime_types (xine);
    if (!text || !*text)
      goto read_fail;
    sep = sep2 = text - 1;
    for (;;)
    {
      text = sep + 1;
      sep = strchr (text, ';') ? : text + strlen (text);
      sep2 = which == 'a' ? sep : strchr (text, ':') ? : sep;
      if (!*sep)
        break;
      if (printf ("%.*s;", (int)(sep2 - text), text) < 0 || (lf && puts ("") < 0))
        goto write_fail;
    }
    break;

  case 'e':
    text = xine_get_file_extensions (xine);
    if (!text || !*text)
      goto read_fail;
    sep = text - 1;
    do
    {
      text = sep + 1;
      sep = strchr (text, ' ') ? : text + strlen (text);
      if (sep[-1] != '/' &&
          printf ("%.*s%s", (int)(sep - text), text, lf ? "\n" : *sep ? " " : "") < 0)
        goto write_fail;
    } while (*sep);
    break;
  }

  return 0;

 read_fail:
  fputs ("xine-list: failed to read types info\n", stderr);
  return 1;

 write_fail:
  perror ("xine-list");
  return 1;
}
