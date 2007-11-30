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

void ecma48_input_push_key(ecma48_t *e48, ecma48_mod_e state, ecma48_key_e key)
{
  switch(key) {
  case ECMA48_KEY_ENTER:
    ecma48_push_output_bytes(e48, "\n", 1); break;
  case ECMA48_KEY_TAB:
    ecma48_push_output_bytes(e48, "\t", 1); break;
  case ECMA48_KEY_BACKSPACE:
    ecma48_push_output_bytes(e48, "\b", 1); break;
  case ECMA48_KEY_ESCAPE:
    ecma48_push_output_bytes(e48, "\e", 1); break;
  default:
    printf("Can't cope with pushkey %d\n", key);
  }
}
