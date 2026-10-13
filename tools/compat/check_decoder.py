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
              ROOT / "include/xy_rust_fields.h",
              ROOT / "include/xy_rust_segments.h",
              ROOT / "include/xy_rust_segment_insert.h",
              ROOT / "include/xy_rust_hdmv.h",
              ROOT / "include/xy_rust_dvb.h", ROOT / "include/xy_rust_golomb.h", ROOT / "include/xy_rust_fill.h",
              ROOT / "src/subtitles/Rasterizer.cpp", ROOT / "include/xy_rust_mask.h",
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
