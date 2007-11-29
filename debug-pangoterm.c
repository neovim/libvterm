#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "ecma48.h"

#include <gtk/gtk.h>

int master;
ecma48_t *e48;

int cell_width;
int cell_height;

GtkWidget *termwin;

GdkGC *cursor_gc;

typedef struct {
  PangoLayout *layout;
  GdkColor bg_col;
} term_cell;

term_cell **cells;

const char *default_fg = "gray90";
const char *default_bg = "black";

const char *col_spec[] = {
  "black",
  "red",
  "green",
  "yellow",
  "blue",
  "magenta",
  "cyan",
  "white"
};

typedef struct {
  GdkColor bg_col;
  PangoAttrList *attrs;
} term_pen;

void paint_cell(int row, int col, GdkRectangle *clip, gboolean with_cursor)
{
  GdkWindow *win = termwin->window;

  GdkGC *gc = gdk_gc_new(win);

  if(clip)
    gdk_gc_set_clip_rectangle(gc, clip);

  gdk_gc_set_rgb_fg_color(gc, &cells[row][col].bg_col);

  gdk_draw_rectangle(win,
      gc,
      TRUE,
      col * cell_width, row * cell_height,
      cell_width,         cell_height);

  g_object_unref(G_OBJECT(gc));

  PangoLayout *layout = cells[row][col].layout;

  if(layout) {
    gtk_paint_layout(gtk_widget_get_style(termwin),
        win,
        GTK_WIDGET_STATE(termwin),
        FALSE,
        clip,
        termwin,
        NULL,
        col * cell_width,
        row * cell_height,
        layout);
  }

  if(with_cursor) {
    gdk_draw_rectangle(win,
        cursor_gc,
        FALSE,
        col * cell_width, row * cell_height,
        cell_width - 1,   cell_height - 1);
  }
}

gboolean term_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
  // Work out the rows/columns that need updating
  int first_row = event->area.y / cell_height;
  int first_col = event->area.x / cell_width;

  int last_row = (event->area.y + event->area.height - 1) / cell_height;
  int last_col = (event->area.x + event->area.width  - 1) / cell_width;

  ecma48_position_t cursorpos;

  ecma48_state_get_cursorpos(e48, &cursorpos);

  int row, col;
  for(row = first_row; row <= last_row; row++)
    for(col = first_col; col <= last_col; col++)
      paint_cell(row, col, &event->area, row == cursorpos.row && col == cursorpos.col);

  return TRUE;
}

int term_putchar(ecma48_t *e48, uint32_t codepoint, ecma48_position_t pos, void *pen_p)
{
  term_pen *pen = pen_p;

  char s[] = { codepoint, 0 };

  PangoLayout *layout = cells[pos.row][pos.col].layout;

  pango_layout_set_text(layout, s, -1);
  if(pen->attrs)
    pango_layout_set_attributes(layout, pen->attrs);

  cells[pos.row][pos.col].bg_col = pen->bg_col;

  paint_cell(pos.row, pos.col, NULL, FALSE);

  return 1;
}

int term_movecursor(ecma48_t *e48, ecma48_position_t pos, ecma48_position_t oldpos)
{
  // Clear the old one
  paint_cell(oldpos.row, oldpos.col, NULL, FALSE);

  paint_cell(pos.row, pos.col, NULL, TRUE);

  return 1;
}

int term_copycell(ecma48_t *e48, ecma48_position_t destpos, ecma48_position_t srcpos)
{
  cells[destpos.row][destpos.col].bg_col = cells[srcpos.row][srcpos.col].bg_col;

  g_object_unref(G_OBJECT(cells[destpos.row][destpos.col].layout));
  cells[destpos.row][destpos.col].layout = 
    pango_layout_copy(cells[srcpos.row][srcpos.col].layout);

  paint_cell(destpos.row, destpos.col, NULL, FALSE);

  return 1;
}

int term_erase(ecma48_t *e48, ecma48_rectangle_t rect, void *pen_p)
{
  term_pen *pen = pen_p;

  GdkWindow *win = termwin->window;

  GdkGC *gc = gdk_gc_new(win);

  gdk_gc_set_rgb_fg_color(gc, &pen->bg_col);

  int row, col;
  for(row = rect.start_row; row < rect.end_row; row++)
    for(col = rect.start_col; col < rect.end_col; col++) {
      cells[row][col].bg_col = pen->bg_col;
      pango_layout_set_text(cells[row][col].layout, "", 0);
    }

  gdk_draw_rectangle(win,
      gc,
      TRUE,
      rect.start_col * cell_width, rect.start_row * cell_height,
      (rect.end_col - rect.start_col) * cell_width,
      (rect.end_row - rect.start_row) * cell_height);

  g_object_unref(G_OBJECT(gc));

  return 1;
}

int term_setpen(ecma48_t *e48, int sgrcmd, void **penstore)
{
#define CLONEATTRS \
  do { \
    PangoAttrList *newattrs = pango_attr_list_copy(pen->attrs); \
    pango_attr_list_unref(pen->attrs); \
    pen->attrs = newattrs; \
  } while(0)

#define ADDATTR(a) \
  do { \
    PangoAttribute *newattr = (a); \
    newattr->start_index = 0; \
    newattr->end_index = 100; /* can't really know for sure */ \
    pango_attr_list_change(pen->attrs, newattr); \
  } while(0)

  term_pen *pen = *penstore;

  if(!*penstore) {
    pen = g_new0(term_pen, 1);
    *penstore = pen;
    pen->attrs = NULL;
  }

  switch(sgrcmd) {
  case -1:
  case 0: // Reset all
    gdk_color_parse(default_bg, &pen->bg_col);

    if(pen->attrs)
      pango_attr_list_unref(pen->attrs);
    pen->attrs = pango_attr_list_new();

    GdkColor fg_col;
    gdk_color_parse(default_fg, &fg_col);
    ADDATTR(pango_attr_foreground_new(
        fg_col.red * 256, fg_col.green * 256, fg_col.blue * 256)
      );
    break;

  case 1: // Bold
    CLONEATTRS;
    ADDATTR(pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    break;

  case 30: case 31: case 32: case 33:
  case 34: case 35: case 36: case 37: // Foreground colour
  case 39: // Default foreground
    {
      CLONEATTRS;
      GdkColor fg_col;
      gdk_color_parse(sgrcmd == 39 ? default_fg : col_spec[sgrcmd - 30], &fg_col);
      ADDATTR(pango_attr_foreground_new(
          fg_col.red * 256, fg_col.green * 256, fg_col.blue * 256)
        );
    }
    break;

  case 40: case 41: case 42: case 43:
  case 44: case 45: case 46: case 47: // Background colour
  case 49: // Default background
    gdk_color_parse(sgrcmd == 49 ? default_bg : col_spec[sgrcmd - 40], &pen->bg_col);
    break;

  default:
    return 0;
  }

  return 1;
}

static ecma48_state_callbacks_t cb = {
  .putchar    = term_putchar,
  .movecursor = term_movecursor,
  .copycell   = term_copycell,
  .erase      = term_erase,
  .setpen     = term_setpen,
};

gboolean stdin_readable(GIOChannel *source, GIOCondition cond, gpointer data)
{
  char buffer[8192];

  size_t bytes = read(0, buffer, sizeof buffer);

  if(bytes == 0) {
    fprintf(stderr, "STDIN closed\n");
    exit(0);
  }
  if(bytes < 0) {
    fprintf(stderr, "read(STDIN) failed - %s\n", strerror(errno));
    exit(1);
  }

  write(master, buffer, bytes);

  return TRUE;
}

gboolean master_readable(GIOChannel *source, GIOCondition cond, gpointer data)
{
  char buffer[8192];

  size_t bytes = read(master, buffer, sizeof buffer);

  if(bytes == 0) {
    fprintf(stderr, "master closed\n");
    exit(0);
  }
  if(bytes < 0) {
    fprintf(stderr, "read(master) failed - %s\n", strerror(errno));
    exit(1);
  }

  ecma48_push_bytes(e48, buffer, bytes);

  return TRUE;
}

int main(int argc, char *argv[])
{
  gtk_init(&argc, &argv);

  struct winsize size = { 25, 80, 0, 0 };

  e48 = ecma48_new();
  ecma48_set_size(e48, size.ws_row, size.ws_col);

  ecma48_set_state_callbacks(e48, &cb);

  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  termwin = window;

  g_signal_connect(G_OBJECT(window), "expose-event", GTK_SIGNAL_FUNC(term_expose), NULL);

  PangoContext *pctx = gtk_widget_get_pango_context(window);

  cells = g_new0(term_cell*, size.ws_row);

  int row;
  for(row = 0; row < size.ws_row; row++) {
    cells[row] = g_new0(term_cell, size.ws_col);

    int col;
    for(col = 0; col < size.ws_col; col++) {
      cells[row][col].layout = pango_layout_new(pctx);
    }
  }

  PangoFontMetrics *metrics = pango_context_get_metrics(pctx,
      pango_context_get_font_description(pctx), pango_context_get_language(pctx));

  int width = (pango_font_metrics_get_approximate_char_width(metrics) + 
               pango_font_metrics_get_approximate_digit_width(metrics)) / 2;

  printf("Font metrics: ascent=%d descent=%d width=%d\n", 
      pango_font_metrics_get_ascent(metrics),
      pango_font_metrics_get_descent(metrics),
      width);

  int height = pango_font_metrics_get_ascent(metrics) + pango_font_metrics_get_descent(metrics);

  cell_width  = PANGO_PIXELS_CEIL(width);
  cell_height = PANGO_PIXELS_CEIL(height);

  gtk_widget_show_all(window);

  gtk_window_resize(GTK_WINDOW(window), 
      size.ws_col * cell_width, size.ws_row * cell_height);

  //gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  cursor_gc = gdk_gc_new(window->window);

  GdkColor col;
  gdk_color_parse("black", &col);
  gdk_gc_set_rgb_fg_color(cursor_gc, &col);

  ecma48_state_initialise(e48);

  pid_t kid = forkpty(&master, NULL, NULL, &size);
  if(kid == 0) {
    execvp(argv[1], argv + 1);
    fprintf(stderr, "Cannot exec(%s) - %s\n", argv[1], strerror(errno));
    _exit(1);
  }

  GIOChannel *gio_stdin = g_io_channel_unix_new(0);
  g_io_add_watch(gio_stdin, G_IO_IN, stdin_readable, NULL);

  GIOChannel *gio_master = g_io_channel_unix_new(master);
  g_io_add_watch(gio_master, G_IO_IN, master_readable, NULL);

  gtk_main();

  return 0;
}
