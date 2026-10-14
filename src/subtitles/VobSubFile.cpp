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
#include <winioctl.h>
#include <algorithm>
#include "TextFile.h"
#include "VobSubFile.h"
#ifndef USE_UNRAR_STATIC
#include "unrar.h"
#else
#include "unrar/dll.hpp"
#endif
#include "RTS.h"

//

struct lang_type {
    unsigned short id;
    LPCSTR lang_long;
} lang_tbl[] = {
    {'--', "(Not detected)"},
    {'cc', "Closed Caption"},
    {'aa', "Afar"},
    {'ab', "Abkhazian"},
    {'af', "Afrikaans"},
    {'am', "Amharic"},
    {'ar', "Arabic"},
    {'as', "Assamese"},
    {'ay', "Aymara"},
    {'az', "Azerbaijani"},
    {'ba', "Bashkir"},
    {'be', "Byelorussian"},
    {'bg', "Bulgarian"},
    {'bh', "Bihari"},
    {'bi', "Bislama"},
    {'bn', "Bengali; Bangla"},
    {'bo', "Tibetan"},
    {'br', "Breton"},
    {'ca', "Catalan"},
    {'co', "Corsican"},
    {'cs', "Czech"},
    {'cy', "Welsh"},
    {'da', "Dansk"},
    {'de', "Deutsch"},
    {'dz', "Bhutani"},
    {'el', "Greek"},
    {'en', "English"},
    {'eo', "Esperanto"},
    {'es', "Espanol"},
    {'et', "Estonian"},
    {'eu', "Basque"},
    {'fa', "Persian"},
    {'fi', "Finnish"},
    {'fj', "Fiji"},
    {'fo', "Faroese"},
    {'fr', "Francais"},
    {'fy', "Frisian"},
    {'ga', "Irish"},
    {'gd', "Scots Gaelic"},
    {'gl', "Galician"},
    {'gn', "Guarani"},
    {'gu', "Gujarati"},
    {'ha', "Hausa"},
    {'he', "Hebrew"},
    {'hi', "Hindi"},
    {'hr', "Hrvatski"},
    {'hu', "Hungarian"},
    {'hy', "Armenian"},
    {'ia', "Interlingua"},
    {'id', "Indonesian"},
    {'ie', "Interlingue"},
    {'ik', "Inupiak"},
    {'in', "Indonesian"},
    {'is', "Islenska"},
    {'it', "Italiano"},
    {'iu', "Inuktitut"},
    {'iw', "Hebrew"},
    {'ja', "Japanese"},
    {'ji', "Yiddish"},
    {'jw', "Javanese"},
    {'ka', "Georgian"},
    {'kk', "Kazakh"},
    {'kl', "Greenlandic"},
    {'km', "Cambodian"},
    {'kn', "Kannada"},
    {'ko', "Korean"},
    {'ks', "Kashmiri"},
    {'ku', "Kurdish"},
    {'ky', "Kirghiz"},
    {'la', "Latin"},
    {'ln', "Lingala"},
    {'lo', "Laothian"},
    {'lt', "Lithuanian"},
    {'lv', "Latvian, Lettish"},
    {'mg', "Malagasy"},
    {'mi', "Maori"},
    {'mk', "Macedonian"},
    {'ml', "Malayalam"},
    {'mn', "Mongolian"},
    {'mo', "Moldavian"},
    {'mr', "Marathi"},
    {'ms', "Malay"},
    {'mt', "Maltese"},
    {'my', "Burmese"},
    {'na', "Nauru"},
    {'ne', "Nepali"},
    {'nl', "Nederlands"},
    {'no', "Norsk"},
    {'oc', "Occitan"},
    {'om', "(Afan) Oromo"},
    {'or', "Oriya"},
    {'pa', "Punjabi"},
    {'pl', "Polish"},
    {'ps', "Pashto, Pushto"},
    {'pt', "Portugues"},
    {'qu', "Quechua"},
    {'rm', "Rhaeto-Romance"},
    {'rn', "Kirundi"},
    {'ro', "Romanian"},
    {'ru', "Russian"},
    {'rw', "Kinyarwanda"},
    {'sa', "Sanskrit"},
    {'sd', "Sindhi"},
    {'sg', "Sangho"},
    {'sh', "Serbo-Croatian"},
    {'si', "Sinhalese"},
    {'sk', "Slovak"},
    {'sl', "Slovenian"},
    {'sm', "Samoan"},
    {'sn', "Shona"},
    {'so', "Somali"},
    {'sq', "Albanian"},
    {'sr', "Serbian"},
    {'ss', "Siswati"},
    {'st', "Sesotho"},
    {'su', "Sundanese"},
    {'sv', "Svenska"},
    {'sw', "Swahili"},
    {'ta', "Tamil"},
    {'te', "Telugu"},
    {'tg', "Tajik"},
    {'th', "Thai"},
    {'ti', "Tigrinya"},
    {'tk', "Turkmen"},
    {'tl', "Tagalog"},
    {'tn', "Setswana"},
    {'to', "Tonga"},
    {'tr', "Turkish"},
    {'ts', "Tsonga"},
    {'tt', "Tatar"},
    {'tw', "Twi"},
    {'ug', "Uighur"},
    {'uk', "Ukrainian"},
    {'ur', "Urdu"},
    {'uz', "Uzbek"},
    {'vi', "Vietnamese"},
    {'vo', "Volapuk"},
    {'wo', "Wolof"},
    {'xh', "Xhosa"},
    {'yi', "Yiddish"},              // formerly ji
    {'yo', "Yoruba"},
    {'za', "Zhuang"},
    {'zh', "Chinese"},
    {'zu', "Zulu"},
};

int find_lang(unsigned short id)
{
    int lo = 0, hi = _countof(lang_tbl) - 1;

    while (lo < hi) {
        int mid = (lo + hi) >> 1;

        if (id < lang_tbl[mid].id) {
            hi = mid;
        } else if (id > lang_tbl[mid].id) {
            lo = mid + 1;
        } else {
            return mid;
        }
    }

    return (id == lang_tbl[lo].id ? lo : 0);
}

//
// CVobSubFile
//

CVobSubFile::CVobSubFile(CCritSec* pLock)
    : CSubPicProviderImpl(pLock)
    , m_sub(1024 * 1024)
    , m_iLang(0)
{
}

CVobSubFile::~CVobSubFile()
{
}

//

void CVobSubFile::TrimExtension(CString& fn)
{
    int i = fn.ReverseFind('.');
    if (i > 0) {
        CString ext = fn.Mid(i).MakeLower();
        if (ext == _T(".ifo") || ext == _T(".idx") || ext == _T(".sub")
                || ext == _T(".sst") || ext == _T(".son") || ext == _T(".rar")) {
            fn = fn.Left(i);
        }
    }
}

bool CVobSubFile::Open(CString fn)
{
    TrimExtension(fn);

    do {
        Close();

        int ver;
        if (!ReadIdx(fn + _T(".idx"), ver)) {
            break;
        }

        if (ver < 6 && !ReadIfo(fn + _T(".ifo"))) {
            break;
        }

        if (!ReadSub(fn + _T(".sub")) && !ReadRar(fn + _T(".rar"))) {
            break;
        }

        m_title = fn;

        for (size_t i = 0; i < m_langs.size(); i++) {
            CAtlArray<SubPos>& sp = m_langs[i].subpos;

            for (size_t j = 0; j < sp.GetCount(); j++) {
                sp[j].stop = sp[j].start;
                sp[j].fForced = false;

                size_t packetSize = 0, dataSize = 0;
                BYTE* buff = GetPacket(j, packetSize, dataSize, i);
                if (!buff) {
                    sp[j].fValid = false;
                    continue;
                }

                m_img.delay = j + 1 < sp.GetCount() ? sp[j + 1].start - sp[j].start : 3000;
                m_img.GetPacketInfo(buff, packetSize, dataSize);
                if (j + 1 < sp.GetCount()) {
                    m_img.delay = min(m_img.delay, sp[j + 1].start - sp[j].start);
                }

                sp[j].stop = sp[j].start + m_img.delay;
                sp[j].fForced = m_img.fForced;
                sp[j].fAnimated = m_img.fAnimated;

                if (j > 0 && sp[j - 1].stop > sp[j].start) {
                    sp[j - 1].stop = sp[j].start;
                }

                delete [] buff;
            }
        }

        return true;
    } while (false);

    Close();

    return false;
}

void CVobSubFile::Close()
{
    InitSettings();
    m_title.Empty();
    m_sub.SetLength(0);
    m_img.Invalidate();
    m_iLang = -1;
    for(int i = 0; i < 32; i++)
    {
        m_langs[i].id = 0;
        m_langs[i].name.Empty();
        m_langs[i].alt.Empty();
        m_langs[i].subpos.RemoveAll();
    }
}

//

bool CVobSubFile::ReadIdx(CString fn, int& ver)
{
    CWebTextFile f;
    if (!f.Open(fn)) {
        return false;
    }

    bool fError = false;

    int id = -1, delay = 0, vobid = -1, cellid = -1;
    __int64 celltimestamp = 0;

    CString str;
    for (ptrdiff_t line = 0; !fError && f.ReadString(str); line++) {
        str.Trim();

        if (line == 0) {
            TCHAR buff[] = _T("VobSub index file, v");

            const TCHAR* s = str;

            int i = str.Find(buff);
            if (i < 0 || _stscanf_s(&s[i + _tcslen(buff)], _T("%d"), &ver) != 1
                    || ver > VOBSUBIDXVER) {
                TRACE(_T("[CVobSubFile::ReadIdx] Wrong file version!\n"));
                fError = true;
                continue;
            }
        } else if (!str.GetLength()) {
            continue;
        } else if (str[0] == _T('#')) {
            TCHAR buff[] = _T("Vob/Cell ID:");

            const TCHAR* s = str;

            int i = str.Find(buff);
            if (i >= 0) {
                _stscanf_s(&s[i + _tcslen(buff)], _T("%d, %d (PTS: %I64d)"), &vobid, &cellid, &celltimestamp);
            }

            continue;
        }

        int i = str.Find(':');
        if (i <= 0) {
            continue;
        }

        CString entry = str.Left(i).MakeLower();

        str = str.Mid(i + 1);
        str.Trim();
        if (str.IsEmpty()) {
            continue;
        }

        if (entry == _T("size")) {
            int x, y;
            if (_stscanf_s(str, _T("%dx%d"), &x, &y) != 2) {
                fError = true;
            }
            m_size.cx = x;
            m_size.cy = y;
        } else if (entry == _T("org")) {
            if (_stscanf_s(str, _T("%d,%d"), &m_x, &m_y) != 2) {
                fError = true;
            } else {
                m_org = CPoint(m_x, m_y);
            }
        } else if (entry == _T("scale")) {
            if (ver < 5) {
                int scale = 100;
                if (_stscanf_s(str, _T("%d%%"), &scale) != 1) {
                    fError = true;
                }
                m_scale_x = m_scale_y = scale;
            } else {
                if (_stscanf_s(str, _T("%d%%,%d%%"), &m_scale_x, &m_scale_y) != 2) {
                    fError = true;
                }
            }
        } else if (entry == _T("alpha")) {
            if (_stscanf_s(str, _T("%d"), &m_alpha) != 1) {
                fError = true;
            }
        } else if (entry == _T("smooth")) {
            str.MakeLower();

            if (str.Find(_T("old")) >= 0 || str.Find(_T("2")) >= 0) {
                m_fSmooth = 2;
            } else if (str.Find(_T("on")) >= 0 || str.Find(_T("1")) >= 0) {
                m_fSmooth = 1;
            } else if (str.Find(_T("off")) >= 0 || str.Find(_T("0")) >= 0) {
                m_fSmooth = 0;
            } else {
                fError = true;
            }
        } else if (entry == _T("fadein/out")) {
            if (_stscanf_s(str, _T("%d,%d"), &m_fadein, &m_fadeout) != 2) {
                fError = true;
            }
        } else if (entry == _T("align")) {
            str.MakeLower();

            int k = 0;
            CString token;
            for (int j = 0; j < 3; j++) {
                token = str.Tokenize(_T(" "), k);
                if (token.IsEmpty()) {
                    break;
                }

                if (j == 0) {
                    if (token == _T("on") || token == _T("1")) {
                        m_fAlign = true;
                    } else if (token == _T("off") || token == _T("0")) {
                        m_fAlign = false;
                    } else {
                        fError = true;
                        break;
                    }
                } else if (j == 1) {
                    if (token == _T("at")) {
                        j--;
                        continue;
                    }

                    if (token == _T("left")) {
                        m_alignhor = 0;
                    } else if (token == _T("center")) {
                        m_alignhor = 1;
                    } else if (token == _T("right")) {
                        m_alignhor = 2;
                    } else {
                        fError = true;
                        break;
                    }
                } else if (j == 2) {
                    if (token == _T("top")) {
                        m_alignver = 0;
                    } else if (token == _T("center")) {
                        m_alignver = 1;
                    } else if (token == _T("bottom")) {
                        m_alignver = 2;
                    } else {
                        fError = true;
                        break;
                    }
                }
            }
        } else if (entry == _T("time offset")) {
            bool fNegative = false;
            if (str[0] == '-') {
                fNegative = true;
            }
            str.TrimLeft(_T("+-"));

            TCHAR c;
            int hh, mm, ss, ms;
            int n = _stscanf_s(str, _T("%d%c%d%c%d%c%d"), &hh, &c, 1, &mm, &c, 1, &ss, &c, 1, &ms);

            switch (n) {
                case 1: // We have read only one integer, interpret it as an offset expressed in milliseconds
                    m_toff = hh * (fNegative ? -1 : 1);
                    break;
                case 7: // We have read 4 integers + 3 separators, interpret them as hh:mm:ss.ms
                    m_toff = (hh * 60 * 60 * 1000 + mm * 60 * 1000 + ss * 1000 + ms) * (fNegative ? -1 : 1);
                    break;
                default:
                    fError = true;
                    m_toff = 0;
                    break;
            }
        } else if (entry == _T("forced subs")) {
            str.MakeLower();

            if (str.Find(_T("on")) >= 0 || str.Find(_T("1")) >= 0) {
                m_fOnlyShowForcedSubs = true;
            } else if (str.Find(_T("off")) >= 0 || str.Find(_T("0")) >= 0) {
                m_fOnlyShowForcedSubs = false;
            } else {
                fError = true;
            }
        } else if (entry == _T("langidx")) {
            int iLang = -1;
            if (_stscanf_s(str, _T("%d"), &iLang) != 1) {
                fError = true;
            }
            m_iLang = (iLang < 0 && size_t(iLang) >= m_langs.size()) ? -1 : size_t(iLang);
        } else if (entry == _T("palette")) {
            if (_stscanf_s(str, _T("%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x"),
                           (unsigned int *)&m_orgpal[0], (unsigned int *)&m_orgpal[1], (unsigned int *)&m_orgpal[2], (unsigned int *)&m_orgpal[3],
                           (unsigned int *)&m_orgpal[4], (unsigned int *)&m_orgpal[5], (unsigned int *)&m_orgpal[6], (unsigned int *)&m_orgpal[7],
                           (unsigned int *)&m_orgpal[8], (unsigned int *)&m_orgpal[9], (unsigned int *)&m_orgpal[10], (unsigned int *)&m_orgpal[11],
                           (unsigned int *)&m_orgpal[12], (unsigned int *)&m_orgpal[13], (unsigned int *)&m_orgpal[14], (unsigned int *)&m_orgpal[15]
                          ) != 16) {
                fError = true;
            }
        } else if (entry == _T("custom colors")) {
            str.MakeLower();

            if (str.Find(_T("on")) == 0 || str.Find(_T("1")) == 0) {
                m_fCustomPal = true;
            } else if (str.Find(_T("off")) == 0 || str.Find(_T("0")) == 0) {
                m_fCustomPal = false;
            } else {
                fError = true;
            }

            i = str.Find(_T("tridx:"));
            if (i < 0) {
                fError = true;
                continue;
            }
            str = str.Mid(i + (int)_tcslen(_T("tridx:")));

            int tridx;
            if (_stscanf_s(str, _T("%x"), &tridx) != 1) {
                fError = true;
                continue;
            }
            tridx = ((tridx & 0x1000) >> 12) | ((tridx & 0x100) >> 7) | ((tridx & 0x10) >> 2) | ((tridx & 1) << 3);

            i = str.Find(_T("colors:"));
            if (i < 0) {
                fError = true;
                continue;
            }
            str = str.Mid(i + (int)_tcslen(_T("colors:")));

            RGBQUAD pal[4];
            if (_stscanf_s(str, _T("%x,%x,%x,%x"), (unsigned int *)&pal[0], (unsigned int *)&pal[1], (unsigned int *)&pal[2], (unsigned int *)&pal[3]) != 4) {
                fError = true;
                continue;
            }

            SetCustomPal(pal, tridx);
        } else if (entry == _T("id")) {
            str.MakeLower();

            WORD langid = ((str[0] & 0xff) << 8) | (str[1] & 0xff);

            i = str.Find(_T("index:"));
            if (i < 0) {
                fError = true;
                continue;
            }
            str = str.Mid(i + (int)_tcslen(_T("index:")));

            if (_stscanf_s(str, _T("%d"), &id) != 1 || id < 0 || size_t(id) >= m_langs.size()) {
                fError = true;
                continue;
            }
            if (m_iLang == -1) {
                m_iLang = size_t(id);
            }

            m_langs[id].id = langid;
            m_langs[id].name = lang_tbl[find_lang(langid)].lang_long;
            m_langs[id].alt = lang_tbl[find_lang(langid)].lang_long;

            delay = 0;
        } else if (id >= 0 && entry == _T("alt")) {
            m_langs[id].alt = str;
        } else if (id >= 0 && entry == _T("delay")) {
            bool fNegative = false;
            if (str[0] == '-') {
                fNegative = true;
            }
            str.TrimLeft(_T("+-"));

            TCHAR c;
            int hh, mm, ss, ms;
            if (_stscanf_s(str, _T("%d%c%d%c%d%c%d"), &hh, &c, 1, &mm, &c, 1, &ss, &c, 1, &ms) != 4 + 3) {
                fError = true;
                continue;
            }

            delay += (hh * 60 * 60 * 1000 + mm * 60 * 1000 + ss * 1000 + ms) * (fNegative ? -1 : 1);
        } else if (id >= 0 && entry == _T("timestamp")) {
            SubPos sb;

            sb.vobid = (char)vobid;
            sb.cellid = (char)cellid;
            sb.celltimestamp = celltimestamp;
            sb.fValid = true;

            bool fNegative = false;
            if (str[0] == '-') {
                fNegative = true;
            }
            str.TrimLeft(_T("+-"));

            TCHAR c;
            int hh, mm, ss, ms;
            if (_stscanf_s(str, _T("%d%c%d%c%d%c%d"), &hh, &c, 1, &mm, &c, 1, &ss, &c, 1, &ms) != 4 + 3) {
                fError = true;
                continue;
            }

            sb.start = (hh * 60 * 60 * 1000 + mm * 60 * 1000 + ss * 1000 + ms) * (fNegative ? -1 : 1) + delay;

            i = str.Find(_T("filepos:"));
            if (i < 0) {
                fError = true;
                continue;
            }
            str = str.Mid(i + (int)_tcslen(_T("filepos:")));

            if (_stscanf_s(str, _T("%I64x"), &sb.filepos) != 1) {
                fError = true;
                continue;
            }

            if (delay < 0 && !m_langs[id].subpos.IsEmpty()) {
                __int64 ts = m_langs[id].subpos[m_langs[id].subpos.GetCount() - 1].start;

                if (sb.start < ts) {
                    delay += (int)(ts - sb.start);
                    sb.start = ts;
                }
            }

            m_langs[id].subpos.Add(sb);
        } else {
            fError = true;
        }
    }

    return !fError;
}

bool CVobSubFile::ReadSub(CString fn)
{
    CFile f;
    if (!f.Open(fn, CFile::modeRead | CFile::typeBinary | CFile::shareDenyNone)) {
        return false;
    }

    m_sub.SetLength(f.GetLength());
    m_sub.SeekToBegin();

    int len;
    BYTE buff[2048];
    while ((len = f.Read(buff, sizeof(buff))) > 0 && *(DWORD*)buff == 0xba010000) {
        m_sub.Write(buff, len);
    }

    return true;
}

static unsigned char* RARbuff = nullptr;
static unsigned int RARpos = 0;

static int CALLBACK MyCallbackProc(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2)
{
    if (msg == UCM_PROCESSDATA) {
        ASSERT(RARbuff);

        memcpy(&RARbuff[RARpos], (char*)P1, (size_t)P2);
        RARpos += (unsigned int)P2;
    }

    return 1;
}

bool CVobSubFile::ReadRar(CString fn)
{
#ifndef USE_UNRAR_STATIC
#ifdef _WIN64
    HMODULE h = LoadLibrary(_T("unrar64.dll"));
#else
    HMODULE h = LoadLibrary(_T("unrar.dll"));
#endif
    if(!h) return(false);

    RAROpenArchiveEx OpenArchiveEx = (RAROpenArchiveEx)GetProcAddress(h, "RAROpenArchiveEx");
    RARCloseArchive  CloseArchive  = (RARCloseArchive)GetProcAddress(h, "RARCloseArchive");
    RARReadHeaderEx  ReadHeaderEx  = (RARReadHeaderEx)GetProcAddress(h, "RARReadHeaderEx");
    RARProcessFile   ProcessFile   = (RARProcessFile)GetProcAddress(h, "RARProcessFile");
    RARSetCallback   SetCallback   = (RARSetCallback)GetProcAddress(h, "RARSetCallback");

    if (!(OpenArchiveEx && CloseArchive && ReadHeaderEx && ProcessFile && SetCallback)) {
        FreeLibrary(h);
        return false;
    }

#else

#define OpenArchiveEx      RAROpenArchiveEx
#define CloseArchive       RARCloseArchive
#define ReadHeaderEx       RARReadHeaderEx
#define ProcessFile        RARProcessFile
#define SetCallback        RARSetCallback
#endif /* USE_UNRAR_STATIC */

    RAROpenArchiveDataEx OpenArchiveData;
    ZeroMemory(&OpenArchiveData, sizeof(OpenArchiveData));

    OpenArchiveData.ArcNameW = (LPTSTR)(LPCTSTR)fn;
    char fnA[MAX_PATH];
    size_t size;
    if (wcstombs_s(&size, fnA, fn, fn.GetLength())) {
        fnA[0] = 0;
    }
    OpenArchiveData.ArcName = fnA;
    OpenArchiveData.OpenMode = RAR_OM_EXTRACT;
    OpenArchiveData.CmtBuf = 0;
    OpenArchiveData.Callback = MyCallbackProc;
    HANDLE hArcData = OpenArchiveEx(&OpenArchiveData);
    if (!hArcData) {
#ifndef USE_UNRAR_STATIC
        FreeLibrary(h);
#endif
        return false;
    }

    RARHeaderDataEx HeaderDataEx;
    ZeroMemory(&HeaderDataEx, sizeof(HeaderDataEx));

    while (ReadHeaderEx(hArcData, &HeaderDataEx) == 0) {
        CString subfn(HeaderDataEx.FileNameW);

        if (!subfn.Right(4).CompareNoCase(_T(".sub"))) {
            CAutoVectorPtr<BYTE> buff;
            if (!buff.Allocate(HeaderDataEx.UnpSize)) {
                CloseArchive(hArcData);
#ifndef USE_UNRAR_STATIC
                FreeLibrary(h);
#endif
                return false;
            }

            RARbuff = buff;
            RARpos = 0;

            if (ProcessFile(hArcData, RAR_TEST, nullptr, nullptr)) {
                CloseArchive(hArcData);
#ifndef USE_UNRAR_STATIC
                FreeLibrary(h);
#endif

                return false;
            }

            m_sub.SetLength(HeaderDataEx.UnpSize);
            m_sub.SeekToBegin();
            m_sub.Write(buff, HeaderDataEx.UnpSize);
            m_sub.SeekToBegin();

            RARbuff = nullptr;
            RARpos = 0;

            break;
        }

        ProcessFile(hArcData, RAR_SKIP, nullptr, nullptr);
    }

    CloseArchive(hArcData);
#ifndef USE_UNRAR_STATIC
    FreeLibrary(h);
#endif

    return true;
}

bool CVobSubFile::ReadIfo(CString fn)
{
    CFile f;
    if (!f.Open(fn, CFile::modeRead | CFile::typeBinary | CFile::shareDenyNone)) {
        return false;
    }

    /* PGC1 */

    f.Seek(0xc0 + 0x0c, SEEK_SET);

    DWORD pos;
    ReadBEdw(pos);

    f.Seek(pos * 0x800 + 0x0c, CFile::begin);

    DWORD offset;
    ReadBEdw(offset);

    /* Subpic palette */

    f.Seek(pos * 0x800 + offset + 0xa4, CFile::begin);

    for (size_t i = 0; i < 16; i++) {
        BYTE y, u, v, tmp;

        f.Read(&tmp, 1);
        f.Read(&y, 1);
        f.Read(&u, 1);
        f.Read(&v, 1);

        y = (y - 16) * 255 / 219;

        m_orgpal[i].rgbRed = (BYTE)min(max(1.0*y + 1.4022*(u-128), 0), 255);
        m_orgpal[i].rgbGreen = (BYTE)min(max(1.0*y - 0.3456*(u-128) - 0.7145*(v-128), 0), 255);
        m_orgpal[i].rgbBlue = (BYTE)min(max(1.0*y + 1.7710*(v-128), 0) , 255);
    }

    return true;
}

//

BYTE* CVobSubFile::GetPacket(size_t idx, size_t& packetSize, size_t& dataSize, size_t iLang /*= SIZE_T_ERROR*/)
{
    BYTE* ret = nullptr;

    if (iLang >= m_langs.size()) {
        iLang = m_iLang;
    }
    CAtlArray<SubPos>& sp = m_langs[iLang].subpos;

    do {
        if (idx >= sp.GetCount()) {
            break;
        }

        if ((__int64)m_sub.Seek(sp[idx].filepos, CFile::begin) != sp[idx].filepos) {
            break;
        }

        BYTE buff[0x800];
        if (sizeof(buff) != m_sub.Read(buff, sizeof(buff))) {
            break;
        }

        ASSERT(iLang < BYTE_MAX);

        // let's check a few things to make sure...
        if (*(DWORD*)&buff[0x00] != 0xba010000
                || *(DWORD*)&buff[0x0e] != 0xbd010000
                || !(buff[0x15] & 0x80)
                || (buff[0x17] & 0xf0) != 0x20
                || (buff[buff[0x16] + 0x17] & 0xe0) != 0x20
                || (buff[buff[0x16] + 0x17] & 0x1f) != (BYTE)iLang) {
            break;
        }

        packetSize = (buff[buff[0x16] + 0x18] << 8) + buff[buff[0x16] + 0x19];
        dataSize = (buff[buff[0x16] + 0x1a] << 8) + buff[buff[0x16] + 0x1b];

        try {
            ret = DEBUG_NEW BYTE[packetSize];
        } catch (CMemoryException* e) {
            ASSERT(FALSE);
            e->Delete();
            break;
        }

        size_t i = 0, sizeLeft = packetSize;
        for (size_t size; i < packetSize; i += size, sizeLeft -= size) {
            size_t hsize = 0x18 + buff[0x16];
            size = min(sizeLeft, 0x800 - hsize);
            memcpy(&ret[i], &buff[hsize], size);

            if (size != sizeLeft) {
                while (m_sub.Read(buff, sizeof(buff))) {
                    if (/*!(buff[0x15] & 0x80) &&*/ buff[buff[0x16] + 0x17] == (iLang | 0x20)) {
                        break;
                    }
                }
            }
        }

        if (i != packetSize || sizeLeft > 0) {
            delete [] ret;
            ret = nullptr;
        }
    } while (false);

    return ret;
}

const CVobSubFile::SubPos* CVobSubFile::GetFrameInfo(size_t idx, size_t iLang /*= SIZE_T_ERROR*/) const
{
    if (iLang >= m_langs.size()) {
        iLang = m_iLang;
    }
    const CAtlArray<SubPos>& sp = m_langs[iLang].subpos;

    if (idx >= sp.GetCount()
            || !sp[idx].fValid
            || (m_fOnlyShowForcedSubs && !sp[idx].fForced)) {
        return nullptr;
    }

    return &sp[idx];
}

bool CVobSubFile::GetFrame(size_t idx, size_t iLang /*= SIZE_T_ERROR*/, REFERENCE_TIME rt /*= -1*/)
{
    if (iLang >= m_langs.size()) {
        iLang = m_iLang;
    }
    CAtlArray<SubPos>& sp = m_langs[iLang].subpos;

    if (idx >= sp.GetCount()) {
        return false;
    }

    if (m_img.iLang != iLang || m_img.iIdx != idx
            || (sp[idx].fAnimated && sp[idx].start + m_img.tCurrent <= rt)) {
        size_t packetSize = 0, dataSize = 0;
        CAutoVectorPtr<BYTE> buff;
        buff.Attach(GetPacket(idx, packetSize, dataSize, iLang));
        if (!buff || packetSize == 0 || dataSize == 0) {
            return false;
        }

        m_img.start = sp[idx].start;

        bool ret = m_img.Decode(buff, packetSize, dataSize, rt >= 0 ? int(rt - sp[idx].start) : INT_MAX,
                                m_fCustomPal, m_tridx, m_orgpal, m_cuspal, true);

        m_img.delay = sp[idx].stop - sp[idx].start;

        if (!ret) {
            return false;
        }

        m_img.iIdx = idx;
        m_img.iLang = iLang;
    }

    return (m_fOnlyShowForcedSubs ? m_img.fForced : true);
}

size_t CVobSubFile::GetFrameIdxByTimeStamp(__int64 time)
{
    if (m_iLang >= m_langs.size() || m_langs[m_iLang].subpos.IsEmpty()) {
        return -1;
    }

    CAtlArray<SubPos>& sp = m_langs[m_iLang].subpos;

    size_t i = 0, j = sp.GetCount() - 1, ret = -1;

    if (time >= sp[j].start) {
        return j;
    }

    while (i < j) {
        size_t mid = (i + j) >> 1;
        __int64 midstart = sp[mid].start;

        if (time == midstart) {
            ret = mid;
            break;
        } else if (time < midstart) {
            ret = -1;
            if (j == mid) {
                mid--;
            }
            j = mid;
        } else if (time > midstart) {
            ret = mid;
            if (i == mid) {
                mid++;
            }
            i = mid;
        }
    }

    return ret;
}

//

STDMETHODIMP CVobSubFile::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);
    *ppv = nullptr;

    return
        QI(IPersist)
        QI(ISubStream)
        QI(ISubPicProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicProvider

STDMETHODIMP_(POSITION) CVobSubFile::GetStartPosition(REFERENCE_TIME rt, double fps)
{
    rt /= 10000;

    size_t i = GetFrameIdxByTimeStamp(rt);

    const SubPos* sp = GetFrameInfo(i);
    if (!sp) {
        return nullptr;
    }

    if (rt >= sp->stop) {
        if (!GetFrameInfo(++i)) {
            return nullptr;
        }
    }

    return (POSITION)(i + 1);
}

STDMETHODIMP_(POSITION) CVobSubFile::GetNext(POSITION pos)
{
    size_t i = (size_t)pos;
    return (GetFrameInfo(i) ? (POSITION)(i + 1) : nullptr);
}

STDMETHODIMP_(REFERENCE_TIME) CVobSubFile::GetStart(POSITION pos, double fps)
{
    size_t i = (size_t)pos - 1;
    const SubPos* sp = GetFrameInfo(i);
    return (sp ? 10000i64 * sp->start : 0);
}

STDMETHODIMP_(REFERENCE_TIME) CVobSubFile::GetStop(POSITION pos, double fps)
{
    size_t i = (size_t)pos - 1;
    const SubPos* sp = GetFrameInfo(i);
    return (sp ? 10000i64 * sp->stop : 0);
}

STDMETHODIMP_(bool) CVobSubFile::IsAnimated(POSITION pos)
{
    size_t i = (size_t)pos - 1;
    const SubPos* sp = GetFrameInfo(i);
    return (sp ? sp->fAnimated : false);
}

STDMETHODIMP CVobSubFile::Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECT& bbox)
{
    if (spd.bpp != 32) {
        return E_INVALIDARG;
    }

    rt /= 10000;

    if (!GetFrame(GetFrameIdxByTimeStamp(rt), -1, rt)) {
        return E_FAIL;
    }

    if (rt >= (m_img.start + m_img.delay)) {
        return E_FAIL;
    }

    return __super::Render(spd, bbox);
}

// IPersist

STDMETHODIMP CVobSubFile::GetClassID(CLSID* pClassID)
{
    return pClassID ? *pClassID = __uuidof(this), S_OK : E_POINTER;
}

// ISubStream

STDMETHODIMP_(int) CVobSubFile::GetStreamCount()
{
    int iStreamCount = 0;
    for(int i = 0; i < 32; i++) {
        if(m_langs[i].subpos.GetCount()) {
            iStreamCount++;
        }
    }
    return iStreamCount;
}

STDMETHODIMP CVobSubFile::GetStreamInfo(int iStream, WCHAR** ppName, LCID* pLCID)
{
    for(int i = 0; i < 32; i++)
    {
        SubLang& sl = m_langs[i];

        if (sl.subpos.IsEmpty() || iStream-- > 0) {
            continue;
        }

        if (ppName) {
            *ppName = (WCHAR*)CoTaskMemAlloc((sl.alt.GetLength() + 1) * sizeof(WCHAR));
            if (!(*ppName)) {
                return E_OUTOFMEMORY;
            }

            wcscpy_s(*ppName, sl.alt.GetLength() + 1, CStringW(sl.alt));
        }

        if (pLCID) {
            *pLCID = 0; // TODO: make lcid out of "sl.id"
        }

        return S_OK;
    }

    return E_FAIL;
}

STDMETHODIMP_(int) CVobSubFile::GetStream()
{
    int iStream = 0;

    if (m_iLang < m_langs.size()) {
        for (size_t i = 0; i < m_iLang; i++) {
            if (!m_langs[i].subpos.IsEmpty()) {
                iStream++;
            }
        }
    }

    return iStream;
}

STDMETHODIMP CVobSubFile::SetStream(int iStream)
{
    for (size_t i = 0; i < m_langs.size(); i++) {
        const CAtlArray<SubPos>& sp = m_langs[i].subpos;

        if (sp.IsEmpty() || iStream-- > 0) {
            continue;
        }

        m_iLang = i;

        m_img.Invalidate();

        break;
    }

    return iStream < 0 ? S_OK : E_FAIL;
}

STDMETHODIMP CVobSubFile::Reload()
{
    CFileStatus s;
    if(!CFile::GetStatus(m_title + _T(".idx"), s)) return E_FAIL;
    return !m_title.IsEmpty() && Open(m_title) ? S_OK : E_FAIL;
}

// StretchBlt

static void PixelAtBiLinear(RGBQUAD& c, int x, int y, CVobSubImage& src)
{
    int w = src.rect.Width(),
        h = src.rect.Height();

    int x1 = (x >> 16), y1 = (y >> 16) * w,
        x2 = min(x1 + 1, w - 1), y2 = min(y1 + w, (h - 1) * w);

    RGBQUAD* ptr = src.lpPixels;

    RGBQUAD c11 = ptr[y1 + x1], c12 = ptr[y1 + x2],
            c21 = ptr[y2 + x1], c22 = ptr[y2 + x2];

    __int64 u2 = x & 0xffff,
            v2 = y & 0xffff,
            u1 = 0x10000 - u2,
            v1 = 0x10000 - v2;

    int v1u1 = int(v1 * u1 >> 16) * c11.rgbReserved,
        v1u2 = int(v1 * u2 >> 16) * c12.rgbReserved,
        v2u1 = int(v2 * u1 >> 16) * c21.rgbReserved,
        v2u2 = int(v2 * u2 >> 16) * c22.rgbReserved;

    c.rgbRed = (c11.rgbRed * v1u1 + c12.rgbRed * v1u2
                + c21.rgbRed * v2u1 + c22.rgbRed * v2u2) >> 24;
    c.rgbGreen = (c11.rgbGreen * v1u1 + c12.rgbGreen * v1u2
                  + c21.rgbGreen * v2u1 + c22.rgbGreen * v2u2) >> 24;
    c.rgbBlue = (c11.rgbBlue * v1u1 + c12.rgbBlue * v1u2
                 + c21.rgbBlue * v2u1 + c22.rgbBlue * v2u2) >> 24;
    c.rgbReserved = BYTE((v1u1 + v1u2
                          + v2u1 + v2u2) >> 16);
}

static void StretchBlt(SubPicDesc& spd, CRect dstrect, CVobSubImage& src)
{
    if (dstrect.IsRectEmpty()) {
        return;
    }

    if ((dstrect & CRect(0, 0, spd.w, spd.h)).IsRectEmpty()) {
        return;
    }

    int sw = src.rect.Width(),
        sh = src.rect.Height(),
        dw = dstrect.Width(),
        dh = dstrect.Height();

    int srcx = 0,
        srcy = 0,
        srcdx = (sw << 16) / dw >> 1,
        srcdy = (sh << 16) / dh >> 1;

    if (dstrect.left < 0) {
        srcx = -dstrect.left * (srcdx << 1);
        dstrect.left = 0;
    }
    if (dstrect.top < 0) {
        srcy = -dstrect.top * (srcdy << 1);
        dstrect.top = 0;
    }
    if (dstrect.right > spd.w) {
        dstrect.right = spd.w;
    }
    if (dstrect.bottom > spd.h) {
        dstrect.bottom = spd.h;
    }

    if ((dstrect & CRect(0, 0, spd.w, spd.h)).IsRectEmpty()) {
        return;
    }

    dw = dstrect.Width();
    dh = dstrect.Height();

    for (int y = dstrect.top; y < dstrect.bottom; y++, srcy += (srcdy << 1)) {
        RGBQUAD* ptr = (RGBQUAD*) & ((BYTE*)spd.bits)[y * spd.pitch] + dstrect.left;
        RGBQUAD* endptr = ptr + dw;

        for (int sx = srcx; ptr < endptr; sx += (srcdx << 1), ptr++) {
            //          PixelAtBiLinear(*ptr, sx, srcy, src);
            ////
            RGBQUAD cc[4];

            PixelAtBiLinear(cc[0], sx,         srcy,         src);
            PixelAtBiLinear(cc[1], sx + srcdx, srcy,         src);
            PixelAtBiLinear(cc[2], sx,         srcy + srcdy, src);
            PixelAtBiLinear(cc[3], sx + srcdx, srcy + srcdy, src);

            ptr->rgbRed = (cc[0].rgbRed + cc[1].rgbRed + cc[2].rgbRed + cc[3].rgbRed) >> 2;
            ptr->rgbGreen = (cc[0].rgbGreen + cc[1].rgbGreen + cc[2].rgbGreen + cc[3].rgbGreen) >> 2;
            ptr->rgbBlue = (cc[0].rgbBlue + cc[1].rgbBlue + cc[2].rgbBlue + cc[3].rgbBlue) >> 2;
            ptr->rgbReserved = (cc[0].rgbReserved + cc[1].rgbReserved + cc[2].rgbReserved + cc[3].rgbReserved) >> 2;
            ////
            ptr->rgbRed = ptr->rgbRed * ptr->rgbReserved >> 8;
            ptr->rgbGreen = ptr->rgbGreen * ptr->rgbReserved >> 8;
            ptr->rgbBlue = ptr->rgbBlue * ptr->rgbReserved >> 8;
            ptr->rgbReserved = ~ptr->rgbReserved;

        }
    }
}

//
// CVobSubSettings
//

void CVobSubSettings::InitSettings()
{
    m_size.SetSize(720, 480);
    m_toff = m_x = m_y = 0;
    m_org.SetPoint(0, 0);
    m_scale_x = m_scale_y = m_alpha = 100;
    m_fadein = m_fadeout = 50;
    m_fSmooth = 0;
    m_fAlign = false;
    m_alignhor = m_alignver = 0;
    m_fOnlyShowForcedSubs = false;
    m_fCustomPal = false;
    m_tridx = 0;
    ZeroMemory(m_orgpal, sizeof(m_orgpal));
    ZeroMemory(m_cuspal, sizeof(m_cuspal));
}

void CVobSubSettings::SetCustomPal(const RGBQUAD* cuspal, int tridx)
{
    memcpy(m_cuspal, cuspal, sizeof(RGBQUAD) * 4);
    m_tridx = tridx & 0xf;
    for (int i = 0; i < 4; i++) {
        m_cuspal[i].rgbReserved = (tridx & (1 << i)) ? 0 : 0xff;
    }
    m_img.Invalidate();
}

void CVobSubSettings::GetDestrect(CRect& r)
{
    int w = MulDiv(m_img.rect.Width(), m_scale_x, 100);
    int h = MulDiv(m_img.rect.Height(), m_scale_y, 100);

    if (!m_fAlign) {
        r.left = MulDiv(m_img.rect.left, m_scale_x, 100);
        r.right = MulDiv(m_img.rect.right, m_scale_x, 100);
        r.top = MulDiv(m_img.rect.top, m_scale_y, 100);
        r.bottom = MulDiv(m_img.rect.bottom, m_scale_y, 100);
    } else {
        switch (m_alignhor) {
            case 0:
                r.left = 0;
                r.right = w;
                break; // left
            case 1:
                r.left = -(w >> 1);
                r.right = -(w >> 1) + w;
                break; // center
            case 2:
                r.left = -w;
                r.right = 0;
                break; // right
            default:
                r.left = MulDiv(m_img.rect.left, m_scale_x, 100);
                r.right = MulDiv(m_img.rect.right, m_scale_x, 100);
                break;
        }

        switch (m_alignver) {
            case 0:
                r.top = 0;
                r.bottom = h;
                break; // top
            case 1:
                r.top = -(h >> 1);
                r.bottom = -(h >> 1) + h;
                break; // center
            case 2:
                r.top = -h;
                r.bottom = 0;
                break; // bottom
            default:
                r.top = MulDiv(m_img.rect.top, m_scale_y, 100);
                r.bottom = MulDiv(m_img.rect.bottom, m_scale_y, 100);
                break;
        }
    }

    r += m_org;
}

void CVobSubSettings::GetDestrect(CRect& r, int w, int h)
{
    GetDestrect(r);

    r.left = MulDiv(r.left, w, m_size.cx);
    r.right = MulDiv(r.right, w, m_size.cx);
    r.top = MulDiv(r.top, h, m_size.cy);
    r.bottom = MulDiv(r.bottom, h, m_size.cy);
}

void CVobSubSettings::SetAlignment(bool fAlign, int x, int y, int hor /*= 1*/, int ver /*= 1*/)
{
    m_fAlign = fAlign;
    if (fAlign) {
        m_org.x = MulDiv(m_size.cx, x, 100);
        m_org.y = MulDiv(m_size.cy, y, 100);
        m_alignhor = min(max(hor, 0), 2);
        m_alignver = min(max(ver, 0), 2);
    } else {
        m_org.x = m_x;
        m_org.y = m_y;
    }
}

HRESULT CVobSubSettings::Render(SubPicDesc& spd, RECT& bbox)
{
    CRect r;
    GetDestrect(r, spd.w, spd.h);
    StretchBlt(spd, r, m_img);
    r &= CRect(CPoint(0, 0), CSize(spd.w, spd.h));
    bbox = r;
    return !r.IsRectEmpty() ? S_OK : S_FALSE;
}

//
// CVobSubStream
//

CVobSubStream::CVobSubStream(CCritSec* pLock)
    : CSubPicProviderImpl(pLock)
{
}

CVobSubStream::~CVobSubStream()
{
}

void CVobSubStream::Open(CString name, BYTE* pData, int len)
{
    CAutoLock cAutoLock(&m_csSubPics);

    m_name = name;

    CAtlList<CString> lines;
    Explode(CString(CStringA((CHAR*)pData, len)), lines, '\n');
    while (lines.GetCount()) {
        CAtlList<CString> sl;
        Explode(lines.RemoveHead(), sl, ':', 2);
        if (sl.GetCount() != 2) {
            continue;
        }
        CString key = sl.GetHead();
        CString value = sl.GetTail();
        if (key == _T("size")) {
            _stscanf_s(value, _T("%dx %d"), &m_size.cx, &m_size.cy);
        } else if (key == _T("org")) {
            _stscanf_s(value, _T("%d, %d"), &m_org.x, &m_org.y);
        } else if (key == _T("scale")) {
            _stscanf_s(value, _T("%d%%, %d%%"), &m_scale_x, &m_scale_y);
        } else if (key == _T("alpha")) {
            _stscanf_s(value, _T("%d%%"), &m_alpha);
        } else if (key == _T("smooth"))
            m_fSmooth =
                value == _T("0") || value == _T("OFF") ? 0 :
                value == _T("1") || value == _T("ON") ? 1 :
                value == _T("2") || value == _T("OLD") ? 2 :
                0;
        else if (key == _T("align")) {
            Explode(value, sl, ' ');
            if (sl.GetCount() == 4) {
                sl.RemoveAt(sl.FindIndex(1));
            }
            if (sl.GetCount() == 3) {
                m_fAlign = sl.RemoveHead() == _T("ON");
                CString hor = sl.GetHead(), ver = sl.GetTail();
                m_alignhor = hor == _T("LEFT") ? 0 : hor == _T("CENTER") ? 1 : hor == _T("RIGHT") ? 2 : 1;
                m_alignver = ver == _T("TOP") ? 0 : ver == _T("CENTER") ? 1 : /*ver == _T("BOTTOM") ? 2 :*/ 2;
            }
        } else if (key == _T("fade in/out")) {
            _stscanf_s(value, _T("%d, %d"), &m_fadein, &m_fadeout);
        } else if (key == _T("time offset")) {
            m_toff = _tcstol(value, nullptr, 10);
        } else if (key == _T("forced subs")) {
            m_fOnlyShowForcedSubs = value == _T("1") || value == _T("ON");
        } else if (key == _T("palette")) {
            Explode(value, sl, ',', 16);
            for (size_t i = 0; i < 16 && sl.GetCount(); i++) {
                *(DWORD*)&m_orgpal[i] = _tcstol(sl.RemoveHead(), nullptr, 16);
            }
        } else if (key == _T("custom colors")) {
            m_fCustomPal = Explode(value, sl, ',', 3) == _T("ON");
            if (sl.GetCount() == 3) {
                sl.RemoveHead();
                CAtlList<CString> tridx, colors;

                Explode(sl.RemoveHead(), tridx, ':', 2);
                int _tridx = 1;
                if (tridx.RemoveHead() == _T("tridx")) {
                    TCHAR tr[4];
                    _stscanf_s(tridx.RemoveHead(), _T("%c%c%c%c"), &tr[0], 1, &tr[1], 1, &tr[2], 1, &tr[3], 1);
                    for (size_t i = 0; i < 4; i++) {
                        _tridx |= ((tr[i] == '1') ? 1 : 0) << i;
                    }
                }

                Explode(sl.RemoveHead(), colors, ':', 2);
                if (colors.RemoveHead() == _T("colors")) {
                    Explode(colors.RemoveHead(), colors, ',', 4);

                    RGBQUAD pal[4];
                    for (size_t i = 0; i < 4 && colors.GetCount(); i++) {
                        *(DWORD*)&pal[i] = _tcstol(colors.RemoveHead(), nullptr, 16);
                    }

                    SetCustomPal(pal, _tridx);
                }
            }
        }
    }
}

void CVobSubStream::Add(REFERENCE_TIME tStart, REFERENCE_TIME tStop, BYTE* pData, int len)
{
    if (len <= 4 || ((pData[0] << 8) | pData[1]) != len) {
        return;
    }

    CVobSubImage vsi;
    vsi.GetPacketInfo(pData, (pData[0] << 8) | pData[1], (pData[2] << 8) | pData[3]);

    CAutoPtr<SubPic> p(DEBUG_NEW SubPic());
    p->tStart = tStart;
    p->tStop = vsi.delay > 0 ? (tStart + 10000i64 * vsi.delay) : tStop;
    p->fAnimated = vsi.fAnimated;
    p->pData.SetCount(len);
    memcpy(p->pData.GetData(), pData, p->pData.GetCount());

    CAutoLock cAutoLock(&m_csSubPics);
    while (m_subpics.GetCount() && m_subpics.GetTail()->tStart >= tStart) {
        m_subpics.RemoveTail();
        m_img.iIdx = -1;
    }

    // We can only render one subpicture at a time, thus if there is overlap
    // we have to fix it. tStop = tStart seems to work.
    if (m_subpics.GetCount() && m_subpics.GetTail()->tStop > p->tStart) {
        TRACE(_T("[CVobSubStream::Add] Vobsub timestamp overlap detected! ")
              _T("Subpicture #%d, StopTime %I64d > %I64d (Next StartTime), making them equal!\n"),
              m_subpics.GetCount(), m_subpics.GetTail()->tStop, p->tStart);
        m_subpics.GetTail()->tStop = p->tStart;
    }

    m_subpics.AddTail(p);
}

void CVobSubStream::RemoveAll()
{
    CAutoLock cAutoLock(&m_csSubPics);
    m_subpics.RemoveAll();
    m_img.iIdx = -1;
}

STDMETHODIMP CVobSubStream::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);
    *ppv = nullptr;

    return
        QI(IPersist)
        QI(ISubStream)
        QI(ISubPicProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicProvider

STDMETHODIMP_(POSITION) CVobSubStream::GetStartPosition(REFERENCE_TIME rt, double fps)
{
    CAutoLock cAutoLock(&m_csSubPics);
    POSITION pos = m_subpics.GetTailPosition();
    for (; pos; m_subpics.GetPrev(pos)) {
        SubPic* sp = m_subpics.GetAt(pos);
        if (sp->tStart <= rt) {
            if (sp->tStop <= rt) {
                m_subpics.GetNext(pos);
            }
            break;
        }
    }
    return pos;
}

STDMETHODIMP_(POSITION) CVobSubStream::GetNext(POSITION pos)
{
    CAutoLock cAutoLock(&m_csSubPics);
    m_subpics.GetNext(pos);
    return pos;
}

STDMETHODIMP_(REFERENCE_TIME) CVobSubStream::GetStart(POSITION pos, double fps)
{
    CAutoLock cAutoLock(&m_csSubPics);
    return m_subpics.GetAt(pos)->tStart;
}

STDMETHODIMP_(REFERENCE_TIME) CVobSubStream::GetStop(POSITION pos, double fps)
{
    CAutoLock cAutoLock(&m_csSubPics);
    return m_subpics.GetAt(pos)->tStop;
}

STDMETHODIMP_(bool) CVobSubStream::IsAnimated(POSITION pos)
{
    CAutoLock cAutoLock(&m_csSubPics);
    return m_subpics.GetAt(pos)->fAnimated;
}

STDMETHODIMP CVobSubStream::Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECT& bbox)
{
    if (spd.bpp != 32) {
        return E_INVALIDARG;
    }

    for (POSITION pos = m_subpics.GetTailPosition(); pos; m_subpics.GetPrev(pos)) {
        SubPic* sp = m_subpics.GetAt(pos);
        if (sp->tStart <= rt && rt < sp->tStop) {
            if (m_img.iIdx != (size_t)pos || (sp->fAnimated && sp->tStart + m_img.tCurrent * 10000i64 <= rt)) {
                BYTE* pData = sp->pData.GetData();
                m_img.Decode(
                    pData, (pData[0] << 8) | pData[1], (pData[2] << 8) | pData[3], int((rt - sp->tStart) / 10000i64),
                    m_fCustomPal, m_tridx, m_orgpal, m_cuspal, true);
                m_img.iIdx = (size_t)pos;
            }

            return __super::Render(spd, bbox);
        }
    }

    return E_FAIL;
}

// IPersist

STDMETHODIMP CVobSubStream::GetClassID(CLSID* pClassID)
{
    return pClassID ? *pClassID = __uuidof(this), S_OK : E_POINTER;
}

// ISubStream

STDMETHODIMP_(int) CVobSubStream::GetStreamCount()
{
    return 1;
}

STDMETHODIMP CVobSubStream::GetStreamInfo(int i, WCHAR** ppName, LCID* pLCID)
{
    CAutoLock cAutoLock(&m_csSubPics);

    if (ppName) {
        *ppName = (WCHAR*)CoTaskMemAlloc((m_name.GetLength() + 1) * sizeof(WCHAR));
        if (!(*ppName)) {
            return E_OUTOFMEMORY;
        }
        wcscpy_s(*ppName, m_name.GetLength() + 1, CStringW(m_name));
    }

    if (pLCID) {
        *pLCID = 0; // TODO
    }

    return S_OK;
}

STDMETHODIMP_(int) CVobSubStream::GetStream()
{
    return 0;
}

STDMETHODIMP CVobSubStream::SetStream(int iStream)
{
    return iStream == 0 ? S_OK : E_FAIL;
}
