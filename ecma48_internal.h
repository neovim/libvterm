#ifndef __ECMA48_INTERNAL_H__
#define __ECMA48_INTERNAL_H__

#include "ecma48.h"

#include <glib.h>

struct ecma48_s
{
  int rows;
  int cols;

  ecma48_parser_callbacks_t *parser_callbacks;

  GString *buffer;
};

size_t ecma48_parser_interpret_bytes(ecma48_t *e48, char *bytes, size_t len);

#endif
