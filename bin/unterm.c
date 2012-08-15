#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "vterm.h"

void dump_row(VTerm *vt, VTermScreen *vts, int row)
{
  int cols;
  vterm_get_size(vt, NULL, &cols);
  VTermRect rect = {
    .start_row = row,
    .start_col = 0,
    .end_row   = row+1,
    .end_col   = cols,
  };

  size_t len = vterm_screen_get_text(vts, NULL, 0, rect);
  char *text = malloc(len + 1);
  text[len] = 0;

  vterm_screen_get_text(vts, text, len, rect);

  printf("%s\n", text);

  free(text);
}

int main(int argc, char *argv[])
{
  const char *file = argv[1];
  int fd = open(file, O_RDONLY);
  if(fd == -1) {
    fprintf(stderr, "Cannot open %s - %s\n", file, strerror(errno));
    exit(1);
  }

  VTerm *vt = vterm_new(25, 80);
  VTermScreen *vts = vterm_obtain_screen(vt);
  vterm_screen_set_callbacks(vts, NULL, NULL);

  vterm_screen_reset(vts, 1);

  int len;
  char buffer[1024];
  while((len = read(fd, buffer, sizeof(buffer))) > 0) {
    vterm_push_bytes(vt, buffer, len);
  }

  int rows;
  vterm_get_size(vt, &rows, NULL);
  for(int row = 0; row < rows; row++) {
    dump_row(vt, vts, row);
  }

  close(fd);

  vterm_free(vt);
}
