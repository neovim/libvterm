#ifndef __ECMA48_INTERNAL_H__
#define __ECMA48_INTERNAL_H__

#include "ecma48.h"

#include <glib.h>

struct ecma48_state
{
  ecma48_parser_callbacks_t *parser_callbacks;

  GString *buffer;
};


#endif
