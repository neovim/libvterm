#ifndef __VTERM_H__
#define __VTERM_H__

#include <stdint.h>
#include <stdlib.h>

#include "vterm_input.h"

typedef struct _VTerm VTerm;
typedef struct _VTermState VTermState;
typedef struct _VTermScreen VTermScreen;

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
  int (*text)(const char *bytes, size_t len, void *user);
  int (*control)(unsigned char control, void *user);
  int (*escape)(const char *bytes, size_t len, void *user);
  int (*csi)(const char *intermed, const long args[], int argcount, char command, void *user);
  int (*osc)(const char *command, size_t cmdlen, void *user);
  int (*resize)(int rows, int cols, void *user);
} VTermParserCallbacks;

typedef struct {
  uint8_t red, green, blue;
} VTermColor;

typedef enum {
  /* VTERM_VALUETYPE_NONE = 0 */
  VTERM_VALUETYPE_BOOL = 1,
  VTERM_VALUETYPE_INT,
  VTERM_VALUETYPE_STRING,
  VTERM_VALUETYPE_COLOR,
} VTermValueType;

typedef union {
  int boolean;
  int number;
  char *string;
  VTermColor color;
} VTermValue;

typedef enum {
  /* VTERM_ATTR_NONE = 0 */
  VTERM_ATTR_BOLD = 1,   // bool:   1, 22
  VTERM_ATTR_UNDERLINE,  // number: 4, 21, 24
  VTERM_ATTR_ITALIC,     // bool:   3, 23
  VTERM_ATTR_BLINK,      // bool:   5, 25
  VTERM_ATTR_REVERSE,    // bool:   7, 27
  VTERM_ATTR_FONT,       // number: 10-19
  VTERM_ATTR_FOREGROUND, // color:  30-39 90-97
  VTERM_ATTR_BACKGROUND, // color:  40-49 100-107
} VTermAttr;

typedef enum {
  /* VTERM_PROP_NONE = 0 */
  VTERM_PROP_CURSORVISIBLE = 1, // bool
  VTERM_PROP_CURSORBLINK,       // bool
  VTERM_PROP_ALTSCREEN,         // bool
  VTERM_PROP_TITLE,             // string
  VTERM_PROP_ICONNAME,          // string
} VTermProp;

typedef void (*VTermMouseFunc)(int x, int y, int button, int pressed, void *data);

typedef struct {
  int (*putglyph)(const uint32_t chars[], int width, VTermPos pos, void *user);
  int (*movecursor)(VTermPos pos, VTermPos oldpos, int visible, void *user);
  int (*moverect)(VTermRect dest, VTermRect src, void *user);
  int (*erase)(VTermRect rect, void *user);
  int (*initpen)(void *user);
  int (*setpenattr)(VTermAttr attr, VTermValue *val, void *user);
  int (*settermprop)(VTermProp prop, VTermValue *val, void *user);
  int (*setmousefunc)(VTermMouseFunc func, void *data, void *user);
  int (*bell)(void *user);
  int (*resize)(int rows, int cols, void *user);
} VTermStateCallbacks;

typedef struct {
  int (*damage)(VTermRect rect, void *user);
  int (*movecursor)(VTermPos pos, VTermPos oldpos, int visible, void *user);
  int (*settermprop)(VTermProp prop, VTermValue *val, void *user);
  int (*setmousefunc)(VTermMouseFunc func, void *data, void *user);
  int (*bell)(void *user);
  int (*resize)(int rows, int cols, void *user);
} VTermScreenCallbacks;

VTerm *vterm_new(int rows, int cols);
void vterm_get_size(VTerm *vt, int *rowsp, int *colsp);
void vterm_set_size(VTerm *vt, int rows, int cols);

void vterm_set_parser_callbacks(VTerm *vt, const VTermParserCallbacks *callbacks, void *user);

VTermState *vterm_obtain_state(VTerm *vt);

void vterm_state_reset(VTermState *state);
void vterm_state_set_callbacks(VTermState *state, const VTermStateCallbacks *callbacks, void *user);
void vterm_state_get_cursorpos(VTermState *state, VTermPos *cursorpos);
int  vterm_state_get_penattr(VTermState *state, VTermAttr attr, VTermValue *val);

VTermValueType vterm_get_attr_type(VTermAttr attr);
VTermValueType vterm_get_prop_type(VTermProp prop);

VTermScreen *vterm_initialise_screen(VTerm *vt);

void vterm_screen_set_callbacks(VTermScreen *screen, const VTermScreenCallbacks *callbacks, void *user);

void   vterm_screen_reset(VTermScreen *screen);
size_t vterm_screen_get_chars(VTermScreen *screen, uint32_t *chars, size_t len, const VTermRect rect);

void vterm_input_push_str(VTerm *vt, VTermModifier state, const char *str, size_t len);
void vterm_input_push_key(VTerm *vt, VTermModifier state, VTermKey key);

void vterm_parser_set_utf8(VTerm *vt, int is_utf8);
void vterm_push_bytes(VTerm *vt, const char *bytes, size_t len);

size_t vterm_output_bufferlen(VTerm *vt);
size_t vterm_output_bufferread(VTerm *vt, char *buffer, size_t len);

void vterm_copy_cells(VTermRect dest,
                      VTermRect src,
                      void (*copycell)(VTermPos dest, VTermPos src, void *user),
                      void *user);

#endif
