#include "ecma48_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

/*****************
 * API functions *
 *****************/

ecma48_state_t *ecma48_state_new(void)
{
  ecma48_state_t *state = g_new0(struct ecma48_state, 1);

  state->buffer = g_string_new(NULL);

  return state;
}

void ecma48_state_set_parser_callbacks(ecma48_state_t *state, ecma48_parser_callbacks_t *callbacks)
{
  state->parser_callbacks = callbacks;
}

void ecma48_state_push_bytes(ecma48_state_t *state, char *bytes, size_t len)
{
  if((state->buffer->len)) {
    g_string_append_len(state->buffer, bytes, len);
    size_t eaten = ecma48_parser_interpret_bytes(state, state->buffer->str, state->buffer->len);
    g_string_erase(state->buffer, 0, eaten);
  }
  else {
    size_t eaten = ecma48_parser_interpret_bytes(state, bytes, len);
    if(eaten < len)
      g_string_append_len(state->buffer, bytes, len);
  }
}
