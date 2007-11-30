#include "ecma48_internal.h"

#include <stdio.h>

#include <glib.h>

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
    fprintf(stderr, "libecma48: Unhandled control 0x%02x\n", (unsigned char)control);
}

static void ecma48_on_parser_escape(ecma48_t *e48, char escape)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->escape)
    done = (*e48->parser_callbacks->escape)(e48, escape);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled escape ESC 0x%02x\n", (unsigned char)escape);
}

static void ecma48_on_parser_csi(ecma48_t *e48, char *args, size_t arglen, char command)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->csi_raw)
    done = (*e48->parser_callbacks->csi_raw)(e48, args, arglen, command);

  if(done)
    return;

  if(arglen == 0 || args[0] < 0x3c || args[0] > 0x3f) {
    int argcount = 1; // Always at least 1 arg

    int i;
    for(i = 0; i < arglen; i++)
      if(args[i] == 0x3b)
        argcount++;

    // TODO: ECMA-48 allows 123:456 as an argument, but we'll ignore .
    // and trailing digits
    int *csi_args = g_alloca(argcount * sizeof(int));

    int argi;
    for(argi = 0; argi < argcount; argi++)
      csi_args[argi] = -1;

    argi = 0;
    int pos;
    for(pos = 0; pos < arglen; pos++) {
      switch(args[pos]) {
      case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
      case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
        if(csi_args[argi] == -1)
          csi_args[argi] = 0;
        csi_args[argi] *= 10;
        csi_args[argi] += args[pos] - '0';
        break;
      case 0x3b:
        argi++;
        break;
      default:
        fprintf(stderr, "TODO: Parse %c in CSI\n", args[pos]);
        break;
      }
    }

    //printf("Parsed CSI args %.*s as:", arglen, args);
    //for(argi = 0; argi < argcount; argi++)
    //  printf(" %d", csi_args[argi]);
    //printf("\n");

    if(e48->parser_callbacks &&
       e48->parser_callbacks->csi)
      done = (*e48->parser_callbacks->csi)(e48, csi_args, argcount, command);

    if(!done && e48->state)
      done = ecma48_state_on_csi(e48, csi_args, argcount, command);
  }

  if(!done)
    fprintf(stderr, "libecma48: Unhandled CSI %.*s %c\n", arglen, args, command);
}

size_t ecma48_parser_interpret_bytes(ecma48_t *e48, char *bytes, size_t len)
{
  size_t pos = 0;
  size_t pos_end = 0;

  gboolean in_esc = FALSE;
  gboolean in_csi = FALSE;
  size_t csi_start;

  for(pos = 0; pos < len; pos++) {
    unsigned char c = bytes[pos];

    if(in_esc) {
      switch(c) {
      case 0x5b: // CSI
        in_csi = TRUE; in_esc = FALSE;
        csi_start = pos + 1;
        break;
      default:
        ecma48_on_parser_escape(e48, c);
        in_esc = FALSE;
        pos_end = pos;
      }
    }
    else if(in_csi) {
      if(c >= 0x40 && c <= 0x7f) {
        ecma48_on_parser_csi(e48, bytes + csi_start, pos - csi_start, c);
        in_csi = FALSE;
        pos_end = pos;
      }
    }
    else {
      if(c < 0x20 || (c >= 0x80 && c < 0x9f)) {
        switch(c) {
        case 0x1b: // ESC
          in_esc = TRUE; break;
        case 0x9b: // CSI
          in_csi = TRUE; in_esc = FALSE;
          csi_start = pos + 1;
          break;
        default:
          ecma48_on_parser_control(e48, c);
          pos_end = pos;
          break;
        }
      }
      else {
        size_t start = pos;

        while((pos+1) < len && bytes[pos+1] >= 0x20)
          pos++;

        ecma48_on_parser_text(e48, bytes + start, pos - start + 1);
        pos_end = pos;
      }
    }
  }

  return pos_end + 1;
}
