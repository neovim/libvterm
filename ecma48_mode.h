#ifndef __ECMA48_MODE_H__
#define __ECMA48_MODE_H__

typedef enum {
  ECMA48_MODE_NONE,

  ECMA48_MODE_KEYPAD,

  // DEC private modes
  ECMA48_MODE_DEC_CURSOR,
  ECMA48_MODE_DEC_CURSORVISIBLE,
} ecma48_mode;

typedef struct {
  int keypad:1;
  int cursor:1;
  int cursor_visible:1;
} ecma48_modevalues;

#endif
