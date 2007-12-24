#include "vterm_internal.h"

#include <stdio.h>

void vterm_input_push_str(vterm_t *e48, vterm_mod state, const char *str, size_t len)
{
  vterm_mod state_noshift = state & ~VTERM_MOD_SHIFT;

  if(state_noshift == 0)
    // Normal text - ignore just shift
    vterm_push_output_bytes(e48, str, len);
  else if(len == 1) {
    char c = str[0];

    if(state & VTERM_MOD_CTRL)
      c &= 0x1f;

    if(state & VTERM_MOD_ALT) {
      vterm_push_output_sprintf(e48, "\e%c", c);
    }
    else {
      vterm_push_output_bytes(e48, &c, 1);
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

  { KEYCODE_LITERAL, '\n' },
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
};

void vterm_input_push_key(vterm_t *e48, vterm_mod state, vterm_key key)
{
  if(key == VTERM_KEY_NONE || key >= VTERM_KEY_MAX)
    return;

  keycodes_s k = keycodes[key];

  switch(k.type) {
  case KEYCODE_LITERAL:
    vterm_push_output_bytes(e48, &k.literal, 1);
    break;

  case KEYCODE_CSI_CURSOR:
    if(e48->mode.cursor) {
      vterm_push_output_sprintf(e48, "\eO%c", k.literal);
      break;
    }
    /* else FALLTHROUGH */
  case KEYCODE_CSI:
    if(state == 0)
      vterm_push_output_sprintf(e48, "\e[%c", k.literal);
    else
      vterm_push_output_sprintf(e48, "\e[1;%d%c", state + 1, k.literal);
    break;

  case KEYCODE_CSINUM:
    if(state == 0)
      vterm_push_output_sprintf(e48, "\e[%d%c", k.csinum, k.literal);
    else
      vterm_push_output_sprintf(e48, "\e[%d;%d%c", k.csinum, state + 1, k.literal);
    break;
  }
}
