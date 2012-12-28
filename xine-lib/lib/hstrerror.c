#include "config.h"

#ifdef WIN32
#include <winsock.h>
#else
#include <netdb.h>
#endif
#include <errno.h>

#include "xine/xineintl.h"

/**
 * get error descriptions in DNS lookups
 */
const char *xine_private_hstrerror(int err) {
  switch (err) {
    case 0: return _("No error");
    case HOST_NOT_FOUND: return _("Unknown host");
    case NO_DATA: return _("No address associated with name");
    case NO_RECOVERY: return _("Unknown server error");
    case TRY_AGAIN: return _("Host name lookup failure");
    default: return _("Unknown error");
  }
}
