#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define streq(a,b) (strcmp(a,b)==0)

static int getvalue(int *argip, int argc, char *argv[], const char *options[])
{
  if(*argip >= argc) {
    fprintf(stderr, "Expected an option value\n");
    exit(1);
  }

  const char *arg = argv[(*argip)++];

  int value = -1;
  while(options[++value])
    if(streq(arg, options[value]))
      return value;

  fprintf(stderr, "Unrecognised option value %s\n", arg);
  exit(1);
}

static int getbool(int *argip, int argc, char *argv[])
{
  return getvalue(argip, argc, argv, (const char *[]){"off", "on", NULL});
}

static char *helptext[] = {
  "reset",
  "keypad [app|num]",
  "screen [off|on]",
  "curblink [off|on]",
  "cursor [off|on]",
  "mouse [off|click|clickdrag|motion]",
  "altscreen [off|on]",
  NULL
};

static void usage(int exitcode)
{
  fprintf(stderr, "Control a libvterm-based terminal\n"
      "\n"
      "Options:\n");

  for(char **p = helptext; *p; p++)
    fprintf(stderr, "  %s\n", *p);

  exit(exitcode);
}

int main(int argc, char *argv[])
{
  int argi = 1;

  if(argc == 1)
    usage(0);

  while(argi < argc) {
    const char *arg = argv[argi++];

    if(streq(arg, "reset")) {
      printf("\ec");
    }
    else if(streq(arg, "keypad")) {
      switch(getvalue(&argi, argc, argv, (const char *[]){"app", "num", NULL})) {
      case 0:
        printf("\e="); break;
      case 1:
        printf("\e>"); break;
      }
    }
    else if(streq(arg, "screen")) {
      printf("\e[?5%c", getbool(&argi, argc, argv) ? 'h' : 'l');
    }
    else if(streq(arg, "curblink")) {
      printf("\e[?12%c", getbool(&argi, argc, argv) ? 'h' : 'l');
    }
    else if(streq(arg, "cursor")) {
      printf("\e[?25%c", getbool(&argi, argc, argv) ? 'h' : 'l');
    }
    else if(streq(arg, "mouse")) {
      switch(getvalue(&argi, argc, argv, (const char *[]){"off", "click", "clickdrag", "motion", NULL})) {
      case 0:
        printf("\e[?1000l"); break;
      case 1:
        printf("\e[?1000h"); break;
      case 2:
        printf("\e[?1002h"); break;
      case 3:
        printf("\e[?1003h"); break;
      }
    }
    else if(streq(arg, "altscreen")) {
      printf("\e[?1049%c", getbool(&argi, argc, argv) ? 'h' : 'l');
    }
    else {
      fprintf(stderr, "Unrecognised command %s\n", arg);
      exit(1);
    }
  }
}
