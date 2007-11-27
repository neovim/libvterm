#include "ecma48_internal.h"

#include <stdio.h>

struct ecma48_state_s
{
  ecma48_state_callbacks_t *callbacks;

  /* Current cursor position */
  int row;
  int col;
};

static ecma48_state_t *ecma48_state_new(void)
{
  ecma48_state_t *state = g_new0(ecma48_state_t, 1);

  state->row = 0;
  state->col = 0;

  return state;
}

void ecma48_state_free(ecma48_state_t *state)
{
  g_free(state);
}

void ecma48_set_state_callbacks(ecma48_t *e48, ecma48_state_callbacks_t *callbacks)
{
  if(callbacks) {
    if(!e48->state) {
      e48->state = ecma48_state_new();
    }
    e48->state->callbacks = callbacks;
  }
  else {
    if(e48->state) {
      ecma48_state_free(e48->state);
      e48->state = NULL;
    }
  }
}

int ecma48_state_on_text(ecma48_t *e48, char *bytes, size_t len)
{
  // TODO: Need a Unicode engine here to convert bytes into Chars
  uint32_t *chars = g_malloc(len * sizeof(uint32_t));
  int i;
  for(i = 0; i < len; i++)
    chars[i] = bytes[i];

  ecma48_state_t *state = e48->state;

  for(i = 0; i < len; i++) {
    uint32_t c = chars[i];

    if(state->col == e48->cols) {
      state->col = 0;
      // TODO: bounds checking
      state->row++;
    }

    int done = 0;

    if(state->callbacks &&
       state->callbacks->putchar)
      done = (*state->callbacks->putchar)(e48, c, state->row, state->col);

    if(!done)
      fprintf(stderr, "libecma48: Unhandled putchar U+%04x at (%d,%d)\n",
          c, state->col, state->row);

    state->col++;
  }

  return 1;
}

int ecma48_state_on_control(ecma48_t *e48, char control)
{
  ecma48_state_t *state = e48->state;

  switch(control) {
  case 0x0a: // CR
    state->col = 0;
    break;

  case 0x0d: // LF
    state->row++;
    // TODO: Bounds check for scroll
    break;

  default:
    return 0;
  }

  return 1;
}
