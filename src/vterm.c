#include "vterm_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

/*****************
 * API functions *
 *****************/

vterm_t *vterm_new(int rows, int cols)
{
  vterm_t *vt = g_new0(struct vterm_s, 1);

  vt->rows = rows;
  vt->cols = cols;

  vt->inbuffer = g_string_new(NULL);
  vt->outbuffer = g_string_new(NULL);

  return vt;
}

void vterm_get_size(vterm_t *vt, int *rowsp, int *colsp)
{
  if(rowsp)
    *rowsp = vt->rows;
  if(colsp)
    *colsp = vt->cols;
}

void vterm_set_size(vterm_t *vt, int rows, int cols)
{
  vt->rows = rows;
  vt->cols = cols;
}

void vterm_set_parser_callbacks(vterm_t *vt, const vterm_parser_callbacks_t *callbacks)
{
  vt->parser_callbacks = callbacks;
}

void vterm_parser_set_utf8(vterm_t *vt, int is_utf8)
{
  vt->is_utf8 = is_utf8;
}

void vterm_push_bytes(vterm_t *vt, const char *bytes, size_t len)
{
  if((vt->inbuffer->len)) {
    g_string_append_len(vt->inbuffer, bytes, len);
    size_t eaten = vterm_parser_interpret_bytes(vt, vt->inbuffer->str, vt->inbuffer->len);
    g_string_erase(vt->inbuffer, 0, eaten);
  }
  else {
    size_t eaten = vterm_parser_interpret_bytes(vt, bytes, len);
    if(eaten < len)
      g_string_append_len(vt->inbuffer, bytes + eaten, len - eaten);
  }
}

void vterm_push_output_bytes(vterm_t *vt, const char *bytes, size_t len)
{
  g_string_append_len(vt->outbuffer, bytes, len);
}

void vterm_push_output_vsprintf(vterm_t *vt, const char *format, va_list args)
{
  g_string_append_vprintf(vt->outbuffer, format, args);
}

void vterm_push_output_sprintf(vterm_t *vt, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vterm_push_output_vsprintf(vt, format, args);
  va_end(args);
}

size_t vterm_output_bufferlen(vterm_t *vt)
{
  return vt->outbuffer->len;
}

size_t vterm_output_bufferread(vterm_t *vt, char *buffer, size_t len)
{
  if(len > vt->outbuffer->len)
    len = vt->outbuffer->len;

  strncpy(buffer, vt->outbuffer->str, len);

  g_string_erase(vt->outbuffer, 0, len);

  return len;
}
