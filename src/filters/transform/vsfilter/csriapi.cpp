/* 
 *	Copyright (C) 2007 Niels Martin Hansen
 *	http://aegisub.net/
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include "..\..\..\subtitles\RTS.h"

#define CSRIAPI extern "C" __declspec(dllexport)
#define CSRI_OWN_HANDLES
typedef const char *csri_rend;
extern "C" struct csri_vsfilter_inst {
	CRenderedTextSubtitle *rts;
	CSize script_res;
	CSize screen_res;
	CRect video_rect;
	enum csri_pixfmt pixfmt;
	size_t readorder;
};
typedef struct csri_vsfilter_inst csri_inst;
#include "csri.h"
static csri_rend csri_vsfilter = "vsfilter";


// Scripts are only opened from memory (csri_open_mem). This export stays
// because CSRI loaders skip a renderer DLL that doesn't have it.
CSRIAPI csri_inst *csri_open_file(csri_rend *renderer, const char *filename, struct csri_openflag *flags)
{
	return 0;
}


CSRIAPI csri_inst *csri_open_mem(csri_rend *renderer, const void *data, size_t length, struct csri_openflag *flags)
{
	csri_inst *inst = new csri_inst();

	inst->rts = new CRenderedTextSubtitle();
	if (inst->rts->Open((BYTE*)data, (int)length, DEFAULT_CHARSET, _T("CSRI memory subtitles"))) {
		inst->readorder = 0;
		return inst;
	} else {
		delete inst->rts;
		delete inst;
		return 0;
	}
}


CSRIAPI void csri_close(csri_inst *inst)
{
	if (!inst) return;

	delete inst->rts;
	delete inst;
}


CSRIAPI int csri_request_fmt(csri_inst *inst, const struct csri_fmt *fmt)
{
	if (!inst || !fmt->width || !fmt->height) {
		return -1;
	}

	// The renderer only produces 32-bit BGRA
	if (fmt->pixfmt != CSRI_F_BGR_) {
		return -1;
	}
	inst->pixfmt = fmt->pixfmt;
	inst->screen_res = CSize(fmt->width, fmt->height);
	inst->video_rect = CRect(0, 0, fmt->width, fmt->height);
	return 0;
}


CSRIAPI void csri_render(csri_inst *inst, struct csri_frame *frame, double time)
{
	const double arbitrary_framerate = 25.0;
	if (inst->pixfmt != CSRI_F_BGR_) {
		return; // no supported format was requested
	}
	SubPicDesc spd;
	spd.w = inst->screen_res.cx;
	spd.h = inst->screen_res.cy;
	spd.type = MSP_RGBA;
	spd.bpp = 32;
	spd.bits = frame->planes[0];
	spd.pitch = frame->strides[0];
	spd.vidrect = inst->video_rect;

	inst->rts->Render(spd, (REFERENCE_TIME)(time*10000000), arbitrary_framerate, inst->video_rect);
}


// No extensions supported
CSRIAPI void *csri_query_ext(csri_rend *rend, csri_ext_id extname) { return 0; }

// Get info for renderer
static struct csri_info csri_vsfilter_info = {
#ifdef _DEBUG
	"pf-xy-vsfilter_textsub_debug", // name
#else
	"pf-xy-vsfilter_textsub", // name
#endif
	"3.2", // version
	"xy-VSFilter/TextSub", // longname
	"Gabest", // author
	"Copyright (c) 2003-2014 by Gabest et al." // copyright
};

CSRIAPI struct csri_info *csri_renderer_info(csri_rend *rend) { return &csri_vsfilter_info; }
CSRIAPI csri_rend *csri_renderer_byname(const char *name, const char *specific) { return &csri_vsfilter; }
CSRIAPI csri_rend *csri_renderer_default() { return &csri_vsfilter; }
CSRIAPI csri_rend *csri_renderer_next(csri_rend *prev) { return 0; }
