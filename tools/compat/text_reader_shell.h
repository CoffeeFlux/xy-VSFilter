// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only in-memory stand-in for MFC file I/O. All decoding, BOM detection,
// line reading, seeking and position-accounting methods come from the sources.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include "xy_rust_reader.h"
#include "xy_rust_seek.h"

using std::min;
using std::max;
// Windows uses long long here even on platforms where int64_t aliases long.
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
static_assert(sizeof(LONGLONG) == 8, "Windows LONGLONG width");
typedef uint16_t WORD;
typedef unsigned char BYTE;
typedef unsigned int UINT;
typedef const char* LPCTSTR;

class MemoryFile {
public:
    enum { begin, current, end, modeRead = 1, typeBinary = 2, shareDenyNone = 4 };
    std::vector<unsigned char> bytes;
    size_t offset;
    size_t read_limit, fail_read, read_calls;
    bool partial_failure;
    std::vector<size_t> read_requests;
    std::vector<int> diagnostics;
    explicit MemoryFile(const std::vector<unsigned char>& input) : bytes(input), offset(0),
        read_limit(4096), fail_read(0), read_calls(0), partial_failure(false) {}
    bool Open(LPCTSTR, int) { offset = 0; return true; }
    void Close() { offset = 0; }
    size_t Read(void* target, size_t count) {
        read_requests.push_back(count);
        ++read_calls;
        count = min(min(count, bytes.size() - offset), read_limit);
        if (read_calls == fail_read) {
            if (partial_failure && count) {
                static_cast<unsigned char*>(target)[0] = bytes[offset++];
            }
            throw std::runtime_error("injected native read failure");
        }
        if (count) memcpy(target, &bytes[offset], count);
        offset += count;
        return count;
    }
    ULONGLONG GetPosition() const { return offset; }
    ULONGLONG GetLength() const { return bytes.size(); }
    ULONGLONG Seek(LONGLONG position, unsigned origin) {
        if (origin != begin || position < 0 || static_cast<uint64_t>(position) > bytes.size()) abort();
        offset = static_cast<size_t>(position);
        return offset;
    }
};

struct OriginalTextState {
    unsigned char m_readbuffer[4096];
    size_t m_bufferPos, m_bufferCount;
    std::vector<wchar_t> m_convertedBuffer;
    OriginalTextState() : m_bufferPos(0), m_bufferCount(0) {}
    size_t BufferPosition() const { return m_bufferPos; }
    size_t BufferCount() const { return m_bufferCount; }
    std::vector<wchar_t> Converted() const { return m_convertedBuffer; }
};
struct CandidateTextState {
    XyRust::TextBuffer m_rustBuffer;
    size_t BufferPosition() const { return m_rustBuffer.Position(); }
    size_t BufferCount() const { return m_rustBuffer.Count(); }
    std::vector<wchar_t> Converted() const {
        std::vector<wchar_t> result;
        m_rustBuffer.CopyLine(result);
        return result;
    }
};

// This shell is instantiated twice using a macro name. The original methods
// use __super, which the generated test header maps to MemoryFile.
#define TEXT_READER_SHELL(Name, State) \
class Name : public MemoryFile, public State { \
public: \
    enum enc { DEFAULT_ENCODING, UTF8, LE16, BE16 }; \
    enum charerror { CHARERR_NEED_MORE=-1, CHARERR_REOPEN=-2 }; \
    enc m_encoding, m_defaultencoding; \
    int m_offset; \
    Name(const std::vector<unsigned char>& input, int encoding) : MemoryFile(input), \
        m_encoding(static_cast<enc>(encoding)), m_defaultencoding(m_encoding), \
        m_offset(0) {} \
    bool Open(LPCTSTR name); \
    ULONGLONG GetPosition() const; \
    ULONGLONG GetLength() const; \
    ULONGLONG Seek(LONGLONG offset, unsigned origin); \
    int NextChar(); \
    bool ReadLine(); \
};
