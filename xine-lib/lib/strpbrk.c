#include <stddef.h>

/* Shamefully copied from glibc 2.2.3 */
char *xine_private_strpbrk(const char *s, const char *accept) {

  while(*s != '\0') {
    const char *a = accept;
    while(*a != '\0')
      if(*a++ == *s)
        return(char *) s;
    ++s;
  }

  return NULL;
}
