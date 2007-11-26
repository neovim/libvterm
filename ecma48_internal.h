#ifndef __ECMA48_INTERNAL_H__
#define __ECMA48_INTERNAL_H__

#include "ecma48.h"

#include <glib.h>

struct ecma48_state
{
  ecma48_parser_callbacks_t *parser_callbacks;

  GString *buffer;
};

size_t ecma48_parser_interpret_bytes(ecma48_state_t *state, char *bytes, size_t len);

void ecma48_on_parser_text   (ecma48_state_t *state, char *bytes, size_t len);
void ecma48_on_parser_control(ecma48_state_t *state, char control);
void ecma48_on_parser_escape (ecma48_state_t *state, char escape);
void ecma48_on_parser_csi    (ecma48_state_t *state, char *args);

#endif
