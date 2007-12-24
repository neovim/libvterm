#ifndef __VTERM_MODE_H__
#define __VTERM_MODE_H__

typedef enum {
  ECMA48_MODE_NONE,

  ECMA48_MODE_KEYPAD,

  // DEC private modes
  ECMA48_MODE_DEC_CURSOR,
  ECMA48_MODE_DEC_CURSORBLINK,
  ECMA48_MODE_DEC_CURSORVISIBLE,
  ECMA48_MODE_DEC_MOUSE,
  ECMA48_MODE_DEC_ALTSCREEN,
  ECMA48_MODE_DEC_SAVECURSOR,

  ECMA48_MODE_MAX, // Must be last
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
