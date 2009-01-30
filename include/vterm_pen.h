#ifndef __VTERM_PEN_H__
#define __VTERM_PEN_H__

typedef struct {
  uint8_t red, green, blue;
} vterm_attrvalue_color;

typedef union {
  int boolean;
  int number;
  vterm_attrvalue_color color;
} vterm_attrvalue;

typedef enum {
  VTERM_ATTR_NONE,
  VTERM_ATTR_BOLD,       // bool:  1
  VTERM_ATTR_UNDERLINE,  // count: 4, 21, 24
  VTERM_ATTR_ITALIC,     // bool:  3, 23
  VTERM_ATTR_REVERSE,    // bool:  7, 27
  VTERM_ATTR_FOREGROUND, // color: 30-39
  VTERM_ATTR_BACKGROUND, // color: 40-49
} vterm_attr;

#endif
