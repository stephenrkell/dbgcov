// Extracted from Git's `xdiff/xutils.c`

int xdl_num_out(char *out, long val) {
  char *ptr, *str = out;
  char buf[32];

  ptr = buf + sizeof(buf) - 1; // Report `buf` as may be defined via addr taken
  *ptr = '\0';
  if (val < 0) {
    *--ptr = '-';
    val = -val;
  }
  for (; val && ptr > buf; val /= 10)
    *--ptr = "0123456789"[val % 10];
  if (*ptr)
    for (; *ptr; ptr++, str++)
      *str = *ptr;
  else
    *str++ = '0';
  *str = '\0';

  return str - out;
}
