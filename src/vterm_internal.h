#ifndef __VTERM_INTERNAL_H__
#define __VTERM_INTERNAL_H__

#include "vterm.h"

#include <glib.h>

typedef struct
{
  /* [0] for user code, [1] for buffer engine */
  const VTermStateCallbacks *callbacks[2];

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

  /* [0] for user code, [1] for state engine */
  const VTermParserCallbacks *parser_callbacks[2];

  GString *inbuffer;
  GString *outbuffer;
  VTermState *state;

  struct {
    int keypad:1;
    int cursor:1;
    int autowrap:1;
    int cursor_visible:1;
    int alt_screen:1;
    int saved_cursor:1;
  } mode;
};

size_t vterm_parser_interpret_bytes(VTerm *vt, const char bytes[], size_t len);

void vterm_push_output_bytes(VTerm *vt, const char *bytes, size_t len);
void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args);
void vterm_push_output_sprintf(VTerm *vt, const char *format, ...);

void vterm_state_setpen(VTerm *vt, const long args[], int argcount);

int vterm_unicode_width(int codepoint);
int vterm_unicode_is_combining(int codepoint);

#endif
