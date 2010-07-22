#include "vterm.h"

#include <stdio.h>
#include <string.h>

#define streq(a,b) (!strcmp(a,b))
#define strstartswith(a,b) (!strncmp(a,b,strlen(b)))

static VTerm *vt;

static int parser_text(const uint32_t codepoints[], int npoints, void *user)
{
  printf("text ");
  for(int i = 0; i < npoints; i++)
    printf(i ? ",%x" : "%x", codepoints[i]);
  printf("\n");

  return 1;
}

static int parser_control(unsigned char control, void *user)
{
  printf("control %02x\n", control);

  return 1;
}

static int parser_escape(const char bytes[], size_t len, void *user)
{
  printf("escape %02x\n", bytes[0]);

  return 1;
}

static int parser_csi(const char *intermed, const long args[], int argcount, char command, void *user)
{
  printf("csi %02x", command);
  for(int i = 0; i < argcount; i++) {
    char sep = i ? ',' : ' ';

    if(args[i] == CSI_ARG_MISSING)
      printf("%c*", sep);
    else
      printf("%c%ld%s", sep, CSI_ARG(args[i]), CSI_ARG_HAS_MORE(args[i]) ? "+" : "");
  }

  if(intermed && intermed[0]) {
    printf(" ");
    for(int i = 0; intermed[i]; i++)
      printf("%02x", intermed[i]);
  }

  printf("\n");

  return 1;
}

static int parser_osc(const char *command, size_t cmdlen, void *user)
{
  printf("osc ");
  for(int i = 0; i < cmdlen; i++)
    printf("%02x", command[i]);
  printf("\n");

  return 1;
}

static VTermParserCallbacks parser_cbs = {
  .text    = parser_text,
  .control = parser_control,
  .escape  = parser_escape,
  .csi     = parser_csi,
  .osc     = parser_osc,
};

int main(int argc, char **argv)
{
  char line[1024];
  int flag;

  int err;

  setvbuf(stdout, NULL, _IONBF, 0);

  while(fgets(line, sizeof line, stdin)) {
    err = 0;

    char *nl;
    if((nl = strchr(line, '\n')))
      *nl = '\0';

    if(streq(line, "INIT")) {
      if(!vt)
        vt = vterm_new(25, 80);
    }

    else if(streq(line, "WANTPARSER")) {
      vterm_set_parser_callbacks(vt, &parser_cbs, NULL);
    }

    else if(sscanf(line, "UTF8 %d", &flag)) {
      vterm_parser_set_utf8(vt, flag);
    }

    else if(strstartswith(line, "PUSH ")) {
      /* Convert hex chars inplace */
      char *outpos, *inpos, *bytes;
      bytes = inpos = outpos = line + 5;
      while(*inpos) {
        int ch;
        sscanf(inpos, "%2x", &ch);
        *outpos = ch;
        outpos += 1; inpos += 2;
      }

      vterm_push_bytes(vt, bytes, outpos - bytes);
    }

    else
      err = 1;

    printf(err ? "?\n" : "DONE\n");
  }

  return 0;
}
