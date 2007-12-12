#include "CUnit/CUnit.h"

#include "ecma48.h"

ecma48_t *e48;

int ecma48_init(void)
{
  e48 = ecma48_new(80, 25);
  return e48 ? 0 : 1;
}

static void test_getsize(void)
{
  int rows, cols;
  ecma48_get_size(e48, &rows, &cols);

  CU_ASSERT_EQUAL(rows, 80);
  CU_ASSERT_EQUAL(cols, 25);
}

static void test_setsize(void)
{
  ecma48_set_size(e48, 100, 40);

  int rows, cols;
  ecma48_get_size(e48, &rows, &cols);

  CU_ASSERT_EQUAL(rows, 100);
  CU_ASSERT_EQUAL(cols, 40);
}


int ecma48_fini(void)
{
  // TODO: free
  return 0;
}

#include "01ecma48.inc"
