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
              ROOT / "include/xy_rust_ass.h", ROOT / "include/xy_rust_reader.h", ROOT / "include/xy_rust_writer.h", ROOT / "include/xy_rust_seek.h",
              ROOT / "include/xy_rust_drawing.h", ROOT / "include/xy_rust_layout.h", ROOT / "include/xy_rust_text_runs.h", ROOT / "include/xy_rust_wrap.h", ROOT / "include/xy_rust_line_layout.h", ROOT / "src/subtitles/RTS.h",
              ROOT / "include/xy_rust_drawing_tokens.h", ROOT / "include/xy_rust_path_align.h", ROOT / "include/xy_rust_path_buffer.h", ROOT / "include/xy_rust_spans.h", ROOT / "include/xy_rust_arc_queue.h", ROOT / "include/xy_rust_ellipse.h",
              ROOT / "src/subtitles/xy_widen_region.cpp", ROOT / "src/subtitles/xy_circular_array_queue.h",
              ROOT / "include/xy_rust_pipe_line.h", ROOT / "include/xy_rust_subrip_cue.h", ROOT / "include/xy_rust_sami_file.h", ROOT / "include/xy_rust_microdvd_style.h", ROOT / "include/xy_rust_font_records.h", ROOT / "include/xy_rust_ass_headers.h", ROOT / "include/xy_rust_ass_style.h", ROOT / "include/xy_rust_ass_dialogue.h", ROOT / "include/xy_rust_encoding.h", ROOT / "include/xy_rust_sami.h", ROOT / "include/xy_rust_case_replace.h", ROOT / "include/xy_rust_remove_tags.h", ROOT / "include/xy_rust_mpl2.h", ROOT / "include/xy_rust_microdvd.h", ROOT / "include/xy_rust_find_char.h", ROOT / "include/xy_rust_fields.h", ROOT / "include/xy_rust_subrip.h", ROOT / "include/xy_rust_webvtt.h", ROOT / "include/xy_rust_realtext.h",
              ROOT / "src/subtitles/RealTextParser.cpp", ROOT / "src/subtitles/RealTextParser.h",
              ROOT / "include/xy_rust_cc.h", ROOT / "src/subtitles/CCDecoder.cpp", ROOT / "src/subtitles/CCDecoder.h",
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
    seek = '#include "text_seek_shell.h"\n#define __super NativeSeekFile\n'
    for name, content in (("ReferenceSeek", source), ("CandidateSeek", candidate_source)):
        seek += f"namespace {name} {{\n"
        seek += "class CTextFile : public NativeSeekFile { public: ULONGLONG GetPosition()const; ULONGLONG GetLength()const; ULONGLONG Seek(LONGLONG,UINT); };\n"
        if name == "CandidateSeek":
            seek += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("ULONGLONG CTextFile::GetPosition(", "ULONGLONG CTextFile::GetLength(", "ULONGLONG CTextFile::Seek("):
            definition = extract_definition(content.decode("utf-8-sig"), signature)
            if name == "CandidateSeek" and "::Seek(" in signature:
                assert definition.count("XyRust::SeekTarget") == 1
                definition = definition.replace("XyRust::SeekTarget", "XyRust::ObservedSeekTarget")
            seek += definition + "\n"
        seek += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    seek += "#undef __super\n"
    (destination / "original_text_seek.h").write_text(seek, encoding="utf-8")
    writer = '#include "text_writer_shell.h"\n'
    for name, content in (("ReferenceTextWriter", source), ("CandidateTextWriter", candidate_source)):
        writer += f"namespace {name} {{\n"
        writer += "class CTextFile : public WriterBase { public: enum {DEFAULT_ENCODING,UTF8,LE16,BE16}; int m_encoding; explicit CTextFile(int e):m_encoding(e){} void WriteString(LPCWSTR); };\n"
        if name == "CandidateTextWriter":
            writer += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(content.decode("utf-8-sig"), "void CTextFile::WriteString(")
        if name == "CandidateTextWriter":
            assert definition.count("XyRust::TextWriter buffer") == 3
            definition = definition.replace("XyRust::TextWriter buffer", "XyRust::ObservedTextWriter buffer")
        writer += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_text_writer.h").write_text(writer, encoding="utf-8")
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
    cc_source = original("src/subtitles/CCDecoder.cpp", reference["commit"])
    cc_header = original("src/subtitles/CCDecoder.h", reference["commit"])
    cc_class = extract_method(cc_header.decode("utf-8-sig"), "class CCDecoder").replace("{", "{\npublic:", 1)
    cc = '#include "cc_shell.h"\n'
    for name, content in (("ReferenceCC", cc_source), ("CandidateCC", (ROOT / "src/subtitles/CCDecoder.cpp").read_bytes())):
        cc += f"namespace {name} {{\n" + cc_class + ";\n"
        if name == "CandidateCC":
            cc += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("CCDecoder::CCDecoder(", "CCDecoder::~CCDecoder(", "void CCDecoder::MoveCursor(",
                          "void CCDecoder::OffsetCursor(", "void CCDecoder::PutChar(", "void CCDecoder::SaveDisp(",
                          "void CCDecoder::DecodeCC("):
            cc += extract_definition(content.decode("utf-8-sig"), signature) + "\n"
        cc += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_cc.h").write_text(cc, encoding="utf-8")
    layout_source = original("src/subtitles/RTS.cpp", reference["commit"])
    layout_header = original("src/subtitles/RTS.h", reference["commit"])
    layout = '#include "layout_rects_shell.h"\n'
    for name, content, header_content in (("ReferenceLayout", layout_source, layout_header), ("CandidateLayout", (ROOT / "src/subtitles/RTS.cpp").read_bytes(), (ROOT / "src/subtitles/RTS.h").read_bytes())):
        layout += f"namespace {name} {{\n"
        if name == "CandidateLayout":
            layout += "#define XY_VSFILTER_RUST_TEXT\n"
        declaration = extract_method(header_content.decode("utf-8-sig"), "class CScreenLayoutAllocator")
        brace = declaration.index("{") + 1
        declaration = declaration[:brace] + "\npublic:\n" + declaration[brace:]
        if name == "CandidateLayout":
            assert declaration.count("XyRust::LayoutRects m_subrects") == 1
            declaration = declaration.replace("XyRust::LayoutRects m_subrects", "XyRust::ObservedLayoutRects m_subrects")
        layout += declaration + ";\n"
        for signature in ("void CScreenLayoutAllocator::Empty(", "void CScreenLayoutAllocator::AdvanceToSegment(", "CRect CScreenLayoutAllocator::AllocRect("):
            decoded = content.decode("utf-8-sig")
            if signature == "CRect CScreenLayoutAllocator::AllocRect(":
                start = decoded.index(signature)
                end = decoded.index("// CRenderedTextSubtitle", start)
                definition = decoded[start:end].rstrip()
                assert definition.endswith("}")
            else:
                definition = extract_definition(decoded, signature)
            layout += definition + "\n"
        layout += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_layout_rects.h").write_text(layout, encoding="utf-8")
    text_runs = '#include "text_runs_shell.h"\n'
    for name, content in (("ReferenceTextRuns", layout_source), ("CandidateTextRuns", (ROOT / "src/subtitles/RTS.cpp").read_bytes())):
        text_runs += f"namespace {name} {{\n"
        text_runs += "class CRenderedTextSubtitle : public RenderState { public: void ParseString(CSubtitle*, CStringW, const FwSTSStyle&); };\n"
        if name == "CandidateTextRuns":
            text_runs += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(content.decode("utf-8-sig"), "void CRenderedTextSubtitle::ParseString(")
        if name == "CandidateTextRuns":
            assert definition.count("XyRust::TextRuns runs") == 1
            definition = definition.replace("XyRust::TextRuns runs", "XyRust::ObservedTextRuns runs")
        text_runs += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_text_runs.h").write_text(text_runs, encoding="utf-8")
    wrap_width = '#include "wrap_width_shell.h"\n'
    for name, content in (("ReferenceWrap", layout_source), ("CandidateWrap", (ROOT / "src/subtitles/RTS.cpp").read_bytes())):
        wrap_width += f"namespace {name} {{\n"
        wrap_width += "class CSubtitle { public: WrapWords m_words; int m_wrapStyle; int GetFullLineWidth(POSITION); int GetWrapWidth(POSITION,int); };\n"
        if name == "CandidateWrap":
            wrap_width += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("int CSubtitle::GetFullLineWidth(", "int CSubtitle::GetWrapWidth("):
            definition = extract_definition(content.decode("utf-8-sig"), signature)
            if name == "CandidateWrap" and "GetWrapWidth" in signature:
                assert definition.count("XyRust::WrapWidth wrap") == 1
                definition = definition.replace("XyRust::WrapWidth wrap", "XyRust::ObservedWrapWidth wrap")
            wrap_width += definition + "\n"
        wrap_width += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_wrap_width.h").write_text(wrap_width, encoding="utf-8")
    line_layout = '#include "line_layout_shell.h"\n'
    for name, content in (("ReferenceLineLayout", layout_source), ("CandidateLineLayout", (ROOT / "src/subtitles/RTS.cpp").read_bytes())):
        line_layout += f"namespace {name} {{\n"
        line_layout += "class CSubtitle : public LineState { public: void MakeLines(CSize,CRect); };\n"
        if name == "CandidateLineLayout":
            line_layout += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(content.decode("utf-8-sig"), "void CSubtitle::MakeLines(")
        if name == "CandidateLineLayout":
            for function in ("GrowLineSpace", "PlaceLines"):
                assert definition.count("XyRust::" + function) == 1
                definition = definition.replace("XyRust::" + function, "XyRust::Observed" + function)
        line_layout += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_line_layout.h").write_text(line_layout, encoding="utf-8")
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
                  "std::wstring CRealTextParser::RenderTags(", "void CRealTextParser::PopTag(",
                  "std::wstring CRealTextParser::FormatTimecode(", "bool CRealTextParser::OutputSRT("]
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
                expected = 4
                if definition.count(plain) != expected or definition.count(first) != 1:
                    raise RuntimeError("RealText line insertion checkpoints changed")
                definition = definition.replace(plain, "m_RealText.m_mapLines[realTextLineKey(m_RealText.m_mapLines, pairTimecodes, szLine)] = szLine;")
                definition = definition.replace(first, "m_RealText.m_mapLines[realTextLineKey(m_RealText.m_mapLines, std::make_pair(0, iStartTimecode), szLine)] = szLine;")
                if name == "CandidateRealText":
                    bridge = "m_RealText.m_mapLines[pairTimecodes] = line;"
                    if definition.count(bridge) != 1:
                        raise RuntimeError("RealText document bridge insertion changed")
                    definition = definition.replace(bridge, "m_RealText.m_mapLines[realTextLineKey(m_RealText.m_mapLines, pairTimecodes, line)] = line;")
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
    micro_style = '#include "microdvd_style_shell.h"\n'
    for signature in ("STSStyle::STSStyle(", "void STSStyle::SetDefault("):
        micro_style += extract_definition(font_source.decode("utf-8-sig"), signature) + "\n"
    for name, content in (("ReferenceMicroDvdStyle", font_source), ("CandidateMicroDvdStyle", candidate_font)):
        decoded = content.decode("utf-8-sig")
        micro_style += f"namespace {name} {{\n"
        if name == "CandidateMicroDvdStyle":
            micro_style += "#define XY_VSFILTER_RUST_TEXT\n"
        start = decoded.index("static STSStyle* GetMicroDVDStyle(")
        end = decoded.index("static CStringW MicroDVD2SSA(", start)
        definition = decoded[start:end].rstrip()
        assert definition.endswith("}")
        if name == "CandidateMicroDvdStyle":
            assert definition.count("XyRust::MicroDvdStyle tags") == 1
            definition = definition.replace("XyRust::MicroDvdStyle tags", "XyRust::ObservedMicroDvdStyle tags")
        micro_style += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_microdvd_style.h").write_text(micro_style, encoding="utf-8")
    document = '#include "ass_document_shell.h"\n'
    for signature in ("STSStyle::STSStyle(", "void STSStyle::SetDefault("):
        document += extract_definition(font_source.decode("utf-8-sig"), signature) + "\n"
    for name, content in (("ReferenceAssDocument", font_source), ("CandidateAssDocument", candidate_font)):
        decoded = content.decode("utf-8-sig")
        document += f"namespace {name} {{\n"
        if name == "CandidateAssDocument":
            document += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static inline CStringW GetStr(", "static inline int GetInt(", "static inline double GetFloat(", "static inline CStringW::PCXSTR TryNextStr(", "static inline int NextInt(", "static bool LoadUUEFont("):
            definition = extract_definition(decoded, signature)
            if name == "CandidateAssDocument" and signature == "static bool LoadUUEFont(":
                assert definition.count("XyRust::FontRecords records") == 1
                definition = definition.replace("XyRust::FontRecords records", "XyRust::ObservedFontRecords records")
            document += definition + "\n"
        start = decoded.index("static bool OpenSubStationAlpha(")
        end = decoded.index("static bool OpenXombieSub(", start)
        definition = decoded[start:end].rstrip()
        assert definition.endswith("}")
        definition = re.sub(r"(\d+)i64\b", r"\1LL", definition)
        if name == "CandidateAssDocument":
            for before, after in (("XyRust::AssHeaders headers", "XyRust::ObservedAssHeaders headers"), ("XyRust::AssStyle fields", "XyRust::ObservedAssStyle fields"), ("XyRust::AssDialogue dialogue", "XyRust::ObservedAssDialogue dialogue")):
                assert definition.count(before) == 1
                definition = definition.replace(before, after)
        document += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_ass_document.h").write_text(document, encoding="utf-8")
    style = '#include "ass_style_shell.h"\n'
    for signature in ("STSStyle::STSStyle(", "void STSStyle::SetDefault("):
        style += extract_definition(font_source.decode("utf-8-sig"), signature) + "\n"
    for name, content in (("ReferenceAssStyle", font_source), ("CandidateAssStyle", candidate_font)):
        decoded = content.decode("utf-8-sig")
        style += f"namespace {name} {{\n"
        if name == "CandidateAssStyle":
            style += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static inline CStringW GetStr(", "static inline int GetInt(", "static inline double GetFloat("):
            style += extract_definition(decoded, signature) + "\n"
        parser = decoded.index("static bool OpenSubStationAlpha(")
        start = decoded.index('        else if(entry == L"style")', parser)
        end = decoded.index('        else if(entry == L"[script info]")', start)
        branch = decoded[start:end]
        assert branch.count('else if(entry == L"style")') == 1
        branch = branch.replace('else if(entry == L"style")', 'if(true)')
        if name == "CandidateAssStyle":
            assert branch.count("XyRust::AssStyle fields") == 1
            branch = branch.replace("XyRust::AssStyle fields", "XyRust::ObservedAssStyle fields")
        style += "static bool ParseStyle(CSimpleTextSubtitle& ret, CStringW& buff, int sver) {\n"
        style += branch + "return true;\n}\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_ass_style.h").write_text(style, encoding="utf-8")
    dialogue = '#include "ass_dialogue_shell.h"\n'
    for name, content in (("ReferenceAssDialogue", font_source), ("CandidateAssDialogue", candidate_font)):
        decoded = content.decode("utf-8-sig")
        dialogue += f"namespace {name} {{\n"
        if name == "CandidateAssDialogue":
            dialogue += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static inline CStringW::PCXSTR TryNextStr(", "static inline int NextInt("):
            dialogue += extract_definition(decoded, signature) + "\n"
        parser = decoded.index("static bool OpenSubStationAlpha(")
        start = decoded.index('        if(entry == L"dialogue")', parser)
        end = decoded.index('        else if(entry == L"style")', start)
        branch = decoded[start:end]
        assert branch.count('if(entry == L"dialogue")') == 1
        branch = branch.replace('if(entry == L"dialogue")', 'if(true)')
        # The suffix changes spelling only, for compilers without MSVC i64.
        branch = re.sub(r"(\d+)i64\b", r"\1LL", branch)
        if name == "CandidateAssDialogue":
            assert branch.count("XyRust::AssDialogue dialogue") == 1
            branch = branch.replace("XyRust::AssDialogue dialogue", "XyRust::ObservedAssDialogue dialogue")
        dialogue += "static bool ParseDialogue(CTextFile* file, CSimpleTextSubtitle& ret, CStringW buff, int version) {\n"
        dialogue += branch + "return true;\n}\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_ass_dialogue.h").write_text(dialogue, encoding="utf-8")
    encoding = '#include "encoding_shell.h"\n'
    for name, content in (("ReferenceEncoding", font_source), ("CandidateEncoding", candidate_font)):
        decoded = content.decode("utf-8-sig")
        encoding += f"namespace {name} {{\n"
        if name == "CandidateEncoding":
            encoding += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static DWORD CharSetToCodePage(", "static int FindChar("):
            encoding += extract_definition(decoded, signature) + "\n"
        start = decoded.index("static CStringW ToMBCS(")
        end = decoded.index("static CStringW RemoveSSATags(", start)
        definition = decoded[start:end].rstrip()
        assert definition.endswith("}")
        if name == "CandidateEncoding":
            assert definition.count("XyRust::Encoding convert") == 2
            definition = definition.replace("XyRust::Encoding convert", "XyRust::ObservedEncoding convert")
            assert definition.count("XyRust::SsaEncoding convert") == 2
            definition = definition.replace("XyRust::SsaEncoding convert", "XyRust::ObservedSsaEncoding convert")
        encoding += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_encoding.h").write_text(encoding, encoding="utf-8")
    sami = '#include "sami_shell.h"\n'
    for name, content in (("ReferenceSami", font_source), ("CandidateSami", candidate_font)):
        decoded = content.decode("utf-8-sig").replace("\r\n", "\n")
        sami += f"namespace {name} {{\n"
        start = decoded.index("static struct htmlcolor")
        end = decoded.index("\n};", start) + 3
        table = decoded[start:end]
        assert table.count("TCHAR* name;") == 1
        # Table contents are verbatim; names are immutable in this harness.
        sami += table.replace("TCHAR* name;", "const TCHAR* name;") + "\n"
        sami += "static SamiColors g_colors(htmlcolors, sizeof(htmlcolors)/sizeof(*htmlcolors));\n"
        if name == "CandidateSami":
            sami += "#define XY_VSFILTER_RUST_TEXT\n"
        sami += extract_definition(decoded, "static void ReplaceNoCase(") + "\n"
        start = decoded.index("static CStringW SMI2SSA(")
        end = decoded.index("static bool OpenSami(", start)
        definition = decoded[start:end].rstrip()
        assert definition.endswith("}")
        if name == "CandidateSami":
            bridge = "XyRust::Sami convert"
            assert definition.count(bridge) == 1
            definition = definition.replace(bridge, "XyRust::ObservedSami convert")
            assert definition.count("XyRust::SamiArguments arguments") == 1
            definition = definition.replace("XyRust::SamiArguments arguments", "XyRust::ObservedSamiArguments arguments")
        sami += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_sami.h").write_text(sami, encoding="utf-8")
    sami_file = '#include "sami_file_shell.h"\n#include "original_sami.h"\n'
    for name, content in (("ReferenceSami", font_source), ("CandidateSami", candidate_font)):
        sami_file += f"namespace {name} {{\n"
        if name == "CandidateSami":
            sami_file += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(content.decode("utf-8-sig"), "static bool OpenSami(")
        if name == "CandidateSami":
            assert definition.count("XyRust::SamiFile state") == 1
            definition = definition.replace("XyRust::SamiFile state", "XyRust::ObservedSamiFile state")
        sami_file += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_sami_file.h").write_text(sami_file, encoding="utf-8")
    microdvd = '#include "microdvd_shell.h"\n'
    for name, content in (("ReferenceMicroDVD", font_source), ("CandidateMicroDVD", candidate_font)):
        microdvd += f"namespace {name} {{\n"
        if name == "CandidateMicroDVD":
            microdvd += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static DWORD CharSetToCodePage(", "static int FindChar(", "static CStringW MicroDVD2SSA(", "static CStringW MPL22SSA(", "static CStringW RemoveSSATags(", "static void ReplaceNoCase("):
            decoded = content.decode("utf-8-sig")
            if signature == "static CStringW MicroDVD2SSA(":
                # Its unmatched braces inside tag literals defeat brace counting.
                start = decoded.index(signature)
                end = decoded.index("static bool OpenMicroDVD(", start)
                definition = decoded[start:end].rstrip()
                assert definition.endswith("}")
            else:
                definition = extract_definition(decoded, signature)
            if name == "CandidateMicroDVD" and signature == "static CStringW MicroDVD2SSA(":
                bridge = "XyRust::MicroDVD convert"
                assert definition.count(bridge) == 1
                definition = definition.replace(bridge, "XyRust::ObservedMicroDVD convert")
            if name == "CandidateMicroDVD" and signature == "static CStringW MPL22SSA(":
                bridge = "XyRust::MPL2 convert"
                assert definition.count(bridge) == 1
                definition = definition.replace(bridge, "XyRust::ObservedMPL2 convert")
            if name == "CandidateMicroDVD" and signature == "static CStringW RemoveSSATags(":
                bridge = "XyRust::RemoveTags convert"
                assert definition.count(bridge) == 1
                definition = definition.replace(bridge, "XyRust::ObservedRemoveTags convert")
            if name == "CandidateMicroDVD" and signature == "static void ReplaceNoCase(":
                bridge = "XyRust::CaseReplace convert"
                assert definition.count(bridge) == 1
                definition = definition.replace(bridge, "XyRust::ObservedCaseReplace convert")
            microdvd += definition + "\n"
        microdvd += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_microdvd.h").write_text(microdvd, encoding="utf-8")
    find_char = '#include "find_char_shell.h"\n'
    for name, content in (("ReferenceFindChar", font_source), ("CandidateFindChar", candidate_font)):
        find_char += f"namespace {name} {{\n"
        if name == "CandidateFindChar":
            find_char += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static DWORD CharSetToCodePage(", "static int FindChar("):
            definition = extract_definition(content.decode("utf-8-sig"), signature)
            if name == "CandidateFindChar" and signature == "static int FindChar(":
                bridge = "XyRust::FindChar search"
                assert definition.count(bridge) == 1
                definition = definition.replace(bridge, "XyRust::ObservedSearch search")
                assert definition.count("XyRust::FindUnicode(") == 1
                definition = definition.replace("XyRust::FindUnicode(", "XyRust::ObservedUnicode(")
            find_char += definition + "\n"
        find_char += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_find_char.h").write_text(find_char, encoding="utf-8")
    pipe_line = '#include "pipe_line_shell.h"\n'
    for name, content in (("ReferencePipeLine", font_source), ("CandidatePipeLine", candidate_font)):
        pipe_line += f"namespace {name} {{\n"
        if name == "CandidatePipeLine":
            pipe_line += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static DWORD CharSetToCodePage(", "static int FindChar(", "static bool OpenOldSubRipper(", "static bool OpenVPlayer("):
            decoded = content.decode("utf-8-sig")
            if signature == "static bool OpenOldSubRipper(":
                start = decoded.index(signature)
                end = decoded.index("static bool OpenSubViewer(", start)
                definition = decoded[start:end].rstrip()
                assert definition.endswith("}")
            else:
                definition = extract_definition(decoded, signature)
            definition = re.sub(r"(\d+)i64\b", r"\1LL", definition)
            if name == "CandidatePipeLine":
                if signature == "static int FindChar(":
                    definition = definition.replace("XyRust::FindChar search", "XyRust::ObservedSearch search")
                    definition = definition.replace("XyRust::FindUnicode(", "XyRust::ObservedUnicode(")
                if signature.startswith("static bool Open"):
                    assert definition.count("XyRust::PipeLine line") == 1
                    definition = definition.replace("XyRust::PipeLine line", "XyRust::ObservedPipeLine line")
            pipe_line += definition + "\n"
        pipe_line += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_pipe_line.h").write_text(pipe_line, encoding="utf-8")
    subrip_cue = '#include "subrip_cue_shell.h"\n'
    for name, content in (("ReferenceSubRipCue", font_source), ("CandidateSubRipCue", candidate_font)):
        subrip_cue += f"namespace {name} {{\n"
        if name == "CandidateSubRipCue":
            subrip_cue += "#define XY_VSFILTER_RUST_TEXT\n"
        for signature in ("static CStringW SubRipper2SSA(", "static bool OpenSubRipper("):
            definition = extract_definition(content.decode("utf-8-sig"), signature)
            definition = re.sub(r"(\d+)i64\b", r"\1LL", definition)
            if name == "CandidateSubRipCue" and signature == "static bool OpenSubRipper(":
                assert definition.count("XyRust::SubRipCue cue") == 1
                definition = definition.replace("XyRust::SubRipCue cue", "XyRust::ObservedSubRipCue cue")
            subrip_cue += definition + "\n"
        subrip_cue += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_subrip_cue.h").write_text(subrip_cue, encoding="utf-8")
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
    stored_drawing = '#include "drawing_storage_shell.h"\n'
    for name, content in (("OriginalStoredDrawing", lexer_source), ("CandidateStoredDrawing", candidate_lexer)):
        text = content.decode("utf-8-sig")
        stored_drawing += f"STORED_DRAWING_SHELL({name})\n#define CPolygon {name}\n"
        if name == "CandidateStoredDrawing":
            stored_drawing += "#define XY_VSFILTER_RUST_TEXT\n"
        a = text.index("bool CPolygon::ParseStr()")
        definition = text[a:text.index("bool CPolygon::CreatePath(", a)].rstrip()
        assert definition.endswith("}")
        if name == "CandidateStoredDrawing":
            a = definition.index("    CStringW str = m_str.Get();")
            b = definition.index("        // Drawing command state.", a)
            definition = definition[:a] + original_token_input + definition[b:]
        stored_drawing += definition + "\n"
        stored_drawing += extract_definition(text, "CPolygon::CPolygon(CPolygon& src)") + "\n"
        definition = extract_definition(text, "bool CPolygon::CreatePath(")
        assert definition.count("realloc(") == 2
        stored_drawing += definition.replace("realloc(", "drawingRealloc(") + "\n#undef CPolygon\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_drawing_storage.h").write_text(stored_drawing, encoding="utf-8")
    token_code = lexer_source.decode("utf-8-sig").split("#include", 1)[0]
    token_code += '#include "drawing_tokens_shell.h"\n'
    for name, content in (("OriginalDrawingTokens", lexer_source), ("CandidateDrawingTokens", candidate_lexer)):
        text = content.decode("utf-8-sig")
        if name == "OriginalDrawingTokens":
            block = original_token_input[original_token_input.index("    int k = 0;"):]
        else:
            a = text.index("    // Drawing token input boundary.")
            b = text.index("        // Drawing command state.", a)
            block = "XyRust::DrawingTokens tokens(str);\n" + text[a:b]
            token_code += "#define XY_VSFILTER_RUST_TEXT\n"
        token_code += f"namespace {name} {{\nDrawingTokenList Scan(const CStringW& input) {{\nDrawingTokenList result;\n"
        token_code += "CStringW str = input;\n" + block
        token_code += "result.emplace_back(c, s);\n}\nreturn result;\n}\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_drawing_tokens.h").write_text(token_code, encoding="utf-8")
    preparation_code = '#include "drawing_tokens_shell.h"\n'
    for name, content in (("OriginalDrawingPreparation", lexer_source), ("CandidateDrawingPreparation", candidate_lexer)):
        text = content.decode("utf-8-sig")
        a = text.index("    CStringW str = m_str.Get();", text.index("bool CPolygon::ParseStr()"))
        b = text.index("        switch(c)", a) if name == "OriginalDrawingPreparation" else text.index("        // Drawing command state.", a)
        block = text[a:b].replace("m_str.Get()", "input", 1)
        if name == "CandidateDrawingPreparation":
            assert block.count("XyRust::DrawingTokens tokens") == 1
            block = block.replace("XyRust::DrawingTokens tokens", "XyRust::ObservedDrawingTokens tokens")
            preparation_code += "#define XY_VSFILTER_RUST_TEXT\n"
        preparation_code += f"namespace {name} {{\nDrawingTokenList Scan(const CStringW& input) {{\nDrawingTokenList result;\n"
        preparation_code += block + "result.emplace_back(c,s);\n}\nreturn result;\n}\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_drawing_preparation.h").write_text(preparation_code, encoding="utf-8")

    hdmv_source = original("src/subtitles/CompositionObject.cpp", reference["commit"])
    candidate_hdmv = (ROOT / "src/subtitles/CompositionObject.cpp").read_bytes()
    golomb_source = original("src/dsutil/GolombBuffer.cpp", reference["commit"])
    golomb_header = original("src/dsutil/GolombBuffer.h", reference["commit"])
    raster_source = original("src/subtitles/Rasterizer.cpp", reference["commit"])
    path_align = '#include "path_align_shell.h"\n'
    for name, content in (("ReferenceAlign", raster_source), ("CandidateAlign", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        path_align += f"namespace {name} {{\n"
        path_align += "class PathData : public AlignState { public: PathData(const std::vector<POINT>& p,int n):AlignState(p,n){} void AlignLeftTop(CPoint*,CSize*); };\n"
        if name == "CandidateAlign":
            path_align += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(content.decode("utf-8-sig"), "void PathData::AlignLeftTop(")
        if name == "CandidateAlign":
            assert definition.count("XyRust::AlignPath") == 1
            definition = definition.replace("XyRust::AlignPath", "XyRust::ObservedAlignPath")
        path_align += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_path_align.h").write_text(path_align, encoding="utf-8")
    path_buffer = '#include "path_buffer_shell.h"\n'
    for name, content in (("ReferencePathBuffer", raster_source), ("CandidatePathBuffer", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        path_buffer += f"namespace {name} {{\n"
        path_buffer += "class PathData : public PathBufferState { public: explicit PathData(int n):PathBufferState(n){} bool PartialEndPath(HDC,PathOffset,PathOffset); };\n"
        if name == "CandidatePathBuffer":
            path_buffer += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(content.decode("utf-8-sig").replace("\r\n", "\n"), "bool PathData::PartialEndPath(")
        definition = definition.replace("long dx, long dy", "PathOffset dx, PathOffset dy")
        for before, after in (("::CloseFigure(", "::PathCloseFigure("), ("::EndPath(", "::PathEndPath("), ("::AbortPath(", "::PathAbortPath("), ("GetPath(", "PathGetPath("), ("DebugBreak()", "PathDebugBreak()"), ("realloc(", "pathRealloc(")):
            definition = definition.replace(before, after)
        if name == "CandidatePathBuffer":
            assert definition.count("XyRust::PathBuffer temporary") == 1
            definition = definition.replace("XyRust::PathBuffer temporary", "XyRust::ObservedPathBuffer temporary")
        else:
            assert definition.count("POINT* pPoints = DEBUG_NEW POINT[nPoints];") == 1
            definition = definition.replace("POINT* pPoints = DEBUG_NEW POINT[nPoints];", "POINT* pPoints = DEBUG_NEW POINT[nPoints];\n        PathTemporaryCreated();")
            assert definition.count("delete[] pPoints;") == 2
            definition = definition.replace("delete[] pPoints;", "delete[] pPoints; PathTemporaryReleased();")
        path_buffer += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_path_buffer.h").write_text(path_buffer, encoding="utf-8")
    span_merge = '#include "span_merge_shell.h"\n'
    for name, content in (("ReferenceSpans", raster_source), ("CandidateSpans", (ROOT / "src/subtitles/Rasterizer.cpp").read_bytes())):
        span_merge += f"namespace {name} {{\n"
        if name == "CandidateSpans":
            span_merge += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(content.decode("utf-8-sig").replace("\r\n", "\n"), "void OverlapRegion(")
        # Static reference control flow: ++itA is outside the unbraced while.
        # Guard that unresolved branch before entry; it is never counted as a pass.
        tail = "while(itA != itAE)\n        dst.push_back(*itA);\n        ++itA;"
        assert tail in definition
        definition = definition.replace(tail, "if(itA != itAE) throw UnresolvedSpanTail();\n    " + tail)
        if name == "CandidateSpans":
            assert definition.count("XyRust::SpanMerge merged") == 1
            definition = definition.replace("XyRust::SpanMerge merged", "XyRust::ObservedSpanMerge merged")
        if os.name != "nt":
            definition = definition.replace("unsigned __int64", "uint64_t").replace("__int64", "int64_t")
        span_merge += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_span_merge.h").write_text(span_merge, encoding="utf-8")
    queue_header = original("src/subtitles/xy_circular_array_queue.h", reference["commit"])
    (destination / "original_arc_queue.h").write_bytes(queue_header)
    widen_source = original("src/subtitles/xy_widen_region.cpp", reference["commit"])
    arc_code = '#include "arc_queue_shell.h"\n'
    for name, content in (("ReferenceArcs", widen_source), ("CandidateArcs", (ROOT / "src/subtitles/xy_widen_region.cpp").read_bytes())):
        text = content.decode("utf-8-sig")
        arc_code += f"namespace {name} {{\n"
        if name == "CandidateArcs":
            arc_code += "#define XY_VSFILTER_RUST_TEXT\n#ifdef XY_VSFILTER_RUST_TEXT\ntypedef XyRust::ArcQueue<LinkArcItem> LinkArc;\n#else\ntypedef XYCircularArrayQueue<LinkArcItem> LinkArc;\n#endif\n"
            for signature in ("static void ArcAppend(", "static void ArcCenter(", "static void ArcDeadline("):
                arc_code += extract_definition(text, signature) + "\n"
        else:
            arc_code += "typedef XYCircularArrayQueue<LinkArcItem> LinkArc;\n"
        arc_code += "class WidenRegionCreaterImpl { public: ArcEllipse* m_ellipse; explicit WidenRegionCreaterImpl(ArcEllipse* e):m_ellipse(e){} int cross_left(const LinkArc&,XY_POINT); void add_left(LinkArc*,XY_POINT); void add_right(LinkArc*,XY_POINT); };\n"
        for signature in ("int WidenRegionCreaterImpl::cross_left(", "void WidenRegionCreaterImpl::add_left(", "void WidenRegionCreaterImpl::add_right("):
            arc_code += extract_definition(text, signature) + "\n"
        arc_code += "}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_arc_updates.h").write_text(arc_code, encoding="utf-8")
    ellipse_code = '#include "ellipse_arc_shell.h"\n'
    for name, content in (("ReferenceEllipse", widen_source), ("CandidateEllipse", (ROOT / "src/subtitles/xy_widen_region.cpp").read_bytes())):
        text = content.decode("utf-8-sig")
        ellipse_code += f"namespace {name} {{\n"
        if name == "CandidateEllipse":
            ellipse_code += "#define XY_VSFILTER_RUST_TEXT\n"
        definition = extract_definition(text, "int gen_left_arc(")
        if name == "CandidateEllipse":
            assert definition.count("XyRust::GenerateEllipseArc") == 1
            definition = definition.replace("XyRust::GenerateEllipseArc", "XyRust::ObservedEllipseArc")
        if os.name != "nt":
            definition = definition.replace("__int64", "int64_t")
        ellipse_code += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_ellipse_arc.h").write_text(ellipse_code, encoding="utf-8")
    storage_code = '#include "ellipse_storage_shell.h"\n'
    for name, content in (("ReferenceEllipseStorage", widen_source), ("CandidateEllipseStorage", (ROOT / "src/subtitles/xy_widen_region.cpp").read_bytes())):
        text = content.decode("utf-8-sig")
        storage_code += f"namespace {name} {{\n"
        if name == "CandidateEllipseStorage":
            storage_code += "#define XY_VSFILTER_RUST_TEXT\n"
        start = text.index("struct XyEllipse")
        end = text.index("class WidenRegionCreaterImpl", start)
        definition = text[start:end] + extract_definition(text, "int gen_left_arc(") + "\n"
        definition += text[text.index("XyEllipse::XyEllipse()"):]
        if name == "CandidateEllipseStorage":
            assert definition.count("XyRust::EllipseScratch cross_x") == 1
            definition = definition.replace("XyRust::EllipseScratch cross_x", "XyRust::ObservedEllipseScratch cross_x")

        if os.name != "nt":
            definition = definition.replace("__int64", "int64_t")
        storage_code += definition + "\n}\n#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_ellipse_storage.h").write_text(storage_code, encoding="utf-8")
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
    stored_drawing_executable = destination / ("drawing-storage-oracle.exe" if os.name == "nt" else "drawing-storage-oracle")
    stored_drawing_compile = [arg.replace("decoder_oracle.cpp", "drawing_storage_oracle.cpp").replace("decoder-oracle", "drawing-storage-oracle") for arg in compile_command]
    run(stored_drawing_compile)
    stored_drawing_result = run([str(stored_drawing_executable)], capture_output=True, text=True)
    print(stored_drawing_result.stdout, end="", flush=True)
    print(stored_drawing_result.stderr, end="", file=sys.stderr)
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
    # Check text adapters before the longer pixel campaigns for faster feedback.
    find_char_executable = destination / ("find-char-oracle.exe" if os.name == "nt" else "find-char-oracle")
    find_char_compile = [arg.replace("fill_oracle.cpp", "find_char_oracle.cpp").replace("fill-oracle", "find-char-oracle") for arg in fill_compile]
    if os.name == "nt":
        find_char_compile += ["/D_UNICODE", "/DUNICODE", "gdi32.lib"]
    run(find_char_compile)
    find_char_result = run([str(find_char_executable)], capture_output=True, text=True)
    print(find_char_result.stdout, end="", flush=True)
    print(find_char_result.stderr, end="", file=sys.stderr)
    text_runs_executable = destination / ("text-runs-oracle.exe" if os.name == "nt" else "text-runs-oracle")
    text_runs_compile = [arg.replace("find_char_oracle.cpp", "text_runs_oracle.cpp").replace("find-char-oracle", "text-runs-oracle") for arg in find_char_compile]
    run(text_runs_compile)
    text_runs_result = run([str(text_runs_executable)], capture_output=True, text=True)
    print(text_runs_result.stdout, end="", flush=True)
    print(text_runs_result.stderr, end="", file=sys.stderr)
    line_layout_executable = destination / ("line-layout-oracle.exe" if os.name == "nt" else "line-layout-oracle")
    line_layout_compile = [arg.replace("find_char_oracle.cpp", "line_layout_oracle.cpp").replace("find-char-oracle", "line-layout-oracle") for arg in find_char_compile]
    run(line_layout_compile)
    line_layout_result = run([str(line_layout_executable)], capture_output=True, text=True)
    print(line_layout_result.stdout, end="", flush=True)
    print(line_layout_result.stderr, end="", file=sys.stderr)
    wrap_executable = destination / ("wrap-width-oracle.exe" if os.name == "nt" else "wrap-width-oracle")
    wrap_compile = [arg.replace("find_char_oracle.cpp", "wrap_width_oracle.cpp").replace("find-char-oracle", "wrap-width-oracle") for arg in find_char_compile]
    run(wrap_compile)
    wrap_result = run([str(wrap_executable)], capture_output=True, text=True)
    print(wrap_result.stdout, end="", flush=True)
    print(wrap_result.stderr, end="", file=sys.stderr)
    layout_executable = destination / ("layout-rects-oracle.exe" if os.name == "nt" else "layout-rects-oracle")
    layout_compile = [arg.replace("find_char_oracle.cpp", "layout_rects_oracle.cpp").replace("find-char-oracle", "layout-rects-oracle") for arg in find_char_compile]
    if os.name == "nt":
        layout_compile += ["user32.lib"]
    run(layout_compile)
    layout_result = run([str(layout_executable)], capture_output=True, text=True)
    print(layout_result.stdout, end="", flush=True)
    print(layout_result.stderr, end="", file=sys.stderr)
    seek_executable = destination / ("text-seek-oracle.exe" if os.name == "nt" else "text-seek-oracle")
    seek_compile = [arg.replace("find_char_oracle.cpp", "text_seek_oracle.cpp").replace("find-char-oracle", "text-seek-oracle") for arg in find_char_compile]
    run(seek_compile)
    seek_result = run([str(seek_executable)], capture_output=True, text=True)
    print(seek_result.stdout, end="", flush=True)
    print(seek_result.stderr, end="", file=sys.stderr)
    path_align_executable = destination / ("path-align-oracle.exe" if os.name == "nt" else "path-align-oracle")
    path_align_compile = [arg.replace("find_char_oracle.cpp", "path_align_oracle.cpp").replace("find-char-oracle", "path-align-oracle") for arg in find_char_compile]
    run(path_align_compile)
    path_align_result = run([str(path_align_executable)], capture_output=True, text=True)
    print(path_align_result.stdout, end="", flush=True)
    print(path_align_result.stderr, end="", file=sys.stderr)
    path_buffer_executable = destination / ("path-buffer-oracle.exe" if os.name == "nt" else "path-buffer-oracle")
    path_buffer_compile = [arg.replace("find_char_oracle.cpp", "path_buffer_oracle.cpp").replace("find-char-oracle", "path-buffer-oracle") for arg in find_char_compile]
    run(path_buffer_compile)
    path_buffer_result = run([str(path_buffer_executable)], capture_output=True, text=True)
    print(path_buffer_result.stdout, end="", flush=True)
    print(path_buffer_result.stderr, end="", file=sys.stderr)
    span_executable = destination / ("span-merge-oracle.exe" if os.name == "nt" else "span-merge-oracle")
    span_compile = [arg.replace("find_char_oracle.cpp", "span_merge_oracle.cpp").replace("find-char-oracle", "span-merge-oracle") for arg in find_char_compile]
    run(span_compile)
    span_result = run([str(span_executable)], capture_output=True, text=True)
    print(span_result.stdout, end="", flush=True)
    print(span_result.stderr, end="", file=sys.stderr)
    writer_executable = destination / ("text-writer-oracle.exe" if os.name == "nt" else "text-writer-oracle")
    writer_compile = [arg.replace("find_char_oracle.cpp", "text_writer_oracle.cpp").replace("find-char-oracle", "text-writer-oracle") for arg in find_char_compile]
    run(writer_compile)
    writer_result = run([str(writer_executable)], capture_output=True, text=True)
    print(writer_result.stdout, end="", flush=True)
    print(writer_result.stderr, end="", file=sys.stderr)
    arc_executable = destination / ("arc-queue-oracle.exe" if os.name == "nt" else "arc-queue-oracle")
    arc_compile = [arg.replace("find_char_oracle.cpp", "arc_queue_oracle.cpp").replace("find-char-oracle", "arc-queue-oracle") for arg in find_char_compile]
    run(arc_compile)
    arc_result = run([str(arc_executable)], capture_output=True, text=True)
    print(arc_result.stdout, end="", flush=True)
    print(arc_result.stderr, end="", file=sys.stderr)
    ellipse_executable = destination / ("ellipse-arc-oracle.exe" if os.name == "nt" else "ellipse-arc-oracle")
    ellipse_compile = [arg.replace("find_char_oracle.cpp", "ellipse_arc_oracle.cpp").replace("find-char-oracle", "ellipse-arc-oracle") for arg in find_char_compile]
    run(ellipse_compile)
    ellipse_result = run([str(ellipse_executable)], capture_output=True, text=True)
    print(ellipse_result.stdout, end="", flush=True)
    print(ellipse_result.stderr, end="", file=sys.stderr)
    ellipse_storage_executable = destination / ("ellipse-storage-oracle.exe" if os.name == "nt" else "ellipse-storage-oracle")
    ellipse_storage_compile = [arg.replace("find_char_oracle.cpp", "ellipse_storage_oracle.cpp").replace("find-char-oracle", "ellipse-storage-oracle") for arg in find_char_compile]
    # This oracle observes the FP environment. Clang's ordinary optimized
    # packed math can evaluate unused lanes and raise ambient denormal flags
    # even between two reference executions with different allocation history.
    # Keep the native source, but require FP-observable compilation here.
    # Actual production flags are exercised separately by paired Windows DLLs.
    if os.name == "nt":
        ellipse_storage_compile += ["/fp:strict"]
    elif sys.platform == "darwin":
        ellipse_storage_compile += ["-ffp-model=strict"]
    else:
        ellipse_storage_compile += ["-frounding-math", "-ftrapping-math", "-fno-tree-vectorize", "-fno-tree-slp-vectorize"]
    run(ellipse_storage_compile)
    ellipse_storage_result = run([str(ellipse_storage_executable)], capture_output=True, text=True)
    print(ellipse_storage_result.stdout, end="", flush=True)
    print(ellipse_storage_result.stderr, end="", file=sys.stderr)
    pipe_line_executable = destination / ("pipe-line-oracle.exe" if os.name == "nt" else "pipe-line-oracle")
    pipe_line_compile = [arg.replace("find_char_oracle.cpp", "pipe_line_oracle.cpp").replace("find-char-oracle", "pipe-line-oracle") for arg in find_char_compile]
    run(pipe_line_compile)
    pipe_line_result = run([str(pipe_line_executable)], capture_output=True, text=True)
    print(pipe_line_result.stdout, end="", flush=True)
    print(pipe_line_result.stderr, end="", file=sys.stderr)
    subrip_cue_executable = destination / ("subrip-cue-oracle.exe" if os.name == "nt" else "subrip-cue-oracle")
    subrip_cue_compile = [arg.replace("find_char_oracle.cpp", "subrip_cue_oracle.cpp").replace("find-char-oracle", "subrip-cue-oracle") for arg in find_char_compile]
    run(subrip_cue_compile)
    subrip_cue_result = run([str(subrip_cue_executable)], capture_output=True, text=True)
    print(subrip_cue_result.stdout, end="", flush=True)
    print(subrip_cue_result.stderr, end="", file=sys.stderr)
    sami_file_executable = destination / ("sami-file-oracle.exe" if os.name == "nt" else "sami-file-oracle")
    sami_file_compile = [arg.replace("find_char_oracle.cpp", "sami_file_oracle.cpp").replace("find-char-oracle", "sami-file-oracle") for arg in find_char_compile]
    run(sami_file_compile)
    sami_file_result = run([str(sami_file_executable)], capture_output=True, text=True)
    print(sami_file_result.stdout, end="", flush=True)
    print(sami_file_result.stderr, end="", file=sys.stderr)
    micro_style_executable = destination / ("microdvd-style-oracle.exe" if os.name == "nt" else "microdvd-style-oracle")
    micro_style_compile = [arg.replace("find_char_oracle.cpp", "microdvd_style_oracle.cpp").replace("find-char-oracle", "microdvd-style-oracle") for arg in find_char_compile]
    run(micro_style_compile)
    micro_style_result = run([str(micro_style_executable)], capture_output=True, text=True)
    print(micro_style_result.stdout, end="", flush=True)
    print(micro_style_result.stderr, end="", file=sys.stderr)
    document_executable = destination / ("ass-document-oracle.exe" if os.name == "nt" else "ass-document-oracle")
    document_compile = [arg.replace("find_char_oracle.cpp", "ass_document_oracle.cpp").replace("find-char-oracle", "ass-document-oracle") for arg in find_char_compile]
    run(document_compile)
    document_result = run([str(document_executable)], capture_output=True, text=True)
    print(document_result.stdout, end="", flush=True)
    print(document_result.stderr, end="", file=sys.stderr)
    style_executable = destination / ("ass-style-oracle.exe" if os.name == "nt" else "ass-style-oracle")
    style_compile = [arg.replace("find_char_oracle.cpp", "ass_style_oracle.cpp").replace("find-char-oracle", "ass-style-oracle") for arg in find_char_compile]
    run(style_compile)
    style_result = run([str(style_executable)], capture_output=True, text=True)
    print(style_result.stdout, end="", flush=True)
    print(style_result.stderr, end="", file=sys.stderr)
    dialogue_executable = destination / ("ass-dialogue-oracle.exe" if os.name == "nt" else "ass-dialogue-oracle")
    dialogue_compile = [arg.replace("find_char_oracle.cpp", "ass_dialogue_oracle.cpp").replace("find-char-oracle", "ass-dialogue-oracle") for arg in find_char_compile]
    run(dialogue_compile)
    dialogue_result = run([str(dialogue_executable)], capture_output=True, text=True)
    print(dialogue_result.stdout, end="", flush=True)
    print(dialogue_result.stderr, end="", file=sys.stderr)
    encoding_executable = destination / ("encoding-oracle.exe" if os.name == "nt" else "encoding-oracle")
    encoding_compile = [arg.replace("find_char_oracle.cpp", "encoding_oracle.cpp").replace("find-char-oracle", "encoding-oracle") for arg in find_char_compile]
    run(encoding_compile)
    encoding_result = run([str(encoding_executable)], capture_output=True, text=True)
    print(encoding_result.stdout, end="", flush=True)
    print(encoding_result.stderr, end="", file=sys.stderr)
    microdvd_executable = destination / ("microdvd-oracle.exe" if os.name == "nt" else "microdvd-oracle")
    microdvd_compile = [arg.replace("find_char_oracle.cpp", "microdvd_oracle.cpp").replace("find-char-oracle", "microdvd-oracle") for arg in find_char_compile]
    run(microdvd_compile)
    microdvd_result = run([str(microdvd_executable)], capture_output=True, text=True)
    print(microdvd_result.stdout, end="", flush=True)
    print(microdvd_result.stderr, end="", file=sys.stderr)
    mpl2_executable = destination / ("mpl2-oracle.exe" if os.name == "nt" else "mpl2-oracle")
    mpl2_compile = [arg.replace("microdvd_oracle.cpp", "mpl2_oracle.cpp").replace("microdvd-oracle", "mpl2-oracle") for arg in microdvd_compile]
    run(mpl2_compile)
    mpl2_result = run([str(mpl2_executable)], capture_output=True, text=True)
    print(mpl2_result.stdout, end="", flush=True)
    print(mpl2_result.stderr, end="", file=sys.stderr)
    remove_tags_executable = destination / ("remove-tags-oracle.exe" if os.name == "nt" else "remove-tags-oracle")
    remove_tags_compile = [arg.replace("microdvd_oracle.cpp", "remove_tags_oracle.cpp").replace("microdvd-oracle", "remove-tags-oracle") for arg in microdvd_compile]
    run(remove_tags_compile)
    remove_tags_result = run([str(remove_tags_executable)], capture_output=True, text=True)
    print(remove_tags_result.stdout, end="", flush=True)
    print(remove_tags_result.stderr, end="", file=sys.stderr)
    case_replace_executable = destination / ("case-replace-oracle.exe" if os.name == "nt" else "case-replace-oracle")
    case_replace_compile = [arg.replace("microdvd_oracle.cpp", "case_replace_oracle.cpp").replace("microdvd-oracle", "case-replace-oracle") for arg in microdvd_compile]
    run(case_replace_compile)
    case_replace_result = run([str(case_replace_executable)], capture_output=True, text=True)
    print(case_replace_result.stdout, end="", flush=True)
    print(case_replace_result.stderr, end="", file=sys.stderr)
    sami_executable = destination / ("sami-oracle.exe" if os.name == "nt" else "sami-oracle")
    sami_compile = [arg.replace("microdvd_oracle.cpp", "sami_oracle.cpp").replace("microdvd-oracle", "sami-oracle") for arg in microdvd_compile]
    run(sami_compile)
    sami_result = run([str(sami_executable)], capture_output=True, text=True)
    print(sami_result.stdout, end="", flush=True)
    print(sami_result.stderr, end="", file=sys.stderr)
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
    realtext_format_executable = destination / ("realtext-format-oracle.exe" if os.name == "nt" else "realtext-format-oracle")
    realtext_format_compile = [arg.replace("fill_oracle.cpp", "realtext_format_oracle.cpp").replace("fill-oracle", "realtext-format-oracle") for arg in fill_compile]
    run(realtext_format_compile)
    realtext_format_result = run([str(realtext_format_executable)], capture_output=True, text=True)
    print(realtext_format_result.stdout, end="", flush=True)
    print(realtext_format_result.stderr, end="", file=sys.stderr)
    realtext_lists_executable = destination / ("realtext-lists-oracle.exe" if os.name == "nt" else "realtext-lists-oracle")
    realtext_lists_compile = [arg.replace("fill_oracle.cpp", "realtext_lists_oracle.cpp").replace("fill-oracle", "realtext-lists-oracle") for arg in fill_compile]
    run(realtext_lists_compile)
    realtext_lists_result = run([str(realtext_lists_executable)], capture_output=True, text=True)
    print(realtext_lists_result.stdout, end="", flush=True)
    print(realtext_lists_result.stderr, end="", file=sys.stderr)
    realtext_document_executable = destination / ("realtext-document-oracle.exe" if os.name == "nt" else "realtext-document-oracle")
    realtext_document_compile = [arg.replace("fill_oracle.cpp", "realtext_document_oracle.cpp").replace("fill-oracle", "realtext-document-oracle") for arg in fill_compile]
    run(realtext_document_compile)
    realtext_document_result = run([str(realtext_document_executable)], capture_output=True, text=True)
    print(realtext_document_result.stdout, end="", flush=True)
    print(realtext_document_result.stderr, end="", file=sys.stderr)
    cc_executable = destination / ("cc-oracle.exe" if os.name == "nt" else "cc-oracle")
    cc_compile = [arg.replace("fill_oracle.cpp", "cc_oracle.cpp").replace("fill-oracle", "cc-oracle") for arg in fill_compile]
    if os.name == "nt":
        cc_compile += ["/D_UNICODE", "/DUNICODE"]
    run(cc_compile)
    cc_result = run([str(cc_executable)], capture_output=True, text=True)
    print(cc_result.stdout, end="", flush=True)
    print(cc_result.stderr, end="", file=sys.stderr)
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
        "cc_compile_command": cc_compile,
        "cc_result": cc_result.stdout, "cc_stderr": cc_result.stderr,
        "cc_text_instrumentation": "candidate CaptionText handled/declined queries counted; complete SaveDisp with fixture Add boundary and native ATL strings on Windows",
        "cc_scope": "owned byte-loop command state, exact raw-byte tables/duplicate consumption, cursor normalization, initialized cell update and owned SaveDisp text; native display transitions/timestamp arithmetic retained; complete DecodeCC with fixture subtitle sink, real Windows ATL strings and portable UTF-16 string model; raw logging/files and native arithmetic overflow outside fixtures",
        "cc_source_sha256": hashlib.sha256(cc_source).hexdigest(), "cc_header_sha256": hashlib.sha256(cc_header).hexdigest(),
        "realtext_document_compile_command": realtext_document_compile,
        "realtext_document_result": realtext_document_result.stdout, "realtext_document_stderr": realtext_document_result.stderr,
        "realtext_document_scope": "owned document control flow; native extraction/conversion/render/public-state actions retain order; callbacks observe partial public state and injected interruptions across repeated parses; representable numeric fixtures only",
        "realtext_lists_compile_command": realtext_lists_compile,
        "realtext_lists_result": realtext_lists_result.stdout, "realtext_lists_stderr": realtext_lists_result.stderr,
        "realtext_lists_scope": "owned current/open tag lists with copied raw names, flags and ordered attributes; native list-operation fixture shell calls pinned PopTag, plus complete ParseRealText composition; allocation failure timing unverified",
        "realtext_format_compile_command": realtext_format_compile,
        "realtext_format_result": realtext_format_result.stdout, "realtext_format_stderr": realtext_format_result.stderr,
        "realtext_format_scope": "signed timestamp components and original linear precision divisor; native stream construction, locale, separators, insertion order and OutputSRT retained; unrepresentable precision arithmetic excluded from parity",
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
        "text_runs_compile_command": text_runs_compile,
        "text_runs_result": text_runs_result.stdout, "text_runs_stderr": text_runs_result.stderr,
        "text_runs_scope": "Complete pinned CRenderedTextSubtitle::ParseString; actual ATL strings on Windows, portable strings elsewhere; Rust escape preparation with native NUL fallback; modeled CText constructor and word-list boundaries, exact word arguments/float bits, live karaoke/scaling state, callback mutations/interruption and partial output; replacement-bearing NUL fixtures, native font metrics and allocation failures excluded",
        "line_layout_compile_command": line_layout_compile,
        "line_layout_result": line_layout_result.stdout, "line_layout_stderr": line_layout_result.stderr,
        "line_layout_scope": "Complete pinned CSubtitle::MakeLines; actual ATL rectangle/point/size on Windows; modeled line creation and insertion, exact retained lines/borders/rectangles and callback order, interruptions and live field mutations, errno/Windows LastError; representable arithmetic and successful allocation only",
        "wrap_width_compile_command": wrap_compile,
        "wrap_width_result": wrap_result.stdout, "wrap_width_stderr": wrap_result.stderr,
        "wrap_width_scope": "Complete pinned CSubtitle::GetWrapWidth and native GetFullLineWidth; native word metrics modeled, actual ATL list on Windows; exact result and ordered word traversal, errno/Windows LastError; representable arithmetic only; full-line accumulation and native font metrics retained",
        "layout_rects_compile_command": layout_compile,
        "layout_rects_result": layout_result.stdout, "layout_rects_stderr": layout_result.stderr,
        "layout_rects_scope": "Complete pinned CScreenLayoutAllocator methods and class record layout; actual ATL CRect/list/array on Windows, controlled models on portable hosts; exact returned rectangles and full ordered retained records after each operation; Rust collision passes and segment retention, native border geometry; errno/Windows LastError; representable arithmetic, finite collision sequences and successful allocation only",
        "text_seek_compile_command": seek_compile,
        "text_seek_result": seek_result.stdout, "text_seek_stderr": seek_result.stderr,
        "text_seek_scope": "Complete pinned Seek/GetPosition/GetLength with controlled native file callbacks and buffer bookkeeping; exact unsigned wrap/signed clamp, returned target and state, interruptions/mutations, errno/Windows LastError; physical signed addition overflow excluded; full reader oracle separately exercises actual Rust buffer",
        "path_align_compile_command": path_align_compile,
        "path_align_result": path_align_result.stdout, "path_align_stderr": path_align_result.stderr,
        "path_align_scope": "Complete pinned PathData::AlignLeftTop over initialized point/type buffers; actual Windows POINT/ATL geometry; exact guarded buffers, origin/size, errno/FP/Windows error state; native cleanup modeled, outputs disjoint and representable arithmetic only",
        "ellipse_storage_compile_command": ellipse_storage_compile,
        "ellipse_storage_result": ellipse_storage_result.stdout, "ellipse_storage_stderr": ellipse_storage_result.stderr,
        "ellipse_storage_scope": "Complete pinned XyEllipse initialization/query/destruction including native floating-point intersection tables, compiled with FP-observable settings recorded in the command; ordinary optimized x64 ambient flag mismatch is unresolved and is not counted as a pass. Compare valid centered arc indices, initialized negative matrix rows, float/double fields, returned intersections and errno/FP/Windows error state; center-coordinate native overloads tested for nonnegative relative offsets, candidate negative offsets separately checked against direct native indexed values. No uninitialized row, out-of-range query, OOM or unsupported arithmetic is counted as parity.",
        "ellipse_arc_compile_command": ellipse_compile,
        "ellipse_arc_result": ellipse_result.stdout, "ellipse_arc_stderr": ellipse_result.stderr,
        "ellipse_arc_scope": "Complete pinned integer gen_left_arc with initialized guarded buffers; degenerate axes, exhaustive bounded radii, large aspect ratios and repeated generation, untouched padding, native return/errno/FP/Windows error state. Nonnegative radii and representable native arithmetic; original fallback retained on decline. Native floating-point intersection tables unchanged.",
        "arc_queue_compile_command": arc_compile,
        "arc_queue_result": arc_result.stdout, "arc_queue_stderr": arc_result.stderr,
        "arc_queue_scope": "Pinned circular queue and complete add_left/add_right/cross_left methods with controlled native ellipse callbacks; initialized positive capacities and nonnegative representable indices/counts, successful allocation; every physical slot including removed slots, callback order, errno/FP/Windows error state. Ellipse math and outer span/list algorithm remain native and require renderer comparisons.",
        "path_buffer_compile_command": path_buffer_compile,
        "path_buffer_result": path_buffer_result.stdout, "path_buffer_stderr": path_buffer_result.stderr,
        "path_buffer_scope": "Pinned PartialEndPath with modeled output-only GDI and guarded realloc boundaries; successful complete output, zero points, EndPath failure, callback order, buffer bytes/capacity and cleanup timing. DebugBreak continuations are modeled separately and never counted as native compatibility passes. Rust buffers lend both disjoint outputs in one FFI call; no Rust call using the owner until native consumption ends. Native OOM, negative counts and unrepresentable geometry excluded.",
        "span_merge_compile_command": span_compile,
        "span_merge_result": span_result.stdout, "span_merge_stderr": span_result.stderr,
        "span_merge_scope": "Pinned OverlapRegion with leftover-A loop guarded before entry and excluded, never treated as a passing comparison; direct Rust decline checked for excluded cases. Exact terminating span outputs/order, source preservation, vector capacity, errno/FP flags, repeated production widening; native offsets retained, dy>=0 and successful allocation only",
        "text_writer_compile_command": writer_compile,
        "text_writer_result": writer_result.stdout, "text_writer_stderr": writer_result.stderr,
        "text_writer_scope": "Complete pinned CTextFile::WriteString; native ATL strings/UTF-8 conversion on Windows, modeled UTF-8 and 16-bit Windows write layout on portable hosts; controlled native write boundaries, exact bytes/default-string arguments, errno/LastError and callback interruptions/source mutation; allocation failures and OS file persistence excluded",
        "pipe_line_compile_command": pipe_line_compile,
        "pipe_line_result": pipe_line_result.stdout, "pipe_line_stderr": pipe_line_result.stderr,
        "pipe_line_scope": "Complete pinned OpenOldSubRipper/OpenVPlayer and FindChar; native ATL/CRT/codepage APIs on Windows, portable strings and codepage models elsewhere; controlled file/trim/Add boundaries; exact output records, input positions, errno and ordered callbacks/interruption; representable numeric arithmetic",
        "subrip_cue_compile_command": subrip_cue_compile,
        "subrip_cue_result": subrip_cue_result.stdout, "subrip_cue_stderr": subrip_cue_result.stderr,
        "subrip_cue_scope": "Complete pinned OpenSubRipper and SubRipper2SSA; native ATL/secure CRT on Windows, portable wide-character scan spellings elsewhere; controlled line I/O, FastTrim and Add sink; exact emitted records, input positions, errno and native callback order/interruption; successful representable numeric conversions and replacement-free NUL fixtures",
        "sami_file_compile_command": sami_file_compile,
        "sami_file_result": sami_file_result.stdout, "sami_file_stderr": sami_file_result.stderr,
        "sami_file_scope": "Complete pinned OpenSami and SMI2SSA; native ATL/case/CRT on Windows, portable strings elsewhere; controlled line I/O, FastTrim and Add sink; probe read-ahead, seek, caption/comment state, timestamp callback ordering, exact emitted records, errno and interruptions; representable timestamp arithmetic and replacement-free NUL fixtures",
        "microdvd_style_compile_command": micro_style_compile,
        "microdvd_style_result": micro_style_result.stdout, "microdvd_style_stderr": micro_style_result.stderr,
        "microdvd_style_scope": "Whole pinned GetMicroDVDStyle and native defaults; native ATL/CRT on Windows, portable strings elsewhere; all stored fields/float bits, native case/prefix/scan order, errno and floating flags; successful position scans and representable scanf values only; allocation failures and full file/style-map integration excluded",
        "ass_document_compile_command": document_compile,
        "ass_document_result": document_result.stdout, "ass_document_stderr": document_result.stderr,
        "ass_document_scope": "Complete pinned OpenSubStationAlpha and LoadUUEFont, including migrated header/dialogue/style/font-record owners; ignored loader replies and mutations; native ATL/CRT in Windows CI, portable strings elsewhere; controlled line I/O, FastTrim utility, style-map and font-registration boundaries; complete retained settings, entry arguments, input position, callback order/interruption and partial failures; representable numeric arithmetic only",
        "ass_style_compile_command": style_compile,
        "ass_style_result": style_result.stdout, "ass_style_stderr": style_result.stderr,
        "ass_style_scope": "Complete pinned style branch and native defaults; actual ATL/CRT in Windows CI, portable string model elsewhere; remaining input, all style fields/float bits, errno/floating flags, retained numeric callbacks and interruptions; GetStr private TrimLeft call identity already replaced by prior StringField migration; overflowing scanf conversions and full file/style-map integration excluded",
        "ass_dialogue_compile_command": dialogue_compile,
        "ass_dialogue_result": dialogue_result.stdout, "ass_dialogue_stderr": dialogue_result.stderr,
        "ass_dialogue_scope": "Complete OpenSubStationAlpha dialogue branch and NextInt/TryNextStr against pinned source; i64 literal suffix spelling normalized only; native ATL/CRT on Windows, portable string models elsewhere; raw UTF-16/NULs, failed fields, callback order, entry arguments and errno; full file/style parsing and unrepresentable timestamp arithmetic excluded",
        "encoding_compile_command": encoding_compile,
        "encoding_result": encoding_result.stdout, "encoding_stderr": encoding_result.stderr,
        "encoding_scope": "Owned ToMBCS, ToUnicode and both SSA wrappers against pinned methods; native fragment concatenation, ATL and Windows conversion APIs in Windows CI, portable conversion models elsewhere; raw UTF-16/NULs, initialized returned bytes only, failed conversions, trailing lead bytes, callback order/interruption and errno/LastError",
        "sami_compile_command": sami_compile,
        "sami_result": sami_result.stdout, "sami_stderr": sami_result.stderr,
        "sami_scope": "Whole SMI2SSA and ReplaceNoCase against pinned method/table data; native ATL CStringW and CAtlMap/CRT on Windows, portable models elsewhere; table name pointers const-qualified without changing entries; native numeric/color/trim callbacks, post-trim mutations, owned font arguments and errno; replacement-bearing NUL outcomes excluded",
        "case_replace_compile_command": case_replace_compile,
        "case_replace_result": case_replace_result.stdout, "case_replace_stderr": case_replace_result.stderr,
        "case_replace_scope": "Whole ReplaceNoCase with native lowercase checkpoint, mutable caller text, source/from/to aliases and source mutations during lowercase; Windows ATL/CRT in CI; empty-needle nonterminating and replacement-bearing NUL outcomes excluded",
        "remove_tags_compile_command": remove_tags_compile,
        "remove_tags_result": remove_tags_result.stdout, "remove_tags_stderr": remove_tags_result.stderr,
        "remove_tags_scope": "Whole RemoveSSATags composed with both FindChar paths; native replacement-free NUL fallback; replacement-bearing NUL outcomes excluded; native Windows ATL/CRT and charset APIs in CI",
        "mpl2_compile_command": mpl2_compile,
        "mpl2_result": mpl2_result.stdout, "mpl2_stderr": mpl2_result.stderr,
        "mpl2_scope": "Whole MPL22SSA composed with MicroDVD and FindChar; native NUL-without-replacement fallback; tag/replacement-bearing NUL outcomes excluded; Windows ATL/CRT and charset APIs in CI",
        "microdvd_compile_command": microdvd_compile,
        "microdvd_result": microdvd_result.stdout, "microdvd_stderr": microdvd_result.stderr,
        "microdvd_scope": "Complete MicroDVD2SSA and charset scanner against pinned reference; Windows ATL/CRT and charset APIs in CI, portable models locally; valid numeric scans, native callback interruptions, raw UTF-16 and embedded NULs",
        "find_char_compile_command": find_char_compile,
        "find_char_result": find_char_result.stdout, "find_char_stderr": find_char_result.stderr,
        "find_char_scope": "Native charset-aware FindChar and Unicode NUL/start/sentinel comparisons; actual Windows ATL, charset and lead-byte APIs in Windows CI, portable model elsewhere; classification order and injected interruptions; representable charset arithmetic only",
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
        "drawing_storage_compile_command": stored_drawing_compile,
        "drawing_storage_result": stored_drawing_result.stdout, "drawing_storage_stderr": stored_drawing_result.stderr,
        "drawing_storage_scope": "Pinned complete ParseStr with synthetic token/point delivery, copy constructor and CreatePath; native finite representable scaling and metrics, exact path records and copies, cache bypass, errno/FP flags, guarded modeled allocation/reallocation failures and retry; native OS allocation failure and empty valid paths excluded",
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
