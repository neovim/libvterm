#include "ecma48_internal.h"

#include <stdio.h>

struct ecma48_state_s
{
  ecma48_state_callbacks_t *callbacks;

  /* Current cursor position */
  ecma48_position_t pos;
};

static ecma48_state_t *ecma48_state_new(void)
{
  ecma48_state_t *state = g_new0(ecma48_state_t, 1);

  state->pos.row = 0;
  state->pos.col = 0;

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

static void scroll(ecma48_t *e48)
{
  ecma48_state_t *state = e48->state;

  ecma48_rectangle_t rect = {
    .start_row = 0,
    .end_row   = e48->rows,
    .start_col = 0,
    .end_col   = e48->cols,
  };

  if(state->callbacks &&
     state->callbacks->scroll)
    (*state->callbacks->scroll)(e48, rect, 1, 0);

  rect.start_row = e48->rows - 1;

  if(state->callbacks &&
     state->callbacks->erase)
    (*state->callbacks->erase)(e48, rect);
}

static void linefeed(ecma48_t *e48)
{
  ecma48_state_t *state = e48->state;

  if(state->pos.row == (e48->rows-1))
    scroll(e48);
  else
    state->pos.row++;
}

int ecma48_state_on_text(ecma48_t *e48, char *bytes, size_t len)
{
  // TODO: Need a Unicode engine here to convert bytes into Chars
  uint32_t *chars = g_alloca(len * sizeof(uint32_t));
  int i;
  for(i = 0; i < len; i++)
    chars[i] = bytes[i];

  ecma48_state_t *state = e48->state;

  ecma48_position_t oldpos = state->pos;

  for(i = 0; i < len; i++) {
    uint32_t c = chars[i];

    if(state->pos.col == e48->cols) {
      linefeed(e48);
      state->pos.col = 0;
    }

    int done = 0;

    if(state->callbacks &&
       state->callbacks->putchar)
      done = (*state->callbacks->putchar)(e48, c, state->pos);

    if(!done)
      fprintf(stderr, "libecma48: Unhandled putchar U+%04x at (%d,%d)\n",
          c, state->pos.col, state->pos.row);

    state->pos.col++;
  }

  if(state->callbacks &&
     state->callbacks->movecursor)
    (*state->callbacks->movecursor)(e48, state->pos, oldpos);

  return 1;
}

int ecma48_state_on_control(ecma48_t *e48, char control)
{
  ecma48_state_t *state = e48->state;

  ecma48_position_t oldpos = state->pos;

  switch(control) {
  case 0x0a: // CR
    state->pos.col = 0;
    if(state->callbacks &&
       state->callbacks->movecursor)
      (*state->callbacks->movecursor)(e48, state->pos, oldpos);
    break;

  case 0x0d: // LF
    linefeed(e48);
    break;

  default:
    return 0;
  }

  return 1;
}
