#ifndef __ECMA48_INTERNAL_H__
#define __ECMA48_INTERNAL_H__

#include "ecma48.h"

#include <glib.h>

typedef struct ecma48_state_s
{
  ecma48_state_callbacks_t *callbacks;

  /* Current cursor position */
  ecma48_position_t pos;
  /* Saved cursor position under DEC mode 1048/1049 */
  ecma48_position_t saved_pos;

  int scrollregion_start;
  int scrollregion_end;

  /* Current pen - entirely managed by user code */
  void *pen;

  /* Mouse state */
  int mouse_col, mouse_row;
  int mouse_buttons;
} ecma48_state_t;

struct ecma48_s
{
  int rows;
  int cols;

  int is_utf8;

  ecma48_parser_callbacks_t *parser_callbacks;

  GString *inbuffer;
  GString *outbuffer;
  ecma48_state_t *state;

  ecma48_modevalues mode;
};

size_t ecma48_parser_interpret_bytes(ecma48_t *e48, char *bytes, size_t len);

void ecma48_push_output_bytes(ecma48_t *e48, char *bytes, size_t len);
void ecma48_push_output_vsprintf(ecma48_t *e48, char *format, va_list args);
void ecma48_push_output_sprintf(ecma48_t *e48, char *format, ...);

int ecma48_state_on_text(ecma48_t *e48, int codepoints[], int npoints);
int ecma48_state_on_control(ecma48_t *e48, unsigned char control);
int ecma48_state_on_escape(ecma48_t *e48, char escape);
int ecma48_state_on_csi(ecma48_t *e48, char *intermed, int *args, int argcount, char command);

void ecma48_state_setpen(ecma48_t *e48, int args[], int argcount);

void ecma48_state_initmodes(ecma48_t *e48);
void ecma48_state_setmode(ecma48_t *e48, ecma48_mode mode, int val);

#endif
