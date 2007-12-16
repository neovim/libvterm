#include "CUnit/CUnit.h"

#include "ecma48.h"

#include <glib.h>

static ecma48_t *e48;

static int cb_count;
static int cb_n;
static int cb_p[256];

static int cb_text(ecma48_t *_e48, int codepoints[], int npoints)
{
  cb_count++;
  cb_n = npoints;
  memcpy(cb_p, codepoints, npoints * sizeof(codepoints[0]));

  return 1;
}

static ecma48_parser_callbacks_t parser_cbs = {
  .text = cb_text,
};

int parser_utf8_init(void)
{
  e48 = ecma48_new(80, 25);
  if(!e48)
    return 1;

  ecma48_parser_set_utf8(e48, 1);
  ecma48_set_parser_callbacks(e48, &parser_cbs);

  return 0;
}

static void test_low(void)
{
  cb_count = 0;
  ecma48_push_bytes(e48, "123", 3);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 3);
  CU_ASSERT_EQUAL(cb_p[0], '1');
  CU_ASSERT_EQUAL(cb_p[1], '2');
  CU_ASSERT_EQUAL(cb_p[2], '3');
}

/* We want to prove the UTF-8 parser correctly handles all the sequences.
 * Easy way to do this is to check it does low/high boundary cases, as that
 * leaves only two for each sequence length
 *
 * These ranges are therefore:
 *
 * Two bytes:
 * U+0080 = 000 10000000 =>    00010   000000
 *                       => 11000010 10000000 = C2 80
 * U+07FF = 111 11111111 =>    11111   111111
 *                       => 11011111 10111111 = DF BF
 *
 * Three bytes:
 * U+0800 = 00001000 00000000 =>     0000   100000   000000
 *                            => 11100000 10100000 10000000 = E0 A0 80
 * U+FFFF = 11111111 11111111 =>     1111   111111   111111
 *                            => 11101111 10111111 10111111 = EF BF BF
 *
 * Four bytes:
 * U+10000  = 00001 00000000 00000000 =>      000   010000   000000   000000
 *                                    => 11110000 10010000 10000000 10000000 = F0 90 80 80
 * U+1FFFFF = 11111 11111111 11111111 =>      111   111111   111111   111111
 *                                    => 11110111 10111111 10111111 10111111 = F7 BF BF BF
 *
 */

static void test_2byte(void)
{
  cb_count = 0;
  ecma48_push_bytes(e48, "\xC2\x80\xDF\xBF", 4);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], 0x0080);
  CU_ASSERT_EQUAL(cb_p[1], 0x07FF);
}

static void test_3byte(void)
{
  cb_count = 0;
  ecma48_push_bytes(e48, "\xE0\xA0\x80\xEF\xBF\xBF", 6);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], 0x0800);
  CU_ASSERT_EQUAL(cb_p[1], 0xFFFF);
}

static void test_4byte(void)
{
  cb_count = 0;
  ecma48_push_bytes(e48, "\xF0\x90\x80\x80\xF7\xBF\xBF\xBF", 8);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], 0x010000);
  CU_ASSERT_EQUAL(cb_p[1], 0x1FFFFF);
}

/* Next up, we check some invalid sequences
 *  + Early termination (back to low bytes too soon)
 *  + Early restart (another sequence introduction before the previous one was finished)
 */

#define UTF8_INVALID 0xFFFD

static void test_earlyterm(void)
{
  // Two byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xC2!", 2);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], '!');

  // Three byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xE0!\xE0\xA0!", 5);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 4);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], '!');
  CU_ASSERT_EQUAL(cb_p[2], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[3], '!');

  // Four byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xF0!\xF0\x90!\xF0\x90\x80!", 9);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 6);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], '!');
  CU_ASSERT_EQUAL(cb_p[2], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[3], '!');
  CU_ASSERT_EQUAL(cb_p[4], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[5], '!');
}

static void test_earlyrestart(void)
{
  // Two byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xC2\xC2\x90", 3);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], 0x0090);

  // Three byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xE0\xC2\x90\xE0\xA0\xC2\x90", 7);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 4);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], 0x0090);
  CU_ASSERT_EQUAL(cb_p[2], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[3], 0x0090);

  // Four byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xF0\xC2\x90\xF0\x90\xC2\x90\xF0\x90\x80\xC2\x90", 12);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 6);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], 0x0090);
  CU_ASSERT_EQUAL(cb_p[2], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[3], 0x0090);
  CU_ASSERT_EQUAL(cb_p[4], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[5], 0x0090);
}

/* Test the overlong sequences by giving an overlong encoding of U+0000 and
 * an encoding of the highest codepoint still too short
 *
 * Two bytes:
 * U+0000 = C0 80
 * U+007F = 000 01111111 =>    00001   111111 =>
 *                       => 11000001 10111111 => C1 BF
 *
 * Three bytes:
 * U+0000 = E0 80 80
 * U+07FF = 00000111 11111111 =>     0000   011111   111111
 *                            => 11100000 10011111 10111111 = E0 9F BF
 *
 * Four bytes:
 * U+0000 = F0 80 80 80
 * U+FFFF = 11111111 11111111 =>      000   001111   111111   111111
 *                            => 11110000 10001111 10111111 10111111 = F0 8F BF BF
 */

static void test_overlong(void)
{
  // Two byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xC0\x80\xC1\xBF", 4);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], UTF8_INVALID);

  // Three byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xE0\x80\x80\xE0\x9F\xBF", 6);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], UTF8_INVALID);

  // Four byte
  cb_count = 0;
  ecma48_push_bytes(e48, "\xF0\x80\x80\x80\xF0\x8F\xBF\xBF", 8);

  CU_ASSERT_EQUAL(cb_count, 1);
  CU_ASSERT_EQUAL(cb_n, 2);
  CU_ASSERT_EQUAL(cb_p[0], UTF8_INVALID);
  CU_ASSERT_EQUAL(cb_p[1], UTF8_INVALID);
}

#include "03parser_utf8.inc"
