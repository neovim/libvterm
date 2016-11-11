#define _XOPEN_SOURCE 500  /* strdup */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define streq(a,b) (strcmp(a,b)==0)

#include <termios.h>

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

typedef enum {
  OFF,
  ON,
  QUERY,
} BoolQuery;

static BoolQuery getboolq(int *argip, int argc, char *argv[])
{
  return getchoice(argip, argc, argv, (const char *[]){"off", "on", "query", NULL});
}

static char *helptext[] = {
  "reset",
  "keypad [app|num]",
  "screen [off|on|query]",
  "cursor [off|on|query]",
  "curblink [off|on|query]",
  "curshape [block|under|bar]",
  "mouse [off|click|clickdrag|motion]",
  "altscreen [off|on|query]",
  "bracketpaste [off|on|query]",
  "icontitle [STR]",
  "icon [STR]",
  "title [STR]",
  NULL
};

static bool seticanon(bool icanon, bool echo)
{
  struct termios termios;

  tcgetattr(0, &termios);

  bool ret = (termios.c_lflag & ICANON);

  if(icanon) termios.c_lflag |=  ICANON;
  else       termios.c_lflag &= ~ICANON;

  if(echo) termios.c_lflag |=  ECHO;
  else     termios.c_lflag &= ~ECHO;

  tcsetattr(0, TCSANOW, &termios);

  return ret;
}

static char *read_csi()
{
  /* TODO: This really should be a more robust CSI parser
   */
  char c;

  /* await CSI - 8bit or 2byte 7bit form */
  bool in_esc = false;
  while((c = getchar())) {
    if(c == 0x9b)
      break;
    if(in_esc && c == 0x5b)
      break;
    if(!in_esc && c == 0x1b)
      in_esc = true;
    else
      in_esc = false;
  }

  char csi[32];
  int i = 0;
  for(; i < sizeof(csi)-1; i++) {
    c = csi[i] = getchar();
    if(c >= 0x40 && c <= 0x7e)
      break;
  }
  csi[++i] = 0;

  // TODO: returns longer than 32?

  return strdup(csi);
}

static void usage(int exitcode)
{
  fprintf(stderr, "Control a libvterm-based terminal\n"
      "\n"
      "Options:\n");

  for(char **p = helptext; *p; p++)
    fprintf(stderr, "  %s\n", *p);

  exit(exitcode);
}

static bool query_dec_mode(int mode)
{
  printf("\e[?%d$p", mode);

  char *s = NULL;
  do {
    if(s)
      free(s);
    s = read_csi();

    /* expect "?" mode ";" value "$y" */

    int reply_mode, reply_value;
    char reply_cmd;
    /* If the sscanf format string ends in a literal, we can't tell from
     * its return value if it matches. Hence we'll %c the cmd and check it
     * explicitly
     */
    if(sscanf(s, "?%d;%d$%c", &reply_mode, &reply_value, &reply_cmd) < 3)
      continue;
    if(reply_cmd != 'y')
      continue;

    if(reply_mode != mode)
      continue;

    free(s);

    if(reply_value == 1 || reply_value == 3)
      return true;
    if(reply_value == 2 || reply_value == 4)
      return false;

    printf("Unrecognised reply to DECRQM: %d\n", reply_value);
    return false;
  } while(1);
}

static void do_dec_mode(int mode, BoolQuery val, const char *name)
{
  switch(val) {
    case OFF:
    case ON:
      printf("\e[?%d%c", mode, val == ON ? 'h' : 'l');
      break;

    case QUERY:
      if(query_dec_mode(mode))
        printf("%s on\n", name);
      else
        printf("%s off\n", name);
      break;
  }
}

bool wasicanon;

void restoreicanon(void)
{
  seticanon(wasicanon, true);
}

int main(int argc, char *argv[])
{
  int argi = 1;

  if(argc == 1)
    usage(0);

  wasicanon = seticanon(false, false);
  atexit(restoreicanon);

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
      do_dec_mode(5, getboolq(&argi, argc, argv), "screen");
    }
    else if(streq(arg, "cursor")) {
      do_dec_mode(25, getboolq(&argi, argc, argv), "cursor");
    }
    else if(streq(arg, "curblink")) {
      do_dec_mode(12, getboolq(&argi, argc, argv), "curblink");
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
      do_dec_mode(1049, getboolq(&argi, argc, argv), "altscreen");
    }
    else if(streq(arg, "bracketpaste")) {
      do_dec_mode(2004, getboolq(&argi, argc, argv), "bracketpaste");
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
