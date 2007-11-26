#ifndef __ECMA48_H__
#define __ECMA48_H__

#include <stdint.h>
#include <stdlib.h>

typedef struct ecma48_state ecma48_state_t;

typedef struct {
  int (*text)(ecma48_state_t *state, char *s, size_t len);
  int (*control)(ecma48_state_t *state, char control);
  int (*escape)(ecma48_state_t *state, char escape);
  int (*csi)(ecma48_state_t *state, char *args);
} ecma48_parser_callbacks_t;

ecma48_state_t *ecma48_state_new(void);
void ecma48_state_set_parser_callbacks(ecma48_state_t *state, ecma48_parser_callbacks_t *callbacks);

void ecma48_state_push_bytes(ecma48_state_t *state, char *bytes, size_t len);

#endif
