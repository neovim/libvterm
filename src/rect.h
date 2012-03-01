/*
 * Some utility functions on VTermRect structures
 */

#define STRFrect "(%d,%d-%d,%d)"
#define ARGSrect(r) (r).start_row, (r).start_col, (r).end_row, (r).end_col

/* Expand dst to contain src as well */
static void rect_expand(VTermRect *dst, VTermRect *src)
{
  if(dst->start_row > src->start_row) dst->start_row = src->start_row;
  if(dst->start_col > src->start_col) dst->start_col = src->start_col;
  if(dst->end_row   < src->end_row)   dst->end_row   = src->end_row;
  if(dst->end_col   < src->end_col)   dst->end_col   = src->end_col;
}
