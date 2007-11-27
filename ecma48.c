#include "ecma48_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

/*****************
 * API functions *
 *****************/

ecma48_t *ecma48_new(void)
{
  ecma48_t *e48 = g_new0(struct ecma48_s, 1);

  e48->buffer = g_string_new(NULL);

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
  if((e48->buffer->len)) {
    g_string_append_len(e48->buffer, bytes, len);
    size_t eaten = ecma48_parser_interpret_bytes(e48, e48->buffer->str, e48->buffer->len);
    g_string_erase(e48->buffer, 0, eaten);
  }
  else {
    size_t eaten = ecma48_parser_interpret_bytes(e48, bytes, len);
    if(eaten < len)
      g_string_append_len(e48->buffer, bytes, len);
  }
}
