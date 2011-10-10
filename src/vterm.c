#include "vterm_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <glib.h>

/*****************
 * API functions *
 *****************/

void *vterm_allocator_new(VTerm *vt, size_t size)
{
  return g_malloc0(size);
}

void vterm_allocator_free(VTerm *vt, void *ptr)
{
  g_free(ptr);
}

VTerm *vterm_new(int rows, int cols)
{
  VTerm *vt = g_new0(VTerm, 1);

  vt->rows = rows;
  vt->cols = cols;

  vt->inbuffer_len = 64;
  vt->inbuffer_cur = 0;
  vt->inbuffer = vterm_allocator_new(vt, vt->inbuffer_len);

  vt->outbuffer_len = 64;
  vt->outbuffer_cur = 0;
  vt->outbuffer = vterm_allocator_new(vt, vt->outbuffer_len);

  return vt;
}

void vterm_free(VTerm *vt)
{
  if(vt->screen)
    vterm_screen_free(vt->screen);

  if(vt->state)
    vterm_state_free(vt->state);

  vterm_allocator_free(vt, vt->inbuffer);
  vterm_allocator_free(vt, vt->outbuffer);

  vterm_allocator_free(vt, vt);
}

void vterm_get_size(VTerm *vt, int *rowsp, int *colsp)
{
  if(rowsp)
    *rowsp = vt->rows;
  if(colsp)
    *colsp = vt->cols;
}

void vterm_set_size(VTerm *vt, int rows, int cols)
{
  vt->rows = rows;
  vt->cols = cols;

  if(vt->parser_callbacks && vt->parser_callbacks->resize)
    (*vt->parser_callbacks->resize)(rows, cols, vt->cbdata);
}

void vterm_set_parser_callbacks(VTerm *vt, const VTermParserCallbacks *callbacks, void *user)
{
  vt->parser_callbacks = callbacks;
  vt->cbdata = user;
}

void vterm_parser_set_utf8(VTerm *vt, int is_utf8)
{
  vt->is_utf8 = is_utf8;
}

void vterm_push_bytes(VTerm *vt, const char *bytes, size_t len)
{
  if(vt->inbuffer_cur) {
    if(len > vt->inbuffer_len - vt->inbuffer_cur) {
      fprintf(stderr, "vterm_push_bytes(): buffer overflow; truncating input\n");
      len = vt->inbuffer_len - vt->inbuffer_cur;
    }
    memcpy(vt->inbuffer + vt->inbuffer_cur, bytes, len);
    vt->inbuffer_cur += len;

    size_t eaten = vterm_parser_interpret_bytes(vt, vt->inbuffer, vt->inbuffer_cur);

    if(eaten < vt->inbuffer_cur) {
      memmove(vt->inbuffer, vt->inbuffer + eaten, vt->inbuffer_cur - eaten);
    }

    vt->inbuffer_cur -= eaten;
  }
  else {
    size_t eaten = vterm_parser_interpret_bytes(vt, bytes, len);
    if(eaten < len) {
      bytes += eaten;
      len   -= eaten;

      if(len > vt->inbuffer_len) {
        fprintf(stderr, "vterm_push_bytes(): buffer overflow; truncating input\n");
        len = vt->inbuffer_len;
      }

      memcpy(vt->inbuffer, bytes, len);
      vt->inbuffer_cur = len;
    }
  }
}

void vterm_push_output_bytes(VTerm *vt, const char *bytes, size_t len)
{
  if(len > vt->outbuffer_len - vt->outbuffer_cur) {
    fprintf(stderr, "vterm_push_output(): buffer overflow; truncating output\n");
    len = vt->outbuffer_len - vt->outbuffer_cur;
  }

  memcpy(vt->outbuffer + vt->outbuffer_cur, bytes, len);
  vt->outbuffer_cur += len;
}

void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args)
{
  int written = vsnprintf(vt->outbuffer + vt->outbuffer_cur,
      vt->outbuffer_len - vt->outbuffer_cur,
      format, args);
  vt->outbuffer_cur += written;
}

void vterm_push_output_sprintf(VTerm *vt, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vterm_push_output_vsprintf(vt, format, args);
  va_end(args);
}

size_t vterm_output_bufferlen(VTerm *vt)
{
  return vt->outbuffer_cur;
}

size_t vterm_output_bufferread(VTerm *vt, char *buffer, size_t len)
{
  if(len > vt->outbuffer_cur)
    len = vt->outbuffer_cur;

  memcpy(buffer, vt->outbuffer, len);

  if(len < vt->outbuffer_cur)
    memmove(vt->outbuffer, vt->outbuffer + len, vt->outbuffer_cur - len);

  vt->outbuffer_cur -= len;

  return len;
}

VTermValueType vterm_get_attr_type(VTermAttr attr)
{
  switch(attr) {
    case VTERM_ATTR_BOLD:       return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_UNDERLINE:  return VTERM_VALUETYPE_INT;
    case VTERM_ATTR_ITALIC:     return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_BLINK:      return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_REVERSE:    return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_STRIKE:     return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_FONT:       return VTERM_VALUETYPE_INT;
    case VTERM_ATTR_FOREGROUND: return VTERM_VALUETYPE_COLOR;
    case VTERM_ATTR_BACKGROUND: return VTERM_VALUETYPE_COLOR;
  }
  return 0; /* UNREACHABLE */
}

VTermValueType vterm_get_prop_type(VTermProp prop)
{
  switch(prop) {
    case VTERM_PROP_CURSORVISIBLE: return VTERM_VALUETYPE_BOOL;
    case VTERM_PROP_CURSORBLINK:   return VTERM_VALUETYPE_BOOL;
    case VTERM_PROP_ALTSCREEN:     return VTERM_VALUETYPE_BOOL;
    case VTERM_PROP_TITLE:         return VTERM_VALUETYPE_STRING;
    case VTERM_PROP_ICONNAME:      return VTERM_VALUETYPE_STRING;
  }
  return 0; /* UNREACHABLE */
}

void vterm_copy_cells(VTermRect dest,
    VTermRect src,
    void (*copycell)(VTermPos dest, VTermPos src, void *user),
    void *user)
{
  int downward  = src.start_row - dest.start_row;
  int rightward = src.start_col - dest.start_col;

  int init_row, test_row, init_col, test_col;
  int inc_row, inc_col;

  if(downward < 0) {
    init_row = dest.end_row - 1;
    test_row = dest.start_row - 1;
    inc_row = -1;
  }
  else if(downward == 0) {
    init_row = dest.start_row;
    test_row = dest.end_row;
    inc_row = +1;
  }
  else /* downward > 0 */ {
    init_row = dest.start_row;
    test_row = dest.end_row;
    inc_row = +1;
  }

  if(rightward < 0) {
    init_col = dest.end_col - 1;
    test_col = dest.start_col - 1;
    inc_col = -1;
  }
  else if(rightward == 0) {
    init_col = dest.start_col;
    test_col = dest.end_col;
    inc_col = +1;
  }
  else /* rightward > 0 */ {
    init_col = dest.start_col;
    test_col = dest.end_col;
    inc_col = +1;
  }

  VTermPos pos;
  for(pos.row = init_row; pos.row != test_row; pos.row += inc_row)
    for(pos.col = init_col; pos.col != test_col; pos.col += inc_col) {
      VTermPos srcpos = { pos.row + downward, pos.col + rightward };
      (*copycell)(pos, srcpos, user);
    }
}
