#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* This function will leak a small amount of memory */
int xine_private_setenv(const char *name, const char *val) {
  int len;
  char *env;

  len = strlen(name) + strlen(val) + 2;
  env = malloc(len);
  if (env != NULL) {
    snprintf(env, len, "%s=%s", name, val);
    putenv(env);
    return 0;
  } else return -1;
}

