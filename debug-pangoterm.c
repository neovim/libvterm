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
#include <gdk/gdkkeysyms.h>

#ifdef DEBUG
# define DEBUG_PRINT_INPUT
#endif

int master;
ecma48_t *e48;

int cell_width;
int cell_height;

GdkRectangle invalid_area;
int cursor_visible;
GdkRectangle cursor_area;

GtkWidget *termwin;
GdkPixmap *termbuffer;

GdkGC *cursor_gc;

typedef struct {
  GdkColor fg_col;
  GdkColor bg_col;
} term_cell;

term_cell **cells;

const char *default_fg = "gray90";
const char *default_bg = "black";

const char *cursor_col = "white";

const char *default_font = "Leonine Sans Mono";
const int default_size = 9;

PangoFontDescription *fontdesc;

GtkIMContext *im_context;

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
  GdkColor fg_col;
  GdkColor bg_col;
  gboolean reverse;
  PangoAttrList *attrs;
  PangoLayout *layout;
} term_pen;

ecma48_key convert_keyval(guint gdk_keyval)
{
  switch(gdk_keyval) {
  case GDK_BackSpace:
    return ECMA48_KEY_BACKSPACE;
  case GDK_Tab:
    return ECMA48_KEY_TAB;
  case GDK_Return:
    return ECMA48_KEY_ENTER;
  case GDK_Escape:
    return ECMA48_KEY_ESCAPE;

  case GDK_Up:
    return ECMA48_KEY_UP;
  case GDK_Down:
    return ECMA48_KEY_DOWN;
  case GDK_Left:
    return ECMA48_KEY_LEFT;
  case GDK_Right:
    return ECMA48_KEY_RIGHT;

  case GDK_Insert:
    return ECMA48_KEY_INS;
  case GDK_Delete:
    return ECMA48_KEY_DEL;
  case GDK_Home:
    return ECMA48_KEY_HOME;
  case GDK_End:
    return ECMA48_KEY_END;
  case GDK_Page_Up:
    return ECMA48_KEY_PAGEUP;
  case GDK_Page_Down:
    return ECMA48_KEY_PAGEDOWN;

  default:
    return ECMA48_KEY_NONE;
  }
}

void repaint_area(GdkRectangle *area)
{
  GdkWindow *win = termwin->window;

  GdkGC *gc = gdk_gc_new(win);

  gdk_gc_set_clip_rectangle(gc, area);

  gdk_draw_drawable(win,
      gc,
      termbuffer,
      0, 0, 0, 0, -1, -1);

  g_object_unref(G_OBJECT(gc));
}

gboolean term_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
  repaint_area(&event->area);

  if(cursor_visible && gdk_rectangle_intersect(&cursor_area, &event->area, NULL))
    gdk_draw_rectangle(termwin->window,
        cursor_gc,
        FALSE,
        cursor_area.x,
        cursor_area.y,
        cursor_area.width - 1,
        cursor_area.height - 1);

  return TRUE;
}

gboolean term_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  gboolean ret = gtk_im_context_filter_keypress(im_context, event);

  if(ret)
    return TRUE;

  // We don't need to track the state of modifier bits
  if(event->is_modifier)
    return FALSE;

  ecma48_mod state = ECMA48_MOD_NONE;
  if(event->state & GDK_SHIFT_MASK)
    state |= ECMA48_MOD_SHIFT;
  if(event->state & GDK_CONTROL_MASK)
    state |= ECMA48_MOD_CTRL;
  if(event->state & GDK_MOD1_MASK)
    state |= ECMA48_MOD_ALT;

  ecma48_key keyval = convert_keyval(event->keyval);

  if(keyval)
    ecma48_input_push_key(e48, state, keyval);
  else {
    size_t len = strlen(event->string);
    if(len)
      ecma48_input_push_str(e48, state, event->string, len);
    else
      printf("Unsure how to handle key %d with no string\n", event->keyval);
  }

  size_t bufflen = ecma48_output_bufferlen(e48);
  if(bufflen) {
    char buffer[bufflen];
    bufflen = ecma48_output_bufferread(e48, buffer, bufflen);
    write(master, buffer, bufflen);
  }

  return FALSE;
}

gboolean im_commit(GtkIMContext *context, gchar *str, gpointer user_data)
{
  ecma48_input_push_str(e48, 0, str, strlen(str));

  size_t bufflen = ecma48_output_bufferlen(e48);
  if(bufflen) {
    char buffer[bufflen];
    bufflen = ecma48_output_bufferread(e48, buffer, bufflen);
    write(master, buffer, bufflen);
  }

  return FALSE;
}

int term_putchar(ecma48_t *e48, uint32_t codepoint, ecma48_position_t pos, void *pen_p)
{
  term_pen *pen = pen_p;

  char *s = g_ucs4_to_utf8(&codepoint, 1, NULL, NULL, NULL);
  PangoLayout *layout = pen->layout;

  pango_layout_set_text(layout, s, -1);

  g_free(s);

  if(pen->attrs)
    pango_layout_set_attributes(layout, pen->attrs);

  GdkColor fg = cells[pos.row][pos.col].fg_col = pen->reverse ? pen->bg_col : pen->fg_col;
  GdkColor bg = cells[pos.row][pos.col].bg_col = pen->reverse ? pen->fg_col : pen->bg_col;

  GdkGC *gc = gdk_gc_new(termbuffer);

  gdk_gc_set_rgb_fg_color(gc, &bg);

  GdkRectangle destarea = {
    .x      = pos.col * cell_width,
    .y      = pos.row * cell_height,
    .width  = cell_width,
    .height = cell_height
  };

  gdk_draw_rectangle(termbuffer,
      gc,
      TRUE,
      destarea.x,
      destarea.y,
      destarea.width,
      destarea.height);

  if(layout) {
    gdk_draw_layout_with_colors(termbuffer,
        gc,
        destarea.x,
        destarea.y,
        layout,
        &fg,
        NULL);
  }

  g_object_unref(G_OBJECT(gc));

  gdk_rectangle_union(&destarea, &invalid_area, &invalid_area);

  return 1;
}

int term_movecursor(ecma48_t *e48, ecma48_position_t pos, ecma48_position_t oldpos, int visible)
{
  GdkRectangle destarea = {
    .x      = oldpos.col * cell_width,
    .y      = oldpos.row * cell_height,
    .width  = cell_width,
    .height = cell_height
  };

  gdk_rectangle_union(&destarea, &invalid_area, &invalid_area);

  cursor_area.x      = pos.col * cell_width;
  cursor_area.y      = pos.row * cell_height;
  cursor_area.width  = cell_width;
  cursor_area.height = cell_height;

  cursor_visible = visible;

  return 1;
}

int term_scroll(ecma48_t *e48, ecma48_rectangle_t rect, int downward, int rightward)
{
  GdkGC *gc = gdk_gc_new(termbuffer);

  int rows = rect.end_row - rect.start_row - downward;
  int cols = rect.end_col - rect.start_col - rightward;

  GdkRectangle destarea = {
    .x      = rect.start_col * cell_width,
    .y      = rect.start_row * cell_height,
    .width  = cols * cell_width,
    .height = rows * cell_height
  };

  gdk_draw_drawable(termbuffer,
      gc,
      termbuffer,
      (rect.start_col + rightward) * cell_width,
      (rect.start_row + downward ) * cell_height,
      destarea.x,
      destarea.y,
      destarea.width,
      destarea.height);

  gdk_rectangle_union(&destarea, &invalid_area, &invalid_area);

  return 0; // Because we still need to get copycell to move the metadata
}

int term_copycell(ecma48_t *e48, ecma48_position_t destpos, ecma48_position_t srcpos)
{
  cells[destpos.row][destpos.col].fg_col = cells[srcpos.row][srcpos.col].fg_col;
  cells[destpos.row][destpos.col].bg_col = cells[srcpos.row][srcpos.col].bg_col;

  return 1;
}

int term_erase(ecma48_t *e48, ecma48_rectangle_t rect, void *pen_p)
{
  term_pen *pen = pen_p;

  GdkGC *gc = gdk_gc_new(termbuffer);

  GdkColor bg = pen->reverse ? pen->fg_col : pen->bg_col;
  gdk_gc_set_rgb_fg_color(gc, &bg);

  int row, col;
  for(row = rect.start_row; row < rect.end_row; row++)
    for(col = rect.start_col; col < rect.end_col; col++) {
      cells[row][col].bg_col = bg;
    }

  GdkRectangle destarea = {
    .x      = rect.start_col * cell_width,
    .y      = rect.start_row * cell_height,
    .width  = (rect.end_col - rect.start_col) * cell_width,
    .height = (rect.end_row - rect.start_row) * cell_height,
  };

  gdk_draw_rectangle(termbuffer,
      gc,
      TRUE,
      destarea.x,
      destarea.y,
      destarea.width,
      destarea.height);

  g_object_unref(G_OBJECT(gc));

  gdk_rectangle_union(&destarea, &invalid_area, &invalid_area);

  return 1;
}

int term_setpen(ecma48_t *e48, int sgrcmd, void **penstore)
{
  term_pen *pen = *penstore;

  if(!*penstore) {
    pen = g_new0(term_pen, 1);
    *penstore = pen;
    pen->attrs = pango_attr_list_new();
    pen->layout = pango_layout_new(gtk_widget_get_pango_context(termwin));
    pango_layout_set_font_description(pen->layout, fontdesc);
    gdk_color_parse(default_fg, &pen->fg_col);
    gdk_color_parse(default_bg, &pen->bg_col);
    return 1;
  }

  return 0;
}

static void lookup_colour(int palette, int index, const char *def, GdkColor *col)
{
  switch(palette) {
  case 0:
    if(index == -1)
      gdk_color_parse(def,col);
    else if(index >= 0 && index < 8)
      gdk_color_parse(col_spec[index], col);
    break;

  case 5: // XTerm 256-colour mode
    if(index >= 0 && index < 16)
      // Normal 16 colours
      // TODO: support low/high intensities
      gdk_color_parse(col_spec[index % 8], col);
    else if(index >= 16 && index < 232) {
      // 216-colour cube
      index -= 16;

      col->blue  = (index     % 6) * (0xffff/6);
      col->green = (index/6   % 6) * (0xffff/6);
      col->red   = (index/6/6 % 6) * (0xffff/6);
    }
    else if(index >= 232 && index < 256) {
      // 24 greyscales
      index -= 232;

      col->blue  = index * 0xffff / 24;
      col->green = index * 0xffff / 24;
      col->red   = index * 0xffff / 24;
    }
    break;

  default:
    printf("Unrecognised colour palette %d\n", palette);
  }
}

int term_setpenattr(ecma48_t *e48, ecma48_attr attr, ecma48_attrvalue *val, void **penstore)
{
#define ADDATTR(a) \
  do { \
    PangoAttribute *newattr = (a); \
    newattr->start_index = 0; \
    newattr->end_index = 100; /* can't really know for sure */ \
    pango_attr_list_change(pen->attrs, newattr); \
  } while(0)

  term_pen *pen = *penstore;

  switch(attr) {
  case ECMA48_ATTR_BOLD:
    ADDATTR(pango_attr_weight_new(val->boolean ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL));
    break;

  case ECMA48_ATTR_UNDERLINE:
    ADDATTR(pango_attr_underline_new(val->value == 1 ? PANGO_UNDERLINE_SINGLE :
                                     val->value == 2 ? PANGO_UNDERLINE_DOUBLE :
                                                      PANGO_UNDERLINE_NONE));
    break;

  case ECMA48_ATTR_REVERSE:
    pen->reverse = val->boolean;
    break;

  case ECMA48_ATTR_FOREGROUND:
    lookup_colour(val->color.palette, val->color.index, default_fg, &pen->fg_col);
    break;

  case ECMA48_ATTR_BACKGROUND:
    lookup_colour(val->color.palette, val->color.index, default_bg, &pen->bg_col);
    break;

  default:
    return 0;
  }

  return 1;
}

static ecma48_state_callbacks_t cb = {
  .putchar    = term_putchar,
  .movecursor = term_movecursor,
  .scroll     = term_scroll,
  .copycell   = term_copycell,
  .erase      = term_erase,
  .setpen     = term_setpen,
  .setpenattr = term_setpenattr,
};

gboolean master_readable(GIOChannel *source, GIOCondition cond, gpointer data)
{
  char buffer[8192];

  ssize_t bytes = read(master, buffer, sizeof buffer);

  if(bytes == 0) {
    fprintf(stderr, "master closed\n");
    exit(0);
  }
  if(bytes < 0) {
    fprintf(stderr, "read(master) failed - %s\n", strerror(errno));
    exit(1);
  }

#ifdef DEBUG_PRINT_INPUT
  printf("Read %d bytes from master:\n", bytes);
  int i;
  for(i = 0; i < bytes; i++) {
    printf(i % 16 == 0 ? " |  %02x" : " %02x", buffer[i]);
    if(i % 16 == 15)
      printf("\n");
  }
  if(i % 16)
    printf("\n");
#endif

  invalid_area.x = 0;
  invalid_area.y = 0;
  invalid_area.width = 0;
  invalid_area.height = 0;

  ecma48_push_bytes(e48, buffer, bytes);

  if(invalid_area.width && invalid_area.height)
    repaint_area(&invalid_area);

  if(cursor_visible)
    gdk_draw_rectangle(termwin->window,
        cursor_gc,
        FALSE,
        cursor_area.x,
        cursor_area.y,
        cursor_area.width - 1,
        cursor_area.height - 1);

  return TRUE;
}

int main(int argc, char *argv[])
{
  gtk_init(&argc, &argv);

  struct winsize size = { 25, 80, 0, 0 };

  e48 = ecma48_new();
  ecma48_parser_set_utf8(e48, 1);
  ecma48_set_size(e48, size.ws_row, size.ws_col);

  ecma48_set_state_callbacks(e48, &cb);

  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  termwin = window;

  g_signal_connect(G_OBJECT(window), "expose-event", GTK_SIGNAL_FUNC(term_expose), NULL);
  g_signal_connect(G_OBJECT(window), "key-press-event", GTK_SIGNAL_FUNC(term_keypress), NULL);

  im_context = gtk_im_context_simple_new();

  g_signal_connect(G_OBJECT(im_context), "commit", GTK_SIGNAL_FUNC(im_commit), NULL);

  PangoContext *pctx = gtk_widget_get_pango_context(window);

  fontdesc = pango_font_description_new();
  pango_font_description_set_family(fontdesc, default_font);
  pango_font_description_set_size(fontdesc, default_size * PANGO_SCALE);

  cells = g_new0(term_cell*, size.ws_row);

  int row;
  for(row = 0; row < size.ws_row; row++) {
    cells[row] = g_new0(term_cell, size.ws_col);
  }

  PangoFontMetrics *metrics = pango_context_get_metrics(pctx,
      pango_context_get_font_description(pctx), pango_context_get_language(pctx));

  int width = (pango_font_metrics_get_approximate_char_width(metrics) + 
               pango_font_metrics_get_approximate_digit_width(metrics)) / 2;

  int height = pango_font_metrics_get_ascent(metrics) + pango_font_metrics_get_descent(metrics);

  cell_width  = PANGO_PIXELS_CEIL(width);
  cell_height = PANGO_PIXELS_CEIL(height) + 1;

  gtk_widget_show_all(window);

  termbuffer = gdk_pixmap_new(window->window,
      size.ws_col * cell_width, size.ws_row * cell_height, -1);

  gtk_window_resize(GTK_WINDOW(window), 
      size.ws_col * cell_width, size.ws_row * cell_height);

  //gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  cursor_gc = gdk_gc_new(window->window);

  GdkColor col;
  gdk_color_parse(cursor_col, &col);
  gdk_gc_set_rgb_fg_color(cursor_gc, &col);

  ecma48_state_initialise(e48);

  pid_t kid = forkpty(&master, NULL, NULL, &size);
  if(kid == 0) {
    execvp(argv[1], argv + 1);
    fprintf(stderr, "Cannot exec(%s) - %s\n", argv[1], strerror(errno));
    _exit(1);
  }

  GIOChannel *gio_master = g_io_channel_unix_new(master);
  g_io_add_watch(gio_master, G_IO_IN|G_IO_HUP, master_readable, NULL);

  gtk_main();

  return 0;
}
