#include "vterm_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef DEBUG
# define DEBUG_GLYPH_COMBINE
#endif

static vterm_state_t *vterm_state_new(void)
{
  vterm_state_t *state = g_new0(vterm_state_t, 1);

  state->pos.row = 0;
  state->pos.col = 0;

  state->combine_chars_size = 16;
  state->combine_chars = g_new0(uint32_t, state->combine_chars_size);

  return state;
}

void vterm_state_free(vterm_state_t *state)
{
  g_free(state);
}

void vterm_set_state_callbacks(vterm_t *vt, const vterm_state_callbacks_t *callbacks)
{
  if(callbacks) {
    if(!vt->state) {
      vt->state = vterm_state_new();
    }
    vt->state->callbacks = callbacks;

    // Initialise the modes
    vterm_state_initmodes(vt);
  }
  else {
    if(vt->state) {
      vterm_state_free(vt->state);
      vt->state = NULL;
    }
  }
}

void vterm_state_initialise(vterm_t *vt)
{
  vterm_state_t *state = vt->state;

  if(!state)
    return;

  state->pos.row = 0;
  state->pos.col = 0;

  state->scrollregion_start = 0;
  state->scrollregion_end = vt->rows;

  state->pen = NULL;

  if(state->callbacks &&
     state->callbacks->setpen)
    (*state->callbacks->setpen)(vt, 0, &state->pen);

  if(state->callbacks &&
     state->callbacks->erase) {
    vterm_rectangle_t rect = { 0, vt->rows, 0, vt->cols };
    (*state->callbacks->erase)(vt, rect, state->pen);
  }
}

void vterm_state_get_cursorpos(vterm_t *vt, vterm_position_t *cursorpos)
{
  vterm_state_t *state = vt->state;

  if(!state) {
    cursorpos->col = -1;
    cursorpos->row = -1;
  }
  else {
    *cursorpos = state->pos;
  }
}

static void scroll(vterm_t *vt, vterm_rectangle_t rect, int downward, int rightward)
{
  vterm_state_t *state = vt->state;

  if(!downward && !rightward)
    return;

  int done_scroll = 0;

  if(state->callbacks &&
     state->callbacks->scroll)
    done_scroll = (*state->callbacks->scroll)(vt, rect, downward, rightward);

  if(!done_scroll &&
     state->callbacks &&
     state->callbacks->copycell) {
    // User code doesn't implement a real scroll; so instead we'll synthesize
    // one out of copycell
    int init_row, test_row, init_col, test_col;
    int inc_row, inc_col;

    if(downward < 0) {
      init_row = rect.end_row - 1;
      test_row = rect.start_row - downward - 1;
      inc_row = -1;
    }
    else if(downward == 0) {
      init_row = rect.start_row;
      test_row = rect.end_row;
      inc_row = +1;
    }
    else /* downward > 0 */ {
      init_row = rect.start_row;
      test_row = rect.end_row - downward;
      inc_row = +1;
    }

    if(rightward < 0) {
      init_col = rect.end_col - 1;
      test_col = rect.start_col - rightward - 1;
      inc_col = -1;
    }
    else if(rightward == 0) {
      init_col = rect.start_col;
      test_col = rect.end_col;
      inc_col = +1;
    }
    else /* rightward > 0 */ {
      init_col = rect.start_col;
      test_col = rect.end_col - rightward;
      inc_col = +1;
    }

    vterm_position_t pos;
    for(pos.row = init_row; pos.row != test_row; pos.row += inc_row)
      for(pos.col = init_col; pos.col != test_col; pos.col += inc_col) {
        vterm_position_t srcpos = { pos.row + downward, pos.col + rightward };
        (*state->callbacks->copycell)(vt, pos, srcpos);
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
    (*state->callbacks->erase)(vt, rect, state->pen);
}

static void updatecursor(vterm_t *vt, const vterm_state_t *state, vterm_position_t *oldpos)
{
  if(state->pos.col != oldpos->col || state->pos.row != oldpos->row) {
    if(state->callbacks &&
      state->callbacks->movecursor)
      (*state->callbacks->movecursor)(vt, state->pos, *oldpos, vt->mode.cursor_visible);
  }
}

static void linefeed(vterm_t *vt)
{
  vterm_state_t *state = vt->state;

  if(state->pos.row == (state->scrollregion_end-1)) {
    vterm_rectangle_t rect = {
      .start_row = state->scrollregion_start,
      .end_row   = state->scrollregion_end,
      .start_col = 0,
      .end_col   = vt->cols,
    };

    scroll(vt, rect, 1, 0);
  }
  else
    state->pos.row++;
}

static void grow_combine_buffer(vterm_state_t *state)
{
  state->combine_chars_size *= 2;
  state->combine_chars = g_realloc(state->combine_chars, state->combine_chars_size * sizeof(state->combine_chars[0]));
}

int vterm_state_on_text(vterm_t *vt, const int codepoints[], int npoints)
{
  vterm_state_t *state = vt->state;

  vterm_position_t oldpos = state->pos;

  int i = 0;

  /* This is a combining char. that needs to be merged with the previous
   * glyph output */
  if(vterm_unicode_is_combining(codepoints[i])) {
    /* See if the cursor has moved since */
    if(state->pos.row == state->combine_pos.row && state->pos.col == state->combine_pos.col + state->combine_width) {
#ifdef DEBUG_GLYPH_COMBINE
    int printpos;
    printf("DEBUG: COMBINING SPLIT GLYPH of chars {");
    for(printpos = 0; state->combine_chars[printpos]; printpos++)
      printf("U+%04x ", state->combine_chars[printpos]);
    printf("} + {");
#endif

      /* Find where we need to append these combining chars */
      int saved_i = 0;
      while(state->combine_chars[saved_i])
        saved_i++;

      /* Add extra ones */
      while(i < npoints && vterm_unicode_is_combining(codepoints[i])) {
        if(saved_i >= state->combine_chars_size)
          grow_combine_buffer(state);
        state->combine_chars[saved_i++] = codepoints[i++];
      }
      if(saved_i >= state->combine_chars_size)
        grow_combine_buffer(state);
      state->combine_chars[saved_i] = 0;

#ifdef DEBUG_GLYPH_COMBINE
      for(; state->combine_chars[printpos]; printpos++)
        printf("U+%04x ", state->combine_chars[printpos]);
      printf("}\n");
#endif

      /* Now render it */
      int done = 0;

      if(state->callbacks && state->callbacks->putglyph) {
        done = (*state->callbacks->putglyph)(vt, state->combine_chars, state->combine_width, state->combine_pos, state->pen);
      }

      if(!done && state->callbacks && state->callbacks->putchar) {
        done = (*state->callbacks->putchar)(vt, state->combine_chars[0], state->combine_width, state->combine_pos, state->pen);
        // TODO: We have lost information here; namely, the combining chars.
        // Anything we can do about this?
      }

      if(!done)
        fprintf(stderr, "libvterm: Unhandled putglyph U+%04x at (%d,%d)\n",
            state->combine_chars[0], state->pos.col, state->pos.row);
    }
    else {
      fprintf(stderr, "libvterm: TODO: Skip over split char+combining\n");
    }
  }

  for(; i < npoints; i++) {
    if(state->pos.col >= vt->cols) {
      linefeed(vt);
      state->pos.col = 0;
    }

    // Try to find combining characters following this
    int glyph_starts = i;
    int glyph_ends;
    for(glyph_ends = i + 1; glyph_ends < npoints; glyph_ends++)
      if(!vterm_unicode_is_combining(codepoints[glyph_ends]))
        break;

    int width = 0;

    uint32_t chars[glyph_ends - glyph_starts + 1];

    for( ; i < glyph_ends; i++) {
      chars[i - glyph_starts] = codepoints[i];
      width += vterm_unicode_width(codepoints[i]);
    }

    chars[glyph_ends - glyph_starts] = 0;
    i--;

#ifdef DEBUG_GLYPH_COMBINE
    int printpos;
    printf("DEBUG: COMBINED GLYPH of %d chars {", glyph_ends - glyph_starts);
    for(printpos = 0; printpos < glyph_ends - glyph_starts; printpos++)
      printf("U+%04x ", chars[printpos]);
    printf("}, onscreen width %d\n", width);
#endif

    int done = 0;

    if(state->callbacks && state->callbacks->putglyph) {
      done = (*state->callbacks->putglyph)(vt, chars, width, state->pos, state->pen);
    }

    if(!done && state->callbacks && state->callbacks->putchar) {
      done = (*state->callbacks->putchar)(vt, chars[0], width, state->pos, state->pen);
      // TODO: We have lost information here; namely, the combining chars.
      // Anything we can do about this?
    }

    if(!done)
      fprintf(stderr, "libvterm: Unhandled putglyph U+%04x at (%d,%d)\n",
          chars[0], state->pos.col, state->pos.row);

    if(i == npoints - 1) {
      /* End of the buffer. Save the chars in case we have to combine with
       * more on the next call */
      int save_i;
      for(save_i = 0; chars[save_i]; save_i++) {
        if(save_i >= state->combine_chars_size)
          grow_combine_buffer(state);
        state->combine_chars[save_i] = chars[save_i];
      }
      if(save_i >= state->combine_chars_size)
        grow_combine_buffer(state);
      state->combine_chars[save_i] = 0;
      state->combine_width = width;
      state->combine_pos = state->pos;
    }

    state->pos.col += width;
  }

  updatecursor(vt, state, &oldpos);

  return 1;
}

int vterm_state_on_control(vterm_t *vt, unsigned char control)
{
  vterm_state_t *state = vt->state;

  vterm_position_t oldpos = state->pos;

  switch(control) {
  case 0x07: // BEL - ECMA-48 8.3.3
    if(state->callbacks &&
       state->callbacks->bell)
      (*state->callbacks->bell)(vt);
    break;

  case 0x08: // BS - ECMA-48 8.3.5
    if(state->pos.col > 0)
      state->pos.col--;
    break;

  case 0x09: // HT - ECMA-48 8.3.60
    // TODO: Implement variable tabstops
    if(state->pos.col == vt->cols - 1)
      break;
    do {
      state->pos.col++;
    } while(state->pos.col % 8 && state->pos.col < (vt->cols-1));
    break;

  case 0x0a: // LF - ECMA-48 8.3.74
    linefeed(vt);
    break;

  case 0x0d: // CR - ECMA-48 8.3.15
    state->pos.col = 0;
    break;

  case 0x8d: // RI - ECMA-48 8.3.104
    if(state->pos.row == state->scrollregion_start) {
      vterm_rectangle_t rect = {
        .start_row = state->scrollregion_start,
        .end_row   = state->scrollregion_end,
        .start_col = 0,
        .end_col   = vt->cols,
      };

      scroll(vt, rect, -1, 0);
    }
    else
      if(state->pos.row)
        state->pos.row--;
    break;

  default:
    return 0;
  }

  updatecursor(vt, state, &oldpos);

  return 1;
}

int vterm_state_on_escape(vterm_t *vt, char escape)
{
  switch(escape) {
  case 0x3d:
    vterm_state_setmode(vt, VTERM_MODE_KEYPAD, 1);
    break;

  case 0x3e:
    vterm_state_setmode(vt, VTERM_MODE_KEYPAD, 0);
    break;

  default:
    return 0;
  }

  return 1;
}

static void set_dec_mode(vterm_t *vt, int num, int val)
{
  switch(num) {
  case 1:
    vterm_state_setmode(vt, VTERM_MODE_DEC_CURSOR, val);
    break;

  case 12:
    vterm_state_setmode(vt, VTERM_MODE_DEC_CURSORBLINK, val);
    break;

  case 25:
    vterm_state_setmode(vt, VTERM_MODE_DEC_CURSORVISIBLE, val);
    break;

  case 1000:
    vterm_state_setmode(vt, VTERM_MODE_DEC_MOUSE, val);
    break;

  case 1047:
    vterm_state_setmode(vt, VTERM_MODE_DEC_ALTSCREEN, val);
    break;

  case 1048:
    vterm_state_setmode(vt, VTERM_MODE_DEC_SAVECURSOR, val);
    break;

  case 1049:
    vterm_state_setmode(vt, VTERM_MODE_DEC_ALTSCREEN, val);
    vterm_state_setmode(vt, VTERM_MODE_DEC_SAVECURSOR, val);
    break;

  default:
    printf("libvterm: Unknown DEC mode %d\n", num);
    return;
  }
}

static int vterm_state_on_csi_qmark(vterm_t *vt, const int *args, int argcount, char command)
{
  switch(command) {
  case 0x68: // DEC private mode set
    if(args[0] != -1)
      set_dec_mode(vt, args[0], 1);
    break;

  case 0x6c: // DEC private mode reset
    if(args[0] != -1)
      set_dec_mode(vt, args[0], 0);
    break;

  default:
    return 0;
  }

  return 1;
}

int vterm_state_on_csi(vterm_t *vt, const char *intermed, const int args[], int argcount, char command)
{
  if(intermed) {
    if(strcmp(intermed, "?") == 0)
      return vterm_state_on_csi_qmark(vt, args, argcount, command);

    return 0;
  }

  vterm_state_t *state = vt->state;

  vterm_position_t oldpos = state->pos;

#define LBOUND(v,min) if((v) < (min)) (v) = (min)
#define UBOUND(v,max) if((v) > (max)) (v) = (max)

  // Some temporaries for later code
  int count;
  int row, col;
  vterm_rectangle_t rect;

  switch(command) {
  case 0x40: // ICH - ECMA-48 8.3.64
    count = args[0] == -1 ? 1 : args[0];

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = vt->cols;

    scroll(vt, rect, 0, -count);

    break;

  case 0x41: // CUU - ECMA-48 8.3.22
    count = args[0] == -1 ? 1 : args[0];
    state->pos.row -= count;
    LBOUND(state->pos.row, 0);
    break;

  case 0x42: // CUD - ECMA-48 8.3.19
    count = args[0] == -1 ? 1 : args[0];
    state->pos.row += count;
    UBOUND(state->pos.row, vt->rows-1);
    break;

  case 0x43: // CUF - ECMA-48 8.3.20
    count = args[0] == -1 ? 1 : args[0];
    state->pos.col += count;
    UBOUND(state->pos.col, vt->cols-1);
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
    UBOUND(state->pos.row, vt->rows-1);
    state->pos.col = col-1;
    UBOUND(state->pos.col, vt->cols-1);
    break;

  case 0x4a: // ED - ECMA-48 8.3.39
    if(!state->callbacks || !state->callbacks->erase)
      return 1;

    switch(args[0]) {
    case -1:
    case 0:
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
      rect.start_col = state->pos.col; rect.end_col = vt->cols;
      (*state->callbacks->erase)(vt, rect, state->pen);
      rect.start_row = state->pos.row + 1; rect.end_row = vt->rows;
      rect.start_col = 0;
      (*state->callbacks->erase)(vt, rect, state->pen);
      break;

    case 1:
      rect.start_row = 0; rect.end_row = state->pos.row;
      rect.start_col = 0; rect.end_col = vt->cols;
      (*state->callbacks->erase)(vt, rect, state->pen);
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
                          rect.end_col = state->pos.col + 1;
      (*state->callbacks->erase)(vt, rect, state->pen);
      break;

    case 2:
      rect.start_row = 0; rect.end_row = vt->rows;
      rect.start_col = 0; rect.end_col = vt->cols;
      (*state->callbacks->erase)(vt, rect, state->pen);
      break;
    }

  case 0x4b: // EL - ECMA-48 8.3.41
    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;

    switch(args[0]) {
    case -1:
    case 0:
      rect.start_col = state->pos.col; rect.end_col = vt->cols; break;
    case 1:
      rect.start_col = 0; rect.end_col = state->pos.col + 1; break;
    case 2:
      rect.start_col = 0; rect.end_col = vt->cols; break;
    default:
      return 0;
    }

    if(state->callbacks && state->callbacks->erase)
      (*state->callbacks->erase)(vt, rect, state->pen);

    break;

  case 0x4c: // IL - ECMA-48 8.3.67
    count = args[0] == -1 ? 1 : args[0];

    rect.start_row = state->pos.row;
    rect.end_row   = state->scrollregion_end;
    rect.start_col = 0;
    rect.end_col   = vt->cols;

    scroll(vt, rect, -count, 0);

    break;

  case 0x4d: // DL - ECMA-48 8.3.32
    count = args[0] == -1 ? 1 : args[0];

    rect.start_row = state->pos.row;
    rect.end_row   = state->scrollregion_end;
    rect.start_col = 0;
    rect.end_col   = vt->cols;

    scroll(vt, rect, count, 0);

    break;

  case 0x50: // DCH - ECMA-48 8.3.26
    count = args[0] == -1 ? 1 : args[0];

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = vt->cols;

    scroll(vt, rect, 0, count);

    break;

  case 0x6d: // SGR - ECMA-48 8.3.117
    vterm_state_setpen(vt, args, argcount);
    break;

  case 0x72: // DECSTBM - DEC custom
    state->scrollregion_start = args[0] == -1 ? 0 : args[0]-1;
    state->scrollregion_end = argcount < 2 || args[1] == -1 ? vt->rows : args[1];
    break;

  default:
    return 0;
  }

  updatecursor(vt, state, &oldpos);

  return 1;
}
