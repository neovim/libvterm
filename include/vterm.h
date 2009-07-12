#ifndef __VTERM_H__
#define __VTERM_H__

#include <stdint.h>
#include <stdlib.h>

#include "vterm_input.h"
#include "vterm_pen.h"

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

typedef enum {
  VTERM_MODE_NONE,

  VTERM_MODE_KEYPAD,

  // DEC private modes
  VTERM_MODE_DEC_CURSOR,
  VTERM_MODE_DEC_AUTOWRAP,
  VTERM_MODE_DEC_CURSORBLINK,
  VTERM_MODE_DEC_CURSORVISIBLE,
  VTERM_MODE_DEC_MOUSE,
  VTERM_MODE_DEC_ALTSCREEN,
  VTERM_MODE_DEC_SAVECURSOR,

  VTERM_MODE_MAX, // Must be last
} VTermMode;

typedef struct {
  int keypad:1;
  int cursor:1;
  int autowrap:1;
  int cursor_blink:1;
  int cursor_visible:1;
  int alt_screen:1;
  int saved_cursor:1;
} VTermModeValues;

typedef void (*VTermMouseFunc)(int x, int y, int button, int pressed, void *data);

typedef struct {
  int (*putglyph)(VTerm *vt, const uint32_t chars[], int width, VTermPos pos, void *pen);
  int (*movecursor)(VTerm *vt, VTermPos pos, VTermPos oldpos, int visible);
  int (*copyrect)(VTerm *vt, VTermRect dest, VTermRect src);
  int (*copycell)(VTerm *vt, VTermPos dest, VTermPos src);
  int (*erase)(VTerm *vt, VTermRect rect, void *pen);
  int (*initpen)(VTerm *vt, void **penstore);
  int (*setpenattr)(VTerm *vt, VTermAttr attr, VTermAttrValue *val, void **penstore);
  int (*setmode)(VTerm *vt, VTermMode mode, int val);
  int (*setmousefunc)(VTerm *vt, VTermMouseFunc func, void *data);
  int (*bell)(VTerm *vt);
} VTermStateCallbacks;

VTerm *vterm_new(int rows, int cols);
void vterm_get_size(VTerm *vt, int *rowsp, int *colsp);
void vterm_set_size(VTerm *vt, int rows, int cols);

void vterm_set_parser_callbacks(VTerm *vt, const VTermParserCallbacks *callbacks);
void vterm_set_state_callbacks(VTerm *vt, const VTermStateCallbacks *callbacks);

void vterm_state_initialise(VTerm *vt);
void vterm_state_get_cursorpos(VTerm *vt, VTermPos *cursorpos);

void vterm_input_push_str(VTerm *vt, VTermModifier state, const char *str, size_t len);
void vterm_input_push_key(VTerm *vt, VTermModifier state, VTermKey key);

void vterm_parser_set_utf8(VTerm *vt, int is_utf8);
void vterm_push_bytes(VTerm *vt, const char *bytes, size_t len);

size_t vterm_output_bufferlen(VTerm *vt);
size_t vterm_output_bufferread(VTerm *vt, char *buffer, size_t len);

#endif
