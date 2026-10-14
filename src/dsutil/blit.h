#pragma once

// Copies a video frame into the output buffer, converting between the formats
// CBaseVideoFilter can negotiate. RGB destinations are bottom-up DIBs.
// Returns false if the conversion isn't supported.

extern bool BitBltFromI420ToI420(int w, int h, BYTE* dsty, BYTE* dstu, BYTE* dstv, int dstpitch, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch);
extern bool BitBltFromI420ToYUY2(int w, int h, BYTE* dst, int dstpitch, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch);
extern bool BitBltFromI420ToYUY2Interlaced(int w, int h, BYTE* dst, int dstpitch, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch);
extern bool BitBltFromI420ToRGB(int w, int h, BYTE* dst, int dstpitch, int dbpp, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch);
extern bool BitBltFromYUY2ToYUY2(int w, int h, BYTE* dst, int dstpitch, BYTE* src, int srcpitch);
extern bool BitBltFromYUY2ToRGB(int w, int h, BYTE* dst, int dstpitch, int dbpp, BYTE* src, int srcpitch);
extern bool BitBltFromRGBToRGB(int w, int h, BYTE* dst, int dstpitch, int dbpp, BYTE* src, int srcpitch, int sbpp);

extern bool BitBltFromP010ToP010(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch);
extern bool BitBltFromP210ToP210(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch);
extern bool BitBltFromNV12ToNV12(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch);
