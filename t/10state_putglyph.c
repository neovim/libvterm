#include "CUnit/CUnit.h"

#include "vterm.h"

#include <glib.h>

static VTerm *vt;

#define MAX_CP 100
static uint32_t codepoints[MAX_CP];
static int width[MAX_CP];
static int column[MAX_CP];
static int this_cp;

static int cb_putglyph(VTerm *_vt, const uint32_t chars[], int _width, VTermPos pos, void *pen)
{
  CU_ASSERT_PTR_EQUAL(vt, _vt);

  CU_ASSERT_TRUE_FATAL(this_cp < MAX_CP);

  int i;
  for(i = 0; chars[i]; i++) {
    codepoints[this_cp] = chars[i];
    width[this_cp] = _width;
    column[this_cp] = pos.col;

    this_cp++;
  }

  return 1;
}

static VTermStateCallbacks state_cbs = {
  .putglyph = cb_putglyph,
};

int state_putglyph_init(void)
{
  vt = vterm_new(25, 80);
  if(!vt)
    return 1;

  vterm_parser_set_utf8(vt, 1);
  vterm_set_state_callbacks(vt, &state_cbs);

  return 0;
}

static void test_low(void)
{
  this_cp = 0;
  vterm_state_initialise(vt);

  vterm_push_bytes(vt, "ABC", 3);

  CU_ASSERT_EQUAL(this_cp, 3);

  CU_ASSERT_EQUAL(codepoints[0], 'A');
  CU_ASSERT_EQUAL(width[0], 1);
  CU_ASSERT_EQUAL(column[0], 0);

  CU_ASSERT_EQUAL(codepoints[1], 'B');
  CU_ASSERT_EQUAL(width[1], 1);
  CU_ASSERT_EQUAL(column[1], 1);

  CU_ASSERT_EQUAL(codepoints[2], 'C');
  CU_ASSERT_EQUAL(width[2], 1);
  CU_ASSERT_EQUAL(column[2], 2);
}

static void test_uni_1char(void)
{
  this_cp = 0;
  vterm_state_initialise(vt);

  /* U+00C1 = 0xC3 0x81  name: LATIN CAPITAL LETTER A WITH ACUTE
   * U+00E9 = 0xC3 0xA9  name: LATIN SMALL LETTER E WITH ACUTE
   */
  vterm_push_bytes(vt, "\xC3\x81\xC3\xA9", 4);

  CU_ASSERT_EQUAL(this_cp, 2);

  CU_ASSERT_EQUAL(codepoints[0], 0x00C1);
  CU_ASSERT_EQUAL(width[0], 1);
  CU_ASSERT_EQUAL(column[0], 0);

  CU_ASSERT_EQUAL(codepoints[1], 0x00E9);
  CU_ASSERT_EQUAL(width[1], 1);
  CU_ASSERT_EQUAL(column[1], 1);
}

static void test_uni_widechar(void)
{
  this_cp = 0;
  vterm_state_initialise(vt);

  /* U+FF10 = 0xEF 0xBC 0x90  name: FULLWIDTH DIGIT ZERO
   */
  vterm_push_bytes(vt, "\xEF\xBC\x90 ", 4);

  CU_ASSERT_EQUAL(this_cp, 2);

  CU_ASSERT_EQUAL(codepoints[0], 0xFF10);
  CU_ASSERT_EQUAL(width[0], 2);
  CU_ASSERT_EQUAL(column[0], 0);

  CU_ASSERT_EQUAL(codepoints[1], ' ');
  CU_ASSERT_EQUAL(width[1], 1);
  CU_ASSERT_EQUAL(column[1], 2);
}

static void test_uni_combining(void)
{
  this_cp = 0;
  vterm_state_initialise(vt);

  /* U+0301 = 0xCC 0x81  name: COMBINING ACUTE
   */
  vterm_push_bytes(vt, "e\xCC\x81Z", 4);

  CU_ASSERT_EQUAL(this_cp, 3);

  CU_ASSERT_EQUAL(codepoints[0], 'e');
  CU_ASSERT_EQUAL(codepoints[1], 0x0301);
  CU_ASSERT_EQUAL(width[0], 1);
  CU_ASSERT_EQUAL(column[0], 0);

  CU_ASSERT_EQUAL(codepoints[2], 'Z');
  CU_ASSERT_EQUAL(width[2], 1);
  CU_ASSERT_EQUAL(column[2], 1);

  /* Now try it again, split across two buffers. Since the text implementation
   * cannot know the second will be coming, it has to emit the bare 'e' first
   */
  this_cp = 0;
  vterm_state_initialise(vt);

  vterm_push_bytes(vt, "e", 1);

  CU_ASSERT_EQUAL(this_cp, 1);

  CU_ASSERT_EQUAL(codepoints[0], 'e');
  CU_ASSERT_EQUAL(width[0], 1);
  CU_ASSERT_EQUAL(column[0], 0);

  vterm_push_bytes(vt, "\xCC\x81Z", 3);

  CU_ASSERT_EQUAL(this_cp, 4);

  CU_ASSERT_EQUAL(codepoints[1], 'e');
  CU_ASSERT_EQUAL(codepoints[2], 0x0301);
  CU_ASSERT_EQUAL(width[1], 1);
  CU_ASSERT_EQUAL(column[1], 0);

  CU_ASSERT_EQUAL(codepoints[3], 'Z');
  CU_ASSERT_EQUAL(width[3], 1);
  CU_ASSERT_EQUAL(column[3], 1);

  /* Now try again with two combining chars, split across three buffers, to
   * ensure the saving/combining logic both interact */
  this_cp = 0;
  vterm_state_initialise(vt);

  vterm_push_bytes(vt, "e", 1);

  CU_ASSERT_EQUAL(this_cp, 1);

  CU_ASSERT_EQUAL(codepoints[0], 'e');
  CU_ASSERT_EQUAL(width[0], 1);
  CU_ASSERT_EQUAL(column[0], 0);

  vterm_push_bytes(vt, "\xCC\x81", 2);

  CU_ASSERT_EQUAL(this_cp, 3);

  CU_ASSERT_EQUAL(codepoints[1], 'e');
  CU_ASSERT_EQUAL(codepoints[2], 0x0301);
  CU_ASSERT_EQUAL(width[1], 1);
  CU_ASSERT_EQUAL(column[1], 0);

  vterm_push_bytes(vt, "\xCC\x82", 2);

  CU_ASSERT_EQUAL(this_cp, 6);

  CU_ASSERT_EQUAL(codepoints[3], 'e');
  CU_ASSERT_EQUAL(codepoints[4], 0x0301);
  CU_ASSERT_EQUAL(codepoints[5], 0x0302);
  CU_ASSERT_EQUAL(width[3], 1);
  CU_ASSERT_EQUAL(column[3], 0);
}

#include "10state_putglyph.inc"
