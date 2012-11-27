#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define streq(a,b) (strcmp(a,b)==0)

static char *getvalue(int *argip, int argc, char *argv[])
{
  if(*argip >= argc) {
    fprintf(stderr, "Expected an option value\n");
    exit(1);
  }

  return argv[(*argip)++];
}

static int getchoice(int *argip, int argc, char *argv[], const char *options[])
{
  const char *arg = getvalue(argip, argc, argv);

  int value = -1;
  while(options[++value])
    if(streq(arg, options[value]))
      return value;

  fprintf(stderr, "Unrecognised option value %s\n", arg);
  exit(1);
}

static int getbool(int *argip, int argc, char *argv[])
{
  return getchoice(argip, argc, argv, (const char *[]){"off", "on", NULL});
}

static char *helptext[] = {
  "reset",
  "keypad [app|num]",
  "screen [off|on]",
  "cursor [off|on]",
  "curblink [off|on]",
  "curshape [block|under|bar]",
  "mouse [off|click|clickdrag|motion]",
  "altscreen [off|on]",
  "icontitle [STR]",
  "icon [STR]",
  "title [STR]",
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
      switch(getchoice(&argi, argc, argv, (const char *[]){"app", "num", NULL})) {
      case 0:
        printf("\e="); break;
      case 1:
        printf("\e>"); break;
      }
    }
    else if(streq(arg, "screen")) {
      printf("\e[?5%c", getbool(&argi, argc, argv) ? 'h' : 'l');
    }
    else if(streq(arg, "cursor")) {
      printf("\e[?25%c", getbool(&argi, argc, argv) ? 'h' : 'l');
    }
    else if(streq(arg, "curblink")) {
      printf("\e[?12%c", getbool(&argi, argc, argv) ? 'h' : 'l');
    }
    else if(streq(arg, "curshape")) {
      // TODO: This ought to query the current value of DECSCUSR because it
      //   may need blinking on or off
      int shape = getchoice(&argi, argc, argv, (const char *[]){"block", "under", "bar", NULL});
      printf("\e[%d q", 1 + (shape * 2));
    }
    else if(streq(arg, "mouse")) {
      switch(getchoice(&argi, argc, argv, (const char *[]){"off", "click", "clickdrag", "motion", NULL})) {
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
    else if(streq(arg, "icontitle")) {
      printf("\e]0;%s\a", getvalue(&argi, argc, argv));
    }
    else if(streq(arg, "icon")) {
      printf("\e]1;%s\a", getvalue(&argi, argc, argv));
    }
    else if(streq(arg, "title")) {
      printf("\e]2;%s\a", getvalue(&argi, argc, argv));
    }
    else {
      fprintf(stderr, "Unrecognised command %s\n", arg);
      exit(1);
    }
  }
}
