#include "vterm_internal.h"

#include <stdio.h>

void vterm_input_push_str(VTerm *vt, VTermModifier mod, const char *str, size_t len)
{
  VTermModifier mod_noshift = mod & ~VTERM_MOD_SHIFT;

  if(mod_noshift == 0)
    // Normal text - ignore just shift
    vterm_push_output_bytes(vt, str, len);
  else if(len == 1) {
    char c = str[0];

    if(mod & VTERM_MOD_CTRL)
      c &= 0x1f;

    if(mod & VTERM_MOD_ALT) {
      vterm_push_output_sprintf(vt, "\e%c", c);
    }
    else {
      vterm_push_output_bytes(vt, &c, 1);
    }
  }
  else {
    printf("Can't cope with push_str with non-zero state %d\n", mod);
  }
}

typedef struct {
  enum {
    KEYCODE_NONE,
    KEYCODE_LITERAL,
    KEYCODE_CSI,
    KEYCODE_CSI_CURSOR,
    KEYCODE_CSINUM,
  } type;
  char literal;
  int csinum;
} keycodes_s;

keycodes_s keycodes[] = {
  { KEYCODE_NONE }, // NONE

  { KEYCODE_LITERAL, '\r' }, // ENTER
  { KEYCODE_LITERAL, '\t' }, // TAB
  { KEYCODE_LITERAL, '\b' }, // BACKSPACE
  { KEYCODE_LITERAL, '\e' }, // ESCAPE

  { KEYCODE_CSI_CURSOR, 'A' }, // UP
  { KEYCODE_CSI_CURSOR, 'B' }, // DOWN
  { KEYCODE_CSI_CURSOR, 'D' }, // LEFT
  { KEYCODE_CSI_CURSOR, 'C' }, // RIGHT

  { KEYCODE_CSINUM, '~', 2 },  // INS
  { KEYCODE_CSINUM, '~', 3 },  // DEL
  { KEYCODE_CSI_CURSOR, 'H' }, // HOME
  { KEYCODE_CSI_CURSOR, 'F' }, // END
  { KEYCODE_CSINUM, '~', 5 },  // PAGEUP
  { KEYCODE_CSINUM, '~', 6 },  // PAGEDOWN

  { KEYCODE_NONE },            // F0 - shouldn't happen
  { KEYCODE_CSI_CURSOR, 'P' }, // F1
  { KEYCODE_CSI_CURSOR, 'Q' }, // F2
  { KEYCODE_CSI_CURSOR, 'R' }, // F3
  { KEYCODE_CSI_CURSOR, 'S' }, // F4
  { KEYCODE_CSINUM, '~', 15 }, // F5
  { KEYCODE_CSINUM, '~', 17 }, // F6
  { KEYCODE_CSINUM, '~', 18 }, // F7
  { KEYCODE_CSINUM, '~', 19 }, // F8
  { KEYCODE_CSINUM, '~', 20 }, // F9
  { KEYCODE_CSINUM, '~', 21 }, // F10
  { KEYCODE_CSINUM, '~', 23 }, // F11
  { KEYCODE_CSINUM, '~', 24 }, // F12
};

void vterm_input_push_key(VTerm *vt, VTermModifier mod, VTermKey key)
{
  if(key == VTERM_KEY_NONE || key >= VTERM_KEY_MAX)
    return;

  if(key >= sizeof(keycodes)/sizeof(keycodes[0]))
    return;

  keycodes_s k = keycodes[key];

  switch(k.type) {
  case KEYCODE_NONE:
    break;

  case KEYCODE_LITERAL:
    vterm_push_output_bytes(vt, &k.literal, 1);
    break;

  case KEYCODE_CSI_CURSOR:
    if(vt->state->mode.cursor && mod == 0) {
      vterm_push_output_sprintf(vt, "\eO%c", k.literal);
      break;
    }
    /* else FALLTHROUGH */
  case KEYCODE_CSI:
    if(mod == 0)
      vterm_push_output_sprintf(vt, "\e[%c", k.literal);
    else
      vterm_push_output_sprintf(vt, "\e[1;%d%c", mod + 1, k.literal);
    break;

  case KEYCODE_CSINUM:
    if(mod == 0)
      vterm_push_output_sprintf(vt, "\e[%d%c", k.csinum, k.literal);
    else
      vterm_push_output_sprintf(vt, "\e[%d;%d%c", k.csinum, mod + 1, k.literal);
    break;
  }
}
