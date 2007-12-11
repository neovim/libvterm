#ifndef __ECMA48_PEN_H__
#define __ECMA48_PEN_H__

typedef union {
  int boolean;
  int value;
  struct {
    int palette;
    int index;
  } color;
} ecma48_attrvalue;

typedef enum {
  ECMA48_ATTR_NONE,
  ECMA48_ATTR_BOLD,       // bool:  1
  ECMA48_ATTR_UNDERLINE,  // count: 4, 21, 24
  ECMA48_ATTR_REVERSE,    // bool:  7, 27
  ECMA48_ATTR_FOREGROUND, // color: 30-39
  ECMA48_ATTR_BACKGROUND, // color: 40-49
} ecma48_attr;

#endif
