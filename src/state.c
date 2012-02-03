#include "vterm_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef DEBUG
# define DEBUG_GLYPH_COMBINE
#endif

#define MOUSE_WANT_DRAG 0x01
#define MOUSE_WANT_MOVE 0x02

/* Some convenient wrappers to make callback functions easier */

static void putglyph(VTermState *state, const uint32_t chars[], int width, VTermPos pos)
{
  if(state->callbacks && state->callbacks->putglyph)
    if((*state->callbacks->putglyph)(chars, width, pos, state->cbdata))
      return;

  fprintf(stderr, "libvterm: Unhandled putglyph U+%04x at (%d,%d)\n", chars[0], pos.col, pos.row);
}

static void updatecursor(VTermState *state, VTermPos *oldpos, int cancel_phantom)
{
  if(state->pos.col == oldpos->col && state->pos.row == oldpos->row)
    return;

  if(cancel_phantom)
    state->at_phantom = 0;

  if(state->callbacks && state->callbacks->movecursor)
    if((*state->callbacks->movecursor)(state->pos, *oldpos, state->mode.cursor_visible, state->cbdata))
      return;
}

static void erase(VTermState *state, VTermRect rect)
{
  if(state->callbacks && state->callbacks->erase)
    if((*state->callbacks->erase)(rect, state->cbdata))
      return;
}

static VTermState *vterm_state_new(VTerm *vt)
{
  VTermState *state = vterm_allocator_malloc(vt, sizeof(VTermState));

  state->vt = vt;

  state->rows = vt->rows;
  state->cols = vt->cols;

  // 90% grey so that pure white is brighter
  state->default_fg.red = state->default_fg.green = state->default_fg.blue = 240;
  state->default_bg.red = state->default_bg.green = state->default_bg.blue = 0;

  return state;
}

void vterm_state_free(VTermState *state)
{
  vterm_allocator_free(state->vt, state->combine_chars);
  vterm_allocator_free(state->vt, state);
}

static void scroll(VTermState *state, VTermRect rect, int downward, int rightward)
{
  if(!downward && !rightward)
    return;

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

  if(state->callbacks && state->callbacks->moverect)
    (*state->callbacks->moverect)(dest, src, state->cbdata);

  if(downward > 0)
    rect.start_row = rect.end_row - downward;
  else if(downward < 0)
    rect.end_row = rect.start_row - downward;

  if(rightward > 0)
    rect.start_col = rect.end_col - rightward;
  else if(rightward < 0)
    rect.end_col = rect.start_col - rightward;

  erase(state, rect);
}

static void linefeed(VTermState *state)
{
  if(state->pos.row == SCROLLREGION_END(state) - 1) {
    VTermRect rect = {
      .start_row = state->scrollregion_start,
      .end_row   = SCROLLREGION_END(state),
      .start_col = 0,
      .end_col   = state->cols,
    };

    scroll(state, rect, 1, 0);
  }
  else if(state->pos.row < state->rows-1)
    state->pos.row++;
}

static void grow_combine_buffer(VTermState *state)
{
  size_t    new_size = state->combine_chars_size * 2;
  uint32_t *new_chars = vterm_allocator_malloc(state->vt, new_size);

  memcpy(new_chars, state->combine_chars, state->combine_chars_size);

  vterm_allocator_free(state->vt, state->combine_chars);
  state->combine_chars = new_chars;
}

static void set_col_tabstop(VTermState *state, int col)
{
  unsigned char mask = 1 << (col & 7);
  state->tabstops[col >> 3] |= mask;
}

static void clear_col_tabstop(VTermState *state, int col)
{
  unsigned char mask = 1 << (col & 7);
  state->tabstops[col >> 3] &= ~mask;
}

static int is_col_tabstop(VTermState *state, int col)
{
  unsigned char mask = 1 << (col & 7);
  return state->tabstops[col >> 3] & mask;
}

static void tab(VTermState *state, int count, int direction)
{
  while(count--)
    while(state->pos.col >= 0 && state->pos.col < state->cols-1) {
      state->pos.col += direction;

      if(is_col_tabstop(state, state->pos.col))
        break;
    }
}

static int on_text(const char bytes[], size_t len, void *user)
{
  VTermState *state = user;

  VTermPos oldpos = state->pos;

  // We'll have at most len codepoints
  uint32_t codepoints[len];
  int npoints = 0;
  size_t eaten = 0;

  VTermEncoding *enc = !(bytes[eaten] & 0x80) ? state->encoding[state->gl_set] :
                       state->vt->is_utf8     ? vterm_lookup_encoding(ENC_UTF8, 'u') :
                                                state->encoding[state->gr_set];

  (*enc->decode)(enc, codepoints, &npoints, len, bytes, &eaten, len);

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
      putglyph(state, state->combine_chars, state->combine_width, state->combine_pos);
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

    if(state->at_phantom) {
      linefeed(state);
      state->pos.col = 0;
      state->at_phantom = 0;
    }

    if(state->mode.insert) {
      /* TODO: This will be a little inefficient for large bodies of text, as
       * it'll have to 'ICH' effectively before every glyph. We should scan
       * ahead and ICH as many times as required
       */
      VTermRect rect = {
        .start_row = state->pos.row,
        .end_row   = state->pos.row + 1,
        .start_col = state->pos.col,
        .end_col   = state->cols,
      };
      scroll(state, rect, 0, -1);
    }
    putglyph(state, chars, width, state->pos);

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

    if(state->pos.col + width >= state->cols) {
      if(state->mode.autowrap)
        state->at_phantom = 1;
    }
    else {
      state->pos.col += width;
    }
  }

  updatecursor(state, &oldpos, 0);

  return eaten;
}

static int on_control(unsigned char control, void *user)
{
  VTermState *state = user;

  VTermPos oldpos = state->pos;

  switch(control) {
  case 0x00: // NUL - ECMA-48 8.3.88
    /* no effect */
    break;

  case 0x07: // BEL - ECMA-48 8.3.3
    if(state->callbacks && state->callbacks->bell)
      (*state->callbacks->bell)(state->cbdata);
    break;

  case 0x08: // BS - ECMA-48 8.3.5
    if(state->pos.col > 0)
      state->pos.col--;
    break;

  case 0x09: // HT - ECMA-48 8.3.60
    tab(state, 1, +1);
    break;

  case 0x0a: // LF - ECMA-48 8.3.74
  case 0x0b: // VT
  case 0x0c: // FF
    linefeed(state);
    break;

  case 0x0d: // CR - ECMA-48 8.3.15
    state->pos.col = 0;
    break;

  case 0x0e: // LS1 - ECMA-48 8.3.76
    state->gl_set = 1;
    break;

  case 0x0f: // LS0 - ECMA-48 8.3.75
    state->gl_set = 0;
    break;

  case 0x84: // IND - DEPRECATED but implemented for completeness
    linefeed(state);
    break;

  case 0x85: // NEL - ECMA-48 8.3.86
    linefeed(state);
    state->pos.col = 0;
    break;

  case 0x8d: // RI - ECMA-48 8.3.104
    if(state->pos.row == state->scrollregion_start) {
      VTermRect rect = {
        .start_row = state->scrollregion_start,
        .end_row   = SCROLLREGION_END(state),
        .start_col = 0,
        .end_col   = state->cols,
      };

      scroll(state, rect, -1, 0);
    }
    else if(state->pos.row > 0)
        state->pos.row--;
    break;

  default:
    return 0;
  }

  updatecursor(state, &oldpos, 1);

  return 1;
}

static void mousefunc(int col, int row, int button, int pressed, void *data)
{
  VTermState *state = data;

  int old_col     = state->mouse_col;
  int old_row     = state->mouse_row;
  int old_buttons = state->mouse_buttons;

  state->mouse_col = col;
  state->mouse_row = row;

  if(button > 0) {
    if(pressed)
      state->mouse_buttons |= (1 << (button-1));
    else
      state->mouse_buttons &= ~(1 << (button-1));
  }

  if(state->mouse_buttons != old_buttons) {
    if(button < 4) {
      vterm_push_output_sprintf(state->vt, "\e[M%c%c%c", pressed ? button-1 + 0x20 : 0x23, col + 0x21, row + 0x21);
    }
  }
  else if(col != old_col || row != old_row) {
    if((state->mouse_flags & MOUSE_WANT_DRAG && state->mouse_buttons) ||
       (state->mouse_flags & MOUSE_WANT_MOVE)) {
      int button = state->mouse_buttons & 0x01 ? 1 :
                   state->mouse_buttons & 0x02 ? 2 :
                   state->mouse_buttons & 0x04 ? 3 : 4;
      vterm_push_output_sprintf(state->vt, "\e[M%c%c%c", button-1 + 0x40, col + 0x21, row + 0x21);
    }
  }
}

static int settermprop_bool(VTermState *state, VTermProp prop, int v)
{
  VTermValue val;
  val.boolean = v;

#ifdef DEBUG
  if(VTERM_VALUETYPE_BOOL != vterm_get_prop_type(prop)) {
    fprintf(stderr, "Cannot set prop %d as it has type %d, not type BOOL\n",
        prop, vterm_get_prop_type(prop));
    return;
  }
#endif

  if(state->callbacks && state->callbacks->settermprop)
    if((*state->callbacks->settermprop)(prop, &val, state->cbdata))
      return 1;

  return 0;
}

static int settermprop_int(VTermState *state, VTermProp prop, int v)
{
  VTermValue val;
  val.number = v;

#ifdef DEBUG
  if(VTERM_VALUETYPE_INT != vterm_get_prop_type(prop)) {
    fprintf(stderr, "Cannot set prop %d as it has type %d, not type int\n",
        prop, vterm_get_prop_type(prop));
    return;
  }
#endif

  if(state->callbacks && state->callbacks->settermprop)
    if((*state->callbacks->settermprop)(prop, &val, state->cbdata))
      return 1;

  return 0;
}

static int settermprop_string(VTermState *state, VTermProp prop, const char *str, size_t len)
{
  char strvalue[len+1];
  strncpy(strvalue, str, len);
  strvalue[len] = 0;

  VTermValue val;
  val.string = strvalue;

#ifdef DEBUG
  if(VTERM_VALUETYPE_STRING != vterm_get_prop_type(prop)) {
    fprintf(stderr, "Cannot set prop %d as it has type %d, not type STRING\n",
        prop, vterm_get_prop_type(prop));
    return;
  }
#endif

  if(state->callbacks && state->callbacks->settermprop)
    if((*state->callbacks->settermprop)(prop, &val, state->cbdata))
      return 1;

  return 0;
}

static void savecursor(VTermState *state, int save)
{
  state->mode.saved_cursor = save;
  if(save) {
    state->saved_pos = state->pos;
  }
  else {
    VTermPos oldpos = state->pos;
    state->pos = state->saved_pos;
    updatecursor(state, &oldpos, 1);
  }
}

static void altscreen(VTermState *state, int alt)
{
  /* Only store that we're on the alternate screen if the usercode said it
   * switched */
  if(!settermprop_bool(state, VTERM_PROP_ALTSCREEN, alt))
    return;

  state->mode.alt_screen = alt;
  if(alt) {
    VTermRect rect = {
      .start_row = 0,
      .start_col = 0,
      .end_row = state->rows,
      .end_col = state->cols,
    };
    erase(state, rect);
  }
}

static int on_escape(const char *bytes, size_t len, void *user)
{
  VTermState *state = user;

  /* Command byte is the final byte */
  switch(bytes[len-1]) {
  case '0': case 'A': case 'B': case 'u':
    if(len != 2)
      return 0;

    {
      int setnum = bytes[0] - 0x28;
      VTermEncoding *newenc = vterm_lookup_encoding(ENC_SINGLE_94, bytes[1]);

      if(newenc)
        state->encoding[setnum] = newenc;
    }

    return 2;

  case 0x38:
    if(len == 2 && bytes[0] == '#') { // DECALN
      VTermPos pos;
      uint32_t E[] = { 'E', 0 };
      for(pos.row = 0; pos.row < state->rows; pos.row++)
        for(pos.col = 0; pos.col < state->cols; pos.col++)
          putglyph(state, E, 1, pos);

      return 2;
    }
    return 0;

  case 0x3d:
    state->mode.keypad = 1;
    return 1;

  case 0x3e:
    state->mode.keypad = 0;
    return 1;

  case 0x6e: // LS2 - ECMA-48 8.3.78
    state->gl_set = 2;
    return 1;

  case 0x6f: // LS3 - ECMA-48 8.3.80
    state->gl_set = 3;
    return 1;

  default:
    return 0;
  }
}

static void set_mode(VTermState *state, int num, int val)
{
  switch(num) {
  case 4: // IRM - ECMA-48 7.2.10
    state->mode.insert = val;
    break;

  default:
    fprintf(stderr, "libvterm: Unknown mode %d\n", num);
    return;
  }
}

static void set_dec_mode(VTermState *state, int num, int val)
{
  switch(num) {
  case 1:
    state->mode.cursor = val;
    break;

  case 5:
    settermprop_bool(state, VTERM_PROP_REVERSE, val);
    break;

  case 6: // DECOM - origin mode
    {
      VTermPos oldpos = state->pos;
      state->mode.origin = val;
      state->pos.row = state->mode.origin ? state->scrollregion_start : 0;
      state->pos.col = 0;
      updatecursor(state, &oldpos, 1);
    }
    break;

  case 7:
    state->mode.autowrap = val;
    break;

  case 12:
    settermprop_bool(state, VTERM_PROP_CURSORBLINK, val);
    break;

  case 25:
    state->mode.cursor_visible = val;
    settermprop_bool(state, VTERM_PROP_CURSORVISIBLE, val);
    break;

  case 1000:
  case 1002:
  case 1003:
    if(val) {
      state->mouse_col     = 0;
      state->mouse_row     = 0;
      state->mouse_buttons = 0;

      state->mouse_flags = 0;

      if(num == 1002)
        state->mouse_flags |= MOUSE_WANT_DRAG;
      if(num == 1003)
        state->mouse_flags |= MOUSE_WANT_MOVE;
    }

    if(state->callbacks && state->callbacks->setmousefunc)
      (*state->callbacks->setmousefunc)(val ? mousefunc : NULL, state, state->cbdata);

    break;

  case 1047:
    altscreen(state, val);
    break;

  case 1048:
    savecursor(state, val);
    break;

  case 1049:
    altscreen(state, val);
    savecursor(state, val);
    break;

  default:
    fprintf(stderr, "libvterm: Unknown DEC mode %d\n", num);
    return;
  }
}

static int on_csi(const char *leader, const long args[], int argcount, const char *intermed, char command, void *user)
{
  VTermState *state = user;
  int leader_byte = 0;
  int intermed_byte = 0;

  if(leader && leader[0]) {
    if(leader[1]) // longer than 1 char
      return 0;

    switch(leader[0]) {
    case '?':
    case '>':
      leader_byte = leader[0];
      break;
    default:
      return 0;
    }
  }

  if(intermed && intermed[0]) {
    if(intermed[1]) // longer than 1 char
      return 0;

    switch(intermed[0]) {
    case ' ':
      intermed_byte = intermed[0];
      break;
    default:
      return 0;
    }
  }

  VTermPos oldpos = state->pos;

  // Some temporaries for later code
  int count, val;
  int row, col;
  VTermRect rect;

#define LEADER(l,b) ((l << 8) | b)
#define INTERMED(i,b) ((i << 16) | b)

  switch(intermed_byte << 16 | leader_byte << 8 | command) {
  case 0x40: // ICH - ECMA-48 8.3.64
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = state->cols;

    scroll(state, rect, 0, -count);

    break;

  case 0x41: // CUU - ECMA-48 8.3.22
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row -= count;
    state->at_phantom = 0;
    break;

  case 0x42: // CUD - ECMA-48 8.3.19
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row += count;
    state->at_phantom = 0;
    break;

  case 0x43: // CUF - ECMA-48 8.3.20
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col += count;
    state->at_phantom = 0;
    break;

  case 0x44: // CUB - ECMA-48 8.3.18
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col -= count;
    state->at_phantom = 0;
    break;

  case 0x45: // CNL - ECMA-48 8.3.12
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col = 0;
    state->pos.row += count;
    state->at_phantom = 0;
    break;

  case 0x46: // CPL - ECMA-48 8.3.13
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col = 0;
    state->pos.row -= count;
    state->at_phantom = 0;
    break;

  case 0x47: // CHA - ECMA-48 8.3.9
    val = CSI_ARG_OR(args[0], 1);
    state->pos.col = val-1;
    state->at_phantom = 0;
    break;

  case 0x48: // CUP - ECMA-48 8.3.21
    row = CSI_ARG_OR(args[0], 1);
    col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    // zero-based
    state->pos.row = row-1;
    state->pos.col = col-1;
    if(state->mode.origin)
      state->pos.row += state->scrollregion_start;
    state->at_phantom = 0;
    break;

  case 0x49: // CHT - ECMA-48 8.3.10
    count = CSI_ARG_COUNT(args[0]);
    tab(state, count, +1);
    break;

  case 0x4a: // ED - ECMA-48 8.3.39
    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
      rect.start_col = state->pos.col; rect.end_col = state->cols;
      if(rect.end_col > rect.start_col)
        erase(state, rect);

      rect.start_row = state->pos.row + 1; rect.end_row = state->rows;
      rect.start_col = 0;
      if(rect.end_row > rect.start_row)
        erase(state, rect);
      break;

    case 1:
      rect.start_row = 0; rect.end_row = state->pos.row;
      rect.start_col = 0; rect.end_col = state->cols;
      if(rect.end_col > rect.start_col)
        erase(state, rect);

      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
                          rect.end_col = state->pos.col + 1;
      if(rect.end_row > rect.start_row)
        erase(state, rect);
      break;

    case 2:
      rect.start_row = 0; rect.end_row = state->rows;
      rect.start_col = 0; rect.end_col = state->cols;
      erase(state, rect);
      break;
    }
    break;

  case 0x4b: // EL - ECMA-48 8.3.41
    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;

    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_col = state->pos.col; rect.end_col = state->cols; break;
    case 1:
      rect.start_col = 0; rect.end_col = state->pos.col + 1; break;
    case 2:
      rect.start_col = 0; rect.end_col = state->cols; break;
    default:
      return 0;
    }

    if(rect.end_col > rect.start_col)
      erase(state, rect);

    break;

  case 0x4c: // IL - ECMA-48 8.3.67
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->pos.row;
    rect.end_row   = SCROLLREGION_END(state);
    rect.start_col = 0;
    rect.end_col   = state->cols;

    scroll(state, rect, -count, 0);

    break;

  case 0x4d: // DL - ECMA-48 8.3.32
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->pos.row;
    rect.end_row   = SCROLLREGION_END(state);
    rect.start_col = 0;
    rect.end_col   = state->cols;

    scroll(state, rect, count, 0);

    break;

  case 0x50: // DCH - ECMA-48 8.3.26
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = state->cols;

    scroll(state, rect, 0, count);

    break;

  case 0x53: // SU - ECMA-48 8.3.147
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->scrollregion_start;
    rect.end_row   = SCROLLREGION_END(state);
    rect.start_col = 0;
    rect.end_col   = state->cols;

    scroll(state, rect, count, 0);

    break;

  case 0x54: // SD - ECMA-48 8.3.113
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->scrollregion_start;
    rect.end_row   = SCROLLREGION_END(state);
    rect.start_col = 0;
    rect.end_col   = state->cols;

    scroll(state, rect, -count, 0);

    break;

  case 0x58: // ECH - ECMA-48 8.3.38
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = state->pos.col + count;

    erase(state, rect);
    break;

  case 0x5a: // CBT - ECMA-48 8.3.7
    count = CSI_ARG_COUNT(args[0]);
    tab(state, count, -1);
    break;

  case 0x60: // HPA - ECMA-48 8.3.57
    col = CSI_ARG_OR(args[0], 1);
    state->pos.col = col-1;
    state->at_phantom = 0;
    break;

  case 0x61: // HPR - ECMA-48 8.3.59
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col += count;
    state->at_phantom = 0;
    break;

  case LEADER('>', 0x63): // DEC secondary Device Attributes
    vterm_push_output_sprintf(state->vt, "\e[>%d;%d;%dc", 0, 100, 0);
    break;

  case 0x64: // VPA - ECMA-48 8.3.158
    row = CSI_ARG_OR(args[0], 1);
    state->pos.row = row-1;
    if(state->mode.origin)
      state->pos.row += state->scrollregion_start;
    state->at_phantom = 0;
    break;

  case 0x65: // VPR - ECMA-48 8.3.160
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row += count;
    state->at_phantom = 0;
    break;

  case 0x66: // HVP - ECMA-48 8.3.63
    row = CSI_ARG_OR(args[0], 1);
    col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    // zero-based
    state->pos.row = row-1;
    state->pos.col = col-1;
    if(state->mode.origin)
      state->pos.row += state->scrollregion_start;
    state->at_phantom = 0;
    break;

  case 0x68: // SM - ECMA-48 8.3.125
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_mode(state, CSI_ARG(args[0]), 1);
    break;

  case LEADER('?', 0x68): // DEC private mode set
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_dec_mode(state, CSI_ARG(args[0]), 1);
    break;

  case 0x6a: // HPB - ECMA-48 8.3.58
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col -= count;
    state->at_phantom = 0;
    break;

  case 0x6b: // VPB - ECMA-48 8.3.159
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row -= count;
    state->at_phantom = 0;
    break;

  case 0x6c: // RM - ECMA-48 8.3.106
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_mode(state, CSI_ARG(args[0]), 0);
    break;

  case LEADER('?', 0x6c): // DEC private mode reset
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_dec_mode(state, CSI_ARG(args[0]), 0);
    break;

  case 0x6d: // SGR - ECMA-48 8.3.117
    vterm_state_setpen(state, args, argcount);
    break;

  case INTERMED(' ', 0x71): // DECSCUSR - DEC set cursor shape
    val = CSI_ARG_OR(args[0], 1);

    switch(val) {
    case 0: case 1:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 1);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_BLOCK);
      break;
    case 2:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 0);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_BLOCK);
      break;
    case 3:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 1);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_UNDERLINE);
      break;
    case 4:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 0);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_UNDERLINE);
      break;
    }
    break;

  case 0x72: // DECSTBM - DEC custom
    state->scrollregion_start = CSI_ARG_OR(args[0], 1) - 1;
    state->scrollregion_end = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? -1 : CSI_ARG(args[1]);
    if(state->scrollregion_start == 0 && state->scrollregion_end == state->rows)
      state->scrollregion_end = -1;
    break;

  default:
    return 0;
  }

#define LBOUND(v,min) if((v) < (min)) (v) = (min)
#define UBOUND(v,max) if((v) > (max)) (v) = (max)

  LBOUND(state->pos.col, 0);
  UBOUND(state->pos.col, state->cols-1);

  if(state->mode.origin) {
    LBOUND(state->pos.row, state->scrollregion_start);
    UBOUND(state->pos.row, state->scrollregion_end-1);
  }
  else {
    LBOUND(state->pos.row, 0);
    UBOUND(state->pos.row, state->rows-1);
  }

  updatecursor(state, &oldpos, 1);

  return 1;
}

static int on_osc(const char *command, size_t cmdlen, void *user)
{
  VTermState *state = user;

  if(cmdlen < 2)
    return 0;

  if(strncmp(command, "0;", 2) == 0) {
    settermprop_string(state, VTERM_PROP_ICONNAME, command + 2, cmdlen - 2);
    settermprop_string(state, VTERM_PROP_TITLE, command + 2, cmdlen - 2);
    return 1;
  }
  else if(strncmp(command, "1;", 2) == 0) {
    settermprop_string(state, VTERM_PROP_ICONNAME, command + 2, cmdlen - 2);
    return 1;
  }
  else if(strncmp(command, "2;", 2) == 0) {
    settermprop_string(state, VTERM_PROP_TITLE, command + 2, cmdlen - 2);
    return 1;
  }

  return 0;
}

static int on_resize(int rows, int cols, void *user)
{
  VTermState *state = user;
  VTermPos oldpos = state->pos;

  if(cols != state->cols) {
    unsigned char *newtabstops = vterm_allocator_malloc(state->vt, (cols + 7) / 8);

    /* TODO: This can all be done much more efficiently bytewise */
    int col;
    for(col = 0; col < state->cols && col < cols; col++) {
      unsigned char mask = 1 << (col & 7);
      if(state->tabstops[col >> 3] & mask)
        newtabstops[col >> 3] |= mask;
      else
        newtabstops[col >> 3] &= ~mask;
      }

    for( ; col < cols; col++) {
      unsigned char mask = 1 << (col & 7);
      if(col % 8 == 0)
        newtabstops[col >> 3] |= mask;
      else
        newtabstops[col >> 3] &= ~mask;
    }

    vterm_allocator_free(state->vt, state->tabstops);
    state->tabstops = newtabstops;
  }

  state->rows = rows;
  state->cols = cols;

  if(state->pos.row >= rows)
    state->pos.row = rows - 1;
  if(state->pos.col >= cols)
    state->pos.col = cols - 1;

  if(state->at_phantom && state->pos.col < cols-1) {
    state->at_phantom = 0;
    state->pos.col++;
  }

  if(state->callbacks && state->callbacks->resize)
    (*state->callbacks->resize)(rows, cols, state->cbdata);

  updatecursor(state, &oldpos, 1);

  return 1;
}

static const VTermParserCallbacks parser_callbacks = {
  .text    = on_text,
  .control = on_control,
  .escape  = on_escape,
  .csi     = on_csi,
  .osc     = on_osc,
  .resize  = on_resize,
};

VTermState *vterm_obtain_state(VTerm *vt)
{
  if(vt->state)
    return vt->state;

  VTermState *state = vterm_state_new(vt);
  vt->state = state;

  state->combine_chars_size = 16;
  state->combine_chars = vterm_allocator_malloc(state->vt, sizeof(uint32_t) * state->combine_chars_size);

  state->tabstops = vterm_allocator_malloc(state->vt, (state->cols + 7) / 8);

  vterm_set_parser_callbacks(vt, &parser_callbacks, state);

  return state;
}

void vterm_state_reset(VTermState *state)
{
  state->pos.row = 0;
  state->pos.col = 0;
  state->at_phantom = 0;

  state->scrollregion_start = 0;
  state->scrollregion_end = -1;

  state->mode.autowrap = 1;
  state->mode.cursor_visible = 1;
  state->mode.origin = 0;

  for(int col = 0; col < state->cols; col++)
    if(col % 8 == 0)
      set_col_tabstop(state, col);
    else
      clear_col_tabstop(state, col);

  if(state->callbacks && state->callbacks->initpen)
    (*state->callbacks->initpen)(state->cbdata);

  vterm_state_resetpen(state);

  VTermRect rect = { 0, state->rows, 0, state->cols };
  erase(state, rect);

  VTermEncoding *default_enc = state->vt->is_utf8 ?
      vterm_lookup_encoding(ENC_UTF8,      'u') :
      vterm_lookup_encoding(ENC_SINGLE_94, 'B');

  for(int i = 0; i < 4; i++)
    state->encoding[i] = default_enc;

  state->gl_set = 0;
  state->gr_set = 0;
}

void vterm_state_get_cursorpos(VTermState *state, VTermPos *cursorpos)
{
  *cursorpos = state->pos;
}

void vterm_state_set_callbacks(VTermState *state, const VTermStateCallbacks *callbacks, void *user)
{
  if(callbacks) {
    state->callbacks = callbacks;
    state->cbdata = user;

    // Initialise the props
    settermprop_bool(state, VTERM_PROP_CURSORBLINK, 1);
    settermprop_bool(state, VTERM_PROP_CURSORVISIBLE, state->mode.cursor_visible);
    settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_BLOCK);

    if(state->callbacks && state->callbacks->initpen)
      (*state->callbacks->initpen)(state->cbdata);
  }
  else {
    state->callbacks = NULL;
    state->cbdata = NULL;
  }
}
