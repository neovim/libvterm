#include "ecma48_internal.h"

void ecma48_on_parser_text(ecma48_state_t *state, char *bytes, size_t len)
{
  (*state->parser_callbacks->text)(state, bytes, len);
}

void ecma48_on_parser_control(ecma48_state_t *state, char control)
{
  (*state->parser_callbacks->control)(state, control);
}

void ecma48_on_parser_escape(ecma48_state_t *state, char escape)
{
  (*state->parser_callbacks->escape)(state, escape);
}

void ecma48_on_parser_csi(ecma48_state_t *state, char *args)
{
  (*state->parser_callbacks->csi)(state, args);
}
