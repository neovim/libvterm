#ifndef __VTERM_PEN_H__
#define __VTERM_PEN_H__

typedef union {
  int boolean;
  int value;
  struct {
    int palette;
    int index;
  } color;
} vterm_attrvalue;

typedef enum {
  VTERM_ATTR_NONE,
  VTERM_ATTR_BOLD,       // bool:  1
  VTERM_ATTR_UNDERLINE,  // count: 4, 21, 24
  VTERM_ATTR_REVERSE,    // bool:  7, 27
  VTERM_ATTR_FOREGROUND, // color: 30-39
  VTERM_ATTR_BACKGROUND, // color: 40-49
} vterm_attr;

#endif
