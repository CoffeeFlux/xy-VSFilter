// Checks the frame copies and conversions in dsutil/blit.cpp.
//
// Every case runs on random frames of many sizes (odd ones included), with
// random padding and with top-down and bottom-up sources. Destinations start
// out filled with random bytes, so bytes a conversion must leave alone are
// checked too. The expected hashes were recorded when blit.cpp replaced
// VirtualDub's Kasumi blitter and this test compared the two directly: the
// output matched Kasumi's C code everywhere except where that was wrong
// (green in RGB565 -> RGB32, and the last Cr sample of I420 -> YUY2 rows
// for odd widths of 32k+1).

#include <afx.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "../../src/dsutil/blit.h"

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
    };

    struct I420
    {
        Plane y, u, v;

        I420(Rng& rng, int w, int h, int pitch, bool bottomup)
            : y(rng, h, pitch, bottomup)
            , u(rng, (h + 1) / 2, pitch / 2, bottomup)
            , v(rng, (h + 1) / 2, pitch / 2, bottomup) {}
    };

    int Pitch(Rng& rng, int rowbytes)
    {
        return (rowbytes + 1 + rng.Below(16)) & ~1;
    }

    // FNV-1a over every output buffer of a case, padding included.
    struct Hashes
    {
        std::map<std::string, uint64_t> hashes;

        void Add(const std::string& name, const Plane& out)
        {
            uint64_t& h = hashes[name];
            if (!h) h = 0xcbf29ce484222325ULL;
            for (size_t i = 0; i < out.mem.size(); i++) {
                h = (h ^ out.mem[i]) * 0x100000001b3ULL;
            }
        }
    };

    const int kSizes[][2] = {
        {1, 1}, {1, 2}, {2, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 7}, {6, 6}, {7, 5}, {8, 9},
        {9, 8}, {10, 3}, {16, 16}, {17, 3}, {31, 33}, {33, 5}, {64, 48}, {100, 1}, {1, 100}, {130, 66},
    };

    const struct { const char* name; uint64_t hash; } kExpected[] = {
        {"I420->I420", 0x647be0beb139413cULL},
        {"I420->RGB16", 0xe6c03bda735326f8ULL},
        {"I420->RGB24", 0x629ec55f1a7fea7bULL},
        {"I420->RGB32", 0x154713097f6ca94fULL},
        {"I420->YUY2", 0x2958eefc7a325a1bULL},
        {"I420->YUY2 interlaced", 0x86eb66a2b5a345e0ULL},
        {"NV12->NV12", 0xbe27ad91b8eb8e9dULL},
        {"P010->P010", 0x1c914bfb0f7142dfULL},
        {"P210->P210", 0x5ff9030e663bde6eULL},
        {"RGB16->RGB16", 0x39285b91afcbeae4ULL},
        {"RGB16->RGB24", 0xa510dd210bd34aebULL},
        {"RGB16->RGB32", 0x022af44ed87d1652ULL},
        {"RGB24->RGB16", 0xa6c724ce003b260fULL},
        {"RGB24->RGB24", 0xb1b81f2077cdaeaeULL},
        {"RGB24->RGB32", 0xfc0da646e4136fa6ULL},
        {"RGB32->RGB16", 0x2b37c22b2e92e94fULL},
        {"RGB32->RGB24", 0x81a9429789715ffaULL},
        {"RGB32->RGB32", 0x5d36501ee88857ebULL},
        {"YUY2->RGB16", 0x8fddde387af14f29ULL},
        {"YUY2->RGB24", 0xd5a0228e46c22033ULL},
        {"YUY2->RGB32", 0xf89e1fa7a2aa77d9ULL},
        {"YUY2->YUY2", 0x2c147763fd89bceeULL},
    };
}

TEST(Blit, MatchesRecordedOutput)
{
    Rng rng(12345);
    Hashes r;

    for (size_t si = 0; si < sizeof(kSizes) / sizeof(kSizes[0]); si++) {
        const int w = kSizes[si][0], h = kSizes[si][1];
        for (int bottomup = 0; bottomup < 2; bottomup++) {
            SCOPED_TRACE(testing::Message() << w << "x" << h << (bottomup ? " bottom-up" : " top-down"));

            // I420 sources
            const int ipitch = Pitch(rng, (w + 1) & ~1);
            I420 src(rng, w, h, ipitch, !!bottomup);

            {
                const int dpitch = Pitch(rng, (w + 1) & ~1);
                I420 a(rng, w, h, dpitch, false);
                EXPECT_TRUE(BitBltFromI420ToI420(w, h, a.y.row0, a.u.row0, a.v.row0, dpitch, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                r.Add("I420->I420", a.y);
                r.Add("I420->I420", a.u);
                r.Add("I420->I420", a.v);
            }
            {
                const int dpitch = Pitch(rng, (w + 1) / 2 * 4);
                Plane a(rng, h, dpitch, false);
                EXPECT_TRUE(BitBltFromI420ToYUY2(w, h, a.row0, dpitch, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                r.Add("I420->YUY2", a);
            }
            if (!(w & 1) && !(h & 1) && h >= 4) {
                const int dpitch = Pitch(rng, w * 2);
                Plane a(rng, h, dpitch, false);
                EXPECT_TRUE(BitBltFromI420ToYUY2Interlaced(w, h, a.row0, dpitch, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                r.Add("I420->YUY2 interlaced", a);
            }
            for (int dbpp = 16; dbpp <= 32; dbpp += 8) {
                const int dpitch = Pitch(rng, w * dbpp / 8);
                Plane a(rng, h, dpitch, false);
                EXPECT_TRUE(BitBltFromI420ToRGB(w, h, a.row0, dpitch, dbpp, src.y.row0, src.u.row0, src.v.row0, src.y.pitch));
                r.Add("I420->RGB" + std::to_string(dbpp), a);
            }

            // YUY2 sources
            const int ypitch = Pitch(rng, (w + 1) / 2 * 4);
            Plane yuy2(rng, h, ypitch, !!bottomup);
            {
                const int dpitch = Pitch(rng, (w + 1) / 2 * 4);
                Plane a(rng, h, dpitch, false);
                EXPECT_TRUE(BitBltFromYUY2ToYUY2(w, h, a.row0, dpitch, yuy2.row0, yuy2.pitch));
                r.Add("YUY2->YUY2", a);
            }
            for (int dbpp = 16; dbpp <= 32; dbpp += 8) {
                const int dpitch = Pitch(rng, w * dbpp / 8);
                Plane a(rng, h, dpitch, false);
                EXPECT_TRUE(BitBltFromYUY2ToRGB(w, h, a.row0, dpitch, dbpp, yuy2.row0, yuy2.pitch));
                r.Add("YUY2->RGB" + std::to_string(dbpp), a);
            }

            // RGB sources
            for (int sbpp = 16; sbpp <= 32; sbpp += 8) {
                const int spitch = Pitch(rng, w * sbpp / 8);
                Plane rgb(rng, h, spitch, !!bottomup);
                for (int dbpp = 16; dbpp <= 32; dbpp += 8) {
                    const int dpitch = Pitch(rng, w * dbpp / 8);
                    Plane a(rng, h, dpitch, false);
                    EXPECT_TRUE(BitBltFromRGBToRGB(w, h, a.row0, dpitch, dbpp, rgb.row0, rgb.pitch, sbpp));
                    r.Add("RGB" + std::to_string(sbpp) + "->RGB" + std::to_string(dbpp), a);
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
                    Plane a(rng, copies[ci].rows, dpitch, false);
                    EXPECT_TRUE(copies[ci].fn(w, h, a.row0, dpitch, s.row0, spitch));
                    r.Add(copies[ci].name, a);
                }
            }
        }
    }

    ASSERT_EQ(sizeof(kExpected) / sizeof(kExpected[0]), r.hashes.size());
    for (size_t i = 0; i < sizeof(kExpected) / sizeof(kExpected[0]); i++) {
        EXPECT_EQ(kExpected[i].hash, r.hashes[kExpected[i].name]) << kExpected[i].name;
    }
}
