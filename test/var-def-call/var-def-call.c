#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

int xsnprintf(char *dst, size_t max, const char *fmt, ...)
{
  va_list ap;
  int len;

  va_start(ap, fmt); // consider `ap` defined after this
  len = vsnprintf(dst, max, fmt, ap);
  va_end(ap);

  if (len < 0)
    printf("your snprintf is broken");
  if (len >= max)
    printf("attempt to snprintf into too-small buffer");
  return len;
}
