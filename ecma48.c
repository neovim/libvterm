#include "ecma48_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

/*****************
 * API functions *
 *****************/

ecma48_t *ecma48_new(void)
{
  ecma48_t *e48 = g_new0(struct ecma48_s, 1);

  e48->inbuffer = g_string_new(NULL);
  e48->outbuffer = g_string_new(NULL);

  // Some of the modes default to being set
  e48->mode.cursor_visible = 1;

  return e48;
}

void ecma48_get_size(ecma48_t *e48, int *rowsp, int *colsp)
{
  if(rowsp)
    *rowsp = e48->rows;
  if(colsp)
    *colsp = e48->cols;
}

void ecma48_set_size(ecma48_t *e48, int rows, int cols)
{
  e48->rows = rows;
  e48->cols = cols;
}

void ecma48_set_parser_callbacks(ecma48_t *e48, ecma48_parser_callbacks_t *callbacks)
{
  e48->parser_callbacks = callbacks;
}

void ecma48_parser_set_utf8(ecma48_t *e48, int is_utf8)
{
  e48->is_utf8 = is_utf8;
}

void ecma48_push_bytes(ecma48_t *e48, char *bytes, size_t len)
{
  if((e48->inbuffer->len)) {
    g_string_append_len(e48->inbuffer, bytes, len);
    size_t eaten = ecma48_parser_interpret_bytes(e48, e48->inbuffer->str, e48->inbuffer->len);
    g_string_erase(e48->inbuffer, 0, eaten);
  }
  else {
    size_t eaten = ecma48_parser_interpret_bytes(e48, bytes, len);
    if(eaten < len)
      g_string_append_len(e48->inbuffer, bytes, len);
  }
}

void ecma48_push_output_bytes(ecma48_t *e48, char *bytes, size_t len)
{
  g_string_append_len(e48->outbuffer, bytes, len);
}

void ecma48_push_output_vsprintf(ecma48_t *e48, char *format, va_list args)
{
  g_string_append_vprintf(e48->outbuffer, format, args);
}

void ecma48_push_output_sprintf(ecma48_t *e48, char *format, ...)
{
  va_list args;
  va_start(args, format);
  ecma48_push_output_vsprintf(e48, format, args);
  va_end(args);
}

size_t ecma48_output_bufferlen(ecma48_t *e48)
{
  return e48->outbuffer->len;
}

size_t ecma48_output_bufferread(ecma48_t *e48, char *buffer, size_t len)
{
  if(len > e48->outbuffer->len)
    len = e48->outbuffer->len;

  strncpy(buffer, e48->outbuffer->str, len);

  g_string_erase(e48->outbuffer, 0, len);

  return len;
}
