#include "vterm_internal.h"

#include <stdio.h>

// Some conveniences
static void setpenattr_bool(vterm_t *vt, vterm_attr attr, int boolean)
{
  vterm_state_t *state = vt->state;
  vterm_attrvalue val = { .boolean = boolean };

  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(vt, attr, &val, &state->pen);
}

static void setpenattr_int(vterm_t *vt, vterm_attr attr, int value)
{
  vterm_state_t *state = vt->state;
  vterm_attrvalue val = { .value = value };

  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(vt, attr, &val, &state->pen);
}

static void setpenattr_col(vterm_t *vt, vterm_attr attr, int palette, int index)
{
  vterm_state_t *state = vt->state;
  vterm_attrvalue val = { .color.palette = palette, .color.index = index };

  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(vt, attr, &val, &state->pen);
}

void vterm_state_setpen(vterm_t *vt, const int args[], int argcount)
{
  // SGR - ECMA-48 8.3.117
  vterm_state_t *state = vt->state;

  int argi;

  for(argi = 0; argi < argcount; argi++) {
    int done = 0;

    if(state->callbacks &&
       state->callbacks->setpen)
      done = (*state->callbacks->setpen)(vt, args[argi], &state->pen);

    if(done)
      continue;

    // This logic is easier to do 'done' backwards; set it true, and make it
    // false again in the 'default' case
    done = 1;

    switch(args[argi]) {
    case -1:
    case 0: // Reset
      setpenattr_bool(vt, VTERM_ATTR_BOLD, 0);
      setpenattr_int(vt, VTERM_ATTR_UNDERLINE, 0);
      setpenattr_bool(vt, VTERM_ATTR_ITALIC, 0);
      setpenattr_bool(vt, VTERM_ATTR_REVERSE, 0);
      setpenattr_col(vt, VTERM_ATTR_FOREGROUND, 0, -1);
      setpenattr_col(vt, VTERM_ATTR_BACKGROUND, 0, -1);
      break;

    case 1: // Bold on
      setpenattr_bool(vt, VTERM_ATTR_BOLD, 1);
      break;

    case 3: // Italic on
      setpenattr_bool(vt, VTERM_ATTR_ITALIC, 1);
      break;

    case 4: // Underline single
      setpenattr_int(vt, VTERM_ATTR_UNDERLINE, 1);
      break;

    case 7: // Reverse on
      setpenattr_bool(vt, VTERM_ATTR_REVERSE, 1);
      break;

    case 21: // Underline double
      setpenattr_int(vt, VTERM_ATTR_UNDERLINE, 2);
      break;

    case 23: // Italic and Gothic (currently unsupported) off
      setpenattr_bool(vt, VTERM_ATTR_ITALIC, 0);
      break;

    case 24: // Underline off
      setpenattr_int(vt, VTERM_ATTR_UNDERLINE, 0);
      break;

    case 27: // Reverse off
      setpenattr_bool(vt, VTERM_ATTR_REVERSE, 0);
      break;

    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37: // Foreground colour palette
      setpenattr_col(vt, VTERM_ATTR_FOREGROUND, 0, args[argi] - 30);
      break;

    case 38: // Foreground colour alternative palette
      // Expect two more attributes
      if(argcount - argi >= 2) {
        setpenattr_col(vt, VTERM_ATTR_FOREGROUND, args[argi+1], args[argi+2]);
        argi += 2;
      }
      else {
        argi = argcount-1;
      }
      break;

    case 39: // Foreground colour default
      setpenattr_col(vt, VTERM_ATTR_FOREGROUND, 0, -1);
      break;

    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47: // Background colour palette
      setpenattr_col(vt, VTERM_ATTR_BACKGROUND, 0, args[argi] - 40);
      break;

    case 48: // Background colour alternative palette
      // Expect two more attributes
      if(argcount - argi >= 2) {
        setpenattr_col(vt, VTERM_ATTR_BACKGROUND, args[argi+1], args[argi+2]);
        argi += 2;
      }
      else {
        argi = argcount-1;
      }
      break;

    case 49: // Default background
      setpenattr_col(vt, VTERM_ATTR_BACKGROUND, 0, -1);
      break;

    default:
      done = 0;
      break;
    }

    if(!done)
      fprintf(stderr, "libvterm: Unhandled CSI SGR %d\n", args[argi]);
  }
}
