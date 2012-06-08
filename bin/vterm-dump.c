#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vterm.h"

static int parser_text(const char bytes[], size_t len, void *user)
{
  int i;
  for(i = 0; i < len; i++)
    if(bytes[i] < 0x20 || (bytes[i] >= 0x80 && bytes[i] < 0xa0))
      break;

  printf("TEXT %.*s\n", i, bytes);
  return i;
}

static int parser_control(unsigned char control, void *user)
{
  printf("CONTROL 0x%02x\n", control);
  return 1;
}

static int parser_escape(const char bytes[], size_t len, void *user)
{
  if(bytes[0] >= 0x20 && bytes[0] < 0x30) {
    if(len < 2)
      return -1;
    len = 2;
  }
  else {
    len = 1;
  }

  printf("ESC ");
  for(int i = 0; i < len; i++)
    printf("%c ", bytes[i]);
  printf("\n");

  return len;
}

static int parser_csi(const char *leader, const long args[], int argcount, const char *intermed, char command, void *user)
{
  printf("CSI ");

  if(leader && leader[0])
    printf("%s ", leader);

  for(int i = 0; i < argcount; i++) {
    if(i)
      printf(",");

    if(args[i] == CSI_ARG_MISSING)
      printf("*");
    else
      printf("%ld%s", CSI_ARG(args[i]), CSI_ARG_HAS_MORE(args[i]) ? "+" : " ");
  }

  if(intermed && intermed[0])
    printf("%s ", intermed);

  printf("%c\n", command);

  return 1;
}

static int parser_osc(const char *command, size_t cmdlen, void *user)
{
  printf("OSC %.*s\n", (int)cmdlen, command);

  return 1;
}

static int parser_dcs(const char *command, size_t cmdlen, void *user)
{
  printf("DCS %.*s\n", (int)cmdlen, command);

  return 1;
}

static VTermParserCallbacks parser_cbs = {
  .text    = &parser_text,
  .control = &parser_control,
  .escape  = &parser_escape,
  .csi     = &parser_csi,
  .osc     = &parser_osc,
  .dcs     = &parser_dcs,
};

int main(int argc, char *argv[])
{
  int fd = open(argv[1], O_RDONLY);
  if(fd == -1) {
    fprintf(stderr, "Cannot open %s - %s\n", argv[1], strerror(errno));
    exit(1);
  }

  /* Size matters not for the parser */
  VTerm *vt = vterm_new(25, 80);
  vterm_set_parser_callbacks(vt, &parser_cbs, NULL);

  int len;
  char buffer[1024];
  while((len = read(fd, buffer, sizeof(buffer))) > 0) {
    vterm_push_bytes(vt, buffer, len);
  }

  close(fd);
  vterm_free(vt);
}
