#include "vterm_internal.h"

#include <stdio.h>

void vterm_state_initmodes(vterm_t *vt)
{
  vterm_mode mode;
  for(mode = VTERM_MODE_NONE; mode < VTERM_MODE_MAX; mode++) {
    int val = 0;

    switch(mode) {
    case VTERM_MODE_DEC_AUTOWRAP:
    case VTERM_MODE_DEC_CURSORBLINK:
    case VTERM_MODE_DEC_CURSORVISIBLE:
      val = 1;
      break;

    default:
      break;
    }

    vterm_state_setmode(vt, mode, val);
  }
}

static void mousefunc(int x, int y, int button, int pressed, void *data)
{
  vterm_t *vt = data;
  vterm_state_t *state = vt->state;

  int old_buttons = state->mouse_buttons;

  if(pressed)
    state->mouse_buttons |= (1 << button);
  else
    state->mouse_buttons &= ~(1 << button);

  if(state->mouse_buttons != old_buttons) {
    if(button < 4) {
      vterm_push_output_sprintf(vt, "\e[M%c%c%c", pressed ? button + 31 : 35, x + 33, y + 33);
    }
  }
}

void vterm_state_setmode(vterm_t *vt, vterm_mode mode, int val)
{
  vterm_state_t *state = vt->state;

  int done = 0;
  if(state->callbacks && state->callbacks->setmode)
    done = (*state->callbacks->setmode)(vt, mode, val);

  switch(mode) {
  case VTERM_MODE_NONE:
  case VTERM_MODE_MAX:
    break;

  case VTERM_MODE_KEYPAD:
    vt->mode.keypad = val;
    break;

  case VTERM_MODE_DEC_CURSOR:
    vt->mode.cursor = val;
    break;

  case VTERM_MODE_DEC_AUTOWRAP:
    vt->mode.autowrap = val;
    break;

  case VTERM_MODE_DEC_CURSORBLINK:
    vt->mode.cursor_blink = val;
    break;

  case VTERM_MODE_DEC_CURSORVISIBLE:
    vt->mode.cursor_visible = val;
    break;

  case VTERM_MODE_DEC_MOUSE:
    if(state->callbacks && state->callbacks->setmousefunc) {
      if(val) {
        state->mouse_buttons = 0;
      }
      (*state->callbacks->setmousefunc)(vt, val ? mousefunc : NULL, vt);
    }
    break;

  case VTERM_MODE_DEC_ALTSCREEN:
    /* Only store that we're on the alternate screen if the usercode said it
     * switched */
    if(done)
      vt->mode.alt_screen = val;
    if(done && val) {
      if(state->callbacks && state->callbacks->erase) {
        vterm_rectangle_t rect = {
          .start_row = 0,
          .start_col = 0,
          .end_row = vt->rows,
          .end_col = vt->cols,
        };
        (*state->callbacks->erase)(vt, rect, state->pen);
      }
    }
    break;

  case VTERM_MODE_DEC_SAVECURSOR:
    vt->mode.saved_cursor = val;
    if(val) {
      state->saved_pos = state->pos;
    }
    else {
      vterm_position_t oldpos = state->pos;
      state->pos = state->saved_pos;
      if(state->callbacks && state->callbacks->movecursor)
        (*state->callbacks->movecursor)(vt, state->pos, oldpos, vt->mode.cursor_visible);
    }
    break;

  }
}
