#include "ecma48_internal.h"

#include <stdio.h>
#include <string.h>

struct ecma48_state_s
{
  ecma48_state_callbacks_t *callbacks;

  /* Current cursor position */
  ecma48_position_t pos;

  int scrollregion_start;
  int scrollregion_end;

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

  state->scrollregion_start = 0;
  state->scrollregion_end = e48->rows;

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

static void scroll(ecma48_t *e48, ecma48_rectangle_t rect, int downward, int rightward)
{
  ecma48_state_t *state = e48->state;

  if(!downward && !rightward)
    return;

  int done_scroll = 0;

  if(state->callbacks &&
     state->callbacks->scroll)
    done_scroll = (*state->callbacks->scroll)(e48, rect, downward, rightward);

  if(!done_scroll &&
     state->callbacks &&
     state->callbacks->copycell) {
    // User code doesn't implement a real scroll; so instead we'll synthesize
    // one out of copycell
    int init_row, test_row, init_col, test_col;
    int inc_row, inc_col;

    if(downward < 0) {
      init_row = rect.end_row - 1;
      test_row = rect.start_row - downward;
      inc_row = -1;
    }
    else if(downward == 0) {
      init_row = rect.start_row;
      test_row = rect.end_row;
      inc_row = +1;
    }
    else /* downward > 0 */ {
      init_row = rect.start_row + downward;
      test_row = rect.end_row - 1;
      inc_row = +1;
    }

    if(rightward < 0) {
      init_col = rect.end_col - 1;
      test_col = rect.start_col - rightward;
      inc_col = -1;
    }
    else if(rightward == 0) {
      init_col = rect.start_col;
      test_col = rect.end_col;
      inc_col = +1;
    }
    else /* rightward > 0 */ {
      init_col = rect.start_col + rightward;
      test_col = rect.end_col - 1;
      inc_col = +1;
    }

    ecma48_position_t pos;
    for(pos.row = init_row; pos.row != test_row; pos.row += inc_row)
      for(pos.col = init_col; pos.col != test_col; pos.col += inc_col) {
        ecma48_position_t srcpos = { pos.row + downward, pos.col + rightward };
        (*state->callbacks->copycell)(e48, pos, srcpos);
      }

    done_scroll = 1;
  }

  if(downward > 0)
    rect.start_row = rect.end_row - downward;
  else if(downward < 0)
    rect.end_row = rect.start_row - downward;

  if(rightward > 0)
    rect.start_col = rect.end_col - rightward;
  else if(rightward < 0)
    rect.end_col = rect.start_col - rightward;

  if(state->callbacks &&
     state->callbacks->erase)
    (*state->callbacks->erase)(e48, rect, state->pen);
}

static void updatecursor(ecma48_t *e48, ecma48_state_t *state, ecma48_position_t *oldpos)
{
  if(state->pos.col != oldpos->col || state->pos.row != oldpos->row) {
    if(state->callbacks &&
      state->callbacks->movecursor)
      (*state->callbacks->movecursor)(e48, state->pos, *oldpos, e48->mode.cursor_visible);
  }
}

static void linefeed(ecma48_t *e48)
{
  ecma48_state_t *state = e48->state;

  if(state->pos.row == (state->scrollregion_end-1)) {
    ecma48_rectangle_t rect = {
      .start_row = state->scrollregion_start,
      .end_row   = state->scrollregion_end,
      .start_col = 0,
      .end_col   = e48->cols,
    };

    scroll(e48, rect, 1, 0);
  }
  else
    state->pos.row++;
}

int ecma48_state_on_text(ecma48_t *e48, int codepoints[], int npoints)
{
  ecma48_state_t *state = e48->state;

  ecma48_position_t oldpos = state->pos;

  int i;
  for(i = 0; i < npoints; i++) {
    int c = codepoints[i];

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

int ecma48_state_on_escape(ecma48_t *e48, char escape)
{
  switch(escape) {
  case 0x3d:
    e48->mode.keypad = 1;
    break;

  case 0x3e:
    e48->mode.keypad = 0;
    break;

  default:
    return 0;
  }

  return 1;
}

static void change_dec_mode(ecma48_t *e48, int mode, int set)
{
  ecma48_state_t *state = e48->state;

  switch(mode) {
  case 1:
    e48->mode.cursor = set;
    break;

  case 25:
    e48->mode.cursor_visible = set;
    if(state->callbacks && state->callbacks->movecursor)
      (*state->callbacks->movecursor)(e48, state->pos, state->pos, e48->mode.cursor_visible);
    break;

  default:
    printf("libecma48: Unknown DEC mode %d\n", mode);
    break;
  }
}

static int ecma48_state_on_csi_qmark(ecma48_t *e48, int *args, int argcount, char command)
{
  switch(command) {
  case 0x68: // DEC private mode set
    if(args[0] != -1)
      change_dec_mode(e48, args[0], 1);
    break;

  case 0x6c: // DEC private mode reset
    if(args[0] != -1)
      change_dec_mode(e48, args[0], 0);
    break;

  default:
    return 0;
  }

  return 1;
}

int ecma48_state_on_csi(ecma48_t *e48, char *intermed, int *args, int argcount, char command)
{
  if(intermed) {
    if(strcmp(intermed, "?") == 0)
      return ecma48_state_on_csi_qmark(e48, args, argcount, command);

    return 0;
  }

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
  case 0x40: // ICH - ECMA-48 8.3.64
    count = args[0] == -1 ? 1 : args[0];

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = e48->cols;

    scroll(e48, rect, 0, -count);

    break;

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

  case 0x50: // DCH - ECMA-48 8.3.26
    count = args[0] == -1 ? 1 : args[0];

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = e48->cols;

    scroll(e48, rect, 0, count);

    break;

  case 0x6d: // SGR - ECMA-48 8.3.117
    if(state->callbacks &&
       state->callbacks->setpen)
      for(argi = 0; argi < argcount; argi++) {
        if(!(*state->callbacks->setpen)(e48, args[argi], &state->pen))
          fprintf(stderr, "libecma48: Unhandled CSI SGR %d\n", args[argi]);
      }

    break;

  case 0x72: // DECSTBM - DEC custom
    state->scrollregion_start = args[0] == -1 ? 0 : args[0]-1;
    state->scrollregion_end = argcount < 2 || args[1] == -1 ? e48->rows : args[1];
    break;

  default:
    return 0;
  }

  updatecursor(e48, state, &oldpos);

  return 1;
}
