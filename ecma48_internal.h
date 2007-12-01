#ifndef __ECMA48_INTERNAL_H__
#define __ECMA48_INTERNAL_H__

#include "ecma48.h"

#include <glib.h>

typedef struct ecma48_state_s ecma48_state_t;

struct ecma48_s
{
  int rows;
  int cols;

  int is_utf8;

  ecma48_parser_callbacks_t *parser_callbacks;

  GString *inbuffer;
  GString *outbuffer;
  ecma48_state_t *state;

  struct {
    int keypad:1;
    int cursor:1;
    int cursor_visible:1;
  } mode;
};

size_t ecma48_parser_interpret_bytes(ecma48_t *e48, char *bytes, size_t len);

void ecma48_push_output_bytes(ecma48_t *e48, char *bytes, size_t len);
void ecma48_push_output_vsprintf(ecma48_t *e48, char *format, va_list args);
void ecma48_push_output_sprintf(ecma48_t *e48, char *format, ...);

int ecma48_state_on_text(ecma48_t *e48, int codepoints[], int npoints);
int ecma48_state_on_control(ecma48_t *e48, unsigned char control);
int ecma48_state_on_escape(ecma48_t *e48, char escape);
int ecma48_state_on_csi(ecma48_t *e48, char *intermed, int *args, int argcount, char command);

#endif
