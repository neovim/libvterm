#include "vterm_internal.h"

#include <stdio.h>

/* The following functions copied and adapted from libtermkey
 *
 * http://www.leonerd.org.uk/code/libtermkey/
 */
static inline unsigned int utf8_seqlen(long codepoint)
{
  if(codepoint < 0x0000080) return 1;
  if(codepoint < 0x0000800) return 2;
  if(codepoint < 0x0010000) return 3;
  if(codepoint < 0x0200000) return 4;
  if(codepoint < 0x4000000) return 5;
  return 6;
}

static int fill_utf8(long codepoint, char *str)
{
  int nbytes = utf8_seqlen(codepoint);

  str[nbytes] = 0;

  // This is easier done backwards
  int b = nbytes;
  while(b > 1) {
    b--;
    str[b] = 0x80 | (codepoint & 0x3f);
    codepoint >>= 6;
  }

  switch(nbytes) {
    case 1: str[0] =        (codepoint & 0x7f); break;
    case 2: str[0] = 0xc0 | (codepoint & 0x1f); break;
    case 3: str[0] = 0xe0 | (codepoint & 0x0f); break;
    case 4: str[0] = 0xf0 | (codepoint & 0x07); break;
    case 5: str[0] = 0xf8 | (codepoint & 0x03); break;
    case 6: str[0] = 0xfc | (codepoint & 0x01); break;
  }

  return nbytes;
}
/* end copy */

void vterm_input_push_char(VTerm *vt, VTermModifier mod, uint32_t c)
{
  /* The shift modifier is never important for Unicode characters
   * apart from Space
   */
  if(c != ' ')
    mod &= ~VTERM_MOD_SHIFT;

  if(mod == 0) {
    // Normal text - ignore just shift
    char str[6];
    int seqlen = fill_utf8(c, str);
    vterm_push_output_bytes(vt, str, seqlen);
    return;
  }

  int needs_CSIu;
  switch(c) {
    /* Special Ctrl- letters that can't be represented elsewise */
    case 'h': case 'i': case 'j': case 'm': case '[':
      needs_CSIu = 1;
      break;
    /* All other characters needs CSIu except for letters a-z */
    default:
      needs_CSIu = (c < 'a' || c > 'z');
  }

  /* ALT we can just prefix with ESC; anything else requires CSI u */
  if(needs_CSIu && (mod & ~VTERM_MOD_ALT)) {
    vterm_push_output_sprintf(vt, "\e[%d;%du", c, mod+1);
    return;
  }

  if(mod & VTERM_MOD_CTRL)
    c &= 0x1f;

  vterm_push_output_sprintf(vt, "%s%c", mod & VTERM_MOD_ALT ? "\e" : "", c);
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
    if(mod & (VTERM_MOD_SHIFT|VTERM_MOD_CTRL))
      vterm_push_output_sprintf(vt, "\e[%d;%du", k.literal, mod+1);
    else
      vterm_push_output_sprintf(vt, mod & VTERM_MOD_ALT ? "\e%c" : "%c", k.literal);
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
