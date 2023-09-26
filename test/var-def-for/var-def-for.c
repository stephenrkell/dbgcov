// Extracted from Git's `xdiff/xprepare.c`

#define XDL_MIN(a, b) ((a) < (b) ? (a): (b))

typedef struct {
  unsigned long ha;
} xrecord_t;

typedef struct {
  long nrec;
  long dstart, dend;
  xrecord_t **recs;
} xdfile_t;

int xdl_trim_ends(xdfile_t *xdf1, xdfile_t *xdf2) {
  long i, lim;
  xrecord_t **recs1, **recs2;

  recs1 = xdf1->recs;
  recs2 = xdf2->recs;
  for (i = 0, lim = XDL_MIN(xdf1->nrec, xdf2->nrec); i < lim;
       i++, recs1++, recs2++)
    if ((*recs1)->ha != (*recs2)->ha)
      break;

  xdf1->dstart = xdf2->dstart = i;

  recs1 = xdf1->recs + xdf1->nrec - 1;
  recs2 = xdf2->recs + xdf2->nrec - 1;
  for (lim -= i, i = 0; i < lim; i++, recs1--, recs2--)
    if ((*recs1)->ha != (*recs2)->ha)
      break;

  xdf1->dend = xdf1->nrec - i - 1;
  xdf2->dend = xdf2->nrec - i - 1;

  return 0;
}
