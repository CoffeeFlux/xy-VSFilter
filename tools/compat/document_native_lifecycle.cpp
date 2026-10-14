// SPDX-License-Identifier: GPL-2.0-or-later
// Full native class lifecycle probe: compile this source separately against the
// pinned and candidate checkouts' actual static libraries. No copied methods,
// reference member invalidation, allocator failures, dialogs or rendering.
#include <stdafx.h> // runner puts the selected checkout's src/subtitles first
#include <STS.h>
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include <RustDocumentOwner.h>
#endif
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <exception>
#include <stdexcept>

static unsigned sequence = 0, empty_events[2] = {}, change_events[2] = {};
static void check(bool value, const char* message) {
    if(!value) throw std::runtime_error(message);
}
template<class String> static void units(const String& value) {
    printf(" %d",value.GetLength());
    for(int i=0;i<value.GetLength();i++) printf(" %04x",unsigned(static_cast<uint16_t>(value[i])));
}
static void real(double value) {
    static_assert(sizeof(double)==sizeof(uint64_t),"IEEE double trace width");
    uint64_t bits;std::memcpy(&bits,&value,sizeof bits);printf(" %016llx",static_cast<unsigned long long>(bits));
}
static void style(const STSStyle& value) {
    printf("style %d",value.charSet);units(value.fontName);real(value.fontSize);
    printf(" %d %d %d %d",value.fontWeight,value.fItalic,value.fUnderline,value.fStrikeOut);
    const RECT margin=value.marginRect.get();
    printf(" %ld %ld %ld %ld %d %d",margin.left,margin.top,margin.right,margin.bottom,value.scrAlignment,value.borderStyle);
    real(value.outlineWidthX);real(value.outlineWidthY);real(value.shadowDepthX);real(value.shadowDepthY);
    for(int i=0;i<4;i++)printf(" %08lx %u",static_cast<unsigned long>(value.colors[i]),unsigned(value.alpha[i]));
    real(value.fontScaleX);real(value.fontScaleY);real(value.fontSpacing);real(value.fBlur);real(value.fGaussianBlur);
    real(value.fontAngleZ);real(value.fontAngleX);real(value.fontAngleY);real(value.fontShiftX);real(value.fontShiftY);
    printf(" %d\n",value.relativeTo);
}

class TraceDocument : public CSimpleTextSubtitle {
    unsigned id_;
public:
    explicit TraceDocument(unsigned id) : id_(id) {
        // Assign every observed metadata scalar before the first observation;
        // constructor-indeterminate m_simple/m_lcid are never read.
        m_simple = id != 0;m_name = id ? L"destination" : L"source";
        m_lcid = 1033+id;m_subtitleType=EXTSRT;m_mode=TIME;m_encoding=CTextFile::UTF8;
        m_path = id ? L"destination-path" : L"source-path";
        m_dstScreenSize=CSize(640+id,480+id);m_defaultWrapStyle=int(id);m_collisions=int(id);
        m_fScaledBAS=id!=0;m_layout_size=CSize(800+id,600+id);
        m_defaultStyle=STSStyle();m_defaultStyle.charSet=ANSI_CHARSET;m_defaultStyle.fontSize=19+id;
        m_fForcedDefaultStyle=false;m_ePARCompensationType=EPCTDownscale;
        m_dPARCompensation=1.25+id;m_eYCbCrMatrix=YCbCrMatrix_BT709;m_eYCbCrRange=YCbCrRange_PC;
    }
    ~TraceDocument() override {printf("event %u %u destructor-body\n",++sequence,id_);}
    size_t Entries() const {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        return m_entries.GetCount();
#else
        return GetEntryCount();
#endif
    }
    size_t Segments() const {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        return m_segments.GetCount();
#else
        return GetSegmentCount();
#endif
    }
    STSEntry Entry(int index) const {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        return m_entries.GetAt(index);
#else
        return GetEntrySnapshot(index);
#endif
    }
    STSSegment Segment(int index) const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        STSSegment value;check(m_document->Segment(index,value),"expected segment");return value;
#else
        return m_segments.GetAt(index);
#endif
    }
    void Animate(int index) {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        m_segments.GetAt(index).animated=true;
#else
        OrSegmentAnimated(index,true);
#endif
    }
    void Dump(const char* stage) const {
        printf("state %u %u %s %zu %zu\n",++sequence,id_,stage,Entries(),Segments());
        printf("metadata %d",m_simple);units(m_name);
        printf(" %lu %d %d %d",static_cast<unsigned long>(m_lcid),int(m_subtitleType),int(m_mode),int(m_encoding));units(m_path);
        printf(" %ld %ld %d %d %d %ld %ld %d %d",m_dstScreenSize.cx,m_dstScreenSize.cy,
            m_defaultWrapStyle,m_collisions,m_fScaledBAS,m_layout_size.cx,m_layout_size.cy,
            m_fForcedDefaultStyle,int(m_ePARCompensationType));
        real(m_dPARCompensation);printf(" %d %d\n",int(m_eYCbCrMatrix),int(m_eYCbCrRange));
        style(m_defaultStyle);
        printf("styles %zu\n",m_styles.GetCount());
        POSITION pos=m_styles.GetStartPosition();
        while(pos){CString name;STSStyle* value=NULL;m_styles.GetNextAssoc(pos,name,value);printf("name");units(name);printf("\n");style(*value);}
        for(size_t i=0;i<Entries();i++) {
            const STSEntry e=Entry(int(i));printf("entry %zu",i);units(e.str);units(e.style);units(e.actor);units(e.effect);
            printf(" %d %ld %ld %ld %ld %d %lld %lld %d\n",e.fUnicode,e.marginRect.left,e.marginRect.top,
                e.marginRect.right,e.marginRect.bottom,e.layer,e.start,e.end,e.readorder);
        }
        for(size_t i=0;i<Segments();i++) {
            const STSSegment s=Segment(int(i));printf("segment %zu %lld %lld %d %zu",i,s.start,s.end,s.animated,s.subs.GetCount());
            for(size_t j=0;j<s.subs.GetCount();j++)printf(" %d",s.subs[j]);printf("\n");
        }
    }
    void Empty() override {
        ++empty_events[id_];printf("event %u %u empty-enter\n",++sequence,id_);Dump("before-empty");
        CSimpleTextSubtitle::Empty();
        printf("event %u %u empty-exit\n",++sequence,id_);Dump("after-empty");
    }
protected:
    void OnChanged() override {
        ++change_events[id_];printf("event %u %u changed-enter\n",++sequence,id_);Dump("callback-state");
        // Actual native read operations reenter after rebuild's Rust loan ends.
        // They never mutate/reallocate reference storage during the callback.
        printf("callback query %u %d %d %d\n",id_,SearchSub(-1,25.0),SearchSub(0,25.0),SearchSub(4,25.0));
        for(size_t i=0;i<Entries();i++) {
            CStringW wide=GetStrW(int(i),true),packed=GetStrWA(int(i),true);
            printf("text callback %u %zu %d",id_,i,IsEntryUnicode(int(i)));units(wide);units(packed);printf("\n");
        }
        printf("event %u %u changed-exit\n",++sequence,id_);
    }
};

static void populate(TraceDocument& doc) {
    STSStyle* normal=new STSStyle;normal->charSet=ANSI_CHARSET;doc.AddStyle(CString(L"Default"),normal);
    STSStyle* japanese=new STSStyle;japanese->charSet=SHIFTJIS_CHARSET;doc.AddStyle(CString(L"Japanese"),japanese);
    doc.Add(CStringW(L" first\nline "),true,0,10000000,CString(L"Default"),CString(L"actor"),CString(L""),CRect(-1,2,3,4),2,9);
    const wchar_t packed[]={0x82,0xa0};
    doc.Add(CStringW(packed,2),false,2000000,8000000,CString(L"Japanese"),CString(L""),CString(L""),CRect(0,0,0,0),1,0);
    doc.AddSTSEntryOnly(CStringW(L"marker"),true,4000000,4000000);
    // Genuine AddStyle renames the original map value and matching entries.
    STSStyle* replacement=new STSStyle;replacement->charSet=SHIFTJIS_CHARSET;replacement->fontSize=21;
    doc.AddStyle(CString(L"Japanese"),replacement);
    doc.CreateSegments();doc.Animate(0);doc.Dump("populated");
}
static void text_edits(TraceDocument& doc) {
    unsigned notifications=change_events[1];
    const wchar_t raw[]={L'R',0,0xd800,L'\n',L'Z'};
    doc.SetStr(0,CStringW(raw,5),true);doc.Dump("setstr-retain-unicode");
    check(doc.IsEntryUnicode(0),"SetStr retains stored Unicode flag");
    // Known Windows charsets, normal valid Shift-JIS byte sequence; native
    // TranslateCharsetInfo and codepage conversions execute in both binaries.
    doc.ConvertUnicode(1,true);doc.Dump("convert-sjis-to-unicode");
    doc.ConvertUnicode(1,true);doc.Dump("convert-unicode-noop");
    doc.SetStr(1,CStringW(L"edited\ntext"),false);doc.Dump("setstr-input-packed-store-unicode");
    check(doc.IsEntryUnicode(1),"SetStr never toggles stored Unicode flag");
    doc.ConvertUnicode(1,false);doc.Dump("convert-unicode-to-sjis");
    doc.ConvertUnicode(1,false);doc.Dump("convert-packed-noop");
    doc.SetStr(1,CStringW(L"plain\ntext"),true);doc.Dump("setstr-input-unicode-store-packed");
    check(!doc.IsEntryUnicode(1),"SetStr retains packed storage flag");
    check(change_events[1]==notifications,"text edits have no OnChanged");
}
int main() {
    try {
        if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
        printf("provenance pointer_bits=%zu\n",sizeof(void*)*8);
        printf("environment ansi=%u oem=%u\n",GetACP(),GetOEMCP());
        {
            TraceDocument source(0),destination(1);populate(source);populate(destination);
            unsigned notifications=change_events[1];
            destination.Copy(source);destination.Dump("copied-nonempty");source.Dump("copy-source-unchanged");
            check(change_events[1]==notifications,"Copy does not call OnChanged");
            text_edits(destination);source.Dump("copy-independent-source");
            destination.Sort(false);destination.Dump("sort-normal");
            destination.Sort(true);destination.Dump("sort-readorder");
            destination.ConvertToFrameBased(25.0);destination.Animate(0);destination.Dump("converted-to-frame");
            notifications=change_events[1];destination.ConvertToFrameBased(25.0);destination.Dump("frame-noop");
            check(change_events[1]==notifications,"frame no-op does not notify");
            destination.ConvertToTimeBased(25.0);destination.Animate(0);destination.Dump("converted-to-time");
            notifications=change_events[1];destination.ConvertToTimeBased(25.0);destination.Dump("time-noop");
            check(change_events[1]==notifications,"time no-op does not notify");
            destination.RemoveAllEntries();destination.Dump("entries-only-removed");
            check(change_events[1]==notifications,"RemoveAllEntries does not notify");
            destination.CreateSegments();destination.Dump("rebuilt-empty");
            destination.Copy(source);destination.Dump("copy-after-clear");
            notifications=change_events[1];destination.Empty();destination.Empty();destination.Dump("repeated-empty");
            check(change_events[1]==notifications,"Empty does not notify");
            destination.Copy(source);destination.Copy(destination);destination.Dump("self-copy");
            check(destination.Entries()==0&&destination.Segments()==0&&destination.m_styles.GetCount()==0,"self-copy reads after Empty");
            check(destination.m_dstScreenSize==CSize(0,0),"self-copy screen size reset");
            source.Copy(source);source.Dump("source-self-copy");
            populate(source);source.Dump("nonempty-before-destruction");
            printf("before-destruction empty=%u,%u changed=%u,%u\n",empty_events[0],empty_events[1],change_events[0],change_events[1]);
        }
        // Base destruction calls base Empty, not the already-destroyed override.
        printf("after-destruction empty=%u,%u changed=%u,%u\n",empty_events[0],empty_events[1],change_events[0],change_events[1]);
        printf("document-native-lifecycle-v1 complete\n");return 0;
    } catch(const std::exception& error) {
        fprintf(stderr,"document-native-lifecycle-v1 failed: %s\n",error.what());return 2;
    } catch(...) {
        fprintf(stderr,"document-native-lifecycle-v1 failed: native exception\n");return 3;
    }
}
