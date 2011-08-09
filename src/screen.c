#include "vterm_internal.h"

#define MAX_CHARS_PER_CELL 6

typedef struct
{
  uint32_t chars[MAX_CHARS_PER_CELL];
} VTermScreenCell;

struct _VTermScreen
{
  VTerm *vt;

  int cols;
  VTermScreenCell *buffer;
};

static inline VTermScreenCell *getcell(VTermScreen *screen, int row, int col)
{
  /* TODO: Bounds checking */
  return screen->buffer + (screen->cols * row) + col;
}

static int putglyph(const uint32_t chars[], int width, VTermPos pos, void *user)
{
  VTermScreen *screen = user;
  VTermScreenCell *cell = getcell(screen, pos.row, pos.col);
  int i;

  for(i = 0; i < MAX_CHARS_PER_CELL && chars[i]; i++)
    cell->chars[i] = chars[i];
  if(i < MAX_CHARS_PER_CELL)
    cell->chars[i] = 0;

  for(int col = 1; col < width; col++)
    getcell(screen, pos.row, pos.col + col)->chars[0] = 0;

  return 1;
}

static int copycell(VTermPos dest, VTermPos src, void *user)
{
  VTermScreen *screen = user;
  VTermScreenCell *destcell = getcell(screen, dest.row, dest.col);
  VTermScreenCell *srccell = getcell(screen, src.row, src.col);

  *destcell = *srccell;

  return 1;
}

static int erase(VTermRect rect, void *user)
{
  VTermScreen *screen = user;

  for(int row = rect.start_row; row < rect.end_row; row++)
    for(int col = rect.start_col; col < rect.end_col; col++)
      getcell(screen, row, col)->chars[0] = 0;

  return 1;
}

static VTermStateCallbacks state_cbs = {
  .putglyph = &putglyph,
  .copycell = &copycell,
  .erase    = &erase,
};

static VTermScreen *screen_new(VTerm *vt)
{
  VTermScreen *screen = g_new0(VTermScreen, 1);
  int rows, cols;

  vterm_get_size(vt, &rows, &cols);

  screen->vt = vt;

  screen->cols = cols;
  screen->buffer = g_new0(VTermScreenCell, rows * cols);

  vterm_state_set_callbacks(vterm_obtain_state(vt), &state_cbs, screen);

  return screen;
}

void vterm_screen_reset(VTermScreen *screen)
{
  vterm_state_reset(vterm_obtain_state(screen->vt));
}

size_t vterm_screen_get_chars(VTermScreen *screen, uint32_t *chars, size_t len, const VTermRect rect)
{
  size_t outpos = 0;

  for(int row = rect.start_row; row < rect.end_row; row++) {
    for(int col = rect.start_col; col < rect.end_col; col++) {
      VTermScreenCell *cell = getcell(screen, row, col);
      for(int i = 0; i < MAX_CHARS_PER_CELL && cell->chars[i]; i++) {
        if(chars && outpos < len)
          chars[outpos] = cell->chars[i];
        outpos++;
      }
    }
  }

  return outpos;
}

VTermScreen *vterm_initialise_screen(VTerm *vt)
{
  if(vt->screen)
    return vt->screen;

  VTermScreen *screen = screen_new(vt);
  vt->screen = screen;

  return screen;
}
