#ifndef __VTERM_INTERNAL_H__
#define __VTERM_INTERNAL_H__

#include "vterm.h"

#include <glib.h>

typedef struct vterm_state_s
{
  const vterm_state_callbacks_t *callbacks;

  /* Current cursor position */
  vterm_position_t pos;
  /* Saved cursor position under DEC mode 1048/1049 */
  vterm_position_t saved_pos;

  int scrollregion_start;
  int scrollregion_end;

  /* Current pen - entirely managed by user code */
  void *pen;

  /* Mouse state */
  int mouse_col, mouse_row;
  int mouse_buttons;
} vterm_state_t;

struct vterm_s
{
  int rows;
  int cols;

  int is_utf8;

  const vterm_parser_callbacks_t *parser_callbacks;

  GString *inbuffer;
  GString *outbuffer;
  vterm_state_t *state;

  vterm_modevalues mode;
};

size_t vterm_parser_interpret_bytes(vterm_t *vt, const char bytes[], size_t len);

void vterm_push_output_bytes(vterm_t *vt, const char *bytes, size_t len);
void vterm_push_output_vsprintf(vterm_t *vt, const char *format, va_list args);
void vterm_push_output_sprintf(vterm_t *vt, const char *format, ...);

int vterm_state_on_text(vterm_t *vt, const int codepoints[], int npoints);
int vterm_state_on_control(vterm_t *vt, unsigned char control);
int vterm_state_on_escape(vterm_t *vt, char escape);
int vterm_state_on_csi(vterm_t *vt, const char *intermed, const int args[], int argcount, char command);

void vterm_state_setpen(vterm_t *vt, const int args[], int argcount);

void vterm_state_initmodes(vterm_t *vt);
void vterm_state_setmode(vterm_t *vt, vterm_mode mode, int val);

#endif
