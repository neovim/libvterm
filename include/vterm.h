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

typedef struct {
  int (*text)(vterm_t *vt, const int codepoints[], int npoints);
  int (*control)(vterm_t *vt, char control);
  int (*escape)(vterm_t *vt, char escape);
  int (*csi_raw)(vterm_t *vt, const char *args, size_t arglen, char command);
  int (*csi)(vterm_t *vt, const char *intermed, const int args[], int argcount, char command);
  int (*osc)(vterm_t *vt, const char *command, size_t cmdlen);
} vterm_parser_callbacks_t;

typedef void (*vterm_mousefunc)(int x, int y, int button, int pressed, void *data);

typedef struct {
  int (*putchar)(vterm_t *vt, uint32_t codepoint, vterm_position_t pos, void *pen);
  int (*movecursor)(vterm_t *vt, vterm_position_t pos, vterm_position_t oldpos, int visible);
  int (*scroll)(vterm_t *vt, vterm_rectangle_t rect, int downward, int rightward);
  int (*copycell)(vterm_t *vt, vterm_position_t dest, vterm_position_t src);
  int (*erase)(vterm_t *vt, vterm_rectangle_t rect, void *pen);
  int (*setpen)(vterm_t *vt, int sgrcmd, void **penstore);
  int (*setpenattr)(vterm_t *vt, vterm_attr attr, vterm_attrvalue *val, void **penstore);
  int (*setmode)(vterm_t *vt, vterm_mode mode, int val);
  int (*setmousefunc)(vterm_t *vt, vterm_mousefunc func, void *data);
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
