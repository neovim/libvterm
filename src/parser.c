#include "vterm_internal.h"

#include <stdio.h>
#include <string.h>

#include <glib.h>

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
  if(arglen == 0 || args[0] < 0x3c || args[0] > 0x3e) {
    int i;
    for(i = 0; i < arglen; i++)
      // Treat 0x3f '?' as an intermediate byte, even though it's actually a
      // DEC custom extension. Most terms seem to use that
      if((args[i] & 0xf0) != 0x20 && args[i] != 0x3f)
        break;

    int intermedcount = i;

    int argcount = 1; // Always at least 1 arg

    for( ; i < arglen; i++)
      if(args[i] == 0x3b || args[i] == 0x3a) // ; or :
        argcount++;

    long *csi_args = g_alloca(argcount * sizeof(long));

    int argi;
    for(argi = 0; argi < argcount; argi++)
      csi_args[argi] = CSI_ARG_MISSING;

    argi = 0;
    int pos;
    for(pos = intermedcount; pos < arglen; pos++) {
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

    char *intermed = NULL;
    if(intermedcount) {
      intermed = g_alloca(intermedcount + 1); // for terminating NUL
      strncpy(intermed, args, intermedcount);
      intermed[intermedcount] = 0;
    }

    //printf("Parsed CSI args %.*s as:\n", arglen, args);
    //printf(" intermed: %s\n", intermed);
    //for(argi = 0; argi < argcount; argi++) {
    //  printf(" %lu", CSI_ARG(csi_args[argi]));
    //  if(!CSI_ARG_HAS_MORE(csi_args[argi]))
    //    printf("\n");
    //}

    if(vt->parser_callbacks && vt->parser_callbacks->csi)
      if((*vt->parser_callbacks->csi)(intermed, csi_args, argcount, command, vt->cbdata))
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

size_t vterm_parser_interpret_bytes(VTerm *vt, const char bytes[], size_t len)
{
  size_t pos = 0;
  size_t eaten = 0;

  enum {
    NORMAL,
    ESC,
    CSI,
    OSC,
  } parse_state = NORMAL;

  size_t string_start;

  for(pos = 0; pos < len; pos++) {
    unsigned char c = bytes[pos];

    switch(parse_state) {
    case ESC:
      switch(c) {
      case 0x5b: // CSI
        parse_state = CSI;
        string_start = pos + 1;
        break;
      case 0x5d: // OSC
        parse_state = OSC;
        string_start = pos + 1;
        break;
      default:
        if(c >= 0x40 && c < 0x60) {
          // C1 emulations using 7bit clean
          // ESC 0x40 == 0x80
          on_control(vt, c + 0x40);
        }
        else {
          size_t esc_eaten = on_escape(vt, bytes + pos, len - pos);
          if(esc_eaten < 0)
            return eaten;
          if(esc_eaten > 0)
            pos += (esc_eaten - 1); // we'll ++ it again in a moment
        }
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      break;

    case CSI:
      if(c >= 0x40 && c <= 0x7f) {
        on_csi(vt, bytes + string_start, pos - string_start, c);
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      break;

    case OSC:
      if(c == 0x07 || (c == 0x9c && !vt->is_utf8)) {
        on_osc(vt, bytes + string_start, pos - string_start);
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      else if(c == 0x5c && bytes[pos-1] == 0x1b) {
        on_osc(vt, bytes + string_start, pos - string_start - 1);
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      break;

    case NORMAL:
      if(c < 0x20 || (c >= 0x80 && c < 0xa0 && !vt->is_utf8)) {
        switch(c) {
        case 0x1b: // ESC
          parse_state = ESC;
          break;
        case 0x9b: // CSI
          parse_state = CSI;
          string_start = pos + 1;
          break;
        case 0x9d: // OSC
          parse_state = OSC;
          string_start = pos + 1;
          break;
        default:
          on_control(vt, c);
          eaten = pos + 1;
          break;
        }
      }
      else {
        size_t text_eaten = on_text(vt, bytes + pos, len - pos);

        if(text_eaten == 0)
          return pos;

        pos += text_eaten;
        eaten = pos;

        // pos is now the first character we didn't like
        pos--;
      }
      break;

    }
  }

  return eaten;
}
