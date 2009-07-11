#include "vterm_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef DEBUG
# define DEBUG_GLYPH_COMBINE
#endif

static VTermState *vterm_state_new(void)
{
  VTermState *state = g_new0(VTermState, 1);

  state->pos.row = 0;
  state->pos.col = 0;

  state->combine_chars_size = 16;
  state->combine_chars = g_new0(uint32_t, state->combine_chars_size);

  return state;
}

void vterm_state_free(VTermState *state)
{
  g_free(state);
}

void vterm_set_state_callbacks(VTerm *vt, const VTermStateCallbacks *callbacks)
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

  if(state->callbacks &&
     state->callbacks->setpen)
    (*state->callbacks->setpen)(vt, 0, &state->pen);

  if(state->callbacks &&
     state->callbacks->erase) {
    VTermRect rect = { 0, vt->rows, 0, vt->cols };
    (*state->callbacks->erase)(vt, rect, state->pen);
  }
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

  if(state->callbacks && state->callbacks->scroll)
    done = (*state->callbacks->scroll)(vt, rect, downward, rightward);

  if(!done && state->callbacks && state->callbacks->copyrect) {
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

    done = (*state->callbacks->copyrect)(vt, dest, src);
  }

  if(!done && state->callbacks && state->callbacks->copycell) {
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
        (*state->callbacks->copycell)(vt, pos, srcpos);
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

  if(state->callbacks &&
     state->callbacks->erase)
    (*state->callbacks->erase)(vt, rect, state->pen);
}

static void updatecursor(VTerm *vt, const VTermState *state, VTermPos *oldpos)
{
  if(state->pos.col != oldpos->col || state->pos.row != oldpos->row) {
    if(state->callbacks &&
      state->callbacks->movecursor)
      (*state->callbacks->movecursor)(vt, state->pos, *oldpos, vt->mode.cursor_visible);
  }
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

int vterm_state_on_text(VTerm *vt, const int codepoints[], int npoints)
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
      int done = 0;

      if(state->callbacks && state->callbacks->putglyph) {
        done = (*state->callbacks->putglyph)(vt, state->combine_chars, state->combine_width, state->combine_pos, state->pen);
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

int vterm_state_on_control(VTerm *vt, unsigned char control)
{
  VTermState *state = vt->state;

  VTermPos oldpos = state->pos;

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

int vterm_state_on_escape(VTerm *vt, const char *bytes, size_t len)
{
  switch(bytes[0]) {
  case 0x28: case 0x29: case 0x2a: case 0x2b:
    if(len < 2)
      return -1;
    // TODO: "Designate G%d charset %c\n", bytes[0] - 0x28, bytes[1];
    return 2;

  case 0x3d:
    vterm_state_setmode(vt, VTERM_MODE_KEYPAD, 1);
    return 1;

  case 0x3e:
    vterm_state_setmode(vt, VTERM_MODE_KEYPAD, 0);
    return 1;

  default:
    return 0;
  }
}

static void set_dec_mode(VTerm *vt, int num, int val)
{
  switch(num) {
  case 1:
    vterm_state_setmode(vt, VTERM_MODE_DEC_CURSOR, val);
    break;

  case 7:
    vterm_state_setmode(vt, VTERM_MODE_DEC_AUTOWRAP, val);
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

static int vterm_state_on_csi_qmark(VTerm *vt, const long *args, int argcount, char command)
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

int vterm_state_on_csi(VTerm *vt, const char *intermed, const long args[], int argcount, char command)
{
  if(intermed) {
    if(strcmp(intermed, "?") == 0)
      return vterm_state_on_csi_qmark(vt, args, argcount, command);

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
    if(!state->callbacks || !state->callbacks->erase)
      return 1;

    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
      rect.start_col = state->pos.col; rect.end_col = vt->cols;
      if(rect.end_col > rect.start_col)
        (*state->callbacks->erase)(vt, rect, state->pen);

      rect.start_row = state->pos.row + 1; rect.end_row = vt->rows;
      rect.start_col = 0;
      if(rect.end_row > rect.start_row)
        (*state->callbacks->erase)(vt, rect, state->pen);
      break;

    case 1:
      rect.start_row = 0; rect.end_row = state->pos.row;
      rect.start_col = 0; rect.end_col = vt->cols;
      if(rect.end_col > rect.start_col)
        (*state->callbacks->erase)(vt, rect, state->pen);

      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
                          rect.end_col = state->pos.col + 1;
      if(rect.end_row > rect.start_row)
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

    if(state->callbacks && state->callbacks->erase)
      if(rect.end_col > rect.start_col)
        (*state->callbacks->erase)(vt, rect, state->pen);

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

    if(state->callbacks && state->callbacks->erase)
      (*state->callbacks->erase)(vt, rect, state->pen);

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
