#include "vterm_internal.h"

#include <stdio.h>
#include <string.h>

#define CSI_ARGS_MAX 16
#define CSI_LEADER_MAX 16

static size_t on_text(VTerm *vt, const char bytes[], size_t len)
{
  size_t eaten;

  if(vt->parser_callbacks && vt->parser_callbacks->text)
    if((eaten = (*vt->parser_callbacks->text)(bytes, len, vt->cbdata)))
      return eaten;

  fprintf(stderr, "libvterm: Unhandled text (%zu chars)\n", len);
  return 0;
}

static void on_control(VTerm *vt, unsigned char control)
{
  if(vt->parser_callbacks && vt->parser_callbacks->control)
    if((*vt->parser_callbacks->control)(control, vt->cbdata))
      return;

  fprintf(stderr, "libvterm: Unhandled control 0x%02x\n", control);
}

static size_t on_escape(VTerm *vt, const char bytes[], size_t len)
{
  size_t eaten;

  if(vt->parser_callbacks && vt->parser_callbacks->escape)
    if((eaten = (*vt->parser_callbacks->escape)(bytes, len, vt->cbdata)))
      return eaten;

  fprintf(stderr, "libvterm: Unhandled escape ESC 0x%02x\n", bytes[0]);
  return 0;
}

static void on_csi(VTerm *vt, const char *args, size_t arglen, char command)
{
  if(arglen == 0 || args[0] < 0x3c || args[0] > 0x3d) {
    int i = 0;

    // Extract leader bytes 0x3c to 0x3f
    for( ; i < arglen; i++)
      if(args[i] < 0x3c || args[i] > 0x3f)
        break;

    int leadercount = i;

    int argcount = 1; // Always at least 1 arg

    for( ; i < arglen; i++)
      if(args[i] == 0x3b || args[i] == 0x3a) // ; or :
        argcount++;

    /* TODO: Consider if these buffers should live in the VTerm struct itself */
    long csi_args[CSI_ARGS_MAX];
    if(argcount > CSI_ARGS_MAX)
      argcount = CSI_ARGS_MAX;

    int argi;
    for(argi = 0; argi < argcount; argi++)
      csi_args[argi] = CSI_ARG_MISSING;

    argi = 0;
    int pos;
    for(pos = leadercount; pos < arglen && argi < argcount; pos++) {
      switch(args[pos]) {
      case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
      case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
        if(csi_args[argi] == CSI_ARG_MISSING)
          csi_args[argi] = 0;
        csi_args[argi] *= 10;
        csi_args[argi] += args[pos] - '0';
        break;
      case 0x3a:
        csi_args[argi] |= CSI_ARG_FLAG_MORE;
        /* FALLTHROUGH */
      case 0x3b:
        argi++;
        break;
      default:
        fprintf(stderr, "TODO: Parse %c in CSI\n", args[pos]);
        break;
      }
    }

    char leader[CSI_LEADER_MAX];
    if(leadercount) {
      if(leadercount > CSI_LEADER_MAX - 1)
        leadercount = CSI_LEADER_MAX - 1;
      strncpy(leader, args, leadercount);
      leader[leadercount] = 0;
    }

    //printf("Parsed CSI args %.*s as:\n", arglen, args);
    //printf(" leader: %s\n", leader);
    //for(argi = 0; argi < argcount; argi++) {
    //  printf(" %lu", CSI_ARG(csi_args[argi]));
    //  if(!CSI_ARG_HAS_MORE(csi_args[argi]))
    //    printf("\n");
    //}

    if(vt->parser_callbacks && vt->parser_callbacks->csi)
      if((*vt->parser_callbacks->csi)(leadercount ? leader : NULL, csi_args, argcount, command, vt->cbdata))
        return;
  }

  fprintf(stderr, "libvterm: Unhandled CSI %.*s %c\n", (int)arglen, args, command);
}

static void on_osc(VTerm *vt, const char *command, size_t cmdlen)
{
  if(vt->parser_callbacks && vt->parser_callbacks->osc)
    if((*vt->parser_callbacks->osc)(command, cmdlen, vt->cbdata))
      return;

  fprintf(stderr, "libvterm: Unhandled OSC %.*s\n", (int)cmdlen, command);
}

static void on_dcs(VTerm *vt, const char *command, size_t cmdlen)
{
  if(vt->parser_callbacks && vt->parser_callbacks->dcs)
    if((*vt->parser_callbacks->dcs)(command, cmdlen, vt->cbdata))
      return;

  fprintf(stderr, "libvterm: Unhandled DCS %.*s\n", (int)cmdlen, command);
}

static void append_strbuffer(VTerm *vt, const char *str, size_t len)
{
  if(len > vt->strbuffer_len - vt->strbuffer_cur) {
    len = vt->strbuffer_len - vt->strbuffer_cur;
    fprintf(stderr, "Truncating strbuffer preserve to %zd bytes\n", len);
  }

  if(len > 0) {
    strncpy(vt->strbuffer + vt->strbuffer_cur, str, len);
    vt->strbuffer_cur += len;
  }
}

static size_t do_string(VTerm *vt, const char *str_frag, size_t len, char command)
{
  if(vt->strbuffer_cur) {
    if(str_frag)
      append_strbuffer(vt, str_frag, len);

    str_frag = vt->strbuffer;
    len = vt->strbuffer_cur;
    vt->strbuffer_cur = 0;
  }
  else if(!str_frag) {
    fprintf(stderr, "parser.c: TODO: No strbuffer _and_ no final fragment???\n");
    len = 0;
  }

  switch(vt->parser_state) {
  case NORMAL:
    return on_text(vt, str_frag, len);
  case ESC:
    return on_escape(vt, str_frag, len);
  case CSI:
    on_csi(vt, str_frag, len, command);
    return 0;
  case OSC:
    on_osc(vt, str_frag, len);
    return 0;
  case DCS:
    on_dcs(vt, str_frag, len);
    return 0;
  }

  return 0;
}

void vterm_push_bytes(VTerm *vt, const char *bytes, size_t len)
{
  size_t pos = 0;
  const char *string_start;

  switch(vt->parser_state) {
  case NORMAL:
    string_start = NULL;
    break;
  case ESC:
  case CSI:
  case OSC:
  case DCS:
    string_start = bytes;
    break;
  }

#define ENTER_STRING_STATE(st) do { vt->parser_state = st; string_start = bytes + pos + 1; } while(0)
#define ENTER_NORMAL_STATE()   do { vt->parser_state = NORMAL; string_start = NULL; } while(0)

  for(pos = 0; pos < len; pos++) {
    unsigned char c = bytes[pos];

    switch(vt->parser_state) {
    case ESC:
      switch(c) {
      case 0x50: // DCS
        ENTER_STRING_STATE(DCS);
        break;
      case 0x5b: // CSI
        ENTER_STRING_STATE(CSI);
        break;
      case 0x5d: // OSC
        ENTER_STRING_STATE(OSC);
        break;
      default:
        if(c >= 0x40 && c < 0x60) {
          // C1 emulations using 7bit clean
          // ESC 0x40 == 0x80
          on_control(vt, c + 0x40);
          ENTER_NORMAL_STATE();
        }
        else {
          size_t esc_eaten = do_string(vt, bytes + pos, len - pos, 0);
          if(esc_eaten <= 0)
            goto pause;

          ENTER_NORMAL_STATE();
          pos += (esc_eaten - 1); // we'll ++ it again in a moment
        }
      }
      break;

    case CSI:
      if(c >= 0x40 && c <= 0x7f) {
        do_string(vt, string_start, bytes + pos - string_start, c);
        ENTER_NORMAL_STATE();
      }
      break;

    case OSC:
    case DCS:
      if(c == 0x07 || (c == 0x9c && !vt->is_utf8)) {
        do_string(vt, string_start, bytes + pos - string_start, 0);
        ENTER_NORMAL_STATE();
      }
      else if(c == 0x5c && bytes[pos-1] == 0x1b) {
        do_string(vt, string_start, bytes + pos - string_start - 1, 0);
        ENTER_NORMAL_STATE();
      }
      break;

    case NORMAL:
      if(c < 0x20 || (c >= 0x80 && c < 0xa0 && !vt->is_utf8)) {
        switch(c) {
        case 0x1b: // ESC
          ENTER_STRING_STATE(ESC);
          break;
        case 0x90: // DCS
          ENTER_STRING_STATE(DCS);
          break;
        case 0x9b: // CSI
          ENTER_STRING_STATE(CSI);
          break;
        case 0x9d: // OSC
          ENTER_STRING_STATE(OSC);
          break;
        default:
          on_control(vt, c);
          break;
        }
      }
      else {
        size_t text_eaten = do_string(vt, bytes + pos, len - pos, 0);

        if(text_eaten == 0) {
          string_start = bytes + pos;
          goto pause;
        }

        pos += (text_eaten - 1); // we'll ++ it again in a moment
      }
      break;
    }
  }

pause:
  if(string_start) {
    size_t remaining = len - (string_start - bytes);
    append_strbuffer(vt, string_start, remaining);
  }
}
