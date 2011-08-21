#include "vterm_internal.h"

#include <stdio.h>

#define MAX_CHARS_PER_CELL 6

#define UNICODE_SPACE 0x20
#define UNICODE_LINEFEED 0x13

typedef struct
{
  uint32_t chars[MAX_CHARS_PER_CELL];
} VTermScreenCell;

struct _VTermScreen
{
  VTerm *vt;

  const VTermScreenCallbacks *callbacks;
  void *cbdata;

  int rows;
  int cols;
  VTermScreenCell *buffer;
};

static inline VTermScreenCell *getcell(VTermScreen *screen, int row, int col)
{
  /* TODO: Bounds checking */
  return screen->buffer + (screen->cols * row) + col;
}

static void damagerect(VTermScreen *screen, VTermRect rect)
{
  if(screen->callbacks && screen->callbacks->damage)
    (*screen->callbacks->damage)(rect, screen->cbdata);
}

static void damagecell(VTermScreen *screen, int row, int col)
{
  VTermRect rect = {
    .start_row = row,
    .end_row   = row+1,
    .start_col = col,
    .end_col   = col+1,
  };

  damagerect(screen, rect);
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
    getcell(screen, pos.row, pos.col + col)->chars[0] = (uint32_t)-1;

  VTermRect rect = {
    .start_row = pos.row,
    .end_row   = pos.row+1,
    .start_col = pos.col,
    .end_col   = pos.col+width,
  };

  damagerect(screen, rect);

  return 1;
}

static int copycell(VTermPos dest, VTermPos src, void *user)
{
  VTermScreen *screen = user;
  VTermScreenCell *destcell = getcell(screen, dest.row, dest.col);
  VTermScreenCell *srccell = getcell(screen, src.row, src.col);

  *destcell = *srccell;

  damagecell(screen, dest.row, dest.col);

  return 1;
}

static int erase(VTermRect rect, void *user)
{
  VTermScreen *screen = user;

  for(int row = rect.start_row; row < rect.end_row; row++)
    for(int col = rect.start_col; col < rect.end_col; col++)
      getcell(screen, row, col)->chars[0] = 0;

  damagerect(screen, rect);

  return 1;
}

static int resize(int new_rows, int new_cols, void *user)
{
  VTermScreen *screen = user;

  VTermScreenCell *new_buffer = g_new0(VTermScreenCell, new_rows * new_cols);

  for(int row = 0; row < new_rows; row++) {
    for(int col = 0; col < new_cols; col++) {
      VTermScreenCell *new_cell = new_buffer + row*new_cols + col;

      if(row < screen->rows && col < screen->cols)
        *new_cell = *(getcell(screen, row, col));
      else
        new_cell->chars[0] = 0;
    }
  }

  if(screen->buffer)
    free(screen->buffer);

  if(new_cols > screen->cols) {
    VTermRect rect = {
      .start_row = 0,
      .end_row   = screen->rows,
      .start_col = screen->cols,
      .end_col   = new_cols,
    };
    damagerect(screen, rect);
  }

  if(new_rows > screen->rows) {
    VTermRect rect = {
      .start_row = screen->rows,
      .end_row   = new_rows,
      .start_col = 0,
      .end_col   = new_cols,
    };
    damagerect(screen, rect);
  }

  screen->rows = new_rows;
  screen->cols = new_cols;
  screen->buffer = new_buffer;

  return 1;
}

static VTermStateCallbacks state_cbs = {
  .putglyph = &putglyph,
  .copycell = &copycell,
  .erase    = &erase,
  .resize   = &resize,
};

static VTermScreen *screen_new(VTerm *vt)
{
  VTermScreen *screen = g_new0(VTermScreen, 1);
  int rows, cols;

  vterm_get_size(vt, &rows, &cols);

  screen->vt = vt;

  screen->cols = 0;
  screen->buffer = NULL;

  resize(rows, cols, screen);

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
  int padding = 0;
#define PUT(c) (chars && outpos < len) ? chars[outpos++] = (c) : outpos++

  for(int row = rect.start_row; row < rect.end_row; row++) {
    for(int col = rect.start_col; col < rect.end_col; col++) {
      VTermScreenCell *cell = getcell(screen, row, col);

      if(cell->chars[0] == 0)
        // Erased cell, might need a space
        padding++;
      else if(cell->chars[0] == (uint32_t)-1)
        // Gap behind a double-width char, do nothing
        ;
      else {
        while(padding) {
          PUT(UNICODE_SPACE);
          padding--;
        }
        for(int i = 0; i < MAX_CHARS_PER_CELL && cell->chars[i]; i++) {
          PUT(cell->chars[i]);
        }
      }
    }

    if(row < rect.end_row - 1) {
      PUT(UNICODE_LINEFEED);
      padding = 0;
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

void vterm_screen_set_callbacks(VTermScreen *screen, const VTermScreenCallbacks *callbacks, void *user)
{
  screen->callbacks = callbacks;
  screen->cbdata = user;
}
