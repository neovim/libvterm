#ifndef __VTERM_H__
#define __VTERM_H__

#include <stdint.h>
#include <stdlib.h>

#include "vterm_input.h"
#include "vterm_mode.h"
#include "vterm_pen.h"

typedef struct vterm_s vterm_t;

typedef struct {
  int row;
  int col;
} vterm_position_t;

typedef struct {
  int start_row;
  int end_row;
  int start_col;
  int end_col;
} vterm_rectangle_t;

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
  int (*text)(vterm_t *vt, const int codepoints[], int npoints);
  int (*control)(vterm_t *vt, char control);
  int (*escape)(vterm_t *vt, char escape);
  int (*csi_raw)(vterm_t *vt, const char *args, size_t arglen, char command);
  int (*csi)(vterm_t *vt, const char *intermed, const long args[], int argcount, char command);
  int (*osc)(vterm_t *vt, const char *command, size_t cmdlen);
} vterm_parser_callbacks_t;

typedef void (*vterm_mousefunc)(int x, int y, int button, int pressed, void *data);

typedef struct {
  int (*putchar)(vterm_t *vt, uint32_t codepoint, int width, vterm_position_t pos, void *pen); // DEPRECATED in favour of putglyph
  int (*putglyph)(vterm_t *vt, const uint32_t chars[], int width, vterm_position_t pos, void *pen);
  int (*movecursor)(vterm_t *vt, vterm_position_t pos, vterm_position_t oldpos, int visible);
  int (*scroll)(vterm_t *vt, vterm_rectangle_t rect, int downward, int rightward);
  int (*copycell)(vterm_t *vt, vterm_position_t dest, vterm_position_t src);
  int (*erase)(vterm_t *vt, vterm_rectangle_t rect, void *pen);
  int (*setpen)(vterm_t *vt, int sgrcmd, void **penstore);
  int (*setpenattr)(vterm_t *vt, vterm_attr attr, vterm_attrvalue *val, void **penstore);
  int (*setmode)(vterm_t *vt, vterm_mode mode, int val);
  int (*setmousefunc)(vterm_t *vt, vterm_mousefunc func, void *data);
  int (*bell)(vterm_t *vt);
} vterm_state_callbacks_t;

vterm_t *vterm_new(int rows, int cols);
void vterm_get_size(vterm_t *vt, int *rowsp, int *colsp);
void vterm_set_size(vterm_t *vt, int rows, int cols);

void vterm_set_parser_callbacks(vterm_t *vt, const vterm_parser_callbacks_t *callbacks);
void vterm_set_state_callbacks(vterm_t *vt, const vterm_state_callbacks_t *callbacks);

void vterm_state_initialise(vterm_t *vt);
void vterm_state_get_cursorpos(vterm_t *vt, vterm_position_t *cursorpos);

void vterm_input_push_str(vterm_t *vt, vterm_mod state, const char *str, size_t len);
void vterm_input_push_key(vterm_t *vt, vterm_mod state, vterm_key key);

void vterm_parser_set_utf8(vterm_t *vt, int is_utf8);
void vterm_push_bytes(vterm_t *vt, const char *bytes, size_t len);

size_t vterm_output_bufferlen(vterm_t *vt);
size_t vterm_output_bufferread(vterm_t *vt, char *buffer, size_t len);

#endif
