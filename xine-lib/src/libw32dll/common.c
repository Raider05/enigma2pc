#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

static char *get_win32_codecs_path(config_values_t *cfg) {
  DIR                *dir;
  char               *path, *cfgpath;
  char               *listpath[] = { "",
                                     "/usr/lib/codecs",
                                     "/usr/local/lib/codecs",
                                     "/usr/lib/win32",
                                     "/usr/local/lib/win32",
                                     NULL };
  int                 i = 0;

  cfgpath = cfg->register_filename (cfg, "decoder.external.win32_codecs_path", WIN32_PATH, XINE_CONFIG_STRING_IS_DIRECTORY_NAME,
					 _("path to Win32 codecs"),
					 _("If you have the Windows or Apple Quicktime codec packs "
					   "installed, specify the path the codec directory here. "
					   "If xine can find the Windows or Apple Quicktime codecs, "
					   "it will use them to decode various Windows Media and "
					   "Quicktime streams for you. Consult the xine FAQ for "
					   "more information on how to install the codecs."),
					 10, NULL, NULL);

  while (listpath[i]) {
    if (i == 0) path = cfgpath;
    else path = listpath[i];

    if ((dir = opendir(path)) != NULL) {
      closedir(dir);
      return path;
    }

    i++;
  }

  return NULL;
}
