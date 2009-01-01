#include "CUnit/CUnit.h"

#include "vterm.h"

#include <glib.h>

static vterm_t *vt;

static vterm_position_t cursor;

static int cb_putglyph(vterm_t *_vt, const uint32_t chars[], int _width, vterm_position_t pos, void *pen)
{
  return 1;
}

static int cb_movecursor(vterm_t *_vt, vterm_position_t pos, vterm_position_t oldpos, int visible)
{
  cursor = pos;

  return 1;
}

static vterm_state_callbacks_t state_cbs = {
  .putglyph   = cb_putglyph,
  .movecursor = cb_movecursor,
};

int state_movecursor_init(void)
{
  vt = vterm_new(25, 80);
  if(!vt)
    return 1;

  vterm_parser_set_utf8(vt, 1);
  vterm_set_state_callbacks(vt, &state_cbs);

  return 0;
}

static void test_initial(void)
{
  vterm_state_initialise(vt);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);
}

static void test_c0(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABC", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 3);

  vterm_push_bytes(vt, "\b", 1);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 2);

  vterm_push_bytes(vt, "\t", 1);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 8);

  vterm_push_bytes(vt, "\r", 1);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\n", 1);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 0);
}

static void test_c1(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABC", 3);
  vterm_push_bytes(vt, "\eD", 2);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 3);

  vterm_push_bytes(vt, "\eM", 2);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 3);

  vterm_push_bytes(vt, "\eE", 2);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 0);
}

static void test_cu_basic(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "\e[B", 3);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[3B", 4);

  CU_ASSERT_EQUAL(cursor.row, 4);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[C", 3);

  CU_ASSERT_EQUAL(cursor.row, 4);
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "\e[3C", 4);

  CU_ASSERT_EQUAL(cursor.row, 4);
  CU_ASSERT_EQUAL(cursor.col, 4);

  vterm_push_bytes(vt, "\e[A", 3);

  CU_ASSERT_EQUAL(cursor.row, 3);
  CU_ASSERT_EQUAL(cursor.col, 4);

  vterm_push_bytes(vt, "\e[3A", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 4);

  vterm_push_bytes(vt, "\e[D", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 3);

  vterm_push_bytes(vt, "\e[3D", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "   ", 3);
  vterm_push_bytes(vt, "\e[E", 3);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "   ", 3);
  vterm_push_bytes(vt, "\e[2E", 4);

  CU_ASSERT_EQUAL(cursor.row, 3);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "   ", 3);
  vterm_push_bytes(vt, "\e[F", 3);

  CU_ASSERT_EQUAL(cursor.row, 2);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "   ", 3);
  vterm_push_bytes(vt, "\e[2F", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\n", 1);
  vterm_push_bytes(vt, "\e[20G", 5);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 19);

  vterm_push_bytes(vt, "\e[G", 3);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[10;5H", 7);

  CU_ASSERT_EQUAL(cursor.row, 9);
  CU_ASSERT_EQUAL(cursor.col, 4);

  vterm_push_bytes(vt, "\e[8H", 4);

  CU_ASSERT_EQUAL(cursor.row, 7);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\n", 1);
  vterm_push_bytes(vt, "\e[H", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);
}

static void test_cu_bounds(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "\e[A", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[D", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[25;80H", 8);

  CU_ASSERT_EQUAL(cursor.row, 24);
  CU_ASSERT_EQUAL(cursor.col, 79);

  vterm_push_bytes(vt, "\e[B", 3);

  CU_ASSERT_EQUAL(cursor.row, 24);
  CU_ASSERT_EQUAL(cursor.col, 79);

  vterm_push_bytes(vt, "\e[C", 3);

  CU_ASSERT_EQUAL(cursor.row, 24);
  CU_ASSERT_EQUAL(cursor.col, 79);

  vterm_push_bytes(vt, "\e[E", 3);

  CU_ASSERT_EQUAL(cursor.row, 24);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[H", 3);
  vterm_push_bytes(vt, "\e[F", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[999G", 6);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 79);

  vterm_push_bytes(vt, "\e[99;99H", 8);

  CU_ASSERT_EQUAL(cursor.row, 24);
  CU_ASSERT_EQUAL(cursor.col, 79);
}

static void test_hvp_basic(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "\e[5`", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 4);

  vterm_push_bytes(vt, "\e[3a", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 7);

  vterm_push_bytes(vt, "\e[3j", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 4);

  vterm_push_bytes(vt, "\e[3;3f", 6);

  CU_ASSERT_EQUAL(cursor.row, 2);
  CU_ASSERT_EQUAL(cursor.col, 2);

  vterm_push_bytes(vt, "\e[5d", 4);

  CU_ASSERT_EQUAL(cursor.row, 4);
  CU_ASSERT_EQUAL(cursor.col, 2);

  vterm_push_bytes(vt, "\e[2e", 4);

  CU_ASSERT_EQUAL(cursor.row, 6);
  CU_ASSERT_EQUAL(cursor.col, 2);

  vterm_push_bytes(vt, "\e[2k", 4);

  CU_ASSERT_EQUAL(cursor.row, 4);
  CU_ASSERT_EQUAL(cursor.col, 2);
}

static void test_tabs(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "\t", 1);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 8);

  vterm_push_bytes(vt, "   ", 3);
  vterm_push_bytes(vt, "\t", 1);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 16);

  vterm_push_bytes(vt, "       ", 7);
  vterm_push_bytes(vt, "\t", 1);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 24);

  vterm_push_bytes(vt, "        ", 8);
  vterm_push_bytes(vt, "\t", 1);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 40);

  vterm_push_bytes(vt, "\e[I", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 48);

  vterm_push_bytes(vt, "\e[2I", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 64);

  vterm_push_bytes(vt, "\e[Z", 3);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 56);

  vterm_push_bytes(vt, "\e[2Z", 4);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 40);
}

#include "11state_movecursor.inc"
