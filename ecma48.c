#include "ecma48.h"

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

struct ecma48_state
{
  ecma48_parser_callbacks_t *parser_callbacks;

  GString *buffer;
};

size_t interpret_bytes(ecma48_state_t *state, char *bytes, size_t len)
{
  size_t pos = 0;

  size_t eaten = 0;

  gboolean in_esc = FALSE;
  gboolean in_csi = FALSE;
  size_t csi_start;

  for(pos = 0; pos < len; pos++) {
    unsigned char c = bytes[pos];

    if(in_esc) {
      switch(c) {
      case '[': // CSI
        in_csi = TRUE; in_esc = FALSE;
        csi_start = pos;
        break;
      default:
        (*state->parser_callbacks->escape)(state, c);
        in_esc = FALSE;
        eaten = pos;
      }
    }
    else if(in_csi) {
      if(c >= 0x40 && c <= 0x7f) {
        char *csi_args = g_strndup(bytes + csi_start, pos - csi_start + 1);
        (*state->parser_callbacks->csi)(state, csi_args);
        in_csi = FALSE;
        eaten = pos;
      }
    }
    else {
      if(c < 0x20 || (c >= 0x80 && c < 0x9f)) {
        switch(c) {
        case 0x1b: // ESC
          in_esc = TRUE; break;
        case 0x9b: // CSI
          in_csi = TRUE; in_esc = FALSE;
          csi_start = pos;
          break;
        default:
          (*state->parser_callbacks->control)(state, c);
          eaten = pos;
          break;
        }
      }
      else {
        size_t start = pos;

        while(pos < len && bytes[pos+1] >= 0x20)
          pos++;

        (*state->parser_callbacks->text)(state, bytes + start, pos - start + 1);
        eaten = pos;
      }
    }
  }

  return eaten;
}

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
    size_t eaten = interpret_bytes(state, state->buffer->str, state->buffer->len);
    g_string_erase(state->buffer, 0, eaten);
  }
  else {
    size_t eaten = interpret_bytes(state, bytes, len);
    if(eaten < len)
      g_string_append_len(state->buffer, bytes, len);
  }
}
