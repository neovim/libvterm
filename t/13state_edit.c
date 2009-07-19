#include "CUnit/CUnit.h"

#include "vterm.h"

#include <glib.h>

static VTerm *vt;

char buffer[25][80];
static VTermPos cursor;

static int cb_putglyph(const uint32_t chars[], int _width, VTermPos pos, void *user)
{
  buffer[pos.row][pos.col] = chars[0];

  return 1;
}

static int cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
  cursor = pos;

  return 1;
}

static int cb_copycell(VTermPos dest, VTermPos src, void *user)
{
  buffer[dest.row][dest.col] = buffer[src.row][src.col];

  return 1;
}

static int cb_erase(VTermRect rect, void *user)
{
  for(int row = rect.start_row; row < rect.end_row; row++)
    for(int col = rect.start_col; col < rect.end_col; col++)
      buffer[row][col] = 0;

  return 1;
}

static VTermStateCallbacks state_cbs = {
  .putglyph   = cb_putglyph,
  .movecursor = cb_movecursor,
  .copycell   = cb_copycell,
  .erase      = cb_erase,
};

int state_edit_init(void)
{
  vt = vterm_new(25, 80);
  if(!vt)
    return 1;

  vterm_parser_set_utf8(vt, 1);
  vterm_set_state_callbacks(vt, &state_cbs, NULL);

  return 0;
}

static void test_ich(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  CU_ASSERT_EQUAL(buffer[0][0], 0);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "A", 1);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "CD", 2);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 'C');
  CU_ASSERT_EQUAL(buffer[0][2], 'D');

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 3);

  vterm_push_bytes(vt, "\e[2D", 4);
  vterm_push_bytes(vt, "\e[@", 3);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 'C');
  CU_ASSERT_EQUAL(buffer[0][3], 'D');

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "B", 1);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 'B');
  CU_ASSERT_EQUAL(buffer[0][2], 'C');
  CU_ASSERT_EQUAL(buffer[0][3], 'D');

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 2);

  vterm_push_bytes(vt, "\e[3@", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 'B');
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[0][3], 0);
  CU_ASSERT_EQUAL(buffer[0][4], 0);
  CU_ASSERT_EQUAL(buffer[0][5], 'C');
  CU_ASSERT_EQUAL(buffer[0][6], 'D');
}

static void test_dch(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABBC", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 'B');
  CU_ASSERT_EQUAL(buffer[0][2], 'B');
  CU_ASSERT_EQUAL(buffer[0][3], 'C');

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 4);

  vterm_push_bytes(vt, "\e[3D", 4);
  vterm_push_bytes(vt, "\e[P", 3);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 'B');
  CU_ASSERT_EQUAL(buffer[0][2], 'C');
  CU_ASSERT_EQUAL(buffer[0][3], 0);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "\e[3P", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[0][3], 0);
}

static void test_ech(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABC", 3);

  vterm_push_bytes(vt, "\e[2D", 4);
  vterm_push_bytes(vt, "\e[X", 3);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 'C');

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "\e[3X", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[0][3], 0);

  CU_ASSERT_EQUAL(cursor.row, 0);
  CU_ASSERT_EQUAL(cursor.col, 1);
}

static void test_il(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "A\r\nC", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[1][0], 'C');

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "\e[L", 3);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[1][0], 0);
  CU_ASSERT_EQUAL(buffer[2][0], 'C');

  CU_ASSERT_EQUAL(cursor.row, 1);
  /* TODO: ECMA-48 says we should move to line home, but neither xterm nor
   * xfce4-terminal do this
   */
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "\r", 1);
  vterm_push_bytes(vt, "B", 1);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[1][0], 'B');
  CU_ASSERT_EQUAL(buffer[2][0], 'C');

  vterm_push_bytes(vt, "\e[3L", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[1][0], 0);
  CU_ASSERT_EQUAL(buffer[2][0], 0);
  CU_ASSERT_EQUAL(buffer[3][0], 0);
  CU_ASSERT_EQUAL(buffer[4][0], 'B');
  CU_ASSERT_EQUAL(buffer[5][0], 'C');
}

static void test_dl(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "A\r\nB\r\nB\r\nC", 10);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[1][0], 'B');
  CU_ASSERT_EQUAL(buffer[2][0], 'B');
  CU_ASSERT_EQUAL(buffer[3][0], 'C');

  CU_ASSERT_EQUAL(cursor.row, 3);
  CU_ASSERT_EQUAL(cursor.col, 1);

  vterm_push_bytes(vt, "\e[2H", 4);
  vterm_push_bytes(vt, "\e[M", 3);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[1][0], 'B');
  CU_ASSERT_EQUAL(buffer[2][0], 'C');
  CU_ASSERT_EQUAL(buffer[3][0], 0);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 0);

  vterm_push_bytes(vt, "\e[3M", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[1][0], 0);
  CU_ASSERT_EQUAL(buffer[2][0], 0);
  CU_ASSERT_EQUAL(buffer[3][0], 0);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 0);
}

static void test_el0(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABCDE", 5);

  vterm_push_bytes(vt, "\e[3D", 4);
  vterm_push_bytes(vt, "\e[0K", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 'B');
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[0][3], 0);
  CU_ASSERT_EQUAL(buffer[0][4], 0);

  CU_ASSERT_EQUAL(cursor.col, 2);
}

static void test_el1(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABCDE", 5);

  vterm_push_bytes(vt, "\e[3D", 4);
  vterm_push_bytes(vt, "\e[1K", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 0);
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[0][3], 'D');
  CU_ASSERT_EQUAL(buffer[0][4], 'E');

  CU_ASSERT_EQUAL(cursor.col, 2);
}

static void test_el2(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABCDE", 5);

  vterm_push_bytes(vt, "\e[3D", 4);
  vterm_push_bytes(vt, "\e[2K", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 0);
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[0][3], 0);
  CU_ASSERT_EQUAL(buffer[0][4], 0);

  CU_ASSERT_EQUAL(cursor.col, 2);
}

static void test_ed0(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABC\r\nDEF\r\nGHI", 13);

  vterm_push_bytes(vt, "\e[2;2H", 6);
  vterm_push_bytes(vt, "\e[0J", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 'A');
  CU_ASSERT_EQUAL(buffer[0][1], 'B');
  CU_ASSERT_EQUAL(buffer[0][2], 'C');
  CU_ASSERT_EQUAL(buffer[1][0], 'D');
  CU_ASSERT_EQUAL(buffer[1][1], 0);
  CU_ASSERT_EQUAL(buffer[1][2], 0);
  CU_ASSERT_EQUAL(buffer[2][0], 0);
  CU_ASSERT_EQUAL(buffer[2][1], 0);
  CU_ASSERT_EQUAL(buffer[2][2], 0);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 1);
}

static void test_ed1(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABC\r\nDEF\r\nGHI", 13);

  vterm_push_bytes(vt, "\e[2;2H", 6);
  vterm_push_bytes(vt, "\e[1J", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 0);
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[1][0], 0);
  CU_ASSERT_EQUAL(buffer[1][1], 0);
  CU_ASSERT_EQUAL(buffer[1][2], 'F');
  CU_ASSERT_EQUAL(buffer[2][0], 'G');
  CU_ASSERT_EQUAL(buffer[2][1], 'H');
  CU_ASSERT_EQUAL(buffer[2][2], 'I');

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 1);
}

static void test_ed2(void)
{
  vterm_state_initialise(vt);
  vterm_state_get_cursorpos(vt, &cursor);

  vterm_push_bytes(vt, "ABC\r\nDEF\r\nGHI", 13);

  vterm_push_bytes(vt, "\e[2;2H", 6);
  vterm_push_bytes(vt, "\e[2J", 4);

  CU_ASSERT_EQUAL(buffer[0][0], 0);
  CU_ASSERT_EQUAL(buffer[0][1], 0);
  CU_ASSERT_EQUAL(buffer[0][2], 0);
  CU_ASSERT_EQUAL(buffer[1][0], 0);
  CU_ASSERT_EQUAL(buffer[1][1], 0);
  CU_ASSERT_EQUAL(buffer[1][2], 0);
  CU_ASSERT_EQUAL(buffer[2][0], 0);
  CU_ASSERT_EQUAL(buffer[2][1], 0);
  CU_ASSERT_EQUAL(buffer[2][2], 0);

  CU_ASSERT_EQUAL(cursor.row, 1);
  CU_ASSERT_EQUAL(cursor.col, 1);
}

#include "13state_edit.inc"
