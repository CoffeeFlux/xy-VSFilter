#!/usr/bin/env python3
"""Compile the original C++ decoder and compare it with the real Rust C ABI.

No Python packages, Rust dependencies, Windows headers, or copied reference
algorithm are needed. Run in a VS Native Tools prompt on Windows (x86 or x64).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def run(command, **kwargs):
    try:
        return subprocess.run(command, cwd=ROOT, check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        for output in (error.stdout, error.stderr):
            if output:
                print(output.decode(errors="replace") if isinstance(output, bytes) else output, file=sys.stderr)
        raise


def original(path, commit):
    return run(["git", "show", f"{commit}:{path}"], capture_output=True).stdout


def extract_method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def extract_definition(source, signature):
    """Skip forward declarations when a signature also appears before a body."""
    start = source.index(signature)
    while ";" in source[start:source.index("{", start)]:
        start = source.index(signature, start + len(signature))
    return extract_method(source[start:], signature)


def capture_inputs():
    inputs = [ROOT / "Cargo.toml", ROOT / "Cargo.lock", ROOT / "include/xy_rust_text.h",
              ROOT / "include/xy_rust_ass.h", ROOT / "include/xy_rust_reader.h",
              ROOT / "include/xy_rust_drawing.h",
              ROOT / "include/xy_rust_drawing_tokens.h",
              ROOT / "include/xy_rust_fields.h", ROOT / "include/xy_rust_subrip.h", ROOT / "include/xy_rust_webvtt.h", ROOT / "include/xy_rust_realtext.h",
              ROOT / "src/subtitles/RealTextParser.cpp", ROOT / "src/subtitles/RealTextParser.h",
              ROOT / "include/xy_rust_segments.h",
              ROOT / "include/xy_rust_segment_insert.h",
              ROOT / "include/xy_rust_hdmv.h",
              ROOT / "include/xy_rust_dvb.h", ROOT / "include/xy_rust_golomb.h", ROOT / "include/xy_rust_fill.h",
              ROOT / "src/subtitles/Rasterizer.cpp", ROOT / "include/xy_rust_mask.h", ROOT / "include/xy_rust_bilinear.h", ROOT / "include/xy_rust_be_blur.h", ROOT / "include/xy_rust_gauss_integer.h", ROOT / "include/xy_rust_be_filter.h", ROOT / "include/xy_rust_transpose.h", ROOT / "include/xy_rust_byte_float.h",
              ROOT / "src/subtitles/xy_filter.cpp", ROOT / "src/subtitles/VobSubImage.cpp", ROOT / "include/xy_rust_vobsub.h",
              ROOT / "src/dsutil/GolombBuffer.cpp", ROOT / "src/dsutil/GolombBuffer.h",
              ROOT / "src/subtitles/CompositionObject.cpp",
              ROOT / "include/xy_rust_timeline_search.h",
              ROOT / "src/subtitles/TextFile.h",
              ROOT / "src/subtitles/TextFile.cpp", ROOT / "src/subtitles/STS.cpp",
              ROOT / "src/subtitles/RTS.cpp", ROOT / "tools/compat/reference.json"]
    inputs += sorted((ROOT / "tools/compat").glob("*oracle.cpp"))
    inputs += sorted((ROOT / "tools/compat").glob("*shell.h"))
    inputs += [ROOT / "tools/compat/check_decoder.py"]
    inputs += sorted((ROOT / "rust").rglob("*.rs")) + sorted((ROOT / "rust").rglob("Cargo.toml"))
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in inputs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", help="Rust target; must match the C++ compiler")
    parser.add_argument("--cxx", default="cl" if os.name == "nt" else "c++")
    parser.add_argument("--sanitize", action="store_true", help="Clang/GCC ASan + UBSan")
    args = parser.parse_args()
    reference = json.loads((ROOT / "tools/compat/reference.json").read_text())
    destination = ROOT / "artifacts/decoder" / (args.target or "host")
    destination.mkdir(parents=True, exist_ok=True)
    # A failed rerun must never leave a previous run's successful report behind.
    (destination / "report.json").write_text(json.dumps({"complete": False, "reference": reference}) + "\n")
    candidate_inputs = capture_inputs()
    candidate_commit = run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    source = original("src/subtitles/TextFile.cpp", reference["commit"])
    header = original("include/Utf8.h", reference["commit"])
    (destination / "Utf8.h").write_bytes(header)
    method = extract_method(source.decode("utf-8-sig"), "int CTextFile::NextChar()")
    # Minimal platform-independent class shell; method body is verbatim.
    shell = '''#include <cstddef>
#include "Utf8.h"
class CTextFile {
public:
    enum enc { DEFAULT_ENCODING, UTF8, LE16, BE16 };
    enum charerror { CHARERR_NEED_MORE=-1, CHARERR_REOPEN=-2 };
    enc m_encoding;
    unsigned char m_readbuffer[16];
    size_t m_bufferPos, m_bufferCount;
    int NextChar();
};
'''
    license_header = source.decode("utf-8-sig").split("#include", 1)[0]
    (destination / "original_next_char.h").write_text(license_header + shell + method, encoding="utf-8")
    candidate_source = (ROOT / "src/subtitles/TextFile.cpp").read_bytes()
    methods = ["bool CTextFile::Open(", "ULONGLONG CTextFile::GetPosition(",
               "ULONGLONG CTextFile::GetLength(", "ULONGLONG CTextFile::Seek(",
               "bool CTextFile::ReadLine(", "int CTextFile::NextChar("]
    reader = license_header + '''#include "text_reader_shell.h"
#include "Utf8.h"
#include "xy_rust_text.h"
#define __super MemoryFile
#define XY_LOG_ERROR(message) (diagnostics.push_back(c))
'''
    for name, content in (("OriginalTextFile", source), ("CandidateTextFile", candidate_source)):
        state = "OriginalTextState" if name == "OriginalTextFile" else "CandidateTextState"
        reader += f"\nTEXT_READER_SHELL({name}, {state})\n#define CTextFile {name}\n"
        if name == "CandidateTextFile":
            reader += "#define XY_VSFILTER_RUST_TEXT\n"
        reader += "\n".join(extract_method(content.decode("utf-8-sig"), signature) for signature in methods)
        reader += "\n#undef CTextFile\n#undef XY_VSFILTER_RUST_TEXT\n"
    reader += "#undef __super\n#undef XY_LOG_ERROR\n"
    (destination / "original_text_reader.h").write_text(reader, encoding="utf-8")
    font_source = original("src/subtitles/STS.cpp", reference["commit"])
    candidate_font = (ROOT / "src/subtitles/STS.cpp").read_bytes()
    font = font_source.decode("utf-8-sig").split("#include", 1)[0]
    font += '#include "font_shell.h"\n#include "xy_rust_text.h"\n'
    for name, content in (("OriginalFont", font_source), ("CandidateFont", candidate_font)):
        text = content.decode("utf-8-sig")
        start = text.index("static bool LoadFont(CString& font)")
        end = text.index("    HANDLE hFont = INVALID_HANDLE_VALUE;", start)
        font += f"namespace {name} {{\nstd::vector<BYTE> decoded;\n"
        if name == "CandidateFont":
            font += "#define XY_VSFILTER_RUST_TEXT\n"
        # Stop exactly where Windows font registration starts; retain the entire
        # original decode/allocation/length-check block without rewriting it.
        font += text[start:end] + '\n decoded.assign((BYTE*)pData, (BYTE*)pData + datalen);\n return true;\n}\n}\n'
        font += "#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_font.h").write_text(font, encoding="utf-8")
    realtext_source = original("src/subtitles/RealTextParser.cpp", reference["commit"])
    realtext_header = original("src/subtitles/RealTextParser.h", reference["commit"])
    realtext_class = extract_method(realtext_header.decode("utf-8-sig"), "class CRealTextParser")
    realtext = '#include "realtext_shell.h"\n'
    signatures = ["CRealTextParser::CRealTextParser()", "CRealTextParser::~CRealTextParser()",
                  "bool CRealTextParser::ParseRealText(", "const CRealTextParser::Subtitles& CRealTextParser::GetParsedSubtitles(",
                  "bool CRealTextParser::ExtractTag(", "bool CRealTextParser::ExtractTextTag(",
                  "bool CRealTextParser::ExtractString(", "bool CRealTextParser::SkipSpaces(",
                  "bool CRealTextParser::GetString(", "bool CRealTextParser::GetAttributes(",
                  "int CRealTextParser::GetTimecode(", "std::wstring CRealTextParser::StringToLower(",
                  "std::wstring CRealTextParser::RenderTags(", "void CRealTextParser::PopTag("]
    for name, content in (("ReferenceRealText", realtext_source), ("CandidateRealText", (ROOT / "src/subtitles/RealTextParser.cpp").read_bytes())):
        realtext += f"namespace {name} {{\n#define private public\n" + realtext_class + ";\n#undef private\n"
        if name == "CandidateRealText":
            realtext += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in signatures:
            definition = extract_definition(content.decode("utf-8-sig"), signature)
            if signature == "std::wstring CRealTextParser::StringToLower(":
                brace = definition.index("{") + 1
                definition = definition[:brace] + '\n    realTextAttributeHook(2, p_crszString);\n' + definition[brace:]
            if signature == "bool CRealTextParser::ParseRealText(":
                plain = "m_RealText.m_mapLines[pairTimecodes] = szLine;"
                first = "m_RealText.m_mapLines[std::make_pair(0, iStartTimecode)] = szLine;"
                expected = 4 if name == "ReferenceRealText" else 6
                if definition.count(plain) != expected or definition.count(first) != 1:
                    raise RuntimeError("RealText line insertion checkpoints changed")
                definition = definition.replace(plain, "m_RealText.m_mapLines[realTextLineKey(m_RealText.m_mapLines, pairTimecodes, szLine)] = szLine;")
                definition = definition.replace(first, "m_RealText.m_mapLines[realTextLineKey(m_RealText.m_mapLines, std::make_pair(0, iStartTimecode), szLine)] = szLine;")
            realtext += definition + "\n"
        realtext += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_realtext.h").write_text(realtext, encoding="utf-8")
    webvtt = '#include "webvtt_shell.h"\n'
    for name, content in (("ReferenceWebVTT", font_source), ("CandidateWebVTT", candidate_font)):
        webvtt += f"namespace {name} {{\n"
        if name == "CandidateWebVTT":
            webvtt += "#define XY_VSFILTER_RUST_TEXT\n"
        webvtt += extract_definition(content.decode("utf-8-sig"), "static void WebVTTCueStrip(") + "\n"
        webvtt += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_webvtt.h").write_text(webvtt, encoding="utf-8")
    subrip = '#include "subrip_shell.h"\n'
    for name, content in (("ReferenceSubRip", font_source), ("CandidateSubRip", candidate_font)):
        subrip += f"namespace {name} {{\n"
        if name == "CandidateSubRip":
            subrip += "#define XY_VSFILTER_RUST_TEXT\n"
        subrip += extract_definition(content.decode("utf-8-sig"), "static CStringW SubRipper2SSA(") + "\n"
        subrip += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_subrip.h").write_text(subrip, encoding="utf-8")
    fields = font_source.decode("utf-8-sig").split("#include", 1)[0]
    fields += '#include "fields_shell.h"\n'
    for name, content in (("OriginalFields", font_source), ("CandidateFields", candidate_font)):
        fields += f"namespace {name} {{\n"
        if name == "CandidateFields":
            fields += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static inline CStringW::PCXSTR TryNextStr(", "static inline CStringW GetStr("):
            fields += extract_method(content.decode("utf-8-sig"), signature) + "\n"
        fields += "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_fields.h").write_text(fields, encoding="utf-8")
    segment_header = original("src/subtitles/STS.h", reference["commit"])
    segment_class = extract_method(segment_header.decode("utf-8-sig"), "class STSSegment")
    segments = font_source.decode("utf-8-sig").split("#include", 1)[0]
    segments += '#include "segments_shell.h"\n' + segment_class + ";\n"
    for name, content in (("OriginalSegments", font_source), ("CandidateSegments", candidate_font)):
        segments += f"SEGMENTS_SHELL({name})\n#define CSimpleTextSubtitle {name}\n"
        if name == "CandidateSegments":
            segments += "#define XY_VSFILTER_RUST_TEXT\n"
        segments += extract_method(content.decode("utf-8-sig"), "void CSimpleTextSubtitle::CreateSegments()")
        segments += "\n#undef CSimpleTextSubtitle\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_segments.h").write_text(segments, encoding="utf-8")
    insertion = '#include "original_segments.h"\n#include "segment_insert_shell.h"\n'
    insertion += segment_class.replace("STSSegment", "InsertionSegment").replace("CAtlArray<int>", "ObservedArray<int>") + ";\n"
    insertion += extract_method(font_source.decode("utf-8-sig"), "static bool SegmentCompStart(").replace("STSSegment", "InsertionSegment") + "\n"
    for name, content in (("OriginalInsertion", font_source), ("CandidateInsertion", candidate_font)):
        method = extract_method(content.decode("utf-8-sig"), "void CSimpleTextSubtitle::Add(")
        insertion += f"INSERTION_SHELL({name})\n#define STSSegment InsertionSegment\n"
        if name == "CandidateInsertion":
            insertion += "#define XY_VSFILTER_RUST_TEXT\n"
        # The unchanged string/style/WebVTT preprocessing remains native. Feed
        # its prepared entry to the exact original/candidate timeline tail.
        insertion += f"void {name}::AddPrepared(REFERENCE_TIME start, REFERENCE_TIME end, int readorder) {{\n"
        insertion += "if (start > end) return;\nPreparedEntry sub = {start,end,0};\n"
        a = method.index("    sub.readorder  =")
        insertion += method[a:method.index(";", a)+1] + "\n"
        insertion += method[method.index("    int n = (int)m_entries.GetCount();"):]
        insertion += "\n#undef STSSegment\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_segment_insert.h").write_text(insertion, encoding="utf-8")
    searches = '#include "original_segments.h"\n#include "timeline_search_shell.h"\n'
    for name, content in (("OriginalTimelineSearch", font_source), ("CandidateTimelineSearch", candidate_font)):
        searches += f"TIMELINE_SEARCH_SHELL({name})\n#define CSimpleTextSubtitle {name}\n"
        if name == "CandidateTimelineSearch":
            searches += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("int CSimpleTextSubtitle::SearchSub(",
                          "const STSSegment* CSimpleTextSubtitle::SearchSubs(",
                          "STSSegment* CSimpleTextSubtitle::SearchSubs2("):
            searches += extract_method(content.decode("utf-8-sig"), signature) + "\n"
        for method in ("TranslateStart", "TranslateSegmentStart", "TranslateSegmentEnd"):
            block = extract_method(content.decode("utf-8-sig"), "REFERENCE_TIME CSimpleTextSubtitle::" + method + "(")
            searches += block.replace("::" + method + "(", "::" + method + "Impl(", 1) + "\n"
        searches += "\n#undef CSimpleTextSubtitle\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_timeline_search.h").write_text(searches, encoding="utf-8")
    lexer_source = original("src/subtitles/RTS.cpp", reference["commit"])
    candidate_lexer = (ROOT / "src/subtitles/RTS.cpp").read_bytes()
    lexer = lexer_source.decode("utf-8-sig").split("#include", 1)[0]
    lexer += '#include "ass_lexer_shell.h"\n#include "xy_rust_ass.h"\n'
    for name, content in (("OriginalLexer", lexer_source), ("CandidateLexer", candidate_lexer)):
        text = content.decode("utf-8-sig")
        lexer += f"namespace {name} {{\n"
        for signature in ("inline CStringW::PCXSTR SkipWhiteSpaceLeft(CStringW::PCXSTR start",
                          "inline CStringW::PCXSTR FastSkipWhiteSpaceRight(",
                          "inline CStringW::PCXSTR FindChar("):
            lexer += extract_method(text, signature) + "\n"
        start = text.index("bool CRenderedTextSubtitle::ParseSSATag( AssTagList *assTags, const CStringW& str )")
        end = text.index("        AssCmdType cmd_type = CMD_COUNT;", start)
        if name == "CandidateLexer":
            lexer += "#define XY_VSFILTER_RUST_TEXT\n"
        # Capture the exact lexical block; command dispatch/style application
        # are unchanged and are exercised by the complete Windows DLL corpus.
        lexer += text[start:end].replace("CRenderedTextSubtitle::", "", 1)
        lexer += "\n assTag.command = cmd;\n }\n return true;\n}\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_ass_lexer.h").write_text(lexer, encoding="utf-8")
    drawing = lexer_source.decode("utf-8-sig").split("#include", 1)[0]
    drawing += '#include "drawing_shell.h"\n'
    original_drawing_text = lexer_source.decode("utf-8-sig")
    token_start = original_drawing_text.index("    CStringW str = m_str.Get();", original_drawing_text.index("bool CPolygon::ParseStr()"))
    token_end = original_drawing_text.index("        switch(c)", token_start)
    original_token_input = original_drawing_text[token_start:token_end]
    for name, content in (("OriginalDrawing", lexer_source), ("CandidateDrawing", candidate_lexer)):
        text = content.decode("utf-8-sig")
        start = text.index("bool CPolygon::ParseStr()")
        end = text.index("    int minx =", start)
        drawing += f"DRAWING_SHELL({name})\n#define CPolygon {name}\n"
        if name == "CandidateDrawing":
            drawing += "#define XY_VSFILTER_RUST_TEXT\n"
        # Keep the complete command-state block unchanged. CString tokenization
        # and GetPOINT are supplied identical results; native DLL tests cover
        # their real CRT behavior plus the unchanged scaling/rasterization tail.
        block = text[start:end]
        if name == "CandidateDrawing":
            # This oracle supplies synthetic GetPOINT outcomes to isolate path
            # state. The separate token oracle below tests the real new input
            # boundary against ATL. Substitute only that input-delivery region.
            a = block.index("    CStringW str = m_str.Get();")
            b = block.index("        // Drawing command state.", a)
            block = block[:a] + original_token_input + block[b:]
        drawing += block + "return true;\n}\n#undef CPolygon\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_drawing.h").write_text(drawing, encoding="utf-8")
    token_code = lexer_source.decode("utf-8-sig").split("#include", 1)[0]
    token_code += '#include "drawing_tokens_shell.h"\n'
    for name, content in (("OriginalDrawingTokens", lexer_source), ("CandidateDrawingTokens", candidate_lexer)):
        text = content.decode("utf-8-sig")
        if name == "OriginalDrawingTokens":
            block = original_token_input[original_token_input.index("    int k = 0;"):]
        else:
            a = text.index("    // Drawing token input boundary.")
            b = text.index("        // Drawing command state.", a)
            block = text[a:b]
            token_code += "#define XY_VSFILTER_RUST_TEXT\n"
        token_code += f"namespace {name} {{\nDrawingTokenList Scan(const CStringW& input) {{\nDrawingTokenList result;\n"
        token_code += "CStringW str = input;\n" + block
        token_code += "result.emplace_back(c, s);\n}\nreturn result;\n}\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_drawing_tokens.h").write_text(token_code, encoding="utf-8")
    hdmv_source = original("src/subtitles/CompositionObject.cpp", reference["commit"])
    candidate_hdmv = (ROOT / "src/subtitles/CompositionObject.cpp").read_bytes()
    golomb_source = original("src/dsutil/GolombBuffer.cpp", reference["commit"])
    golomb_header = original("src/dsutil/GolombBuffer.h", reference["commit"])
    raster_source = original("src/subtitles/Rasterizer.cpp", reference["commit"])
    filter_source = original("src/subtitles/xy_filter.cpp", reference["commit"])
    vobsub_source = original("src/subtitles/VobSubImage.cpp", reference["commit"])
    golomb = '#pragma once\n#include "golomb_shell.h"\n'
    candidate_golomb = (ROOT / "src/dsutil/GolombBuffer.cpp").read_bytes()
    candidate_golomb_header = (ROOT / "src/dsutil/GolombBuffer.h").read_bytes()
    for name, content, declaration in (("ReferenceGolomb", golomb_source, golomb_header),
                                      ("CandidateGolomb", candidate_golomb, candidate_golomb_header)):
        golomb += content.decode("utf-8-sig").split("#include", 1)[0]
        # Fields are public only in the observation harness. Method bodies are
        # extracted verbatim apart from the spelling of MSVC's 64-bit literal.
        golomb += extract_method(declaration.decode("utf-8-sig"), "class CGolombBuffer").replace("private:", "public:").replace("CGolombBuffer", name) + ";\n"
        if name == "CandidateGolomb":
            golomb += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("CGolombBuffer::CGolombBuffer(", "CGolombBuffer::~CGolombBuffer(",
                          "UINT64 CGolombBuffer::BitRead(", "UINT64 CGolombBuffer::UExpGolombRead(",
                          "INT64 CGolombBuffer::SExpGolombRead(", "void CGolombBuffer::Reset()",
                          "void CGolombBuffer::Reset(BYTE*", "void CGolombBuffer::ReadBuffer(",
                          "void CGolombBuffer::BitByteAlign()", "int CGolombBuffer::GetPos()",
                          "void CGolombBuffer::SkipBytes("):
            golomb += extract_method(content.decode("utf-8-sig"), signature).replace("CGolombBuffer", name).replace("1ui64", "UINT64_C(1)") + "\n"
        golomb += "#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_golomb.h").write_text(golomb, encoding="utf-8")
    hdmv = hdmv_source.decode("utf-8-sig").split("#include", 1)[0]
    hdmv += '#include "hdmv_shell.h"\n#include "original_golomb.h"\n'
    hdmv += """
template<class Reader> class ObservedGolombBase : public Reader {
public:
    using Reader::m_nBitPos;
    using Reader::m_bitlen;
    using Reader::m_bitbuff;
    ObservedGolombBase(BYTE* data,int size):Reader(data,size){}
    void Seed(int bits) { if(bits)Reader::BitRead(bits>0 ? bits : -bits,bits<0); }
    bool IsEOF() const {
        const bool value=Reader::IsEOF();
        hdmvObserve({{1,m_nBitPos,m_bitlen,m_bitbuff,value,0,0}});
        return value;
    }
    BYTE ReadByte() {
        const int before=m_nBitPos;
        const BYTE value=Reader::ReadByte();
        hdmvObserve({{2,before,m_nBitPos,m_bitlen,m_bitbuff,value,0}});
        return value;
    }
};
typedef ObservedGolombBase<ReferenceGolomb> ObservedGolomb;
typedef ObservedGolombBase<CandidateGolomb> CandidateObservedGolomb;
"""
    for name, content in (("OriginalHdmv", hdmv_source), ("CandidateHdmv", candidate_hdmv)):
        reader = "ObservedGolomb" if name == "OriginalHdmv" else "CandidateObservedGolomb"
        hdmv += f"#define CGolombBuffer {reader}\n#define CompositionObject {name}\n"
        if name == "CandidateHdmv":
            hdmv += "#define XY_VSFILTER_RUST_TEXT\n"
        hdmv += extract_method(content.decode("utf-8-sig"), "void CompositionObject::RenderHdmv(")
        hdmv += "\n#undef CompositionObject\n#undef XY_VSFILTER_RUST_TEXT\n#undef CGolombBuffer\n"
    hdmv += "#undef CGolombBuffer\n#if HDMV_NATIVE_PIXELS\n"
    for name, content in (("ReferencePixels", raster_source),
                          ("CandidatePixels", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        hdmv += f"namespace {name} {{\n"
        if name == "CandidatePixels":
            hdmv += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static __forceinline void pixmix_c(DWORD *",
                          "static __forceinline void pixmix_sse2(DWORD*",
                          "static __forceinline void pixmix2(DWORD *",
                          "static __forceinline void pixmix2_sse2(DWORD*",
                          "static __forceinline __m128i packed_pix_mix_sse2(",
                          "static __forceinline void packed_pix_mix_c(BYTE* dst, const BYTE* alpha",
                          "static __forceinline void packed_pix_mix_sse2(BYTE* dst, const BYTE* alpha",
                          "static __forceinline void  packed_pix_mix_c              (BYTE* dst, BYTE alpha",
                          "static __forceinline void packed_pix_mix_sse2(BYTE* dst, BYTE alpha",
                          "static __forceinline void packed_pix_add_c(BYTE*",
                          "static __forceinline __m128i packed_pix_add_sse2(",
                          "static __forceinline void packed_pix_add_sse2(BYTE*",
                          "void AlphaBlt8bppC(byte*",
                          "void AlphaBlt8bppSse2(byte*",
                          "void AlphaBlt8bppSingleAlphaC(byte*",
                          "void AlphaBlt8bppSingleAlphaSse2(byte*",
                          "void Rasterizer::FillSolidRect("):
            hdmv += extract_method(content.decode("utf-8-sig"), signature) + "\n"
        hdmv += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    hdmv += "#endif\n"
    (destination / "original_hdmv.h").write_text(hdmv, encoding="utf-8")
    vobsub = '#include "vobsub_packet_shell.h"\n'
    for name, content in (("ReferenceVobSub", vobsub_source), ("CandidateVobSub", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub += f"VOBSUB_PACKET_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobSub":
            vobsub += "#define XY_VSFILTER_RUST_TEXT\n"
        vobsub += extract_definition(content.decode("utf-8-sig"), "void CVobSubImage::GetPacketInfo(") + "\n"
        vobsub += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_packet.h").write_text(vobsub, encoding="utf-8")
    vobsub_pixels = '#include "vobsub_pixels_shell.h"\n'
    for name, content in (("ReferenceVobPixels", vobsub_source), ("CandidateVobPixels", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_pixels += f"VOBSUB_PIXELS_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobPixels":
            vobsub_pixels += "#define XY_VSFILTER_RUST_TEXT\n"
        vobsub_pixels += extract_definition(content.decode("utf-8-sig"), "void CVobSubImage::DrawPixels(") + "\n"
        vobsub_pixels += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_pixels.h").write_text(vobsub_pixels, encoding="utf-8")
    vobsub_rle = '#include "vobsub_rle_shell.h"\n'
    for name, content in (("ReferenceVobRle", vobsub_source), ("CandidateVobRle", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_rle += f"VOBSUB_RLE_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobRle":
            vobsub_rle += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("bool CVobSubImage::Decode(", "BYTE CVobSubImage::GetNibble(", "void CVobSubImage::DrawPixels(", "void CVobSubImage::TrimSubImage("):
            definition = extract_definition(content.decode("utf-8-sig"), signature)
            definition = definition.replace("CVobSubImage::GetNibble(", "CVobSubImage::GetNibbleImpl(", 1).replace("CVobSubImage::DrawPixels(", "CVobSubImage::DrawPixelsImpl(", 1)
            definition = definition.replace("CVobSubImage::TrimSubImage(", "CVobSubImage::TrimSubImageImpl(", 1)
            vobsub_rle += definition + "\n"
        vobsub_rle += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_rle.h").write_text(vobsub_rle, encoding="utf-8")
    vobsub_trim = '#include "vobsub_trim_shell.h"\n'
    for name, content in (("ReferenceVobTrim", vobsub_source), ("CandidateVobTrim", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_trim += f"VOBSUB_TRIM_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobTrim":
            vobsub_trim += "#define XY_VSFILTER_RUST_TEXT\n"
        vobsub_trim += extract_definition(content.decode("utf-8-sig"), "void CVobSubImage::TrimSubImage(") + "\n"
        vobsub_trim += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_trim.h").write_text(vobsub_trim, encoding="utf-8")
    vobsub_scale = '#include "vobsub_scale_shell.h"\n'
    for name, content in (("ReferenceVobScale", vobsub_source), ("CandidateVobScale", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_scale += f"VOBSUB_SCALE_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobScale":
            vobsub_scale += "#define XY_VSFILTER_RUST_TEXT\n"
        vobsub_scale += extract_definition(content.decode("utf-8-sig"), "void CVobSubImage::Scale2x(") + "\n"
        vobsub_scale += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_scale.h").write_text(vobsub_scale, encoding="utf-8")
    vobsub_outline = '#include "vobsub_outline_shell.h"\n'
    for name, content in (("ReferenceVobOutline", vobsub_source), ("CandidateVobOutline", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_outline += f"VOBSUB_OUTLINE_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobOutline":
            vobsub_outline += "#define XY_VSFILTER_RUST_TEXT\n"
        outline_method = extract_definition(content.decode("utf-8-sig"), "CAutoPtrList<COutline>* CVobSubImage::GetOutlineList(")
        vobsub_outline += outline_method + "\n"
        walk_start = outline_method.index("            CPoint pp;")
        walk_end = outline_method.index("            int d = dir - prevdir;", walk_start)
        walk_body = outline_method[walk_start:walk_end].replace("CPoint pp;", "", 1)
        vobsub_outline += f"static std::vector<int32_t> {name}Walk(std::vector<BYTE>& grid,int w,int h,int x,int y,int dir) {{\nBYTE* p=grid.data();CPoint pp;enum {{ UP, RIGHT, DOWN, LEFT }};bool completed=false;do {{\n"
        vobsub_outline += walk_body + "\ncompleted=true;\n}while(false);return {x,y,dir,pp.x,pp.y,completed ? 1 : 0};\n}\n"
        vobsub_outline += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_outline.h").write_text(vobsub_outline, encoding="utf-8")
    vobsub_segment = '#include "vobsub_segment_shell.h"\n'
    for name, content in (("ReferenceVobSegment", vobsub_source), ("CandidateVobSegment", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_segment += f"VOBSUB_SEGMENT_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobSegment":
            vobsub_segment += "#define XY_VSFILTER_RUST_TEXT\n"
        vobsub_segment += extract_definition(content.decode("utf-8-sig"), "int CVobSubImage::GrabSegment(") + "\n"
        vobsub_segment += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_segment.h").write_text(vobsub_segment, encoding="utf-8")
    vobsub_split = '#include "vobsub_split_shell.h"\n'
    for name, content in (("ReferenceVobSplit", vobsub_source), ("CandidateVobSplit", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_split += f"VOBSUB_SPLIT_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobSplit":
            vobsub_split += "#define XY_VSFILTER_RUST_TEXT\n"
        split_method = extract_definition(content.decode("utf-8-sig"), "void CVobSubImage::SplitOutline(")
        # MSVC accepts the pinned two-word functional casts; Clang/GCC require
        # standard cast spelling. Windows compiles the original spelling intact.
        if os.name != "nt":
            for expression in ("j - i", "i", "j"):
                old = f"unsigned int({expression})"
                assert split_method.count(old) == 1
                split_method = split_method.replace(old, f"static_cast<unsigned int>({expression})")
        vobsub_split += split_method + "\n"
        selection_start = split_method.index("#ifdef XY_VSFILTER_RUST_TEXT") if name == "CandidateVobSplit" else split_method.index("    size_t maxlen =")
        selection_end = split_method.index("    o1.RemoveAll();", selection_start)
        vobsub_split += f"static std::vector<size_t> {name}Select(const CAtlArray<UINT>& la,const CAtlArray<UINT>& sa,const CAtlArray<UINT>& ea) {{\nsize_t i,j,k;\n"
        vobsub_split += split_method[selection_start:selection_end] + "return {j,k};\n}\n"
        vobsub_split += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_split.h").write_text(vobsub_split, encoding="utf-8")
    vobsub_path = '#include "vobsub_path_shell.h"\n'
    for name, content in (("ReferenceVobPath", vobsub_source), ("CandidateVobPath", (ROOT / "src/subtitles/VobSubImage.cpp").read_bytes())):
        vobsub_path += f"VOBSUB_PATH_SHELL({name})\n#define CVobSubImage {name}\n"
        if name == "CandidateVobPath":
            vobsub_path += "#define XY_VSFILTER_RUST_TEXT\n"
        vobsub_path += extract_definition(content.decode("utf-8-sig"), "bool CVobSubImage::Polygonize(CStringW&") + "\n"
        vobsub_path += "#undef CVobSubImage\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_vobsub_path.h").write_text(vobsub_path, encoding="utf-8")
    byte_float = '#include "byte_float_shell.h"\n'
    for name, content in (("ReferenceByteFloat", filter_source), ("CandidateByteFloat", (ROOT / "src/subtitles/xy_filter.cpp").read_bytes())):
        byte_float += f"namespace {name} {{\n"
        if name == "CandidateByteFloat":
            byte_float += "#define XY_VSFILTER_RUST_TEXT\n"
        byte_float += extract_definition(content.decode("utf-8-sig"), "void xy_byte_2_float_c(") + "\n"
        byte_float += "#if BYTE_FLOAT_NATIVE_SSE\n" + extract_definition(content.decode("utf-8-sig"), "void xy_byte_2_float_sse(") + "\n#endif\n"
        byte_float += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_byte_float.h").write_text(byte_float, encoding="utf-8")
    transpose = '#include "transpose_shell.h"\n'
    for name, content in (("ReferenceTranspose", filter_source), ("CandidateTranspose", (ROOT / "src/subtitles/xy_filter.cpp").read_bytes())):
        transpose += f"namespace {name} {{\n"
        if name == "CandidateTranspose":
            transpose += "#define XY_VSFILTER_RUST_TEXT\n"
        transpose += extract_definition(content.decode("utf-8-sig"), "void xy_byte_2_byte_transpose_c(") + "\n"
        transpose += extract_definition(content.decode("utf-8-sig"), "void xy_float_2_float_transpose_c(") + "\n"
        transpose += "#if TRANSPOSE_NATIVE_SSE\n" + extract_definition(content.decode("utf-8-sig"), "void xy_float_2_float_transpose_sse(") + "\n#endif\n"
        transpose += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_transpose.h").write_text(transpose, encoding="utf-8")
    filters = '#include "be_filter_shell.h"\n'
    for name, content in (("ReferenceFilter", filter_source), ("CandidateFilter", (ROOT / "src/subtitles/xy_filter.cpp").read_bytes())):
        filters += f"namespace {name} {{\n"
        text_source = content.decode("utf-8-sig")
        rounding = text_source[text_source.index("enum RoundingPolicy"):text_source.index("template<int ROUNDING_POLICY>\r\nvoid xy_be_filter_c(") if "template<int ROUNDING_POLICY>\r\nvoid xy_be_filter_c(" in text_source else text_source.index("template<int ROUNDING_POLICY>\nvoid xy_be_filter_c(")]
        filters += "#if FILTER_NATIVE_SSE\n" + rounding + "\n#else\n"
        # arm64 compares the verbatim scalar rounding methods; no surrogate SSE.
        filters += extract_definition(text_source, "enum RoundingPolicy") + ";\n"
        filters += "template<int POLICY, int precision> struct XyRounding;\n"
        for policy in ("ROUND_DOWN", "ROUND_HALF_DOWN", "ROUND_HALF_UP", "ROUND_HALF_TO_EVEN"):
            definition = extract_definition(text_source, f"struct XyRounding<{policy}, precision>")
            filters += f"template<int precision> struct XyRounding<{policy}, precision> {{\n"
            filters += extract_definition(definition, "__forceinline int round(unsigned in)") + "\n};\n"
        filters += "#endif\n"
        if name == "CandidateFilter":
            filters += "#define XY_VSFILTER_RUST_TEXT\n"
        filters += "template<int ROUNDING_POLICY>\n" + extract_definition(text_source, "void xy_be_filter_c(") + "\n"
        filters += "#if FILTER_NATIVE_SSE\ntemplate<int ROUNDING_POLICY>\n" + extract_definition(text_source, "void xy_be_filter_sse(") + "\n#endif\n"
        filters += "template<int ROUNDING_POLICY>\n" + extract_definition(text_source, "void xy_be_filter2_c(") + "\n"
        filters += "#if FILTER_NATIVE_SSE\ntemplate<int ROUNDING_POLICY>\n" + extract_definition(text_source, "void xy_be_filter2_sse(") + "\n#endif\n"
        filters += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_be_filter.h").write_text(filters, encoding="utf-8")
    integer_gauss = '#include "gauss_integer_shell.h"\n'
    for name, content in (("ReferenceIntegerGauss", raster_source), ("CandidateIntegerGauss", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        integer_gauss += f"namespace {name} {{\n"
        source_text=content.decode("utf-8-sig")
        volume_bits=re.search(r"static const int VOLUME_BITS = [0-9]+;", source_text[source_text.index("class ass_synth_priv"):]).group(0)
        integer_gauss += "class ass_synth_priv { public: " + volume_bits + " };\n"
        if name == "CandidateIntegerGauss":
            integer_gauss += "#define XY_VSFILTER_RUST_TEXT\n"
        integer_gauss += extract_definition(source_text, "static void ass_gauss_blur(") + "\n"
        integer_gauss += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_gauss_integer.h").write_text(integer_gauss, encoding="utf-8")
    blur = '#include "be_blur_shell.h"\n'
    for name, content in (("ReferenceBlur", raster_source), ("CandidateBlur", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        blur += f"namespace {name} {{\n"
        if name == "CandidateBlur":
            blur += "#define XY_VSFILTER_RUST_TEXT\n"
            blur += extract_definition(content.decode("utf-8-sig"), "static void xyRustBeBlur(") + "\n"
        blur += extract_definition(content.decode("utf-8-sig"), "static void be_blur_c(") + "\n"
        blur += "#if BLUR_NATIVE_SSE\n"
        blur += extract_definition(content.decode("utf-8-sig"), "static void be_blur(") + "\n"
        blur += "#endif\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_be_blur.h").write_text(blur, encoding="utf-8")
    bilinear = '#include "bilinear_shell.h"\n'
    for name, content in (("ReferenceBilinear", raster_source), ("CandidateBilinear", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        bilinear += f"namespace {name} {{\n"
        if name == "CandidateBilinear":
            bilinear += "#define XY_VSFILTER_RUST_TEXT\n"
        bilinear += extract_definition(content.decode("utf-8-sig"), "static void Bilinear(")
        bilinear += "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_bilinear.h").write_text(bilinear, encoding="utf-8")
    masks = '#include "mask_shell.h"\n'
    for name, content in (("ReferenceMask", raster_source), ("CandidateMask", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        masks += f"namespace {name} {{\n"
        if name == "CandidateMask":
            masks += "#define XY_VSFILTER_RUST_TEXT\n#ifndef _WIN64\n"
            for signature in ("static XYRustMaskBytes xyRustMaskSnapshot(", "static void xyRustMaskRows("):
                masks += extract_definition(content.decode("utf-8-sig"), signature) + "\n"
            masks += "#endif\n"
        for signature in ("void FillAlphaMashBody_c(BYTE*", "void FillAlphaMashBorder_c(BYTE*",
                          "void FillAlphaMashBodyMasked_c(       BYTE*", "void FillAlphaMashBorderMasked_c(       BYTE*",
                          "void FillAlphaMashBody_sse2(BYTE*", "void FillAlphaMashBorder_sse2( BYTE*",
                          "void FillAlphaMashBodyMasked_sse2(       BYTE*", "void FillAlphaMashBorderMasked_sse2(       BYTE*"):
            masks += extract_definition(content.decode("utf-8-sig"), signature) + "\n"
        masks += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_mask.h").write_text(masks, encoding="utf-8")
    dvb = '#include "original_hdmv.h"\n#include "dvb_shell.h"\n'
    dvb += '#define UnionRect ObservedUnionRect\n'
    for name, content in (("OriginalDvb", hdmv_source), ("CandidateDvb", candidate_hdmv)):
        reader = "ObservedDvbBuffer" if name == "OriginalDvb" else "CandidateDvbBuffer"
        pixels = "false" if name == "OriginalDvb" else "true"
        dvb += f"DVB_SHELL({name},{reader},{pixels})\n#define CGolombBuffer {reader}\n#define CompositionObject {name}\n"
        if name == "CandidateDvb":
            dvb += "#define XY_VSFILTER_RUST_TEXT\n"
        for method in ("RenderDvb", "DvbRenderField"):
            dvb += extract_method(content.decode("utf-8-sig"), f"void CompositionObject::{method}(") + "\n"
        for bits in (2,4,8):
            dvb += extract_method(content.decode("utf-8-sig"), f"void CompositionObject::Dvb{bits}PixelsCodeString(") + "\n"
        dvb += "#undef CompositionObject\n#undef XY_VSFILTER_RUST_TEXT\n#undef CGolombBuffer\n"
    dvb += '#undef CGolombBuffer\n#undef UnionRect\n'
    (destination / "original_dvb.h").write_text(dvb, encoding="utf-8")
    cargo = ["cargo", "rustc", "--locked", "--offline", "--release", "-p", "xy-vsfilter-ffi"]
    if os.name == "nt":
        cargo += ["--features", "native-runtime"]
    if args.target:
        cargo += ["--target", args.target]
    cargo += ["--", "--print", "native-static-libs"]
    build = run(cargo, capture_output=True, text=True)
    print(build.stderr, end="")
    match = re.search(r"native-static-libs: (.+)", build.stderr)
    if not match:
        raise RuntimeError("rustc did not report native link dependencies")
    libraries = shlex.split(match[1])
    library_dir = ROOT / "target"
    if args.target:
        library_dir /= args.target
    library_dir /= "release"
    executable = destination / ("decoder-oracle.exe" if os.name == "nt" else "decoder-oracle")
    if os.name == "nt":
        if args.sanitize:
            parser.error("--sanitize is currently supported with Clang/GCC only")
        compile_command = [args.cxx, "/nologo", "/O2", "/EHsc", "/std:c++14", "/MD",
            f"/I{destination}", f"/I{ROOT / 'include'}", f"/I{ROOT / 'tools/compat'}",
            str(ROOT / "tools/compat/decoder_oracle.cpp"), str(library_dir / "xy_vsfilter_ffi.lib"),
            *libraries, f"/Fe:{executable}", f"/Fo:{destination / 'decoder-oracle.obj'}"]
    else:
        compile_command = [args.cxx, "-std=c++11", "-O2", "-Wall", "-Wextra",
            "-I", str(destination), "-I", str(ROOT / "include"), "-I", str(ROOT / "tools/compat"),
            str(ROOT / "tools/compat/decoder_oracle.cpp"), str(library_dir / "libxy_vsfilter_ffi.a"),
            *libraries, "-o", str(executable)]
        if args.sanitize:
            compile_command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    run(compile_command)
    result = run([str(executable)], capture_output=True, text=True)
    print(result.stdout, end="", flush=True)
    print(result.stderr, end="", file=sys.stderr)
    golomb_executable = destination / ("golomb-oracle.exe" if os.name == "nt" else "golomb-oracle")
    golomb_compile = [arg.replace("decoder_oracle.cpp", "golomb_oracle.cpp")
                      .replace("decoder-oracle", "golomb-oracle") for arg in compile_command]
    run(golomb_compile)
    golomb_result = run([str(golomb_executable)], capture_output=True, text=True)
    print(golomb_result.stdout, end="", flush=True)
    print(golomb_result.stderr, end="", file=sys.stderr)
    reader_executable = destination / ("text-reader-oracle.exe" if os.name == "nt" else "text-reader-oracle")
    reader_compile = [arg.replace("decoder_oracle.cpp", "text_reader_oracle.cpp")
                      .replace("decoder-oracle", "text-reader-oracle") for arg in compile_command]
    run(reader_compile)
    reader_result = run([str(reader_executable)], capture_output=True, text=True)
    print(reader_result.stdout, end="", flush=True)
    print(reader_result.stderr, end="", file=sys.stderr)
    font_executable = destination / ("font-oracle.exe" if os.name == "nt" else "font-oracle")
    font_compile = [arg.replace("decoder_oracle.cpp", "font_oracle.cpp")
                    .replace("decoder-oracle", "font-oracle") for arg in compile_command]
    run(font_compile)
    font_result = run([str(font_executable)], capture_output=True, text=True)
    print(font_result.stdout, end="", flush=True)
    print(font_result.stderr, end="", file=sys.stderr)
    lexer_executable = destination / ("ass-lexer-oracle.exe" if os.name == "nt" else "ass-lexer-oracle")
    lexer_compile = [arg.replace("decoder_oracle.cpp", "ass_lexer_oracle.cpp")
                    .replace("decoder-oracle", "ass-lexer-oracle") for arg in compile_command]
    run(lexer_compile)
    lexer_result = run([str(lexer_executable)], capture_output=True, text=True)
    print(lexer_result.stdout, end="", flush=True)
    print(lexer_result.stderr, end="", file=sys.stderr)
    drawing_executable = destination / ("drawing-oracle.exe" if os.name == "nt" else "drawing-oracle")
    drawing_compile = [arg.replace("decoder_oracle.cpp", "drawing_oracle.cpp")
                       .replace("decoder-oracle", "drawing-oracle") for arg in compile_command]
    run(drawing_compile)
    drawing_result = run([str(drawing_executable)], capture_output=True, text=True)
    print(drawing_result.stdout, end="", flush=True)
    print(drawing_result.stderr, end="", file=sys.stderr)
    token_executable = destination / ("drawing-tokens-oracle.exe" if os.name == "nt" else "drawing-tokens-oracle")
    token_compile = [arg.replace("decoder_oracle.cpp", "drawing_tokens_oracle.cpp")
                     .replace("decoder-oracle", "drawing-tokens-oracle") for arg in compile_command]
    run(token_compile)
    token_result = run([str(token_executable)], capture_output=True, text=True)
    print(token_result.stdout, end="", flush=True)
    print(token_result.stderr, end="", file=sys.stderr)
    fields_executable = destination / ("fields-oracle.exe" if os.name == "nt" else "fields-oracle")
    fields_compile = [arg.replace("decoder_oracle.cpp", "fields_oracle.cpp")
                      .replace("decoder-oracle", "fields-oracle") for arg in compile_command]
    run(fields_compile)
    fields_result = run([str(fields_executable)], capture_output=True, text=True)
    print(fields_result.stdout, end="", flush=True)
    print(fields_result.stderr, end="", file=sys.stderr)
    segments_executable = destination / ("segments-oracle.exe" if os.name == "nt" else "segments-oracle")
    segments_compile = [arg.replace("decoder_oracle.cpp", "segments_oracle.cpp")
                        .replace("decoder-oracle", "segments-oracle") for arg in compile_command]
    run(segments_compile)
    segments_result = run([str(segments_executable)], capture_output=True, text=True)
    print(segments_result.stdout, end="", flush=True)
    print(segments_result.stderr, end="", file=sys.stderr)
    insertion_executable = destination / ("segment-insert-oracle.exe" if os.name == "nt" else "segment-insert-oracle")
    insertion_compile = [arg.replace("decoder_oracle.cpp", "segment_insert_oracle.cpp")
                         .replace("decoder-oracle", "segment-insert-oracle") for arg in compile_command]
    run(insertion_compile)
    insertion_result = run([str(insertion_executable)], capture_output=True, text=True)
    print(insertion_result.stdout, end="", flush=True)
    print(insertion_result.stderr, end="", file=sys.stderr)
    search_executable = destination / ("timeline-search-oracle.exe" if os.name == "nt" else "timeline-search-oracle")
    search_compile = [arg.replace("decoder_oracle.cpp", "timeline_search_oracle.cpp")
                      .replace("decoder-oracle", "timeline-search-oracle") for arg in compile_command]
    run(search_compile)
    search_result = run([str(search_executable)], capture_output=True, text=True)
    print(search_result.stdout, end="", flush=True)
    print(search_result.stderr, end="", file=sys.stderr)
    hdmv_executable = destination / ("hdmv-oracle.exe" if os.name == "nt" else "hdmv-oracle")
    hdmv_compile = [arg.replace("decoder_oracle.cpp", "hdmv_oracle.cpp")
                    .replace("decoder-oracle", "hdmv-oracle") for arg in compile_command]
    run(hdmv_compile)
    hdmv_result = run([str(hdmv_executable)], capture_output=True, text=True)
    print(hdmv_result.stdout, end="", flush=True)
    print(hdmv_result.stderr, end="", file=sys.stderr)
    dvb_executable = destination / ("dvb-oracle.exe" if os.name == "nt" else "dvb-oracle")
    dvb_compile = [arg.replace("decoder_oracle.cpp", "dvb_oracle.cpp")
                   .replace("decoder-oracle", "dvb-oracle") for arg in compile_command]
    if os.name == "nt":
        dvb_compile += ["user32.lib"]  # Compare the actual Windows UnionRect API.
    run(dvb_compile)
    dvb_result = run([str(dvb_executable)], capture_output=True, text=True)
    print(dvb_result.stdout, end="", flush=True)
    print(dvb_result.stderr, end="", file=sys.stderr)
    field_executable = destination / ("dvb-field-oracle.exe" if os.name == "nt" else "dvb-field-oracle")
    field_compile = [arg.replace("dvb_oracle.cpp", "dvb_field_oracle.cpp")
                     .replace("dvb-oracle", "dvb-field-oracle") for arg in dvb_compile]
    run(field_compile)
    field_result = run([str(field_executable)], capture_output=True, text=True)
    print(field_result.stdout, end="", flush=True)
    print(field_result.stderr, end="", file=sys.stderr)
    fill_executable = destination / ("fill-oracle.exe" if os.name == "nt" else "fill-oracle")
    fill_compile = [arg.replace("hdmv_oracle.cpp", "fill_oracle.cpp")
                    .replace("hdmv-oracle", "fill-oracle") for arg in hdmv_compile]
    run(fill_compile)
    fill_result = run([str(fill_executable)], capture_output=True, text=True)
    print(fill_result.stdout, end="", flush=True)
    print(fill_result.stderr, end="", file=sys.stderr)
    pixel_executable = destination / ("pixel-oracle.exe" if os.name == "nt" else "pixel-oracle")
    pixel_compile = [arg.replace("fill_oracle.cpp", "pixel_oracle.cpp").replace("fill-oracle", "pixel-oracle") for arg in fill_compile]
    run(pixel_compile)
    pixel_result = run([str(pixel_executable)], capture_output=True, text=True)
    print(pixel_result.stdout, end="", flush=True)
    print(pixel_result.stderr, end="", file=sys.stderr)
    mask_executable = destination / ("mask-oracle.exe" if os.name == "nt" else "mask-oracle")
    mask_compile = [arg.replace("fill_oracle.cpp", "mask_oracle.cpp").replace("fill-oracle", "mask-oracle") for arg in fill_compile]
    run(mask_compile)
    mask_result = run([str(mask_executable)], capture_output=True, text=True)
    print(mask_result.stdout, end="", flush=True)
    print(mask_result.stderr, end="", file=sys.stderr)
    bilinear_executable = destination / ("bilinear-oracle.exe" if os.name == "nt" else "bilinear-oracle")
    bilinear_compile = [arg.replace("fill_oracle.cpp", "bilinear_oracle.cpp").replace("fill-oracle", "bilinear-oracle") for arg in fill_compile]
    run(bilinear_compile)
    bilinear_result = run([str(bilinear_executable)], capture_output=True, text=True)
    print(bilinear_result.stdout, end="", flush=True)
    print(bilinear_result.stderr, end="", file=sys.stderr)
    blur_executable = destination / ("be-blur-oracle.exe" if os.name == "nt" else "be-blur-oracle")
    blur_compile = [arg.replace("fill_oracle.cpp", "be_blur_oracle.cpp").replace("fill-oracle", "be-blur-oracle") for arg in fill_compile]
    run(blur_compile)
    blur_result = run([str(blur_executable)], capture_output=True, text=True)
    print(blur_result.stdout, end="", flush=True)
    print(blur_result.stderr, end="", file=sys.stderr)
    filter_executable = destination / ("be-filter-oracle.exe" if os.name == "nt" else "be-filter-oracle")
    filter_compile = [arg.replace("fill_oracle.cpp", "be_filter_oracle.cpp").replace("fill-oracle", "be-filter-oracle") for arg in fill_compile]
    run(filter_compile)
    filter_result = run([str(filter_executable)], capture_output=True, text=True)
    print(filter_result.stdout, end="", flush=True)
    print(filter_result.stderr, end="", file=sys.stderr)
    weighted_executable = destination / ("weighted-filter-oracle.exe" if os.name == "nt" else "weighted-filter-oracle")
    weighted_compile = [arg.replace("fill_oracle.cpp", "weighted_filter_oracle.cpp").replace("fill-oracle", "weighted-filter-oracle") for arg in fill_compile]
    run(weighted_compile)
    weighted_result = run([str(weighted_executable)], capture_output=True, text=True)
    print(weighted_result.stdout, end="", flush=True)
    print(weighted_result.stderr, end="", file=sys.stderr)
    transpose_executable = destination / ("transpose-oracle.exe" if os.name == "nt" else "transpose-oracle")
    transpose_compile = [arg.replace("fill_oracle.cpp", "transpose_oracle.cpp").replace("fill-oracle", "transpose-oracle") for arg in fill_compile]
    run(transpose_compile)
    transpose_result = run([str(transpose_executable)], capture_output=True, text=True)
    print(transpose_result.stdout, end="", flush=True)
    print(transpose_result.stderr, end="", file=sys.stderr)
    float_transpose_executable = destination / ("float-transpose-oracle.exe" if os.name == "nt" else "float-transpose-oracle")
    float_transpose_compile = [arg.replace("fill_oracle.cpp", "float_transpose_oracle.cpp").replace("fill-oracle", "float-transpose-oracle") for arg in fill_compile]
    if os.name == "nt" and args.target and args.target.startswith("i686-"):
        # Match common.props' NoExtensions mode for scalar x87 copies.
        float_transpose_compile.insert(1, "/arch:IA32")
    run(float_transpose_compile)
    float_transpose_result = run([str(float_transpose_executable)], capture_output=True, text=True)
    print(float_transpose_result.stdout, end="", flush=True)
    print(float_transpose_result.stderr, end="", file=sys.stderr)
    byte_float_executable = destination / ("byte-float-oracle.exe" if os.name == "nt" else "byte-float-oracle")
    byte_float_compile = [arg.replace("fill_oracle.cpp", "byte_float_oracle.cpp").replace("fill-oracle", "byte-float-oracle") for arg in fill_compile]
    if os.name == "nt" and args.target and args.target.startswith("i686-"):
        byte_float_compile.insert(1, "/arch:IA32")
    run(byte_float_compile)
    byte_float_result = run([str(byte_float_executable)], capture_output=True, text=True)
    print(byte_float_result.stdout, end="", flush=True)
    print(byte_float_result.stderr, end="", file=sys.stderr)
    integer_gauss_executable = destination / ("gauss-integer-oracle.exe" if os.name == "nt" else "gauss-integer-oracle")
    integer_gauss_compile = [arg.replace("fill_oracle.cpp", "gauss_integer_oracle.cpp").replace("fill-oracle", "gauss-integer-oracle") for arg in fill_compile]
    run(integer_gauss_compile)
    integer_gauss_result = run([str(integer_gauss_executable)], capture_output=True, text=True)
    print(integer_gauss_result.stdout, end="", flush=True)
    print(integer_gauss_result.stderr, end="", file=sys.stderr)
    vobsub_executable = destination / ("vobsub-packet-oracle.exe" if os.name == "nt" else "vobsub-packet-oracle")
    vobsub_compile = [arg.replace("fill_oracle.cpp", "vobsub_packet_oracle.cpp").replace("fill-oracle", "vobsub-packet-oracle") for arg in fill_compile]
    run(vobsub_compile)
    vobsub_result = run([str(vobsub_executable)], capture_output=True, text=True)
    print(vobsub_result.stdout, end="", flush=True)
    print(vobsub_result.stderr, end="", file=sys.stderr)
    vobsub_pixels_executable = destination / ("vobsub-pixels-oracle.exe" if os.name == "nt" else "vobsub-pixels-oracle")
    vobsub_pixels_compile = [arg.replace("fill_oracle.cpp", "vobsub_pixels_oracle.cpp").replace("fill-oracle", "vobsub-pixels-oracle") for arg in fill_compile]
    run(vobsub_pixels_compile)
    vobsub_pixels_result = run([str(vobsub_pixels_executable)], capture_output=True, text=True)
    print(vobsub_pixels_result.stdout, end="", flush=True)
    print(vobsub_pixels_result.stderr, end="", file=sys.stderr)
    vobsub_rle_executable = destination / ("vobsub-rle-oracle.exe" if os.name == "nt" else "vobsub-rle-oracle")
    vobsub_rle_compile = [arg.replace("fill_oracle.cpp", "vobsub_rle_oracle.cpp").replace("fill-oracle", "vobsub-rle-oracle") for arg in fill_compile]
    if os.name == "nt":
        vobsub_rle_compile += ["user32.lib"]  # Actual ATL rectangle operations in trimming.
    run(vobsub_rle_compile)
    vobsub_rle_result = run([str(vobsub_rle_executable)], capture_output=True, text=True)
    print(vobsub_rle_result.stdout, end="", flush=True)
    print(vobsub_rle_result.stderr, end="", file=sys.stderr)
    vobsub_trim_executable = destination / ("vobsub-trim-oracle.exe" if os.name == "nt" else "vobsub-trim-oracle")
    vobsub_trim_compile = [arg.replace("fill_oracle.cpp", "vobsub_trim_oracle.cpp").replace("fill-oracle", "vobsub-trim-oracle") for arg in fill_compile]
    if os.name == "nt":
        vobsub_trim_compile += ["user32.lib"]  # Use actual ATL rectangle operations.
    run(vobsub_trim_compile)
    vobsub_trim_result = run([str(vobsub_trim_executable)], capture_output=True, text=True)
    print(vobsub_trim_result.stdout, end="", flush=True)
    print(vobsub_trim_result.stderr, end="", file=sys.stderr)
    vobsub_scale_executable = destination / ("vobsub-scale-oracle.exe" if os.name == "nt" else "vobsub-scale-oracle")
    vobsub_scale_compile = [arg.replace("fill_oracle.cpp", "vobsub_scale_oracle.cpp").replace("fill-oracle", "vobsub-scale-oracle") for arg in fill_compile]
    run(vobsub_scale_compile)
    vobsub_scale_result = run([str(vobsub_scale_executable)], capture_output=True, text=True)
    print(vobsub_scale_result.stdout, end="", flush=True)
    print(vobsub_scale_result.stderr, end="", file=sys.stderr)
    vobsub_outline_executable = destination / ("vobsub-outline-oracle.exe" if os.name == "nt" else "vobsub-outline-oracle")
    vobsub_outline_compile = [arg.replace("fill_oracle.cpp", "vobsub_outline_oracle.cpp").replace("fill-oracle", "vobsub-outline-oracle") for arg in fill_compile]
    run(vobsub_outline_compile)
    vobsub_outline_result = run([str(vobsub_outline_executable)], capture_output=True, text=True)
    print(vobsub_outline_result.stdout, end="", flush=True)
    print(vobsub_outline_result.stderr, end="", file=sys.stderr)
    vobsub_segment_executable = destination / ("vobsub-segment-oracle.exe" if os.name == "nt" else "vobsub-segment-oracle")
    vobsub_segment_compile = [arg.replace("fill_oracle.cpp", "vobsub_segment_oracle.cpp").replace("fill-oracle", "vobsub-segment-oracle") for arg in fill_compile]
    run(vobsub_segment_compile)
    vobsub_segment_result = run([str(vobsub_segment_executable)], capture_output=True, text=True)
    print(vobsub_segment_result.stdout, end="", flush=True)
    print(vobsub_segment_result.stderr, end="", file=sys.stderr)
    vobsub_split_executable = destination / ("vobsub-split-oracle.exe" if os.name == "nt" else "vobsub-split-oracle")
    vobsub_split_compile = [arg.replace("fill_oracle.cpp", "vobsub_split_oracle.cpp").replace("fill-oracle", "vobsub-split-oracle") for arg in fill_compile]
    run(vobsub_split_compile)
    vobsub_split_result = run([str(vobsub_split_executable)], capture_output=True, text=True)
    print(vobsub_split_result.stdout, end="", flush=True)
    print(vobsub_split_result.stderr, end="", file=sys.stderr)
    vobsub_path_executable = destination / ("vobsub-path-oracle.exe" if os.name == "nt" else "vobsub-path-oracle")
    vobsub_path_compile = [arg.replace("fill_oracle.cpp", "vobsub_path_oracle.cpp").replace("fill-oracle", "vobsub-path-oracle") for arg in fill_compile]
    run(vobsub_path_compile)
    vobsub_path_result = run([str(vobsub_path_executable)], capture_output=True, text=True)
    print(vobsub_path_result.stdout, end="", flush=True)
    print(vobsub_path_result.stderr, end="", file=sys.stderr)
    subrip_executable = destination / ("subrip-oracle.exe" if os.name == "nt" else "subrip-oracle")
    subrip_compile = [arg.replace("fill_oracle.cpp", "subrip_oracle.cpp").replace("fill-oracle", "subrip-oracle") for arg in fill_compile]
    run(subrip_compile)
    subrip_result = run([str(subrip_executable)], capture_output=True, text=True)
    print(subrip_result.stdout, end="", flush=True)
    print(subrip_result.stderr, end="", file=sys.stderr)
    webvtt_executable = destination / ("webvtt-oracle.exe" if os.name == "nt" else "webvtt-oracle")
    webvtt_compile = [arg.replace("fill_oracle.cpp", "webvtt_oracle.cpp").replace("fill-oracle", "webvtt-oracle") for arg in fill_compile]
    if os.name == "nt":
        webvtt_compile += ["/D_UNICODE", "/DUNICODE"]
    run(webvtt_compile)
    webvtt_result = run([str(webvtt_executable)], capture_output=True, text=True)
    print(webvtt_result.stdout, end="", flush=True)
    print(webvtt_result.stderr, end="", file=sys.stderr)
    realtext_executable = destination / ("realtext-oracle.exe" if os.name == "nt" else "realtext-oracle")
    realtext_compile = [arg.replace("fill_oracle.cpp", "realtext_oracle.cpp").replace("fill-oracle", "realtext-oracle") for arg in fill_compile]
    run(realtext_compile)
    realtext_result = run([str(realtext_executable)], capture_output=True, text=True)
    print(realtext_result.stdout, end="", flush=True)
    print(realtext_result.stderr, end="", file=sys.stderr)
    realtext_attributes_executable = destination / ("realtext-attributes-oracle.exe" if os.name == "nt" else "realtext-attributes-oracle")
    realtext_attributes_compile = [arg.replace("fill_oracle.cpp", "realtext_attributes_oracle.cpp").replace("fill-oracle", "realtext-attributes-oracle") for arg in fill_compile]
    run(realtext_attributes_compile)
    realtext_attributes_result = run([str(realtext_attributes_executable)], capture_output=True, text=True)
    print(realtext_attributes_result.stdout, end="", flush=True)
    print(realtext_attributes_result.stderr, end="", file=sys.stderr)
    realtext_timecode_executable = destination / ("realtext-timecode-oracle.exe" if os.name == "nt" else "realtext-timecode-oracle")
    realtext_timecode_compile = [arg.replace("fill_oracle.cpp", "realtext_timecode_oracle.cpp").replace("fill-oracle", "realtext-timecode-oracle") for arg in fill_compile]
    run(realtext_timecode_compile)
    realtext_timecode_result = run([str(realtext_timecode_executable)], capture_output=True, text=True)
    print(realtext_timecode_result.stdout, end="", flush=True)
    print(realtext_timecode_result.stderr, end="", file=sys.stderr)
    realtext_tag_executable = destination / ("realtext-tag-oracle.exe" if os.name == "nt" else "realtext-tag-oracle")
    realtext_tag_compile = [arg.replace("fill_oracle.cpp", "realtext_tag_oracle.cpp").replace("fill-oracle", "realtext-tag-oracle") for arg in fill_compile]
    run(realtext_tag_compile)
    realtext_tag_result = run([str(realtext_tag_executable)], capture_output=True, text=True)
    print(realtext_tag_result.stdout, end="", flush=True)
    print(realtext_tag_result.stderr, end="", file=sys.stderr)
    realtext_text_executable = destination / ("realtext-text-oracle.exe" if os.name == "nt" else "realtext-text-oracle")
    realtext_text_compile = [arg.replace("fill_oracle.cpp", "realtext_text_oracle.cpp").replace("fill-oracle", "realtext-text-oracle") for arg in fill_compile]
    run(realtext_text_compile)
    realtext_text_result = run([str(realtext_text_executable)], capture_output=True, text=True)
    print(realtext_text_result.stdout, end="", flush=True)
    print(realtext_text_result.stderr, end="", file=sys.stderr)
    realtext_lower_executable = destination / ("realtext-lower-oracle.exe" if os.name == "nt" else "realtext-lower-oracle")
    realtext_lower_compile = [arg.replace("fill_oracle.cpp", "realtext_lower_oracle.cpp").replace("fill-oracle", "realtext-lower-oracle") for arg in fill_compile]
    run(realtext_lower_compile)
    realtext_lower_result = run([str(realtext_lower_executable)], capture_output=True, text=True)
    print(realtext_lower_result.stdout, end="", flush=True)
    print(realtext_lower_result.stderr, end="", file=sys.stderr)
    realtext_timeline_executable = destination / ("realtext-timeline-oracle.exe" if os.name == "nt" else "realtext-timeline-oracle")
    realtext_timeline_compile = [arg.replace("fill_oracle.cpp", "realtext_timeline_oracle.cpp").replace("fill-oracle", "realtext-timeline-oracle") for arg in fill_compile]
    run(realtext_timeline_compile)
    realtext_timeline_result = run([str(realtext_timeline_executable)], capture_output=True, text=True)
    print(realtext_timeline_result.stdout, end="", flush=True)
    print(realtext_timeline_result.stderr, end="", file=sys.stderr)
    fill_debug_compile = []
    fill_debug_output = "Not run: MSVC Release is covered by fill_result\n"
    fill_debug_stderr = ""
    if os.name != "nt":
        # Keep the original _DEBUG branch explicit, separately from the Release
        # oracle. Clang Release is not an admitted masked-planar reference.
        fill_debug_compile = [arg.replace("fill-oracle", "fill-debug-oracle") for arg in fill_compile] + ["-D_DEBUG"]
        run(fill_debug_compile)
        fill_debug = run([str(destination / "fill-debug-oracle")], capture_output=True, text=True)
        fill_debug_output, fill_debug_stderr = fill_debug.stdout, fill_debug.stderr
        print(fill_debug_output, end="", flush=True)
        print(fill_debug_stderr, end="", file=sys.stderr)
    compiler = subprocess.run([args.cxx] if os.name == "nt" else [args.cxx, "--version"],
                              capture_output=True, text=True)
    if candidate_inputs != capture_inputs():
        raise RuntimeError("Candidate inputs changed during validation; rerun from stable sources")
    if candidate_commit != run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip():
        raise RuntimeError("Candidate commit changed during validation; rerun from a stable checkout")
    report = {"complete": True, "reference": reference, "source_sha256": hashlib.sha256(source).hexdigest(),
        "header_sha256": hashlib.sha256(header).hexdigest(), "target": args.target or "host",
        "sanitizers": args.sanitize, "compile_command": compile_command,
        "cpp_compiler": compiler.stdout + compiler.stderr,
        "candidate_commit": candidate_commit,
        "candidate_inputs": candidate_inputs,
        "candidate_inputs_stable": True,
        "hdmv_source_sha256": hashlib.sha256(hdmv_source).hexdigest(),
        "hdmv_golomb_source_sha256": hashlib.sha256(golomb_source).hexdigest(),
        "hdmv_golomb_header_sha256": hashlib.sha256(golomb_header).hexdigest(),
        "hdmv_raster_source_sha256": hashlib.sha256(raster_source).hexdigest(),
        "xy_filter_source_sha256": hashlib.sha256(filter_source).hexdigest(),
        "vobsub_source_sha256": hashlib.sha256(vobsub_source).hexdigest(),
        "vobsub_packet_compile_command": vobsub_compile,
        "vobsub_packet_result": vobsub_result.stdout, "vobsub_packet_stderr": vobsub_result.stderr,
        "vobsub_pixels_compile_command": vobsub_pixels_compile,
        "vobsub_pixels_result": vobsub_pixels_result.stdout, "vobsub_pixels_stderr": vobsub_pixels_result.stderr,
        "vobsub_rle_compile_command": vobsub_rle_compile,
        "vobsub_rle_result": vobsub_rle_result.stdout, "vobsub_rle_stderr": vobsub_rle_result.stderr,
        "vobsub_trim_compile_command": vobsub_trim_compile,
        "vobsub_trim_result": vobsub_trim_result.stdout, "vobsub_trim_stderr": vobsub_trim_result.stderr,
        "vobsub_scale_compile_command": vobsub_scale_compile,
        "vobsub_scale_result": vobsub_scale_result.stdout, "vobsub_scale_stderr": vobsub_scale_result.stderr,
        "vobsub_outline_compile_command": vobsub_outline_compile,
        "vobsub_outline_result": vobsub_outline_result.stdout, "vobsub_outline_stderr": vobsub_outline_result.stderr,
        "realtext_timeline_compile_command": realtext_timeline_compile,
        "realtext_timeline_result": realtext_timeline_result.stdout, "realtext_timeline_stderr": realtext_timeline_result.stderr,
        "realtext_timeline_scope": "owned start/end history and interval selection; native default-duration arithmetic and map writes retained; representable arithmetic fixtures only",
        "realtext_timeline_instrumentation": "all native ParseRealText line map keys wrapped before insertion to compare partial maps, ordering, injected exceptions and live duration/map changes",
        "realtext_lower_compile_command": realtext_lower_compile,
        "realtext_lower_result": realtext_lower_result.stdout, "realtext_lower_stderr": realtext_lower_result.stderr,
        "realtext_lower_scope": "owned lowercase output; native towlower per original code unit and live source length retained; comparisons within each available locale; allocation failure timing unverified",
        "realtext_text_compile_command": realtext_text_compile,
        "realtext_text_result": realtext_text_result.stdout, "realtext_text_stderr": realtext_text_result.stderr,
        "realtext_text_scope": "owned text scanning; native whitespace, output append and source consumption retained; identical source/output uses native fallback; allocation timing and unterminated self-append excluded",
        "realtext_tag_compile_command": realtext_tag_compile,
        "realtext_tag_result": realtext_tag_result.stdout, "realtext_tag_stderr": realtext_tag_result.stderr,
        "realtext_tag_scope": "owned tag extraction state; native whitespace/lowercase, attribute map updates, checked string exceptions and source consumption retained; allocation failure timing unverified",
        "realtext_timecode_compile_command": realtext_timecode_compile,
        "realtext_timecode_result": realtext_timecode_result.stdout, "realtext_timecode_stderr": realtext_timecode_result.stderr,
        "realtext_timecode_scope": "owned reverse fields; native separator detection, padding, numeric conversion and arithmetic retained; nonrepresentable native arithmetic excluded from parity",
        "realtext_attributes_compile_command": realtext_attributes_compile,
        "realtext_attributes_result": realtext_attributes_result.stdout, "realtext_attributes_stderr": realtext_attributes_result.stderr,
        "realtext_attributes_instrumentation": "iswspace wrapper and StringToLower entry hook observe native call order, cursor/map state, injected exceptions and live-input changes",
        "realtext_compile_command": realtext_compile,
        "realtext_result": realtext_result.stdout, "realtext_stderr": realtext_result.stderr,
        "realtext_source_sha256": hashlib.sha256(realtext_source).hexdigest(),
        "realtext_header_sha256": hashlib.sha256(realtext_header).hexdigest(),
        "realtext_numeric_backend": "native Windows iswdigit/_wtoi" if os.name == "nt" else "host iswdigit/wcstol model; conversion remains native",
        "webvtt_compile_command": webvtt_compile,
        "webvtt_result": webvtt_result.stdout, "webvtt_stderr": webvtt_result.stderr,
        "webvtt_string_backend": "installed ATL CStringW" if os.name == "nt" else "wstring model with host iswspace; Windows comparison required",
        "subrip_compile_command": subrip_compile,
        "subrip_result": subrip_result.stdout, "subrip_stderr": subrip_result.stderr,
        "subrip_string_backend": "installed ATL CStringW" if os.name == "nt" else "wstring replacement model; Windows comparison required",
        "vobsub_path_compile_command": vobsub_path_compile,
        "vobsub_path_result": vobsub_path_result.stdout, "vobsub_path_stderr": vobsub_path_result.stderr,
        "vobsub_path_string_backend": "installed ATL CStringW" if os.name == "nt" else "swprintf/wstring with LONG-range coordinates",
        "vobsub_split_compile_command": vobsub_split_compile,
        "vobsub_split_result": vobsub_split_result.stdout, "vobsub_split_stderr": vobsub_split_result.stderr,
        "vobsub_split_cast_normalization": "none; original MSVC spelling" if os.name == "nt" else "three unsigned int functional casts per method changed to static_cast<unsigned int>",
        "vobsub_segment_compile_command": vobsub_segment_compile,
        "vobsub_segment_result": vobsub_segment_result.stdout, "vobsub_segment_stderr": vobsub_segment_result.stderr,
        "weighted_filter_compile_command": weighted_compile,
        "weighted_filter_result": weighted_result.stdout, "weighted_filter_stderr": weighted_result.stderr,
        "gauss_integer_compile_command": integer_gauss_compile,
        "gauss_integer_result": integer_gauss_result.stdout, "gauss_integer_stderr": integer_gauss_result.stderr,
        "byte_float_compile_command": byte_float_compile,
        "byte_float_result": byte_float_result.stdout, "byte_float_stderr": byte_float_result.stderr,
        "float_transpose_compile_command": float_transpose_compile,
        "float_transpose_result": float_transpose_result.stdout, "float_transpose_stderr": float_transpose_result.stderr,
        "transpose_compile_command": transpose_compile,
        "transpose_result": transpose_result.stdout, "transpose_stderr": transpose_result.stderr,
        "be_filter_compile_command": filter_compile,
        "be_filter_result": filter_result.stdout, "be_filter_stderr": filter_result.stderr,
        "be_blur_compile_command": blur_compile,
        "be_blur_result": blur_result.stdout, "be_blur_stderr": blur_result.stderr,
        "bilinear_compile_command": bilinear_compile,
        "bilinear_result": bilinear_result.stdout, "bilinear_stderr": bilinear_result.stderr,
        "mask_compile_command": mask_compile,
        "mask_result": mask_result.stdout, "mask_stderr": mask_result.stderr,
        "pixel_compile_command": pixel_compile,
        "pixel_result": pixel_result.stdout, "pixel_stderr": pixel_result.stderr,
        "fill_debug_compile_command": fill_debug_compile,
        "fill_debug_result": fill_debug_output, "fill_debug_stderr": fill_debug_stderr,
        "fill_compile_command": fill_compile,
        "fill_result": fill_result.stdout, "fill_stderr": fill_result.stderr,
        "golomb_compile_command": golomb_compile,
        "golomb_result": golomb_result.stdout, "golomb_stderr": golomb_result.stderr,
        "dvb_field_compile_command": field_compile,
        "dvb_field_result": field_result.stdout, "dvb_field_stderr": field_result.stderr,
        "dvb_compile_command": dvb_compile,
        "dvb_result": dvb_result.stdout, "dvb_stderr": dvb_result.stderr,
        "hdmv_compile_command": hdmv_compile,
        "hdmv_result": hdmv_result.stdout, "hdmv_stderr": hdmv_result.stderr,
        "rustc": run(["rustc", "-Vv"], capture_output=True, text=True).stdout,
        "result": result.stdout, "stderr": result.stderr,
        "candidate_source_sha256": hashlib.sha256(candidate_source).hexdigest(),
        "text_reader_compile_command": reader_compile,
        "text_reader_result": reader_result.stdout, "text_reader_stderr": reader_result.stderr,
        "font_source_sha256": hashlib.sha256(font_source).hexdigest(),
        "candidate_font_source_sha256": hashlib.sha256(candidate_font).hexdigest(),
        "font_compile_command": font_compile,
        "font_result": font_result.stdout, "font_stderr": font_result.stderr,
        "lexer_source_sha256": hashlib.sha256(lexer_source).hexdigest(),
        "candidate_lexer_source_sha256": hashlib.sha256(candidate_lexer).hexdigest(),
        "lexer_compile_command": lexer_compile,
        "lexer_result": lexer_result.stdout, "lexer_stderr": lexer_result.stderr,
        "drawing_compile_command": drawing_compile,
        "drawing_result": drawing_result.stdout, "drawing_stderr": drawing_result.stderr,
        "drawing_tokens_compile_command": token_compile,
        "drawing_tokens_result": token_result.stdout, "drawing_tokens_stderr": token_result.stderr,
        "fields_compile_command": fields_compile,
        "fields_result": fields_result.stdout, "fields_stderr": fields_result.stderr,
        "segments_header_sha256": hashlib.sha256(segment_header).hexdigest(),
        "segments_compile_command": segments_compile,
        "segments_result": segments_result.stdout, "segments_stderr": segments_result.stderr,
        "segment_insert_compile_command": insertion_compile,
        "segment_insert_result": insertion_result.stdout, "segment_insert_stderr": insertion_result.stderr,
        "timeline_search_compile_command": search_compile,
        "timeline_search_result": search_result.stdout, "timeline_search_stderr": search_result.stderr}
    (destination / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
