#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define FLEX_ALLOC_MEM(x, flexname, buf, len) do { \
  size_t flex_array_len_ = (len); \
  (x) = calloc(1, sizeof(*(x)) + flex_array_len_ + 1); \
  memcpy((void *)(x)->flexname, (buf), flex_array_len_); \
} while (0)

#define REALLOC_ARRAY(x, alloc) (x) = realloc((x), sizeof(*(x)) * (alloc))

#define alloc_nr(x) (((x)+16)*3/2)

#define ALLOC_GROW(x, nr, alloc) \
  do { \
    if ((nr) > alloc) { \
      if (alloc_nr(alloc) < (nr)) \
        alloc = (nr); \
      else \
        alloc = alloc_nr(alloc); \
      REALLOC_ARRAY(x, alloc); \
    } \
  } while (0)
