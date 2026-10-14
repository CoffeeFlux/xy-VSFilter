/* SPDX-License-Identifier: GPL-2.0-or-later
 * A test double for the comparison tool. NEVER a subtitle renderer or oracle.
 */
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define CSRIAPI __declspec(dllexport)
#endif
#include "csri.h"

static int renderer;
static struct csri_info info = {"fake", "1", "Harness test double", "test", "test"};
struct instance { unsigned calls; unsigned char input_byte, dialog_response; };

csri_rend *csri_renderer_default(void) { return &renderer; }
struct csri_info *csri_renderer_info(csri_rend *r) { (void)r; return &info; }
csri_rend *csri_renderer_next(csri_rend *r) { (void)r; return NULL; }
csri_rend *csri_renderer_byname(const char *a, const char *b) { (void)a; (void)b; return &renderer; }
void *csri_query_ext(csri_rend *r, csri_ext_id name) { (void)r; (void)name; return NULL; }
csri_inst *csri_open_file(csri_rend *r, const char *path, struct csri_openflag *flags)
{
    int dialog_response = 0;
    (void)r; (void)path; (void)flags;
#ifdef DIALOG
    dialog_response = MessageBoxW(NULL, L"Harness warning", L"Warning", MB_OKCANCEL);
#endif
#ifdef DIALOG_DIFFERENT
    dialog_response = MessageBoxW(NULL, L"Harness changed warning", L"Warning", MB_OKCANCEL);
#endif
#ifdef REJECT
    return NULL;
#else
    struct instance *state = (struct instance *)calloc(1, sizeof(struct instance));
    if (state) state->dialog_response = (unsigned char)dialog_response;
    return (csri_inst *)state;
#endif
}
csri_inst *csri_open_mem(csri_rend *r, const void *data, size_t size, struct csri_openflag *flags)
{
    struct instance *state = (struct instance *)csri_open_file(r, "", flags);
    if (state && size) state->input_byte = ((const unsigned char *)data)[0];
    return (csri_inst *)state;
}
void csri_close(csri_inst *i) { free(i); }
int csri_request_fmt(csri_inst *i, const struct csri_fmt *fmt)
{ (void)i; return fmt->pixfmt == CSRI_F_BGR_ ? 0 : -1; }
void csri_render(csri_inst *i, struct csri_frame *frame, double time)
{
    struct instance *state = (struct instance *)i;
#ifdef CRASH
    abort();
#endif
#ifdef HANG
    for (;;) { }
#endif
#ifdef NATIVE_EXCEPTION
    RaiseException(0xe06d7363, 0, 0, NULL);
#endif
    frame->planes[0][0] = (unsigned char)(++state->calls + (int)(time * 10));
#if defined(DIALOG) || defined(DIALOG_DIFFERENT)
    frame->planes[0][1] = state->dialog_response;
#endif
#ifdef INPUT_BYTE
    frame->planes[0][1] = state->input_byte;
#endif
#ifdef DIFFERENT
    frame->planes[0][1] ^= 1;
#endif
#ifdef CORRUPT_GUARD
    frame->planes[0][-1] = 0;
#endif
}
