#include "CUnit/CUnit.h"

#include "vterm.h"

#include <glib.h>

static vterm_t *vt;

static GSList *cbs;

typedef struct {
  enum { 
    CB_TEXT,
    CB_CONTROL,
    CB_ESCAPE,
    CB_CSI_RAW,
    CB_CSI,
    CB_OSC,
  } type;
  union {
    struct { int *codepoints; int npoints; } text;
    unsigned char control;
    char escape;
    struct { char *args; size_t arglen; char command; } csi_raw;
    struct { char *intermed; int *args; int argcount; char command; } csi;
    struct { char *command; size_t cmdlen; } osc;
  } val;
} cb;

static int cb_text(vterm_t *_vt, const int codepoints[], int npoints)
{
  CU_ASSERT_PTR_EQUAL(vt, _vt);

  cb *c = g_new0(cb, 1);
  c->type = CB_TEXT;
  c->val.text.npoints = npoints;
  c->val.text.codepoints = g_new0(int, npoints);
  memcpy(c->val.text.codepoints, codepoints, npoints * sizeof(codepoints[0]));

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int cb_control(vterm_t *_vt, char control)
{
  CU_ASSERT_PTR_EQUAL(vt, _vt);

  cb *c = g_new0(cb, 1);
  c->type = CB_CONTROL;
  c->val.control = control;

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int cb_escape(vterm_t *_vt, char escape)
{
  CU_ASSERT_PTR_EQUAL(vt, _vt);

  cb *c = g_new0(cb, 1);
  c->type = CB_ESCAPE;
  c->val.escape = escape;

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int capture_csi_raw = 0;

static int cb_csi_raw(vterm_t *_vt, const char *args, size_t arglen, char command)
{
  CU_ASSERT_PTR_EQUAL(vt, _vt);

  if(!capture_csi_raw)
    return 0;

  cb *c = g_new0(cb, 1);
  c->type = CB_CSI_RAW;
  c->val.csi_raw.args = g_strndup(args, arglen);
  c->val.csi_raw.arglen = arglen;
  c->val.csi_raw.command = command;

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int cb_csi(vterm_t *_vt, const char *intermed, const int args[], int argcount, char command)
{
  CU_ASSERT_PTR_EQUAL(vt, _vt);

  cb *c = g_new0(cb, 1);
  c->type = CB_CSI;
  c->val.csi.intermed = g_strdup(intermed);
  c->val.csi.argcount = argcount;
  c->val.csi.args = g_new0(int, argcount);
  memcpy(c->val.csi.args, args, argcount * sizeof(args[0]));
  c->val.csi.command = command;

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int cb_osc(vterm_t *_vt, const char *command, size_t cmdlen)
{
  CU_ASSERT_PTR_EQUAL(vt, _vt);

  cb *c = g_new0(cb, 1);
  c->type = CB_OSC;
  c->val.osc.command = g_strndup(command, cmdlen);
  c->val.osc.cmdlen = cmdlen;

  cbs = g_slist_append(cbs, c);

  return 1;
}

static void free_cbs(void)
{
  GSList *this_cb_p;
  for(this_cb_p = cbs; this_cb_p; this_cb_p = g_slist_next(this_cb_p)) {
    cb *c = this_cb_p->data;
    switch(c->type) {
    case CB_TEXT:
      g_free(c->val.text.codepoints); break;
    case CB_CONTROL:
    case CB_ESCAPE:
      break;
    case CB_CSI_RAW:
      g_free(c->val.csi_raw.args); break;
    case CB_CSI:
      g_free(c->val.csi.intermed); g_free(c->val.csi.args); break;
    case CB_OSC:
      g_free(c->val.osc.command); break;
    }

    g_free(c);
  }

  g_slist_free(cbs);
  cbs = NULL;
}

static vterm_parser_callbacks_t parser_cbs = {
  .text    = cb_text,
  .control = cb_control,
  .escape  = cb_escape,
  .csi_raw = cb_csi_raw,
  .csi     = cb_csi,
  .osc     = cb_osc,
};

int parser_init(void)
{
  vt = vterm_new(80, 25);
  if(!vt)
    return 1;

  vterm_parser_set_utf8(vt, 0);
  vterm_set_parser_callbacks(vt, &parser_cbs);

  return 0;
}

static void test_basictext(void)
{
  vterm_push_bytes(vt, "hello", 5);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_TEXT);
  CU_ASSERT_EQUAL(c->val.text.npoints, 5);
  CU_ASSERT_EQUAL(c->val.text.codepoints[0], 'h');
  CU_ASSERT_EQUAL(c->val.text.codepoints[1], 'e');
  CU_ASSERT_EQUAL(c->val.text.codepoints[2], 'l');
  CU_ASSERT_EQUAL(c->val.text.codepoints[3], 'l');
  CU_ASSERT_EQUAL(c->val.text.codepoints[4], 'o');

  free_cbs();
}

static void test_c0(void)
{
  vterm_push_bytes(vt, "\x03", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 3);

  free_cbs();

  vterm_push_bytes(vt, "\x1f", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x1f);

  free_cbs();
}

static void test_c1_8bit(void)
{
  vterm_push_bytes(vt, "\x83", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x83);

  free_cbs();

  vterm_push_bytes(vt, "\x9f", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x9f);

  free_cbs();
}

static void test_c1_7bit(void)
{
  vterm_push_bytes(vt, "\e\x43", 2);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x83);

  free_cbs();

  vterm_push_bytes(vt, "\e\x5f", 2);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x9f);

  free_cbs();
}

static void test_highbytes(void)
{
  vterm_push_bytes(vt, "\xa0\xcc\xfe", 3);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_TEXT);
  CU_ASSERT_EQUAL(c->val.text.npoints, 3);
  CU_ASSERT_EQUAL(c->val.text.codepoints[0], 0xa0);
  CU_ASSERT_EQUAL(c->val.text.codepoints[1], 0xcc);
  CU_ASSERT_EQUAL(c->val.text.codepoints[2], 0xfe);

  free_cbs();
}

static void test_mixed(void)
{
  vterm_push_bytes(vt, "1\n2", 3);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 3);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_TEXT);
  CU_ASSERT_EQUAL(c->val.text.npoints, 1);
  CU_ASSERT_EQUAL(c->val.text.codepoints[0], '1');

  c = cbs->next->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, '\n');

  c = cbs->next->next->data;
  CU_ASSERT_EQUAL(c->type, CB_TEXT);
  CU_ASSERT_EQUAL(c->val.text.npoints, 1);
  CU_ASSERT_EQUAL(c->val.text.codepoints[0], '2');

  free_cbs();
}

static void test_escape(void)
{
  vterm_push_bytes(vt, "\e=", 2);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_ESCAPE);
  CU_ASSERT_EQUAL(c->val.control, '=');

  free_cbs();
}

static void test_csi_raw(void)
{
  capture_csi_raw = 1;

  vterm_push_bytes(vt, "\e[A", 3);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI_RAW);
  CU_ASSERT_EQUAL(c->val.csi_raw.command, 'A');
  CU_ASSERT_EQUAL(c->val.csi_raw.arglen, 0);

  free_cbs();

  vterm_push_bytes(vt, "\e[15B", 5);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI_RAW);
  CU_ASSERT_EQUAL(c->val.csi_raw.command, 'B');
  CU_ASSERT_EQUAL(c->val.csi_raw.arglen, 2);
  CU_ASSERT_NSTRING_EQUAL(c->val.csi_raw.args, "15", 2);

  free_cbs();

  vterm_push_bytes(vt, "\e[?004C", 7);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI_RAW);
  CU_ASSERT_EQUAL(c->val.csi_raw.command, 'C');
  CU_ASSERT_EQUAL(c->val.csi_raw.arglen, 4);
  CU_ASSERT_NSTRING_EQUAL(c->val.csi_raw.args, "?004", 4);

  free_cbs();
}

static void test_csi_0arg(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "\e[a", 3);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'a');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 1);
  CU_ASSERT_EQUAL(c->val.csi.args[0], -1);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  free_cbs();
}

static void test_csi_1arg(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "\e[9b", 4);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'b');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 1);
  CU_ASSERT_EQUAL(c->val.csi.args[0], 9);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  free_cbs();
}

static void test_csi_2arg(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "\e[3;4c", 6);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'c');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 2);
  CU_ASSERT_EQUAL(c->val.csi.args[0], 3);
  CU_ASSERT_EQUAL(c->val.csi.args[1], 4);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  free_cbs();
}

static void test_csi_manydigits(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "\e[678d", 6);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'd');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 1);
  CU_ASSERT_EQUAL(c->val.csi.args[0], 678);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  free_cbs();
}

static void test_csi_leadingzero(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "\e[007e", 6);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'e');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 1);
  CU_ASSERT_EQUAL(c->val.csi.args[0], 7);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  free_cbs();
}

static void test_csi_qmark(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "\e[?2;7f", 7);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'f');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 2);
  CU_ASSERT_EQUAL(c->val.csi.args[0], 2);
  CU_ASSERT_EQUAL(c->val.csi.args[1], 7);
  CU_ASSERT_PTR_NOT_NULL(c->val.csi.intermed);
  CU_ASSERT_STRING_EQUAL(c->val.csi.intermed, "?");

  free_cbs();
}

static void test_mixedcsi(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "A\e[8mB", 6);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 3);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_TEXT);
  CU_ASSERT_EQUAL(c->val.text.npoints, 1);
  CU_ASSERT_EQUAL(c->val.text.codepoints[0], 'A');

  c = cbs->next->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'm');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 1);
  CU_ASSERT_EQUAL(c->val.csi.args[0], 8);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  c = cbs->next->next->data;
  CU_ASSERT_EQUAL(c->type, CB_TEXT);
  CU_ASSERT_EQUAL(c->val.text.npoints, 1);
  CU_ASSERT_EQUAL(c->val.text.codepoints[0], 'B');

  free_cbs();
}

static void test_splitwrite(void)
{
  capture_csi_raw = 0;

  vterm_push_bytes(vt, "\e", 1);

  CU_ASSERT_PTR_NULL(cbs);

  vterm_push_bytes(vt, "[a", 2);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'a');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 1);
  CU_ASSERT_EQUAL(c->val.csi.args[0], -1);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  free_cbs();

  vterm_push_bytes(vt, "foo\e[", 5);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_TEXT);
  CU_ASSERT_EQUAL(c->val.text.npoints, 3);
  CU_ASSERT_EQUAL(c->val.text.codepoints[0], 'f');
  CU_ASSERT_EQUAL(c->val.text.codepoints[1], 'o');
  CU_ASSERT_EQUAL(c->val.text.codepoints[2], 'o');

  free_cbs();

  vterm_push_bytes(vt, "4b", 2);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CSI);
  CU_ASSERT_EQUAL(c->val.csi.command, 'b');
  CU_ASSERT_EQUAL(c->val.csi.argcount, 1);
  CU_ASSERT_EQUAL(c->val.csi.args[0], 4);
  CU_ASSERT_PTR_NULL(c->val.csi.intermed);

  free_cbs();
}

static void test_osc(void)
{
  vterm_push_bytes(vt, "\e]1;Hello\x07", 10);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_OSC);
  CU_ASSERT_EQUAL(c->val.osc.cmdlen, 7);
  CU_ASSERT_STRING_EQUAL(c->val.osc.command, "1;Hello");

  free_cbs();

  vterm_push_bytes(vt, "\e]1;Hello\e\\", 11);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_OSC);
  CU_ASSERT_EQUAL(c->val.osc.cmdlen, 7);
  CU_ASSERT_STRING_EQUAL(c->val.osc.command, "1;Hello");

  free_cbs();

  // We need to string concat this because \x9d1 is \xd1
  vterm_push_bytes(vt, "\x9d" "1;Hello\x9c", 9);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_OSC);
  CU_ASSERT_EQUAL(c->val.osc.cmdlen, 7);
  CU_ASSERT_STRING_EQUAL(c->val.osc.command, "1;Hello");

  free_cbs();
}

#include "02parser.inc"
