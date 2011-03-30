#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

/* suck up the non-standard openpty/forkpty */
#if defined(__FreeBSD__)
# include <libutil.h>
# include <termios.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__)
# include <termios.h>
# include <util.h>
#else
# include <pty.h>
#endif

#include "vterm.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#ifdef DEBUG
# define DEBUG_PRINT_INPUT
#endif

int master;
VTerm *vt;

int cell_width_pango;
int cell_width;
int cell_height;

GdkRectangle invalid_area;
int cursor_visible;
GdkRectangle cursor_area;

guint cursor_timer_id;

GtkWidget *termwin;

// Actual stores of Pixmaps
GdkPixmap *termbuffer_main;
GdkPixmap *termbuffer_alternate;

// This always points at one of the above
GdkPixmap *termbuffer;
GdkGC *termbuffer_gc;

GdkGC *cursor_gc;

typedef struct {
  GdkColor fg_col;
  GdkColor bg_col;
} term_cell;

term_cell **cells;

static char *default_fg = "gray90";
static char *default_bg = "black";

static char *cursor_col = "white";
static gint cursor_blink_interval = 500;

static char *default_font = "DejaVu Sans Mono";
static int default_size = 9;

static char *alt_fonts[] = {
  "Courier 10 Pitch",
};

static GOptionEntry option_entries[] = {
  /* long_name, short_name, flags, arg, arg_data, description, arg_description */
  { "foreground", 0,   0, G_OPTION_ARG_STRING, &default_fg, "Default foreground colour", "COL" },
  { "background", 0,   0, G_OPTION_ARG_STRING, &default_bg, "Default background colour", "COL" },
  { "cursor",     0,   0, G_OPTION_ARG_STRING, &cursor_col, "Cursor colour", "COL" },

  { "font",       0,   0, G_OPTION_ARG_STRING, &default_font, "Font name", "FONT" },
  { "size",       's', 0, G_OPTION_ARG_INT,    &default_size, "Font size", "INT" },

  { NULL },
};

PangoFontDescription *fontdesc;

GtkIMContext *im_context;

VTermMouseFunc mousefunc;
void *mousedata;

typedef struct {
  GdkColor fg_col;
  GdkColor bg_col;
  gboolean reverse;
  PangoAttrList *attrs;
  PangoLayout *layout;
} term_pen;

GString *glyphs = NULL;
GArray *glyph_widths = NULL;
GdkRectangle glyph_area;
term_pen *glyph_pen;

VTermKey convert_keyval(guint gdk_keyval)
{
  if(gdk_keyval >= GDK_F1 && gdk_keyval <= GDK_F35)
    return VTERM_KEY_FUNCTION(gdk_keyval - GDK_F1 + 1);

  switch(gdk_keyval) {
  case GDK_BackSpace:
    return VTERM_KEY_BACKSPACE;
  case GDK_Tab:
    return VTERM_KEY_TAB;
  case GDK_Return:
    return VTERM_KEY_ENTER;
  case GDK_Escape:
    return VTERM_KEY_ESCAPE;

  case GDK_Up:
    return VTERM_KEY_UP;
  case GDK_Down:
    return VTERM_KEY_DOWN;
  case GDK_Left:
    return VTERM_KEY_LEFT;
  case GDK_Right:
    return VTERM_KEY_RIGHT;

  case GDK_Insert:
    return VTERM_KEY_INS;
  case GDK_Delete:
    return VTERM_KEY_DEL;
  case GDK_Home:
    return VTERM_KEY_HOME;
  case GDK_End:
    return VTERM_KEY_END;
  case GDK_Page_Up:
    return VTERM_KEY_PAGEUP;
  case GDK_Page_Down:
    return VTERM_KEY_PAGEDOWN;

  default:
    return VTERM_KEY_NONE;
  }
}

static void update_termbuffer(void)
{
  if(termbuffer_gc) {
    g_object_unref(termbuffer_gc);
    termbuffer_gc = NULL;
  }

  if(termbuffer)
    termbuffer_gc = gdk_gc_new(termbuffer);
}

static void add_glyph(const uint32_t chars[], int width)
{
  char *chars_str = g_ucs4_to_utf8(chars, -1, NULL, NULL, NULL);

  g_array_set_size(glyph_widths, glyphs->len + 1);
  g_array_index(glyph_widths, int, glyphs->len) = width;

  g_string_append(glyphs, chars_str);

  g_free(chars_str);

  return;
}

static void flush_glyphs(void)
{
  if(!glyphs->len) {
    glyph_area.width = 0;
    glyph_area.height = 0;
    return;
  }

  gdk_gc_set_clip_rectangle(termbuffer_gc, &glyph_area);

  PangoLayout *layout = glyph_pen->layout;

  pango_layout_set_text(layout, glyphs->str, glyphs->len);

  if(glyph_pen->attrs)
    pango_layout_set_attributes(layout, glyph_pen->attrs);

  // Now adjust all the widths
  PangoLayoutIter *iter = pango_layout_get_iter(layout);
  do {
    PangoLayoutRun *run = pango_layout_iter_get_run(iter);
    if(!run)
      continue;

    PangoGlyphString *glyph_str = run->glyphs;
    int i;
    for(i = 0; i < glyph_str->num_glyphs; i++) {
      PangoGlyphInfo *glyph = &glyph_str->glyphs[i];
      int str_index = run->item->offset + glyph_str->log_clusters[i];
      int char_width = g_array_index(glyph_widths, int, str_index);
      if(glyph->geometry.width && glyph->geometry.width != char_width * cell_width_pango) {
        /* Adjust its x_offset to match the width change, to ensure it still
         * remains centered in the cell */
        glyph->geometry.x_offset -= (glyph->geometry.width - char_width * cell_width_pango) / 2;
        glyph->geometry.width = char_width * cell_width_pango;
      }
    }
  } while(pango_layout_iter_next_run(iter));

  pango_layout_iter_free(iter);

  gdk_draw_layout_with_colors(termbuffer,
      termbuffer_gc,
      glyph_area.x,
      glyph_area.y,
      layout,
      glyph_pen->reverse ? &glyph_pen->bg_col : &glyph_pen->fg_col,
      NULL);

  gdk_rectangle_union(&glyph_area, &invalid_area, &invalid_area);

  glyph_area.width = 0;
  glyph_area.height = 0;

  g_string_truncate(glyphs, 0);
}

void repaint_area(GdkRectangle *area)
{
  GdkWindow *win = termwin->window;

  gdk_gc_set_clip_rectangle(termbuffer_gc, area);

  gdk_draw_drawable(win,
      termbuffer_gc,
      termbuffer,
      0, 0, 0, 0, -1, -1);
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

  VTermModifier state = VTERM_MOD_NONE;
  if(event->state & GDK_SHIFT_MASK)
    state |= VTERM_MOD_SHIFT;
  if(event->state & GDK_CONTROL_MASK)
    state |= VTERM_MOD_CTRL;
  if(event->state & GDK_MOD1_MASK)
    state |= VTERM_MOD_ALT;

  VTermKey keyval = convert_keyval(event->keyval);

  if(keyval)
    vterm_input_push_key(vt, state, keyval);
  else {
    size_t len = strlen(event->string);
    if(len)
      vterm_input_push_str(vt, state, event->string, len);
    else
      printf("Unsure how to handle key %d with no string\n", event->keyval);
  }

  size_t bufflen = vterm_output_bufferlen(vt);
  if(bufflen) {
    char buffer[bufflen];
    bufflen = vterm_output_bufferread(vt, buffer, bufflen);
    write(master, buffer, bufflen);
  }

  return FALSE;
}

gboolean term_mousepress(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  if(!mousefunc)
    return FALSE;

  (*mousefunc)(event->x / cell_width, event->y / cell_height, event->button, event->type == GDK_BUTTON_PRESS, mousedata);

  size_t bufflen = vterm_output_bufferlen(vt);
  if(bufflen) {
    char buffer[bufflen];
    bufflen = vterm_output_bufferread(vt, buffer, bufflen);
    write(master, buffer, bufflen);
  }

  return FALSE;
}

gboolean im_commit(GtkIMContext *context, gchar *str, gpointer user_data)
{
  vterm_input_push_str(vt, 0, str, strlen(str));

  size_t bufflen = vterm_output_bufferlen(vt);
  if(bufflen) {
    char buffer[bufflen];
    bufflen = vterm_output_bufferread(vt, buffer, bufflen);
    write(master, buffer, bufflen);
  }

  return FALSE;
}

int term_putglyph(const uint32_t chars[], int width, VTermPos pos, void *user)
{
  term_pen *pen = user;

  cells[pos.row][pos.col].fg_col = pen->reverse ? pen->bg_col : pen->fg_col;
  GdkColor bg = cells[pos.row][pos.col].bg_col = pen->reverse ? pen->fg_col : pen->bg_col;

  gdk_gc_set_rgb_fg_color(termbuffer_gc, &bg);

  GdkRectangle destarea = {
    .x      = pos.col * cell_width,
    .y      = pos.row * cell_height,
    .width  = cell_width * width,
    .height = cell_height
  };

  if(destarea.y != glyph_area.y || destarea.x != glyph_area.x + glyph_area.width)
    flush_glyphs();

  gdk_gc_set_clip_rectangle(termbuffer_gc, &destarea);

  gdk_draw_rectangle(termbuffer,
      termbuffer_gc,
      TRUE,
      destarea.x,
      destarea.y,
      destarea.width,
      destarea.height);

  add_glyph(chars, width);
  glyph_pen = pen;

  if(glyph_area.width && glyph_area.height)
    gdk_rectangle_union(&destarea, &glyph_area, &glyph_area);
  else
    glyph_area = destarea;

  return 1;
}

int term_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
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

  cursor_visible = visible;

  return 1;
}

gboolean cursor_blink(gpointer data)
{
  invalid_area.x = 0;
  invalid_area.y = 0;
  invalid_area.width = 0;
  invalid_area.height = 0;

  cursor_visible = !cursor_visible;
  gdk_rectangle_union(&cursor_area, &invalid_area, &invalid_area);

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

int term_copyrect(VTermRect dest, VTermRect src, void *user)
{
  flush_glyphs();

  GdkRectangle destarea = {
    .x      = dest.start_col * cell_width,
    .y      = dest.start_row * cell_height,
    .width  = (dest.end_col - dest.start_col) * cell_width,
    .height = (dest.end_row - dest.start_row) * cell_height,
  };

  gdk_gc_set_clip_rectangle(termbuffer_gc, &destarea);

  gdk_draw_drawable(termbuffer,
      termbuffer_gc,
      termbuffer,
      src.start_col * cell_width,
      src.start_row * cell_height,
      destarea.x,
      destarea.y,
      destarea.width,
      destarea.height);

  gdk_rectangle_union(&destarea, &invalid_area, &invalid_area);

  return 0; // Because we still need to get copycell to move the metadata
}

int term_copycell(VTermPos destpos, VTermPos srcpos, void *user)
{
  cells[destpos.row][destpos.col].fg_col = cells[srcpos.row][srcpos.col].fg_col;
  cells[destpos.row][destpos.col].bg_col = cells[srcpos.row][srcpos.col].bg_col;

  return 1;
}

int term_erase(VTermRect rect, void *user)
{
  flush_glyphs();

  term_pen *pen = user;

  GdkColor bg = pen->reverse ? pen->fg_col : pen->bg_col;
  gdk_gc_set_rgb_fg_color(termbuffer_gc, &bg);

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

  gdk_gc_set_clip_rectangle(termbuffer_gc, &destarea);

  gdk_draw_rectangle(termbuffer,
      termbuffer_gc,
      TRUE,
      destarea.x,
      destarea.y,
      destarea.width,
      destarea.height);

  gdk_rectangle_union(&destarea, &invalid_area, &invalid_area);

  return 1;
}

int term_setpenattr(VTermAttr attr, VTermValueType type, VTermValue *val, void *user)
{
  flush_glyphs();

#define ADDATTR(a) \
  do { \
    PangoAttribute *newattr = (a); \
    newattr->start_index = 0; \
    newattr->end_index = -1; \
    pango_attr_list_change(pen->attrs, newattr); \
  } while(0)

  term_pen *pen = user;

  switch(attr) {
  case VTERM_ATTR_BOLD:
    ADDATTR(pango_attr_weight_new(val->boolean ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL));
    break;

  case VTERM_ATTR_UNDERLINE:
    ADDATTR(pango_attr_underline_new(val->number == 1 ? PANGO_UNDERLINE_SINGLE :
                                     val->number == 2 ? PANGO_UNDERLINE_DOUBLE :
                                                        PANGO_UNDERLINE_NONE));
    break;

  case VTERM_ATTR_ITALIC:
    ADDATTR(pango_attr_style_new(val->boolean ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL));
    break;

  case VTERM_ATTR_REVERSE:
    pen->reverse = val->boolean;
    break;

  case VTERM_ATTR_FONT:
    if(val->number == 0 || val->number > sizeof(alt_fonts)/sizeof(alt_fonts[0]))
      ADDATTR(pango_attr_family_new(default_font));
    else
      ADDATTR(pango_attr_family_new(alt_fonts[val->number - 1]));
    break;

  case VTERM_ATTR_FOREGROUND:
    // Upscale 8->16bit
    pen->fg_col.red   = 257 * val->color.red;
    pen->fg_col.green = 257 * val->color.green;
    pen->fg_col.blue  = 257 * val->color.blue;
    break;

  case VTERM_ATTR_BACKGROUND:
    // Upscale 8->16bit
    pen->bg_col.red   = 257 * val->color.red;
    pen->bg_col.green = 257 * val->color.green;
    pen->bg_col.blue  = 257 * val->color.blue;
    break;

  default:
    return 0;
  }

  return 1;
}

int term_settermprop(VTermProp prop, VTermValueType type, VTermValue *val, void *user)
{
  switch(prop) {
  case VTERM_PROP_CURSORVISIBLE:
    cursor_visible = val->boolean;
    gdk_rectangle_union(&cursor_area, &invalid_area, &invalid_area);
    break;

  case VTERM_PROP_CURSORBLINK:
    if(val->boolean) {
      cursor_timer_id = g_timeout_add(cursor_blink_interval, cursor_blink, NULL);
    }
    else {
      g_source_remove(cursor_timer_id);
    }
    break;

  case VTERM_PROP_ALTSCREEN:
    {
      int rows, cols;
      vterm_get_size(vt, &rows, &cols);

      GdkRectangle rect = {
        .x = 0,
        .y = 0,
        .width  = cols * cell_width,
        .height = rows * cell_height,
      };

      termbuffer = val->boolean ? termbuffer_alternate : termbuffer_main;
      update_termbuffer();

      gdk_rectangle_union(&rect, &invalid_area, &invalid_area);
    }
    break;

  case VTERM_PROP_ICONNAME:
    gdk_window_set_icon_name(GDK_WINDOW(termwin->window), val->string);
    break;

  case VTERM_PROP_TITLE:
    gtk_window_set_title(GTK_WINDOW(termwin), val->string);
    break;

  default:
    return 0;
  }

  return 1;
}

int term_setmousefunc(VTermMouseFunc func, void *data, void *user)
{
  mousefunc = func;
  mousedata = data;

  GdkEventMask mask = gdk_window_get_events(termwin->window);

  if(func)
    mask |= GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK;
  else
    mask &= ~(GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

  gdk_window_set_events(termwin->window, mask);

  return 1;
}

int term_bell(void *user)
{
  gtk_widget_error_bell(GTK_WIDGET(termwin));
  return 1;
}

static VTermStateCallbacks cb = {
  .putglyph     = term_putglyph,
  .movecursor   = term_movecursor,
  .copyrect     = term_copyrect,
  .copycell     = term_copycell,
  .erase        = term_erase,
  .setpenattr   = term_setpenattr,
  .settermprop  = term_settermprop,
  .setmousefunc = term_setmousefunc,
  .bell         = term_bell,
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
  printf("Read %zd bytes from master:\n", bytes);
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

  vterm_push_bytes(vt, buffer, bytes);

  flush_glyphs();

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
  GError *args_error = NULL;
  GOptionContext *args_context;

  args_context = g_option_context_new("commandline...");
  g_option_context_add_main_entries(args_context, option_entries, NULL);
  g_option_context_add_group(args_context, gtk_get_option_group(TRUE));
  if(!g_option_context_parse(args_context, &argc, &argv, &args_error)) {
    fprintf(stderr, "Option parsing failed: %s\n", args_error->message);
    exit (1);
  }

  gtk_init(&argc, &argv);

  struct winsize size = { 25, 80, 0, 0 };

  vt = vterm_new(size.ws_row, size.ws_col);
  vterm_parser_set_utf8(vt, 1);

  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  termwin = window;

  glyphs = g_string_sized_new(128);
  glyph_widths = g_array_new(FALSE, FALSE, sizeof(int));

  gtk_widget_realize(window);

  term_pen *pen = g_new0(term_pen, 1);
  pen->attrs = pango_attr_list_new();
  pen->layout = pango_layout_new(gtk_widget_get_pango_context(termwin));
  pango_layout_set_font_description(pen->layout, fontdesc);
  gdk_color_parse(default_fg, &pen->fg_col);
  gdk_color_parse(default_bg, &pen->bg_col);

  VTermState *vts = vterm_obtain_state(vt);
  vterm_state_set_callbacks(vts, &cb, pen);

  g_signal_connect(G_OBJECT(window), "expose-event", GTK_SIGNAL_FUNC(term_expose), NULL);
  g_signal_connect(G_OBJECT(window), "key-press-event", GTK_SIGNAL_FUNC(term_keypress), NULL);

  g_signal_connect(G_OBJECT(termwin), "button-press-event",   GTK_SIGNAL_FUNC(term_mousepress), NULL);
  g_signal_connect(G_OBJECT(termwin), "button-release-event", GTK_SIGNAL_FUNC(term_mousepress), NULL);

  im_context = gtk_im_context_simple_new();

  g_signal_connect(G_OBJECT(im_context), "commit", GTK_SIGNAL_FUNC(im_commit), NULL);

  PangoContext *pctx = gtk_widget_get_pango_context(window);

  fontdesc = pango_font_description_new();
  pango_font_description_set_family(fontdesc, default_font);
  pango_font_description_set_size(fontdesc, default_size * PANGO_SCALE);

  pango_context_set_font_description(pctx, fontdesc);

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

  cell_width_pango = width;
  cell_width  = PANGO_PIXELS_CEIL(width);
  cell_height = PANGO_PIXELS_CEIL(height);

  cursor_area.width  = cell_width;
  cursor_area.height = cell_height;

  termbuffer_main = gdk_pixmap_new(window->window,
      size.ws_col * cell_width, size.ws_row * cell_height, -1);
  termbuffer_alternate = gdk_pixmap_new(window->window,
      size.ws_col * cell_width, size.ws_row * cell_height, -1);

  termbuffer = termbuffer_main;
  update_termbuffer();

  gtk_window_resize(GTK_WINDOW(window), 
      size.ws_col * cell_width, size.ws_row * cell_height);

  //gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  cursor_gc = gdk_gc_new(window->window);

  GdkColor col;
  gdk_color_parse(cursor_col, &col);
  gdk_gc_set_rgb_fg_color(cursor_gc, &col);

  vterm_state_reset(vts);

  pid_t kid = forkpty(&master, NULL, NULL, &size);
  if(kid == 0) {
    if(argc > 1) {
      execvp(argv[1], argv + 1);
      fprintf(stderr, "Cannot exec(%s) - %s\n", argv[1], strerror(errno));
    }
    else {
      char *shell = getenv("SHELL");
      char *args[2] = { shell, NULL };
      execvp(shell, args);
      fprintf(stderr, "Cannot exec(%s) - %s\n", shell, strerror(errno));
    }
    _exit(1);
  }

  GIOChannel *gio_master = g_io_channel_unix_new(master);
  g_io_add_watch(gio_master, G_IO_IN|G_IO_HUP, master_readable, NULL);

  gtk_widget_show_all(window);

  gtk_main();

  return 0;
}
