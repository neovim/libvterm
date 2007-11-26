#include "ecma48_internal.h"

#include <stdio.h>

void ecma48_on_parser_text(ecma48_state_t *state, char *bytes, size_t len)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->text)
    done = (*state->parser_callbacks->text)(state, bytes, len);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled text (%d bytes): %.*s\n", len, len, bytes);
}

void ecma48_on_parser_control(ecma48_state_t *state, char control)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->control)
    done = (*state->parser_callbacks->control)(state, control);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled control 0x%02x\n", control);
}

void ecma48_on_parser_escape(ecma48_state_t *state, char escape)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->escape)
    done = (*state->parser_callbacks->escape)(state, escape);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled escape ESC 0x%02x\n", escape);
}

void ecma48_on_parser_csi(ecma48_state_t *state, char *args)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->csi)
    done = (*state->parser_callbacks->csi)(state, args);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled CSI %s\n", args);
}
