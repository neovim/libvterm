#include "ecma48_internal.h"

#include <stdio.h>

struct ecma48_state_s
{
  ecma48_state_callbacks_t *callbacks;

  /* Current cursor position */
  ecma48_position_t pos;

  /* Current pen - entirely managed by user code */
  void *pen;
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

void ecma48_state_initialise(ecma48_t *e48)
{
  ecma48_state_t *state = e48->state;

  if(!state)
    return;

  state->pos.row = 0;
  state->pos.col = 0;

  state->pen = NULL;

  if(state->callbacks &&
     state->callbacks->setpen)
    (*state->callbacks->setpen)(e48, 0, &state->pen);

  if(state->callbacks &&
     state->callbacks->erase) {
    ecma48_rectangle_t rect = { 0, e48->rows, 0, e48->cols };
    (*state->callbacks->erase)(e48, rect, state->pen);
  }
}

void ecma48_state_get_cursorpos(ecma48_t *e48, ecma48_position_t *cursorpos)
{
  ecma48_state_t *state = e48->state;

  if(!state) {
    cursorpos->col = -1;
    cursorpos->row = -1;
  }
  else {
    *cursorpos = state->pos;
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

  int done_scroll = 0;

  if(state->callbacks &&
     state->callbacks->scroll)
    done_scroll = (*state->callbacks->scroll)(e48, rect, 1, 0);

  if(!done_scroll &&
     state->callbacks &&
     state->callbacks->copycell) {
    // User code doesn't implement a real scroll; so instead we'll synthesize
    // one out of copycell
    int init_row, test_row, init_col, test_col;
    int inc_row, inc_col;

    init_row = rect.start_row + 1;
    test_row = rect.end_row - 1;
    inc_row = +1;

    init_col = rect.start_col;
    test_col = rect.end_col;
    inc_col = +1;

    ecma48_position_t pos;
    for(pos.row = init_row; pos.row != test_row; pos.row += inc_row)
      for(pos.col = init_col; pos.col != test_col; pos.col += inc_col) {
        ecma48_position_t srcpos = { pos.row + 1, pos.col };
        (*state->callbacks->copycell)(e48, pos, srcpos);
      }

    done_scroll = 1;
  }

  rect.start_row = e48->rows - 1;

  if(state->callbacks &&
     state->callbacks->erase)
    (*state->callbacks->erase)(e48, rect, state->pen);
}

static void updatecursor(ecma48_t *e48, ecma48_state_t *state, ecma48_position_t *oldpos)
{
  if(state->pos.col != oldpos->col || state->pos.row != oldpos->row) {
    if(state->callbacks &&
      state->callbacks->movecursor)
      (*state->callbacks->movecursor)(e48, state->pos, *oldpos);
  }
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
      done = (*state->callbacks->putchar)(e48, c, state->pos, state->pen);

    if(!done)
      fprintf(stderr, "libecma48: Unhandled putchar U+%04x at (%d,%d)\n",
          c, state->pos.col, state->pos.row);

    state->pos.col++;
  }

  updatecursor(e48, state, &oldpos);

  return 1;
}

int ecma48_state_on_control(ecma48_t *e48, char control)
{
  ecma48_state_t *state = e48->state;

  ecma48_position_t oldpos = state->pos;

  switch(control) {
  case 0x08: // BS - ECMA-48 8.3.5
    if(state->pos.col > 0)
      state->pos.col--;
    break;

  case 0x09: // HT - ECMA-48 8.3.60
    // TODO: Implement variable tabstops
    if(state->pos.col == e48->cols - 1)
      break;
    do {
      state->pos.col++;
    } while(state->pos.col % 8 && state->pos.col < (e48->cols-1));
    break;

  case 0x0a: // LF - ECMA-48 8.3.74
    linefeed(e48);
    break;

  case 0x0d: // CR - ECMA-48 8.3.15
    state->pos.col = 0;
    break;

  default:
    return 0;
  }

  updatecursor(e48, state, &oldpos);

  return 1;
}

int ecma48_state_on_csi(ecma48_t *e48, int *args, int argcount, char command)
{
  ecma48_state_t *state = e48->state;

  ecma48_position_t oldpos = state->pos;

#define LBOUND(v,min) if((v) < (min)) (v) = (min)
#define UBOUND(v,max) if((v) > (max)) (v) = (max)

  // Some temporaries for later code
  int count;
  int row, col;
  ecma48_rectangle_t rect;
  int argi;

  switch(command) {
  case 0x41: // CUU - ECMA-48 8.3.22
    count = args[0] == -1 ? 1 : args[0];
    state->pos.row -= count;
    LBOUND(state->pos.row, 0);
    break;

  case 0x42: // CUD - ECMA-48 8.3.19
    count = args[0] == -1 ? 1 : args[0];
    state->pos.row += count;
    UBOUND(state->pos.row, e48->rows-1);
    break;

  case 0x43: // CUF - ECMA-48 8.3.20
    count = args[0] == -1 ? 1 : args[0];
    state->pos.col += count;
    UBOUND(state->pos.col, e48->cols-1);
    break;

  case 0x44: // CUB - ECMA-48 8.3.18
    count = args[0] == -1 ? 1 : args[0];
    state->pos.col -= count;
    LBOUND(state->pos.col, 0);
    break;

  case 0x48: // CUP - ECMA-48 8.3.21
    row = args[0] == -1                 ? 1 : args[0];
    col = argcount < 2 || args[1] == -1 ? 1 : args[1];
    // zero-based
    state->pos.row = row-1;
    UBOUND(state->pos.row, e48->rows-1);
    state->pos.col = col-1;
    UBOUND(state->pos.col, e48->cols-1);
    break;

  case 0x4a: // ED - ECMA-48 8.3.39
    if(!state->callbacks || !state->callbacks->erase)
      return 1;

    switch(args[0]) {
    case -1:
    case 0:
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
      rect.start_col = state->pos.col; rect.end_col = e48->cols;
      (*state->callbacks->erase)(e48, rect, state->pen);
      rect.start_row = state->pos.row + 1; rect.end_row = e48->rows;
      rect.start_col = 0;
      (*state->callbacks->erase)(e48, rect, state->pen);
      break;

    case 1:
      rect.start_row = 0; rect.end_row = state->pos.row;
      rect.start_col = 0; rect.end_col = e48->cols;
      (*state->callbacks->erase)(e48, rect, state->pen);
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
                          rect.end_col = state->pos.col + 1;
      (*state->callbacks->erase)(e48, rect, state->pen);
      break;

    case 2:
      rect.start_row = 0; rect.end_row = e48->rows;
      rect.start_col = 0; rect.end_col = e48->cols;
      (*state->callbacks->erase)(e48, rect, state->pen);
      break;
    }

  case 0x4b: // EL - ECMA-48 8.3.41
    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;

    switch(args[0]) {
    case -1:
    case 0:
      rect.start_col = state->pos.col; rect.end_col = e48->cols; break;
    case 1:
      rect.start_col = 0; rect.end_col = state->pos.col + 1; break;
    case 2:
      rect.start_col = 0; rect.end_col = e48->cols; break;
    default:
      return 0;
    }

    if(state->callbacks && state->callbacks->erase)
      (*state->callbacks->erase)(e48, rect, state->pen);

    break;

  case 0x6d: // SGR - ECMA-48 8.3.117
    if(state->callbacks &&
       state->callbacks->setpen)
      for(argi = 0; argi < argcount; argi++) {
        if(!(*state->callbacks->setpen)(e48, args[argi], &state->pen))
          fprintf(stderr, "libecma48: Unhandled CSI SGR %d\n", args[argi]);
      }

    break;

  default:
    return 0;
  }

  updatecursor(e48, state, &oldpos);

  return 1;
}
