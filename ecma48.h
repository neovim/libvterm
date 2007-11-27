#ifndef __ECMA48_H__
#define __ECMA48_H__

#include <stdint.h>
#include <stdlib.h>

typedef struct ecma48_s ecma48_t;

typedef struct {
  int row;
  int col;
} ecma48_position_t;

typedef struct {
  int (*text)(ecma48_t *state, char *s, size_t len);
  int (*control)(ecma48_t *state, char control);
  int (*escape)(ecma48_t *state, char escape);
  int (*csi)(ecma48_t *state, char *args);
} ecma48_parser_callbacks_t;

typedef struct {
  int (*putchar)(ecma48_t *e48, uint32_t codepoint, ecma48_position_t pos);
  int (*movecursor)(ecma48_t *e48, ecma48_position_t pos, ecma48_position_t oldpos);
} ecma48_state_callbacks_t;

ecma48_t *ecma48_new(void);
void ecma48_set_size(ecma48_t *e48, int rows, int cols);

void ecma48_set_parser_callbacks(ecma48_t *e48, ecma48_parser_callbacks_t *callbacks);
void ecma48_set_state_callbacks(ecma48_t *e48, ecma48_state_callbacks_t *callbacks);

void ecma48_push_bytes(ecma48_t *e48, char *bytes, size_t len);

#endif
