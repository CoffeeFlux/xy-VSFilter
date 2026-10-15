// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
// Experimental native value boundary only. Canonical entries/segments live in
// Rust, including supplied default/named styles and raw name/path identity.
// Interpretation and script layout scalars are supplied by native construction
// and parsing.
// Other metadata, string preparation and CRT numeric/sort/name-generation
// choices remain native.
#include "STS.h"
#include "xy_rust_document.h"
#include <windows.h>
#include <array>
#include <climits>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace XyRust {
class DocumentStatusError : public std::runtime_error {
    uint32_t status_;
public:
    explicit DocumentStatusError(uint32_t status)
        : std::runtime_error("experimental Rust document status " + std::to_string(status)), status_(status) {}
    uint32_t Status() const noexcept { return status_; }
};
namespace DocumentDetail {
using Bytes = std::vector<uint8_t>;
inline void Check(uint32_t status) {
    if(status != XY_DOCUMENT_V1_OK) throw DocumentStatusError(status);
}
inline void Shape(bool condition) { if(!condition) Check(XY_DOCUMENT_V1_INVALID_INPUT); }
inline uint32_t Index(int index) {
    if(index < 0) Check(XY_DOCUMENT_V1_OUT_OF_RANGE);
    return static_cast<uint32_t>(index);
}
inline uint32_t Count(size_t count) {
    if(count > INT32_MAX) Check(XY_DOCUMENT_V1_COUNT_OVERFLOW);
    return static_cast<uint32_t>(count);
}
inline size_t Size(uint64_t bytes) {
    if(bytes > SIZE_MAX || bytes > PTRDIFF_MAX) Check(XY_DOCUMENT_V1_COUNT_OVERFLOW);
    return static_cast<size_t>(bytes);
}
inline size_t MulSize(size_t left, size_t right) {
    if(right && left > static_cast<size_t>(PTRDIFF_MAX) / right) Check(XY_DOCUMENT_V1_COUNT_OVERFLOW);
    return left * right;
}
inline size_t AddSize(size_t left, size_t right) {
    if(left > static_cast<size_t>(PTRDIFF_MAX) || right > static_cast<size_t>(PTRDIFF_MAX) - left) Check(XY_DOCUMENT_V1_COUNT_OVERFLOW);
    return left + right;
}
// One shared recursive gate per linked module. The inline function's local
// static is shared across translation units. It serializes EACH C ABI call,
// never a whole native parser/render operation or output value construction.
class Gate {
    CRITICAL_SECTION section_;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
public:
    Gate() { InitializeCriticalSection(&section_); }
    ~Gate() { DeleteCriticalSection(&section_); }
    void Enter() { EnterCriticalSection(&section_); }
    void Leave() { LeaveCriticalSection(&section_); }
};
inline Gate& SharedGate() { static Gate gate; return gate; }
class Loan {
    Gate& gate_;
public:
    Loan() : gate_(SharedGate()) { gate_.Enter(); }
    ~Loan() { gate_.Leave(); }
    Loan(const Loan&) = delete;
    Loan& operator=(const Loan&) = delete;
};
template<class Call> inline uint32_t Invoke(Call call) {
    Loan loan;
    return call();
}
inline void Release(uint32_t status) noexcept {
    // Explicitly unresolved EXPERIMENTAL internal lifecycle failure. This is
    // not native allocation/exception compatibility or a public HRESULT mapping.
    // Never throw from destruction, retry Busy, spin or fall back to native data.
    if(status != XY_DOCUMENT_V1_OK) std::terminate();
}
template<class Buffer> inline uint64_t Load(const Buffer& bytes, size_t offset, size_t width) {
    Shape(width <= 8 && offset <= bytes.size() && width <= bytes.size() - offset);
    uint64_t result = 0;
    for(size_t i = 0; i < width; ++i) result |= uint64_t(bytes[offset + i]) << (i * 8);
    return result;
}
inline int32_t I32(uint64_t value) {
    uint32_t bits = static_cast<uint32_t>(value); int32_t result;
    std::memcpy(&result, &bits, sizeof result); return result;
}
inline int64_t I64(uint64_t value) {
    int64_t result; std::memcpy(&result, &value, sizeof result); return result;
}
template<class Buffer> inline void Put(Buffer& bytes, size_t offset, uint64_t value, size_t width) {
    Shape(width <= 8 && offset <= bytes.size() && width <= bytes.size() - offset);
    for(size_t i = 0; i < width; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
inline void Append(Bytes& bytes, uint64_t value, size_t width) {
    const size_t offset = bytes.size(); bytes.resize(AddSize(offset, width)); Put(bytes, offset, value, width);
}
inline Bytes Units(const CStringW& value) {
    static_assert(sizeof(wchar_t) == 2, "Windows raw UTF-16 boundary");
    const size_t length = static_cast<size_t>(value.GetLength());
    Count(length); Bytes bytes; bytes.reserve(AddSize(length, length));
    for(size_t i = 0; i < length; ++i) Append(bytes, static_cast<uint16_t>(value[static_cast<int>(i)]), 2);
    return bytes;
}
inline CStringW String(const Bytes& bytes, size_t& offset, uint32_t length) {
    Count(length); const size_t size = AddSize(length, length);
    Shape(offset <= bytes.size() && size <= bytes.size() - offset);
    if(length == 0) return CStringW();
    std::vector<wchar_t> units; units.reserve(length);
    for(uint32_t i = 0; i < length; ++i) units.push_back(static_cast<wchar_t>(Load(bytes, offset + size_t(i) * 2, 2)));
    offset += size; return CStringW(units.data(), static_cast<int>(length));
}
inline Bytes EntryBytes(const CStringW& text, bool unicode, int64_t start, int64_t end,
    const CString& style, const CString& actor, const CString& effect,
    const CRect& margin_rect, int layer, int readorder) {
    static_assert(sizeof(TCHAR) == 2, "Experimental document requires the native Unicode configuration");
    Bytes fields[4] = {Units(text), Units(style), Units(actor), Units(effect)};
    size_t total = XY_DOCUMENT_V1_ENTRY_HEADER_BYTES;
    for(const auto& field : fields) total = AddSize(total, field.size());
    Bytes bytes(XY_DOCUMENT_V1_ENTRY_HEADER_BYTES, 0); bytes.reserve(total);
    Put(bytes, 0, static_cast<uint64_t>(start), 8); Put(bytes, 8, static_cast<uint64_t>(end), 8);
    const LONG margins[4] = {margin_rect.left, margin_rect.top, margin_rect.right, margin_rect.bottom};
    for(size_t i = 0; i < 4; ++i) Put(bytes, 16 + 4 * i, static_cast<uint32_t>(margins[i]), 4);
    Put(bytes, 32, static_cast<uint32_t>(layer), 4); Put(bytes, 36, static_cast<uint32_t>(readorder), 4);
    Put(bytes, 40, unicode ? 1 : 0, 4);
    for(size_t i = 0; i < 4; ++i) {Put(bytes, 44 + 4 * i, Count(fields[i].size() / 2), 4); bytes.insert(bytes.end(), fields[i].begin(), fields[i].end());}
    return bytes;
}
inline Bytes EntryBytes(const STSEntry& entry) {
    return EntryBytes(entry.str,entry.fUnicode,entry.start,entry.end,entry.style,
        entry.actor,entry.effect,entry.marginRect,entry.layer,entry.readorder);
}
inline STSEntry EntryValue(const Bytes& bytes) {
    Shape(bytes.size() >= XY_DOCUMENT_V1_ENTRY_HEADER_BYTES && Load(bytes, 60, 4) == 0 && Load(bytes, 40, 4) <= 1);
    uint32_t lengths[4]; size_t total = XY_DOCUMENT_V1_ENTRY_HEADER_BYTES;
    for(size_t i = 0; i < 4; ++i) { lengths[i] = Count(static_cast<size_t>(Load(bytes, 44 + 4 * i, 4))); total = AddSize(total, AddSize(lengths[i], lengths[i])); }
    Shape(total == bytes.size()); size_t offset = XY_DOCUMENT_V1_ENTRY_HEADER_BYTES;
    STSEntry entry; entry.str = String(bytes, offset, lengths[0]); entry.style = String(bytes, offset, lengths[1]);
    entry.actor = String(bytes, offset, lengths[2]); entry.effect = String(bytes, offset, lengths[3]);
    entry.start = I64(Load(bytes, 0, 8)); entry.end = I64(Load(bytes, 8, 8));
    entry.marginRect.SetRect(I32(Load(bytes,16,4)), I32(Load(bytes,20,4)), I32(Load(bytes,24,4)), I32(Load(bytes,28,4)));
    entry.layer = I32(Load(bytes,32,4)); entry.readorder = I32(Load(bytes,36,4)); entry.fUnicode = Load(bytes,40,4) != 0;
    return entry;
}
inline STSSegment SegmentValue(const Bytes& bytes) {
    Shape(bytes.size() >= XY_DOCUMENT_V1_SEGMENT_HEADER_BYTES && Load(bytes,16,4) <= 1 && Load(bytes,24,4) == 0 && Load(bytes,28,4) == 0);
    const uint32_t count = Count(static_cast<size_t>(Load(bytes,20,4)));
    const size_t total = AddSize(XY_DOCUMENT_V1_SEGMENT_HEADER_BYTES, MulSize(count, 4));
    Shape(total == bytes.size());
    for(uint32_t i = 0; i < count; ++i) Count(static_cast<size_t>(Load(bytes,32 + size_t(i)*4,4)));
    STSSegment segment(I64(Load(bytes,0,8)), I64(Load(bytes,8,8))); segment.animated = Load(bytes,16,4) != 0;
    for(uint32_t i = 0; i < count; ++i) segment.subs.Add(static_cast<int>(Load(bytes,32 + size_t(i)*4,4)));
    return segment;
}
// Style wire is independent of STSStyle object layout and FwRect/CString
// handles. Keep native materialization outside each admitted C ABI call.
enum { StyleHeaderBytes = 192 };
template<class Style> inline std::array<const double*,15> StyleFloatFields(const Style& value) {
    return {{&value.fontSize,&value.outlineWidthX,&value.outlineWidthY,
        &value.shadowDepthX,&value.shadowDepthY,&value.fontScaleX,&value.fontScaleY,
        &value.fontSpacing,&value.fBlur,&value.fGaussianBlur,&value.fontAngleZ,
        &value.fontAngleX,&value.fontAngleY,&value.fontShiftX,&value.fontShiftY}};
}
template<class Style> inline std::array<double*,15> StyleFloatFields(Style& value) {
    return {{&value.fontSize,&value.outlineWidthX,&value.outlineWidthY,
        &value.shadowDepthX,&value.shadowDepthY,&value.fontScaleX,&value.fontScaleY,
        &value.fontSpacing,&value.fBlur,&value.fGaussianBlur,&value.fontAngleZ,
        &value.fontAngleX,&value.fontAngleY,&value.fontShiftX,&value.fontShiftY}};
}
template<class Style> inline Bytes StyleBytes(const Style& value) {
    static_assert(sizeof(double) == 8 && sizeof(int) == 4 && sizeof(COLORREF) == 4,
        "Windows style transport widths");
    const Bytes font = Units(value.fontName);
    Bytes bytes(AddSize(StyleHeaderBytes, font.size()), 0);
    const int fields[9] = {value.charSet,value.fontWeight,value.scrAlignment,value.borderStyle,value.relativeTo,
        static_cast<int>(value.marginRect.get().left),static_cast<int>(value.marginRect.get().top),
        static_cast<int>(value.marginRect.get().right),static_cast<int>(value.marginRect.get().bottom)};
    for(size_t i = 0; i < 9; ++i) Put(bytes,i * 4,static_cast<uint32_t>(fields[i]),4);
    for(size_t i = 0; i < 4; ++i) {
        Put(bytes,36 + i * 4,static_cast<uint32_t>(value.colors[i]),4);
        bytes[52 + i] = value.alpha[i];
    }
    bytes[56] = value.fItalic ? 1 : 0; bytes[57] = value.fUnderline ? 1 : 0;
    bytes[58] = value.fStrikeOut ? 1 : 0;
    Put(bytes,64,Count(font.size() / 2),4);
    const auto floats = StyleFloatFields(value);
    for(size_t i = 0; i < floats.size(); ++i) {
        uint64_t bits; std::memcpy(&bits,floats[i],sizeof bits);
        Put(bytes,72 + i * 8,bits,8);
    }
    for(size_t i = 0; i < font.size(); ++i) bytes[StyleHeaderBytes + i] = font[i];
    return bytes;
}
template<class Style = STSStyle> inline Style StyleValue(const Bytes& bytes) {
    Shape(bytes.size() >= StyleHeaderBytes);
    for(size_t i = 56; i < 59; ++i) Shape(bytes[i] <= 1);
    for(size_t i = 59; i < 64; ++i) Shape(bytes[i] == 0);
    for(size_t i = 68; i < 72; ++i) Shape(bytes[i] == 0);
    const uint32_t length = Count(static_cast<size_t>(Load(bytes,64,4)));
    Shape(bytes.size() == AddSize(StyleHeaderBytes,MulSize(length,2)));
    // Validate the entire record before constructing native strings/flyweights.
    Style value;
    value.charSet = I32(Load(bytes,0,4)); value.fontWeight = I32(Load(bytes,4,4));
    value.scrAlignment = I32(Load(bytes,8,4)); value.borderStyle = I32(Load(bytes,12,4));
    value.relativeTo = I32(Load(bytes,16,4));
    value.marginRect = CRect(I32(Load(bytes,20,4)),I32(Load(bytes,24,4)),
        I32(Load(bytes,28,4)),I32(Load(bytes,32,4)));
    for(size_t i = 0; i < 4; ++i) {
        value.colors[i] = static_cast<COLORREF>(Load(bytes,36 + i * 4,4));
        value.alpha[i] = bytes[52 + i];
    }
    value.fItalic = bytes[56] != 0; value.fUnderline = bytes[57] != 0;
    value.fStrikeOut = bytes[58] != 0;
    const auto floats = StyleFloatFields(value);
    for(size_t i = 0; i < floats.size(); ++i) {
        const uint64_t bits = Load(bytes,72 + i * 8,8);
        std::memcpy(floats[i],&bits,sizeof bits);
    }
    size_t offset = StyleHeaderBytes; value.fontName = String(bytes,offset,length);
    return value;
}
} // namespace DocumentDetail

class DocumentOwner;
class OpenLifecycle;
class UnknownStylesPass;
class StyleAddition;
class NamedStyleCopy;
class DocumentReply {
    uint64_t id_ = 0;
    explicit DocumentReply(uint64_t id) : id_(id) {}
    friend class DocumentOwner;
    friend class UnknownStylesPass;
    friend class StyleAddition;
    friend class NamedStyleCopy;
public:
    DocumentReply(const DocumentReply&) = delete;
    DocumentReply& operator=(const DocumentReply&) = delete;
    DocumentReply(DocumentReply&& other) noexcept : id_(other.id_) { other.id_ = 0; }
    DocumentReply& operator=(DocumentReply&& other) noexcept {
        if(this != &other) { Close(); id_ = other.id_; other.id_ = 0; } return *this;
    }
    ~DocumentReply() noexcept { Close(); }
    void Close() noexcept {
        if(id_) { const uint64_t id = id_; id_ = 0; DocumentDetail::Release(DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_drop_v1(id);})); }
    }
    uint64_t Size() const {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_size_v1(id_,out.data(),out.size());}));
        return DocumentDetail::Load(out,0,8);
    }
    DocumentDetail::Bytes Read() const {
        DocumentDetail::Bytes out(DocumentDetail::Size(Size()));
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_read_v1(id_,0,out.data(),out.size());}));
        return out;
    }
};
struct DocumentQuery { int index; bool found; size_t count; };
struct DocumentTextOperation {
    uint32_t action;
    bool target_unicode, strip_tags, has_style;
    int charset;
    CStringW text;
};

class DocumentOwner {
    friend class OpenLifecycle;
    friend class UnknownStylesPass;
    friend class StyleAddition;
    friend class NamedStyleCopy;
    uint64_t id_ = 0;
    static DocumentTextOperation DecodeTextOperation(const DocumentDetail::Bytes& bytes) {
        DocumentDetail::Shape(bytes.size() >= 32);
        const uint32_t action = static_cast<uint32_t>(DocumentDetail::Load(bytes,0,4));
        const uint64_t target = DocumentDetail::Load(bytes,4,4), strip = DocumentDetail::Load(bytes,8,4);
        const uint64_t has_style = DocumentDetail::Load(bytes,12,4);
        const int charset = DocumentDetail::I32(DocumentDetail::Load(bytes,16,4));
        const uint32_t units = DocumentDetail::Count(DocumentDetail::Size(DocumentDetail::Load(bytes,24,8)));
        DocumentDetail::Shape(action <= 3 && target <= 1 && strip <= 1 && has_style <= 1 &&
            DocumentDetail::Load(bytes,20,4) == 0 &&
            (action != 3 || (units == 0 && strip == 0 && has_style == 0 && charset == 0)) &&
            bytes.size() == DocumentDetail::AddSize(32,DocumentDetail::MulSize(units,2)));
        size_t offset = 32;
        return {action,target != 0,strip != 0,has_style != 0,charset,DocumentDetail::String(bytes,offset,units)};
    }
    template<size_t N> std::array<uint8_t,N> EntryFields(int index) const {
        static_assert(N == XY_DOCUMENT_V1_ENTRY_HEADER_BYTES, "entry header size");
        std::array<uint8_t,N> out{}; const uint32_t i = DocumentDetail::Index(index);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_entry_header_v1(id_,i,out.data(),out.size());})); return out;
    }
    bool SegmentFields(uint32_t index, std::array<uint8_t,XY_DOCUMENT_V1_SEGMENT_HEADER_BYTES>& out) const {
        const uint32_t status = DocumentDetail::Invoke([&]{return xy_vsfilter_document_segment_header_v1(id_,index,out.data(),out.size());});
        if(status == XY_DOCUMENT_V1_OUT_OF_RANGE) return false; DocumentDetail::Check(status); return true;
    }
    DocumentReply SnapshotRaw(uint32_t kind, uint32_t index) const {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_open_v1(id_,kind,index,out.data(),out.size());}));
        return DocumentReply(DocumentDetail::Load(out,0,8));
    }
    DocumentReply Snapshot(uint32_t kind, int index) const {
        return SnapshotRaw(kind, DocumentDetail::Index(index));
    }
public:
    DocumentOwner() {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_create_v1(out.data(),out.size());})); id_ = DocumentDetail::Load(out,0,8);
    }
    DocumentOwner(const DocumentOwner&) = delete;
    DocumentOwner& operator=(const DocumentOwner&) = delete;
    ~DocumentOwner() noexcept {
        if(id_) { const uint64_t id = id_; id_ = 0; DocumentDetail::Release(DocumentDetail::Invoke([&]{return xy_vsfilter_document_destroy_v1(id);})); }
    }
    std::array<uint8_t,XY_DOCUMENT_V1_COUNTS_BYTES> Counts() const {
        std::array<uint8_t,XY_DOCUMENT_V1_COUNTS_BYTES> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_counts_v1(id_,out.data(),out.size());})); return out;
    }
    size_t Count() const { return static_cast<size_t>(DocumentDetail::Load(Counts(),0,4)); }
    size_t SegmentCount() const { return static_cast<size_t>(DocumentDetail::Load(Counts(),4,4)); }
    DocumentReply SnapshotEntry(int index) const { return Snapshot(0,index); }
    DocumentReply SnapshotSegment(int index) const { return Snapshot(1,index); }
    STSEntry Entry(int index) const { return DocumentDetail::EntryValue(SnapshotEntry(index).Read()); }
    // Scalar-only immutable snapshot. CRT qsort still receives actual native
    // STSEntry-width records; payload strings remain owned by Rust throughout.
    void MaterializeSortRecords(CAtlArray<STSEntry>& records) const {
        const auto bytes = Snapshot(7,0).Read();
        DocumentDetail::Shape(records.GetCount() == 0 && bytes.size() >= XY_DOCUMENT_V1_SORT_HEADER_BYTES);
        const uint32_t count = DocumentDetail::Count(static_cast<size_t>(DocumentDetail::Load(bytes,0,4)));
        DocumentDetail::Shape(DocumentDetail::Load(bytes,4,4) == 0 &&
            bytes.size() == DocumentDetail::AddSize(XY_DOCUMENT_V1_SORT_HEADER_BYTES,
                DocumentDetail::MulSize(count,XY_DOCUMENT_V1_SORT_RECORD_BYTES)));
        for(uint32_t i = 0; i < count; ++i) {
            const size_t offset = XY_DOCUMENT_V1_SORT_HEADER_BYTES + size_t(i) * XY_DOCUMENT_V1_SORT_RECORD_BYTES;
            STSEntry entry;
            entry.fUnicode = false;
            entry.marginRect.SetRect(static_cast<int>(i),0,0,0);
            entry.start = DocumentDetail::I64(DocumentDetail::Load(bytes,offset,8));
            entry.end = 0;
            entry.layer = DocumentDetail::I32(DocumentDetail::Load(bytes,offset+8,4));
            entry.readorder = DocumentDetail::I32(DocumentDetail::Load(bytes,offset+12,4));
            records.Add(entry);
        }
    }
    bool Segment(int index, STSSegment& value) const {
        if(index < 0) return false;
        std::array<uint8_t,8> out{};
        const uint32_t status = DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_open_v1(id_,1,static_cast<uint32_t>(index),out.data(),out.size());});
        if(status == XY_DOCUMENT_V1_OUT_OF_RANGE) return false; DocumentDetail::Check(status);
        DocumentReply reply(DocumentDetail::Load(out,0,8)); value = DocumentDetail::SegmentValue(reply.Read()); return true;
    }
    REFERENCE_TIME EntryStart(int i) const {return DocumentDetail::I64(DocumentDetail::Load(EntryFields<64>(i),0,8));}
    REFERENCE_TIME EntryEnd(int i) const {return DocumentDetail::I64(DocumentDetail::Load(EntryFields<64>(i),8,8));}
    int EntryLayer(int i) const {return DocumentDetail::I32(DocumentDetail::Load(EntryFields<64>(i),32,4));}
    int EntryReadOrder(int i) const {return DocumentDetail::I32(DocumentDetail::Load(EntryFields<64>(i),36,4));}
    CString EntryEffect(int i) const {return Entry(i).effect;}
    REFERENCE_TIME SegmentStart(int i) const {
        std::array<uint8_t,32> out{}; if(i < 0 || !SegmentFields(static_cast<uint32_t>(i),out)) DocumentDetail::Check(XY_DOCUMENT_V1_OUT_OF_RANGE);
        return DocumentDetail::I64(DocumentDetail::Load(out,0,8));
    }
    REFERENCE_TIME SegmentEnd(int i) const {
        std::array<uint8_t,32> out{}; if(i < 0 || !SegmentFields(static_cast<uint32_t>(i),out)) DocumentDetail::Check(XY_DOCUMENT_V1_OUT_OF_RANGE);
        return DocumentDetail::I64(DocumentDetail::Load(out,8,8));
    }
    bool SegmentEntryCount(int i, size_t& count) const {
        std::array<uint8_t,32> out{}; if(i < 0 || !SegmentFields(static_cast<uint32_t>(i),out)) return false;
        count = static_cast<size_t>(DocumentDetail::Load(out,20,4)); return true;
    }
    bool IsAnimated(unsigned i) const {
        std::array<uint8_t,32> out{}; return SegmentFields(i,out) && DocumentDetail::Load(out,16,4) != 0;
    }
    int AddPrepared(const STSEntry& entry, uint32_t mode) {
        const auto bytes = DocumentDetail::EntryBytes(entry); std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_add_prepared_v1(id_,mode,bytes.data(),bytes.size(),out.data(),out.size());}));
        return DocumentDetail::I32(DocumentDetail::Load(out,4,4));
    }
    // Native FastTrim/rejections/WebVTT have finished. Rust owns only the final
    // CR/LF/style suffix followed by the existing prepared-record insertion.
    int AddFinalSuffix(const CStringW& text, bool unicode, int64_t start, int64_t end,
        const CString& style, const CString& actor, const CString& effect,
        const CRect& margin_rect, int layer, int readorder, uint32_t mode) {
        const auto bytes = DocumentDetail::EntryBytes(text,unicode,start,end,style,actor,effect,margin_rect,layer,readorder);
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_add_prepared_v1(
            id_,mode,bytes.data(),bytes.size(),out.data(),out.size());}));
        return DocumentDetail::I32(DocumentDetail::Load(out,4,4));
    }
    // Canonical supplied default value lives in Rust. Native defaults/LOGFONT
    // preparation are caller operations; no native writable default-style loan.
    template<class Style> void SetDefaultStyle(const Style& value) {
        const auto bytes = DocumentDetail::StyleBytes(value);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_default_style_v1(id_,bytes.data(),bytes.size());}));
    }
    template<class Style = STSStyle> Style DefaultStyle() const { return DocumentDetail::StyleValue<Style>(Snapshot(2,0).Read()); }
    // Forced default bypasses entry access, including negative native indices.
    // The Rust resolver checks the raw index only in the unforced branch.
    template<class Style = STSStyle> Style ResolvedStyle(int index) const {
        return DocumentDetail::StyleValue<Style>(SnapshotRaw(8,static_cast<uint32_t>(index)).Read());
    }
    struct ResolvedCharset { bool has_style; int32_t value; };
    ResolvedCharset ResolvedCharSet(int index) const {
        const auto bytes = SnapshotRaw(9,static_cast<uint32_t>(index)).Read();
        DocumentDetail::Shape(bytes.size() == 8);
        const uint32_t state = static_cast<uint32_t>(DocumentDetail::Load(bytes,0,4));
        const int32_t value = DocumentDetail::I32(DocumentDetail::Load(bytes,4,4));
        DocumentDetail::Shape((state == 1 && value == -1) || state == 2);
        return {state == 2,value};
    }
    void CopySelectedState(const DocumentOwner& source) {
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_copy_selected_state_v1(id_,source.id_);}));
    }
    int32_t Metadata(uint32_t field) const {
        DocumentDetail::Shape(field <= 4); std::array<uint8_t,4> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_get_metadata_v1(id_,field,out.data(),out.size());}));
        return DocumentDetail::I32(DocumentDetail::Load(out,0,4));
    }
    void SetMetadata(uint32_t field, int32_t value) {
        DocumentDetail::Shape(field <= 4);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_metadata_v1(id_,field,value);}));
    }
    uint32_t LCIDValue() const { return static_cast<uint32_t>(Metadata(4)); }
    void SetLCID(uint32_t value) { SetMetadata(4, DocumentDetail::I32(value)); }
    double PARCompensation() const {
        static_assert(sizeof(double) == sizeof(uint64_t), "Native PAR bit transport width");
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_get_par_bits_v1(id_,out.data(),out.size());}));
        const uint64_t bits = DocumentDetail::Load(out,0,8);
        double value; std::memcpy(&value,&bits,sizeof value); return value;
    }
    void SetPARCompensation(double value) {
        static_assert(sizeof(double) == sizeof(uint64_t), "Native PAR bit transport width");
        uint64_t bits; std::memcpy(&bits,&value,sizeof bits);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_par_bits_v1(id_,bits);}));
    }
    CSize LayoutSize() const {
        const auto bytes = Snapshot(10,0).Read();
        DocumentDetail::Shape(bytes.size() == 8);
        return CSize(DocumentDetail::I32(DocumentDetail::Load(bytes,0,4)),DocumentDetail::I32(DocumentDetail::Load(bytes,4,4)));
    }
    void SetLayoutSize(const CSize& value) {
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_layout_size_v1(id_,value.cx,value.cy);}));
    }
    CSize ScreenSize() const {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_get_script_size_v1(id_,out.data(),out.size());}));
        return CSize(DocumentDetail::I32(DocumentDetail::Load(out,0,4)),DocumentDetail::I32(DocumentDetail::Load(out,4,4)));
    }
    void SetScreenSize(const CSize& value) {
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_script_size_v1(id_,value.cx,value.cy);}));
    }
    int32_t ScriptField(uint32_t field) const {
        DocumentDetail::Shape(field <= 6); std::array<uint8_t,4> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_get_script_field_v1(id_,field,out.data(),out.size());}));
        return DocumentDetail::I32(DocumentDetail::Load(out,0,4));
    }
    void SetScriptField(uint32_t field, int32_t value) {
        DocumentDetail::Shape(field <= 6);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_script_field_v1(id_,field,value);}));
    }
    int32_t Interpretation(uint32_t field) const {
        DocumentDetail::Shape(field <= 2); std::array<uint8_t,4> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_get_interpretation_v1(id_,field,out.data(),out.size());}));
        return DocumentDetail::I32(DocumentDetail::Load(out,0,4));
    }
    void SetInterpretation(uint32_t field, int32_t value) {
        DocumentDetail::Shape(field <= 2);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_interpretation_v1(id_,field,value);}));
    }
    CString Identity(uint32_t field) const {
        DocumentDetail::Shape(field <= 1); std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_open_v1(id_,5+field,0,out.data(),out.size());}));
        DocumentReply reply(DocumentDetail::Load(out,0,8)); const auto bytes = reply.Read();
        DocumentDetail::Shape(bytes.size()%2 == 0); size_t offset = 0;
        return DocumentDetail::String(bytes,offset,DocumentDetail::Count(bytes.size()/2));
    }
    void SetIdentity(uint32_t field, const CString& value) {
        DocumentDetail::Shape(field <= 1); const auto bytes = DocumentDetail::Units(value);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_identity_v1(id_,field,bytes.data(),bytes.size());}));
    }
    size_t NamedStyleCount() const {
        std::array<uint8_t,4> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_named_style_count_v1(id_,out.data(),out.size());}));
        return static_cast<size_t>(DocumentDetail::Load(out,0,4));
    }
    // 0 missing, 1 present-null, 2 supplied value. All materialization happens
    // after the Rust call and uses an independent immutable snapshot.
    template<class Style = STSStyle> uint32_t LookupNamedStyle(const CString& name, Style* output = nullptr) const {
        const auto key = DocumentDetail::Units(name); std::array<uint8_t,16> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_named_style_lookup_v1(id_,key.data(),key.size(),output?1:0,out.data(),out.size());}));
        const uint32_t state = static_cast<uint32_t>(DocumentDetail::Load(out,0,4));
        const uint64_t snapshot = DocumentDetail::Load(out,8,8);
        DocumentDetail::Shape(state <= 2 && DocumentDetail::Load(out,4,4) == 0 &&
            ((snapshot != 0) == (output != nullptr && state == 2)));
        if(snapshot) { DocumentReply reply(snapshot); *output = DocumentDetail::StyleValue<Style>(reply.Read()); }
        return state;
    }
    template<class Style> void PutNamedStyle(const CString& name, const Style& value) {
        const auto key = DocumentDetail::Units(name); const auto bytes = DocumentDetail::StyleBytes(value);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_named_style_put_v1(id_,key.data(),key.size(),bytes.data(),bytes.size());}));
    }
    void PutNullNamedStyle(const CString& name) {
        const auto key = DocumentDetail::Units(name);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_named_style_put_v1(id_,key.data(),key.size(),nullptr,0);}));
    }
    // Native classification and CRT naming have finished. Keep incoming-style
    // insertion/deletion in the caller after its conflict CString scope ends.
    template<class Style> void CommitStyleRename(const CString& old, const CString& replacement, const Style& previous) {
        const auto old_bytes = DocumentDetail::Units(old), new_bytes = DocumentDetail::Units(replacement);
        const auto style = DocumentDetail::StyleBytes(previous);
        const size_t total = DocumentDetail::AddSize(DocumentDetail::AddSize(
            DocumentDetail::AddSize(16,old_bytes.size()),new_bytes.size()),style.size());
        DocumentDetail::Bytes packet(16,0); packet.reserve(total);
        DocumentDetail::Put(packet,0,DocumentDetail::Count(old_bytes.size()/2),4);
        DocumentDetail::Put(packet,4,DocumentDetail::Count(new_bytes.size()/2),4);
        DocumentDetail::Put(packet,8,static_cast<uint64_t>(style.size()),8);
        packet.insert(packet.end(),old_bytes.begin(),old_bytes.end());
        packet.insert(packet.end(),new_bytes.begin(),new_bytes.end());
        packet.insert(packet.end(),style.begin(),style.end());
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_commit_style_rename_v1(id_,packet.data(),packet.size());}));
    }
    void RemoveNamedStyle(const CString& name) {
        const auto key = DocumentDetail::Units(name);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_named_style_remove_v1(id_,key.data(),key.size());}));
    }
    void ClearNamedStyles() {
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_named_style_clear_v1(id_);}));
    }
    bool NamedStyleNameAt(size_t ordinal, CString& value) const {
        const uint32_t index = DocumentDetail::Count(ordinal); std::array<uint8_t,8> out{};
        const uint32_t status = DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_open_v1(id_,3,index,out.data(),out.size());});
        if(status == XY_DOCUMENT_V1_OUT_OF_RANGE) return false; DocumentDetail::Check(status);
        DocumentReply reply(DocumentDetail::Load(out,0,8)); const auto bytes = reply.Read();
        DocumentDetail::Shape(bytes.size()%2 == 0); size_t offset = 0;
        value = DocumentDetail::String(bytes,offset,DocumentDetail::Count(bytes.size()/2)); return true;
    }
    template<class Style> bool NamedStyleValueAt(size_t ordinal, Style& value) const {
        const uint32_t index = DocumentDetail::Count(ordinal); std::array<uint8_t,8> out{};
        const uint32_t status = DocumentDetail::Invoke([&]{return xy_vsfilter_document_snapshot_open_v1(id_,4,index,out.data(),out.size());});
        if(status == XY_DOCUMENT_V1_OUT_OF_RANGE) return false; DocumentDetail::Check(status);
        DocumentReply reply(DocumentDetail::Load(out,0,8)); value = DocumentDetail::StyleValue<Style>(reply.Read()); return true;
    }
    void CopyFrom(const DocumentOwner& source) {DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_copy_storage_v1(id_,source.id_);}));}
    // Base document Empty only: renderer Deinit/virtual dispatch stay native.
    void Empty() {DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_empty_v1(id_);}));}
    void Clear() {DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_clear_storage_v1(id_);}));}
    void RemoveEntries() {DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_remove_entries_v1(id_);}));}
    void Rebuild() {DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_rebuild_segments_v1(id_);}));}
    DocumentTextOperation TextOperation(int i, uint32_t mode) const {
        const uint32_t index = DocumentDetail::Index(i);
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_text_plan_v1(
            id_,index,mode,out.data(),out.size());}));
        DocumentReply reply(DocumentDetail::Load(out,0,8));
        const auto bytes = reply.Read();
        return DecodeTextOperation(bytes);
    }
    DocumentTextOperation TextWriteOperation(int i, const CStringW& text, bool unicode) const {
        const uint32_t index = DocumentDetail::Index(i);
        const auto input = DocumentDetail::Units(text);
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_text_plan_v1(
            id_,index,unicode ? 1 : 0,input.data(),input.size(),out.data(),out.size());}));
        DocumentReply reply(DocumentDetail::Load(out,0,8));
        const auto operation = DecodeTextOperation(reply.Read());
        DocumentDetail::Shape(operation.action != 3 && !operation.strip_tags);
        return operation;
    }
    void SetText(int i, const CStringW& text, int flag = -1) {
        const auto bytes = DocumentDetail::Units(text); const uint32_t index = DocumentDetail::Index(i);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_prepared_text_v1(id_,index,flag,bytes.data(),bytes.size());}));
    }
    void SetStyle(int i, const CString& style) {
        const auto bytes = DocumentDetail::Units(style); const uint32_t index = DocumentDetail::Index(i);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_prepared_style_v1(id_,index,bytes.data(),bytes.size());}));
    }
    void SetTime(int i, bool start, REFERENCE_TIME value) {
        const uint32_t index = DocumentDetail::Index(i);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_set_entry_time_v1(id_,index,start ? 1 : 0,value);}));
    }
    void RenameStyle(const CString& old, const CString& replacement) {
        const auto old_bytes = DocumentDetail::Units(old), new_bytes = DocumentDetail::Units(replacement);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_rename_style_cstring_v1(id_,old_bytes.data(),old_bytes.size(),new_bytes.data(),new_bytes.size());}));
    }
    void Reorder(const std::vector<uint32_t>& order) {
        DocumentDetail::Count(order.size()); DocumentDetail::Bytes bytes;
        for(uint32_t index : order) DocumentDetail::Append(bytes,index,4);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_reorder_entries_v1(id_,bytes.data(),bytes.size());}));
    }
    void ReplaceTimes(const std::vector<std::pair<int64_t,int64_t>>& times) {
        DocumentDetail::Count(times.size()); DocumentDetail::Bytes bytes;
        for(const auto& time : times) {DocumentDetail::Append(bytes,static_cast<uint64_t>(time.first),8);DocumentDetail::Append(bytes,static_cast<uint64_t>(time.second),8);}
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_replace_entry_times_v1(id_,bytes.data(),bytes.size());}));
    }
    // Immutable scalar-only copy; no raw CString payload is materialized.
    std::vector<std::pair<int64_t,int64_t>> EntryTimes() const {
        const auto bytes = Snapshot(11,0).Read();
        DocumentDetail::Shape(bytes.size() >= XY_DOCUMENT_V1_TIME_HEADER_BYTES);
        const uint32_t count = DocumentDetail::Count(static_cast<size_t>(DocumentDetail::Load(bytes,0,4)));
        DocumentDetail::Shape(DocumentDetail::Load(bytes,4,4) == 0 &&
            bytes.size() == DocumentDetail::AddSize(XY_DOCUMENT_V1_TIME_HEADER_BYTES,
                DocumentDetail::MulSize(count,XY_DOCUMENT_V1_TIME_RECORD_BYTES)));
        std::vector<std::pair<int64_t,int64_t>> times;
        times.reserve(count);
        for(uint32_t i = 0; i < count; ++i) {
            const size_t offset = XY_DOCUMENT_V1_TIME_HEADER_BYTES + size_t(i) * XY_DOCUMENT_V1_TIME_RECORD_BYTES;
            times.emplace_back(DocumentDetail::I64(DocumentDetail::Load(bytes,offset,8)),
                DocumentDetail::I64(DocumentDetail::Load(bytes,offset+8,8)));
        }
        return times;
    }
    bool CommitTimeConversion(uint32_t target_mode, const std::vector<std::pair<int64_t,int64_t>>& times) {
        DocumentDetail::Count(times.size()); DocumentDetail::Bytes bytes;
        bytes.reserve(DocumentDetail::MulSize(times.size(),XY_DOCUMENT_V1_TIME_RECORD_BYTES));
        for(const auto& time : times) {
            DocumentDetail::Append(bytes,static_cast<uint64_t>(time.first),8);
            DocumentDetail::Append(bytes,static_cast<uint64_t>(time.second),8);
        }
        std::array<uint8_t,8> applied{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_commit_time_conversion_v1(
            id_,target_mode,bytes.data(),bytes.size(),applied.data(),applied.size());}));
        const uint64_t value = DocumentDetail::Load(applied,0,8);
        DocumentDetail::Shape(value <= 1);
        return value != 0;
    }
    void AnimationOr(int i, bool animated) {
        const uint32_t index = DocumentDetail::Index(i);
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_animation_or_v1(id_,index,animated ? 1 : 0);}));
    }
    bool CorrectLastEnd(REFERENCE_TIME bound) {
        std::array<uint8_t,16> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_correct_last_end_v1(id_,bound,out.data(),out.size());}));
        return DocumentDetail::Load(out,0,4) != 0;
    }
    DocumentQuery Query(uint32_t mode, REFERENCE_TIME time) const {
        std::array<uint8_t,16> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_query_time_v1(id_,mode,time,out.data(),out.size());}));
        DocumentDetail::Shape(DocumentDetail::Load(out,4,4) <= 1 && DocumentDetail::Load(out,12,4) == 0);
        return {DocumentDetail::I32(DocumentDetail::Load(out,0,4)),DocumentDetail::Load(out,4,4) != 0,DocumentDetail::Count(static_cast<size_t>(DocumentDetail::Load(out,8,4)))};
    }
};
// Each operation is fully owned scalar policy state. Native file services,
// virtual callbacks and parsers execute after registry admission has ended.
class OpenLifecycle {
    uint64_t id_ = 0;
    uint32_t count_;
public:
    enum Kind : uint32_t {
        InitialEmpty=0, CapturePosition=1, Parse=2, SyntaxWarning=3, Seek=4,
        RetryEmpty=5, AbortEmpty=6, SuccessHeader=7, Sidecar=8, Rebuild=9,
        UnknownStyles=10, DoneFalse=11, DoneTrue=12
    };
    struct Operation { Kind kind; uint32_t index; };
    OpenLifecycle(const DocumentOwner& owner, uint32_t count) : count_(count) {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_open_lifecycle_begin_v1(
            owner.id_,count,out.data(),out.size());}));
        id_ = DocumentDetail::Load(out,0,8);
    }
    OpenLifecycle(const OpenLifecycle&) = delete;
    OpenLifecycle& operator=(const OpenLifecycle&) = delete;
    ~OpenLifecycle() noexcept {
        if(id_) { const uint64_t id = id_; id_ = 0;
            DocumentDetail::Release(DocumentDetail::Invoke([&]{return xy_vsfilter_document_open_lifecycle_drop_v1(id);})); }
    }
    Operation Next(int acknowledgement = -2) {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_open_lifecycle_step_v1(
            id_,acknowledgement,out.data(),out.size());}));
        const uint32_t kind = static_cast<uint32_t>(DocumentDetail::Load(out,0,4));
        const uint32_t index = static_cast<uint32_t>(DocumentDetail::Load(out,4,4));
        DocumentDetail::Shape(kind <= DoneTrue && ((kind == Parse || kind == SuccessHeader) ? index < count_ : index == 0));
        return {static_cast<Kind>(kind),index};
    }
};
// Scoped policy state; the only native work between Step and Resume is the
// existing message formatting/dialog. No borrowed owner data crosses that call.
class UnknownStylesPass {
    uint64_t id_ = 0;
public:
    explicit UnknownStylesPass(const DocumentOwner& owner) {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_unknown_open_v1(
            owner.id_,out.data(),out.size());}));
        id_ = DocumentDetail::Load(out,0,8);
    }
    UnknownStylesPass(const UnknownStylesPass&) = delete;
    UnknownStylesPass& operator=(const UnknownStylesPass&) = delete;
    ~UnknownStylesPass() noexcept {
        if(id_) { const uint64_t id = id_; id_ = 0;
            DocumentDetail::Release(DocumentDetail::Invoke([&]{return xy_vsfilter_document_unknown_drop_v1(id);})); }
    }
    uint32_t Next(CString& warning_name) {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_unknown_step_v1(
            id_,out.data(),out.size());}));
        DocumentReply reply(DocumentDetail::Load(out,0,8));
        const auto bytes = reply.Read();
        DocumentDetail::Shape(bytes.size() >= 16);
        const uint32_t action = static_cast<uint32_t>(DocumentDetail::Load(bytes,0,4));
        const uint32_t units = DocumentDetail::Count(DocumentDetail::Size(DocumentDetail::Load(bytes,8,8)));
        DocumentDetail::Shape(action <= 2 && DocumentDetail::Load(bytes,4,4) == 0 &&
            (action == 2 || units == 0) && bytes.size() == DocumentDetail::AddSize(16,DocumentDetail::MulSize(units,2)));
        if(action == 2) { size_t offset = 16; warning_name = DocumentDetail::String(bytes,offset,units); }
        return action;
    }
    void Resume(bool accepted) {
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_unknown_resume_v1(id_,accepted ? 1 : 0);}));
    }
};
// Per-copy state and copied styles remain in Rust. Native code supplies only
// the same CRT-generated suffix candidates used by direct AddStyle calls.
class NamedStyleCopy {
    uint64_t id_ = 0;
public:
    NamedStyleCopy(const DocumentOwner& destination, const DocumentOwner& source, bool append) {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_copy_styles_open_v1(
            destination.id_,source.id_,append ? 1 : 0,out.data(),out.size());}));
        id_ = DocumentDetail::Load(out,0,8);
    }
    NamedStyleCopy(const NamedStyleCopy&) = delete;
    NamedStyleCopy& operator=(const NamedStyleCopy&) = delete;
    ~NamedStyleCopy() noexcept {
        if(id_) { const uint64_t id = id_; id_ = 0;
            DocumentDetail::Release(DocumentDetail::Invoke([&]{return xy_vsfilter_document_copy_styles_drop_v1(id);})); }
    }
    uint32_t Next(CString& name) {
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_copy_styles_step_v1(
            id_,out.data(),out.size());}));
        DocumentReply reply(DocumentDetail::Load(out,0,8));
        const auto bytes = reply.Read();
        DocumentDetail::Shape(bytes.size() >= 16);
        const uint32_t action = static_cast<uint32_t>(DocumentDetail::Load(bytes,0,4));
        const uint32_t units = DocumentDetail::Count(DocumentDetail::Size(DocumentDetail::Load(bytes,8,8)));
        DocumentDetail::Shape(action <= 2 && DocumentDetail::Load(bytes,4,4) == 0 &&
            (action == 2 || units == 0) && bytes.size() == DocumentDetail::AddSize(16,DocumentDetail::MulSize(units,2)));
        if(action == 2) { size_t offset = 16; name = DocumentDetail::String(bytes,offset,units); }
        return action;
    }
    bool Submit(const CString& candidate) {
        const auto bytes = DocumentDetail::Units(candidate);
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_copy_styles_submit_v1(
            id_,bytes.data(),bytes.size(),out.data(),out.size());}));
        const uint64_t applied = DocumentDetail::Load(out,0,8);
        DocumentDetail::Shape(applied <= 1);
        return applied != 0;
    }
};
// Rust owns classification and the complete successful conflict commit.
// CString digit parsing/formatting remains in the native AddStyle adapter.
class StyleAddition {
    uint64_t id_ = 0;
    CString name_;
public:
    StyleAddition(const DocumentOwner& owner, const CString& name, const STSStyle* value) {
        const auto name_bytes = DocumentDetail::Units(name);
        const auto style_bytes = value ? DocumentDetail::StyleBytes(*value) : DocumentDetail::Bytes();
        std::array<uint8_t,16> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_add_style_begin_v1(
            owner.id_,name_bytes.data(),name_bytes.size(),style_bytes.data(),style_bytes.size(),out.data(),out.size());}));
        id_ = DocumentDetail::Load(out,8,8);
        try {
            DocumentReply reply(DocumentDetail::Load(out,0,8));
            const auto bytes = reply.Read();
            DocumentDetail::Shape(bytes.size() >= 24);
            const uint32_t action = static_cast<uint32_t>(DocumentDetail::Load(bytes,0,4));
            const uint64_t run = DocumentDetail::Load(bytes,8,8);
            const uint32_t units = DocumentDetail::Count(DocumentDetail::Size(DocumentDetail::Load(bytes,16,8)));
            DocumentDetail::Shape(action <= 1 && DocumentDetail::Load(bytes,4,4) == 0 && run == id_ &&
                ((action == 0 && run == 0) || (action == 1 && run != 0)) &&
                bytes.size() == DocumentDetail::AddSize(24,DocumentDetail::MulSize(units,2)));
            if(action == 1) { size_t offset = 24; name_ = DocumentDetail::String(bytes,offset,units); }
        } catch(...) {
            if(id_) { const uint64_t id = id_; id_ = 0;
                DocumentDetail::Release(DocumentDetail::Invoke([&]{return xy_vsfilter_document_add_style_drop_v1(id);})); }
            throw;
        }
    }
    StyleAddition(const StyleAddition&) = delete;
    StyleAddition& operator=(const StyleAddition&) = delete;
    ~StyleAddition() noexcept {
        if(id_) { const uint64_t id = id_; id_ = 0;
            DocumentDetail::Release(DocumentDetail::Invoke([&]{return xy_vsfilter_document_add_style_drop_v1(id);})); }
    }
    bool NeedsSuffix() const { return id_ != 0; }
    const CString& Name() const { return name_; }
    bool Submit(const CString& candidate) {
        const auto bytes = DocumentDetail::Units(candidate);
        std::array<uint8_t,8> out{};
        DocumentDetail::Check(DocumentDetail::Invoke([&]{return xy_vsfilter_document_add_style_submit_v1(
            id_,bytes.data(),bytes.size(),out.data(),out.size());}));
        const uint64_t applied = DocumentDetail::Load(out,0,8);
        DocumentDetail::Shape(applied <= 1);
        return applied != 0;
    }
};
} // namespace XyRust
#endif // XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
