// Extracted from Git's `help.c`

#include "util.h"

struct cmdnames {
  int alloc;
  int cnt;
  struct cmdname {
    size_t len; /* also used for similarity index in help.c */
    char name[];
  } **names;
};

void add_cmdname(struct cmdnames *cmds, const char *name, int len)
{
  struct cmdname *ent;
  FLEX_ALLOC_MEM(ent, name, name, len);
  ent->len = len;

  ALLOC_GROW(cmds->names, cmds->cnt + 1, cmds->alloc);
  cmds->names[cmds->cnt++] = ent;
}
