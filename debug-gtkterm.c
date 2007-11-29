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

typedef struct {
  GtkWidget *label;
} term_cell;

term_cell **cells;

int term_putchar(ecma48_t *e48, uint32_t codepoint, ecma48_position_t pos, void *pen)
{
  char str[2] = {codepoint, 0};
  gtk_label_set_text(GTK_LABEL(cells[pos.row][pos.col].label), str);

  return 1;
}

int term_movecursor(ecma48_t *e48, ecma48_position_t pos, ecma48_position_t oldpos)
{
  // TODO: Need to find some way to display this information
  printf("Cursor is now at (%d,%d)\n", pos.col, pos.row);

  return 1;
}

// This function is currently unused but retained for historic interest
int term_scroll(ecma48_t *e48, ecma48_rectangle_t rect, int downward, int rightward)
{
  int init_row, test_row, init_col, test_col;
  int inc_row, inc_col;

  if(downward < 0) {
    init_row = rect.end_row - 1;
    test_row = rect.start_row - downward;
    inc_row = -1;
  }
  else if (downward == 0) {
    init_row = rect.start_row;
    test_row = rect.end_row;
    inc_row = +1;
  }
  else {
    init_row = rect.start_row + downward;
    test_row = rect.end_row - 1;
    inc_row = +1;
  }

  if(rightward < 0) {
    init_col = rect.end_col - 1;
    test_col = rect.start_col - rightward;
    inc_col = -1;
  }
  else if (rightward == 0) {
    init_col = rect.start_col;
    test_col = rect.end_col;
    inc_col = +1;
  }
  else {
    init_col = rect.start_col + rightward;
    test_col = rect.end_col - 1;
    inc_col = +1;
  }

  int row, col;
  for(row = init_row; row != test_row; row += inc_row)
    for(col = init_col; col != test_col; col += inc_col) {
      GtkWidget *dest = cells[row][col].label;
      GtkWidget *src  = cells[row+downward][col+rightward].label;

      const char *text = gtk_label_get_text(GTK_LABEL(src));
      gtk_label_set_text(GTK_LABEL(dest), text);
    }

  return 1;
}

int term_copycell(ecma48_t *e48, ecma48_position_t destpos, ecma48_position_t srcpos)
{
  GtkWidget *dest = cells[destpos.row][destpos.col].label;
  GtkWidget *src  = cells[srcpos.row][srcpos.col].label;

  const char *text = gtk_label_get_text(GTK_LABEL(src));
  gtk_label_set_text(GTK_LABEL(dest), text);

  return 1;
}

int term_erase(ecma48_t *e48, ecma48_rectangle_t rect, void *pen)
{
  int row, col;
  for(row = rect.start_row; row < rect.end_row; row++)
    for(col = rect.start_col; col < rect.end_col; col++) {
      GtkWidget *dest = cells[row][col].label;

      gtk_label_set_text(GTK_LABEL(dest), "");
    }

  return 1;
}

static ecma48_state_callbacks_t cb = {
  .putchar    = term_putchar,
  .movecursor = term_movecursor,
  // .scroll     = term_scroll,
  .copycell   = term_copycell,
  .erase      = term_erase,
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

  cells = g_new0(term_cell*, size.ws_row);
  GtkWidget *table = gtk_table_new(size.ws_row, size.ws_col, TRUE);

  int row;
  for(row = 0; row < size.ws_row; row++) {
    cells[row] = g_new0(term_cell, size.ws_col);

    int col;
    for(col = 0; col < size.ws_col; col++) {
      GtkWidget *label = gtk_label_new("");
      cells[row][col].label = label;

      gtk_table_attach_defaults(GTK_TABLE(table), label,
          col, col + 1, row, row + 1);
    }
  }

  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_container_add(GTK_CONTAINER(window), table);

  gtk_widget_show_all(window);

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
