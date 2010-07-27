#include "vterm_internal.h"

#include <stdio.h>

static const VTermColor ansi_colors[] = {
  /* R    G    B */
  {   0,   0,   0 }, // black
  { 255,   0,   0 }, // red
  {   0, 255,   0 }, // green
  { 255, 255,   0 }, // yellow
  {  64,  64, 255 }, // blue - make it more visible
  { 255,   0, 255 }, // magenta
  {   0, 255, 255 }, // cyan
  { 240, 240, 240 }, // white == 90% grey

  // high intensity
  { 128, 128, 128 }, // black
  { 255, 128, 128 }, // red
  { 128, 255, 128 }, // green
  { 255, 255, 128 }, // yellow
  { 160, 160, 255 }, // blue - make it more visible
  { 255, 128, 255 }, // magenta
  { 128, 255, 255 }, // cyan
  { 255, 255, 255 }, // white == 90% grey
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
  if(index == -1) {
    if(is_bg)
      col->red = col->green = col->blue = 0;
    else
      // 90% grey so that pure white is brighter
      col->red = col->green = col->blue = 240;
  }
  else if(index >= 0 && index < 16) {
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

static void setpenattr(VTermState *state, VTermAttr attr, VTermValue *val)
{
  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(attr, val, state->cbdata);
}

static void setpenattr_bool(VTermState *state, VTermAttr attr, int boolean)
{
  VTermValue val = { .boolean = boolean };
  setpenattr(state, attr, &val);
}

static void setpenattr_int(VTermState *state, VTermAttr attr, int number)
{
  VTermValue val = { .number = number };
  setpenattr(state, attr, &val);
}

static void setpenattr_col_ansi(VTermState *state, VTermAttr attr, long col)
{
  VTermValue val;

  lookup_colour_ansi(col, attr == VTERM_ATTR_BACKGROUND, &val.color);

  setpenattr(state, attr, &val);
}

static int setpenattr_col_palette(VTermState *state, VTermAttr attr, const long args[], int argcount)
{
  VTermValue val;

  if(!argcount)
    return 0;

  int eaten = lookup_colour(CSI_ARG(args[0]), args + 1, argcount - 1, attr == VTERM_ATTR_BACKGROUND, &val.color);

  setpenattr(state, attr, &val);

  return eaten + 1; // we ate palette
}

void vterm_state_setpen(VTermState *state, const long args[], int argcount)
{
  // SGR - ECMA-48 8.3.117

  int argi = 0;

  while(argi < argcount) {
    // This logic is easier to do 'done' backwards; set it true, and make it
    // false again in the 'default' case
    int done = 1;

    long arg;
    switch(arg = CSI_ARG(args[argi])) {
    case CSI_ARG_MISSING:
    case 0: // Reset
      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 0);
      setpenattr_bool(state, VTERM_ATTR_ITALIC, 0);
      setpenattr_bool(state, VTERM_ATTR_REVERSE, 0);
      setpenattr_int(state, VTERM_ATTR_FONT, 0);
      setpenattr_col_ansi(state, VTERM_ATTR_FOREGROUND, -1);
      setpenattr_col_ansi(state, VTERM_ATTR_BACKGROUND, -1);
      break;

    case 1: // Bold on
      setpenattr_bool(state, VTERM_ATTR_BOLD, 1);
      break;

    case 3: // Italic on
      setpenattr_bool(state, VTERM_ATTR_ITALIC, 1);
      break;

    case 4: // Underline single
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 1);
      break;

    case 7: // Reverse on
      setpenattr_bool(state, VTERM_ATTR_REVERSE, 1);
      break;

    case 10: case 11: case 12: case 13: case 14:
    case 15: case 16: case 17: case 18: case 19: // Select font
      setpenattr_int(state, VTERM_ATTR_FONT, CSI_ARG(args[argi]) - 10);
      break;

    case 21: // Underline double
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 2);
      break;

    case 22: // Bold off
      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
      break;

    case 23: // Italic and Gothic (currently unsupported) off
      setpenattr_bool(state, VTERM_ATTR_ITALIC, 0);
      break;

    case 24: // Underline off
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 0);
      break;

    case 27: // Reverse off
      setpenattr_bool(state, VTERM_ATTR_REVERSE, 0);
      break;

    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37: // Foreground colour palette
      setpenattr_col_ansi(state, VTERM_ATTR_FOREGROUND, CSI_ARG(args[argi]) - 30);
      break;

    case 38: // Foreground colour alternative palette
      argi += setpenattr_col_palette(state, VTERM_ATTR_FOREGROUND, args + argi + 1, argcount - argi - 1);
      break;

    case 39: // Foreground colour default
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

    default:
      done = 0;
      break;
    }

    if(!done)
      fprintf(stderr, "libvterm: Unhandled CSI SGR %lu\n", arg);

    while(CSI_ARG_HAS_MORE(args[argi++]));
  }
}
