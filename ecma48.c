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

  return e48;
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
