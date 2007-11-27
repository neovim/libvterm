#include "ecma48_internal.h"

#include <stdio.h>

static void ecma48_on_parser_text(ecma48_t *e48, char *bytes, size_t len)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->text)
    done = (*e48->parser_callbacks->text)(e48, bytes, len);

  if(!done && e48->state)
    done = ecma48_state_on_text(e48, bytes, len);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled text (%d bytes): %.*s\n", len, len, bytes);
}

static void ecma48_on_parser_control(ecma48_t *e48, char control)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->control)
    done = (*e48->parser_callbacks->control)(e48, control);

  if(!done && e48->state)
    done = ecma48_state_on_control(e48, control);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled control 0x%02x\n", control);
}

static void ecma48_on_parser_escape(ecma48_t *e48, char escape)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->escape)
    done = (*e48->parser_callbacks->escape)(e48, escape);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled escape ESC 0x%02x\n", escape);
}

static void ecma48_on_parser_csi(ecma48_t *e48, char *args, size_t arglen, char command)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->csi)
    done = (*e48->parser_callbacks->csi)(e48, args, arglen, command);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled CSI %.*s %c\n", arglen, args, command);
}

size_t ecma48_parser_interpret_bytes(ecma48_t *e48, char *bytes, size_t len)
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
        ecma48_on_parser_escape(e48, c);
        in_esc = FALSE;
        eaten = pos;
      }
    }
    else if(in_csi) {
      if(c >= 0x40 && c <= 0x7f) {
        ecma48_on_parser_csi(e48, bytes + csi_start, pos - csi_start, c);
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
          ecma48_on_parser_control(e48, c);
          eaten = pos;
          break;
        }
      }
      else {
        size_t start = pos;

        while(pos < len && bytes[pos+1] >= 0x20)
          pos++;

        ecma48_on_parser_text(e48, bytes + start, pos - start + 1);
        eaten = pos;
      }
    }
  }

  return eaten;
}
