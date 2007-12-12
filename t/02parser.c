#include "CUnit/CUnit.h"

#include "ecma48.h"

#include <glib.h>

static ecma48_t *e48;

static GSList *cbs;

typedef struct {
  enum { CB_TEXT, CB_CONTROL, CB_ESCAPE, CB_CSI_RAW, CB_CSI } type;
  union {
    struct { int *codepoints; int npoints; } text;
    unsigned char control;
    char escape;
    struct { char *args; size_t arglen; char command; } csi_raw;
    struct { char *intermed; int *args; int argcount; char command; } csi;
  } val;
} cb;

static int cb_text(ecma48_t *_e48, int codepoints[], int npoints)
{
  CU_ASSERT_PTR_EQUAL(e48, _e48);

  cb *c = g_new0(cb, 1);
  c->type = CB_TEXT;
  c->val.text.npoints = npoints;
  c->val.text.codepoints = g_new0(int, npoints);
  memcpy(c->val.text.codepoints, codepoints, npoints * sizeof(codepoints[0]));

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int cb_control(ecma48_t *_e48, char control)
{
  CU_ASSERT_PTR_EQUAL(e48, _e48);

  cb *c = g_new0(cb, 1);
  c->type = CB_CONTROL;
  c->val.control = control;

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int cb_escape(ecma48_t *_e48, char escape)
{
  CU_ASSERT_PTR_EQUAL(e48, _e48);

  cb *c = g_new0(cb, 1);
  c->type = CB_ESCAPE;
  c->val.escape = escape;

  cbs = g_slist_append(cbs, c);

  return 1;
}

static int capture_csi_raw = 0;

static int cb_csi_raw(ecma48_t *_e48, char *args, size_t arglen, char command)
{
  CU_ASSERT_PTR_EQUAL(e48, _e48);

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

static int cb_csi(ecma48_t *_e48, char *intermed, int *args, int argcount, char command)
{
  CU_ASSERT_PTR_EQUAL(e48, _e48);

  cb *c = g_new0(cb, 1);
  c->type = CB_CSI;
  c->val.csi.intermed = g_strdup(intermed);
  c->val.csi.args = g_new0(int, argcount);
  memcpy(c->val.csi.args, args, argcount * sizeof(args[0]));
  c->val.csi.command = command;

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
    }

    g_free(c);
  }

  g_slist_free(cbs);
  cbs = NULL;
}

static ecma48_parser_callbacks_t parser_cbs = {
  .text    = cb_text,
  .control = cb_control,
  .escape  = cb_escape,
  .csi_raw = cb_csi_raw,
  .csi     = cb_csi,
};

int parser_init(void)
{
  e48 = ecma48_new(80, 25);
  if(!e48)
    return 1;

  ecma48_parser_set_utf8(e48, 0);
  ecma48_set_parser_callbacks(e48, &parser_cbs);

  return 0;
}

static void test_basictext(void)
{
  ecma48_push_bytes(e48, "hello", 5);

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
  ecma48_push_bytes(e48, "\x03", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 3);

  free_cbs();

  ecma48_push_bytes(e48, "\x1f", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x1f);

  free_cbs();
}

static void test_c1_8bit(void)
{
  ecma48_push_bytes(e48, "\x83", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x83);

  free_cbs();

  ecma48_push_bytes(e48, "\x9f", 1);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x9f);

  free_cbs();
}

static void test_c1_7bit(void)
{
  ecma48_push_bytes(e48, "\e\x43", 2);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  cb *c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x83);

  free_cbs();

  ecma48_push_bytes(e48, "\e\x5f", 2);

  CU_ASSERT_EQUAL(g_slist_length(cbs), 1);

  c = cbs->data;
  CU_ASSERT_EQUAL(c->type, CB_CONTROL);
  CU_ASSERT_EQUAL(c->val.control, 0x9f);

  free_cbs();
}

static void test_highbytes(void)
{
  ecma48_push_bytes(e48, "\xa0\xcc\xfe", 3);

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
  ecma48_push_bytes(e48, "1\n2", 3);

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

#include "02parser.inc"
