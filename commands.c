#include "ecma48_internal.h"

void ecma48_on_parser_text(ecma48_state_t *state, char *bytes, size_t len)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->text)
    done = (*state->parser_callbacks->text)(state, bytes, len);
}

void ecma48_on_parser_control(ecma48_state_t *state, char control)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->control)
    done = (*state->parser_callbacks->control)(state, control);
}

void ecma48_on_parser_escape(ecma48_state_t *state, char escape)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->escape)
    done = (*state->parser_callbacks->escape)(state, escape);
}

void ecma48_on_parser_csi(ecma48_state_t *state, char *args)
{
  int done = 0;

  if(state->parser_callbacks &&
     state->parser_callbacks->csi)
    done = (*state->parser_callbacks->csi)(state, args);
}
