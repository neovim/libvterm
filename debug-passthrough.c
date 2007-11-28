#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>

#include "ecma48.h"

int master;
ecma48_t *e48;

int text(ecma48_t *e48, char *s, size_t len)
{
  printf("Wrote %d text: %.*s\n", len, len, s);
  return 1;
}

int control(ecma48_t *e48, char control)
{
  printf("Control function 0x%02x\n", control);
  return 1;
}

int escape(ecma48_t *e48, char escape)
{
  printf("Escape function ESC 0x%02x\n", escape);
  return 1;
}

int csi_raw(ecma48_t *e48, char *args, size_t arglen, char command)
{
  printf("CSI %.*s %c\n", arglen, args, command);
  return 1;
}

static ecma48_parser_callbacks_t cb = {
  .text    = text,
  .control = control,
  .escape  = escape,
  .csi_raw = csi_raw,
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

  write(1, buffer, bytes);

  return TRUE;
}

int main(int argc, char *argv[])
{
  e48 = ecma48_new();
  ecma48_set_parser_callbacks(e48, &cb);

  pid_t kid = forkpty(&master, NULL, NULL, NULL);
  if(kid == 0) {
    execvp(argv[1], argv + 1);
    fprintf(stderr, "Cannot exec(%s) - %s\n", argv[1], strerror(errno));
    _exit(1);
  }

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);

  GIOChannel *gio_stdin = g_io_channel_unix_new(0);
  g_io_add_watch(gio_stdin, G_IO_IN, stdin_readable, NULL);

  GIOChannel *gio_master = g_io_channel_unix_new(master);
  g_io_add_watch(gio_master, G_IO_IN, master_readable, NULL);

  g_main_loop_run(loop);

  return 0;
}
