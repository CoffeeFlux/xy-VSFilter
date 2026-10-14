/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2015 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "VobSubImage.h"
#include "RTS.h"
#include <math.h>
#include <algorithm>

CVobSubImage::CVobSubImage()
    : org(CSize(0, 0))
    , lpTemp1(nullptr)
    , lpTemp2(nullptr)
    , nPlane(0)
    , fCustomPal(false)
    , fAligned(true)
    , tridx(0)
    , orgpal(nullptr)
    , cuspal(nullptr)
    , iLang(-1)
    , iIdx(-1)
    , fForced(false)
    , fAnimated(false)
    , tCurrent(-1)
    , start(0)
    , delay(0)
    , rect(CRect(0, 0, 0, 0))
    , lpPixels(nullptr)
{
    ZeroMemory(&pal, sizeof(pal));
}

CVobSubImage::~CVobSubImage()
{
    Free();
}

bool CVobSubImage::Alloc(int w, int h)
{
    // if there is nothing to crop TrimSubImage might even add a 1 pixel
    // wide border around the text, that's why we need a bit more memory
    // to be allocated.

    if (lpTemp1 == nullptr || w * h > org.cx * org.cy || (w + 2) * (h + 2) > (org.cx + 2) * (org.cy + 2)) {
        Free();

        try {
            lpTemp1 = DEBUG_NEW RGBQUAD[w * h];
        } catch (CMemoryException* e) {
            ASSERT(FALSE);
            e->Delete();
            return false;
        }

        try {
            lpTemp2 = DEBUG_NEW RGBQUAD[(w + 2) * (h + 2)];
        } catch (CMemoryException* e) {
            ASSERT(FALSE);
            e->Delete();
            delete [] lpTemp1;
            lpTemp1 = nullptr;
            return false;
        }

        org.cx = w;
        org.cy = h;
    }

    lpPixels = lpTemp1;

    return true;
}

void CVobSubImage::Free()
{
    SAFE_DELETE_ARRAY(lpTemp1);
    SAFE_DELETE_ARRAY(lpTemp2);

    lpPixels = nullptr;
}

bool CVobSubImage::Decode(BYTE* lpData, size_t packetSize, size_t dataSize, int t,
                          bool fCustomPal,
                          int tridx,
                          RGBQUAD* orgpal /*[16]*/, RGBQUAD* cuspal /*[4]*/,
                          bool fTrim)
{
    GetPacketInfo(lpData, packetSize, dataSize, t);

    if (!Alloc(rect.Width(), rect.Height())) {
        return false;
    }

    lpPixels = lpTemp1;

    nPlane = 0;
    fAligned = true;

    this->fCustomPal = fCustomPal;
    this->orgpal = orgpal;
    this->tridx = tridx;
    this->cuspal = cuspal;

    CPoint p = rect.TopLeft();

    size_t end[] = { nOffset[1], dataSize };

    while (nOffset[nPlane] < end[nPlane]) {
        DWORD code;

        if ((code = GetNibble(lpData)) >= 0x4
                || (code = (code << 4) | GetNibble(lpData)) >= 0x10
                || (code = (code << 4) | GetNibble(lpData)) >= 0x40
                || (code = (code << 4) | GetNibble(lpData)) >= 0x100) {
            DrawPixels(p, code >> 2, code & 3);
            if ((p.x += code >> 2) < rect.right) {
                continue;
            }
        }

        DrawPixels(p, rect.right - p.x, code & 3);

        if (!fAligned) {
            GetNibble(lpData);    // align to byte
        }

        p.x = rect.left;
        p.y++;
        nPlane = 1 - nPlane;
    }

    rect.bottom = min(p.y, rect.bottom);

    if (fTrim) {
        TrimSubImage();
    }

    return true;
}

void CVobSubImage::GetPacketInfo(const BYTE* lpData, size_t packetSize, size_t dataSize, int t /*= INT_MAX*/)
{
    //  delay = 0;

    size_t i, nextctrlblk = dataSize;
    WORD pal = 0, tr = 0;
    WORD nPal = 0, nTr = 0;

    do {
        i = nextctrlblk;

        tCurrent = 1024 * ((lpData[i] << 8) | lpData[i + 1]) / 90;
        i += 2;
        nextctrlblk = (lpData[i] << 8) | lpData[i + 1];
        i += 2;

        if (nextctrlblk > packetSize || nextctrlblk < dataSize) {
            ASSERT(0);
            return;
        }

        if (tCurrent > t) {
            break;
        }

        bool bBreak = false;

        while (!bBreak) {
            size_t len = 0;

            switch (lpData[i]) {
                case 0x00:
                    len = 0;
                    break;
                case 0x01:
                    len = 0;
                    break;
                case 0x02:
                    len = 0;
                    break;
                case 0x03:
                    len = 2;
                    break;
                case 0x04:
                    len = 2;
                    break;
                case 0x05:
                    len = 6;
                    break;
                case 0x06:
                    len = 4;
                    break;
                default:
                    len = 0;
                    break;
            }

            if (i + len >= packetSize) {
                TRACE(_T("Warning: Wrong subpicture parameter block ending\n"));
                break;
            }

            switch (lpData[i++]) {
                case 0x00: // forced start displaying
                    fForced = true;
                    break;
                case 0x01: // start displaying
                    fForced = false;
                    break;
                case 0x02: // stop displaying
                    delay = tCurrent;
                    break;
                case 0x03:
                    pal = (lpData[i] << 8) | lpData[i + 1];
                    i += 2;
                    nPal++;
                    break;
                case 0x04:
                    tr = (lpData[i] << 8) | lpData[i + 1];
                    i += 2;
                    nTr++;
                    //tr &= 0x00f0;
                    break;
                case 0x05:
                    rect = CRect((lpData[i] << 4) + (lpData[i + 1] >> 4),
                                 (lpData[i + 3] << 4) + (lpData[i + 4] >> 4),
                                 ((lpData[i + 1] & 0x0f) << 8) + lpData[i + 2] + 1,
                                 ((lpData[i + 4] & 0x0f) << 8) + lpData[i + 5] + 1);
                    i += 6;
                    break;
                case 0x06:
                    nOffset[0] = (lpData[i] << 8) + lpData[i + 1];
                    i += 2;
                    nOffset[1] = (lpData[i] << 8) + lpData[i + 1];
                    i += 2;
                    break;
                case 0xff: // end of ctrlblk
                    bBreak = true;
                    continue;
                default: // skip this ctrlblk
                    bBreak = true;
                    break;
            }
        }
    } while (i <= nextctrlblk && i < packetSize);

    for (i = 0; i < 4; i++) {
        this->pal[i].pal = (pal >> (i << 2)) & 0xf;
        this->pal[i].tr = (tr >> (i << 2)) & 0xf;
    }

    fAnimated = (nPal > 1 || nTr > 1);
}

BYTE CVobSubImage::GetNibble(const BYTE* lpData)
{
    size_t& off = nOffset[nPlane];
    BYTE ret = lpData[off];
    if (fAligned) {
        ret >>= 4;
    }
    ret &= 0x0f;
    fAligned = !fAligned;
    if (fAligned) {
        off++;
    }
    return ret;
}

void CVobSubImage::DrawPixels(CPoint p, int length, size_t colorId)
{
    if (length <= 0
            || p.x + length < rect.left
            || p.x >= rect.right
            || p.y < rect.top
            || p.y >= rect.bottom) {
        return;
    }

    if (p.x < rect.left) {
        p.x = rect.left;
    }
    if (p.x + length >= rect.right) {
        length = rect.right - p.x;
    }

    RGBQUAD* ptr = &lpPixels[rect.Width() * (p.y - rect.top) + (p.x - rect.left)];

    RGBQUAD c;

    if (!fCustomPal) {
        c = orgpal[pal[colorId].pal];
        c.rgbReserved = (pal[colorId].tr << 4) | pal[colorId].tr;
    } else {
        c = cuspal[colorId];
    }

    while (length-- > 0) {
        *ptr++ = c;
    }
}

void CVobSubImage::TrimSubImage()
{
    CRect r;
    r.left = rect.Width();
    r.top = rect.Height();
    r.right = 0;
    r.bottom = 0;

    RGBQUAD* ptr = lpTemp1;

    for (int j = 0, y = rect.Height(); j < y; j++) {
        for (int i = 0, x = rect.Width(); i < x; i++, ptr++) {
            if (ptr->rgbReserved) {
                if (r.top > j) {
                    r.top = j;
                }
                if (r.bottom < j) {
                    r.bottom = j;
                }
                if (r.left > i) {
                    r.left = i;
                }
                if (r.right < i) {
                    r.right = i;
                }
            }
        }
    }

    if (r.left > r.right || r.top > r.bottom) {
        return;
    }

    r += CRect(0, 0, 1, 1);

    r &= CRect(CPoint(0, 0), rect.Size());

    int w = r.Width(), h = r.Height();

    DWORD offset = r.top * rect.Width() + r.left;

    r += CRect(1, 1, 1, 1);

    DWORD* src = (DWORD*)&lpTemp1[offset];
    DWORD* dst = (DWORD*)&lpTemp2[1 + w + 1];

    ZeroMemory(lpTemp2, (1 + w + 1)*sizeof(RGBQUAD));

    for (int height = h; height; height--, src += rect.Width()) {
        *dst++ = 0;
        memcpy(dst, src, w * sizeof(RGBQUAD));
        dst += w;
        *dst++ = 0;
    }

    ZeroMemory(dst, (1 + w + 1)*sizeof(RGBQUAD));

    lpPixels = lpTemp2;

    rect = r + rect.TopLeft();
}
