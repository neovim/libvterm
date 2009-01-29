#include "vterm_internal.h"

#include <stdio.h>

void vterm_input_push_str(vterm_t *vt, vterm_mod state, const char *str, size_t len)
{
  vterm_mod state_noshift = state & ~VTERM_MOD_SHIFT;

  if(state_noshift == 0)
    // Normal text - ignore just shift
    vterm_push_output_bytes(vt, str, len);
  else if(len == 1) {
    char c = str[0];

    if(state & VTERM_MOD_CTRL)
      c &= 0x1f;

    if(state & VTERM_MOD_ALT) {
      vterm_push_output_sprintf(vt, "\e%c", c);
    }
    else {
      vterm_push_output_bytes(vt, &c, 1);
    }
  }
  else {
    printf("Can't cope with push_str with non-zero state %d\n", state);
  }
}

typedef struct {
  enum {
    KEYCODE_LITERAL,
    KEYCODE_CSI,
    KEYCODE_CSI_CURSOR,
    KEYCODE_CSINUM,
  } type;
  char literal;
  int csinum;
} keycodes_s;

keycodes_s keycodes[] = {
  { 0 }, // NONE

  { KEYCODE_LITERAL, '\r' },
  { KEYCODE_LITERAL, '\t' },
  { KEYCODE_LITERAL, '\b' },
  { KEYCODE_LITERAL, '\e' },

  { KEYCODE_CSI_CURSOR, 'A' }, // UP
  { KEYCODE_CSI_CURSOR, 'B' }, // DOWN
  { KEYCODE_CSI_CURSOR, 'D' }, // LEFT
  { KEYCODE_CSI_CURSOR, 'C' }, // RIGHT

  { KEYCODE_CSINUM, '~', 2 }, // INS
  { KEYCODE_CSINUM, '~', 3 }, // DEL
  { KEYCODE_CSI_CURSOR, 'H' }, // HOME
  { KEYCODE_CSI_CURSOR, 'F' }, // END
  { KEYCODE_CSINUM, '~', 5 }, // PAGEUP
  { KEYCODE_CSINUM, '~', 6 }, // PAGEDOWN

  { KEYCODE_CSI_CURSOR, '\0' }, // F0 - shouldn't happen
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

void vterm_input_push_key(vterm_t *vt, vterm_mod state, vterm_key key)
{
  if(key == VTERM_KEY_NONE || key >= VTERM_KEY_MAX)
    return;

  if(key >= sizeof(keycodes)/sizeof(keycodes[0]))
    return;

  keycodes_s k = keycodes[key];

  switch(k.type) {
  case KEYCODE_LITERAL:
    vterm_push_output_bytes(vt, &k.literal, 1);
    break;

  case KEYCODE_CSI_CURSOR:
    if(vt->mode.cursor && state == 0) {
      vterm_push_output_sprintf(vt, "\eO%c", k.literal);
      break;
    }
    /* else FALLTHROUGH */
  case KEYCODE_CSI:
    if(state == 0)
      vterm_push_output_sprintf(vt, "\e[%c", k.literal);
    else
      vterm_push_output_sprintf(vt, "\e[1;%d%c", state + 1, k.literal);
    break;

  case KEYCODE_CSINUM:
    if(state == 0)
      vterm_push_output_sprintf(vt, "\e[%d%c", k.csinum, k.literal);
    else
      vterm_push_output_sprintf(vt, "\e[%d;%d%c", k.csinum, state + 1, k.literal);
    break;
  }
}
