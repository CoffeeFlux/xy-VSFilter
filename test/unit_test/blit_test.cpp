// Checks the frame copies and conversions in dsutil/blit.cpp against
// VirtualDub's Kasumi blitter, which they replace.
//
// Every case runs on random frames of many sizes (odd ones included), with
// random padding and with top-down and bottom-up sources. Destinations start
// out filled with random bytes so that bytes a conversion must not touch are
// compared too. The x64 Kasumi build uses its C reference code throughout,
// which blit.cpp must match exactly; the Win32 build swaps in some MMX
// routines, so there differences are only reported.

#include <afx.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "../../src/dsutil/blit.h"

#include <vd2/system/cpuaccel.h>
#include <vd2/system/memory.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>

namespace
{
    struct Rng
    {
        uint32_t s;
        explicit Rng(uint32_t seed) : s(seed) {}
        uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        int Below(int n) { return (int)(Next() % (uint32_t)n); }
    };

    // A plane with padding after each row. Bottom-up planes address row 0 at
    // the end of the buffer with a negative pitch, like CopyBuffer does for
    // flipped input.
    struct Plane
    {
        std::vector<BYTE> mem;
        int pitch;
        BYTE* row0;

        Plane(Rng& rng, int rows, int abspitch, bool bottomup)
            : mem((size_t)abspitch * rows + 64)
        {
            for (size_t i = 0; i < mem.size(); i++) {
                mem[i] = (BYTE)rng.Next();
            }
            pitch = bottomup ? -abspitch : abspitch;
            row0 = &mem[32] + (bottomup ? (ptrdiff_t)abspitch * (rows - 1) : 0);
        }

        Plane(const Plane& o)
            : mem(o.mem), pitch(o.pitch), row0(&mem[0] + (o.row0 - &o.mem[0])) {}
    };

    struct I420
    {
        Plane y, u, v;

        I420(Rng& rng, int w, int h, int pitch, bool bottomup)
            : y(rng, h, pitch, bottomup)
            , u(rng, (h + 1) / 2, pitch / 2, bottomup)
            , v(rng, (h + 1) / 2, pitch / 2, bottomup) {}

    };

    uint64_t Hash(uint64_t h, const std::vector<BYTE>& data)
    {
        for (size_t i = 0; i < data.size(); i++) {
            h = (h ^ data[i]) * 0x100000001b3ULL;
        }
        return h;
    }

    int Pitch(Rng& rng, int rowbytes)
    {
        return (rowbytes + 1 + rng.Below(16)) & ~1;
    }

    int RgbFormat(int bpp)
    {
        switch (bpp) {
        case 16: return nsVDPixmap::kPixFormat_RGB565;
        case 24: return nsVDPixmap::kPixFormat_RGB888;
        case 32: return nsVDPixmap::kPixFormat_XRGB8888;
        }
        return nsVDPixmap::kPixFormat_Null;
    }

    // Kasumi calls, set up as the old dsutil/vd.cpp did.

    VDPixmap KasumiI420(int w, int h, BYTE* y, BYTE* u, BYTE* v, int pitch)
    {
        VDPixmap pxm = {0};
        pxm.data = y;
        pxm.pitch = pitch;
        pxm.w = w;
        pxm.h = h;
        pxm.format = nsVDPixmap::kPixFormat_YUV420_Planar;
        pxm.data2 = u;
        pxm.pitch2 = pitch / 2;
        pxm.data3 = v;
        pxm.pitch3 = pitch / 2;
        return pxm;
    }

    VDPixmap KasumiChunky(int w, int h, BYTE* data, int pitch, int format)
    {
        VDPixmap pxm = {0};
        pxm.data = data;
        pxm.pitch = pitch;
        pxm.w = w;
        pxm.h = h;
        pxm.format = format;
        return pxm;
    }

    // RGB is bottom-up.
    VDPixmap KasumiRGB(int w, int h, BYTE* data, int pitch, int bpp)
    {
        return KasumiChunky(w, h, data + (ptrdiff_t)pitch * (h - 1), -pitch, RgbFormat(bpp));
    }

    struct Results
    {
        std::map<std::string, uint64_t> hashes;
        std::map<std::string, int> mismatches;
        std::string label;
        int cases;

        Results() : cases(0) {}

        // Records the output of one case; ref is Kasumi's output, or NULL if
        // there's nothing to compare with.
        void Add(const std::string& name, const Plane& out, const Plane* ref)
        {
            cases++;
            uint64_t& h = hashes[name];
            if (!h) h = 0xcbf29ce484222325ULL;
            h = Hash(h, out.mem);
            if (ref && out.mem != ref->mem) {
                if (mismatches[name]++ < 3) {
                    size_t i = 0;
                    while (out.mem[i] == ref->mem[i]) i++;
                    ptrdiff_t off = &out.mem[i] - out.row0;
                    printf("%s, %s: first difference in row %d, byte %d: %02x, Kasumi %02x\n", name.c_str(),
                        label.c_str(),
                        (int)(off / out.pitch), (int)(off % out.pitch), out.mem[i], ref->mem[i]);
                }
            }
        }
    };

    const int kSizes[][2] = {
        {1, 1}, {1, 2}, {2, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 7}, {6, 6}, {7, 5}, {8, 9},
        {9, 8}, {10, 3}, {16, 16}, {17, 3}, {31, 33}, {33, 5}, {64, 48}, {100, 1}, {1, 100}, {130, 66},
    };
}

TEST(Blit, MatchesKasumi)
{
    CPUEnableExtensions(CPUCheckForExtensions());
    VDFastMemcpyAutodetect();

    Rng rng(12345);
    Results r;

    for (size_t si = 0; si < sizeof(kSizes) / sizeof(kSizes[0]); si++) {
        const int w = kSizes[si][0], h = kSizes[si][1];
        for (int bottomup = 0; bottomup < 2; bottomup++) {
            SCOPED_TRACE(testing::Message() << w << "x" << h << (bottomup ? " bottom-up" : " top-down"));
            r.label = std::to_string(w) + "x" + std::to_string(h) + (bottomup ? " bottom-up" : " top-down");

            // I420 sources
            const int ipitch = Pitch(rng, (w + 1) & ~1);
            I420 src(rng, w, h, ipitch, !!bottomup);

            {
                const int dpitch = Pitch(rng, (w + 1) & ~1);
                I420 a(rng, w, h, dpitch, false), b = a;
                EXPECT_TRUE(BitBltFromI420ToI420(w, h, a.y.row0, a.u.row0, a.v.row0, dpitch, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                VDPixmapBlt(KasumiI420(w, h, b.y.row0, b.u.row0, b.v.row0, dpitch),
                            KasumiI420(w, h, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                r.Add("I420->I420", a.y, &b.y);
                r.Add("I420->I420", a.u, &b.u);
                r.Add("I420->I420", a.v, &b.v);
            }
            {
                const int dpitch = Pitch(rng, (w + 1) / 2 * 4);
                Plane a(rng, h, dpitch, false), b = a;
                EXPECT_TRUE(BitBltFromI420ToYUY2(w, h, a.row0, dpitch, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                VDPixmapBlt(KasumiChunky(w, h, b.row0, dpitch, nsVDPixmap::kPixFormat_YUV422_YUYV),
                            KasumiI420(w, h, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                if ((w & 1) && (w >> 1) % 16 == 0) {
                    // Kasumi sizes its chroma scratch rows for w/2 samples (rounded
                    // up to 16) but fills ceil(w/2), and for YUY2 output its Cr
                    // and Cb rows are adjacent, so here the last Cr sample of each
                    // row gets overwritten by Cb. Skip that byte.
                    for (int j = 0; j < h; j++) {
                        b.row0[j * dpitch + (w / 2) * 4 + 3] = a.row0[j * dpitch + (w / 2) * 4 + 3];
                    }
                }
                r.Add("I420->YUY2", a, &b);
            }
            if (!(w & 1) && !(h & 1) && h >= 4) {
                const int dpitch = Pitch(rng, w * 2);
                Plane a(rng, h, dpitch, false);
                EXPECT_TRUE(BitBltFromI420ToYUY2Interlaced(w, h, a.row0, dpitch, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                r.Add("I420->YUY2 interlaced", a, NULL);
            }
            for (int dbpp = 16; dbpp <= 32; dbpp += 8) {
                const int dpitch = Pitch(rng, w * dbpp / 8);
                Plane a(rng, h, dpitch, false), b = a;
                EXPECT_TRUE(BitBltFromI420ToRGB(w, h, a.row0, dpitch, dbpp, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                VDPixmapBlt(KasumiRGB(w, h, b.row0, dpitch, dbpp),
                            KasumiI420(w, h, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                r.Add("I420->RGB" + std::to_string(dbpp), a, &b);
            }

            // YUY2 sources
            const int ypitch = Pitch(rng, (w + 1) / 2 * 4);
            Plane yuy2(rng, h, ypitch, !!bottomup);
            {
                const int dpitch = Pitch(rng, (w + 1) / 2 * 4);
                Plane a(rng, h, dpitch, false), b = a;
                EXPECT_TRUE(BitBltFromYUY2ToYUY2(w, h, a.row0, dpitch, yuy2.row0, yuy2.pitch));
                VDPixmapBlt(KasumiChunky(w, h, b.row0, dpitch, nsVDPixmap::kPixFormat_YUV422_YUYV),
                            KasumiChunky(w, h, yuy2.row0, yuy2.pitch, nsVDPixmap::kPixFormat_YUV422_YUYV));
                r.Add("YUY2->YUY2", a, &b);
            }
            for (int dbpp = 16; dbpp <= 32; dbpp += 8) {
                const int dpitch = Pitch(rng, w * dbpp / 8);
                Plane a(rng, h, dpitch, false), b = a;
                EXPECT_TRUE(BitBltFromYUY2ToRGB(w, h, a.row0, dpitch, dbpp, yuy2.row0, yuy2.pitch));
                VDPixmapBlt(KasumiRGB(w, h, b.row0, dpitch, dbpp),
                            KasumiChunky(w, h, yuy2.row0, yuy2.pitch, nsVDPixmap::kPixFormat_YUV422_YUYV));
                r.Add("YUY2->RGB" + std::to_string(dbpp), a, &b);
            }

            // RGB sources
            for (int sbpp = 16; sbpp <= 32; sbpp += 8) {
                const int spitch = Pitch(rng, w * sbpp / 8);
                Plane rgb(rng, h, spitch, !!bottomup);
                for (int dbpp = 16; dbpp <= 32; dbpp += 8) {
                    const int dpitch = Pitch(rng, w * dbpp / 8);
                    Plane a(rng, h, dpitch, false), b = a;
                    EXPECT_TRUE(BitBltFromRGBToRGB(w, h, a.row0, dpitch, dbpp, rgb.row0, rgb.pitch, sbpp));
                    if (sbpp == 16 && dbpp == 32) {
                        // Kasumi's C RGB565 -> XRGB8888 fills the low bits of green
                        // from the wrong source bits (its MMX version doesn't), so
                        // go through its RGB565 -> RGB888 instead.
                        Rng tmprng(1);
                        Plane tmp(tmprng, h, w * 3, false);
                        VDPixmapBlt(KasumiRGB(w, h, tmp.row0, tmp.pitch, 24), KasumiRGB(w, h, rgb.row0, rgb.pitch, sbpp));
                        VDPixmapBlt(KasumiRGB(w, h, b.row0, dpitch, dbpp), KasumiRGB(w, h, tmp.row0, tmp.pitch, 24));
                    } else {
                        VDPixmapBlt(KasumiRGB(w, h, b.row0, dpitch, dbpp), KasumiRGB(w, h, rgb.row0, rgb.pitch, sbpp));
                    }
                    r.Add("RGB" + std::to_string(sbpp) + "->RGB" + std::to_string(dbpp), a, &b);
                }
            }

            // Semi-planar copies: luma rows followed by interleaved chroma rows
            if (!(h & 1)) {
                struct { const char* name; int bytes; int rows; bool (*fn)(int, int, BYTE*, int, const BYTE*, int); } copies[] = {
                    {"P010->P010", w * 2, h + h / 2, BitBltFromP010ToP010},
                    {"P210->P210", w * 2, h * 2, BitBltFromP210ToP210},
                    {"NV12->NV12", w, h + h / 2, BitBltFromNV12ToNV12},
                };
                for (size_t ci = 0; ci < sizeof(copies) / sizeof(copies[0]); ci++) {
                    const int spitch = Pitch(rng, copies[ci].bytes);
                    const int dpitch = Pitch(rng, copies[ci].bytes);
                    Plane s(rng, copies[ci].rows, spitch, false);
                    Plane a(rng, copies[ci].rows, dpitch, false), b = a;
                    EXPECT_TRUE(copies[ci].fn(w, h, a.row0, dpitch, s.row0, spitch));
                    VDMemcpyRect(b.row0, dpitch, s.row0, spitch, copies[ci].bytes, copies[ci].rows);
                    r.Add(copies[ci].name, a, &b);
                }
            }
        }
    }

    printf("%d cases\n", r.cases);
    for (std::map<std::string, uint64_t>::const_iterator it = r.hashes.begin(); it != r.hashes.end(); ++it) {
        int bad = r.mismatches.count(it->first) ? r.mismatches[it->first] : 0;
        printf("    {\"%s\", 0x%016llxULL}, // %d differ from Kasumi\n", it->first.c_str(), (unsigned long long)it->second, bad);
#ifdef _WIN64
        EXPECT_EQ(0, bad) << it->first << " differs from Kasumi";
#endif
    }
}
