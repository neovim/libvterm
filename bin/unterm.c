#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "vterm.h"

#include "../src/utf8.h" // fill_utf8

static VTerm *vt;
static VTermScreen *vts;

static int cols;
static int rows;

void dump_row(int row)
{
  VTermPos pos = { .row = row, .col = 0 };
  while(pos.col < cols) {
    VTermScreenCell cell;
    vterm_screen_get_cell(vts, pos, &cell);

    for(int i = 0; cell.chars[i]; i++) {
      char bytes[6];
      bytes[fill_utf8(cell.chars[i], bytes)] = 0;
      printf("%s", bytes);
    }

    pos.col += cell.width;
  }

  printf("\n");
}

static int screen_prescroll(VTermRect rect, void *user)
{
  if(rect.start_row != 0 || rect.start_col != 0 || rect.end_col != cols)
    return 0;

  for(int row = 0; row < rect.end_row; row++)
    dump_row(row);

  return 1;
}

static int screen_resize(int new_rows, int new_cols, void *user)
{
  rows = new_rows;
  cols = new_cols;
  return 1;
}

static VTermScreenCallbacks cb_screen = {
  .prescroll = &screen_prescroll,
  .resize    = &screen_resize,
};

int main(int argc, char *argv[])
{
  const char *file = argv[1];
  int fd = open(file, O_RDONLY);
  if(fd == -1) {
    fprintf(stderr, "Cannot open %s - %s\n", file, strerror(errno));
    exit(1);
  }

  rows = 25;
  cols = 80;

  vt = vterm_new(rows, cols);
  vts = vterm_obtain_screen(vt);
  vterm_screen_set_callbacks(vts, &cb_screen, NULL);

  vterm_screen_reset(vts, 1);

  int len;
  char buffer[1024];
  while((len = read(fd, buffer, sizeof(buffer))) > 0) {
    vterm_push_bytes(vt, buffer, len);
  }

  for(int row = 0; row < rows; row++) {
    dump_row(row);
  }

  close(fd);

  vterm_free(vt);
}
