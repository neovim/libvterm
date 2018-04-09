#ifndef __VTERM_INTERNAL_H__
#define __VTERM_INTERNAL_H__

#include "vterm.h"

#include <stdarg.h>

#if defined(__GNUC__)
# define INTERNAL __attribute__((visibility("internal")))
#else
# define INTERNAL
#endif

#ifdef DEBUG
# define DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
# define DEBUG_LOG(...)
#endif

#define ESC_S "\x1b"

#define INTERMED_MAX 16

#define CSI_ARGS_MAX 16
#define CSI_LEADER_MAX 16

typedef struct VTermEncoding VTermEncoding;

typedef struct {
  VTermEncoding *enc;

  /* This size should be increased if required by other stateful encodings */
  char           data[4*sizeof(uint32_t)];
} VTermEncodingInstance;

struct VTermPen
{
  VTermColor fg;
  VTermColor bg;
  unsigned int bold:1;
  unsigned int underline:2;
  unsigned int italic:1;
  unsigned int blink:1;
  unsigned int reverse:1;
  unsigned int strike:1;
  unsigned int font:4; /* To store 0-9 */
};

static inline int vterm_color_equal(VTermColor a, VTermColor b)
{
  return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

struct VTermState
{
  VTerm *vt;

  const VTermStateCallbacks *callbacks;
  void *cbdata;

  const VTermParserCallbacks *fallbacks;
  void *fbdata;

  int rows;
  int cols;

  /* Current cursor position */
  VTermPos pos;

  int at_phantom; /* True if we're on the "81st" phantom column to defer a wraparound */

  int scrollregion_top;
  int scrollregion_bottom; /* -1 means unbounded */
#define SCROLLREGION_BOTTOM(state) ((state)->scrollregion_bottom > -1 ? (state)->scrollregion_bottom : (state)->rows)
  int scrollregion_left;
#define SCROLLREGION_LEFT(state)  ((state)->mode.leftrightmargin ? (state)->scrollregion_left : 0)
  int scrollregion_right; /* -1 means unbounded */
#define SCROLLREGION_RIGHT(state) ((state)->mode.leftrightmargin && (state)->scrollregion_right > -1 ? (state)->scrollregion_right : (state)->cols)

  /* Bitvector of tab stops */
  unsigned char *tabstops;

  VTermLineInfo *lineinfo;
#define ROWWIDTH(state,row) ((state)->lineinfo[(row)].doublewidth ? ((state)->cols / 2) : (state)->cols)
#define THISROWWIDTH(state) ROWWIDTH(state, (state)->pos.row)

  /* Mouse state */
  int mouse_col, mouse_row;
  int mouse_buttons;
  int mouse_flags;
#define MOUSE_WANT_CLICK 0x01
#define MOUSE_WANT_DRAG  0x02
#define MOUSE_WANT_MOVE  0x04

  enum { MOUSE_X10, MOUSE_UTF8, MOUSE_SGR, MOUSE_RXVT } mouse_protocol;

  /* Last glyph output, for Unicode recombining purposes */
  uint32_t *combine_chars;
  size_t combine_chars_size; /* Number of ELEMENTS in the above */
  int combine_width; /* The width of the glyph above */
  VTermPos combine_pos;   /* Position before movement */

  struct {
    unsigned int keypad:1;
    unsigned int cursor:1;
    unsigned int autowrap:1;
    unsigned int insert:1;
    unsigned int newline:1;
    unsigned int cursor_visible:1;
    unsigned int cursor_blink:1;
    unsigned int cursor_shape:2;
    unsigned int alt_screen:1;
    unsigned int origin:1;
    unsigned int screen:1;
    unsigned int leftrightmargin:1;
    unsigned int bracketpaste:1;
    unsigned int report_focus:1;
  } mode;

  VTermEncodingInstance encoding[4], encoding_utf8;
  int gl_set, gr_set, gsingle_set;

  struct VTermPen pen;

  VTermColor default_fg;
  VTermColor default_bg;
  VTermColor colors[16]; /* Store the 8 ANSI and the 8 ANSI high-brights only */

  int fg_index;
  int bg_index;
  int bold_is_highbright;

  unsigned int protected_cell : 1;

  /* Saved state under DEC mode 1048/1049 */
  struct {
    VTermPos pos;
    struct VTermPen pen;

    struct {
      unsigned int cursor_visible:1;
      unsigned int cursor_blink:1;
      unsigned int cursor_shape:2;
    } mode;
  } saved;
};

typedef enum {
  VTERM_PARSER_OSC,
  VTERM_PARSER_DCS,

  VTERM_N_PARSER_TYPES
} VTermParserStringType;

struct VTerm
{
  VTermAllocatorFunctions *allocator;
  void *allocdata;

  int rows;
  int cols;

  struct {
    unsigned int utf8:1;
    unsigned int ctrl8bit:1;
  } mode;

  struct {
    enum VTermParserState {
      NORMAL,
      CSI_LEADER,
      CSI_ARGS,
      CSI_INTERMED,
      ESC,
      /* below here are the "string states" */
      STRING,
      ESC_IN_STRING,
    } state;

    int intermedlen;
    char intermed[INTERMED_MAX];

    int csi_leaderlen;
    char csi_leader[CSI_LEADER_MAX];

    int csi_argi;
    long csi_args[CSI_ARGS_MAX];

    const VTermParserCallbacks *callbacks;
    void *cbdata;

    VTermParserStringType stringtype;
    char  *strbuffer;
    size_t strbuffer_len;
    size_t strbuffer_cur;
  } parser;

  /* len == malloc()ed size; cur == number of valid bytes */

  char  *outbuffer;
  size_t outbuffer_len;
  size_t outbuffer_cur;

  VTermState *state;
  VTermScreen *screen;
};

struct VTermEncoding {
  void (*init) (VTermEncoding *enc, void *data);
  void (*decode)(VTermEncoding *enc, void *data,
                 uint32_t cp[], int *cpi, int cplen,
                 const char bytes[], size_t *pos, size_t len);
};

typedef enum {
  ENC_UTF8,
  ENC_SINGLE_94
} VTermEncodingType;

void *vterm_allocator_malloc(VTerm *vt, size_t size);
void  vterm_allocator_free(VTerm *vt, void *ptr);

void vterm_push_output_bytes(VTerm *vt, const char *bytes, size_t len);
void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args);
void vterm_push_output_sprintf(VTerm *vt, const char *format, ...);
void vterm_push_output_sprintf_ctrl(VTerm *vt, unsigned char ctrl, const char *fmt, ...);
void vterm_push_output_sprintf_dcs(VTerm *vt, const char *fmt, ...);

void vterm_state_free(VTermState *state);

void vterm_state_newpen(VTermState *state);
void vterm_state_resetpen(VTermState *state);
void vterm_state_setpen(VTermState *state, const long args[], int argcount);
int  vterm_state_getpen(VTermState *state, long args[], int argcount);
void vterm_state_savepen(VTermState *state, int save);

enum {
  C1_SS3 = 0x8f,
  C1_DCS = 0x90,
  C1_CSI = 0x9b,
  C1_ST  = 0x9c,
};

void vterm_state_push_output_sprintf_CSI(VTermState *vts, const char *format, ...);

void vterm_screen_free(VTermScreen *screen);

VTermEncoding *vterm_lookup_encoding(VTermEncodingType type, char designation);

int vterm_unicode_width(uint32_t codepoint);
int vterm_unicode_is_combining(uint32_t codepoint);

#endif
