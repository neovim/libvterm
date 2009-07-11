#include "CUnit/CUnit.h"

#include "vterm.h"

#include <glib.h>

static vterm_t *vt;

static vterm_position_t cursor;
static vterm_rectangle_t rect;
static int down;
static int right;

static int cb_movecursor(vterm_t *_vt, vterm_position_t pos, vterm_position_t oldpos, int visible)
{
  cursor = pos;

  return 1;
}

static int cb_scroll(vterm_t *_vt, vterm_rectangle_t _rect, int downward, int rightward)
{
  rect = _rect;

  down = downward;
  right = rightward;

  return 1;
}

static vterm_state_callbacks_t state_cbs = {
  .movecursor = cb_movecursor,
  .scroll     = cb_scroll,
};

int state_scroll_init(void)
{
  vt = vterm_new(25, 80);
  if(!vt)
    return 1;

  vterm_parser_set_utf8(vt, 1);
  vterm_set_state_callbacks(vt, &state_cbs);

  return 0;
}

static void test_linefeed(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  down = 0;
  right = 0;

  // We're currently at (0,0). After 24 linefeeds we won't scroll
  int i;
  for(i = 0; i < 24; i++)
    vterm_push_bytes(vt, "\n", 1);

  CU_ASSERT_EQUAL(down,  0);
  CU_ASSERT_EQUAL(right, 0);

  CU_ASSERT_EQUAL(cursor.row, 24);

  vterm_push_bytes(vt, "\n", 1);

  CU_ASSERT_EQUAL(down,  1);
  CU_ASSERT_EQUAL(right, 0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   25);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 24);
}

static void test_ind(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  down = 0;
  right = 0;

  vterm_push_bytes(vt, "\e[25H", 5);
  vterm_push_bytes(vt, "\eD", 2);

  CU_ASSERT_EQUAL(down,  1);
  CU_ASSERT_EQUAL(right, 0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   25);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);
}

static void test_ri(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  down = 0;
  right = 0;

  vterm_push_bytes(vt, "\eM", 2);

  CU_ASSERT_EQUAL(down,  -1);
  CU_ASSERT_EQUAL(right,  0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   25);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 0);
}

static void test_region(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  CU_ASSERT_EQUAL(cursor.row, 0);

  down = 0;
  right = 0;

  vterm_push_bytes(vt, "\e[1;10r", 7);

  CU_ASSERT_EQUAL(down,  0);
  CU_ASSERT_EQUAL(right, 0);

  CU_ASSERT_EQUAL(cursor.row, 0);

  int i;
  for(i = 0; i < 9; i++)
    vterm_push_bytes(vt, "\n", 1);

  CU_ASSERT_EQUAL(down,  0);
  CU_ASSERT_EQUAL(right, 0);

  CU_ASSERT_EQUAL(cursor.row, 9);

  vterm_push_bytes(vt, "\n", 1);

  CU_ASSERT_EQUAL(down,  1);
  CU_ASSERT_EQUAL(right, 0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   10);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 9);

  down = 0;
  right = 0;

  vterm_push_bytes(vt, "\e[9;10r", 7);
  vterm_push_bytes(vt, "\eM", 2);

  CU_ASSERT_EQUAL(down,  0);
  CU_ASSERT_EQUAL(right, 0);

  CU_ASSERT_EQUAL(cursor.row, 8);

  vterm_push_bytes(vt, "\eM", 2);

  CU_ASSERT_EQUAL(down,  -1);
  CU_ASSERT_EQUAL(right,  0);
  CU_ASSERT_EQUAL(rect.start_row,  8);
  CU_ASSERT_EQUAL(rect.end_row,   10);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 8);

  vterm_push_bytes(vt, "\e[25H", 5);

  down = 0;
  right = 0;

  CU_ASSERT_EQUAL(cursor.row, 24);

  vterm_push_bytes(vt, "\n", 1);

  CU_ASSERT_EQUAL(down,  0);
  CU_ASSERT_EQUAL(right, 0);

  CU_ASSERT_EQUAL(cursor.row, 24);
}

static void test_sdsu(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  down = 0;
  right = 0;

  vterm_push_bytes(vt, "\e[S", 3);

  CU_ASSERT_EQUAL(down,  1);
  CU_ASSERT_EQUAL(right, 0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   25);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 0);

  vterm_push_bytes(vt, "\e[2S", 4);

  CU_ASSERT_EQUAL(down,  2);
  CU_ASSERT_EQUAL(right, 0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   25);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 0);

  vterm_push_bytes(vt, "\e[T", 3);

  CU_ASSERT_EQUAL(down, -1);
  CU_ASSERT_EQUAL(right, 0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   25);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 0);

  vterm_push_bytes(vt, "\e[2T", 4);

  CU_ASSERT_EQUAL(down, -2);
  CU_ASSERT_EQUAL(right, 0);
  CU_ASSERT_EQUAL(rect.start_row,  0);
  CU_ASSERT_EQUAL(rect.end_row,   25);
  CU_ASSERT_EQUAL(rect.start_col,  0);
  CU_ASSERT_EQUAL(rect.end_col,   80);

  CU_ASSERT_EQUAL(cursor.row, 0);
}

#include "12state_scroll.inc"
