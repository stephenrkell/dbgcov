// Extracted from Git's `compat/terminal.c`

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/select.h>

int getchar_with_timeout(int timeout) {
  struct timeval tv, *tvp = NULL;
  fd_set readfds;
  int res;

again:
  if (timeout >= 0) {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    tvp = &tv;
  }

  FD_ZERO(&readfds);
  FD_SET(0, &readfds); // several levels of inline functions behind this
  res = select(1, &readfds, NULL, NULL, tvp);
  if (!res)
    return EOF;
  if (res < 0) {
    if (errno == EINTR)
      goto again;
    else
      return EOF;
  }
  return getchar();
}
