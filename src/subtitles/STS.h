/*
*	Copyright (C) 2003-2006 Gabest
*	http://www.gabest.org
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

#pragma once

#include <atlcoll.h>
#include <wxutil.h>
#include "TextFile.h"
#include "GFN.h"
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#ifndef XY_VSFILTER_RUST_TEXT
#error XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL requires XY_VSFILTER_RUST_TEXT
#endif
#include <memory>
namespace XyRust { class DocumentOwner; }
#endif

typedef enum {TIME, FRAME} tmode; // the meaning of STSEntry::start/end

//Coordinate System 2: target scale applied

typedef int    IntCoor2;
typedef float  FloatCoor2;
typedef double DoubleCoor2;
typedef POINT  POINTCoor2;
typedef CPoint CPointCoor2;
typedef SIZE   SIZECoor2;
typedef CSize  CSizeCoor2;
typedef RECT   RECTCoor2;
typedef CRect  CRectCoor2;
typedef FwRect FwRectCoor2;

struct STSStyleBase
{
    int     charSet;
    CString fontName;
    double  fontSize;  // height
    int     fontWeight;
    bool    fItalic;
    bool    fUnderline;
    bool    fStrikeOut;

    bool operator == (const STSStyleBase& s)const;
};

LOGFONTA& operator <<= (LOGFONTA& lfa, const STSStyleBase& s);
LOGFONTW& operator <<= (LOGFONTW& lfw, const STSStyleBase& s);

struct STSStyle: public STSStyleBase
{
public:
    FwRect   marginRect;   // measured from the sides
    int      scrAlignment; // 1 - 9: as on the numpad, 0: default
    int      borderStyle;  // 0: outline, 1: opaque box
    double   outlineWidthX, outlineWidthY;
    double   shadowDepthX, shadowDepthY;
    COLORREF colors[4];    // usually: {primary, secondary, outline/background, shadow}
    BYTE     alpha[4];

    double   fontScaleX, fontScaleY; // percent
    double   fontSpacing;  // +/- pixels

    double   fBlur;
    double   fGaussianBlur;
    double   fontAngleZ, fontAngleX, fontAngleY;
    double   fontShiftX, fontShiftY;
    int      relativeTo;   // 0: window, 1: video, 2: undefined (~window)
                           // Edit: NOT supported now

    STSStyle();

    void SetDefault();

	bool operator == (const STSStyle& s)const;
    bool operator != (const STSStyle& s) const {
        return !(*this == s);
    };
	bool IsFontStyleEqual(const STSStyle& s) const;

    void operator = (const LOGFONT& lf);

    friend CString& operator <<= (CString& style, const STSStyle& s);
    friend STSStyle& operator <<= (STSStyle& s, const CString& style);
};

typedef XyInterned<STSStyle> FwSTSStyle;

//for FwSTSStyle
static inline std::size_t hash_value(const STSStyleBase& s)
{    
    std::size_t hash = CStringElementTraits<CString>::Hash(s.fontName);
    hash = (hash<<5) + (hash) + s.charSet;
    hash = (hash<<5) + (hash) + hash_value(s.fontSize);
    hash = (hash<<5) + (hash) + s.fontWeight;
    hash = (hash<<5) + (hash) + s.fItalic;          //Todo: fix me
    hash = (hash<<5) + (hash) + s.fUnderline;
    hash = (hash<<5) + (hash) + s.fStrikeOut;
    return  hash;
}

static inline std::size_t hash_value(const STSStyle& s)
{
    //Todo: fix me
    std::size_t hash = hash_value(static_cast<const STSStyleBase&>(s));
    hash = (hash<<5) + (hash) + s.colors[0];
    hash = (hash<<5) + (hash) + s.colors[2];
    hash = (hash<<5) + (hash) + hash_value(s.fontScaleX);
    hash = (hash<<5) + (hash) + hash_value(s.fontScaleY);
    hash = (hash<<5) + (hash) + hash_value(s.fontAngleX);
    hash = (hash<<5) + (hash) + hash_value(s.fontAngleY);
    hash = (hash<<5) + (hash) + hash_value(s.fontAngleZ);
    hash = (hash<<5) + (hash) + hash_value(s.fontShiftX);
    hash = (hash<<5) + (hash) + hash_value(s.fontShiftY);

    return  hash;
}

class CSTSStyleMap : public CAtlMap<CString, STSStyle*, CStringElementTraits<CString> >
{
public:
    CSTSStyleMap() {}
    virtual ~CSTSStyleMap() {Free();}
    void Free();
};

typedef struct
{
    CStringW str;
    bool     fUnicode;
    CString  style, actor, effect;
    CRect    marginRect;
    int      layer;
    REFERENCE_TIME start, end;
    int      readorder;
} STSEntry;

class STSSegment
{
public:
    REFERENCE_TIME start, end;
    bool animated; //if this segment has animate effect
    CAtlArray<int> subs;

    STSSegment() {animated=false;}
    STSSegment(REFERENCE_TIME s, REFERENCE_TIME e) {start = s; end = e; animated = false;}
    STSSegment(const STSSegment& stss) {*this = stss;}
    void operator = (const STSSegment& stss) {start = stss.start; end = stss.end; animated = stss.animated; subs.Copy(stss.subs);}
};

class CSimpleTextSubtitle
{
    friend class CSubtitleEditorDlg;

protected:
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    std::unique_ptr<XyRust::DocumentOwner> m_document;
#else
    CAtlArray<STSEntry>   m_entries;
    CAtlArray<STSSegment> m_segments;
#endif
    virtual void OnChanged() {}

public:
    bool           m_simple;
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    CString        m_name;
#endif
    LCID           m_lcid;
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    exttype        m_subtitleType;
#endif
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    tmode          m_mode;
#endif
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    CTextFile::enc m_encoding;
#endif
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    CString        m_path;
#endif

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    CSize          m_dstScreenSize;
    int            m_defaultWrapStyle;
    int            m_collisions;
    bool           m_fScaledBAS;
#endif
    CSize          m_layout_size;

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    CSTSStyleMap   m_styles;
#endif

    bool           m_fForcedDefaultStyle;
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    STSStyle       m_defaultStyle;
#endif

    enum EPARCompensationType
    {
        EPCTDisabled     = 0,
        EPCTDownscale    = 1,
        EPCTUpscale      = 2,
        EPCTAccurateSize = 3
    };

    EPARCompensationType m_ePARCompensationType;
    double               m_dPARCompensation;

    enum YCbCrMatrix
    {
        YCbCrMatrix_BT601,
        YCbCrMatrix_BT709,
        YCbCrMatrix_BT2020,
        YCbCrMatrix_AUTO
    };
    enum YCbCrRange
    {
        YCbCrRange_PC,
        YCbCrRange_TV,
        YCbCrRange_AUTO
    };
    YCbCrMatrix    m_eYCbCrMatrix;
    YCbCrRange     m_eYCbCrRange;
public:
    CSimpleTextSubtitle();
    virtual ~CSimpleTextSubtitle();

    virtual void Copy(CSimpleTextSubtitle& sts);
    virtual void Empty();

    bool IsEmpty();

    void RemoveAllEntries();
    void CreateSegments();

    bool Open(CString    fn           , int CharSet, CString name = _T(""));
    bool Open(CTextFile* f            , int CharSet, CString name);
    bool Open(BYTE     * data, int len, int CharSet, CString name);

    void Add(CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end, CString style = CString(_T("Default")),
        const CString& actor  = CString(_T("")), 
        const CString& effect = CString(_T("")), 
        const CRect& marginRect = CRect(0,0,0,0), int layer = 0, int readorder = -1);
    //void Add(CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end, const CString& style, const CString& actor, const CString& effect, const CRect& marginRect, int layer, int readorder);
    //void Add(CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end);

    //add an STSEntry obj to the array
    //NO addition segments added
    //remember to call sort when all STSEntrys are ready
    void AddSTSEntryOnly(CStringW str, bool fUnicode, REFERENCE_TIME start, REFERENCE_TIME end, CString style = _T("Default"), const CString& actor = _T(""), const CString& effect = _T(""), const CRect& marginRect = CRect(0,0,0,0), int layer = 0, int readorder = -1);

    void ChangeUnknownStylesToDefault();
    void AddStyle(CString name, STSStyle* style); // style will be stored and freed in Empty() later

    bool SetDefaultStyle(STSStyle& s);

    void SetForceDefaultStyle(bool value);

    REFERENCE_TIME TranslateStart(int i, double fps);
    REFERENCE_TIME TranslateEnd  (int i, double fps);

    REFERENCE_TIME TranslateSegmentStart   (int i, double fps);
    REFERENCE_TIME TranslateSegmentEnd     (int i, double fps);
    void TranslateSegmentStartEnd(int i, double fps, /*out*/REFERENCE_TIME& start, /*out*/REFERENCE_TIME& end);

    //find the first STSSegment with stop time > @t
    //@iSegment: return the index, 0 based, of the STSSegment found, return STSSegments count if all STSSegments' stop time are NOT bigger than @t
    //@nSegment: return the STSSegments count
    //@return: the ptr to the STSSegment found, return NULL if such STSSegment not exist
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    const STSSegment* SearchSubs(REFERENCE_TIME t, double fps, /*[out]*/ int* iSegment = NULL, int* nSegments = NULL);

    //find STSSegment with a duration containing @t, i.e. with a start time <= @t and a stop time > @t
    //@iSegment: return the index, 0 based, of the STSSegment found. Return -1 if such STSSegment not exist
    //@nSegment: return the STSSegments count
    //@return: the ptr to the STSSegment found, return NULL if such STSSegment not exist
    STSSegment* SearchSubs2(REFERENCE_TIME t, double fps, /*[out]*/ int* iSegment=NULL, int* nSegments=NULL);

    const STSSegment* GetSegment(int iSegment) {return iSegment >= 0 && iSegment < (int)m_segments.GetCount() ? &m_segments[iSegment] : NULL;}
#endif

    // Internal value/scalar access surface. Snapshots own their strings/members;
    // no caller retains a pointer into the document arrays through this surface.
    size_t GetEntryCount() const;
    size_t GetSegmentCount() const;
    STSEntry GetEntrySnapshot(int i) const;
    REFERENCE_TIME GetEntryEnd(int i) const;
    int GetEntryLayer(int i) const;
    int GetEntryReadOrder(int i) const;
    CString GetEntryEffect(int i) const;
    bool GetSegmentEntryCount(int iSegment, size_t& count) const;
    bool FindNextSegment(REFERENCE_TIME t, double fps,
        int* iSegment = NULL, int* nSegments = NULL);
    bool SearchSubs2Snapshot(REFERENCE_TIME t, double fps, STSSegment& snapshot,
        int* iSegment = NULL, int* nSegments = NULL);
    bool IsSegmentAnimated(unsigned int iSegment) const;
    void OrSegmentAnimated(int iSegment, bool animated);

#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    CSize GetDocumentScreenSize() const;
    void SetDocumentScreenSize(const CSize& value);
    void SetDocumentScreenWidth(int value);
    void SetDocumentScreenHeight(int value);
    int GetDocumentWrapStyle() const;
    void SetDocumentWrapStyle(int value);
    int GetDocumentCollisions() const;
    void SetDocumentCollisions(int value);
    bool GetDocumentScaledBAS() const;
    void SetDocumentScaledBAS(bool value);

    // Interpretation values are supplied by native construction/parsing.
    tmode GetDocumentMode() const;
    exttype GetDocumentType() const;
    CTextFile::enc GetDocumentEncoding() const;
    void SetDocumentMode(tmode value);
    void SetDocumentType(exttype value);
    void SetDocumentEncoding(CTextFile::enc value);

    // Identity is raw code units; getters return independent native values.
    CString GetDocumentName() const;
    CString GetDocumentPath() const;
    void SetDocumentName(const CString& value);
    void SetDocumentPath(const CString& value);

    // Named/default style storage is Rust-owned in the experimental build;
    // enumeration copies only one requested row and exposes no map/pointee loan.
    enum class NamedStyleLookup { Missing, Null, Value };
    NamedStyleLookup LookupNamedStyle(LPCWSTR name, STSStyle* output = NULL) const;
    bool CopyNamedStylesFrom(const CSimpleTextSubtitle& source, bool fAppend = false);
    size_t GetStyleCount() const;
    bool HasNamedStyle(LPCWSTR name) const;
    bool GetNamedStyleCopy(const CString& name, STSStyle& value) const;
    bool GetEntryStyleCopy(int i, STSStyle& value);
    bool SetExistingStyleValue(LPCWSTR name, const STSStyle* value);
    void ResetDefaultStyle(int CharSet);
    bool GetStyleNameAt(size_t ordinal, CString& name) const;
    bool GetStyleValueAt(size_t ordinal, STSStyle& value) const;
#endif

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    STSStyle* GetStyle(int i);
#endif
    bool      GetStyle(int i, STSStyle* const stss);
    int       GetCharSet(int i);

    CStringW GetStrW (int i, bool fSSA = false);

#  define GetStr GetStrW

    friend bool OpenMicroDVD(CTextFile* file, CSimpleTextSubtitle& ret, int CharSet);
private:
#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    bool CopyStyles(const CSTSStyleMap& styles, bool fAppend = false);
#endif
};

extern BYTE   CharSetList[];
extern TCHAR* CharSetNames[];
extern int    CharSetLen;

class CHtmlColorMap : public CAtlMap<CString, DWORD, CStringElementTraits<CString> > {public: CHtmlColorMap();};
extern CHtmlColorMap g_colors;
