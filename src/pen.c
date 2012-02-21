#include "vterm_internal.h"

#include <stdio.h>

static const VTermColor ansi_colors[] = {
  /* R    G    B */
  {   0,   0,   0 }, // black
  { 224,   0,   0 }, // red
  {   0, 224,   0 }, // green
  { 224, 224,   0 }, // yellow
  {   0,   0, 224 }, // blue
  { 224,   0, 224 }, // magenta
  {   0, 224, 224 }, // cyan
  { 224, 224, 224 }, // white == light grey

  // high intensity
  { 128, 128, 128 }, // black
  { 255,  64,  64 }, // red
  {  64, 255,  64 }, // green
  { 255, 255,  64 }, // yellow
  {  64,  64, 255 }, // blue
  { 255,  64, 255 }, // magenta
  {  64, 255, 255 }, // cyan
  { 255, 255, 255 }, // white for real
};

/* Attempt at some gamma ramps */
static int gamma6[] = {
  0, 105, 149, 182, 209, 233, 255
};

static int gamma24[] = {
  0, 49, 72, 90, 105, 117, 129, 139, 149,
  158, 166, 174, 182, 189, 196, 203, 209,
  215, 222, 227, 233, 239, 244, 249, 255,
};

static void lookup_colour_ansi(long index, char is_bg, VTermColor *col)
{
  if(index >= 0 && index < 16) {
    *col = ansi_colors[index];
  }
}

static int lookup_colour(int palette, const long args[], int argcount, char is_bg, VTermColor *col)
{
  long index;

  switch(palette) {
  case 2: // RGB mode - 3 args contain colour values directly
    if(argcount < 3)
      return argcount;

    col->red   = CSI_ARG(args[0]);
    col->green = CSI_ARG(args[1]);
    col->blue  = CSI_ARG(args[2]);

    return 3;

  case 5: // XTerm 256-colour mode
    index = argcount ? CSI_ARG_OR(args[0], -1) : -1;

    if(index >= 0 && index < 16) {
      // Normal 8 colours or high intensity - parse as palette 0
      lookup_colour_ansi(index, is_bg, col);
    }
    else if(index >= 16 && index < 232) {
      // 216-colour cube
      index -= 16;

      col->blue  = gamma6[index     % 6];
      col->green = gamma6[index/6   % 6];
      col->red   = gamma6[index/6/6 % 6];
    }
    else if(index >= 232 && index < 256) {
      // 24 greyscales
      index -= 232;

      col->red   = gamma24[index];
      col->green = gamma24[index];
      col->blue  = gamma24[index];
    }

    return argcount ? 1 : 0;

  default:
    fprintf(stderr, "Unrecognised colour palette %d\n", palette);
    return 0;
  }
}

// Some conveniences

static void setpenattr(VTermState *state, VTermAttr attr, VTermValueType type, VTermValue *val)
{
#ifdef DEBUG
  if(type != vterm_get_attr_type(attr)) {
    fprintf(stderr, "Cannot set attr %d as it has type %d, not type %d\n",
        attr, vterm_get_attr_type(attr), type);
    return;
  }
#endif
  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(attr, val, state->cbdata);
}

static void setpenattr_bool(VTermState *state, VTermAttr attr, int boolean)
{
  VTermValue val = { .boolean = boolean };
  setpenattr(state, attr, VTERM_VALUETYPE_BOOL, &val);
}

static void setpenattr_int(VTermState *state, VTermAttr attr, int number)
{
  VTermValue val = { .number = number };
  setpenattr(state, attr, VTERM_VALUETYPE_INT, &val);
}

static void setpenattr_col_ansi(VTermState *state, VTermAttr attr, long col)
{
  VTermValue val;

  if(col == -1) {
    if(attr == VTERM_ATTR_BACKGROUND)
      val.color = state->default_bg;
    else
      val.color = state->default_fg;
  }
  else
    lookup_colour_ansi(col, attr == VTERM_ATTR_BACKGROUND, &val.color);

  setpenattr(state, attr, VTERM_VALUETYPE_COLOR, &val);
}

static int setpenattr_col_palette(VTermState *state, VTermAttr attr, const long args[], int argcount)
{
  VTermValue val;

  if(!argcount)
    return 0;

  int eaten = lookup_colour(CSI_ARG(args[0]), args + 1, argcount - 1, attr == VTERM_ATTR_BACKGROUND, &val.color);

  setpenattr(state, attr, VTERM_VALUETYPE_COLOR, &val);

  return eaten + 1; // we ate palette
}

void vterm_state_resetpen(VTermState *state)
{
  state->pen.bold = 0;      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
  state->pen.underline = 0; setpenattr_int( state, VTERM_ATTR_UNDERLINE, 0);
  state->pen.italic = 0;    setpenattr_bool(state, VTERM_ATTR_ITALIC, 0);
  state->pen.blink = 0;     setpenattr_bool(state, VTERM_ATTR_BLINK, 0);
  state->pen.reverse = 0;   setpenattr_bool(state, VTERM_ATTR_REVERSE, 0);
  state->pen.strike = 0;    setpenattr_bool(state, VTERM_ATTR_STRIKE, 0);
  state->pen.font = 0;      setpenattr_int( state, VTERM_ATTR_FONT, 0);
  state->fg_ansi = -1;

  setpenattr_col_ansi(state, VTERM_ATTR_FOREGROUND, -1);
  setpenattr_col_ansi(state, VTERM_ATTR_BACKGROUND, -1);
}

void vterm_state_set_default_colors(VTermState *state, VTermColor *default_fg, VTermColor *default_bg)
{
  state->default_fg = *default_fg;
  state->default_bg = *default_bg;
}

void vterm_state_set_bold_highbright(VTermState *state, int bold_is_highbright)
{
  state->bold_is_highbright = bold_is_highbright;
}

void vterm_state_setpen(VTermState *state, const long args[], int argcount)
{
  // SGR - ECMA-48 8.3.117

  int argi = 0;
  int value;

  while(argi < argcount) {
    // This logic is easier to do 'done' backwards; set it true, and make it
    // false again in the 'default' case
    int done = 1;

    long arg;
    switch(arg = CSI_ARG(args[argi])) {
    case CSI_ARG_MISSING:
    case 0: // Reset
      vterm_state_resetpen(state);
      break;

    case 1: // Bold on
      state->pen.bold = 1;
      setpenattr_bool(state, VTERM_ATTR_BOLD, 1);
      if(state->fg_ansi > -1 && state->bold_is_highbright)
        setpenattr_col_ansi(state, VTERM_ATTR_FOREGROUND, state->fg_ansi + (state->pen.bold ? 8 : 0));
      break;

    case 3: // Italic on
      state->pen.italic = 1;
      setpenattr_bool(state, VTERM_ATTR_ITALIC, 1);
      break;

    case 4: // Underline single
      state->pen.underline = 1;
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 1);
      break;

    case 5: // Blink
      state->pen.blink = 1;
      setpenattr_bool(state, VTERM_ATTR_BLINK, 1);
      break;

    case 7: // Reverse on
      state->pen.reverse = 1;
      setpenattr_bool(state, VTERM_ATTR_REVERSE, 1);
      break;

    case 9: // Strikethrough on
      state->pen.strike = 1;
      setpenattr_bool(state, VTERM_ATTR_STRIKE, 1);
      break;

    case 10: case 11: case 12: case 13: case 14:
    case 15: case 16: case 17: case 18: case 19: // Select font
      state->pen.font = CSI_ARG(args[argi]) - 10;
      setpenattr_int(state, VTERM_ATTR_FONT, state->pen.font);
      break;

    case 21: // Underline double
      state->pen.underline = 2;
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 2);
      break;

    case 22: // Bold off
      state->pen.bold = 0;
      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
      break;

    case 23: // Italic and Gothic (currently unsupported) off
      state->pen.italic = 0;
      setpenattr_bool(state, VTERM_ATTR_ITALIC, 0);
      break;

    case 24: // Underline off
      state->pen.underline = 0;
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 0);
      break;

    case 25: // Blink off
      state->pen.blink = 0;
      setpenattr_bool(state, VTERM_ATTR_BLINK, 0);
      break;

    case 27: // Reverse off
      state->pen.reverse = 0;
      setpenattr_bool(state, VTERM_ATTR_REVERSE, 0);
      break;

    case 29: // Strikethrough off
      state->pen.strike = 0;
      setpenattr_bool(state, VTERM_ATTR_STRIKE, 0);
      break;

    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37: // Foreground colour palette
      value = CSI_ARG(args[argi]) - 30;
      state->fg_ansi = value;
      if(state->pen.bold && state->bold_is_highbright)
        value += 8;
      setpenattr_col_ansi(state, VTERM_ATTR_FOREGROUND, value);
      break;

    case 38: // Foreground colour alternative palette
      state->fg_ansi = -1;
      argi += setpenattr_col_palette(state, VTERM_ATTR_FOREGROUND, args + argi + 1, argcount - argi - 1);
      break;

    case 39: // Foreground colour default
      state->fg_ansi = -1;
      setpenattr_col_ansi(state, VTERM_ATTR_FOREGROUND, -1);
      break;

    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47: // Background colour palette
      setpenattr_col_ansi(state, VTERM_ATTR_BACKGROUND, CSI_ARG(args[argi]) - 40);
      break;

    case 48: // Background colour alternative palette
      argi += setpenattr_col_palette(state, VTERM_ATTR_BACKGROUND, args + argi + 1, argcount - argi - 1);
      break;

    case 49: // Default background
      setpenattr_col_ansi(state, VTERM_ATTR_BACKGROUND, -1);
      break;

    case 90: case 91: case 92: case 93:
    case 94: case 95: case 96: case 97: // Foreground colour high-intensity palette
      setpenattr_col_ansi(state, VTERM_ATTR_FOREGROUND, CSI_ARG(args[argi]) - 90 + 8);
      break;

    case 100: case 101: case 102: case 103:
    case 104: case 105: case 106: case 107: // Background colour high-intensity palette
      setpenattr_col_ansi(state, VTERM_ATTR_BACKGROUND, CSI_ARG(args[argi]) - 100 + 8);
      break;

    default:
      done = 0;
      break;
    }

    if(!done)
      fprintf(stderr, "libvterm: Unhandled CSI SGR %lu\n", arg);

    while(CSI_ARG_HAS_MORE(args[argi++]));
  }
}

int vterm_state_get_penattr(VTermState *state, VTermAttr attr, VTermValue *val)
{
  switch(attr) {
  case VTERM_ATTR_BOLD:
    val->boolean = state->pen.bold;
    return 1;

  case VTERM_ATTR_UNDERLINE:
    val->number = state->pen.underline;
    return 1;

  case VTERM_ATTR_ITALIC:
    val->boolean = state->pen.italic;
    return 1;

  case VTERM_ATTR_BLINK:
    val->boolean = state->pen.blink;
    return 1;

  case VTERM_ATTR_REVERSE:
    val->boolean = state->pen.reverse;
    return 1;

  case VTERM_ATTR_STRIKE:
    val->boolean = state->pen.strike;
    return 1;

  case VTERM_ATTR_FONT:
    val->number = state->pen.font;
    return 1;

  case VTERM_ATTR_FOREGROUND:
  case VTERM_ATTR_BACKGROUND:
    /* For now we don't store these.
     * TODO: Think about whether we should store index, or RGB value, or what... */
    return 0;
  }

  return 0;
}
