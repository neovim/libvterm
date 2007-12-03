#ifndef __ECMA48_H__
#define __ECMA48_H__

#include <stdint.h>
#include <stdlib.h>

#include "ecma48_input.h"
#include "ecma48_pen.h"

typedef struct ecma48_s ecma48_t;

typedef struct {
  int row;
  int col;
} ecma48_position_t;

typedef struct {
  int start_row;
  int end_row;
  int start_col;
  int end_col;
} ecma48_rectangle_t;

typedef struct {
  int (*text)(ecma48_t *e48, int codepoints[], int npoints);
  int (*control)(ecma48_t *e48, char control);
  int (*escape)(ecma48_t *e48, char escape);
  int (*csi_raw)(ecma48_t *e48, char *args, size_t arglen, char command);
  int (*csi)(ecma48_t *e48, char *intermed, int *args, int argcount, char command);
} ecma48_parser_callbacks_t;

typedef struct {
  int (*putchar)(ecma48_t *e48, uint32_t codepoint, ecma48_position_t pos, void *pen);
  int (*movecursor)(ecma48_t *e48, ecma48_position_t pos, ecma48_position_t oldpos, int visible);
  int (*scroll)(ecma48_t *e48, ecma48_rectangle_t rect, int downward, int rightward);
  int (*copycell)(ecma48_t *e48, ecma48_position_t dest, ecma48_position_t src);
  int (*erase)(ecma48_t *e48, ecma48_rectangle_t rect, void *pen);
  int (*setpen)(ecma48_t *e48, int sgrcmd, void **penstore);
  int (*setpenattr)(ecma48_t *e48, ecma48_attr attr, ecma48_attrvalue *val, void **penstore);
} ecma48_state_callbacks_t;

ecma48_t *ecma48_new(void);
void ecma48_set_size(ecma48_t *e48, int rows, int cols);

void ecma48_set_parser_callbacks(ecma48_t *e48, ecma48_parser_callbacks_t *callbacks);
void ecma48_set_state_callbacks(ecma48_t *e48, ecma48_state_callbacks_t *callbacks);

void ecma48_state_initialise(ecma48_t *e48);
void ecma48_state_get_cursorpos(ecma48_t *e48, ecma48_position_t *cursorpos);

void ecma48_input_push_str(ecma48_t *e48, ecma48_mod state, char *str, size_t len);
void ecma48_input_push_key(ecma48_t *e48, ecma48_mod state, ecma48_key key);

void ecma48_parser_set_utf8(ecma48_t *e48, int is_utf8);
void ecma48_push_bytes(ecma48_t *e48, char *bytes, size_t len);

size_t ecma48_output_bufferlen(ecma48_t *e48);
size_t ecma48_output_bufferread(ecma48_t *e48, char *buffer, size_t len);

#endif
