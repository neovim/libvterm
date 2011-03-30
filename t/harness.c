#include "vterm.h"
#include "../src/vterm_internal.h" // We pull in some internal bits too

#include <stdio.h>
#include <string.h>

#define streq(a,b) (!strcmp(a,b))
#define strstartswith(a,b) (!strncmp(a,b,strlen(b)))

static VTerm *vt;
static VTermState *state;

static VTermEncoding *encoding;

static int parser_text(const char bytes[], size_t len, void *user)
{
  printf("text ");
  int i;
  for(i = 0; i < len; i++) {
    unsigned char b = bytes[i];
    if(b < 0x20 || (b >= 0x80 && b < 0xa0))
      break;
    printf(i ? ",%x" : "%x", b);
  }
  printf("\n");

  return i;
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

static int want_state_putglyph = 0;
static int state_putglyph(const uint32_t chars[], int width, VTermPos pos, void *user)
{
  if(!want_state_putglyph)
    return 1;

  printf("putglyph ");
  for(int i = 0; chars[i]; i++)
    printf(i ? ",%x" : "%x", chars[i]);
  printf(" %d %d,%d\n", width, pos.row, pos.col);

  return 1;
}

static VTermPos state_pos;
static int state_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
  state_pos = pos;
  return 1;
}

static int want_state_copyrect = 0;
static int state_copyrect(VTermRect dest, VTermRect src, void *user)
{
  if(!want_state_copyrect)
    return 1;

  printf("copyrect %d..%d,%d..%d -> %d..%d,%d..%d\n",
      src.start_row,  src.end_row,  src.start_col,  src.end_col,
      dest.start_row, dest.end_row, dest.start_col, dest.end_col);

  return 1;
}

static int want_state_erase = 0;
static int state_erase(VTermRect rect, void *user)
{
  if(!want_state_erase)
    return 1;

  printf("erase %d..%d,%d..%d\n",
      rect.start_row, rect.end_row, rect.start_col, rect.end_col);

  return 1;
}

static struct {
  int bold;
  int underline;
  int italic;
  int blink;
  int reverse;
  int font;
  VTermColor foreground;
  VTermColor background;
} state_pen;
static int state_setpenattr(VTermAttr attr, VTermValueType type, VTermValue *val, void *user)
{
  switch(attr) {
  case VTERM_ATTR_NONE:
    break;
  case VTERM_ATTR_BOLD:
    state_pen.bold = val->boolean;
    break;
  case VTERM_ATTR_UNDERLINE:
    state_pen.underline = val->number;
    break;
  case VTERM_ATTR_ITALIC:
    state_pen.italic = val->boolean;
    break;
  case VTERM_ATTR_BLINK:
    state_pen.blink = val->boolean;
    break;
  case VTERM_ATTR_REVERSE:
    state_pen.reverse = val->boolean;
    break;
  case VTERM_ATTR_FONT:
    state_pen.font = val->number;
    break;
  case VTERM_ATTR_FOREGROUND:
    state_pen.foreground = val->color;
    break;
  case VTERM_ATTR_BACKGROUND:
    state_pen.background = val->color;
    break;
  }

  return 1;
}

VTermStateCallbacks state_cbs = {
  .putglyph   = state_putglyph,
  .movecursor = state_movecursor,
  .copyrect   = state_copyrect,
  .erase      = state_erase,
  .setpenattr = state_setpenattr,
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

    else if(strstartswith(line, "WANTSTATE") && (line[9] == '\0' || line[9] == ' ')) {
      if(!state)
        state = vterm_obtain_state(vt);
      vterm_state_set_callbacks(state, &state_cbs, NULL);

      int i = 9;
      while(line[i] == ' ')
        i++;
      for( ; line[i]; i++)
        switch(line[i]) {
        case 'g':
          want_state_putglyph = 1;
          break;
        case 'c':
          want_state_copyrect = 1;
          break;
        case 'e':
          want_state_erase = 1;
          break;
        default:
          fprintf(stderr, "Unrecognised WANTSTATE flag '%c'\n", line[i]);
        }
    }

    else if(sscanf(line, "UTF8 %d", &flag)) {
      vterm_parser_set_utf8(vt, flag);
    }

    else if(streq(line, "RESET")) {
      if(state) {
        vterm_state_reset(state);
        vterm_state_get_cursorpos(state, &state_pos);
      }
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

    else if(streq(line, "WANTENCODING")) {
      /* This isn't really external API but it's hard to get this out any
       * other way
       */
      encoding = vterm_lookup_encoding(ENC_UTF8, 'u');
    }

    else if(strstartswith(line, "ENCIN ")) {
      /* Convert hex chars inplace */
      char *outpos, *inpos, *bytes;
      bytes = inpos = outpos = line + 6;
      while(*inpos) {
        int ch;
        sscanf(inpos, "%2x", &ch);
        *outpos = ch;
        outpos += 1; inpos += 2;
      }

      int maxchars = outpos - bytes;

      uint32_t cp[maxchars];
      int cpi = 0;
      size_t pos = 0;

      (*encoding->decode)(encoding, cp, &cpi, maxchars, bytes, &pos, outpos - bytes);

      printf("encout ");
      for(int i = 0; i < cpi; i++) {
        printf(i ? ",%x" : "%x", cp[i]);
      }
      printf("\n");
    }

    else if(line[0] == '?') {
      if(streq(line, "?cursor")) {
        VTermPos pos;
        vterm_state_get_cursorpos(state, &pos);
        if(pos.row != state_pos.row)
          printf("! row mismatch: state=%d,%d event=%d,%d\n",
              pos.row, pos.col, state_pos.row, state_pos.col);
        else if(pos.col != state_pos.col)
          printf("! col mismatch: state=%d,%d event=%d,%d\n",
              pos.row, pos.col, state_pos.row, state_pos.col);
        else
          printf("%d,%d\n", state_pos.row, state_pos.col);
      }
      else if(strstartswith(line, "?pen ")) {
        char *linep = line + 5;
        while(linep[0] == ' ')
          linep++;

        VTermValue val;
#define BOOLSTR(v) ((v) ? "on" : "off")

        if(streq(linep, "bold")) {
          vterm_state_get_penattr(state, VTERM_ATTR_BOLD, &val);
          if(val.boolean != state_pen.bold)
            printf("! pen bold mismatch; state=%s, event=%s\n",
                BOOLSTR(val.boolean), BOOLSTR(state_pen.bold));
          else
            printf("%s\n", BOOLSTR(state_pen.bold));
        }
        else if(streq(linep, "underline")) {
          vterm_state_get_penattr(state, VTERM_ATTR_UNDERLINE, &val);
          if(val.boolean != state_pen.underline)
            printf("! pen underline mismatch; state=%d, event=%d\n",
                val.boolean, state_pen.underline);
          else
            printf("%d\n", state_pen.underline);
        }
        else if(streq(linep, "italic")) {
          vterm_state_get_penattr(state, VTERM_ATTR_ITALIC, &val);
          if(val.boolean != state_pen.italic)
            printf("! pen italic mismatch; state=%s, event=%s\n",
                BOOLSTR(val.boolean), BOOLSTR(state_pen.italic));
          else
            printf("%s\n", BOOLSTR(state_pen.italic));
        }
        else if(streq(linep, "blink")) {
          vterm_state_get_penattr(state, VTERM_ATTR_BLINK, &val);
          if(val.boolean != state_pen.blink)
            printf("! pen blink mismatch; state=%s, event=%s\n",
                BOOLSTR(val.boolean), BOOLSTR(state_pen.blink));
          else
            printf("%s\n", BOOLSTR(state_pen.blink));
        }
        else if(streq(linep, "reverse")) {
          vterm_state_get_penattr(state, VTERM_ATTR_REVERSE, &val);
          if(val.boolean != state_pen.reverse)
            printf("! pen reverse mismatch; state=%s, event=%s\n",
                BOOLSTR(val.boolean), BOOLSTR(state_pen.reverse));
          else
            printf("%s\n", BOOLSTR(state_pen.reverse));
        }
        else if(streq(linep, "font")) {
          vterm_state_get_penattr(state, VTERM_ATTR_FONT, &val);
          if(val.boolean != state_pen.font)
            printf("! pen font mismatch; state=%d, event=%d\n",
                val.boolean, state_pen.font);
          else
            printf("%d\n", state_pen.font);
        }
        else if(streq(linep, "foreground")) {
          printf("rgb(%d,%d,%d)\n", state_pen.foreground.red, state_pen.foreground.green, state_pen.foreground.blue);
        }
        else if(streq(linep, "background")) {
          printf("rgb(%d,%d,%d)\n", state_pen.background.red, state_pen.background.green, state_pen.background.blue);
        }
        else
          printf("?\n");
      }
      else
        printf("?\n");

      continue;
    }

    else
      err = 1;

    printf(err ? "?\n" : "DONE\n");
  }

  return 0;
}
