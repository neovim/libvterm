#ifndef __VTERM_H__
#define __VTERM_H__

#include <stdint.h>
#include <stdlib.h>

#include "vterm_input.h"

typedef struct _VTerm VTerm;

typedef struct {
  int row;
  int col;
} VTermPos;

typedef struct {
  int start_row;
  int end_row;
  int start_col;
  int end_col;
} VTermRect;

/* Flag to indicate non-final subparameters in a single CSI parameter.
 * Consider
 *   CSI 1;2:3:4;5a
 * 1 4 and 5 are final.
 * 2 and 3 are non-final and will have this bit set
 *
 * Don't confuse this with the final byte of the CSI escape; 'a' in this case.
 */
#define CSI_ARG_FLAG_MORE (1<<31)
#define CSI_ARG_MASK      (~(1<<31))

#define CSI_ARG_HAS_MORE(a) ((a) & CSI_ARG_FLAG_MORE)
#define CSI_ARG(a)          ((a) & CSI_ARG_MASK)

/* Can't use -1 to indicate a missing argument; use this instead */
#define CSI_ARG_MISSING ((1UL<<31)-1)

#define CSI_ARG_IS_MISSING(a) (CSI_ARG(a) == CSI_ARG_MISSING)
#define CSI_ARG_OR(a,def)     (CSI_ARG(a) == CSI_ARG_MISSING ? (def) : CSI_ARG(a))

typedef struct {
  int (*text)(VTerm *vt, const int codepoints[], int npoints);
  int (*control)(VTerm *vt, unsigned char control);
  int (*escape)(VTerm *vt, const char *bytes, size_t len);
  int (*csi)(VTerm *vt, const char *intermed, const long args[], int argcount, char command);
  int (*osc)(VTerm *vt, const char *command, size_t cmdlen);
} VTermParserCallbacks;

typedef struct {
  uint8_t red, green, blue;
} VTermColor;

typedef union {
  int boolean;
  int number;
  char *string;
  VTermColor color;
} VTermValue;

typedef enum {
  VTERM_ATTR_NONE,
  VTERM_ATTR_BOLD,       // bool:   1, 22
  VTERM_ATTR_UNDERLINE,  // number: 4, 21, 24
  VTERM_ATTR_ITALIC,     // bool:   3, 23
  VTERM_ATTR_REVERSE,    // bool:   7, 27
  VTERM_ATTR_FONT,       // number: 10-19
  VTERM_ATTR_FOREGROUND, // color:  30-39
  VTERM_ATTR_BACKGROUND, // color:  40-49
} VTermAttr;

typedef enum {
  VTERM_PROP_NONE,
  VTERM_PROP_CURSORVISIBLE, // bool
  VTERM_PROP_CURSORBLINK,   // bool
  VTERM_PROP_ALTSCREEN,     // bool
  VTERM_PROP_TITLE,         // string
  VTERM_PROP_ICONNAME,      // string
} VTermProp;

typedef void (*VTermMouseFunc)(int x, int y, int button, int pressed, void *data);

typedef struct {
  int (*putglyph)(const uint32_t chars[], int width, VTermPos pos, void *user);
  int (*movecursor)(VTermPos pos, VTermPos oldpos, int visible, void *user);
  int (*copyrect)(VTermRect dest, VTermRect src, void *user);
  int (*copycell)(VTermPos dest, VTermPos src, void *user);
  int (*erase)(VTermRect rect, void *user);
  int (*initpen)(void *user);
  int (*setpenattr)(VTermAttr attr, VTermValue *val, void *user);
  int (*settermprop)(VTermProp prop, VTermValue *val, void *user);
  int (*setmousefunc)(VTermMouseFunc func, void *data, void *user);
  int (*bell)(void *user);
} VTermStateCallbacks;

VTerm *vterm_new(int rows, int cols);
void vterm_get_size(VTerm *vt, int *rowsp, int *colsp);
void vterm_set_size(VTerm *vt, int rows, int cols);

void vterm_set_parser_callbacks(VTerm *vt, const VTermParserCallbacks *callbacks);
void vterm_set_state_callbacks(VTerm *vt, const VTermStateCallbacks *callbacks, void *user);

void vterm_state_initialise(VTerm *vt);
void vterm_state_get_cursorpos(VTerm *vt, VTermPos *cursorpos);

void vterm_input_push_str(VTerm *vt, VTermModifier state, const char *str, size_t len);
void vterm_input_push_key(VTerm *vt, VTermModifier state, VTermKey key);

void vterm_parser_set_utf8(VTerm *vt, int is_utf8);
void vterm_push_bytes(VTerm *vt, const char *bytes, size_t len);

size_t vterm_output_bufferlen(VTerm *vt);
size_t vterm_output_bufferread(VTerm *vt, char *buffer, size_t len);

#endif
