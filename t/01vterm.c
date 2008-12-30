#include "CUnit/CUnit.h"

#include "vterm.h"

vterm_t *vt;

int vterm_init(void)
{
  vt = vterm_new(25, 80);
  return vt ? 0 : 1;
}

static void test_getsize(void)
{
  int rows, cols;
  vterm_get_size(vt, &rows, &cols);

  CU_ASSERT_EQUAL(rows, 25);
  CU_ASSERT_EQUAL(cols, 80);
}

static void test_setsize(void)
{
  vterm_set_size(vt, 40, 100);

  int rows, cols;
  vterm_get_size(vt, &rows, &cols);

  CU_ASSERT_EQUAL(rows, 40);
  CU_ASSERT_EQUAL(cols, 100);
}


int vterm_fini(void)
{
  // TODO: free
  return 0;
}

#include "01vterm.inc"
