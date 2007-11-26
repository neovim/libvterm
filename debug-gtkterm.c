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
ecma48_t *state;

GtkWidget ***cells;

int cur_col = 0;
int cur_row = 0;

int text(ecma48_t *state, char *s, size_t len)
{
  size_t pos;
  for(pos = 0; pos < len; pos++) {
    char c = s[pos];

    if(cur_col == 80) {
      cur_col = 1;
      cur_row++;
    }

    char str[2] = {c, 0};
    gtk_label_set_text(GTK_LABEL(cells[cur_row][cur_col]), str);

    cur_col++;
  }

  return 1;
}

int control(ecma48_t *state, char control)
{
  switch(control) {
  case 0x0a:
    cur_col = 0;
    break;
  case 0x0d:
    cur_row++;
    break;
  default:
    printf("Control function 0x%02x\n", control);
    break;
  }

  return 1;
}

static ecma48_parser_callbacks_t cb = {
  .text    = text,
  .control = control,
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

  ecma48_state_push_bytes(state, buffer, bytes);

  return TRUE;
}

int main(int argc, char *argv[])
{
  gtk_init(&argc, &argv);

  state = ecma48_state_new();
  ecma48_state_set_parser_callbacks(state, &cb);

  struct winsize size = { 25, 80, 0, 0 };

  cells = g_new0(GtkWidget**, size.ws_row);
  GtkWidget *table = gtk_table_new(size.ws_row, size.ws_col, TRUE);

  int row;
  for(row = 0; row < size.ws_row; row++) {
    cells[row] = g_new0(GtkWidget*, size.ws_col);

    int col;
    for(col = 0; col < size.ws_col; col++) {
      GtkWidget *label = gtk_label_new("");
      cells[row][col] = label;

      gtk_table_attach_defaults(GTK_TABLE(table), label,
          col, col + 1, row, row + 1);
    }
  }

  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_container_add(GTK_CONTAINER(window), table);

  gtk_widget_show_all(window);

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
