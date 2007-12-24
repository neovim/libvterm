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
  vterm_t *e48 = g_new0(struct vterm_s, 1);

  e48->rows = rows;
  e48->cols = cols;

  e48->inbuffer = g_string_new(NULL);
  e48->outbuffer = g_string_new(NULL);

  return e48;
}

void vterm_get_size(vterm_t *e48, int *rowsp, int *colsp)
{
  if(rowsp)
    *rowsp = e48->rows;
  if(colsp)
    *colsp = e48->cols;
}

void vterm_set_size(vterm_t *e48, int rows, int cols)
{
  e48->rows = rows;
  e48->cols = cols;
}

void vterm_set_parser_callbacks(vterm_t *e48, const vterm_parser_callbacks_t *callbacks)
{
  e48->parser_callbacks = callbacks;
}

void vterm_parser_set_utf8(vterm_t *e48, int is_utf8)
{
  e48->is_utf8 = is_utf8;
}

void vterm_push_bytes(vterm_t *e48, const char *bytes, size_t len)
{
  if((e48->inbuffer->len)) {
    g_string_append_len(e48->inbuffer, bytes, len);
    size_t eaten = vterm_parser_interpret_bytes(e48, e48->inbuffer->str, e48->inbuffer->len);
    g_string_erase(e48->inbuffer, 0, eaten);
  }
  else {
    size_t eaten = vterm_parser_interpret_bytes(e48, bytes, len);
    if(eaten < len)
      g_string_append_len(e48->inbuffer, bytes + eaten, len - eaten);
  }
}

void vterm_push_output_bytes(vterm_t *e48, const char *bytes, size_t len)
{
  g_string_append_len(e48->outbuffer, bytes, len);
}

void vterm_push_output_vsprintf(vterm_t *e48, const char *format, va_list args)
{
  g_string_append_vprintf(e48->outbuffer, format, args);
}

void vterm_push_output_sprintf(vterm_t *e48, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vterm_push_output_vsprintf(e48, format, args);
  va_end(args);
}

size_t vterm_output_bufferlen(vterm_t *e48)
{
  return e48->outbuffer->len;
}

size_t vterm_output_bufferread(vterm_t *e48, char *buffer, size_t len)
{
  if(len > e48->outbuffer->len)
    len = e48->outbuffer->len;

  strncpy(buffer, e48->outbuffer->str, len);

  g_string_erase(e48->outbuffer, 0, len);

  return len;
}
