#include "vterm_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef DEBUG
# define DEBUG_GLYPH_COMBINE
#endif

/* Some convenient wrappers to make callback functions easier */

static void putglyph(VTerm *vt, const uint32_t chars[], int width, VTermPos pos)
{
  VTermState *state = vt->state;

  for(int cb = 0; cb < 2; cb++)
    if(state->callbacks[cb] && state->callbacks[cb]->putglyph)
      if((*state->callbacks[cb]->putglyph)(vt, chars, width, pos, state->pen))
        return;

  fprintf(stderr, "libvterm: Unhandled putglyph U+%04x at (%d,%d)\n", chars[0], pos.col, pos.row);
}

static void updatecursor(VTerm *vt, VTermState *state, VTermPos *oldpos)
{
  if(state->pos.col == oldpos->col && state->pos.row == oldpos->row)
    return;

  for(int cb = 0; cb < 2; cb++)
    if(state->callbacks[cb] && state->callbacks[cb]->movecursor)
      if((*state->callbacks[cb]->movecursor)(vt, state->pos, *oldpos, vt->mode.cursor_visible))
        return;
}

static void erase(VTerm *vt, VTermRect rect)
{
  VTermState *state = vt->state;

  for(int cb = 0; cb < 2; cb++)
    if(state->callbacks[cb] && state->callbacks[cb]->erase)
      if((*state->callbacks[cb]->erase)(vt, rect, state->pen))
        return;
}

static VTermState *vterm_state_new(void)
{
  VTermState *state = g_new0(VTermState, 1);

  state->pos.row = 0;
  state->pos.col = 0;

  state->combine_chars_size = 16;
  state->combine_chars = g_new0(uint32_t, state->combine_chars_size);

  return state;
}

static void vterm_state_free(VTermState *state)
{
  g_free(state);
}

void vterm_state_initialise(VTerm *vt)
{
  VTermState *state = vt->state;

  if(!state)
    return;

  state->pos.row = 0;
  state->pos.col = 0;

  state->scrollregion_start = 0;
  state->scrollregion_end = vt->rows;

  state->pen = NULL;

  for(int cb = 0; cb < 2; cb++)
    if(state->callbacks[cb] && state->callbacks[cb]->initpen)
      (*state->callbacks[cb]->initpen)(vt, &state->pen);

  VTermRect rect = { 0, vt->rows, 0, vt->cols };
  erase(vt, rect);
}

void vterm_state_get_cursorpos(VTerm *vt, VTermPos *cursorpos)
{
  VTermState *state = vt->state;

  if(!state) {
    cursorpos->col = -1;
    cursorpos->row = -1;
  }
  else {
    *cursorpos = state->pos;
  }
}

static void scroll(VTerm *vt, VTermRect rect, int downward, int rightward)
{
  VTermState *state = vt->state;

  if(!downward && !rightward)
    return;

  int done = 0;

  VTermRect src;
  VTermRect dest;

  if(rightward >= 0) {
    /* rect: [XXX................]
     * src:     [----------------]
     * dest: [----------------]
     */
    dest.start_col = rect.start_col;
    dest.end_col   = rect.end_col   - rightward;
    src.start_col  = rect.start_col + rightward;
    src.end_col    = rect.end_col;
  }
  else {
    /* rect: [................XXX]
     * src:  [----------------]
     * dest:    [----------------]
     */
    int leftward = -rightward;
    dest.start_col = rect.start_col + leftward;
    dest.end_col   = rect.end_col;
    src.start_col  = rect.start_col;
    src.end_col    = rect.end_col - leftward;
  }

  if(downward >= 0) {
    dest.start_row = rect.start_row;
    dest.end_row   = rect.end_row   - downward;
    src.start_row  = rect.start_row + downward;
    src.end_row    = rect.end_row;
  }
  else {
    int upward = -downward;
    dest.start_row = rect.start_row + upward;
    dest.end_row   = rect.end_row;
    src.start_row  = rect.start_row;
    src.end_row    = rect.end_row - upward;
  }

  for(int cb = 0; cb < 2; cb++)
    if(state->callbacks[cb] && state->callbacks[cb]->copyrect)
      if((*state->callbacks[cb]->copyrect)(vt, dest, src)) {
        done = 1;
        break;
      }

  if(!done) {
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

    VTermPos pos;
    for(pos.row = init_row; pos.row != test_row; pos.row += inc_row)
      for(pos.col = init_col; pos.col != test_col; pos.col += inc_col) {
        VTermPos srcpos = { pos.row + downward, pos.col + rightward };
        for(int cb = 0; cb < 2; cb++)
          if(state->callbacks[cb] && state->callbacks[cb]->copycell)
            if((*state->callbacks[cb]->copycell)(vt, pos, srcpos))
              break;
      }

    done = 1;
  }

  if(downward > 0)
    rect.start_row = rect.end_row - downward;
  else if(downward < 0)
    rect.end_row = rect.start_row - downward;

  if(rightward > 0)
    rect.start_col = rect.end_col - rightward;
  else if(rightward < 0)
    rect.end_col = rect.start_col - rightward;

  erase(vt, rect);
}

static void linefeed(VTerm *vt)
{
  VTermState *state = vt->state;

  if(state->pos.row == (state->scrollregion_end-1)) {
    VTermRect rect = {
      .start_row = state->scrollregion_start,
      .end_row   = state->scrollregion_end,
      .start_col = 0,
      .end_col   = vt->cols,
    };

    scroll(vt, rect, 1, 0);
  }
  else if(state->pos.row < vt->rows-1)
    state->pos.row++;
}

static void grow_combine_buffer(VTermState *state)
{
  state->combine_chars_size *= 2;
  state->combine_chars = g_realloc(state->combine_chars, state->combine_chars_size * sizeof(state->combine_chars[0]));
}

static int is_col_tabstop(VTermState *state, int col)
{
  // TODO: Implement variable tabstops
  return (col % 8) == 0;
}

static void tab(VTerm *vt, int count, int direction)
{
  VTermState *state = vt->state;

  while(count--)
    while(state->pos.col >= 0 && state->pos.col <= vt->cols-1) {
      state->pos.col += direction;

      if(is_col_tabstop(state, state->pos.col))
        break;
    }
}

static int on_text(VTerm *vt, const int codepoints[], int npoints)
{
  VTermState *state = vt->state;

  VTermPos oldpos = state->pos;

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
      putglyph(vt, state->combine_chars, state->combine_width, state->combine_pos);
    }
    else {
      fprintf(stderr, "libvterm: TODO: Skip over split char+combining\n");
    }
  }

  for(; i < npoints; i++) {
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

    putglyph(vt, chars, width, state->pos);

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

    if(state->pos.col >= vt->cols) {
      if(vt->mode.autowrap) {
        linefeed(vt);
        state->pos.col = 0;
      }
      else {
        state->pos.col = vt->cols - 1;
      }
    }
  }

  updatecursor(vt, state, &oldpos);

  return 1;
}

static int on_control(VTerm *vt, unsigned char control)
{
  VTermState *state = vt->state;

  VTermPos oldpos = state->pos;

  switch(control) {
  case 0x07: // BEL - ECMA-48 8.3.3
    for(int cb = 0; cb < 2; cb++)
      if(state->callbacks[cb] && state->callbacks[cb]->bell)
        if((*state->callbacks[cb]->bell)(vt))
          break;
    break;

  case 0x08: // BS - ECMA-48 8.3.5
    if(state->pos.col > 0)
      state->pos.col--;
    break;

  case 0x09: // HT - ECMA-48 8.3.60
    tab(vt, 1, +1);
    break;

  case 0x0a: // LF - ECMA-48 8.3.74
    linefeed(vt);
    break;

  case 0x0d: // CR - ECMA-48 8.3.15
    state->pos.col = 0;
    break;

  case 0x84: // IND - DEPRECATED but implemented for completeness
    linefeed(vt);
    break;

  case 0x85: // NEL - ECMA-48 8.3.86
    linefeed(vt);
    state->pos.col = 0;
    break;

  case 0x8d: // RI - ECMA-48 8.3.104
    if(state->pos.row == state->scrollregion_start) {
      VTermRect rect = {
        .start_row = state->scrollregion_start,
        .end_row   = state->scrollregion_end,
        .start_col = 0,
        .end_col   = vt->cols,
      };

      scroll(vt, rect, -1, 0);
    }
    else if(state->pos.row > 0)
        state->pos.row--;
    break;

  default:
    return 0;
  }

  updatecursor(vt, state, &oldpos);

  return 1;
}

static void mousefunc(int x, int y, int button, int pressed, void *data)
{
  VTerm *vt = data;
  VTermState *state = vt->state;

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

static void savecursor(VTerm *vt, int save)
{
  VTermState *state = vt->state;

  vt->mode.saved_cursor = save;
  if(save) {
    state->saved_pos = state->pos;
  }
  else {
    VTermPos oldpos = state->pos;
    state->pos = state->saved_pos;
    updatecursor(vt, state, &oldpos);
  }
}

static void setmode(VTerm *vt, VTermMode mode, int val)
{
  VTermState *state = vt->state;

  int done = 0;
  for(int cb = 0; cb < 2; cb++)
    if(state->callbacks[cb] && state->callbacks[cb]->setmode)
      if((*state->callbacks[cb]->setmode)(vt, mode, val)) {
        done = 1;
        break;
      }

  switch(mode) {
  case VTERM_MODE_NONE:
  case VTERM_MODE_MAX:
    break;

  case VTERM_MODE_DEC_CURSORBLINK:
    vt->mode.cursor_blink = val;
    break;

  case VTERM_MODE_DEC_CURSORVISIBLE:
    vt->mode.cursor_visible = val;
    break;

  case VTERM_MODE_DEC_ALTSCREEN:
    /* Only store that we're on the alternate screen if the usercode said it
     * switched */
    if(done)
      vt->mode.alt_screen = val;
    if(done && val) {
      VTermRect rect = {
        .start_row = 0,
        .start_col = 0,
        .end_row = vt->rows,
        .end_col = vt->cols,
      };
      erase(vt, rect);
    }
    break;
  }
}

static int on_escape(VTerm *vt, const char *bytes, size_t len)
{
  switch(bytes[0]) {
  case 0x28: case 0x29: case 0x2a: case 0x2b:
    if(len < 2)
      return -1;
    // TODO: "Designate G%d charset %c\n", bytes[0] - 0x28, bytes[1];
    return 2;

  case 0x3d:
    vt->mode.keypad = 1;
    return 1;

  case 0x3e:
    vt->mode.keypad = 0;
    return 1;

  default:
    return 0;
  }
}

static void set_dec_mode(VTerm *vt, int num, int val)
{
  VTermState *state = vt->state;

  switch(num) {
  case 1:
    vt->mode.cursor = val;
    break;

  case 7:
    vt->mode.autowrap = val;
    break;

  case 12:
    setmode(vt, VTERM_MODE_DEC_CURSORBLINK, val);
    break;

  case 25:
    setmode(vt, VTERM_MODE_DEC_CURSORVISIBLE, val);
    break;

  case 1000:
    if(val)
      state->mouse_buttons = 0;

    for(int cb = 0; cb < 2; cb++)
      if(state->callbacks[cb] && state->callbacks[cb]->setmousefunc)
        if((*state->callbacks[cb]->setmousefunc)(vt, val ? mousefunc : NULL, vt))
          break;

    break;

  case 1047:
    setmode(vt, VTERM_MODE_DEC_ALTSCREEN, val);
    break;

  case 1048:
    savecursor(vt, val);
    break;

  case 1049:
    setmode(vt, VTERM_MODE_DEC_ALTSCREEN, val);
    savecursor(vt, val);
    break;

  default:
    printf("libvterm: Unknown DEC mode %d\n", num);
    return;
  }
}

static int on_csi_qmark(VTerm *vt, const long *args, int argcount, char command)
{
  switch(command) {
  case 0x68: // DEC private mode set
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_dec_mode(vt, CSI_ARG(args[0]), 1);
    break;

  case 0x6c: // DEC private mode reset
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_dec_mode(vt, CSI_ARG(args[0]), 0);
    break;

  default:
    return 0;
  }

  return 1;
}

static int on_csi(VTerm *vt, const char *intermed, const long args[], int argcount, char command)
{
  if(intermed) {
    if(strcmp(intermed, "?") == 0)
      return on_csi_qmark(vt, args, argcount, command);

    return 0;
  }

  VTermState *state = vt->state;

  VTermPos oldpos = state->pos;

#define LBOUND(v,min) if((v) < (min)) (v) = (min)
#define UBOUND(v,max) if((v) > (max)) (v) = (max)

  // Some temporaries for later code
  int count;
  int row, col;
  VTermRect rect;

  switch(command) {
  case 0x40: // ICH - ECMA-48 8.3.64
    count = CSI_ARG_OR(args[0], 1);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = vt->cols;

    scroll(vt, rect, 0, -count);

    break;

  case 0x41: // CUU - ECMA-48 8.3.22
    count = CSI_ARG_OR(args[0], 1);
    state->pos.row -= count;
    LBOUND(state->pos.row, 0);
    break;

  case 0x42: // CUD - ECMA-48 8.3.19
    count = CSI_ARG_OR(args[0], 1);
    state->pos.row += count;
    UBOUND(state->pos.row, vt->rows-1);
    break;

  case 0x43: // CUF - ECMA-48 8.3.20
    count = CSI_ARG_OR(args[0], 1);
    state->pos.col += count;
    UBOUND(state->pos.col, vt->cols-1);
    break;

  case 0x44: // CUB - ECMA-48 8.3.18
    count = CSI_ARG_OR(args[0], 1);
    state->pos.col -= count;
    LBOUND(state->pos.col, 0);
    break;

  case 0x45: // CNL - ECMA-48 8.3.12
    count = CSI_ARG_OR(args[0], 1);
    state->pos.col = 0;
    state->pos.row += count;
    UBOUND(state->pos.row, vt->rows-1);
    break;

  case 0x46: // CPL - ECMA-48 8.3.13
    count = CSI_ARG_OR(args[0], 1);
    state->pos.col = 0;
    state->pos.row -= count;
    LBOUND(state->pos.row, 0);
    break;

  case 0x47: // CHA - ECMA-48 8.3.9
    count = CSI_ARG_OR(args[0], 1);
    state->pos.col = count-1;
    UBOUND(state->pos.col, vt->cols-1);
    break;

  case 0x48: // CUP - ECMA-48 8.3.21
    row = CSI_ARG_OR(args[0], 1);
    col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    // zero-based
    state->pos.row = row-1;
    UBOUND(state->pos.row, vt->rows-1);
    state->pos.col = col-1;
    UBOUND(state->pos.col, vt->cols-1);
    break;

  case 0x49: // CHT - ECMA-48 8.3.10
    count = CSI_ARG_OR(args[0], 1);
    tab(vt, count, +1);
    break;

  case 0x4a: // ED - ECMA-48 8.3.39
    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
      rect.start_col = state->pos.col; rect.end_col = vt->cols;
      if(rect.end_col > rect.start_col)
        erase(vt, rect);

      rect.start_row = state->pos.row + 1; rect.end_row = vt->rows;
      rect.start_col = 0;
      if(rect.end_row > rect.start_row)
        erase(vt, rect);
      break;

    case 1:
      rect.start_row = 0; rect.end_row = state->pos.row;
      rect.start_col = 0; rect.end_col = vt->cols;
      if(rect.end_col > rect.start_col)
        erase(vt, rect);

      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
                          rect.end_col = state->pos.col + 1;
      if(rect.end_row > rect.start_row)
        erase(vt, rect);
      break;

    case 2:
      rect.start_row = 0; rect.end_row = vt->rows;
      rect.start_col = 0; rect.end_col = vt->cols;
      erase(vt, rect);
      break;
    }

  case 0x4b: // EL - ECMA-48 8.3.41
    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;

    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_col = state->pos.col; rect.end_col = vt->cols; break;
    case 1:
      rect.start_col = 0; rect.end_col = state->pos.col + 1; break;
    case 2:
      rect.start_col = 0; rect.end_col = vt->cols; break;
    default:
      return 0;
    }

    if(rect.end_col > rect.start_col)
      erase(vt, rect);

    break;

  case 0x4c: // IL - ECMA-48 8.3.67
    count = CSI_ARG_OR(args[0], 1);

    rect.start_row = state->pos.row;
    rect.end_row   = state->scrollregion_end;
    rect.start_col = 0;
    rect.end_col   = vt->cols;

    scroll(vt, rect, -count, 0);

    break;

  case 0x4d: // DL - ECMA-48 8.3.32
    count = CSI_ARG_OR(args[0], 1);

    rect.start_row = state->pos.row;
    rect.end_row   = state->scrollregion_end;
    rect.start_col = 0;
    rect.end_col   = vt->cols;

    scroll(vt, rect, count, 0);

    break;

  case 0x50: // DCH - ECMA-48 8.3.26
    count = CSI_ARG_OR(args[0], 1);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = vt->cols;

    scroll(vt, rect, 0, count);

    break;

  case 0x53: // SU - ECMA-48 8.3.147
    count = CSI_ARG_OR(args[0], 1);

    rect.start_row = state->scrollregion_start,
    rect.end_row   = state->scrollregion_end,
    rect.start_col = 0,
    rect.end_col   = vt->cols,

    scroll(vt, rect, count, 0);

    break;

  case 0x54: // SD - ECMA-48 8.3.113
    count = CSI_ARG_OR(args[0], 1);

    rect.start_row = state->scrollregion_start,
    rect.end_row   = state->scrollregion_end,
    rect.start_col = 0,
    rect.end_col   = vt->cols,

    scroll(vt, rect, -count, 0);

    break;

  case 0x58: // ECH - ECMA-48 8.3.38
    count = CSI_ARG_OR(args[0], 1);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = state->pos.col + count;

    erase(vt, rect);
    break;

  case 0x5a: // CBT - ECMA-48 8.3.7
    count = CSI_ARG_OR(args[0], 1);
    tab(vt, count, -1);
    break;

  case 0x60: // HPA - ECMA-48 8.3.57
    col = CSI_ARG_OR(args[0], 1);
    state->pos.col = col-1;
    UBOUND(state->pos.col, vt->cols-1);
    break;

  case 0x61: // HPR - ECMA-48 8.3.59
    count = CSI_ARG_OR(args[0], 1);
    state->pos.col += count;
    UBOUND(state->pos.col, vt->cols-1);
    break;

  case 0x64: // VPA - ECMA-48 8.3.158
    row = CSI_ARG_OR(args[0], 1);
    state->pos.row = row-1;
    UBOUND(state->pos.row, vt->rows-1);
    break;

  case 0x65: // VPR - ECMA-48 8.3.160
    count = CSI_ARG_OR(args[0], 1);
    state->pos.row += count;
    UBOUND(state->pos.row, vt->rows-1);
    break;

  case 0x66: // HVP - ECMA-48 8.3.63
    row = CSI_ARG_OR(args[0], 1);
    col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    // zero-based
    state->pos.row = row-1;
    UBOUND(state->pos.row, vt->rows-1);
    state->pos.col = col-1;
    UBOUND(state->pos.col, vt->cols-1);
    break;

  case 0x6a: // HPB - ECMA-48 8.3.58
    count = CSI_ARG_OR(args[0], 1);
    state->pos.col -= count;
    LBOUND(state->pos.col, 0);
    break;

  case 0x6b: // VPB - ECMA-48 8.3.159
    count = CSI_ARG_OR(args[0], 1);
    state->pos.row -= count;
    LBOUND(state->pos.row, 0);
    break;

  case 0x6d: // SGR - ECMA-48 8.3.117
    vterm_state_setpen(vt, args, argcount);
    break;

  case 0x72: // DECSTBM - DEC custom
    state->scrollregion_start = CSI_ARG_OR(args[0], 1) - 1;
    state->scrollregion_end = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? vt->rows : CSI_ARG(args[1]);
    break;

  default:
    return 0;
  }

  updatecursor(vt, state, &oldpos);

  return 1;
}

static const VTermParserCallbacks parser_callbacks = {
  .text    = on_text,
  .control = on_control,
  .escape  = on_escape,
  .csi     = on_csi,
};

void vterm_set_state_callbacks(VTerm *vt, const VTermStateCallbacks *callbacks)
{
  if(callbacks) {
    if(!vt->state) {
      vt->state = vterm_state_new();
    }
    vt->state->callbacks[0] = callbacks;
    vt->parser_callbacks[1] = &parser_callbacks;

    // Initialise the modes
    vt->mode.autowrap = 1;
    VTermMode mode;
    for(mode = VTERM_MODE_NONE; mode < VTERM_MODE_MAX; mode++) {
      int val = 0;

      switch(mode) {
      case VTERM_MODE_DEC_CURSORBLINK:
      case VTERM_MODE_DEC_CURSORVISIBLE:
        val = 1;
        break;

      default:
        break;
      }

      setmode(vt, mode, val);
    }
  }
  else {
    if(vt->state) {
      vterm_state_free(vt->state);
      vt->state = NULL;
    }

    vt->parser_callbacks[1] = NULL;
  }
}
