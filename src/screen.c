#include "vterm_internal.h"

#include <stdio.h>

#define MAX_CHARS_PER_CELL 6

#define UNICODE_SPACE 0x20
#define UNICODE_LINEFEED 0x13

/* State of the pen at some moment in time, also used in a cell */
typedef struct
{
  /* After the bitfield */
  VTermColor   fg, bg;

  unsigned int bold      : 1;
  unsigned int underline : 2;
  unsigned int italic    : 1;
  unsigned int blink     : 1;
  unsigned int reverse   : 1;
  unsigned int strike    : 1;
  unsigned int font      : 4; /* 0 to 9 */
} ScreenPen;

/* Internal representation of a screen cell */
typedef struct
{
  uint32_t chars[MAX_CHARS_PER_CELL];
  ScreenPen pen;
} ScreenCell;

struct _VTermScreen
{
  VTerm *vt;
  VTermState *state;

  const VTermScreenCallbacks *callbacks;
  void *cbdata;

  int rows;
  int cols;

  /* Primary and Altscreen. buffers[1] is lazily allocated as needed */
  ScreenCell *buffers[2];

  /* buffer will == buffers[0] or buffers[1], depending on altscreen */
  ScreenCell *buffer;

  ScreenPen pen;
};

static inline ScreenCell *getcell(VTermScreen *screen, int row, int col)
{
  /* TODO: Bounds checking */
  return screen->buffer + (screen->cols * row) + col;
}

static ScreenCell *realloc_buffer(VTermScreen *screen, ScreenCell *buffer, int new_rows, int new_cols)
{
  ScreenCell *new_buffer = vterm_allocator_malloc(screen->vt, sizeof(ScreenCell) * new_rows * new_cols);

  for(int row = 0; row < new_rows; row++) {
    for(int col = 0; col < new_cols; col++) {
      ScreenCell *new_cell = new_buffer + row*new_cols + col;

      if(buffer && row < screen->rows && col < screen->cols)
        *new_cell = buffer[row * screen->cols + col];
      else {
        new_cell->chars[0] = 0;
        new_cell->pen = screen->pen;
      }
    }
  }

  if(buffer)
    vterm_allocator_free(screen->vt, buffer);

  return new_buffer;
}

static void damagerect(VTermScreen *screen, VTermRect rect)
{
  if(screen->callbacks && screen->callbacks->damage)
    (*screen->callbacks->damage)(rect, screen->cbdata);
}

static int putglyph(const uint32_t chars[], int width, VTermPos pos, void *user)
{
  VTermScreen *screen = user;
  ScreenCell *cell = getcell(screen, pos.row, pos.col);
  int i;

  for(i = 0; i < MAX_CHARS_PER_CELL && chars[i]; i++) {
    cell->chars[i] = chars[i];
    cell->pen = screen->pen;
  }
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

static void copycell(VTermPos dest, VTermPos src, void *user)
{
  VTermScreen *screen = user;
  ScreenCell *destcell = getcell(screen, dest.row, dest.col);
  ScreenCell *srccell = getcell(screen, src.row, src.col);

  *destcell = *srccell;
}

static int moverect(VTermRect dest, VTermRect src, void *user)
{
  VTermScreen *screen = user;

  vterm_copy_cells(dest, src, &copycell, user);

  if(screen->callbacks && screen->callbacks->moverect)
    if((*screen->callbacks->moverect)(dest, src, screen->cbdata))
      return 1;

  damagerect(screen, dest);

  return 1;
}

static int erase(VTermRect rect, void *user)
{
  VTermScreen *screen = user;

  for(int row = rect.start_row; row < rect.end_row; row++)
    for(int col = rect.start_col; col < rect.end_col; col++) {
      ScreenCell *cell = getcell(screen, row, col);
      cell->chars[0] = 0;
      cell->pen = screen->pen;
    }

  damagerect(screen, rect);

  return 1;
}

static int movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->movecursor)
    return (*screen->callbacks->movecursor)(pos, oldpos, visible, screen->cbdata);

  return 0;
}

static int setpenattr(VTermAttr attr, VTermValue *val, void *user)
{
  VTermScreen *screen = user;

  switch(attr) {
  case VTERM_ATTR_BOLD:
    screen->pen.bold = val->boolean;
    return 1;
  case VTERM_ATTR_UNDERLINE:
    screen->pen.underline = val->number;
    return 1;
  case VTERM_ATTR_ITALIC:
    screen->pen.italic = val->boolean;
    return 1;
  case VTERM_ATTR_BLINK:
    screen->pen.blink = val->boolean;
    return 1;
  case VTERM_ATTR_REVERSE:
    screen->pen.reverse = val->boolean;
    return 1;
  case VTERM_ATTR_STRIKE:
    screen->pen.strike = val->boolean;
    return 1;
  case VTERM_ATTR_FONT:
    screen->pen.font = val->number;
    return 1;
  case VTERM_ATTR_FOREGROUND:
    screen->pen.fg = val->color;
    return 1;
  case VTERM_ATTR_BACKGROUND:
    screen->pen.bg = val->color;
    return 1;
  }

  return 0;
}

static int settermprop(VTermProp prop, VTermValue *val, void *user)
{
  VTermScreen *screen = user;

  switch(prop) {
  case VTERM_PROP_ALTSCREEN:
    {
      if(val->boolean && !screen->buffers[1])
        return 0;

      VTermRect rect = {
        .start_row = 0,
        .end_row   = screen->rows,
        .start_col = 0,
        .end_col   = screen->cols,
      };

      screen->buffer = val->boolean ? screen->buffers[1] : screen->buffers[0];
      damagerect(screen, rect);
    }
    break;
  default:
    ; /* ignore */
  }

  if(screen->callbacks && screen->callbacks->settermprop)
    return (*screen->callbacks->settermprop)(prop, val, screen->cbdata);

  return 0;
}

static int setmousefunc(VTermMouseFunc func, void *data, void *user)
{
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->setmousefunc)
    return (*screen->callbacks->setmousefunc)(func, data, screen->cbdata);

  return 0;
}

static int bell(void *user)
{
  VTermScreen *screen = user;

  if(screen->callbacks && screen->callbacks->bell)
    return (*screen->callbacks->bell)(screen->cbdata);

  return 0;
}

static int resize(int new_rows, int new_cols, void *user)
{
  VTermScreen *screen = user;

  int is_altscreen = (screen->buffers[1] && screen->buffer == screen->buffers[1]);

  screen->buffers[0] = realloc_buffer(screen, screen->buffers[0], new_rows, new_cols);
  if(screen->buffers[1])
    screen->buffers[1] = realloc_buffer(screen, screen->buffers[1], new_rows, new_cols);

  screen->buffer = is_altscreen ? screen->buffers[1] : screen->buffers[0];

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

  if(screen->callbacks && screen->callbacks->resize)
    return (*screen->callbacks->resize)(new_rows, new_cols, screen->cbdata);

  return 1;
}

static VTermStateCallbacks state_cbs = {
  .putglyph     = &putglyph,
  .movecursor   = &movecursor,
  .moverect     = &moverect,
  .erase        = &erase,
  .setpenattr   = &setpenattr,
  .settermprop  = &settermprop,
  .setmousefunc = &setmousefunc,
  .bell         = &bell,
  .resize       = &resize,
};

static VTermScreen *screen_new(VTerm *vt)
{
  VTermState *state = vterm_obtain_state(vt);
  if(!state)
    return NULL;

  VTermScreen *screen = vterm_allocator_malloc(vt, sizeof(VTermScreen));
  int rows, cols;

  vterm_get_size(vt, &rows, &cols);

  screen->vt = vt;
  screen->state = state;

  screen->rows = rows;
  screen->cols = cols;

  screen->buffers[0] = realloc_buffer(screen, NULL, rows, cols);

  screen->buffer = screen->buffers[0];

  vterm_state_set_callbacks(screen->state, &state_cbs, screen);

  return screen;
}

void vterm_screen_free(VTermScreen *screen)
{
  vterm_allocator_free(screen->vt, screen->buffers[0]);
  if(screen->buffers[1])
    vterm_allocator_free(screen->vt, screen->buffers[1]);

  vterm_allocator_free(screen->vt, screen);
}

void vterm_screen_reset(VTermScreen *screen)
{
  vterm_state_reset(screen->state);
}

size_t vterm_screen_get_chars(VTermScreen *screen, uint32_t *chars, size_t len, const VTermRect rect)
{
  size_t outpos = 0;
  int padding = 0;
#define PUT(c) (chars && outpos < len) ? chars[outpos++] = (c) : outpos++

  for(int row = rect.start_row; row < rect.end_row; row++) {
    for(int col = rect.start_col; col < rect.end_col; col++) {
      ScreenCell *cell = getcell(screen, row, col);

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

/* Copy internal to external representation of a screen cell */
void vterm_screen_get_cell(VTermScreen *screen, VTermPos pos, VTermScreenCell *cell)
{
  ScreenCell *intcell = getcell(screen, pos.row, pos.col);

  for(int i = 0; ; i++) {
    cell->chars[i] = intcell->chars[i];
    if(!intcell->chars[i])
      break;
  }

  cell->attrs.bold      = intcell->pen.bold;
  cell->attrs.underline = intcell->pen.underline;
  cell->attrs.italic    = intcell->pen.italic;
  cell->attrs.blink     = intcell->pen.blink;
  cell->attrs.reverse   = intcell->pen.reverse;
  cell->attrs.strike    = intcell->pen.strike;
  cell->attrs.font      = intcell->pen.font;

  cell->fg = intcell->pen.fg;
  cell->bg = intcell->pen.bg;

  if(pos.col < (screen->cols - 1) &&
     getcell(screen, pos.row, pos.col + 1)->chars[0] == (uint32_t)-1)
    cell->width = 2;
  else
    cell->width = 1;
}

VTermScreen *vterm_obtain_screen(VTerm *vt)
{
  if(vt->screen)
    return vt->screen;

  VTermScreen *screen = screen_new(vt);
  vt->screen = screen;

  return screen;
}

void vterm_screen_enable_altscreen(VTermScreen *screen, int altscreen)
{

  if(!screen->buffers[1] && altscreen) {
    int rows, cols;
    vterm_get_size(screen->vt, &rows, &cols);

    screen->buffers[1] = realloc_buffer(screen, NULL, rows, cols);
  }
}

void vterm_screen_set_callbacks(VTermScreen *screen, const VTermScreenCallbacks *callbacks, void *user)
{
  screen->callbacks = callbacks;
  screen->cbdata = user;
}
