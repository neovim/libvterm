#include "CUnit/CUnit.h"

#include "ecma48.h"

ecma48_t *e48;

static void test_size(void)
{
  ecma48_set_size(e48, 100, 40);

  int rows, cols;
  ecma48_get_size(e48, &rows, &cols);

  CU_ASSERT_EQUAL(rows, 100);
  CU_ASSERT_EQUAL(cols, 40);
}

int ecma48_init(void)
{
  e48 = ecma48_new();
  return e48 ? 0 : 1;
}

int ecma48_fini(void)
{
  // TODO: free
  return 0;
}

#include "01ecma48.inc"
