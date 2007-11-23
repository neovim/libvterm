#ifndef __ECMA48_H__
#define __ECMA48_H__

#include <stdint.h>
#include <stdlib.h>

typedef struct ecma48_state ecma48_state_t;

typedef struct {
  void (*text)(ecma48_state_t *state, char *s, size_t len);
  void (*control)(ecma48_state_t *state, char control);
  void (*escape)(ecma48_state_t *state, char escape);
  void (*csi)(ecma48_state_t *state, char *args);
} ecma48_callbacks_t;

ecma48_state_t *ecma48_state_new(void);
void ecma48_state_set_callbacks(ecma48_state_t *state, ecma48_callbacks_t *callbacks);

void ecma48_state_push_bytes(ecma48_state_t *state, char *bytes, size_t len);

#endif
