#ifndef __VTERM_MODE_H__
#define __VTERM_MODE_H__

typedef enum {
  VTERM_MODE_NONE,

  VTERM_MODE_KEYPAD,

  // DEC private modes
  VTERM_MODE_DEC_CURSOR,
  VTERM_MODE_DEC_CURSORBLINK,
  VTERM_MODE_DEC_CURSORVISIBLE,
  VTERM_MODE_DEC_MOUSE,
  VTERM_MODE_DEC_ALTSCREEN,
  VTERM_MODE_DEC_SAVECURSOR,

  VTERM_MODE_MAX, // Must be last
} ecma48_mode;

typedef struct {
  int keypad:1;
  int cursor:1;
  int cursor_blink:1;
  int cursor_visible:1;
  int alt_screen:1;
  int saved_cursor:1;
} ecma48_modevalues;

#endif
