#include "ecma48_internal.h"

#include <stdio.h>

static void ecma48_on_parser_text(ecma48_state_t *state, char *bytes, size_t len)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->text)
    done = (*state->parser_callbacks->text)(state, bytes, len);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled text (%d bytes): %.*s\n", len, len, bytes);
}

static void ecma48_on_parser_control(ecma48_state_t *state, char control)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->control)
    done = (*state->parser_callbacks->control)(state, control);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled control 0x%02x\n", control);
}

static void ecma48_on_parser_escape(ecma48_state_t *state, char escape)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->escape)
    done = (*state->parser_callbacks->escape)(state, escape);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled escape ESC 0x%02x\n", escape);
}

static void ecma48_on_parser_csi(ecma48_state_t *state, char *args)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->csi)
    done = (*state->parser_callbacks->csi)(state, args);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled CSI %s\n", args);
}

size_t ecma48_parser_interpret_bytes(ecma48_state_t *state, char *bytes, size_t len)
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
        ecma48_on_parser_escape(state, c);
        in_esc = FALSE;
        eaten = pos;
      }
    }
    else if(in_csi) {
      if(c >= 0x40 && c <= 0x7f) {
        char *csi_args = g_strndup(bytes + csi_start, pos - csi_start + 1);
        ecma48_on_parser_csi(state, csi_args);
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
          ecma48_on_parser_control(state, c);
          eaten = pos;
          break;
        }
      }
      else {
        size_t start = pos;

        while(pos < len && bytes[pos+1] >= 0x20)
          pos++;

        ecma48_on_parser_text(state, bytes + start, pos - start + 1);
        eaten = pos;
      }
    }
  }

  return eaten;
}
