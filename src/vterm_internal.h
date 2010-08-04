#ifndef __VTERM_INTERNAL_H__
#define __VTERM_INTERNAL_H__

#include "vterm.h"

#include <glib.h>

typedef struct _VTermEncoding VTermEncoding;

struct _VTermState
{
  VTerm *vt;

  const VTermStateCallbacks *callbacks;
  void *cbdata;

  int rows;
  int cols;

  /* Current cursor position */
  VTermPos pos;
  /* Saved cursor position under DEC mode 1048/1049 */
  VTermPos saved_pos;

  int at_phantom; /* True if we're on the "81st" phantom column to defer a wraparound */

  int scrollregion_start;
  int scrollregion_end;

  /* Mouse state */
  int mouse_col, mouse_row;
  int mouse_buttons;

  /* Last glyph output, for Unicode recombining purposes */
  uint32_t *combine_chars;
  size_t combine_chars_size;
  int combine_width; // The width of the glyph above
  VTermPos combine_pos;   // Position before movement

  struct {
    int keypad:1;
    int cursor:1;
    int autowrap:1;
    int insert:1;
    int cursor_visible:1;
    int alt_screen:1;
    int saved_cursor:1;
  } mode;

  VTermEncoding *encoding[4];
  int gl_set, gr_set;

  struct {
    unsigned int bold:1;
    unsigned int underline:2;
    unsigned int italic:1;
    unsigned int blink:1;
    unsigned int reverse:1;
    unsigned int font:4; /* To store 0-9 */
  } pen;
};

struct _VTerm
{
  int rows;
  int cols;

  int is_utf8;

  const VTermParserCallbacks *parser_callbacks;
  void *cbdata;

  GString *inbuffer;
  GString *outbuffer;
  VTermState *state;
};

struct _VTermEncoding {
  int (*decode)(VTermEncoding *enc, uint32_t cp[], int *cpi, int cplen,
                  const char bytes[], size_t *pos, size_t len);
};

typedef enum {
  ENC_UTF8,
  ENC_SINGLE_94
} VTermEncodingType;

size_t vterm_parser_interpret_bytes(VTerm *vt, const char bytes[], size_t len);

void vterm_push_output_bytes(VTerm *vt, const char *bytes, size_t len);
void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args);
void vterm_push_output_sprintf(VTerm *vt, const char *format, ...);

void vterm_state_setpen(VTermState *state, const long args[], int argcount);

VTermEncoding *vterm_lookup_encoding(VTermEncodingType type, char designation);

int vterm_unicode_width(int codepoint);
int vterm_unicode_is_combining(int codepoint);

#endif
