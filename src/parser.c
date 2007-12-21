#include "ecma48_internal.h"

#define UNICODE_INVALID 0xFFFD

#ifdef DEBUG
# define DEBUG_PRINT_UTF8
#endif

#include <stdio.h>
#include <string.h>

#include <glib.h>

static void ecma48_on_parser_text(ecma48_t *e48, int codepoints[], int npoints)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->text)
    done = (*e48->parser_callbacks->text)(e48, codepoints, npoints);

  if(!done && e48->state)
    done = ecma48_state_on_text(e48, codepoints, npoints);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled text (%d chars)", npoints);
}

static void ecma48_on_parser_control(ecma48_t *e48, unsigned char control)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->control)
    done = (*e48->parser_callbacks->control)(e48, control);

  if(!done && e48->state)
    done = ecma48_state_on_control(e48, control);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled control 0x%02x\n", control);
}

static void ecma48_on_parser_escape(ecma48_t *e48, char escape)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->escape)
    done = (*e48->parser_callbacks->escape)(e48, escape);

  if(!done && e48->state)
    done = ecma48_state_on_escape(e48, escape);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled escape ESC 0x%02x\n", (unsigned char)escape);
}

static void ecma48_on_parser_csi(ecma48_t *e48, char *args, size_t arglen, char command)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->csi_raw)
    done = (*e48->parser_callbacks->csi_raw)(e48, args, arglen, command);

  if(done)
    return;

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
      if(args[i] == 0x3b)
        argcount++;

    // TODO: ECMA-48 allows 123:456 as an argument, but we'll ignore .
    // and trailing digits
    int *csi_args = g_alloca(argcount * sizeof(int));

    int argi;
    for(argi = 0; argi < argcount; argi++)
      csi_args[argi] = -1;

    argi = 0;
    int pos;
    for(pos = intermedcount; pos < arglen; pos++) {
      switch(args[pos]) {
      case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
      case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
        if(csi_args[argi] == -1)
          csi_args[argi] = 0;
        csi_args[argi] *= 10;
        csi_args[argi] += args[pos] - '0';
        break;
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
    //for(argi = 0; argi < argcount; argi++)
    //  printf(" %d", csi_args[argi]);
    //printf("\n");

    if(e48->parser_callbacks &&
      e48->parser_callbacks->csi)
      done = (*e48->parser_callbacks->csi)(e48, intermed, csi_args, argcount, command);

    if(!done && e48->state)
      done = ecma48_state_on_csi(e48, intermed, csi_args, argcount, command);
  }

  if(!done)
    fprintf(stderr, "libecma48: Unhandled CSI %.*s %c\n", arglen, args, command);
}

static void ecma48_on_parser_osc(ecma48_t *e48, char *command, size_t cmdlen)
{
  int done = 0;

  if(e48->parser_callbacks &&
     e48->parser_callbacks->osc)
    done = (*e48->parser_callbacks->osc)(e48, command, cmdlen);

  if(!done)
    fprintf(stderr, "libecma48: Unhandled OSC %.*s\n", cmdlen, command);
}

static int interpret_utf8(int cp[], int *cpi, char bytes[], size_t *pos, size_t len)
{
  // number of bytes remaining in this codepoint
  int bytes_remaining = 0;
  // number of bytes total in this codepoint once it's finished
  // (for detecting overlongs)
  int bytes_total     = 0;

  int this_cp;

  for( ; *pos < len; (*pos)++) {
    unsigned char c = bytes[*pos];

#ifdef DEBUG_PRINT_UTF8
    printf(" pos=%d c=%02x rem=%d\n", *pos, c, bytes_remaining);
#endif

    if(c < 0x20)
      return 0;

    else if(c >= 0x20 && c < 0x80) {
      if(bytes_remaining)
        cp[(*cpi)++] = UNICODE_INVALID;

      cp[(*cpi)++] = c;
#ifdef DEBUG_PRINT_UTF8
      printf(" UTF-8 char: U+%04x\n", c);
#endif
      bytes_remaining = 0;
    }

    else if(c >= 0x80 && c < 0xc0) {
      if(!bytes_remaining) {
        cp[(*cpi)++] = UNICODE_INVALID;
        continue;
      }

      this_cp <<= 6;
      this_cp |= c & 0x3f;
      bytes_remaining--;

      if(!bytes_remaining) {
#ifdef DEBUG_PRINT_UTF8
        printf(" UTF-8 raw char U+%04x len=%d ", this_cp, bytes_total);
#endif
        // Check for overlong sequences
        switch(bytes_total) {
        case 2:
          if(this_cp <  0x0080) this_cp = UNICODE_INVALID; break;
        case 3:
          if(this_cp <  0x0800) this_cp = UNICODE_INVALID; break;
        case 4:
          if(this_cp < 0x10000) this_cp = UNICODE_INVALID; break;
        case 5:
          if(this_cp < 0x200000) this_cp = UNICODE_INVALID; break;
        case 6:
          if(this_cp < 0x4000000) this_cp = UNICODE_INVALID; break;
        }
        // Now look for plain invalid ones
        if((this_cp >= 0xD800 && this_cp <= 0xDFFF) ||
           this_cp == 0xFFFE ||
           this_cp == 0xFFFF)
          this_cp = UNICODE_INVALID;
#ifdef DEBUG_PRINT_UTF8
        printf(" char: U+%04x\n", this_cp);
#endif
        cp[(*cpi)++] = this_cp;
      }
    }

    else if(c >= 0xc0 && c < 0xe0) {
      if(bytes_remaining)
        cp[(*cpi)++] = UNICODE_INVALID;

      if(len - *pos < 2)
        return 1;

      this_cp = c & 0x1f;
      bytes_total = 2;
      bytes_remaining = 1;
    }

    else if(c >= 0xe0 && c < 0xf0) {
      if(bytes_remaining)
        cp[(*cpi)++] = UNICODE_INVALID;

      if(len - *pos < 3)
        return 1;

      this_cp = c & 0x0f;
      bytes_total = 3;
      bytes_remaining = 2;
    }

    else if(c >= 0xf0 && c < 0xf8) {
      if(bytes_remaining)
        cp[(*cpi)++] = UNICODE_INVALID;

      if(len - *pos < 4)
        return 1;

      this_cp = c & 0x07;
      bytes_total = 4;
      bytes_remaining = 3;
    }

    else if(c >= 0xf8 && c < 0xfc) {
      if(bytes_remaining)
        cp[(*cpi)++] = UNICODE_INVALID;

      if(len - *pos < 5)
        return 1;

      this_cp = c & 0x03;
      bytes_total = 5;
      bytes_remaining = 4;
    }

    else if(c >= 0xfc && c < 0xfe) {
      if(bytes_remaining)
        cp[(*cpi)++] = UNICODE_INVALID;

      if(len - *pos < 6)
        return 1;

      this_cp = c & 0x01;
      bytes_total = 6;
      bytes_remaining = 5;
    }

    else {
      cp[(*cpi)++] = UNICODE_INVALID;
    }
  }

  return 1;
}

size_t ecma48_parser_interpret_bytes(ecma48_t *e48, char *bytes, size_t len)
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
        if(c >= 0x40 && c < 0x60)
          // C1 emulations using 7bit clean
          // ESC 0x40 == 0x80
          ecma48_on_parser_control(e48, c + 0x40);
        else 
          ecma48_on_parser_escape(e48, c);
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      break;

    case CSI:
      if(c >= 0x40 && c <= 0x7f) {
        ecma48_on_parser_csi(e48, bytes + string_start, pos - string_start, c);
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      break;

    case OSC:
      if(c == 0x07 || (c == 0x9c && !e48->is_utf8)) {
        ecma48_on_parser_osc(e48, bytes + string_start, pos - string_start);
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      else if(c == 0x5c && bytes[pos-1] == 0x1b) {
        ecma48_on_parser_osc(e48, bytes + string_start, pos - string_start - 1);
        parse_state = NORMAL;
        eaten = pos + 1;
      }
      break;

    case NORMAL:
      if(c < 0x20 || (c >= 0x80 && c < 0xa0 && !e48->is_utf8)) {
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
          ecma48_on_parser_control(e48, c);
          eaten = pos + 1;
          break;
        }
      }
      else {
        // We'll have at most (len - pos) codepoints. Doesn't matter
        // if we overallocate this
        int *cp = g_alloca((len - pos) * sizeof(int));
        int cpi = 0;

#ifdef DEBUG_PRINT_UTF8
        printf("BEGIN UTF-8\n");
#endif

        int finished;

        if(e48->is_utf8)
          finished = interpret_utf8(cp, &cpi, bytes, &pos, len);
        else {
          finished = 0;

          for( ; pos < len; pos++) {
            c = bytes[pos];

            if(c < 0x20 || (c >= 0x80 && c < 0xa0))
              break;

            cp[cpi++] = c;
          }
        }

        ecma48_on_parser_text(e48, cp, cpi);

        if(finished)
          return pos;

        // pos is now the first character we didn't like
        pos--;

        eaten = pos + 1;
      }
      break;

    }
  }

  return eaten;
}
