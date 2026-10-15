/*
 *  (C) 2003-2006 Gabest
 *  (C) 2006-2013 see Authors.txt 
 *  http://www.gabest.org
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
#include "STS.h"
#include <atlbase.h>

#include <algorithm>
#include <vector>

// gathered from http://www.netwave.or.jp/~shikai/shikai/shcolor.htm

static struct htmlcolor {TCHAR* name; DWORD color;} htmlcolors[] =
{
    {_T("white"), 0xffffff},
    {_T("whitesmoke"), 0xf5f5f5},
    {_T("ghostwhite"), 0xf8f8ff},
    {_T("snow"), 0xfffafa},
    {_T("gainsboro"), 0xdcdcdc},
    {_T("lightgrey"), 0xd3d3d3},
    {_T("silver"), 0xc0c0c0},
    {_T("darkgray"), 0xa9a9a9},
    {_T("gray"), 0x808080},
    {_T("dimgray"), 0x696969},
    {_T("lightslategray"), 0x778899},
    {_T("slategray"), 0x708090},
    {_T("darkslategray"), 0x2f4f4f},
    {_T("black"), 0x000000},

    {_T("azure"), 0xf0ffff},
    {_T("aliceblue"), 0xf0f8ff},
    {_T("mintcream"), 0xf5fffa},
    {_T("honeydew"), 0xf0fff0},
    {_T("lightcyan"), 0xe0ffff},
    {_T("paleturqoise"), 0xafeeee},
    {_T("powderblue"), 0xb0e0e6},
    {_T("lightblue"), 0xadd8ed},
    {_T("lightsteelblue"), 0xb0c4de},
    {_T("skyblue"), 0x87ceeb},
    {_T("lightskyblue"), 0x87cefa},
    {_T("cyan"), 0x00ffff},
    {_T("aqua"), 0x00ff80},
    {_T("deepskyblue"), 0x00bfff},
    {_T("aquamarine"), 0x7fffd4},
    {_T("turquoise"), 0x40e0d0},
    {_T("darkturquoise"), 0x00ced1},
    {_T("lightseagreen"), 0x20b2aa},
    {_T("mediumturquoise"), 0x40e0dd},
    {_T("mediumaquamarine"), 0x66cdaa},
    {_T("cadetblue"), 0x5f9ea0},
    {_T("teal"), 0x008080},
    {_T("darkcyan"), 0x008b8b},
    {_T("comflowerblue"), 0x6495ed},
    {_T("dodgerblue"), 0x1e90ff},
    {_T("steelblue"), 0x4682b4},
    {_T("royalblue"), 0x4169e1},
    {_T("blue"), 0x0000ff},
    {_T("mediumblue"), 0x0000cd},
    {_T("mediumslateblue"), 0x7b68ee},
    {_T("slateblue"), 0x6a5acd},
    {_T("darkslateblue"), 0x483d8b},
    {_T("darkblue"), 0x00008b},
    {_T("midnightblue"), 0x191970},
    {_T("navy"), 0x000080},

    {_T("palegreen"), 0x98fb98},
    {_T("lightgreen"), 0x90ee90},
    {_T("mediumspringgreen"), 0x00fa9a},
    {_T("springgreen"), 0x00ff7f},
    {_T("chartreuse"), 0x7fff00},
    {_T("lawngreen"), 0x7cfc00},
    {_T("lime"), 0x00ff00},
    {_T("limegreen"), 0x32cd32},
    {_T("greenyellow"), 0xadff2f},
    {_T("yellowgreen"), 0x9acd32},
    {_T("darkseagreen"), 0x8fbc8f},
    {_T("mediumseagreen"), 0x3cb371},
    {_T("seagreen"), 0x2e8b57},
    {_T("olivedrab"), 0x6b8e23},
    {_T("forestgreen"), 0x228b22},
    {_T("green"), 0x008000},
    {_T("darkkhaki"), 0xbdb76b},
    {_T("olive"), 0x808000},
    {_T("darkolivegreen"), 0x556b2f},
    {_T("darkgreen"), 0x006400},

    {_T("floralwhite"), 0xfffaf0},
    {_T("seashell"), 0xfff5ee},
    {_T("ivory"), 0xfffff0},
    {_T("beige"), 0xf5f5dc},
    {_T("cornsilk"), 0xfff8dc},
    {_T("lemonchiffon"), 0xfffacd},
    {_T("lightyellow"), 0xffffe0},
    {_T("lightgoldenrodyellow"), 0xfafad2},
    {_T("papayawhip"), 0xffefd5},
    {_T("blanchedalmond"), 0xffedcd},
    {_T("palegoldenrod"), 0xeee8aa},
    {_T("khaki"), 0xf0eb8c},
    {_T("bisque"), 0xffe4c4},
    {_T("moccasin"), 0xffe4b5},
    {_T("navajowhite"), 0xffdead},
    {_T("peachpuff"), 0xffdab9},
    {_T("yellow"), 0xffff00},
    {_T("gold"), 0xffd700},
    {_T("wheat"), 0xf5deb3},
    {_T("orange"), 0xffa500},
    {_T("darkorange"), 0xff8c00},

    {_T("oldlace"), 0xfdf5e6},
    {_T("linen"), 0xfaf0e6},
    {_T("antiquewhite"), 0xfaebd7},
    {_T("lightsalmon"), 0xffa07a},
    {_T("darksalmon"), 0xe9967a},
    {_T("salmon"), 0xfa8072},
    {_T("lightcoral"), 0xf08080},
    {_T("indianred"), 0xcd5c5c},
    {_T("coral"), 0xff7f50},
    {_T("tomato"), 0xff6347},
    {_T("orangered"), 0xff4500},
    {_T("red"), 0xff0000},
    {_T("crimson"), 0xdc143c},
    {_T("firebrick"), 0xb22222},
    {_T("maroon"), 0x800000},
    {_T("darkred"), 0x8b0000},

    {_T("lavender"), 0xe6e6fe},
    {_T("lavenderblush"), 0xfff0f5},
    {_T("mistyrose"), 0xffe4e1},
    {_T("thistle"), 0xd8bfd8},
    {_T("pink"), 0xffc0cb},
    {_T("lightpink"), 0xffb6c1},
    {_T("palevioletred"), 0xdb7093},
    {_T("hotpink"), 0xff69b4},
    {_T("fuchsia"), 0xff00ee},
    {_T("magenta"), 0xff00ff},
    {_T("mediumvioletred"), 0xc71585},
    {_T("deeppink"), 0xff1493},
    {_T("plum"), 0xdda0dd},
    {_T("violet"), 0xee82ee},
    {_T("orchid"), 0xda70d6},
    {_T("mediumorchid"), 0xba55d3},
    {_T("mediumpurple"), 0x9370db},
    {_T("purple"), 0x9370db},
    {_T("blueviolet"), 0x8a2be2},
    {_T("darkviolet"), 0x9400d3},
    {_T("darkorchid"), 0x9932cc},

    {_T("tan"), 0xd2b48c},
    {_T("burlywood"), 0xdeb887},
    {_T("sandybrown"), 0xf4a460},
    {_T("peru"), 0xcd853f},
    {_T("goldenrod"), 0xdaa520},
    {_T("darkgoldenrod"), 0xb8860b},
    {_T("chocolate"), 0xd2691e},
    {_T("rosybrown"), 0xbc8f8f},
    {_T("sienna"), 0xa0522d},
    {_T("saddlebrown"), 0x8b4513},
    {_T("brown"), 0xa52a2a},
};

CHtmlColorMap::CHtmlColorMap()
{
    for(int i = 0; i < _countof(htmlcolors); i++)
        SetAt(htmlcolors[i].name, htmlcolors[i].color);
}

CHtmlColorMap g_colors;

CString g_default_style(_T("Default"));

//


//

static DWORD CharSetToCodePage(DWORD dwCharSet)
{
    CHARSETINFO cs={0};
    DWORD_PTR dwcp = intptr_t(dwCharSet);
    ::TranslateCharsetInfo((DWORD *)dwcp, &cs, TCI_SRCCHARSET);
    return cs.ciACP;
}

static int FindChar(CStringW str, WCHAR c, int pos, bool fUnicode, int CharSet)
{
    if(fUnicode) return(str.Find(c, pos));

    int fStyleMod = 0;

    DWORD cp = CharSetToCodePage(CharSet);
    int OrgCharSet = CharSet;

    for(int i = 0, j = str.GetLength(), k; i < j; i++)
    {
        WCHAR c2 = str[i];

        if(IsDBCSLeadByteEx(cp, (BYTE)c2)) i++;
        else if(i >= pos)
        {
            if(c2 == c) return(i);
        }

        if(c2 == '{') fStyleMod++;
        else if(fStyleMod > 0)
        {
            if(c2 == '}') fStyleMod--;
            else if(c2 == 'e' && i >= 3 && i < j-1 && str.Mid(i-2, 3) == L"\\fe")
            {
                CharSet = 0;
                for(k = i+1; _istdigit(str[k]); k++) CharSet = CharSet*10 + (str[k] - '0');
                if(k == i+1) CharSet = OrgCharSet;

                cp = CharSetToCodePage(CharSet);
            }
        }
    }

    return(-1);
}

static CStringW ToUnicode(CStringW str, DWORD CharSet)
{
    CStringW ret;

    DWORD cp = CharSetToCodePage(CharSet);

    for(int i = 0, j = str.GetLength(); i < j; i++)
    {
        WCHAR wc = str.GetAt(i);
        char c = wc&0xff;

        if(IsDBCSLeadByteEx(cp, (BYTE)wc))
        {
            i++;

            if(i < j)
            {
                char cc[2];
                cc[0] = c;
                cc[1] = (char)str.GetAt(i);

                MultiByteToWideChar(cp, 0, cc, 2, &wc, 1);
            }
        }
        else
        {
            MultiByteToWideChar(cp, 0, &c, 1, &wc, 1);
        }

        ret += wc;
    }

    return(ret);
}

static CStringW MBCSSSAToUnicode(CStringW str, int CharSet)
{
    CStringW ret;

    int OrgCharSet = CharSet;

    for(int j = 0; j < str.GetLength(); )
    {
        j = FindChar(str, '{', 0, false, CharSet);

        if(j >= 0)
        {
            ret += ToUnicode(str.Left(j), CharSet);
            str = str.Mid(j);

            j = FindChar(str, '}', 0, false, CharSet);

            if(j < 0)
            {
                ret += ToUnicode(str, CharSet);
                break;
            }
            else
            {
                int k = str.Find(L"\\fe");
                if(k >= 0 && k < j)
                {
                    CharSet = 0;
                    int l = k+3;
                    for(; _istdigit(str[l]); l++) CharSet = CharSet*10 + (str[l] - '0');
                    if(l == k+3) CharSet = OrgCharSet;
                }

                j++;

                ret += ToUnicode(str.Left(j), OrgCharSet);
                str = str.Mid(j);
                j = 0;
            }
        }
        else
        {
            ret += ToUnicode(str, CharSet);
            break;
        }
    }

    return(ret);
}

static CStringW RemoveSSATags(CStringW str, bool fUnicode, int CharSet)
{
    str.Replace (L"{\\i1}", L"<i>");
    str.Replace (L"{\\i}", L"</i>");

    for(int i = 0, j; i < str.GetLength(); )
    {
        if((i = FindChar(str, '{', i, fUnicode, CharSet)) < 0) break;
        if((j = FindChar(str, '}', i, fUnicode, CharSet)) < 0) break;
        str.Delete(i, j-i+1);
    }

    str.Replace(L"\\N", L"\n");
    str.Replace(L"\\n", L"\n");
    str.Replace(L"\\h", L" ");

    return(str);
}

//

static inline CStringW GetStr(CStringW& buff, WCHAR sep = L',') //throw(...)
{
    buff.TrimLeft();

    int pos = buff.Find(sep);
    if(pos < 0)
    {
        pos = buff.GetLength();
        if(pos < 1) throw 1;
    }

    CStringW ret = buff.Left(pos);
    if(pos < buff.GetLength()) buff.Delete(0, pos + 1);

    return(ret);
}

static inline int GetInt(CStringW& buff, WCHAR sep = L',') //throw(...)
{
    CStringW str;

    str = GetStr(buff, sep);
    str.MakeLower();

    CStringW fmtstr = str.GetLength() > 2 && (str.Left(2) == L"&h" || str.Left(2) == L"0x")
        ? str = str.Mid(2), L"%x"
        : L"%d";

    int ret;
    if(swscanf(str, fmtstr, &ret) != 1) throw 1;

    return(ret);
}

static inline double GetFloat(CStringW& buff, WCHAR sep = L',') //throw(...)
{
    CStringW str;

    str = GetStr(buff, sep);
    str.MakeLower();

    float ret;
    if(swscanf(str, L"%f", &ret) != 1) throw 1;

    return((double)ret);
}

static inline CStringW::PCXSTR TryNextStr(CStringW::PXSTR * buff, WCHAR sep = L',')
{
    CStringW::PXSTR start = NULL;
    CStringW::PXSTR ret = NULL;
    for(start=*buff; *start!=0 && *start==WCHAR(' '); start++) ;
    
    *buff=start;
    ret = start;

    for( ;*start!=0 && *start!=sep; start++) ;

    if(*start!=0)
        *buff = start+1;

    *start = 0;

    return(ret);
}

static inline int NextInt(CStringW::PXSTR * buff, WCHAR sep = L',') //throw(...)
{
    CStringW::PCXSTR str = TryNextStr(buff, sep);
    CStringW::PXSTR strEnd;

    int ret;
    if (str[0] == '&' && (str[1] == 'h' || str[1] == 'H') || str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
        ret = (int)wcstoul(str, &strEnd, 16);
    } else {
        ret = wcstol(str, &strEnd, 10);
    }

    if (str == strEnd) { // Ensure something was parsed
        throw 1;
    }

    return(ret);
}

static bool LoadFont(CString& font)
{
    int len = font.GetLength();

    CAutoVectorPtr<BYTE> pData;
    if(len == 0 || (len&3) == 1 || !pData.Allocate(len))
        return(false);

    const TCHAR* s = font;
    const TCHAR* e = s + len;
    for(BYTE* p = pData; s < e; s++, p++) *p = BYTE(*s - 33);

    for(int i = 0, j = 0, k = len&~3; i < k; i+=4, j+=3)
    {
        pData[j+0] = ((pData[i+0]&63)<<2)|((pData[i+1]>>4)& 3);
        pData[j+1] = ((pData[i+1]&15)<<4)|((pData[i+2]>>2)&15);
        pData[j+2] = ((pData[i+2]& 3)<<6)|((pData[i+3]>>0)&63);
    }

    int datalen = (len&~3)*3/4;

    if((len&3) == 2)
    {
        pData[datalen++] = ((pData[(len&~3)+0]&63)<<2)|((pData[(len&~3)+1]>>4)&3);
    }
    else if((len&3) == 3)
    {
        pData[datalen++] = ((pData[(len&~3)+0]&63)<<2)|((pData[(len&~3)+1]>>4)& 3);
        pData[datalen++] = ((pData[(len&~3)+1]&15)<<4)|((pData[(len&~3)+2]>>2)&15);
    }

    // The font is private to this process and freed when it exits.
    DWORD cFonts;
    AddFontMemResourceEx(pData, datalen, NULL, &cFonts);

    return(true);
}

static bool LoadUUEFont(CTextFile* file)
{
    CString s, font;
    while(file->ReadString(s))
    {
        s.Trim();
        if(s.IsEmpty()) break;
        if(s[0] == '[') // check for standard section headers
        {
        	if(s.Find(_T("[Script Info]")) == 0) break;
        	if(s.Find(_T("[V4+ Styles]")) == 0) break;
        	if(s.Find(_T("[V4 Styles]")) == 0) break;
        	if(s.Find(_T("[Events]")) == 0) break;
        	if(s.Find(_T("[Fonts]")) == 0) break;
        	if(s.Find(_T("[Graphics]")) == 0) break;
        }
        if(s.Find(_T("fontname:")) == 0) {LoadFont(font); font.Empty(); continue;}

        font += s;
    }

    if(!font.IsEmpty())
        LoadFont(font);

    return(true);
}

static bool OpenSubStationAlpha(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    bool fRet = false;

    int version = 3, sver = 3;


    CStringW buff;
    while(true)
    {
    	bool fEOF = !file->ReadString(buff);
    	if(fEOF && buff.IsEmpty()) break;

        buff.Trim();
        if(buff.IsEmpty() || buff.GetAt(0) == ';') continue;

        CStringW entry = GetStr(buff, ':');
//  }
//      catch(...) {continue;}

        entry.MakeLower();

        if(entry == L"dialogue")
        {
            try
            {
                CStringW::PXSTR __buff = buff.GetBuffer();
                int hh1, mm1, ss1, ms1_div10, hh2, mm2, ss2, ms2_div10, layer = 0;
                CString Style, Actor, Effect;
                CRect marginRect;

                if(version <= 4){TryNextStr(&__buff, L'='); NextInt(&__buff);} /* Marked = */
                if(version >= 5)layer = NextInt(&__buff);
                hh1 = NextInt(&__buff, L':');
                mm1 = NextInt(&__buff, L':');
                ss1 = NextInt(&__buff, L'.');
                ms1_div10 = NextInt(&__buff);
                hh2 = NextInt(&__buff, L':');
                mm2 = NextInt(&__buff, L':');
                ss2 = NextInt(&__buff, L'.');
                ms2_div10 = NextInt(&__buff);
                Style = TryNextStr(&__buff);
                Actor = TryNextStr(&__buff);
                marginRect.left = NextInt(&__buff);
                marginRect.right = NextInt(&__buff);
                marginRect.top = marginRect.bottom = NextInt(&__buff);
                if(version >= 6)marginRect.bottom = NextInt(&__buff);
                Effect = TryNextStr(&__buff);

                CStringW buff2 = __buff;
                int len = min(Effect.GetLength(), buff2.GetLength());
                if(Effect.Left(len) == buff2.Left(len)) Effect.Empty();

                Style.TrimLeft(_T('*'));
                if(!Style.CompareNoCase(_T("Default"))) Style = _T("Default");

                ret.AddSTSEntryOnly(buff2,
                    file->IsUnicode(),
                    MS2RT((((hh1 * 60i64 + mm1) * 60i64) + ss1) * 1000i64 + ms1_div10 * 10i64),
                    MS2RT((((hh2 * 60i64 + mm2) * 60i64) + ss2) * 1000i64 + ms2_div10 * 10i64),
                    Style, Actor, Effect,
                    marginRect,
                    layer);
            }
            catch(...)
            {
                //                ASSERT(0);
                //                throw;
                return(false);
            }
        }
        else if(entry == L"style")
        {
            STSStyle* style = DEBUG_NEW STSStyle;
            if(!style) return(false);

            try
            {
                CString StyleName;
                int alpha;
                int encoding;
                CRect tmp_rect;

                StyleName = GetStr(buff);
                style->fontName = GetStr(buff);
                style->fontSize = GetFloat(buff);
                for(size_t i = 0; i < 4; i++) style->colors[i] = (COLORREF)GetInt(buff);
                style->fontWeight = !!GetInt(buff) ? FW_BOLD : FW_NORMAL;
                style->fItalic = !!GetInt(buff);
if(sver >= 5)   style->fUnderline = !!GetInt(buff);
if(sver >= 5)   style->fStrikeOut = !!GetInt(buff);
if(sver >= 5)   style->fontScaleX = GetFloat(buff);
if(sver >= 5)   style->fontScaleY = GetFloat(buff);
if(sver >= 5)   style->fontSpacing = GetFloat(buff);
if(sver >= 5)   style->fontAngleZ = GetFloat(buff);
if(sver >= 4)   style->borderStyle = GetInt(buff);
                style->outlineWidthX = style->outlineWidthY = GetFloat(buff);
                style->shadowDepthX = style->shadowDepthY = GetFloat(buff);
                style->scrAlignment = GetInt(buff);
                tmp_rect.left =  GetInt(buff);
                tmp_rect.right = GetInt(buff);
                tmp_rect.top = tmp_rect.bottom = GetInt(buff);
if(sver >= 6)   tmp_rect.bottom = GetInt(buff);
                style->marginRect = tmp_rect;
if(sver <= 4)   alpha = GetInt(buff);
                encoding = GetInt(buff);
if(sver >= 6)   style->relativeTo = GetInt(buff);

                // Map unsupported extension to the most permissive charSet
                style->charSet = encoding < 0 ? DEFAULT_CHARSET : encoding;
if(sver <= 4)   style->colors[2] = style->colors[3]; // style->colors[2] is used for drawing the outline
if(sver <= 4)   alpha = max(min(alpha, 0xff), 0);
if(sver <= 4)   {for(size_t i = 0; i < 3; i++) style->alpha[i] = alpha; style->alpha[3] = 0x80;}
if(sver >= 5)   for(size_t i = 0; i < 4; i++) {style->alpha[i] = (BYTE)(style->colors[i]>>24); style->colors[i] &= 0xffffff;}
if(sver >= 5)   style->fontScaleX = max(style->fontScaleX, 0);
if(sver >= 5)   style->fontScaleY = max(style->fontScaleY, 0);
if(sver >= 5)   style->fontSpacing = max(style->fontSpacing, 0);
                style->fontAngleX = style->fontAngleY = 0;
                style->borderStyle = style->borderStyle == 1 ? 0 : style->borderStyle == 3 ? 1 : 0;
                style->outlineWidthX = max(style->outlineWidthX, 0);
                style->outlineWidthY = max(style->outlineWidthY, 0);
                style->shadowDepthX = max(style->shadowDepthX, 0);
                style->shadowDepthY = max(style->shadowDepthY, 0);
if(sver <= 4)   style->scrAlignment = (style->scrAlignment&4) ? ((style->scrAlignment&3)+6) // top
                                        : (style->scrAlignment&8) ? ((style->scrAlignment&3)+3) // mid
                                        : (style->scrAlignment&3); // bottom

                StyleName.TrimLeft(_T('*'));

                ret.AddStyle(StyleName, style);
            }
            catch(...)
            {
                delete style;
                return(false);
            }
        }
        else if(entry == L"[script info]")
        {
            fRet = true;
        }
        else if(entry == L"playresx")
        {
            try {ret.m_dstScreenSize.cx = GetInt(buff);}
            catch(...) {ret.m_dstScreenSize = CSize(0, 0); return(false);}

            if(ret.m_dstScreenSize.cy <= 0)
            {
                ret.m_dstScreenSize.cy = (ret.m_dstScreenSize.cx == 1280)
                    ? 1024
                    : ret.m_dstScreenSize.cx * 3 / 4;
            }
        }
        else if(entry == L"playresy")
        {
            try {ret.m_dstScreenSize.cy = GetInt(buff);}
            catch(...) {ret.m_dstScreenSize = CSize(0, 0); return(false);}

            if(ret.m_dstScreenSize.cx <= 0)
            {
                ret.m_dstScreenSize.cx = (ret.m_dstScreenSize.cy == 1024)
                    ? 1280
                    : ret.m_dstScreenSize.cy * 4 / 3;
            }
        }
        else if(entry == L"layoutresx")
        {
            try {ret.m_layout_size.cx = GetInt(buff);}
            catch(...) {ret.m_layout_size = CSize(0, 0); return(false);}
        }
        else if(entry == L"layoutresy")
        {
            try {ret.m_layout_size.cy = GetInt(buff);}
            catch(...) {ret.m_layout_size = CSize(0, 0); return(false);}
        }
        else if(entry == L"wrapstyle")
        {
            try {ret.m_defaultWrapStyle = GetInt(buff);}
            catch(...) {ret.m_defaultWrapStyle = 1; return(false);}
        }
        else if(entry == L"scripttype")
        {
            if(buff.GetLength() >= 4 && !buff.Right(4).CompareNoCase(L"4.00")) version = sver = 4;
            else if(buff.GetLength() >= 5 && !buff.Right(5).CompareNoCase(L"4.00+")) version = sver = 5;
            else if(buff.GetLength() >= 6 && !buff.Right(6).CompareNoCase(L"4.00++")) version = sver = 6;
        }
        else if(entry == L"collisions")
        {
            buff = GetStr(buff);
            buff.MakeLower();
            ret.m_collisions = buff.Find(L"reverse") >= 0 ? 1 : 0;
        }
        else if(entry == L"scaledborderandshadow")
        {
            buff = GetStr(buff);
            buff.MakeLower();
            ret.m_fScaledBAS = buff.Find(L"yes") >= 0;
        }
        else if(entry == L"[v4 styles]")
        {
            fRet = true;
            sver = 4;
        }
        else if(entry == L"[v4+ styles]")
        {
            fRet = true;
            sver = 5;
        }
        else if(entry == L"[v4++ styles]")
        {
            fRet = true;
            sver = 6;
        }
        else if(entry == L"[events]")
        {
            fRet = true;
        }
        else if(entry == L"fontname")
        {
            LoadUUEFont(file);
        }
        if(fEOF) break;
    }
//    ret.Sort();
    return(fRet);
}

typedef bool (*STSOpenFunct)(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet);

struct OpenFunctStruct {
    STSOpenFunct open;
    tmode mode;
};

static OpenFunctStruct OpenFuncts[] =
{
    OpenSubStationAlpha, TIME,
};

static int nOpenFuncts = _countof(OpenFuncts);

//

CSimpleTextSubtitle::CSimpleTextSubtitle()
{
    m_mode                 = TIME;
    m_dstScreenSize        = CSize(0, 0);
    m_defaultWrapStyle     = 0;
    m_collisions           = 0;
    m_fScaledBAS           = false;
    m_encoding             = CTextFile::DEFAULT_ENCODING;
    m_ePARCompensationType = EPCTDisabled;
    m_dPARCompensation     = 1.0;
    m_defaultStyle.charSet = DEFAULT_CHARSET;
}

CSimpleTextSubtitle::~CSimpleTextSubtitle()
{
    Empty();
}

void CSimpleTextSubtitle::Copy(CSimpleTextSubtitle& sts)
{
    Empty();

    m_name                         = sts.m_name;
    m_mode                         = sts.m_mode;
    m_dstScreenSize                = sts.m_dstScreenSize;
    m_defaultWrapStyle             = sts.m_defaultWrapStyle;
    m_collisions                   = sts.m_collisions;
    m_fScaledBAS                   = sts.m_fScaledBAS;
    m_encoding                     = sts.m_encoding;
    m_defaultStyle                 = sts.m_defaultStyle;
    CopyStyles     (sts.m_styles  );
    m_segments.Copy(sts.m_segments);
    m_entries.Copy (sts.m_entries );
}

void CSTSStyleMap::Free()
{
    POSITION pos = GetStartPosition();
    while(pos)
    {
        CString key;
        STSStyle* val;
        GetNextAssoc(pos, key, val);
        delete val;
    }

    RemoveAll();
}

bool CSimpleTextSubtitle::CopyStyles(const CSTSStyleMap& styles, bool fAppend)
{
    if(!fAppend) m_styles.Free();

    POSITION pos = styles.GetStartPosition();
    while(pos)
    {
        CString key;
        STSStyle* val;
        styles.GetNextAssoc(pos, key, val);

        STSStyle* s = DEBUG_NEW STSStyle;
        if(!s) return(false);

        *s = *val;

        AddStyle(key, s);
    }

    return(true);
}

void CSimpleTextSubtitle::Empty()
{
    m_dstScreenSize = CSize(0, 0);
    m_styles.Free();
    m_segments.RemoveAll();
    m_entries.RemoveAll();
}

static bool SegmentCompStart(const STSSegment& segment, REFERENCE_TIME start)
{
    return (segment.start < start);
}

void CSimpleTextSubtitle::Add(CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end,
    CString style, const CString& actor, const CString& effect, const CRect& marginRect, int layer, int readorder)
{
    str.Trim();
    if (str.IsEmpty() || start > end) return;

    str.Remove('\r');
    str.Replace(L"\n", L"\\N");
    if(style.IsEmpty()) style = g_default_style;
    else if(style!=g_default_style)
    {
        style.TrimLeft('*');
    }

    STSEntry sub;
    sub.str        = str;
    sub.fUnicode   = fUnicode;
    sub.style      = style;
    sub.actor      = actor;
    sub.effect     = effect;
    sub.marginRect = marginRect;
    sub.layer      = layer;
    sub.start      = start;
    sub.end        = end;
    sub.readorder  = readorder < 0 ? (int)m_entries.GetCount() : readorder;

    int n = (int)m_entries.GetCount();

    if (start == end) return;

    size_t segmentsCount = m_segments.GetCount();

    if (segmentsCount == 0) { // First segment
        n = (int)m_entries.Add(sub);

        STSSegment stss(start, end);
        stss.subs.Add(n);
        m_segments.Add(stss);
    } else {
        STSSegment* segmentsStart = m_segments.GetData();
        STSSegment* segmentsEnd   = segmentsStart + segmentsCount;
        STSSegment* segment = std::lower_bound(segmentsStart, segmentsEnd, start, SegmentCompStart);

        n = (int)m_entries.Add(sub);

        size_t i = segment - segmentsStart;
        if (i > 0 && m_segments[i - 1].end > start) {
            // The beginning of i-1th segment isn't modified
            // by the new entry so separate it in two segments
            STSSegment stss(m_segments[i - 1].start, start);
            stss.subs.Copy(m_segments[i - 1].subs);
            m_segments[i - 1].start = start;
            m_segments.InsertAt(i - 1, stss);
        } else if (i < segmentsCount && start < m_segments[i].start) {
            // The new entry doesn't start in an existing segment.
            // It might even not overlap with any segment at all
            STSSegment stss(start, min(end, m_segments[i].start));
            stss.subs.Add(n);
            m_segments.InsertAt(i, stss);
            i++;
        }

        REFERENCE_TIME lastEnd = _I64_MAX;
        for (; i < m_segments.GetCount() && m_segments[i].start < end; i++) {
            STSSegment& s = m_segments[i];

            if (lastEnd < s.start) {
                // There is a gap between the segments overlapped by
                // the new entry so we have to create a new one
                STSSegment stss(lastEnd, s.start);
                stss.subs.Add(n);
                lastEnd = s.start; // s might not point on the right segment after inserting so do the modification now
                m_segments.InsertAt(i, stss);
            } else {
                if (end < s.end) {
                    // The end of current segment isn't modified
                    // by the new entry so separate it in two segments
                    STSSegment stss(end, s.end);
                    stss.subs.Copy(s.subs);
                    s.end = end; // s might not point on the right segment after inserting so do the modification now
                    m_segments.InsertAt(i + 1, stss);
                }

                // The array might have been reallocated so create a new reference
                STSSegment& sAdd = m_segments[i];

                // Add the entry to the current segment now that we are you it belongs to it
                size_t entriesCount = sAdd.subs.GetCount();
                // Take a shortcut when possible
                if (!entriesCount || sub.readorder >= m_entries.GetAt(sAdd.subs[entriesCount - 1]).readorder) {
                    sAdd.subs.Add(n);
                } else {
                    for (size_t j = 0; j < entriesCount; j++) {
                        if (sub.readorder < m_entries.GetAt(sAdd.subs[j]).readorder) {
                            sAdd.subs.InsertAt(j, n);
                            break;
                        }
                    }
                }

                lastEnd = sAdd.end;
            }
        }

        if (end > m_segments[i - 1].end) {
            // The new entry ends after the last overlapping segment.
            // It might even not overlap with any segment at all
            STSSegment stss(max(start, m_segments[i - 1].end), end);
            stss.subs.Add(n);
            m_segments.InsertAt(i, stss);
        }
    }
}

void CSimpleTextSubtitle::AddSTSEntryOnly( CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end, CString style /*= _T("Default")*/, const CString& actor /*= _T("")*/, const CString& effect /*= _T("")*/, const CRect& marginRect /*= CRect(0,0,0,0)*/, int layer /*= 0*/, int readorder /*= -1*/ )
{
    str.Trim();
    if (str.IsEmpty() || start > end) return;

    str.Remove('\r');
    str.Replace(L"\n", L"\\N");
    if(style.IsEmpty()) style = _T("Default");
    style.TrimLeft('*');

    STSEntry sub;
    sub.str        = str;
    sub.fUnicode   = fUnicode;
    sub.style      = style;
    sub.actor      = actor;
    sub.effect     = effect;
    sub.marginRect = marginRect;
    sub.layer      = layer;
    sub.start      = start;
    sub.end        = end;
    sub.readorder  = readorder < 0 ? m_entries.GetCount() : readorder;
    m_entries.Add(sub);
    return;
}

void CSimpleTextSubtitle::ChangeUnknownStylesToDefault()
{
    CString last_style = _T("!@dkdfjlakfkjjklezdv^5132ae");
    bool last_style_changed_to_unknow = false;
    for(size_t i = 0; i < m_entries.GetCount(); i++)
    {
        STSEntry& stse = m_entries.GetAt(i);

        STSStyle* val;
        if (last_style == stse.style)
        {
            // simple heuristic rule: the style is likely to be the same as the last one,
            // then there is no need to query the map
            if (last_style_changed_to_unknow)
            {
                stse.style = g_default_style;
            }
        }
        else
        {
            last_style = stse.style;
            last_style_changed_to_unknow = !m_styles.Lookup(stse.style, val);
            if (last_style_changed_to_unknow)
            {
                stse.style = g_default_style;
            }
        }
    }
}

void CSimpleTextSubtitle::AddStyle(CString name, STSStyle* style)
{
    int i, j;

    if(name.IsEmpty()) name = g_default_style;

    STSStyle* val;
    if(m_styles.Lookup(name, val))
    {
        if(*val == *style)
        {
            delete style;
            return;
        }
        const CString& name_str = name;

        int len = name_str.GetLength();

        for(i = len; i > 0 && _istdigit(name_str[i-1]); i--);

        int idx = 1;

        CString name2 = name_str;

        if(i < len && _stscanf_s(name_str.Right(len-i), _T("%d"), &idx) == 1)
        {
            name2 = name_str.Left(i);
        }

        idx++;

        CString name3;
        CString name3_str;
        do
        {
            name3_str.Format(_T("%s%d"), name2, idx);
            name3 = name3_str;
            idx++;
        }
        while(m_styles.Lookup(name3));

        m_styles.RemoveKey(name);
        m_styles[name3] = val;

        for (size_t j = 0, count = m_entries.GetCount(); j < count; j++)
        {
            STSEntry& stse = m_entries.GetAt(j);
            if(stse.style == name) stse.style = name3;
        }
    }

    m_styles[name] = style;
}




STSSegment* CSimpleTextSubtitle::SearchSubs2(REFERENCE_TIME t, double fps, /*[out]*/ int* iSegment, int* nSegments)
{
    int segmentsCount = m_segments.GetCount();
    int i = 0, j = segmentsCount - 1;

    if(iSegment) *iSegment = -1;
    if(nSegments) *nSegments = segmentsCount;

    if(segmentsCount<=0)
    {
        if(iSegment) *iSegment = 0;
        return NULL;
    }

    if(t >= TranslateSegmentEnd(j, fps))
    {
        i = j;
        j++;
    }
    if(t < TranslateSegmentEnd(i, fps))
    {
        j = i;
        i--;
    }

    while(i < j-1)
    {
        int mid = (i + j) >> 1;

        REFERENCE_TIME midt = TranslateSegmentEnd(mid, fps);

        if(t < midt)
            j=mid;
        else
            i=mid;
    }
    if(j<segmentsCount && t>=TranslateSegmentStart(j, fps))
    {
        if(iSegment) *iSegment = j;
        return &m_segments[j];
    }
    return(NULL);
}

REFERENCE_TIME CSimpleTextSubtitle::TranslateStart(int i, double fps)
{
    int n = m_entries.GetCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_entries.GetAt(i).start :
        m_mode == FRAME ? std::llround(m_entries.GetAt(i).start * UNITS_FLOAT / fps) :
        0);
}

REFERENCE_TIME CSimpleTextSubtitle::TranslateEnd(int i, double fps)
{
    int n = m_entries.GetCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_entries.GetAt(i).end :
        m_mode == FRAME ? std::llround(m_entries.GetAt(i).end * UNITS_FLOAT / fps) :
        0);
}

REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentStart(int i, double fps)
{
    int n = m_segments.GetCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_segments[i].start :
        m_mode == FRAME ? std::llround(m_segments[i].start * UNITS_FLOAT / fps) :
        0);
}

REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentEnd(int i, double fps)
{
    int n = m_segments.GetCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_segments[i].end :
        m_mode == FRAME ? std::llround(m_segments[i].end * UNITS_FLOAT / fps) :
        0);
}


STSStyle* CSimpleTextSubtitle::GetStyle(int i)
{
    STSStyle* style = NULL;
    if (!m_styles.Lookup(m_entries.GetAt(i).style, style) &&
        !m_styles.Lookup(         g_default_style, style))
    {
        style = &m_defaultStyle;
    }
    ASSERT(style);

    return style;
}

bool CSimpleTextSubtitle::GetStyle(int i, STSStyle* const stss)
{
    *stss = *GetStyle(i);

    return true;
}

int CSimpleTextSubtitle::GetCharSet(int i)
{
    STSStyle* style = GetStyle(i);
    return style!=NULL ? style->charSet : -1;
}

CStringW CSimpleTextSubtitle::GetStrW(int i, bool fSSA)
{
    STSEntry const& stse = m_entries.GetAt(i);
    int CharSet = GetCharSet(i);

    CStringW str = stse.str;

    if(!stse.fUnicode)
        str = MBCSSSAToUnicode(str, CharSet);

    if(!fSSA)
        str = RemoveSSATags(str, true, CharSet);

    return str;
}

void CSimpleTextSubtitle::CreateSegments()
{
    m_segments.RemoveAll();

    if(m_entries.GetCount()>0)
    {
        size_t start, mid, end;
        CAtlArray<STSSegment> tempSegments;//if add to m_segments directly, then remove empty entities can be a
                                           //complex operation when having large segmentCount and lots of empty entities
        std::vector<REFERENCE_TIME> breakpoints(2*m_entries.GetCount());
        for(size_t i = 0; i < m_entries.GetCount(); i++)
        {
            STSEntry& stse = m_entries.GetAt(i);
            breakpoints[2*i]=stse.start;
            breakpoints[2*i+1]=stse.end;
        }

        std::sort(breakpoints.begin(), breakpoints.end());

        int ptr = 1;
        REFERENCE_TIME prev = breakpoints[0];
        for(size_t i = breakpoints.size()-1; i > 0; i--, ptr++)
        {
            if(breakpoints[ptr] != prev)
            {
                tempSegments.Add(STSSegment(prev, breakpoints[ptr]));
                prev = breakpoints[ptr];
            }
        }

        size_t segmentCount = tempSegments.GetCount();

        for(size_t i = 0; i < m_entries.GetCount(); i++)
        {
            STSEntry& stse = m_entries.GetAt(i);
            start = 0;
            end = segmentCount;
            while(start<end)
            {
                mid = (start+end)>>1;
                if(tempSegments[mid].start < stse.start)
                {
                    start = mid+1;
                }
                else
                {
                    end = mid;
                }
            }
            for(; start < tempSegments.GetCount() && tempSegments[start].end <= stse.end; start++)
                tempSegments[start].subs.Add(i);
        }
        for(size_t i = 0; i < segmentCount; i++)
            if(tempSegments[i].subs.GetCount()>0)
                m_segments.Add(tempSegments[i]);
    }
    OnChanged();
}

bool CSimpleTextSubtitle::Open(CString fn, int CharSet, CString name)
{
    Empty();

    CTextFile f(CTextFile::UTF8);
    if(!f.Open(fn)) return(false);

    fn.Replace('\\', '/');
    if(name.IsEmpty())
    {
        name = fn.Left(fn.ReverseFind('.'));
        name = name.Mid(name.ReverseFind('/')+1);
        name = name.Mid(name.ReverseFind('.')+1);
    }

    return(Open(&f, CharSet, name));
}


bool CSimpleTextSubtitle::Open(CTextFile* f, int CharSet, CString name)
{
    Empty();

    ULONGLONG pos = f->GetPosition();

    for(int i = 0; i < nOpenFuncts; i++)
    {
        if(!OpenFuncts[i].open(f, *this, CharSet) /*|| !GetCount()*/)
        {
            if(m_entries.GetCount() > 0)
            {
                Empty();
                break;
            }

            f->Seek(pos, CTextFile::begin);
            Empty();
            continue;
        }

        m_name = name;
        m_mode = OpenFuncts[i].mode;
        m_encoding = f->GetEncoding();
        m_path = f->GetFilePath();

        if(!m_path.IsEmpty())
        {
            CTextFile f2(CTextFile::UTF8);
            if(f2.Open(m_path + _T(".style")))
                OpenSubStationAlpha(&f2, *this, CharSet);
        }

        //      Sort();
        CreateSegments();

        ChangeUnknownStylesToDefault();

        if(m_dstScreenSize == CSize(0, 0)) m_dstScreenSize = CSize(384, 288);

        return(true);
    }

    return(false);
}

bool CSimpleTextSubtitle::Open(BYTE* data, int len, int CharSet, CString name)
{
    Empty();

    CTextFile f(CTextFile::UTF8);
    if(len < 0 || !f.OpenMemory(data, len)) return(false);

    return(Open(&f, CharSet, name));
}

bool CSimpleTextSubtitle::IsEmpty()
{
    return m_entries.IsEmpty();
}


////////////////////////////////////////////////////////////////////

bool STSStyleBase::operator==( const STSStyleBase& s ) const
{
    return charSet == s.charSet
        && fontName == s.fontName
        && fontSize == s.fontSize
        && fontWeight == s.fontWeight
        && fItalic == s.fItalic
        && fUnderline == s.fUnderline
        && fStrikeOut == s.fStrikeOut;
}

STSStyle::STSStyle()
{
    SetDefault();
}

void STSStyle::SetDefault()
{
    marginRect    = CRect(20, 20, 20, 20);
    scrAlignment  = 2;
    borderStyle   = 0;
    outlineWidthX = outlineWidthY = 2;
    shadowDepthX  = shadowDepthY = 3;
    colors[0]     = 0x00ffffff;
    colors[1]     = 0x0000ffff;
    colors[2]     = 0x00000000;
    colors[3]     = 0x00000000;
    alpha[0]      = 0x00;
    alpha[1]      = 0x00;
    alpha[2]      = 0x00;
    alpha[3]      = 0x80;
    charSet       = DEFAULT_CHARSET;
    fontName      = _T("Arial");
    fontSize      = 18;
    fontScaleX    = fontScaleY = 100;
    fontSpacing   = 0;
    fontWeight    = FW_BOLD;
    fItalic       = false;
    fUnderline    = false;
    fStrikeOut    = false;
    fBlur         = 0;
    fGaussianBlur = 0;
    fontShiftX    = 
    fontShiftY    = 
    fontAngleZ    = 
    fontAngleX    = 
    fontAngleY    = 0;
    relativeTo    = 2;
}

bool STSStyle::operator == (const STSStyle& s) const
{
    return(marginRect          == s.marginRect
        && scrAlignment        == s.scrAlignment
        && borderStyle         == s.borderStyle
        && outlineWidthX       == s.outlineWidthX
        && outlineWidthY       == s.outlineWidthY
        && shadowDepthX        == s.shadowDepthX
        && shadowDepthY        == s.shadowDepthY
        && *((int*)&colors[0]) == *((int*)&s.colors[0])
        && *((int*)&colors[1]) == *((int*)&s.colors[1])
        && *((int*)&colors[2]) == *((int*)&s.colors[2])
        && *((int*)&colors[3]) == *((int*)&s.colors[3])
        && alpha[0]            == s.alpha[0]
        && alpha[1]            == s.alpha[1]
        && alpha[2]            == s.alpha[2]
        && alpha[3]            == s.alpha[3]
        && fBlur               == s.fBlur
        && fGaussianBlur       == s.fGaussianBlur
        && relativeTo          == s.relativeTo
        && IsFontStyleEqual(s));
}

bool STSStyle::IsFontStyleEqual(const STSStyle& s) const
{
    return(
        charSet        == s.charSet
        && fontName    == s.fontName
        && fontSize    == s.fontSize
        && fontScaleX  == s.fontScaleX
        && fontScaleY  == s.fontScaleY
        && fontSpacing == s.fontSpacing
        && fontWeight  == s.fontWeight
        && fItalic     == s.fItalic
        && fUnderline  == s.fUnderline
        && fStrikeOut  == s.fStrikeOut
        && fontAngleZ  == s.fontAngleZ
        && fontAngleX  == s.fontAngleX
        && fontAngleY  == s.fontAngleY
        && fontShiftX  == s.fontShiftX
        && fontShiftY  == s.fontShiftY);
}

void STSStyle::operator = (const LOGFONT& lf)
{
    charSet    = lf.lfCharSet;
    fontName   = lf.lfFaceName;
    HDC hDC    = GetDC(0);
    fontSize   = -MulDiv(lf.lfHeight, 72, GetDeviceCaps(hDC, LOGPIXELSY));
    ReleaseDC(0, hDC);
//  fontAngleZ = (float)(1.0*lf.lfEscapement/10);
    fontWeight = lf.lfWeight;
    fItalic    = !!lf.lfItalic;
    fUnderline = !!lf.lfUnderline;
    fStrikeOut = !!lf.lfStrikeOut;
}

LOGFONTA& operator <<= (LOGFONTA& lfa, const STSStyleBase& s)
{
    lfa.lfCharSet = s.charSet;
    strncpy_s(lfa.lfFaceName, LF_FACESIZE, CStringA(s.fontName), _TRUNCATE);
    HDC hDC = GetDC(0);
    lfa.lfHeight = -MulDiv((int)(s.fontSize+0.5), GetDeviceCaps(hDC, LOGPIXELSY), 72);
    ReleaseDC(0, hDC);
    lfa.lfWeight    = s.fontWeight;
    lfa.lfItalic    = s.fItalic?-1:0;
    lfa.lfUnderline = s.fUnderline?-1:0;
    lfa.lfStrikeOut = s.fStrikeOut?-1:0;
    return(lfa);
}

LOGFONTW& operator <<= (LOGFONTW& lfw, const STSStyleBase& s)
{
    lfw.lfCharSet = s.charSet;
    wcsncpy_s(lfw.lfFaceName, LF_FACESIZE, CStringW(s.fontName), _TRUNCATE);
    HDC hDC = GetDC(0);
    lfw.lfHeight = -MulDiv((int)(s.fontSize+0.5), GetDeviceCaps(hDC, LOGPIXELSY), 72);
    ReleaseDC(0, hDC);
    lfw.lfWeight    = s.fontWeight;
    lfw.lfItalic    = s.fItalic?-1:0;
    lfw.lfUnderline = s.fUnderline?-1:0;
    lfw.lfStrikeOut = s.fStrikeOut?-1:0;
    return(lfw);
}

CString& operator <<= (CString& style, const STSStyle& s)
{
    style.Format(_T("%d;%d;%d;%d;")
                 _T("%d;%d;%f;%f;%f;%f;")
                 _T("0x%06x;0x%06x;0x%06x;0x%06x;")
                 _T("0x%02x;0x%02x;0x%02x;0x%02x;")
                 _T("%d;")
                 _T("%s;%f;%f;%f;%f;%d;")
                 _T("%d;%d;%d;%f;%f;")
                 _T("%f;%f;%f;")
                 _T("%d"),
        s.marginRect.get().left, s.marginRect.get().right, s.marginRect.get().top, s.marginRect.get().bottom,
        s.scrAlignment, s.borderStyle,s.outlineWidthX, s.outlineWidthY, s.shadowDepthX, s.shadowDepthY,
        s.colors[0], s.colors[1], s.colors[2], s.colors[3],
        s.alpha[0], s.alpha[1], s.alpha[2], s.alpha[3],
        s.charSet,
        s.fontName,s.fontSize,s.fontScaleX, s.fontScaleY,s.fontSpacing,s.fontWeight,
        (int)s.fItalic, (int)s.fUnderline, (int)s.fStrikeOut, s.fBlur, s.fGaussianBlur,
        s.fontAngleZ, s.fontAngleX, s.fontAngleY,
        s.relativeTo);

    return(style);
}

STSStyle& operator <<= (STSStyle& s, const CString& style)
{
    s.SetDefault();

    try
    {
        CStringW str = style;
        if(str.Find(';')>=0)
        {
            CRect tmp_rect;
            tmp_rect.left = GetInt(str,L';'); tmp_rect.right = GetInt(str,L';'); tmp_rect.top = GetInt(str,L';'); tmp_rect.bottom = GetInt(str,L';');
            s.marginRect = tmp_rect;
            s.scrAlignment = GetInt(str,L';'); s.borderStyle = GetInt(str,L';');
            s.outlineWidthX = GetFloat(str,L';'); s.outlineWidthY = GetFloat(str,L';'); s.shadowDepthX = GetFloat(str,L';'); s.shadowDepthY = GetFloat(str,L';');
            for(size_t i = 0; i < 4; i++) s.colors[i] = (COLORREF)GetInt(str,L';');
            for(size_t i = 0; i < 4; i++) s.alpha[i] = GetInt(str,L';');
            s.charSet     = GetInt(str,L';');
            s.fontName    = GetStr(str,L';');   s.fontSize   = GetFloat(str,L';');
            s.fontScaleX  = GetFloat(str,L';'); s.fontScaleY = GetFloat(str,L';');
            s.fontSpacing = GetFloat(str,L';'); s.fontWeight = GetInt  (str,L';');
            s.fItalic     = !!GetInt(str,L';'); s.fUnderline = !!GetInt(str,L';'); s.fStrikeOut = !!GetInt(str,L';'); s.fBlur = GetFloat(str,L';'); s.fGaussianBlur = GetFloat(str,L';');
            s.fontAngleZ  = GetFloat(str,L';'); s.fontAngleX = GetFloat(str,L';'); s.fontAngleY = GetFloat(str,L';');
            s.relativeTo  = GetInt(str,L';');
        }
    }
    catch(...)
    {
        s.SetDefault();
    }

    return(s);
}
