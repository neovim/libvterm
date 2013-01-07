#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include "vterm.h"

#include "../src/utf8.h" // fill_utf8

#define streq(a,b) (!strcmp(a,b))

static VTerm *vt;
static VTermScreen *vts;

static int cols;
static int rows;

static enum {
  FORMAT_PLAIN,
  FORMAT_SGR,
} format = FORMAT_PLAIN;

void dump_row(int row)
{
  VTermPos pos = { .row = row, .col = 0 };
  VTermScreenCell prevcell = {};

  while(pos.col < cols) {
    VTermScreenCell cell;
    vterm_screen_get_cell(vts, pos, &cell);

    switch(format) {
      case FORMAT_PLAIN:
        break;
      case FORMAT_SGR:
        {
          // If all 7 attributes change, that means 7 SGRs max
          int sgr[7]; int sgri = 0;

          if(!prevcell.attrs.bold && cell.attrs.bold)
            sgr[sgri++] = 1;
          if(prevcell.attrs.bold && !cell.attrs.bold)
            sgr[sgri++] = 22;

          if(!prevcell.attrs.underline && cell.attrs.underline)
            sgr[sgri++] = 4;
          if(prevcell.attrs.underline && !cell.attrs.underline)
            sgr[sgri++] = 24;

          if(!prevcell.attrs.italic && cell.attrs.italic)
            sgr[sgri++] = 3;
          if(prevcell.attrs.italic && !cell.attrs.italic)
            sgr[sgri++] = 23;

          if(!prevcell.attrs.blink && cell.attrs.blink)
            sgr[sgri++] = 5;
          if(prevcell.attrs.blink && !cell.attrs.blink)
            sgr[sgri++] = 25;

          if(!prevcell.attrs.reverse && cell.attrs.reverse)
            sgr[sgri++] = 7;
          if(prevcell.attrs.reverse && !cell.attrs.reverse)
            sgr[sgri++] = 27;

          if(!prevcell.attrs.strike && cell.attrs.strike)
            sgr[sgri++] = 9;
          if(prevcell.attrs.strike && !cell.attrs.strike)
            sgr[sgri++] = 29;

          if(!prevcell.attrs.font && cell.attrs.font)
            sgr[sgri++] = 10 + cell.attrs.font;
          if(prevcell.attrs.font && !cell.attrs.font)
            sgr[sgri++] = 10;

          if(!sgri)
            break;

          printf("\e[");
          for(int i = 0; i < sgri; i++)
            printf(i ? ";%d" : "%d", sgr[i]);
          printf("m");
        }
        break;
    }

    for(int i = 0; cell.chars[i]; i++) {
      char bytes[6];
      bytes[fill_utf8(cell.chars[i], bytes)] = 0;
      printf("%s", bytes);
    }

    pos.col += cell.width;
    prevcell = cell;
  }

  switch(format) {
    case FORMAT_PLAIN:
      break;
    case FORMAT_SGR:
      if(prevcell.attrs.bold || prevcell.attrs.underline || prevcell.attrs.italic ||
         prevcell.attrs.blink || prevcell.attrs.reverse || prevcell.attrs.strike ||
         prevcell.attrs.font)
        printf("\e[m");
      break;
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
  int opt;
  while((opt = getopt(argc, argv, "f:")) != -1) {
    switch(opt) {
      case 'f':
        if(streq(optarg, "plain"))
          format = FORMAT_PLAIN;
        else if(streq(optarg, "sgr"))
          format = FORMAT_SGR;
        else {
          fprintf(stderr, "Unrecognised format '%s'\n", optarg);
          exit(1);
        }
        break;
    }
  }

  const char *file = argv[optind++];
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
