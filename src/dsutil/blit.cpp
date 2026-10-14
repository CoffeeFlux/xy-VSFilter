// Frame copies and colorspace conversions for CBaseVideoFilter::CopyBuffer.
//
// The YCbCr -> RGB conversions (BT.601, limited range) and the chroma
// upsampling match VirtualDub's Kasumi reference blitters, which this file
// replaced, bit for bit. test/unit_test/blit_test.cpp checks the output.

#include "stdafx.h"
#include "blit.h"
#include <vector>

namespace
{
    void CopyRect(BYTE* dst, ptrdiff_t dstpitch, const BYTE* src, ptrdiff_t srcpitch, size_t rowbytes, int h)
    {
        for (; h > 0; h--, dst += dstpitch, src += srcpitch) {
            memcpy(dst, src, rowbytes);
        }
    }

    // Fixed-point BT.601 limited range coefficients; each term is rounded
    // separately and the sums are clipped.
    struct YCbCrTables
    {
        short y[256], r_cr[256], g_cr[256], g_cb[256], b_cb[256];

        YCbCrTables()
        {
            for (int i = 0; i < 256; i++) {
                y[i]    = (short)(((i - 16)  *  76309 + 32768) >> 16);
                r_cr[i] = (short)(((i - 128) * 104597 + 32768) >> 16);
                g_cr[i] = (short)(((i - 128) * -53279 + 32768) >> 16);
                g_cb[i] = (short)(((i - 128) * -25674 + 32768) >> 16);
                b_cb[i] = (short)(((i - 128) * 132201 + 32768) >> 16);
            }
        }
    };

    const YCbCrTables g_ycbcr;

    inline int Clip(int v)
    {
        return v < 0 ? 0 : v > 255 ? 255 : v;
    }

    // Chroma contribution to R, G and B.
    struct ChromaOffset
    {
        int r, g, b;

        ChromaOffset(BYTE cb, BYTE cr)
            : r(g_ycbcr.r_cr[cr]), g(g_ycbcr.g_cr[cr] + g_ycbcr.g_cb[cb]), b(g_ycbcr.b_cb[cb]) {}
        // Average of two neighbours.
        ChromaOffset(const ChromaOffset& lhs, const ChromaOffset& rhs)
            : r((lhs.r + rhs.r + 1) >> 1), g((lhs.g + rhs.g + 1) >> 1), b((lhs.b + rhs.b + 1) >> 1) {}
    };

    // Writes one pixel. 32-bit pixels get X = 0 unless keepx is set, in which
    // case the fourth byte is left alone.
    template<int bpp, bool keepx>
    inline void PutRGB(BYTE* dst, int r, int g, int b)
    {
        if (bpp == 16) {
            *(WORD*)dst = (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        } else if (bpp == 32 && !keepx) {
            *(DWORD*)dst = (DWORD)((r << 16) | (g << 8) | b);
        } else {
            dst[0] = (BYTE)b;
            dst[1] = (BYTE)g;
            dst[2] = (BYTE)r;
        }
    }

    template<int bpp, bool keepx>
    inline void PutYCbCr(BYTE* dst, BYTE y, const ChromaOffset& c)
    {
        int yy = g_ycbcr.y[y];
        PutRGB<bpp, keepx>(dst, Clip(yy + c.r), Clip(yy + c.g), Clip(yy + c.b));
    }

    inline int ClampRow(int row, int last)
    {
        return row < 0 ? 0 : row > last ? last : row;
    }

    // 4:2:0 chroma rows sit between luma rows: output row j blends chroma row
    // j/2 3:1 with its neighbour on the side of j. Like Kasumi, rows are
    // clamped to (h/2)-1, so an odd height never reads the last chroma row.
    template<typename F>
    void ForEachChromaRow(int w, int h, const BYTE* srcu, const BYTE* srcv, int srcpitch, F row)
    {
        const int cw = (w + 1) >> 1;
        const ptrdiff_t cpitch = srcpitch / 2;
        const int last = h >= 4 ? (h >> 1) - 1 : 0;
        std::vector<BYTE> buf(cw * 2);
        BYTE* cb = &buf[0];
        BYTE* cr = cb + cw;

        for (int j = 0; j < h; j++) {
            int near_row = ClampRow(j >> 1, last);
            int far_row = ClampRow((j & 1) ? (j >> 1) + 1 : (j >> 1) - 1, last);
            const BYTE* u0 = srcu + near_row * cpitch;
            const BYTE* u1 = srcu + far_row * cpitch;
            const BYTE* v0 = srcv + near_row * cpitch;
            const BYTE* v1 = srcv + far_row * cpitch;
            for (int i = 0; i < cw; i++) {
                cb[i] = (BYTE)((3 * u0[i] + u1[i] + 2) >> 2);
                cr[i] = (BYTE)((3 * v0[i] + v1[i] + 2) >> 2);
            }
            row(j, cb, cr);
        }
    }

    // Chroma is co-sited with even columns; odd columns average their two
    // neighbours, except the last column, which repeats its left neighbour.
    inline bool AverageColumn(int x, int w)
    {
        return (x & 1) && x + 1 < w;
    }

    template<int bpp>
    void I420ToRGB(int w, int h, BYTE* dst, int dstpitch, const BYTE* srcy, const BYTE* srcu, const BYTE* srcv, int srcpitch)
    {
        // RGB is bottom-up
        dst += (ptrdiff_t)dstpitch * (h - 1);

        ForEachChromaRow(w, h, srcu, srcv, srcpitch, [&](int j, const BYTE* cb, const BYTE* cr) {
            const BYTE* y = srcy + (ptrdiff_t)srcpitch * j;
            BYTE* d = dst - (ptrdiff_t)dstpitch * j;
            for (int x = 0; x < w; x++, d += bpp / 8) {
                int k = x >> 1;
                BYTE u = AverageColumn(x, w) ? (BYTE)((cb[k] + cb[k + 1] + 1) >> 1) : cb[k];
                BYTE v = AverageColumn(x, w) ? (BYTE)((cr[k] + cr[k + 1] + 1) >> 1) : cr[k];
                PutYCbCr<bpp, false>(d, y[x], ChromaOffset(u, v));
            }
        });
    }

    template<int bpp>
    void YUY2ToRGB(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch)
    {
        // RGB is bottom-up
        dst += (ptrdiff_t)dstpitch * (h - 1);

        for (int j = 0; j < h; j++, src += srcpitch, dst -= dstpitch) {
            BYTE* d = dst;
            for (int x = 0; x < w; x++, d += bpp / 8) {
                const BYTE* p = src + (x >> 1) * 4;
                ChromaOffset c(p[1], p[3]);
                if (AverageColumn(x, w)) {
                    c = ChromaOffset(c, ChromaOffset(p[5], p[7]));
                }
                PutYCbCr<bpp, true>(d, src[x * 2], c);
            }
        }
    }

    void GetRGB(const BYTE* p, int bpp, int& r, int& g, int& b)
    {
        if (bpp == 16) {
            WORD px = *(const WORD*)p;
            r = px >> 11;
            g = (px >> 5) & 0x3f;
            b = px & 0x1f;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
        } else {
            b = p[0];
            g = p[1];
            r = p[2];
        }
    }

    template<int dbpp>
    void ConvertRGB(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch, int sbpp)
    {
        for (int j = 0; j < h; j++, src += srcpitch, dst += dstpitch) {
            const BYTE* s = src;
            BYTE* d = dst;
            for (int x = 0; x < w; x++, s += sbpp / 8, d += dbpp / 8) {
                int r, g, b;
                GetRGB(s, sbpp, r, g, b);
                PutRGB<dbpp, false>(d, r, g, b);
            }
        }
    }
}

bool BitBltFromI420ToI420(int w, int h, BYTE* dsty, BYTE* dstu, BYTE* dstv, int dstpitch, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch)
{
    if (w <= 0 || h <= 0) {
        return true;
    }

    const int cw = (w + 1) >> 1, ch = (h + 1) >> 1;
    CopyRect(dstv, dstpitch / 2, srcv, srcpitch / 2, cw, ch);
    CopyRect(dstu, dstpitch / 2, srcu, srcpitch / 2, cw, ch);
    CopyRect(dsty, dstpitch, srcy, srcpitch, w, h);
    return true;
}

bool BitBltFromYUY2ToYUY2(int w, int h, BYTE* dst, int dstpitch, BYTE* src, int srcpitch)
{
    if (w <= 0 || h <= 0) {
        return true;
    }

    CopyRect(dst, dstpitch, src, srcpitch, ((w + 1) >> 1) * 4, h);
    return true;
}

bool BitBltFromI420ToRGB(int w, int h, BYTE* dst, int dstpitch, int dbpp, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch)
{
    if (dbpp != 16 && dbpp != 24 && dbpp != 32) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return true;
    }

    switch (dbpp) {
    case 16: I420ToRGB<16>(w, h, dst, dstpitch, srcy, srcu, srcv, srcpitch); break;
    case 24: I420ToRGB<24>(w, h, dst, dstpitch, srcy, srcu, srcv, srcpitch); break;
    case 32: I420ToRGB<32>(w, h, dst, dstpitch, srcy, srcu, srcv, srcpitch); break;
    }
    return true;
}

bool BitBltFromI420ToYUY2(int w, int h, BYTE* dst, int dstpitch, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch)
{
    if (srcpitch == 0) srcpitch = w;

    if (w <= 0 || h <= 0) {
        return true;
    }

    ForEachChromaRow(w, h, srcu, srcv, srcpitch, [&](int j, const BYTE* cb, const BYTE* cr) {
        const BYTE* y = srcy + (ptrdiff_t)srcpitch * j;
        BYTE* d = dst + (ptrdiff_t)dstpitch * j;
        for (int x = 0; x < w; x += 2, d += 4) {
            int k = x >> 1;
            d[0] = y[x];
            d[1] = cb[k];
            d[2] = x + 1 < w ? y[x + 1] : y[x];
            d[3] = cr[k];
        }
    });
    return true;
}

static void yuvtoyuy2row_c(BYTE* dst, BYTE* srcy, BYTE* srcu, BYTE* srcv, DWORD width)
{
    WORD* dstw = (WORD*)dst;
    for(; width > 1; width -= 2)
    {
        *dstw++ = (*srcu++<<8)|*srcy++;
        *dstw++ = (*srcv++<<8)|*srcy++;
    }
}

static void yuvtoyuy2row_avg_c(BYTE* dst, BYTE* srcy, BYTE* srcu, BYTE* srcv, DWORD width, int pitchuv)
{
    WORD* dstw = (WORD*)dst;
    for(; width > 1; width -= 2, srcu++, srcv++)
    {
        *dstw++ = (((srcu[0]+srcu[pitchuv])>>1)<<8)|*srcy++;
        *dstw++ = (((srcv[0]+srcv[pitchuv])>>1)<<8)|*srcy++;
    }
}

bool BitBltFromI420ToYUY2Interlaced(int w, int h, BYTE* dst, int dstpitch, BYTE* srcy, BYTE* srcu, BYTE* srcv, int srcpitch)
{
    if(w<=0 || h<=0 || (w&1) || (h&1))
        return false;

    if(srcpitch == 0) srcpitch = w;

    int halfsrcpitch = srcpitch/2;
    do
    {
        yuvtoyuy2row_c(dst, srcy, srcu, srcv, w);
        yuvtoyuy2row_avg_c(dst + dstpitch, srcy + srcpitch, srcu, srcv, w, halfsrcpitch);

        dst += 2*dstpitch;
        srcy += 2*srcpitch;
        srcu += halfsrcpitch;
        srcv += halfsrcpitch;
    }
    while((h -= 2) > 2);

    yuvtoyuy2row_c(dst, srcy, srcu, srcv, w);
    yuvtoyuy2row_c(dst + dstpitch, srcy + srcpitch, srcu, srcv, w);

    return true;
}

bool BitBltFromYUY2ToRGB(int w, int h, BYTE* dst, int dstpitch, int dbpp, BYTE* src, int srcpitch)
{
    if (srcpitch == 0) srcpitch = w;

    if (dbpp != 16 && dbpp != 24 && dbpp != 32) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return true;
    }

    switch (dbpp) {
    case 16: YUY2ToRGB<16>(w, h, dst, dstpitch, src, srcpitch); break;
    case 24: YUY2ToRGB<24>(w, h, dst, dstpitch, src, srcpitch); break;
    case 32: YUY2ToRGB<32>(w, h, dst, dstpitch, src, srcpitch); break;
    }
    return true;
}

bool BitBltFromRGBToRGB(int w, int h, BYTE* dst, int dstpitch, int dbpp, BYTE* src, int srcpitch, int sbpp)
{
    if ((dbpp != 16 && dbpp != 24 && dbpp != 32) || (sbpp != 16 && sbpp != 24 && sbpp != 32)) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return true;
    }

    if (sbpp == dbpp) {
        CopyRect(dst, dstpitch, src, srcpitch, w * dbpp / 8, h);
        return true;
    }

    switch (dbpp) {
    case 16: ConvertRGB<16>(w, h, dst, dstpitch, src, srcpitch, sbpp); break;
    case 24: ConvertRGB<24>(w, h, dst, dstpitch, src, srcpitch, sbpp); break;
    case 32: ConvertRGB<32>(w, h, dst, dstpitch, src, srcpitch, sbpp); break;
    }
    return true;
}

bool BitBltFromP010ToP010(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch)
{
    if (w <= 0 || h <= 0) {
        return true;
    }
    ASSERT(dst && src && w*2<=srcpitch && w*2<=dstpitch && ((h&1)==0));
    CopyRect(dst, dstpitch, src, srcpitch, w*2, h+h/2);

    return true;
}

bool BitBltFromP210ToP210(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch)
{
    if (w <= 0 || h <= 0) {
        return true;
    }
    ASSERT(dst && src && w * 2 <= srcpitch && w * 2 <= dstpitch);
    CopyRect(dst, dstpitch, src, srcpitch, w * 2, h + h);

    return true;
}

bool BitBltFromNV12ToNV12(int w, int h, BYTE* dst, int dstpitch, const BYTE* src, int srcpitch)
{
    if (w <= 0 || h <= 0) {
        return true;
    }
    ASSERT(dst && src && w<=srcpitch && w<=dstpitch && ((h&1)==0));
    CopyRect(dst, dstpitch, src, srcpitch, w, h+h/2);

    return true;
}
