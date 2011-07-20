#include "vterm_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

/*****************
 * API functions *
 *****************/

VTerm *vterm_new(int rows, int cols)
{
  VTerm *vt = g_new0(VTerm, 1);

  vt->rows = rows;
  vt->cols = cols;

  vt->inbuffer = g_string_new(NULL);
  vt->outbuffer = g_string_new(NULL);

  return vt;
}

void vterm_get_size(VTerm *vt, int *rowsp, int *colsp)
{
  if(rowsp)
    *rowsp = vt->rows;
  if(colsp)
    *colsp = vt->cols;
}

void vterm_set_size(VTerm *vt, int rows, int cols)
{
  vt->rows = rows;
  vt->cols = cols;

  if(vt->parser_callbacks && vt->parser_callbacks->resize)
    (*vt->parser_callbacks->resize)(rows, cols, vt->cbdata);
}

void vterm_set_parser_callbacks(VTerm *vt, const VTermParserCallbacks *callbacks, void *user)
{
  vt->parser_callbacks = callbacks;
  vt->cbdata = user;
}

void vterm_parser_set_utf8(VTerm *vt, int is_utf8)
{
  vt->is_utf8 = is_utf8;
}

void vterm_push_bytes(VTerm *vt, const char *bytes, size_t len)
{
  if((vt->inbuffer->len)) {
    g_string_append_len(vt->inbuffer, bytes, len);
    size_t eaten = vterm_parser_interpret_bytes(vt, vt->inbuffer->str, vt->inbuffer->len);
    g_string_erase(vt->inbuffer, 0, eaten);
  }
  else {
    size_t eaten = vterm_parser_interpret_bytes(vt, bytes, len);
    if(eaten < len)
      g_string_append_len(vt->inbuffer, bytes + eaten, len - eaten);
  }
}

void vterm_push_output_bytes(VTerm *vt, const char *bytes, size_t len)
{
  g_string_append_len(vt->outbuffer, bytes, len);
}

void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args)
{
  g_string_append_vprintf(vt->outbuffer, format, args);
}

void vterm_push_output_sprintf(VTerm *vt, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vterm_push_output_vsprintf(vt, format, args);
  va_end(args);
}

size_t vterm_output_bufferlen(VTerm *vt)
{
  return vt->outbuffer->len;
}

size_t vterm_output_bufferread(VTerm *vt, char *buffer, size_t len)
{
  if(len > vt->outbuffer->len)
    len = vt->outbuffer->len;

  strncpy(buffer, vt->outbuffer->str, len);

  g_string_erase(vt->outbuffer, 0, len);

  return len;
}

VTermValueType vterm_get_attr_type(VTermAttr attr)
{
  switch(attr) {
    case VTERM_ATTR_BOLD:       return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_UNDERLINE:  return VTERM_VALUETYPE_INT;
    case VTERM_ATTR_ITALIC:     return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_BLINK:      return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_REVERSE:    return VTERM_VALUETYPE_BOOL;
    case VTERM_ATTR_FONT:       return VTERM_VALUETYPE_INT;
    case VTERM_ATTR_FOREGROUND: return VTERM_VALUETYPE_COLOR;
    case VTERM_ATTR_BACKGROUND: return VTERM_VALUETYPE_COLOR;
  }
  return 0; /* UNREACHABLE */
}

VTermValueType vterm_get_prop_type(VTermProp prop)
{
  switch(prop) {
    case VTERM_PROP_CURSORVISIBLE: return VTERM_VALUETYPE_BOOL;
    case VTERM_PROP_CURSORBLINK:   return VTERM_VALUETYPE_BOOL;
    case VTERM_PROP_ALTSCREEN:     return VTERM_VALUETYPE_BOOL;
    case VTERM_PROP_TITLE:         return VTERM_VALUETYPE_STRING;
    case VTERM_PROP_ICONNAME:      return VTERM_VALUETYPE_STRING;
  }
  return 0; /* UNREACHABLE */
}
