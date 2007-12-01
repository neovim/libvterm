#include "ecma48_internal.h"

#include <stdio.h>

void ecma48_input_push_str(ecma48_t *e48, ecma48_mod_e state, char *str, size_t len)
{
  ecma48_mod_e state_noshift = state & ~ECMA48_MOD_SHIFT;

  if(state_noshift == 0)
    // Normal text - ignore just shift
    ecma48_push_output_bytes(e48, str, len);
  else if (state_noshift == ECMA48_MOD_CTRL && len == 1) {
    // Ctrl + normal symbol
    char c = str[0] & 0x1f;
    ecma48_push_output_bytes(e48, &c, 1);
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

void ecma48_input_push_key(ecma48_t *e48, ecma48_mod_e state, ecma48_key_e key)
{
  if(key == ECMA48_KEY_NONE || key >= ECMA48_KEY_MAX)
    return;

  keycodes_s k = keycodes[key];

  switch(k.type) {
  case KEYCODE_LITERAL:
    ecma48_push_output_bytes(e48, &k.literal, 1);
    break;

  case KEYCODE_CSI_CURSOR:
    if(e48->mode.cursor) {
      ecma48_push_output_sprintf(e48, "\eO%c", k.literal);
      break;
    }
    /* else FALLTHROUGH */
  case KEYCODE_CSI:
    if(state == 0)
      ecma48_push_output_sprintf(e48, "\e[%c", k.literal);
    else
      ecma48_push_output_sprintf(e48, "\e[1;%d%c", state + 1, k.literal);
    break;

  case KEYCODE_CSINUM:
    if(state == 0)
      ecma48_push_output_sprintf(e48, "\e[%d%c", k.csinum, k.literal);
    else
      ecma48_push_output_sprintf(e48, "\e[%d;%d%c", k.csinum, state + 1, k.literal);
    break;
  }
}
