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
#ifdef XY_VSFILTER_RUST_TEXT
#include "xy_rust_text.h"
#include "xy_rust_subrip.h"
#include "xy_rust_find_char.h"
#include "xy_rust_microdvd.h"
#include "xy_rust_microdvd_style.h"
#include "xy_rust_mpl2.h"
#include "xy_rust_remove_tags.h"
#include "xy_rust_case_replace.h"
#include "xy_rust_sami.h"
#include "xy_rust_sami_file.h"
#include "xy_rust_subrip_cue.h"
#include "xy_rust_pipe_line.h"
#include "xy_rust_encoding.h"
#include "xy_rust_ass_dialogue.h"
#include "xy_rust_ass_style.h"
#include "xy_rust_ass_headers.h"
#include "xy_rust_font_records.h"
#include "xy_rust_webvtt.h"
#include "xy_rust_fields.h"
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include "xy_rust_segments.h"
#include "xy_rust_segment_insert.h"
#include "xy_rust_timeline_search.h"
#endif
#endif
#include "STS.h"
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include "RustDocumentOwner.h"
#endif
#include <atlbase.h>

#include "RealTextParser.h"
#include <fstream>

#include "CAutoTiming.h"

#include <algorithm>
#include <vector>
#include "xy_logger.h"
#include <regex>

#include "..\dsutil\DSUtil.h"

// WebVTT helper RegexUtil from mpc-hc

namespace RegexUtil { //taken from SubtitlesProvidersUtils and extended for wstring
    static constexpr std::regex::flag_type RegexFlags(std::regex_constants::ECMAScript | std::regex_constants::icase | std::regex_constants::optimize);
    static constexpr std::wregex::flag_type WRegexFlags(std::regex_constants::ECMAScript | std::regex_constants::icase | std::regex_constants::optimize);

    using wregexResult = std::vector<std::wstring>;
    using wregexResults = std::vector<wregexResult>;

    size_t wstringMatch(const std::wstring& pattern, const std::wstring& text, wregexResults& results);
    size_t wstringMatch(const std::wstring& pattern, const std::wstring& text, wregexResult& result);
    size_t wstringMatch(const std::wregex& pattern, const std::wstring& text, wregexResults& results);
    size_t wstringMatch(const std::wregex& pattern, const std::wstring& text, wregexResult& result);

    using regexResult = std::vector<std::string>;
    using regexResults = std::vector<regexResult>;

    size_t stringMatch(const std::string& pattern, const std::string& text, regexResults& results);
    size_t stringMatch(const std::string& pattern, const std::string& text, regexResult& result);
    size_t stringMatch(const std::regex& pattern, const std::string& text, regexResults& results);
    size_t stringMatch(const std::regex& pattern, const std::string& text, regexResult& result);

};

size_t RegexUtil::stringMatch(const std::string& pattern, const std::string& text, regexResults& results) {
    std::regex regex_pattern(pattern, RegexFlags);
    return stringMatch(regex_pattern, text, results);
}

size_t RegexUtil::stringMatch(const std::regex& pattern, const std::string& text, regexResults& results) {
    results.clear();

    std::string data(text);
    std::smatch match_pieces;
    while (std::regex_search(data, match_pieces, pattern)) {
        regexResult result;
        for (auto match = match_pieces.begin(); match != match_pieces.end(); ++match) {
            if (match != match_pieces.begin()) {
                result.push_back((*match).str());
            }
        }
        results.push_back(result);
        data = match_pieces.suffix().str();
    }
    return results.size();
}

size_t RegexUtil::stringMatch(const std::string& pattern, const std::string& text, regexResult& result) {
    std::regex regex_pattern(pattern, RegexFlags);
    return stringMatch(regex_pattern, text, result);
}

size_t RegexUtil::stringMatch(const std::regex& pattern, const std::string& text, regexResult& result) {
    result.clear();

    std::smatch match_pieces;
    std::regex_search(text, match_pieces, pattern);
    for (const auto& match : match_pieces) {
        if (match != *match_pieces.begin()) {
            result.push_back(match.str());
        }
    }
    return result.size();
}

size_t RegexUtil::wstringMatch(const std::wstring& pattern, const std::wstring& text, wregexResults& results) {
    std::wregex regex_pattern(pattern, WRegexFlags);
    return wstringMatch(regex_pattern, text, results);
}

size_t RegexUtil::wstringMatch(const std::wregex& pattern, const std::wstring& text, wregexResults& results) {
    results.clear();

    std::wstring data(text);
    std::wsmatch match_pieces;
    while (std::regex_search(data, match_pieces, pattern)) {
        wregexResult result;
        for (auto match = match_pieces.begin(); match != match_pieces.end(); ++match) {
            if (match != match_pieces.begin()) {
                result.push_back((*match).str());
            }
        }
        results.push_back(result);
        data = match_pieces.suffix().str();
    }
    return results.size();
}

size_t RegexUtil::wstringMatch(const std::wstring& pattern, const std::wstring& text, wregexResult& result) {
    std::wregex regex_pattern(pattern, RegexFlags);
    return wstringMatch(regex_pattern, text, result);
}

size_t RegexUtil::wstringMatch(const std::wregex& pattern, const std::wstring& text, wregexResult& result) {
    result.clear();

    std::wsmatch match_pieces;
    std::regex_search(text, match_pieces, pattern);
    for (const auto& match : match_pieces) {
        if (match != *match_pieces.begin()) {
            result.push_back(match.str());
        }
    }
    return result.size();
}

// RegexUtil end

#if ENABLE_XY_LOG_TEXT_SUBTITLE
#  define TRACE_SUB(msg) XY_LOG_TRACE(msg)
#else
#  define TRACE_SUB(msg) __noop
#endif

#ifndef ReftimeToCString
#  define  ReftimeToCString(rt) ReftimeToString(rt).GetString()
#endif

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
    for(int i = 0; i < countof(htmlcolors); i++)
        SetAt(htmlcolors[i].name, htmlcolors[i].color);
}

CHtmlColorMap g_colors;

static CStringW SSAColorTag(CStringW arg, CStringW ctag = L"c") {
    DWORD val, color;
    if (g_colors.Lookup(CString(arg), val)) {
        color = (DWORD)val;
    }
    else if ((color = wcstol(arg, nullptr, 16)) == 0) {
        color = 0x00ffffff;    // default is white
    }
    CStringW tmp;
    tmp.Format(L"%02x%02x%02x", color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff);
    return CStringW(L"{\\" + ctag + L"&H") + tmp + L"&}";
}

static std::wstring SSAColorTagCS(std::wstring arg, CStringW ctag = L"c") {
    CStringW _arg(arg.c_str());
    return SSAColorTag(_arg, ctag).GetString();
}

CString g_default_style(_T("Default"));

//

BYTE CharSetList[] =
{
    ANSI_CHARSET,
    DEFAULT_CHARSET,
    SYMBOL_CHARSET,
    SHIFTJIS_CHARSET,
    HANGEUL_CHARSET,
    HANGUL_CHARSET,
    GB2312_CHARSET,
    CHINESEBIG5_CHARSET,
    OEM_CHARSET,
    JOHAB_CHARSET,
    HEBREW_CHARSET,
    ARABIC_CHARSET,
    GREEK_CHARSET,
    TURKISH_CHARSET,
    VIETNAMESE_CHARSET,
    THAI_CHARSET,
    EASTEUROPE_CHARSET,
    RUSSIAN_CHARSET,
    MAC_CHARSET,
    BALTIC_CHARSET
};

TCHAR* CharSetNames[] =
{
    _T("ANSI"),
    _T("DEFAULT"),
    _T("SYMBOL"),
    _T("SHIFTJIS"),
    _T("HANGEUL"),
    _T("HANGUL"),
    _T("GB2312"),
    _T("CHINESEBIG5"),
    _T("OEM"),
    _T("JOHAB"),
    _T("HEBREW"),
    _T("ARABIC"),
    _T("GREEK"),
    _T("TURKISH"),
    _T("VIETNAMESE"),
    _T("THAI"),
    _T("EASTEUROPE"),
    _T("RUSSIAN"),
    _T("MAC"),
    _T("BALTIC"),
};

int CharSetLen = countof(CharSetList);

static void LogSegments(const CAtlArray<STSSegment>& segments)
{
#if ENABLE_XY_LOG_TEXT_SUBTITLE
    for (int i=0, n=segments.GetCount();i<n;i++)
    {
        const STSSegment& s = segments[i];
        TRACE_SUB(_T("\tsegments ")<<i<<_T(":")<<s.start<<_T(" ")
            <<s.end<<_T(" ")<<s.subs.GetCount());
        TRACE_SUB(_T("\tsubs: "));
        for (int j=0, m=s.subs.GetCount();j<m;j++)
        {
            TRACE_SUB(_T("\t\t ")<<s.subs[j]);
        }
    }
#endif
}

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
static void LogStyles(const CSimpleTextSubtitle& document)
{
#if ENABLE_XY_LOG_TEXT_SUBTITLE
    TRACE_SUB("m_styles count:"<<document.GetStyleCount());
    for(size_t ordinal = 0; ; ordinal++)
    {
        CString name;
        if(!document.GetStyleNameAt(ordinal, name)) break;
        TRACE_SUB("\tm_styles["<<name<<"]");
    }
#endif
}
#else
static void LogStyles(const CSTSStyleMap& styles)
{
#if ENABLE_XY_LOG_TEXT_SUBTITLE
    TRACE_SUB("m_styles count:"<<styles.GetCount());

    for(POSITION pos=styles.GetStartPosition(); pos!=NULL;)
    {
        TRACE_SUB("\tm_styles["<<styles.GetNextKey(pos)<<"]");
    }
#endif
}
#endif
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
    if(fUnicode)
    {
#ifdef XY_VSFILTER_RUST_TEXT
        if(pos >= 0) return XyRust::FindUnicode(str, static_cast<uint16_t>(c), pos);
#endif
        return(str.Find(c, pos));
    }

#ifdef XY_VSFILTER_RUST_TEXT
    DWORD cp = CharSetToCodePage(CharSet);
    const int originalCharSet = CharSet;
    XyRust::FindChar search(static_cast<uint16_t>(c), pos, str.GetLength());
    bool reply = false;
    for(;;)
    {
        const unsigned action = search.Step(str, reply);
        const int value = search.Value();
        switch(action)
        {
        case 0: return value;
        case 1: reply = IsDBCSLeadByteEx(cp, static_cast<BYTE>(value)) != 0; break;
        case 2: CharSet = 0; break;
        case 3: reply = _istdigit(static_cast<WCHAR>(value)) != 0; break;
        case 4: CharSet = CharSet*10 + (value - '0'); break;
        case 5:
            if(value) CharSet = originalCharSet;
            cp = CharSetToCodePage(CharSet);
            break;
        }
    }
#else
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
#endif
}

static CStringW ToUnicode(CStringW str, DWORD CharSet)
{
#ifdef XY_VSFILTER_RUST_TEXT
    DWORD cp = CharSetToCodePage(CharSet);
    XyRust::Encoding convert(str);
    for(int unit; (unit = convert.Take()) >= 0; )
    {
        WCHAR wc = static_cast<WCHAR>(unit);
        char c = wc&0xff;
        if(IsDBCSLeadByteEx(cp, (BYTE)wc))
        {
            const int next = convert.Take();
            if(next >= 0)
            {
                char cc[2];
                cc[0] = c;
                cc[1] = static_cast<char>(next);
                MultiByteToWideChar(cp, 0, cc, 2, &wc, 1);
            }
        }
        else
        {
            MultiByteToWideChar(cp, 0, &c, 1, &wc, 1);
        }
        convert.AppendUnit(static_cast<uint16_t>(wc));
    }
    return convert.Text<CStringW>();
#else
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
#endif
}

static CStringW MBCSSSAToUnicode(CStringW str, int CharSet)
{
#ifdef XY_VSFILTER_RUST_TEXT
    const int OrgCharSet = CharSet;
    XyRust::SsaEncoding convert(str);
    int reply = 0;
    for(;;)
    {
        const unsigned action = convert.Step(reply);
        if(action == 0) return convert.Text<CStringW>(true);
        str = convert.Text<CStringW>();
        switch(action)
        {
        case 1: reply = FindChar(str, '{', 0, false, CharSet); break;
        case 2: reply = FindChar(str, '}', 0, false, CharSet); break;
        case 3:
            {
                const int k = str.Find(L"\\fe");
                if(k >= 0 && k < convert.Count()-1)
                {
                    CharSet = 0;
                    int l = k+3;
                    for(; _istdigit(str[l]); l++) CharSet = CharSet*10 + (str[l] - '0');
                    if(l == k+3) CharSet = OrgCharSet;
                }
            }
            break;
        case 4:
        case 5:
        case 6:
            {
                CStringW fragment;
                if(action == 6) fragment += ToUnicode(str, CharSet);
                else fragment += ToUnicode(str.Left(convert.Count()), action == 5 ? OrgCharSet : CharSet);
                convert.Append(fragment);
            }
            break;
        }
    }
#else
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
#endif
}

static CStringW RemoveSSATags(CStringW str, bool fUnicode, int CharSet)
{
#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::RemoveTags convert(str);
    if(convert.Handled())
    {
        int reply = 0;
        for(;;)
        {
            const unsigned action = convert.Step(reply);
            CStringW current = convert.Text<CStringW>();
            if(action == 0) return current;
            reply = FindChar(current, action == 1 ? '{' : '}', convert.Position(), fUnicode, CharSet);
        }
    }
#endif
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

static CStringW SubRipper2SSA(CStringW str, int CharSet)
{
#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::SubRip converter;
    CStringW converted;
    if (converter.Convert(str, converted)) return converted;
#endif
    str.Replace(L"<i>", L"{\\i1}");
    str.Replace(L"</i>", L"{\\i}");
    str.Replace(L"<b>", L"{\\b1}");
    str.Replace(L"</b>", L"{\\b}");
    str.Replace(L"<u>", L"{\\u1}");
    str.Replace(L"</u>", L"{\\u}");

    return(str);
}

static void WebVTTCueStrip(CStringW& str)
{
#ifdef XY_VSFILTER_RUST_TEXT
    const int p = XyRust::WebVTTCuePrefix(str);
    if (p > 0) {
        str.Delete(0, p);
        str.TrimLeft();
    }
#else
    int p = str.Find(L'\n');
    if (p > 0) {
        if (str.Left(6) == _T("align:") || str.Left(9) == _T("position:") || str.Left(9) == _T("vertical:") || str.Left(5) == _T("line:") || str.Left(5) == _T("size:")) {
            str.Delete(0, p);
            str.TrimLeft();
        }
    }
#endif
}

using WebVTTcolorData = struct _WebVTTcolorData { std::wstring color; std::wstring bg; bool applied = false; }; 
using WebVTTcolorMap = std::map<std::wstring, WebVTTcolorData>;

static void WebVTT2SSA(CStringW& str, CStringW& cueTags, WebVTTcolorMap clrMap)
{

    std::vector<WebVTTcolorData> styleStack;
    auto applyStyle = [&styleStack, &str](std::wstring clr, std::wstring bg, int endTag, bool restoring = false) {
        std::wstring tags = L"";
        WebVTTcolorData previous;
        bool applied = false;
        if (styleStack.size() > 0 && !restoring) {
            auto tmp = styleStack.back();
            if (tmp.applied) {
                previous = tmp;
            }
        }
        if (clr != L"" && clr != previous.color) {
            tags += SSAColorTagCS(clr);
        }
        if (bg != L"" && bg != previous.bg) {
            tags += SSAColorTagCS(bg, L"3c");
        }
        if (tags.length() > 0) {
            if (-1 == endTag) {
                str = tags.c_str() + str;
                applied = true;
            }
            else if (str.Mid(endTag + 1, 1) != "<") { //if we are about to open or close a tag, don't set the style yet, as it may change before formattable text arrives
                str = str.Left(endTag + 1) + tags.c_str() + str.Mid(endTag + 1);
                applied = true;
            }
        }
        if (!restoring) {
            styleStack.push_back({ clr, bg, applied }); //push current colors for restoring
        }
    };

    std::wstring clr = L"", bg = L"";
    if (clrMap.count(L"::cue")) { //default cue style
        WebVTTcolorData colorData = clrMap[L"::cue"];
        clr = colorData.color;
        bg = colorData.bg;
        applyStyle(clr, bg, -1);
    }

    int tagPos = str.Find(L"<");
    while (tagPos != std::wstring::npos) {
        int endTag = str.Find(L">", tagPos);
        if (endTag == std::wstring::npos) break;
        CStringW inner = str.Mid(tagPos + 1, endTag - tagPos - 1);
        if (inner.Find(L"/") == 0) { //close tag
            if (styleStack.size() > 0) {//should always be true, unless poorly matched close tags in source
                styleStack.pop_back();
            }
            if (styleStack.size() > 0) {
                auto restoreStyle = styleStack[styleStack.size() - 1];
                clr = restoreStyle.color;
                bg = restoreStyle.bg;
                applyStyle(clr, bg, endTag, true);
            }
            else { //reset default style
                if (endTag + 1 != str.GetLength()) {
                    str = str.Left(endTag + 1) + L"{\\r}" + str.Mid(endTag + 1);
                }
                clr = L"";
                bg = L"";
            }
            tagPos = str.Find(L"<", endTag);
            continue;
        }

        int dotPos = inner.Find(L".");
        if (dotPos == std::wstring::npos) {//it's a simple tag, so we can apply a single style to it, if it exists
            if (clrMap.count(inner.GetString())) {
                WebVTTcolorData colorData = clrMap[inner.GetString()];
                clr = colorData.color;
                bg = colorData.bg;
            }
        }
        else { //could find multiple classes 
            RegexUtil::wregexResults results;
            std::wregex clsPattern(LR"((\.?[^\.]+))");
            RegexUtil::wstringMatch(clsPattern, (const wchar_t*)inner, results);
            if (results.size() > 1) {
                std::wstring type = results[0][0];

                for (auto iter = results.begin() + 1; iter != results.end(); ++iter) { //loop through all classes--whichever is last gets precedence
                    std::wstring cls = (*iter)[0];
                    WebVTTcolorData colorData;
                    if (clrMap.count(type + cls)) {
                        colorData = clrMap[type + cls];
                    }
                    else if (clrMap.count(cls)) {
                        colorData = clrMap[cls];
                    }
                    if (colorData.color != L"") {
                        clr = colorData.color;
                    }
                    if (colorData.bg != L"") {
                        bg = colorData.bg;
                    }
                }
            }
        }

        applyStyle(clr, bg, endTag);
        tagPos = str.Find(L"<", endTag);
    }

    if (str.Find(L'<') >= 0) {
        str.Replace(L"<i>", L"{\\i1}");
        str.Replace(L"</i>", L"{\\i}");
        str.Replace(L"<b>", L"{\\b1}");
        str.Replace(L"</b>", L"{\\b}");
        str.Replace(L"<u>", L"{\\u1}");
        str.Replace(L"</u>", L"{\\u}");
    }

    if (str.Find(L'<') >= 0) {
        std::wstring stdTmp(str);

        // remove tags we don't support
        stdTmp = std::regex_replace(stdTmp, std::wregex(L"<c[.\\w\\d]*>"), L"");
        stdTmp = std::regex_replace(stdTmp, std::wregex(L"</c[.\\w\\d]*>"), L"");
        stdTmp = std::regex_replace(stdTmp, std::wregex(L"<\\d\\d:\\d\\d:\\d\\d.\\d\\d\\d>"), L"");
        stdTmp = std::regex_replace(stdTmp, std::wregex(L"<v[ .][^>]*>"), L"");
        stdTmp = std::regex_replace(stdTmp, std::wregex(L"</v>"), L"");
        stdTmp = std::regex_replace(stdTmp, std::wregex(L"<lang[^>]*>"), L"");
        stdTmp = std::regex_replace(stdTmp, std::wregex(L"</lang>"), L"");
        str = stdTmp.c_str();
    }
    if (str.Find(L'&') >= 0) {
        str.Replace(L"&lt;", L"<");
        str.Replace(L"&gt;", L">");
        str.Replace(L"&nbsp;", L"\\h");
        str.Replace(L"&lrm;", L"");
        str.Replace(L"&rlm;", L"");
        str.Replace(L"&amp;", L"&");
    }

    if (!cueTags.IsEmpty()) {
        std::wstring stdTmp(cueTags);
        std::wregex alignRegex(L"align:(start|left|center|middle|end|right)");
        std::wsmatch match;

        if (std::regex_search(stdTmp, match, alignRegex)) {
            if (match[1] == L"start" || match[1] == L"left") {
                str = L"{\\an1}" + str;
            }
            else if (match[1] == L"center" || match[1] == L"middle") {
                str = L"{\\an2}" + str;
            }
            else {
                str = L"{\\an3}" + str;
            }
        }
    }
}

static void WebVTT2SSA(CStringW& str) {
    CStringW discard;
    WebVTTcolorMap discardMap;
    WebVTT2SSA(str, discard, discardMap);
}

static bool OpenVTT(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet) {
    CStringW buff, start, end, cueTags;

    file->ReadString(buff);
    if (buff.Left(6).Compare(L"WEBVTT") != 0) {
        return false;
    }

    auto readTimeCode = [](LPCWSTR str, int& hh, int& mm, int& ss, int& ms) {
        WCHAR sep;
        int c = swscanf_s(str, L"%d%c%d%c%d%c%d",
            &hh, &sep, 1, &mm, &sep, 1, &ss, &sep, 1, &ms);
        if (c == 5) {
            // Hours value is absent, shift read values
            ms = ss;
            ss = mm;
            mm = hh;
            hh = 0;
        }
        return (c == 5 || c == 7);
    };


    //default cue color classes: https://w3c.github.io/webvtt/#default-text-color
    WebVTTcolorMap cueColors = {
        {L".white", WebVTTcolorData({L"ffffff", L""})},
        {L".lime", WebVTTcolorData({L"00ff00", L""})},
        {L".cyan", WebVTTcolorData({L"00ffff", L""})},
        {L".red", WebVTTcolorData({L"ff0000", L""})},
        {L".yellow", WebVTTcolorData({L"ffff00", L""})},
        {L".magenta", WebVTTcolorData({L"ff00ff", L""})},
        {L".blue", WebVTTcolorData({L"0000ff", L""})},
        {L".black", WebVTTcolorData({L"000000", L""})},
        {L".bg_white", WebVTTcolorData({L"", L"ffffff"})},
        {L".bg_lime", WebVTTcolorData({L"", L"00ff00"})},
        {L".bg_cyan", WebVTTcolorData({L"", L"00ffff"})},
        {L".bg_red", WebVTTcolorData({L"", L"ff0000"})},
        {L".bg_yellow", WebVTTcolorData({L"", L"ffff00"})},
        {L".bg_magenta", WebVTTcolorData({L"", L"ff00ff"})},
        {L".bg_blue", WebVTTcolorData({L"", L"0000ff"})},
        {L".bg_black", WebVTTcolorData({L"", L"000000"})},
    };

    auto parseStyle = [&file, &cueColors](CStringW& buff) {
        CStringW styleStr = L"";
        while (file->ReadString(buff)) {
            if (buff.Find(L"-->") != -1) { //not allowed in style block, so we drop out to cue parsing below
                FastTrimRight(buff);
                break;
            }
            if (buff.IsEmpty()) { //empty line not allowed in style block, drop out
                break;
            }
            styleStr += L" " + buff;
        }

        int startComment = styleStr.Find(L"/*");
        while (startComment != -1) { //remove comments
            int endComment = styleStr.Find(L"*/", startComment + 2);
            if (endComment == -1) {
                endComment = styleStr.GetLength() - 1;
            }
            styleStr.Delete(startComment, endComment - startComment + 1);
            startComment = styleStr.Find(L"/*");
        }

        if (!styleStr.IsEmpty()) {
            auto parseColor = [](std::wstring styles, std::wstring attr = L"color") {
                //we only support color styles for now
                std::wregex clrPat(attr + LR"(\s*:\s*#?([a-zA-Z0-9]*)\s*;)"); //e.g., 0xffffff or white
                std::wregex rgbPat(attr + LR"(\s*:\s*rgb\s*\(\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\)\s*;)");
                std::wsmatch match;
                std::wstring clrStr = L"";
                if (std::regex_search(styles, match, clrPat)) {
                    clrStr = match[1];
                }
                else if (std::regex_search(styles, match, rgbPat)) {
                    int r = stoi(match[1]) & 0xff;
                    int g = stoi(match[2]) & 0xff;
                    int b = stoi(match[3]) & 0xff;
                    DWORD clr = (r << 16) + (g << 8) + b;
                    std::wstringstream hexClr;
                    hexClr << std::hex << clr;
                    clrStr = hexClr.str();
                }
                return clrStr;
            };

            RegexUtil::wregexResults results;
            std::wregex cueDefPattern(LR"(::cue\s*\{([^}]*)\})"); //default cue style
            RegexUtil::wstringMatch(cueDefPattern, (const wchar_t*)styleStr, results);
            if (results.size() > 0) {
                auto iter = results[results.size() - 1];
                std::wstring clr, bgClr;
                clr = parseColor(iter[0]);
                bgClr = parseColor(iter[0], L"background");
                if (clr != L"" || bgClr != L"") {
                    cueColors[L"::cue"] = WebVTTcolorData({ clr, bgClr });
                }
            }

            std::wregex cuePattern(LR"(::cue\(([^)]+)\)\s*\{([^}]*)\})");
            RegexUtil::wstringMatch(cuePattern, (const wchar_t*)styleStr, results);
            for (const auto& iter : results) {
                std::wstring clr, bgClr;
                clr = parseColor(iter[1]);
                bgClr = parseColor(iter[1], L"background");
                if (clr != L"" || bgClr != L"") {
                    cueColors[iter[0]] = WebVTTcolorData({ clr, bgClr });
                }
            }
        }
    };

    CStringW lastStr, lastBuff;
    bool foundFirstCue = false;
    while (file->ReadString(buff)) {
        FastTrimRight(buff);
        if (!foundFirstCue && !buff.IsEmpty()) { //STYLE blocks cannot show up after cues begin
            if (buff == L"STYLE" || buff == L"Style:" /*have seen webvtt with incorrect format using 'Style:' instead of 'STYLE'*/) {
                parseStyle(buff); //note that buff will contain next line when done, so we can still use it below
            }
        }
        if (buff.IsEmpty()) {
            continue;
        }

        int len = buff.GetLength();
        cueTags = L"";
        int c = swscanf_s(buff, L"%s --> %s %[^\n]s", start.GetBuffer(len), len, end.GetBuffer(len), len, cueTags.GetBuffer(len), len);
        start.ReleaseBuffer();
        end.ReleaseBuffer();
        cueTags.ReleaseBuffer();

        int hh1, mm1, ss1, ms1, hh2, mm2, ss2, ms2;

        if ((c == 2 || c == 3) //either start/end or start/end/cuetags
            && readTimeCode(start, hh1, mm1, ss1, ms1)
            && readTimeCode(end, hh2, mm2, ss2, ms2)) {
            foundFirstCue = true;

            CStringW str, tmp;

            while (file->ReadString(tmp)) {
                FastTrimRight(tmp);
                if (tmp.IsEmpty()) {
                    break;
                }
                WebVTT2SSA(tmp, cueTags, cueColors);
                str += tmp + '\n';
            }

            if (lastStr != str || lastBuff != buff) { //discard repeated subs
                ret.Add(str,
                    file->IsUnicode(),
                    MS2RT((((hh1 * 60i64 + mm1) * 60i64) + ss1) * 1000i64 + ms1),
                    MS2RT((((hh2 * 60i64 + mm2) * 60i64) + ss2) * 1000i64 + ms2));
            }

            lastStr = str;
            lastBuff = buff;
        } else {
            continue;
        }
    }

    return !ret.IsEmpty();
}


static bool OpenSubRipper(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    CStringW buff;
    while(file->ReadString(buff)) {
        FastTrim(buff);
        if (buff.IsEmpty()) {
            continue;
        }

        WCHAR sep;
        int num = 0; // This one isn't really used just assigned a new value
        int hh1, mm1, ss1, ms1, hh2, mm2, ss2, ms2;
        WCHAR msStr1[5] = {0}, msStr2[5] = {0};
        int c = swscanf_s(buff, L"%d%c%d%c%d%4[^-] --> %d%c%d%c%d%4s\n",
                          &hh1, &sep, 1, &mm1, &sep, 1, &ss1, msStr1, (unsigned int)_countof(msStr1),
                          &hh2, &sep, 1, &mm2, &sep, 1, &ss2, msStr2, (unsigned int)_countof(msStr2));

        if (c == 1) { // numbering
            num = hh1;
        } else if (c >= 11) { // time info
            // Parse ms if present
            if (2 != swscanf_s(msStr1, L"%c%d", &sep, 1, &ms1)) {
                ms1 = 0;
            }
            if (2 != swscanf_s(msStr2, L"%c%d", &sep, 1, &ms2)) {
                ms2 = 0;
            }

#ifdef XY_VSFILTER_RUST_TEXT
            CStringW tmp;
            XyRust::SubRipCue cue;
            while(true) {
                bool fEOF = !file->ReadString(tmp);
                if (fEOF && tmp.IsEmpty()) break;
                FastTrim(tmp);
                if (tmp.IsEmpty()) cue.EmptyLine();
                int num2;
                WCHAR wc;
                if (cue.Boundary(swscanf_s(tmp, L"%d%c", &num2, &wc, 1) == 1)) {
                    num = num2;
                    break;
                }
                CStringW fragment;
                fragment += tmp + '\n';
                cue.Append(fragment);
                if (fEOF) break;
            }
            CStringW str = cue.Text<CStringW>();
#else
            CStringW str, tmp;

            bool fFoundEmpty = false;

            while(true) {
                bool fEOF = !file->ReadString(tmp);
                if (fEOF && tmp.IsEmpty()) break;

                FastTrim(tmp);
                if (tmp.IsEmpty()) {
                    fFoundEmpty = true;
                }

                int num2;
                WCHAR wc;
                if (swscanf_s(tmp, L"%d%c", &num2, &wc, 1) == 1 && fFoundEmpty) {
                    num = num2;
                    break;
                }

                str += tmp + '\n';

                if(fEOF) break;
            }

#endif

            ret.Add(
                SubRipper2SSA(str, CharSet),
                file->IsUnicode(),
                MS2RT((((hh1 * 60i64 + mm1) * 60i64) + ss1) * 1000i64 + ms1),
                MS2RT((((hh2 * 60i64 + mm2) * 60i64) + ss2) * 1000i64 + ms2));
        } else if (c != EOF) { // might be another format
            return false;
        }
    }

    return !ret.IsEmpty();
}

static bool OpenOldSubRipper(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    CStringW buff;
    while(file->ReadString(buff))
    {
        FastTrim(buff);
        if(buff.IsEmpty()) continue;

#ifdef XY_VSFILTER_RUST_TEXT
        XyRust::PipeLine line(buff);
        for(int i = line.Position(); i >= 0; i = line.Position())
        {
            CStringW source = line.Text<CStringW>();
            line.Found(FindChar(source, '|', i, file->IsUnicode(), CharSet));
        }
        buff = line.Text<CStringW>();
#else
        for(int i = 0; i < buff.GetLength(); i++)
        {
            if((i = FindChar(buff, '|', i, file->IsUnicode(), CharSet)) < 0) break;
            buff.SetAt(i, '\n');
        }
#endif

        int hh1, mm1, ss1, hh2, mm2, ss2;
        int c = swscanf_s(buff, L"{%d:%d:%d}{%d:%d:%d}", &hh1, &mm1, &ss1, &hh2, &mm2, &ss2);

        if(c == 6)
        {
            ret.Add(
                buff.Mid(buff.Find('}', buff.Find('}')+1)+1),
                file->IsUnicode(),
                MS2RT((((hh1 * 60i64 + mm1) * 60i64) + ss1) * 1000i64),
                MS2RT((((hh2 * 60i64 + mm2) * 60i64) + ss2) * 1000i64));
        }
        else if(c != EOF) // might be another format
        {
            return(false);
        }
    }

    return(!ret.IsEmpty());
}

static bool OpenSubViewer(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    STSStyle def;
    CStringW font, color, size;
    bool fBold, fItalic, fStriked, fUnderline;

    CStringW buff;
    while(file->ReadString(buff))
    {
        FastTrim(buff);
        if(buff.IsEmpty()) continue;

        if(buff[0] == '[')
        {
            for(int i = 0; i < buff.GetLength() && buff[i]== '['; )
            {
                int j = buff.Find(']', ++i);
                if(j < i) break;

                CStringW tag = buff.Mid(i,j-i);
                FastTrim(tag);
                tag.MakeLower();

                i += j-i;

                j = buff.Find('[', ++i);
                if(j < 0) j = buff.GetLength();

                CStringW param = buff.Mid(i,j-i);
                param.Trim(L" \\t,");

                i = j;

                if(tag == L"font")
                    font = def.fontName.CompareNoCase(param) ? param : L"";
                else if(tag == L"colf")
                    color = def.colors[0] != wcstol(((LPCWSTR)param)+2, 0, 16) ? param : L"";
                else if(tag == L"size")
                    size = def.fontSize != wcstol(param, 0, 10) ? param : L"";
                else if(tag == L"style")
                {
                    if(param.Find(L"no") >= 0)
                    {
                        fBold = fItalic = fStriked = fUnderline = false;
                    }
                    else
                    {
                        fBold = def.fontWeight < FW_BOLD && param.Find(L"bd") >= 0;
                        fItalic = def.fItalic && param.Find(L"it") >= 0;
                        fStriked = def.fStrikeOut && param.Find(L"st") >= 0;
                        fUnderline = def.fUnderline && param.Find(L"ud") >= 0;
                    }
                }
            }

            continue;
        }

        WCHAR sep;
        int hh1, mm1, ss1, hs1, hh2, mm2, ss2, hs2;
        int c = swscanf_s(buff, L"%d:%d:%d%c%d,%d:%d:%d%c%d\n",
            &hh1, &mm1, &ss1, &sep, 1, &hs1, &hh2, &mm2, &ss2, &sep, 1, &hs2);

        if(c == 10)
        {
            CStringW str;
            VERIFY(file->ReadString(str));

            str.Replace(L"[br]", L"\\N");

            CStringW prefix;
            if(!font.IsEmpty()) prefix += L"\\fn" + font;
            if(!color.IsEmpty()) prefix += L"\\c" + color;
            if(!size.IsEmpty()) prefix += L"\\fs" + size;
            if(fBold) prefix += L"\\b1";
            if(fItalic) prefix += L"\\i1";
            if(fStriked) prefix += L"\\s1";
            if(fUnderline) prefix += L"\\u1";
            if(!prefix.IsEmpty()) str = L"{" + prefix + L"}" + str;

            ret.Add(str,
                file->IsUnicode(),
                MS2RT((((hh1 * 60i64 + mm1) * 60i64) + ss1) * 1000i64 + hs1 * 10i64),
                MS2RT((((hh2 * 60i64 + mm2) * 60i64) + ss2) * 1000i64 + hs2 * 10i64));
        }
        else if(c != EOF) // might be another format
        {
            return(false);
        }
    }

    return(!ret.IsEmpty());
}

static STSStyle* GetMicroDVDStyle(CString str, int CharSet)
{
    STSStyle* ret = DEBUG_NEW STSStyle();
    if(!ret) return(NULL);

#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::MicroDvdStyle tags(str);
    while(tags.Next())
    {
        CString code = tags.Text<CString>();
#else
    for(int i = 0, len = str.GetLength(); i < len; i++)
    {
        int j = str.Find('{', i);
        if(j < 0) j = len;

        if(j >= len) break;

        int k = str.Find('}', j);
        if(k < 0) k = len;

        CString code = str.Mid(j, k-j);
#endif
        if(code.GetLength() > 2) code.SetAt(1, (TCHAR)towlower(code[1]));

        if(!_tcsnicmp(code, _T("{c:$"), 4))
        {
            _stscanf_s(code, _T("{c:$%x"), &ret->colors[0]);
        }
        else if(!_tcsnicmp(code, _T("{f:"), 3))
        {
            ret->fontName = code.Mid(3);
        }
        else if(!_tcsnicmp(code, _T("{s:"), 3))
        {
            float f;
            if(1 == _stscanf_s(code, _T("{s:%f"), &f))
                ret->fontSize = f;
        }
        else if(!_tcsnicmp(code, _T("{h:"), 3))
        {
            _stscanf_s(code, _T("{h:%d"), &ret->charSet);
        }
        else if(!_tcsnicmp(code, _T("{y:"), 3))
        {
            code.MakeLower();
            if(code.Find('b') >= 0) ret->fontWeight = FW_BOLD;
            if(code.Find('i') >= 0) ret->fItalic = true;
            if(code.Find('u') >= 0) ret->fUnderline = true;
            if(code.Find('s') >= 0) ret->fStrikeOut = true;
        }
        else if(!_tcsnicmp(code, _T("{p:"), 3))
        {
            int p;
            _stscanf_s(code, _T("{p:%d"), &p);
            ret->scrAlignment = (p == 0) ? 8 : 2;
        }

#ifndef XY_VSFILTER_RUST_TEXT
        i = k;
#endif
    }

    return(ret);
}

static CStringW MicroDVD2SSA(CStringW str, bool fUnicode, int CharSet)
{
#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::MicroDVD convert(str.GetLength());
    CStringW line;
    int reply = 0;
    for(;;)
    {
        const unsigned action = convert.Step(reply);
        const int start = convert.Field(0), count = convert.Field(1);
        switch(action)
        {
        case 0: return convert.Output<CStringW>();
        case 1: reply = FindChar(str, '|', start, fUnicode, CharSet); break;
        case 2: line = str.Mid(start, count); reply = line.GetLength(); break;
        case 3: reply = FindChar(line, '{', start, fUnicode, CharSet); break;
        case 4:
            {
                // Retain CString's fragment-append behavior at embedded NULs.
                CStringW literal;
                literal += line.Mid(start, count);
                convert.Append(literal, convert.Field(2));
            }
            break;
        case 5: reply = FindChar(line, '}', start, fUnicode, CharSet); break;
        case 6:
            {
                CStringW code = line.Mid(start, count), ret;
                enum {COLOR=0, FONTNAME, FONTSIZE, FONTCHARSET, BOLD, ITALIC, UNDERLINE, STRIKEOUT};
                bool fRestore[8];
                const unsigned previous = convert.Field(2);
                for(int bit=0; bit<8; ++bit) fRestore[bit] = (previous & (1U<<bit)) != 0;
                if(!_wcsnicmp(code, L"{c:$", 4))
                {
                    fRestore[COLOR] = (iswupper(code[1]) == 0);
                    code.MakeLower();

                    int color;
                    swscanf_s(code, L"{c:$%x", &color);
                    code.Format(L"{\\c&H%x&}", color);
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{f:", 3))
                {
                    fRestore[FONTNAME] = (iswupper(code[1]) == 0);

                    code.Format(L"{\\fn%s}", code.Mid(3));
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{s:", 3))
                {
                    fRestore[FONTSIZE] = (iswupper(code[1]) == 0);
                    code.MakeLower();

                    double size;
                    swscanf_s(code, L"{s:%lf", &size);
                    code.Format(L"{\\fs%f}", size);
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{h:", 3))
                {
                    fRestore[COLOR] = (_istupper(code[1]) == 0);
                    code.MakeLower();

                    int iCharSet;
                    swscanf_s(code, L"{h:%d", &iCharSet);
                    code.Format(L"{\\fe%d}", iCharSet);
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{y:", 3))
                {
                    bool f = (_istupper(code[1]) == 0);

                    code.MakeLower();

                    ret += '{';
                    if(code.Find('b') >= 0) {ret += L"\\b1"; fRestore[BOLD] = f;}
                    if(code.Find('i') >= 0) {ret += L"\\i1"; fRestore[ITALIC] = f;}
                    if(code.Find('u') >= 0) {ret += L"\\u1"; fRestore[UNDERLINE] = f;}
                    if(code.Find('s') >= 0) {ret += L"\\s1"; fRestore[STRIKEOUT] = f;}
                    ret += '}';
                }
                else if(!_wcsnicmp(code, L"{o:", 3))
                {
                    code.MakeLower();

                    int x, y;
                    TCHAR c;
                    swscanf_s(code, L"{o:%d%c%d", &x, &c, 1, &y);
                    code.Format(L"{\\move(%d,%d,0,0,0,0)}", x, y);
                    ret += code;
                }
                else ret += code;
                unsigned restore = 0;
                for(int bit=0; bit<8; ++bit) if(fRestore[bit]) restore |= 1U<<bit;
                convert.Append(ret, restore);
            }
            break;
        }
    }
#else
    CStringW ret;

    enum {COLOR=0, FONTNAME, FONTSIZE, FONTCHARSET, BOLD, ITALIC, UNDERLINE, STRIKEOUT};
    bool fRestore[8];
    int fRestoreLen = 8;
    memset(fRestore, 0, sizeof(bool)*fRestoreLen);

    for(int pos = 0, eol; pos < str.GetLength(); pos++)
    {
        if((eol = FindChar(str, '|', pos, fUnicode, CharSet)) < 0) eol = str.GetLength();

        CStringW line = str.Mid(pos, eol-pos);

        pos = eol;

        for(int i = 0, j, k, len = line.GetLength(); i < len; i++)
        {
            if((j = FindChar(line, '{', i, fUnicode, CharSet)) < 0) j = str.GetLength();

            ret += line.Mid(i, j-i);

            if(j >= len) break;

            if((k = FindChar(line, '}', j, fUnicode, CharSet)) < 0) k = len;

            {
                CStringW code = line.Mid(j, k-j);

                if(!_wcsnicmp(code, L"{c:$", 4))
                {
                    fRestore[COLOR] = (iswupper(code[1]) == 0);
                    code.MakeLower();

                    int color;
                    swscanf_s(code, L"{c:$%x", &color);
                    code.Format(L"{\\c&H%x&}", color);
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{f:", 3))
                {
                    fRestore[FONTNAME] = (iswupper(code[1]) == 0);

                    code.Format(L"{\\fn%s}", code.Mid(3));
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{s:", 3))
                {
                    fRestore[FONTSIZE] = (iswupper(code[1]) == 0);
                    code.MakeLower();

                    double size;
                    swscanf_s(code, L"{s:%lf", &size);
                    code.Format(L"{\\fs%f}", size);
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{h:", 3))
                {
                    fRestore[COLOR] = (_istupper(code[1]) == 0);
                    code.MakeLower();

                    int iCharSet;
                    swscanf_s(code, L"{h:%d", &iCharSet);
                    code.Format(L"{\\fe%d}", iCharSet);
                    ret += code;
                }
                else if(!_wcsnicmp(code, L"{y:", 3))
                {
                    bool f = (_istupper(code[1]) == 0);

                    code.MakeLower();

                    ret += '{';
                    if(code.Find('b') >= 0) {ret += L"\\b1"; fRestore[BOLD] = f;}
                    if(code.Find('i') >= 0) {ret += L"\\i1"; fRestore[ITALIC] = f;}
                    if(code.Find('u') >= 0) {ret += L"\\u1"; fRestore[UNDERLINE] = f;}
                    if(code.Find('s') >= 0) {ret += L"\\s1"; fRestore[STRIKEOUT] = f;}
                    ret += '}';
                }
                else if(!_wcsnicmp(code, L"{o:", 3))
                {
                    code.MakeLower();

                    int x, y;
                    TCHAR c;
                    swscanf_s(code, L"{o:%d%c%d", &x, &c, 1, &y);
                    code.Format(L"{\\move(%d,%d,0,0,0,0)}", x, y);
                    ret += code;
                }
                else ret += code;
            }

            i = k;
        }

        if(pos >= str.GetLength()) break;

        for(int i = 0; i < fRestoreLen; i++)
        {
            if(fRestore[i])
            {
                switch(i)
                {
                case COLOR: ret += L"{\\c}"; break;
                case FONTNAME: ret += L"{\\fn}"; break;
                case FONTSIZE: ret += L"{\\fs}"; break;
                case FONTCHARSET: ret += L"{\\fe}"; break;
                case BOLD: ret += L"{\\b}"; break;
                case ITALIC: ret += L"{\\i}"; break;
                case UNDERLINE: ret += L"{\\u}"; break;
                case STRIKEOUT: ret += L"{\\s}"; break;
                default: break;
                }
            }
        }

        ZeroMemory(fRestore, sizeof(bool)*fRestoreLen);

        ret += L"\\N";
    }

    return(ret);
#endif
}

static bool OpenMicroDVD(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    bool fCheck = false, fCheck2 = false;

    CString style(_T("Default"));

    CStringW buff;;
    while(file->ReadString(buff))
    {
        FastTrim(buff);
        if(buff.IsEmpty()) continue;

        int start, end;
        int c = swscanf_s(buff, L"{%d}{%d}", &start, &end);

        if(c != 2) {c = swscanf_s(buff, L"{%d}{}", &start)+1; end = start + 60; fCheck = true;}

        if(c != 2)
        {
            int i;
            if(buff.Find('{') == 0 && (i = buff.Find('}')) > 1 && i < buff.GetLength())
            {
                if(STSStyle* s = GetMicroDVDStyle(buff.Mid(i+1), CharSet))
                {
                    style = buff.Mid(1, i-1);
                    style.MakeUpper();
                    if(style.GetLength()) {CString str = style.Mid(1); str.MakeLower(); style = style.Left(1) + str;}
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                    const int nextCharSet = s->charSet;
                    ret.AddStyle(style, s);
                    CharSet = nextCharSet;
#else
                    ret.AddStyle(style, s);
                    CharSet = s->charSet;
#endif
                    continue;
                }
            }
        }

        if(c == 2)
        {
            if(fCheck2 && !ret.IsEmpty())
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.m_document->CorrectLastEnd(MS2RT(start));
#else
                STSEntry& stse = ret.m_entries[ret.GetEntryCount()-1];
                stse.end = min(stse.end, MS2RT(start));
#endif
                fCheck2 = false;
            }

            ret.Add(
                MicroDVD2SSA(buff.Mid(buff.Find('}', buff.Find('}')+1)+1), file->IsUnicode(), CharSet),
                file->IsUnicode(),
                MS2RT(start), MS2RT(end),
                style);

            if(fCheck)
            {
                fCheck = false;
                fCheck2 = true;
            }
        }
        else if(c != EOF) // might be another format
        {
            return(false);
        }
    }

    return(!ret.IsEmpty());
}

static void ReplaceNoCase(CStringW& str, CStringW from, CStringW to)
{
    CStringW lstr = str;
    lstr.MakeLower();
#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::CaseReplace convert;
    if(convert.Convert(str, lstr, from, to)) return;
#endif

    int i, j, k;

    for(i = 0, j = str.GetLength(); i < j; )
    {
        if((k = lstr.Find(from, i)) >= 0)
        {
            str.Delete(k, from.GetLength()); lstr.Delete(k, from.GetLength());
            str.Insert(k, to); lstr.Insert(k, to);
            i = k + to.GetLength();
            j = str.GetLength();
        }
        else break;
    }
}

static CStringW SMI2SSA(CStringW str, int CharSet)
{
    ReplaceNoCase(str, L"&nbsp;", L" ");
    ReplaceNoCase(str, L"&quot;", L"\"");
    ReplaceNoCase(str, L"<br>", L"\\N");
    ReplaceNoCase(str, L"<i>", L"{\\i1}");
    ReplaceNoCase(str, L"</i>", L"{\\i}");
    ReplaceNoCase(str, L"<b>", L"{\\b1}");
    ReplaceNoCase(str, L"</b>", L"{\\b}");

    CStringW lstr = str;
    lstr.MakeLower();
#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::Sami convert(str, lstr);
    if(convert.Handled())
    {
        while(convert.Step() != 0)
        {
            XyRust::SamiArguments arguments(convert.Text<CStringW>(1));
            CStringW arg;
            arg.TrimLeft(); arg.TrimRight(L" >");
            for (;;)
            {
                CStringW args = arguments.Text<CStringW>(0);
                args.TrimLeft();
                const unsigned action = arguments.Accept(args);
                if(action == 0) break;
                if(action == 2)
                {
                    arg = arguments.Text<CStringW>(1);
                    DWORD color;

                    DWORD val;
                    if(g_colors.Lookup(CString(arg), val))
                        color = (DWORD)val;
                    else if((color = wcstol(arg, NULL, 16) ) == 0)
                        color = 0x00ffffff;  // default is white

                    arg.Format(L"%02x%02x%02x", color&0xff, (color>>8)&0xff, (color>>16)&0xff);
                    convert.Insert(CStringW(L"{\\c&H") + arg + L"&}", 5 + arg.GetLength() + 2);
                }
            }
        }
        return convert.Text<CStringW>(0);
    }
#endif

    // maven@maven.de
    // now parse line
    for(int i = 0, j = str.GetLength(); i < j; )
    {
        int k;
        if((k = lstr.Find('<', i)) < 0) break;

        int chars_inserted = 0;

        int l = 1;
        for(; k+l < j && lstr[k+l] != '>'; l++);
        l++;

// Modified by Cookie Monster
        if (lstr.Find(L"<font ", k) == k)
        {
            CStringW args = lstr.Mid(k+6, l-6); // delete "<font "
            CStringW arg ;

            args.Remove('\"'); args.Remove('#');    // may include 2 * " + #
            arg.TrimLeft(); arg.TrimRight(L" >");

            for (;;)
            {
                args.TrimLeft();
                arg = args.SpanExcluding(L" \t>");
                args = args.Mid(arg.GetLength());

                if(arg.IsEmpty())
                    break;
                if (arg.Find(L"color=") == 0 )
                {
                    DWORD color;

                    arg = arg.Mid(6);   // delete "color="
                    if ( arg.IsEmpty())
                        continue;

                    DWORD val;
                    if(g_colors.Lookup(CString(arg), val))
                        color = (DWORD)val;
                    else if((color = wcstol(arg, NULL, 16) ) == 0)
                        color = 0x00ffffff;  // default is white

                    arg.Format(L"%02x%02x%02x", color&0xff, (color>>8)&0xff, (color>>16)&0xff);
                    lstr.Insert(k + l + chars_inserted, CStringW(L"{\\c&H") + arg + L"&}");
                    str.Insert(k + l + chars_inserted, CStringW(L"{\\c&H") + arg + L"&}");
                    chars_inserted += 5 + arg.GetLength() + 2;
                }
/*
                else if (arg.Find(_T("size=" )) == 0 )
                {
                    uint fsize;

                    arg = arg.Mid(5);   // delete "size="
                    if ( arg.GetLength() == 0)
                        continue;

                    if ( fsize = _tcstol(arg, &tmp, 10) == 0 )
                        continue;

                    lstr.Insert(k + l + chars_inserted, CString(_T("{\\fs")) + arg + _T("&}"));
                    str.Insert(k + l + chars_inserted, CString(_T("{\\fs")) + arg + _T("&}"));
                    chars_inserted += 4 + arg.GetLength() + 2;
                }
*/
            }
        }

// Original Code
/*
        if (lstr.Find(L"<font color=", k) == k)
        {
            CStringW arg = lstr.Mid(k+12, l-12); // may include 2 * " + #

            arg.Remove('\"');
            arg.Remove('#');
            arg.TrimLeft(); arg.TrimRight(L" >");

            if(arg.GetLength() > 0)
            {
                DWORD color;

                CString key = arg;
                void* val;
                if(g_colors.Lookup(key, val)) color = (DWORD)val;
                else color = wcstol(arg, NULL, 16);

                arg.Format(L"%02x%02x%02x", color&0xff, (color>>8)&0xff, (color>>16)&0xff);
            }

            lstr.Insert(k + l + chars_inserted, L"{\\c&H" + arg + L"&}");
            str.Insert(k + l + chars_inserted, L"{\\c&H" + arg + L"&}");
            chars_inserted += 5 + arg.GetLength() + 2;
        }
*/
        else if (lstr.Find(L"</font>", k) == k)
        {
            lstr.Insert(k + l + chars_inserted, L"{\\c}");
            str.Insert(k + l + chars_inserted, L"{\\c}");
            chars_inserted += 4;
        }

        str.Delete(k, l); lstr.Delete(k, l);
        i = k + chars_inserted;
        j = str.GetLength();
    }

    return(str);
}

static bool OpenSami(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    CStringW buff, caption;

    ULONGLONG pos = file->GetPosition();

    bool fSAMI = false;

    while(file->ReadString(buff) && !fSAMI)
    {
        if(buff.MakeUpper().Find(L"<SAMI>") >= 0) fSAMI = true;
    }

    if(!fSAMI) return(false);

    file->Seek(pos, CFile::begin);

#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::SamiFile state;
    while(file->ReadString(buff))
    {
        FastTrim(buff);
        if(buff.IsEmpty()) continue;
        CStringW ubuff = buff;
        ubuff.MakeUpper();
        const unsigned action = state.Begin(ubuff);
        if(action != 0)
        {
            if(action == 2)
            {
                int time = 0;
                for(int i = 12; i < ubuff.GetLength(); i++)
                {
                    if(ubuff[i] != '>' && ubuff[i] != 'M')
                    {
                        if(iswdigit(ubuff[i]))
                        {
                            time *= 10;
                            time += ubuff[i] - 0x30;
                        }
                    }
                    else break;
                }
                caption = state.Text<CStringW>();
                const int start_time = state.Start();
                ret.Add(SMI2SSA(caption, CharSet), file->IsUnicode(), MS2RT(start_time), MS2RT(time));
                state.Flushed(time);
            }
            CStringW fragment;
            fragment += buff;
            state.Append(fragment);
        }
        state.End();
    }
    caption = state.Text<CStringW>();
    const int start_time = state.Start();
    ret.Add(SMI2SSA(caption, CharSet), file->IsUnicode(), MS2RT(start_time), LONGLONG_MAX);

#else
    bool fComment = false;

    int start_time = 0;

    while(file->ReadString(buff))
    {
        FastTrim(buff);
        if(buff.IsEmpty()) continue;

        CStringW ubuff = buff;
        ubuff.MakeUpper();

        if(ubuff.Find(L"<!--") >= 0 || ubuff.Find(L"<TITLE>") >= 0)
            fComment = true;

        if(!fComment)
        {
            int i;

            if((i = ubuff.Find(L"<SYNC START=")) >= 0)
            {
                int time = 0;

                for(i = 12; i < ubuff.GetLength(); i++)
                {
                    if(ubuff[i] != '>' && ubuff[i] != 'M')
                    {
                        if(iswdigit(ubuff[i]))
                        {
                            time *= 10;
                            time += ubuff[i] - 0x30;
                        }
                    }
                    else break;
                }

                ret.Add(
                    SMI2SSA(caption, CharSet),
                    file->IsUnicode(),
                    MS2RT(start_time), MS2RT(time));

                start_time = time;
                caption.Empty();
            }

            caption += buff;
        }

        if(ubuff.Find(L"-->") >= 0 || ubuff.Find(L"</TITLE>") >= 0)
            fComment = false;
    }

    ret.Add(
        SMI2SSA(caption, CharSet),
        file->IsUnicode(),
        MS2RT(start_time), LONGLONG_MAX);

#endif
    return(true);
}

static bool OpenVPlayer(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    CStringW buff;
    while(file->ReadString(buff))
    {
        FastTrim(buff);
        if(buff.IsEmpty()) continue;

#ifdef XY_VSFILTER_RUST_TEXT
        XyRust::PipeLine line(buff);
        for(int i = line.Position(); i >= 0; i = line.Position())
        {
            CStringW source = line.Text<CStringW>();
            line.Found(FindChar(source, '|', i, file->IsUnicode(), CharSet));
        }
        buff = line.Text<CStringW>();
#else
        for(int i = 0; i < buff.GetLength(); i++)
        {
            if((i = FindChar(buff, '|', i, file->IsUnicode(), CharSet)) < 0) break;
            buff.SetAt(i, '\n');
        }
#endif

        int hh, mm, ss;
        int c = swscanf_s(buff, L"%d:%d:%d:", &hh, &mm, &ss);

        if(c == 3)
        {
            CStringW str = buff.Mid(buff.Find(':', buff.Find(':', buff.Find(':')+1)+1)+1);
            ret.Add(str,
                file->IsUnicode(),
                MS2RT((((hh * 60i64 + mm) * 60i64) + ss) * 1000i64),
                MS2RT((((hh * 60i64 + mm) * 60i64) + ss) * 1000i64 + 1000i64 + 50i64 * str.GetLength()));
        }
        else if(c != EOF) // might be another format
        {
            return(false);
        }
    }

    return(!ret.IsEmpty());
}

static inline CStringW GetStr(CStringW& buff, WCHAR sep = L',') //throw(...)
{
#ifdef XY_VSFILTER_RUST_TEXT
    return XyRust::StringField(buff, sep);
#else
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
#endif
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
#ifdef XY_VSFILTER_RUST_TEXT
    return XyRust::NextField(buff, sep);
#else
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
#endif
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

#ifdef XY_VSFILTER_RUST_TEXT
    int datalen = XyRust::DecodeFont(static_cast<LPCTSTR>(font), len, static_cast<BYTE*>(pData));
    if (datalen < 0) return false;
#else
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

#endif

    // The font is private to this process and freed when it exits.
    DWORD cFonts;
    AddFontMemResourceEx(pData, datalen, NULL, &cFonts);

    return(true);
}

static bool LoadUUEFont(CTextFile* file)
{
#ifdef XY_VSFILTER_RUST_TEXT
    CString s;
    XyRust::FontRecords records;
    while(file->ReadString(s))
    {
        FastTrim(s);
        const unsigned action = records.Classify(s);
        if(action == 0) break;
        if(action == 1)
        {
            CString font = records.Text<CString>();
            LoadFont(font);
            records.Clear();
        }
        else
        {
            CString fragment;
            fragment += s;
            records.Append(fragment);
        }
    }
    if(!records.Empty())
    {
        CString font = records.Text<CString>();
        LoadFont(font);
    }
    return(true);
#else
    CString s, font;
    while(file->ReadString(s))
    {
        FastTrim(s);
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
#endif
}

static bool OpenSubStationAlpha(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    bool fRet = false;

    int version = 3, sver = 3;
#ifdef XY_VSFILTER_RUST_TEXT
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::AssDocument headers(*ret.m_document);
#else
    XyRust::AssHeaders headers;
#endif
#define XY_ASS_HEADER(key, id) (entryKind == (id))
#else
#define XY_ASS_HEADER(key, id) (entry == (key))
#endif

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT601;
    ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_TV;
#endif

    CStringW buff;
    while(true)
    {
    	bool fEOF = !file->ReadString(buff);
    	if(fEOF && buff.IsEmpty()) break;

        FastTrim(buff);
        if(buff.IsEmpty() || buff.GetAt(0) == ';') continue;

        CStringW entry = GetStr(buff, ':');
//  }
//      catch(...) {continue;}

        entry.MakeLower();
#ifdef XY_VSFILTER_RUST_TEXT
        const unsigned entryKind = headers.Classify(entry);
        version = headers.Field(0);
        sver = headers.Field(1);
        fRet = headers.Field(2) != 0;
#endif

        if(XY_ASS_HEADER(L"dialogue", 1))
        {
            try
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                headers.BeginDialogue(buff);
                CStringW token;
                WCHAR separator;
                while(headers.DialoguePart(0,token,separator)) {
                    CStringW::PXSTR cursor=token.GetBuffer();
                    headers.DialogueNumber(NextInt(&cursor,separator));
                }
                CStringW style, text;
                headers.DialoguePart(1,style,separator);
                const bool isDefault=!style.CompareNoCase(L"Default");
                headers.DialoguePart(4,text,separator);
                const bool unicode=file->IsUnicode();
                FastTrim(text);
                headers.AppendDialogue(text,unicode,isDefault);
#else
#ifdef XY_VSFILTER_RUST_TEXT
                XyRust::AssDialogue dialogue(buff, version);
                int reply = 0;
                while(dialogue.Step(reply))
                {
                    CStringW token = dialogue.Text<CStringW>(0);
                    CStringW::PXSTR cursor = token.GetBuffer();
                    reply = NextInt(&cursor, dialogue.Separator());
                }
                const int layer = dialogue.Number(0);
                const int hh1 = dialogue.Number(1), mm1 = dialogue.Number(2);
                const int ss1 = dialogue.Number(3), ms1_div10 = dialogue.Number(4);
                const int hh2 = dialogue.Number(5), mm2 = dialogue.Number(6);
                const int ss2 = dialogue.Number(7), ms2_div10 = dialogue.Number(8);
                CString Style = dialogue.Text<CStringW>(1);
                CString Actor = dialogue.Text<CStringW>(2);
                CString Effect = dialogue.Text<CStringW>(3);
                CRect marginRect;
                marginRect.left = dialogue.Number(9);
                marginRect.right = dialogue.Number(10);
                marginRect.top = dialogue.Number(11);
                marginRect.bottom = dialogue.Number(12);
                CStringW buff2 = dialogue.Text<CStringW>(4);
#else
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
#endif
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
#endif
            }
            catch(...)
            {
                //                ASSERT(0);
                //                throw;
                return(false);
            }
        }
        else if(XY_ASS_HEADER(L"style", 2))
        {
            STSStyle* style = DEBUG_NEW STSStyle;
            if(!style) return(false);

            try
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                XyRust::AssStyle fields(buff,sver);
                for(unsigned field;(field=fields.Next())!=0;) {
                    if(field==27)continue;
                    buff=fields.Text<CStringW>();
                    if(field==1 || field==2)fields.StringValue(field,GetStr(buff));
                    else if(field==3 || (field>=12 && field<=15) || field==17 || field==18)fields.Real(field,GetFloat(buff));
                    else fields.Integer(field,GetInt(buff));
                    fields.Accept(buff);
                }
                *style=XyRust::DocumentDetail::StyleValue(fields.Finish());
                const CString StyleName=fields.Name<CString>();
#else
                CString StyleName;
                int alpha;
                int encoding;
                CRect tmp_rect;

#ifdef XY_VSFILTER_RUST_TEXT
                XyRust::AssStyle fields(buff, sver);
                for(unsigned field; (field = fields.Next()) != 0; )
                {
                    if(field == 27) {style->marginRect = tmp_rect; continue;}
                    buff = fields.Text<CStringW>();
                    switch(field)
                    {
                    case 1: StyleName = GetStr(buff); break;
                    case 2: style->fontName = GetStr(buff); break;
                    case 3: style->fontSize = GetFloat(buff); break;
                    case 4: case 5: case 6: case 7: style->colors[field-4] = (COLORREF)GetInt(buff); break;
                    case 8: style->fontWeight = !!GetInt(buff) ? FW_BOLD : FW_NORMAL; break;
                    case 9: style->fItalic = !!GetInt(buff); break;
                    case 10: style->fUnderline = !!GetInt(buff); break;
                    case 11: style->fStrikeOut = !!GetInt(buff); break;
                    case 12: style->fontScaleX = GetFloat(buff); break;
                    case 13: style->fontScaleY = GetFloat(buff); break;
                    case 14: style->fontSpacing = GetFloat(buff); break;
                    case 15: style->fontAngleZ = GetFloat(buff); break;
                    case 16: style->borderStyle = GetInt(buff); break;
                    case 17: style->outlineWidthX = style->outlineWidthY = GetFloat(buff); break;
                    case 18: style->shadowDepthX = style->shadowDepthY = GetFloat(buff); break;
                    case 19: style->scrAlignment = GetInt(buff); break;
                    case 20: tmp_rect.left = GetInt(buff); break;
                    case 21: tmp_rect.right = GetInt(buff); break;
                    case 22: tmp_rect.top = tmp_rect.bottom = GetInt(buff); break;
                    case 23: tmp_rect.bottom = GetInt(buff); break;
                    case 24: alpha = GetInt(buff); break;
                    case 25: encoding = GetInt(buff); break;
                    case 26: style->relativeTo = GetInt(buff); break;
                    }
                    fields.Accept(buff);
                }

#else
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

#endif
#ifdef XY_VSFILTER_RUST_TEXT
                uint32_t colors[4];
                for(size_t i = 0; i < 4; i++) colors[i] = style->colors[i];
                fields.Normalize(colors, sver <= 4 ? alpha : 0, encoding, style->borderStyle, style->scrAlignment);
                style->charSet = static_cast<int>(fields.Number(8));
                for(size_t i = 0; i < 4; i++) {style->colors[i] = fields.Number(i); style->alpha[i] = static_cast<BYTE>(fields.Number(i+4));}
#else
                // Map unsupported extension to the most permissive charSet
                style->charSet = encoding < 0 ? DEFAULT_CHARSET : encoding;
if(sver <= 4)   style->colors[2] = style->colors[3]; // style->colors[2] is used for drawing the outline
if(sver <= 4)   alpha = max(min(alpha, 0xff), 0);
if(sver <= 4)   {for(size_t i = 0; i < 3; i++) style->alpha[i] = alpha; style->alpha[3] = 0x80;}
if(sver >= 5)   for(size_t i = 0; i < 4; i++) {style->alpha[i] = (BYTE)(style->colors[i]>>24); style->colors[i] &= 0xffffff;}
#endif
if(sver >= 5)   style->fontScaleX = max(style->fontScaleX, 0);
if(sver >= 5)   style->fontScaleY = max(style->fontScaleY, 0);
if(sver >= 5)   style->fontSpacing = max(style->fontSpacing, 0);
                style->fontAngleX = style->fontAngleY = 0;
#ifdef XY_VSFILTER_RUST_TEXT
                style->borderStyle = static_cast<int>(fields.Number(9));
#else
                style->borderStyle = style->borderStyle == 1 ? 0 : style->borderStyle == 3 ? 1 : 0;
#endif
                style->outlineWidthX = max(style->outlineWidthX, 0);
                style->outlineWidthY = max(style->outlineWidthY, 0);
                style->shadowDepthX = max(style->shadowDepthX, 0);
                style->shadowDepthY = max(style->shadowDepthY, 0);
#ifdef XY_VSFILTER_RUST_TEXT
                style->scrAlignment = static_cast<int>(fields.Number(10));
#else
if(sver <= 4)   style->scrAlignment = (style->scrAlignment&4) ? ((style->scrAlignment&3)+6) // top
                                        : (style->scrAlignment&8) ? ((style->scrAlignment&3)+3) // mid
                                        : (style->scrAlignment&3); // bottom
#endif

                StyleName.TrimLeft(_T('*'));

#endif
                ret.AddStyle(StyleName, style);
            }
            catch(...)
            {
                delete style;
                return(false);
            }
        }
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        else if(entryKind >= 4 && entryKind <= 8)
        {
            int value;
            try {value = GetInt(buff);}
            catch(...) {headers.NumberFailed(); return false;}
            headers.Number(value);
        }
        else if(entryKind == 9)
        {
            int detected = 0;
            if(buff.GetLength() >= 4 && !buff.Right(4).CompareNoCase(L"4.00")) detected = 4;
            else if(buff.GetLength() >= 5 && !buff.Right(5).CompareNoCase(L"4.00+")) detected = 5;
            else if(buff.GetLength() >= 6 && !buff.Right(6).CompareNoCase(L"4.00++")) detected = 6;
            headers.ScriptVersion(detected);
        }
        else if(entryKind == 10 || entryKind == 11 || entryKind == 17)
        {
            buff = GetStr(buff);
            buff.MakeLower();
            headers.Text(buff);
        }
        else if(entryKind == 16)
        {
            LoadUUEFont(file);
        }
#else
        else if(entry == L"[script info]")
        {
            fRet = true;
        }
        else if(XY_ASS_HEADER(L"playresx", 4))
        {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            try {ret.SetDocumentScreenWidth(GetInt(buff));}
#else
            try {ret.m_dstScreenSize.cx = GetInt(buff);}
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            catch(...) {ret.SetDocumentScreenSize(CSize(0, 0)); return(false);}
#else
            catch(...) {ret.m_dstScreenSize = CSize(0, 0); return(false);}
#endif

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            if(ret.GetDocumentScreenSize().cy <= 0)
#else
            if(ret.m_dstScreenSize.cy <= 0)
#endif
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentScreenHeight((ret.GetDocumentScreenSize().cx == 1280)
                    ? 1024
                    : ret.GetDocumentScreenSize().cx * 3 / 4);
#else
                ret.m_dstScreenSize.cy = (ret.m_dstScreenSize.cx == 1280)
                    ? 1024
                    : ret.m_dstScreenSize.cx * 3 / 4;
#endif
            }
        }
        else if(XY_ASS_HEADER(L"playresy", 5))
        {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            try {ret.SetDocumentScreenHeight(GetInt(buff));}
#else
            try {ret.m_dstScreenSize.cy = GetInt(buff);}
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            catch(...) {ret.SetDocumentScreenSize(CSize(0, 0)); return(false);}
#else
            catch(...) {ret.m_dstScreenSize = CSize(0, 0); return(false);}
#endif

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            if(ret.GetDocumentScreenSize().cx <= 0)
#else
            if(ret.m_dstScreenSize.cx <= 0)
#endif
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentScreenWidth((ret.GetDocumentScreenSize().cy == 1024)
                    ? 1280
                    : ret.GetDocumentScreenSize().cy * 4 / 3);
#else
                ret.m_dstScreenSize.cx = (ret.m_dstScreenSize.cy == 1024)
                    ? 1280
                    : ret.m_dstScreenSize.cy * 4 / 3;
#endif
            }
        }
        else if(XY_ASS_HEADER(L"layoutresx", 6))
        {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            try {ret.SetDocumentLayoutWidth(GetInt(buff));}
            catch(...) {ret.SetDocumentLayoutSize(CSize(0, 0)); return(false);}
#else
            try {ret.m_layout_size.cx = GetInt(buff);}
            catch(...) {ret.m_layout_size = CSize(0, 0); return(false);}
#endif
        }
        else if(XY_ASS_HEADER(L"layoutresy", 7))
        {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            try {ret.SetDocumentLayoutHeight(GetInt(buff));}
            catch(...) {ret.SetDocumentLayoutSize(CSize(0, 0)); return(false);}
#else
            try {ret.m_layout_size.cy = GetInt(buff);}
            catch(...) {ret.m_layout_size = CSize(0, 0); return(false);}
#endif
        }
        else if(XY_ASS_HEADER(L"wrapstyle", 8))
        {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            try {ret.SetDocumentWrapStyle(GetInt(buff));}
#else
            try {ret.m_defaultWrapStyle = GetInt(buff);}
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            catch(...) {ret.SetDocumentWrapStyle(1); return(false);}
#else
            catch(...) {ret.m_defaultWrapStyle = 1; return(false);}
#endif
        }
        else if(XY_ASS_HEADER(L"scripttype", 9))
        {
#ifdef XY_VSFILTER_RUST_TEXT
            int detected = 0;
            if(buff.GetLength() >= 4 && !buff.Right(4).CompareNoCase(L"4.00")) detected = 4;
            else if(buff.GetLength() >= 5 && !buff.Right(5).CompareNoCase(L"4.00+")) detected = 5;
            else if(buff.GetLength() >= 6 && !buff.Right(6).CompareNoCase(L"4.00++")) detected = 6;
            headers.ScriptVersion(detected);
            version = headers.Field(0);
            sver = headers.Field(1);
#else
            if(buff.GetLength() >= 4 && !buff.Right(4).CompareNoCase(L"4.00")) version = sver = 4;
            else if(buff.GetLength() >= 5 && !buff.Right(5).CompareNoCase(L"4.00+")) version = sver = 5;
            else if(buff.GetLength() >= 6 && !buff.Right(6).CompareNoCase(L"4.00++")) version = sver = 6;
#endif
        }
        else if(XY_ASS_HEADER(L"collisions", 10))
        {
            buff = GetStr(buff);
            buff.MakeLower();
#ifdef XY_VSFILTER_RUST_TEXT
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            ret.SetDocumentCollisions(static_cast<int>(headers.Value(buff, 0)));
#else
            ret.m_collisions = static_cast<int>(headers.Value(buff, 0));
#endif
#else
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            ret.SetDocumentCollisions(buff.Find(L"reverse") >= 0 ? 1 : 0);
#else
            ret.m_collisions = buff.Find(L"reverse") >= 0 ? 1 : 0;
#endif
#endif
        }
        else if(XY_ASS_HEADER(L"scaledborderandshadow", 11))
        {
            buff = GetStr(buff);
            buff.MakeLower();
#ifdef XY_VSFILTER_RUST_TEXT
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            ret.SetDocumentScaledBAS(headers.Value(buff, 1) != 0);
#else
            ret.m_fScaledBAS = headers.Value(buff, 1) != 0;
#endif
#else
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            ret.SetDocumentScaledBAS(buff.Find(L"yes") >= 0);
#else
            ret.m_fScaledBAS = buff.Find(L"yes") >= 0;
#endif
#endif
        }
        else if(XY_ASS_HEADER(L"[v4 styles]", 12))
        {
            fRet = true;
            sver = 4;
        }
        else if(XY_ASS_HEADER(L"[v4+ styles]", 13))
        {
            fRet = true;
            sver = 5;
        }
        else if(XY_ASS_HEADER(L"[v4++ styles]", 14))
        {
            fRet = true;
            sver = 6;
        }
        else if(XY_ASS_HEADER(L"[events]", 15))
        {
            fRet = true;
        }
        else if(XY_ASS_HEADER(L"fontname", 16))
        {
            LoadUUEFont(file);
        }
        else if(XY_ASS_HEADER(L"ycbcr matrix", 17))
        {
            buff = GetStr(buff);
            buff.MakeLower();
#ifdef XY_VSFILTER_RUST_TEXT
            const unsigned matrix = headers.Value(buff, 2);
            switch(matrix)
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            case 1: ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_AUTO); ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_AUTO); break;
#else
            case 1: ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_AUTO; ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_AUTO; break;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            case 2: ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT601); ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_TV); break;
#else
            case 2: ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT601; ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_TV; break;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            case 3: ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT709); ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_TV); break;
#else
            case 3: ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT709; ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_TV; break;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            case 4: ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT2020); ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_TV); break;
#else
            case 4: ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT2020; ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_TV; break;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            case 5: ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT601); ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_PC); break;
#else
            case 5: ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT601; ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_PC; break;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            case 6: ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT709); ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_PC); break;
#else
            case 6: ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT709; ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_PC; break;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            case 7: ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT2020); ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_PC); break;
#else
            case 7: ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT2020; ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_PC; break;
#endif
            }
#else
            if (buff.Left(4)==L"none")
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_AUTO);
#else
                ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_AUTO;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_AUTO);
#else
                ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_AUTO;
#endif
            }
            else if (buff.Left(6)==L"tv.601")
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT601);
#else
                ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT601;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_TV);
#else
                ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_TV;
#endif
            }
            else if (buff.Left(6)==L"tv.709")
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT709);
#else
                ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT709;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_TV);
#else
                ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_TV;
#endif
            }
            else if (buff.Left(7)==L"tv.2020")
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT2020);
#else
                ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT2020;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_TV);
#else
                ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_TV;
#endif
            }
            else if (buff.Left(6)==L"pc.601")
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT601);
#else
                ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT601;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_PC);
#else
                ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_PC;
#endif
            }
            else if (buff.Left(6)==L"pc.709")
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT709);
#else
                ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT709;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_PC);
#else
                ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_PC;
#endif
            }
            else if (buff.Left(7)==L"pc.2020")
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorMatrix(CSimpleTextSubtitle::YCbCrMatrix_BT2020);
#else
                ret.m_eYCbCrMatrix = CSimpleTextSubtitle::YCbCrMatrix_BT2020;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentColorRange(CSimpleTextSubtitle::YCbCrRange_PC);
#else
                ret.m_eYCbCrRange = CSimpleTextSubtitle::YCbCrRange_PC;
#endif
            }
#endif
        }
#endif
        if(fEOF) break;
    }
//    ret.Sort();
#undef XY_ASS_HEADER
    return(fRet);
}

static bool OpenXombieSub(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    float version = 0;

//  CMapStringToPtr stylemap;

    CStringW buff;
    while(file->ReadString(buff))
    {
        FastTrim(buff);
        if(buff.IsEmpty() || buff.GetAt(0) == ';') continue;

        CStringW entry = GetStr(buff, L'=');
//  }
//      catch(...) {continue;}

        entry.MakeLower();

        if(entry == L"version")
        {
            version = (float)GetFloat(buff);
        }
        else if(entry == L"screenhorizontal")
        {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            try {ret.SetDocumentScreenWidth(GetInt(buff));}
#else
            try {ret.m_dstScreenSize.cx = GetInt(buff);}
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            catch(...) {ret.SetDocumentScreenSize(CSize(0, 0)); return(false);}
#else
            catch(...) {ret.m_dstScreenSize = CSize(0, 0); return(false);}
#endif

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            if(ret.GetDocumentScreenSize().cy <= 0)
#else
            if(ret.m_dstScreenSize.cy <= 0)
#endif
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentScreenHeight((ret.GetDocumentScreenSize().cx == 1280)
                    ? 1024
                    : ret.GetDocumentScreenSize().cx * 3 / 4);
#else
                ret.m_dstScreenSize.cy = (ret.m_dstScreenSize.cx == 1280)
                    ? 1024
                    : ret.m_dstScreenSize.cx * 3 / 4;
#endif
            }
        }
        else if(entry == L"screenvertical")
        {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            try {ret.SetDocumentScreenHeight(GetInt(buff));}
#else
            try {ret.m_dstScreenSize.cy = GetInt(buff);}
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            catch(...) {ret.SetDocumentScreenSize(CSize(0, 0)); return(false);}
#else
            catch(...) {ret.m_dstScreenSize = CSize(0, 0); return(false);}
#endif

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            if(ret.GetDocumentScreenSize().cx <= 0)
#else
            if(ret.m_dstScreenSize.cx <= 0)
#endif
            {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
                ret.SetDocumentScreenWidth((ret.GetDocumentScreenSize().cy == 1024)
                    ? 1280
                    : ret.GetDocumentScreenSize().cy * 4 / 3);
#else
                ret.m_dstScreenSize.cx = (ret.m_dstScreenSize.cy == 1024)
                    ? 1280
                    : ret.m_dstScreenSize.cy * 4 / 3;
#endif
            }
        }
        else if(entry == L"style")
        {
            STSStyle* style = DEBUG_NEW STSStyle;
            if(!style) return(false);

            try
            {
                CString StyleName;
                CRect tmp_rect;

                StyleName = GetStr(buff) + _T("_") + GetStr(buff);
                style->fontName = GetStr(buff);
                style->fontSize = GetFloat(buff);
                for(size_t  i = 0; i < 4; i++) style->colors[i] = (COLORREF)GetInt(buff);
                for(size_t  i = 0; i < 4; i++) style->alpha[i] = GetInt(buff);
                style->fontWeight = !!GetInt(buff) ? FW_BOLD : FW_NORMAL;
                style->fItalic = !!GetInt(buff);
                style->fUnderline = !!GetInt(buff);
                style->fStrikeOut = !!GetInt(buff);
                style->fBlur = !!GetInt(buff);
                style->fontScaleX = GetFloat(buff);
                style->fontScaleY = GetFloat(buff);
                style->fontSpacing = GetFloat(buff);
                style->fontAngleX = GetFloat(buff);
                style->fontAngleY = GetFloat(buff);
                style->fontAngleZ = GetFloat(buff);
                style->borderStyle = GetInt(buff);
                style->outlineWidthX = style->outlineWidthY = GetFloat(buff);
                style->shadowDepthX = style->shadowDepthY = GetFloat(buff);
                style->scrAlignment = GetInt(buff);

                tmp_rect.left = GetInt(buff);
                tmp_rect.right = GetInt(buff);
                tmp_rect.top = tmp_rect.bottom = GetInt(buff);
                style->marginRect = tmp_rect;

                style->charSet = GetInt(buff);

                style->fontScaleX = max(style->fontScaleX, 0);
                style->fontScaleY = max(style->fontScaleY, 0);
                style->fontSpacing = max(style->fontSpacing, 0);
                style->borderStyle = style->borderStyle == 1 ? 0 : style->borderStyle == 3 ? 1 : 0;
                style->outlineWidthX = max(style->outlineWidthX, 0);
                style->outlineWidthY = max(style->outlineWidthY, 0);
                style->shadowDepthX = max(style->shadowDepthX, 0);
                style->shadowDepthY = max(style->shadowDepthY, 0);

                ret.AddStyle(StyleName, style);
            }
            catch(...)
            {
                delete style;
                return(false);
            }
        }
        else if(entry == L"line")
        {
            try
            {
                CString id;
                int hh1, mm1, ss1, ms1, hh2, mm2, ss2, ms2, layer = 0;
                CString Style, Actor;
                CRect marginRect;

                if(GetStr(buff) != L"D") continue;
                id = GetStr(buff);
                layer = GetInt(buff);
                hh1 = GetInt(buff, L':');
                mm1 = GetInt(buff, L':');
                ss1 = GetInt(buff, L'.');
                ms1 = GetInt(buff);
                hh2 = GetInt(buff, L':');
                mm2 = GetInt(buff, L':');
                ss2 = GetInt(buff, L'.');
                ms2 = GetInt(buff);
                Style = GetStr(buff) + _T("_") + GetStr(buff);
                Actor = GetStr(buff);
                marginRect.left = GetInt(buff);
                marginRect.right = GetInt(buff);
                marginRect.top = marginRect.bottom = GetInt(buff);

                Style.TrimLeft(_T('*'));
                if(!Style.CompareNoCase(_T("Default"))) Style = _T("Default");

                ret.Add(buff,
                    file->IsUnicode(),
                    MS2RT((((hh1 * 60i64 + mm1) * 60i64) + ss1) * 1000i64 + ms1),
                    MS2RT((((hh2 * 60i64 + mm2) * 60i64) + ss2) * 1000i64 + ms2),
                    Style, Actor, _T(""),
                    marginRect,
                    layer);
            }
            catch(...)
            {
                return(false);
            }
        }
        else if(entry == L"fontname")
        {
            LoadUUEFont(file);
        }
    }

    return(!ret.IsEmpty());
}

#include "USFSubtitles.h"

static bool OpenUSF(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    CString str;
    while(file->ReadString(str))
    {
        if(str.Find(_T("USFSubtitles")) >= 0)
        {
            CUSFSubtitles usf;
            size_t len;
            const BYTE* data = file->GetMemoryData(len);
            if((data ? usf.Read(data, len) : usf.Read(file->GetFilePath())) && usf.ConvertToSTS(ret))
                return(true);

            break;
        }
    }

    return(false);
}

static CStringW MPL22SSA(CStringW str, bool fUnicode, int CharSet)
{
#ifdef XY_VSFILTER_RUST_TEXT
    XyRust::MPL2 convert;
    CStringW prepared;
    if(convert.Convert(str, prepared)) return MicroDVD2SSA(prepared, fUnicode, CharSet);
#endif
    // Convert MPL2 italic tags to MicroDVD italic tags
    if (str[0] == L'/') {
        str = L"{y:i}" + str.Mid(1);
    }
    str.Replace(L"|/", L"|{y:i}");

    return MicroDVD2SSA(str, fUnicode, CharSet);
}

static bool OpenMPL2(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    CStringW buff;
    while(file->ReadString(buff)) {
        FastTrim(buff);
        if (buff.IsEmpty()) {
            continue;
        }

        int start, end;
        int c = swscanf_s(buff, L"[%d][%d]", &start, &end);

        if (c == 2) {
            ret.Add(
                MPL22SSA(buff.Mid(buff.Find(']', buff.Find(']') + 1) + 1), file->IsUnicode(), CharSet),
                file->IsUnicode(),
                MS2RT(start * 100i64),
                MS2RT(end * 100i64));
        } else if (c != EOF) { // might be another format
            return false;
        }
    }

    return(!ret.IsEmpty());
}

static bool OpenRealText(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet)
{
    std::wstring szFile;
    CStringW buff;

    while(file->ReadString(buff)) {
        FastTrim(buff);
        if (buff.IsEmpty()) {
            continue;
        }

        // Make sure that the subtitle file starts with a <window> tag
        if (szFile.empty() && buff.CompareNoCase(_T("<window")) < 0) {
            return false;
        }

        szFile += _T("\n") + buff;
    }

    CRealTextParser RealTextParser;
    if (!RealTextParser.ParseRealText(szFile)) {
        return false;
    }

    CRealTextParser::Subtitles crRealText = RealTextParser.GetParsedSubtitles();

    for (auto i = crRealText.m_mapLines.cbegin(); i != crRealText.m_mapLines.cend(); ++i) {
        ret.Add(
            SubRipper2SSA(i->second.c_str(), CharSet),
            file->IsUnicode(),
            MS2RT(i->first.first),
            MS2RT(i->first.second));
    }

    return !ret.IsEmpty();
}

typedef bool (*STSOpenFunct)(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet);

struct OpenFunctStruct {
    STSOpenFunct open;
    tmode mode;
    exttype type;
};

static OpenFunctStruct OpenFuncts[] =
{
    OpenSubStationAlpha, TIME, EXTSSA,
    OpenSubRipper      , TIME, EXTSRT,
    OpenOldSubRipper   , TIME, EXTSRT,
    OpenSubViewer      , TIME, EXTSUB,
    OpenMicroDVD       , FRAME, EXTSSA,
    OpenSami           , TIME, EXTSMI,
    OpenVTT            , TIME, EXTVTT,
    OpenVPlayer        , TIME, EXTSRT,
    OpenXombieSub      , TIME, EXTXSS,
    OpenUSF            , TIME, EXTUSF,
    OpenMPL2           , TIME, EXTSRT,
    OpenRealText       , TIME, EXTRT,
};

static int nOpenFuncts = countof(OpenFuncts);

//

CSimpleTextSubtitle::CSimpleTextSubtitle()
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    : m_document(new XyRust::DocumentOwner)
#endif
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentLayoutSize(CSize());
    SetDocumentType(EXTSRT);
#else
    m_subtitleType         = EXTSRT;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentMode(TIME);
#else
    m_mode                 = TIME;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentScreenSize(CSize(0, 0));
#else
    m_dstScreenSize        = CSize(0, 0);
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentWrapStyle(0);
#else
    m_defaultWrapStyle     = 0;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentCollisions(0);
#else
    m_collisions           = 0;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentScaledBAS(false);
#else
    m_fScaledBAS           = false;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentEncoding(CTextFile::DEFAULT_ENCODING);
#else
    m_encoding             = CTextFile::DEFAULT_ENCODING;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentPARType(EPCTDisabled);
#else
    m_ePARCompensationType = EPCTDisabled;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentPARCompensation(1.0);
#else
    m_dPARCompensation     = 1.0;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentColorMatrix(YCbCrMatrix_AUTO);
#else
    m_eYCbCrMatrix         = YCbCrMatrix_AUTO;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentColorRange(YCbCrRange_AUTO);
#else
    m_eYCbCrRange          = YCbCrRange_AUTO;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentForcedDefaultStyle(false);
#else
    m_fForcedDefaultStyle  = false;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    ResetDefaultStyle(DEFAULT_CHARSET);
#else
    m_defaultStyle.charSet = DEFAULT_CHARSET;
#endif
}

CSimpleTextSubtitle::~CSimpleTextSubtitle()
{
    Empty();
}

void CSimpleTextSubtitle::Copy(CSimpleTextSubtitle& sts)
{
    Empty();

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->CopySelectedState(*sts.m_document);
#else
    m_name                         = sts.m_name;
    m_mode                         = sts.m_mode;
    m_subtitleType                 = sts.m_subtitleType;
    m_dstScreenSize                = sts.m_dstScreenSize;
    m_defaultWrapStyle             = sts.m_defaultWrapStyle;
    m_collisions                   = sts.m_collisions;
    m_fScaledBAS                   = sts.m_fScaledBAS;
    m_encoding                     = sts.m_encoding;
    m_defaultStyle                 = sts.m_defaultStyle;
    m_fForcedDefaultStyle          = sts.m_fForcedDefaultStyle;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    CopyNamedStylesFrom(sts);
#else
    CopyStyles     (sts.m_styles  );
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->CopyFrom(*sts.m_document);
#else
    m_segments.Copy(sts.m_segments);
    m_entries.Copy (sts.m_entries );
#endif
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

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
template<class Submission>
static void SubmitStyleSuffix(const CString& name_str, Submission& submission)
{
    int i;
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
    while(!submission.Submit(name3));
}

bool CSimpleTextSubtitle::CopyNamedStylesFrom(const CSimpleTextSubtitle& source, bool fAppend)
{
    XyRust::NamedStyleCopy copy(*m_document, *source.m_document, fAppend);
    CString name;
    for(;;)
    {
        const uint32_t action = copy.Next(name);
        if(action == 0) return true;
        if(action == 2) SubmitStyleSuffix(name, copy);
    }
}
#else
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

#endif

void CSimpleTextSubtitle::Empty()
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->Empty();
#else
    m_dstScreenSize = CSize(0, 0);
    m_styles.Free();
    m_segments.RemoveAll();
    m_entries.RemoveAll();
#endif
}

static bool SegmentCompStart(const STSSegment& segment, REFERENCE_TIME start)
{
    return (segment.start < start);
}

void CSimpleTextSubtitle::Add(CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end,
    CString style, const CString& actor, const CString& effect, const CRect& marginRect, int layer, int readorder)
{
    TRACE_SUB(ReftimeToCString(start)<<" "<<ReftimeToCString(end)<<" "<<str.GetString()<<" style:"<<style.GetString()
        <<" Unicode:"<<fUnicode
        <<" actor:"<<actor.GetString()<<" effect:"<<effect.GetString()
        <<" (l:"<<marginRect.left<<",t:"<<marginRect.top<<",r:"<<marginRect.right<<",b:"<<marginRect.bottom
        <<" layer:"<<layer<<" readorder:"<<readorder
        <<" entries:"<<GetEntryCount()<<" seg:"<<GetSegmentCount());

    FastTrim(str);
    if (str.IsEmpty() || start > end) return;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    if (GetDocumentType() == EXTVTT) {
#else
    if (m_subtitleType == EXTVTT) {
#endif
        WebVTTCueStrip(str);
        WebVTT2SSA(str);
        if (str.IsEmpty()) return;
    }

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->AddFinalSuffix(str, fUnicode, start, end, style, actor, effect,
        marginRect, layer, readorder, GetDocumentType() == EXTVTT ? 5 : 3);
#else
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
    sub.readorder  = readorder < 0 ? (int)GetEntryCount() : readorder;

    int n = (int)GetEntryCount();

    if (start == end) return;

    size_t segmentsCount = GetSegmentCount();

    if (segmentsCount == 0) { // First segment
        n = (int)m_entries.Add(sub);

        STSSegment stss(start, end);
        stss.subs.Add(n);
        m_segments.Add(stss);
    } else {
        STSSegment* segmentsStart = m_segments.GetData();
        STSSegment* segmentsEnd   = segmentsStart + segmentsCount;
        STSSegment* segment = std::lower_bound(segmentsStart, segmentsEnd, start, SegmentCompStart);

        if (m_subtitleType == EXTVTT
#ifdef XY_VSFILTER_RUST_TEXT
            && segment != segmentsEnd
#endif
            && start == segment->start && end == segment->end) {
            // ToDo: compare new sub with existing one to verify if it is really a duplicate
            //TRACE(_T("Dropping duplicate WebVTT sub (n=%d)\n"), n);
            return;
        }

        n = (int)m_entries.Add(sub);

        size_t i = segment - segmentsStart;
#ifdef XY_VSFILTER_RUST_TEXT
        XyRust::SegmentInsertion insertion;
        for (size_t j = 0; j < GetEntryCount(); ++j) insertion.Order(m_entries.GetAt(j).readorder);
        for (size_t j = 0; j < GetSegmentCount(); ++j) {
            const STSSegment& existing = m_segments[j];
            insertion.Segment(existing.start, existing.end);
            for (size_t k = 0; k < existing.subs.GetCount(); ++k) insertion.Sub(existing.subs[k]);
        }
        insertion.Build(start, end, n, i);
        insertion.Apply<STSSegment>(m_segments);
#else
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
        for (; i < GetSegmentCount() && m_segments[i].start < end; i++) {
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
#endif
    }
#endif
}

void CSimpleTextSubtitle::AddSTSEntryOnly( CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end, CString style /*= _T("Default")*/, const CString& actor /*= _T("")*/, const CString& effect /*= _T("")*/, const CRect& marginRect /*= CRect(0,0,0,0)*/, int layer /*= 0*/, int readorder /*= -1*/ )
{
    FastTrim(str);
    if (str.IsEmpty() || start > end) return;

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->AddFinalSuffix(str, fUnicode, start, end, style, actor, effect,
        marginRect, layer, readorder, 4);
#else
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
    sub.readorder  = readorder < 0 ? GetEntryCount() : readorder;
    m_entries.Add(sub);
#endif
    return;
}

void CSimpleTextSubtitle::ChangeUnknownStylesToDefault()
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::UnknownStylesPass pass(*m_document);
    for(;;) {
        CString name;
        const uint32_t action = pass.Next(name);
        if(action == 0) break;
        if(action == 2) {
            CString msg;
            msg.Format(_T("Unknown style found: \"%s\", changed to \"Default\"!\n\nPress Cancel to ignore further warnings."), name);
            pass.Resume(MessageBox(NULL, msg, _T("Warning"), MB_OKCANCEL|MB_ICONWARNING) == IDOK);
        }
    }
#else
    CAtlMap<CString, STSStyle*, CStringElementTraits<CString> > unknown;
    bool fReport = true;
    CString last_style = _T("!@dkdfjlakfkjjklezdv^5132ae");
    bool last_style_changed_to_unknow = false;
    for(size_t i = 0; i < GetEntryCount(); i++)
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
                if(!unknown.Lookup(stse.style, val))
                {
                    if(fReport && stse.style!=g_default_style)
                    {
                        CString msg;
                        msg.Format(_T("Unknown style found: \"%s\", changed to \"Default\"!\n\nPress Cancel to ignore further warnings."), stse.style);
                        if(MessageBox(NULL, msg, _T("Warning"), MB_OKCANCEL|MB_ICONWARNING) != IDOK) fReport = false;
                    }

                    unknown[stse.style] = NULL;
                }

                stse.style = g_default_style;
            }
        }
    }
#endif
}

void CSimpleTextSubtitle::AddStyle(CString name, STSStyle* style)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::StyleAddition addition(*m_document, name, style);
    if(addition.NeedsSuffix()) SubmitStyleSuffix(addition.Name(), addition);
    delete style;
#else
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

        for (size_t j = 0, count = GetEntryCount(); j < count; j++)
        {
            STSEntry& stse = m_entries.GetAt(j);
            if(stse.style == name) stse.style = name3;
        }
    }

    m_styles[name] = style;
#endif
}

bool CSimpleTextSubtitle::SetDefaultStyle(STSStyle& s)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    LogStyles(*this);
#else
    LogStyles(m_styles);
#endif

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->SetDefaultStyle(s);
#else
    m_defaultStyle = s;
#endif
    return true;
}

void CSimpleTextSubtitle::SetForceDefaultStyle( bool value )
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    SetDocumentForcedDefaultStyle(value);
#else
    m_fForcedDefaultStyle = value;
#endif
}

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
const STSSegment* CSimpleTextSubtitle::SearchSubs(REFERENCE_TIME t, double fps, /*[out]*/ int* iSegment, int* nSegments)
{
#ifdef XY_VSFILTER_RUST_TEXT
    const int count = static_cast<int>(GetSegmentCount());
    if (nSegments) *nSegments = count;
    XyRust::TimelineSearch search(1, t, count);
    while (search.Pending()) search.Supply(TranslateSegmentEnd(search.Index(), fps));
    if (iSegment) *iSegment = search.Result();
    if (search.Found()) return &m_segments[search.Result()];
    if (count <= 0) {
        TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(t));
    } else {
        TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(RT2MS(t)));
    }
    return NULL;
#else
    int segmentsCount = GetSegmentCount();
    int i = 0, j = segmentsCount - 1;

    if(nSegments) *nSegments = segmentsCount;
    if(segmentsCount<=0)
    {
        if(iSegment!=NULL)
            *iSegment = 0;
        TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(t));
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
    if(iSegment!=NULL)
        *iSegment = j;
    if(j<segmentsCount)
    {
        return &m_segments[j];
    }
    TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(RT2MS(t)));
    return(NULL);
#endif
}

STSSegment* CSimpleTextSubtitle::SearchSubs2(REFERENCE_TIME t, double fps, /*[out]*/ int* iSegment, int* nSegments)
{
#ifdef XY_VSFILTER_RUST_TEXT
    const int count = static_cast<int>(GetSegmentCount());
    if (iSegment) *iSegment = -1;
    if (nSegments) *nSegments = count;
    XyRust::TimelineSearch search(2, t, count);
    while (search.Pending()) {
        const int index = search.Index();
        search.Supply(search.Start() ? TranslateSegmentStart(index, fps) : TranslateSegmentEnd(index, fps));
    }
    if (count <= 0 || search.Found()) {
        if (iSegment) *iSegment = search.Result();
    }
    return search.Found() ? &m_segments[search.Result()] : NULL;
#else
    int segmentsCount = GetSegmentCount();
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
#endif
}

#endif

REFERENCE_TIME CSimpleTextSubtitle::TranslateStart(int i, double fps)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    int n = (int)GetEntryCount();
    return(i < 0 || n <= i ? -1 :
        GetDocumentMode() == TIME ? m_document->EntryStart(i) :
        GetDocumentMode() == FRAME ? std::llround(m_document->EntryStart(i) * UNITS_FLOAT / fps) :
        0);
#else
    int n = GetEntryCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_entries.GetAt(i).start :
        m_mode == FRAME ? std::llround(m_entries.GetAt(i).start * UNITS_FLOAT / fps) :
        0);
#endif
}

REFERENCE_TIME CSimpleTextSubtitle::TranslateEnd(int i, double fps)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    int n = (int)GetEntryCount();
    return(i < 0 || n <= i ? -1 :
        GetDocumentMode() == TIME ? m_document->EntryEnd(i) :
        GetDocumentMode() == FRAME ? std::llround(m_document->EntryEnd(i) * UNITS_FLOAT / fps) :
        0);
#else
    int n = GetEntryCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_entries.GetAt(i).end :
        m_mode == FRAME ? std::llround(m_entries.GetAt(i).end * UNITS_FLOAT / fps) :
        0);
#endif
}

REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentStart(int i, double fps)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    int n = (int)GetSegmentCount();
    return(i < 0 || n <= i ? -1 :
        GetDocumentMode() == TIME ? m_document->SegmentStart(i) :
        GetDocumentMode() == FRAME ? std::llround(m_document->SegmentStart(i) * UNITS_FLOAT / fps) :
        0);
#else
    int n = GetSegmentCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_segments[i].start :
        m_mode == FRAME ? std::llround(m_segments[i].start * UNITS_FLOAT / fps) :
        0);
#endif
}

REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentEnd(int i, double fps)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    int n = (int)GetSegmentCount();
    return(i < 0 || n <= i ? -1 :
        GetDocumentMode() == TIME ? m_document->SegmentEnd(i) :
        GetDocumentMode() == FRAME ? std::llround(m_document->SegmentEnd(i) * UNITS_FLOAT / fps) :
        0);
#else
    int n = GetSegmentCount();
    return(i < 0 || n <= i ? -1 :
        m_mode == TIME ? m_segments[i].end :
        m_mode == FRAME ? std::llround(m_segments[i].end * UNITS_FLOAT / fps) :
        0);
#endif
}

void CSimpleTextSubtitle::TranslateSegmentStartEnd(int i, double fps, /*out*/REFERENCE_TIME& start, /*out*/REFERENCE_TIME& end)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    int n = (int)GetSegmentCount();
    if(i < 0 || n <= i)
    {
        start=-1;
        end=-1;
    }
    else
    {
        if(GetDocumentMode() == TIME)
        {
            start = m_document->SegmentStart(i);
            end = m_document->SegmentEnd(i);
        }
        else //GetDocumentMode() == FRAME
        {
            start = std::llround(m_document->SegmentStart(i) * UNITS_FLOAT / fps);
            end = std::llround(m_document->SegmentEnd(i) * UNITS_FLOAT / fps);
        }
    }
#else
    int n = GetSegmentCount();
    if(i < 0 || n <= i)
    {
        start=-1;
        end=-1;
    }
    else
    {
        if(m_mode == TIME)
        {
            start = m_segments[i].start;
            end = m_segments[i].end;
        }
        else //m_mode == FRAME
        {
            start = std::llround(m_segments[i].start * UNITS_FLOAT / fps);
            end = std::llround(m_segments[i].end * UNITS_FLOAT / fps);
        }
    }
#endif
}

// Document access surface: native arrays by default, persistent Rust storage
// in the experimental build. Returned snapshots contain owned native values.
size_t CSimpleTextSubtitle::GetEntryCount() const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->Count();
#else
    return m_entries.GetCount();
#endif
}

size_t CSimpleTextSubtitle::GetSegmentCount() const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->SegmentCount();
#else
    return m_segments.GetCount();
#endif
}

STSEntry CSimpleTextSubtitle::GetEntrySnapshot(int i) const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->Entry(i);
#else
    return m_entries.GetAt(i);
#endif
}

REFERENCE_TIME CSimpleTextSubtitle::GetEntryEnd(int i) const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->EntryEnd(i);
#else
    return m_entries.GetAt(i).end;
#endif
}

int CSimpleTextSubtitle::GetEntryLayer(int i) const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->EntryLayer(i);
#else
    return m_entries.GetAt(i).layer;
#endif
}

int CSimpleTextSubtitle::GetEntryReadOrder(int i) const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->EntryReadOrder(i);
#else
    return m_entries.GetAt(i).readorder;
#endif
}

CString CSimpleTextSubtitle::GetEntryEffect(int i) const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->EntryEffect(i);
#else
    return m_entries.GetAt(i).effect;
#endif
}

bool CSimpleTextSubtitle::GetSegmentEntryCount(int iSegment, size_t& count) const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->SegmentEntryCount(iSegment, count);
#else
    if(iSegment < 0 || iSegment >= (int)GetSegmentCount()) return false;
    count = m_segments[iSegment].subs.GetCount();
    return true;
#endif
}

bool CSimpleTextSubtitle::FindNextSegment(REFERENCE_TIME t, double fps,
    int* iSegment, int* nSegments)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    if(GetDocumentMode() == TIME) {
        const XyRust::DocumentQuery query = m_document->Query(1, t);
        if(nSegments) *nSegments = (int)query.count;
        if(iSegment) *iSegment = query.index;
        if(!query.found) {
            if(query.count == 0) TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(t));
            else TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(RT2MS(t)));
        }
        return query.found;
    }
    XyRust::TranslatedDocumentSearch search(*m_document, 1, t);
    if(nSegments) *nSegments = search.Count();
    while(search.Pending()) search.Supply(TranslateSegmentEnd(search.Index(), fps));
    if(iSegment) *iSegment = search.Result();
    if(!search.Found()) {
        if(search.Count() == 0) TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(t));
        else TRACE_SUB("No subtitles at "<<XY_LOG_VAR_2_STR(RT2MS(t)));
    }
    return search.Found();
#else
    return SearchSubs(t, fps, iSegment, nSegments) != NULL;
#endif
}

bool CSimpleTextSubtitle::SearchSubs2Snapshot(REFERENCE_TIME t, double fps,
    STSSegment& snapshot, int* iSegment, int* nSegments)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    if(GetDocumentMode() == TIME) {
        const XyRust::DocumentQuery query = m_document->Query(2, t);
        if(iSegment) *iSegment = -1;
        if(nSegments) *nSegments = (int)query.count;
        // Match native SearchSubs2's conditional final write, even when the
        // caller aliases iSegment and nSegments (a failed nonempty query leaves
        // the count in that shared output).
        if(query.count == 0 || query.found) {
            if(iSegment) *iSegment = query.index;
        }
        return query.found ? m_document->Segment(query.index, snapshot) : false;
    }
    XyRust::TranslatedDocumentSearch search(*m_document, 2, t);
    if(iSegment) *iSegment = -1;
    if(nSegments) *nSegments = search.Count();
    while(search.Pending()) {
        const int index = search.Index();
        search.Supply(search.Start() ? TranslateSegmentStart(index, fps) : TranslateSegmentEnd(index, fps));
    }
    if(search.Count() == 0 || search.Found()) {
        if(iSegment) *iSegment = search.Result();
    }
    return search.Found() ? m_document->Segment(search.Result(), snapshot) : false;
#else
    const STSSegment* segment = SearchSubs2(t, fps, iSegment, nSegments);
    if(!segment) return false;
    snapshot = *segment;
    return true;
#endif
}

bool CSimpleTextSubtitle::IsSegmentAnimated(unsigned int iSegment) const
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->IsAnimated(iSegment);
#else
    return iSegment < GetSegmentCount() ? m_segments[iSegment].animated : false;
#endif
}

void CSimpleTextSubtitle::OrSegmentAnimated(int iSegment, bool animated)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->AnimationOr(iSegment, animated);
#else
    // Callers must use the index selected for this render sequence. An invalid
    // index is a contract failure, never a silently discarded feedback update.
    m_segments.GetAt(iSegment).animated |= animated;
#endif
}

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
bool CSimpleTextSubtitle::GetDocumentSimple() const
{
    return m_document->Metadata(5) != 0;
}

void CSimpleTextSubtitle::SetDocumentSimple(bool value)
{
    m_document->SetMetadata(5, value ? 1 : 0);
}

LCID CSimpleTextSubtitle::GetDocumentLCID() const
{
    return static_cast<LCID>(m_document->LCIDValue());
}

void CSimpleTextSubtitle::SetDocumentLCID(LCID value)
{
    static_assert(sizeof(LCID) == sizeof(uint32_t), "Native LCID transport width");
    m_document->SetLCID(static_cast<uint32_t>(value));
}

bool CSimpleTextSubtitle::GetDocumentForcedDefaultStyle() const
{
    return m_document->Metadata(3) != 0;
}

void CSimpleTextSubtitle::SetDocumentForcedDefaultStyle(bool value)
{
    m_document->SetMetadata(3, value ? 1 : 0);
}

CSimpleTextSubtitle::EPARCompensationType CSimpleTextSubtitle::GetDocumentPARType() const
{
    return static_cast<EPARCompensationType>(m_document->Metadata(0));
}

void CSimpleTextSubtitle::SetDocumentPARType(EPARCompensationType value)
{
    m_document->SetMetadata(0, static_cast<int32_t>(value));
}

double CSimpleTextSubtitle::GetDocumentPARCompensation() const
{
    return m_document->PARCompensation();
}

void CSimpleTextSubtitle::SetDocumentPARCompensation(double value)
{
    m_document->SetPARCompensation(value);
}

CSimpleTextSubtitle::YCbCrMatrix CSimpleTextSubtitle::GetDocumentColorMatrix() const
{
    return static_cast<YCbCrMatrix>(m_document->Metadata(1));
}

void CSimpleTextSubtitle::SetDocumentColorMatrix(YCbCrMatrix value)
{
    m_document->SetMetadata(1, static_cast<int32_t>(value));
}

CSimpleTextSubtitle::YCbCrRange CSimpleTextSubtitle::GetDocumentColorRange() const
{
    return static_cast<YCbCrRange>(m_document->Metadata(2));
}

void CSimpleTextSubtitle::SetDocumentColorRange(YCbCrRange value)
{
    m_document->SetMetadata(2, static_cast<int32_t>(value));
}

CSize CSimpleTextSubtitle::GetDocumentLayoutSize() const
{
    return m_document->LayoutSize();
}

void CSimpleTextSubtitle::SetDocumentLayoutSize(const CSize& value)
{
    m_document->SetLayoutSize(value);
}

void CSimpleTextSubtitle::SetDocumentLayoutWidth(int value)
{
    m_document->SetScriptField(5, value);
}

void CSimpleTextSubtitle::SetDocumentLayoutHeight(int value)
{
    m_document->SetScriptField(6, value);
}

CSize CSimpleTextSubtitle::GetDocumentScreenSize() const
{
    return m_document->ScreenSize();
}

void CSimpleTextSubtitle::SetDocumentScreenSize(const CSize& value)
{
    m_document->SetScreenSize(value);
}

void CSimpleTextSubtitle::SetDocumentScreenWidth(int value)
{
    m_document->SetScriptField(0, value);
}

void CSimpleTextSubtitle::SetDocumentScreenHeight(int value)
{
    m_document->SetScriptField(1, value);
}

int CSimpleTextSubtitle::GetDocumentWrapStyle() const
{
    return m_document->ScriptField(2);
}

void CSimpleTextSubtitle::SetDocumentWrapStyle(int value)
{
    m_document->SetScriptField(2, value);
}

int CSimpleTextSubtitle::GetDocumentCollisions() const
{
    return m_document->ScriptField(3);
}

void CSimpleTextSubtitle::SetDocumentCollisions(int value)
{
    m_document->SetScriptField(3, value);
}

bool CSimpleTextSubtitle::GetDocumentScaledBAS() const
{
    return m_document->ScriptField(4) != 0;
}

void CSimpleTextSubtitle::SetDocumentScaledBAS(bool value)
{
    m_document->SetScriptField(4, value ? 1 : 0);
}

tmode CSimpleTextSubtitle::GetDocumentMode() const
{
    return static_cast<tmode>(m_document->Interpretation(0));
}

exttype CSimpleTextSubtitle::GetDocumentType() const
{
    return static_cast<exttype>(m_document->Interpretation(1));
}

CTextFile::enc CSimpleTextSubtitle::GetDocumentEncoding() const
{
    return static_cast<CTextFile::enc>(m_document->Interpretation(2));
}

void CSimpleTextSubtitle::SetDocumentMode(tmode value)
{
    m_document->SetInterpretation(0, static_cast<int32_t>(value));
}

void CSimpleTextSubtitle::SetDocumentType(exttype value)
{
    m_document->SetInterpretation(1, static_cast<int32_t>(value));
}

void CSimpleTextSubtitle::SetDocumentEncoding(CTextFile::enc value)
{
    m_document->SetInterpretation(2, static_cast<int32_t>(value));
}

CString CSimpleTextSubtitle::GetDocumentName() const
{
    return m_document->Identity(0);
}

CString CSimpleTextSubtitle::GetDocumentPath() const
{
    return m_document->Identity(1);
}

void CSimpleTextSubtitle::SetDocumentName(const CString& value)
{
    m_document->SetIdentity(0, value);
}

void CSimpleTextSubtitle::SetDocumentPath(const CString& value)
{
    m_document->SetIdentity(1, value);
}

size_t CSimpleTextSubtitle::GetStyleCount() const
{
    return m_document->NamedStyleCount();
}

bool CSimpleTextSubtitle::HasNamedStyle(LPCWSTR name) const
{
    return LookupNamedStyle(name) != NamedStyleLookup::Missing;
}

CSimpleTextSubtitle::NamedStyleLookup CSimpleTextSubtitle::LookupNamedStyle(LPCWSTR name, STSStyle* output) const
{
    if(!name) throw XyRust::DocumentStatusError(XY_DOCUMENT_V1_INVALID_INPUT);
    const uint32_t state = m_document->LookupNamedStyle(CString(name), output);
    return state == 0 ? NamedStyleLookup::Missing : state == 1 ? NamedStyleLookup::Null : NamedStyleLookup::Value;
}

bool CSimpleTextSubtitle::GetNamedStyleCopy(const CString& name, STSStyle& value) const
{
    // Named renderer reset explicitly falls back for both absent and null values.
    return LookupNamedStyle(name, &value) == NamedStyleLookup::Value;
}

bool CSimpleTextSubtitle::GetEntryStyleCopy(int i, STSStyle& value)
{
    return GetStyle(i, &value);
}

bool CSimpleTextSubtitle::SetExistingStyleValue(LPCWSTR name, const STSStyle* value)
{
    const NamedStyleLookup existing = LookupNamedStyle(name);
    if(existing == NamedStyleLookup::Missing) return false;
    ASSERT(existing != NamedStyleLookup::Null);
    // Preserve the second lookup/assert even when the caller skips this row.
    if(value)
    {
        if(existing == NamedStyleLookup::Null)
            throw XyRust::DocumentStatusError(XY_DOCUMENT_V1_UNRESOLVED_REFERENCE);
        m_document->PutNamedStyle(CString(name), *value);
    }
    return true;
}

void CSimpleTextSubtitle::ResetDefaultStyle(int CharSet)
{
    STSStyle value;
    value.charSet = CharSet;
    m_document->SetDefaultStyle(value);
}

bool CSimpleTextSubtitle::GetStyleNameAt(size_t ordinal, CString& name) const
{
    return m_document->NamedStyleNameAt(ordinal, name);
}

bool CSimpleTextSubtitle::GetStyleValueAt(size_t ordinal, STSStyle& value) const
{
    return m_document->NamedStyleValueAt(ordinal, value);
}
#endif

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
STSStyle* CSimpleTextSubtitle::GetStyle(int i)
{
    STSStyle* style = NULL;
    if (!m_fForcedDefaultStyle)
    {
        if (!m_styles.Lookup(m_entries.GetAt(i).style, style) &&
            !m_styles.Lookup(         g_default_style, style))
        {
            style = &m_defaultStyle;
        }
    }
    else
    {
        style = &m_defaultStyle;
    }
    ASSERT(style);

    return style;
}

#endif

bool CSimpleTextSubtitle::GetStyle(int i, STSStyle* const stss)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    *stss = m_document->ResolvedStyle(i);
#else
    *stss = *GetStyle(i);
#endif

    return true;
}

int CSimpleTextSubtitle::GetCharSet(int i)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    const auto resolved = m_document->ResolvedCharSet(i);
    ASSERT(resolved.has_style);
    return resolved.value;
#else
    STSStyle* style = GetStyle(i);
    return style!=NULL ? style->charSet : -1;
#endif
}

CStringW CSimpleTextSubtitle::GetStrW(int i, bool fSSA)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    const auto operation = m_document->TextOperation(i, fSSA ? 0 : 1);
    ASSERT(operation.has_style && (operation.action == 0 || operation.action == 1));
    CStringW text = operation.text;
    if(operation.action == 1) text = MBCSSSAToUnicode(text, operation.charset);
    if(operation.strip_tags) text = RemoveSSATags(text, true, operation.charset);
    return text;
#else
    STSEntry const& stse = m_entries.GetAt(i);
    int CharSet = GetCharSet(i);

    CStringW str = stse.str;

    if(!stse.fUnicode)
        str = MBCSSSAToUnicode(str, CharSet);

    if(!fSSA)
        str = RemoveSSATags(str, true, CharSet);

    return str;
#endif
}

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
void CSimpleTextSubtitle::NotifySegmentsRebuilt()
{
    OnChanged();
#if ENABLE_XY_LOG_TEXT_SUBTITLE
    CAtlArray<STSSegment> segments;
    for(size_t i = 0, count = GetSegmentCount(); i < count; i++) {
        STSSegment segment;
        m_document->Segment((int)i, segment);
        segments.Add(segment);
    }
    LogSegments(segments);
#endif
}
#endif

void CSimpleTextSubtitle::CreateSegments()
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->Rebuild();
    NotifySegmentsRebuilt();
#else
    m_segments.RemoveAll();

#ifdef XY_VSFILTER_RUST_TEXT
    {
        XyRust::Segments segments;
        for (size_t i = 0; i < GetEntryCount(); ++i) {
            const STSEntry& entry = m_entries.GetAt(i);
            segments.Add(entry.start, entry.end);
        }
        const size_t count = segments.Build();
        for (size_t i = 0; i < count; ++i) {
            STSSegment segment(segments.Start(i), segments.End(i));
            const size_t entries = segments.Count(i);
            for (size_t j = 0; j < entries; ++j) segment.subs.Add(segments.Entry(i, j));
            m_segments.Add(segment);
        }
    }
#else
    if(GetEntryCount()>0)
    {
        size_t start, mid, end;
        CAtlArray<STSSegment> tempSegments;//if add to m_segments directly, then remove empty entities can be a
                                           //complex operation when having large segmentCount and lots of empty entities
        std::vector<REFERENCE_TIME> breakpoints(2*GetEntryCount());
        for(size_t i = 0; i < GetEntryCount(); i++)
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

        for(size_t i = 0; i < GetEntryCount(); i++)
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
#endif
    OnChanged();
    LogSegments(m_segments);
#endif
}

bool CSimpleTextSubtitle::Open(CString fn, int CharSet, CString name)
{
    Empty();

    CWebTextFile f(CTextFile::UTF8);
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

static size_t CountLines(CTextFile* f, ULONGLONG from, ULONGLONG to, CString& s = CString())
{
    size_t n = 0;
    f->Seek(from, CFile::begin);
    while(f->ReadString(s) && f->GetPosition() < to) n++;
    return(n);
}

bool CSimpleTextSubtitle::Open(CTextFile* f, int CharSet, CString name)
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::OpenLifecycle run(*m_document, static_cast<uint32_t>(nOpenFuncts));
    auto operation = run.Next();
    ULONGLONG pos = 0;
    const TCHAR* func_name[]={
        TEXT("OpenSubStationAlpha"), TEXT("OpenSubRipper"), TEXT("OpenOldSubRipper"),
        TEXT("OpenSubViewer"), TEXT("OpenMicroDVD"), TEXT("OpenSami"), TEXT("OpenVTT"),
        TEXT("OpenVPlayer"), TEXT("OpenXombieSub"), TEXT("OpenUSF"), TEXT("OpenMPL2"), TEXT("OpenRealText")};
    for(;;)
    {
        switch(operation.kind)
        {
        case XyRust::OpenLifecycle::InitialEmpty:
        case XyRust::OpenLifecycle::RetryEmpty:
        case XyRust::OpenLifecycle::AbortEmpty:
            Empty();
            operation = run.Next(-1);
            break;
        case XyRust::OpenLifecycle::CapturePosition:
            pos = f->GetPosition();
            operation = run.Next(-1);
            break;
        case XyRust::OpenLifecycle::Parse:
        {
            const uint32_t i = operation.index;
            XY_LOG_INFO("Open '"<<f->GetFilePath().GetString()<<"' with "<<func_name[i]<<" begin" );
            const bool parsed = OpenFuncts[i].open(f, *this, CharSet);
            operation = run.Next(parsed ? 1 : 0);
            break;
        }
        case XyRust::OpenLifecycle::SyntaxWarning:
        {
            CString lastLine;
            size_t n = CountLines(f, pos, f->GetPosition(), lastLine);
            CString msg;
            msg.Format(_T("Unable to parse the subtitle file. Syntax error at line %Iu:\n\"%s\""), n + 1, lastLine);
            AfxMessageBox(msg, MB_OK | MB_ICONERROR);
            operation = run.Next(-1);
            break;
        }
        case XyRust::OpenLifecycle::Seek:
            f->Seek(pos, CFile::begin);
            operation = run.Next(-1);
            break;
        case XyRust::OpenLifecycle::SuccessHeader:
        {
            const uint32_t i = operation.index;
            XY_LOG_INFO("Open '"<<f->GetFilePath().GetString()<<"' with "<<func_name[i]<<" succeeded" );
            SetDocumentName(name);
            SetDocumentType(OpenFuncts[i].type);
            SetDocumentMode(OpenFuncts[i].mode);
            SetDocumentEncoding(f->GetEncoding());
            SetDocumentPath(f->GetFilePath());
            // Rust commits parser simplicity before sidecar and callbacks.
            operation = run.Next(-1);
            for(;;)
            {
                switch(operation.kind)
                {
                case XyRust::OpenLifecycle::Sidecar:
                {
                    // The cleanup closes the sidecar before rebuild callbacks.
                    CWebTextFile f2(CTextFile::UTF8);
                    if(f2.Open(f->GetFilePath() + _T(".style")))
                        OpenSubStationAlpha(&f2, *this, CharSet);
                    break;
                }
                case XyRust::OpenLifecycle::Rebuild:
                    CreateSegments();
                    break;
                case XyRust::OpenLifecycle::UnknownStyles:
                    ChangeUnknownStylesToDefault();
                    break;
                case XyRust::OpenLifecycle::DoneTrue:
                    return(true);
                default:
                    XyRust::DocumentDetail::Shape(false);
                }
                operation = run.Next(-1);
            }
        }
        case XyRust::OpenLifecycle::DoneFalse:
            return(false);
        default:
            XyRust::DocumentDetail::Shape(false);
        }
    }
#else
    Empty();

    ULONGLONG pos = f->GetPosition();

    for(int i = 0; i < nOpenFuncts; i++)
    {
         const TCHAR* func_name[]={
             TEXT("OpenSubStationAlpha"),
             TEXT("OpenSubRipper"),
             TEXT("OpenOldSubRipper"),
             TEXT("OpenSubViewer"),
             TEXT("OpenMicroDVD"),
             TEXT("OpenSami"),
             TEXT("OpenVTT"),
             TEXT("OpenVPlayer"),
             TEXT("OpenXombieSub"),
             TEXT("OpenUSF"),
             TEXT("OpenMPL2"),
             TEXT("OpenRealText")};
        XY_LOG_INFO("Open '"<<f->GetFilePath().GetString()<<"' with "<<func_name[i]<<" begin" );
        if(!OpenFuncts[i].open(f, *this, CharSet) /*|| !GetCount()*/)
        {
            if(GetEntryCount() > 0)
            {
                CString lastLine;
                size_t n = CountLines(f, pos, f->GetPosition(), lastLine);
                CString msg;
                msg.Format(_T("Unable to parse the subtitle file. Syntax error at line %Iu:\n\"%s\""), n + 1, lastLine);
                AfxMessageBox(msg, MB_OK | MB_ICONERROR);
                Empty();
                break;
            }

            f->Seek(pos, CFile::begin);
            Empty();
            continue;
        }
        XY_LOG_INFO("Open '"<<f->GetFilePath().GetString()<<"' with "<<func_name[i]<<" succeeded" );

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentName(name);
#else
        m_name = name;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentType(OpenFuncts[i].type);
#else
        m_subtitleType = OpenFuncts[i].type;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentMode(OpenFuncts[i].mode);
#else
        m_mode = OpenFuncts[i].mode;
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentEncoding(f->GetEncoding());
#else
        m_encoding = f->GetEncoding();
#endif
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentPath(f->GetFilePath());
#else
        m_path = f->GetFilePath();
#endif
        m_simple = i > 0;

        if(!m_path.IsEmpty())
        {
            CWebTextFile f2(CTextFile::UTF8);
            if(f2.Open(m_path + _T(".style")))
                OpenSubStationAlpha(&f2, *this, CharSet);
        }

        //      Sort();
        CreateSegments();

        ChangeUnknownStylesToDefault();

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        if(GetDocumentScreenSize() == CSize(0, 0)) SetDocumentScreenSize(CSize(384, 288));
#else
        if(m_dstScreenSize == CSize(0, 0)) m_dstScreenSize = CSize(384, 288);
#endif

        return(true);
    }

    return(false);
#endif
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
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    return m_document->Count() == 0;
#else
    return m_entries.IsEmpty();
#endif
}

void CSimpleTextSubtitle::RemoveAllEntries()
{
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    m_document->RemoveEntries();
#else
    m_entries.RemoveAll();
#endif
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
