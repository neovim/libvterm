#include "ecma48_internal.h"

#include <stdio.h>

// Some conveniences
static void setpenattr_bool(ecma48_t *e48, ecma48_attr attr, int boolean)
{
  ecma48_state_t *state = e48->state;
  ecma48_attrvalue val = { .boolean = boolean };

  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(e48, attr, &val, &state->pen);
}

static void setpenattr_int(ecma48_t *e48, ecma48_attr attr, int value)
{
  ecma48_state_t *state = e48->state;
  ecma48_attrvalue val = { .value = value };

  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(e48, attr, &val, &state->pen);
}

static void setpenattr_col(ecma48_t *e48, ecma48_attr attr, int palette, int index)
{
  ecma48_state_t *state = e48->state;
  ecma48_attrvalue val = { .color.palette = palette, .color.index = index };

  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(e48, attr, &val, &state->pen);
}

void ecma48_state_setpen(ecma48_t *e48, const int args[], int argcount)
{
  // SGR - ECMA-48 8.3.117
  ecma48_state_t *state = e48->state;

  int argi;

  for(argi = 0; argi < argcount; argi++) {
    int done = 0;

    if(state->callbacks &&
       state->callbacks->setpen)
      done = (*state->callbacks->setpen)(e48, args[argi], &state->pen);

    if(done)
      continue;

    // This logic is easier to do 'done' backwards; set it true, and make it
    // false again in the 'default' case
    done = 1;

    switch(args[argi]) {
    case -1:
    case 0: // Reset
      setpenattr_bool(e48, ECMA48_ATTR_BOLD, 0);
      setpenattr_int(e48, ECMA48_ATTR_UNDERLINE, 0);
      setpenattr_bool(e48, ECMA48_ATTR_REVERSE, 0);
      setpenattr_col(e48, ECMA48_ATTR_FOREGROUND, 0, -1);
      setpenattr_col(e48, ECMA48_ATTR_BACKGROUND, 0, -1);
      break;

    case 1: // Bold on
      setpenattr_bool(e48, ECMA48_ATTR_BOLD, 1);
      break;

    case 4: // Underline single
      setpenattr_int(e48, ECMA48_ATTR_UNDERLINE, 1);
      break;

    case 7: // Reverse on
      setpenattr_bool(e48, ECMA48_ATTR_REVERSE, 1);
      break;

    case 21: // Underline double
      setpenattr_int(e48, ECMA48_ATTR_UNDERLINE, 2);
      break;

    case 24: // Underline off
      setpenattr_int(e48, ECMA48_ATTR_UNDERLINE, 0);
      break;

    case 27: // Reverse off
      setpenattr_bool(e48, ECMA48_ATTR_REVERSE, 0);
      break;

    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37: // Foreground colour palette
      setpenattr_col(e48, ECMA48_ATTR_FOREGROUND, 0, args[argi] - 30);
      break;

    case 38: // Foreground colour alternative palette
      // Expect two more attributes
      if(argcount - argi >= 2) {
        setpenattr_col(e48, ECMA48_ATTR_FOREGROUND, args[argi+1], args[argi+2]);
        argi += 2;
      }
      else {
        argi = argcount-1;
      }
      break;

    case 39: // Foreground colour default
      setpenattr_col(e48, ECMA48_ATTR_FOREGROUND, 0, -1);
      break;

    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47: // Background colour palette
      setpenattr_col(e48, ECMA48_ATTR_BACKGROUND, 0, args[argi] - 40);
      break;

    case 48: // Background colour alternative palette
      // Expect two more attributes
      if(argcount - argi >= 2) {
        setpenattr_col(e48, ECMA48_ATTR_BACKGROUND, args[argi+1], args[argi+2]);
        argi += 2;
      }
      else {
        argi = argcount-1;
      }
      break;

    case 49: // Default background
      setpenattr_col(e48, ECMA48_ATTR_BACKGROUND, 0, -1);
      break;

    default:
      done = 0;
      break;
    }

    if(!done)
      fprintf(stderr, "libecma48: Unhandled CSI SGR %d\n", args[argi]);
  }
}
