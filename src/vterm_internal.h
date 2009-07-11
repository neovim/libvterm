#ifndef __VTERM_INTERNAL_H__
#define __VTERM_INTERNAL_H__

#include "vterm.h"

#include <glib.h>

typedef struct
{
  const VTermStateCallbacks *callbacks;

  /* Current cursor position */
  VTermPos pos;
  /* Saved cursor position under DEC mode 1048/1049 */
  VTermPos saved_pos;

  int scrollregion_start;
  int scrollregion_end;

  /* Current pen - entirely managed by user code */
  void *pen;

  /* Mouse state */
  int mouse_col, mouse_row;
  int mouse_buttons;

  /* Last glyph output, for Unicode recombining purposes */
  uint32_t *combine_chars;
  size_t combine_chars_size;
  int combine_width; // The width of the glyph above
  VTermPos combine_pos;   // Position before movement
} VTermState;

struct _VTerm
{
  int rows;
  int cols;

  int is_utf8;

  const VTermParserCallbacks *parser_callbacks;

  GString *inbuffer;
  GString *outbuffer;
  VTermState *state;

  VTermModeValues mode;
};

size_t vterm_parser_interpret_bytes(VTerm *vt, const char bytes[], size_t len);

void vterm_push_output_bytes(VTerm *vt, const char *bytes, size_t len);
void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args);
void vterm_push_output_sprintf(VTerm *vt, const char *format, ...);

int vterm_state_on_text(VTerm *vt, const int codepoints[], int npoints);
int vterm_state_on_control(VTerm *vt, unsigned char control);
int vterm_state_on_escape(VTerm *vt, const char *bytes, size_t len);
int vterm_state_on_csi(VTerm *vt, const char *intermed, const long args[], int argcount, char command);

void vterm_state_setpen(VTerm *vt, const long args[], int argcount);

void vterm_state_initmodes(VTerm *vt);
void vterm_state_setmode(VTerm *vt, VTermMode mode, int val);

int vterm_unicode_width(int codepoint);
int vterm_unicode_is_combining(int codepoint);

#endif
