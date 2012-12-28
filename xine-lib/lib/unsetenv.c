#include "config.h"

#include <stdlib.h>

void xine_private_unsetenv(const char *name) {
  putenv(name);
}
