// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only in-memory stand-in for MFC file I/O. All decoding, BOM detection,
// line reading, seeking and position-accounting methods come from the sources.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

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
    explicit MemoryFile(const std::vector<unsigned char>& input) : bytes(input), offset(0) {}
    bool Open(LPCTSTR, int) { offset = 0; return true; }
    void Close() { offset = 0; }
    size_t Read(void* target, size_t count) {
        count = min(count, bytes.size() - offset);
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

// This shell is instantiated twice using a macro name. The original methods
// use __super, which the generated test header maps to MemoryFile.
#define TEXT_READER_SHELL(Name) \
class Name : public MemoryFile { \
public: \
    enum enc { DEFAULT_ENCODING, UTF8, LE16, BE16 }; \
    enum charerror { CHARERR_NEED_MORE=-1, CHARERR_REOPEN=-2 }; \
    enc m_encoding, m_defaultencoding; \
    int m_offset; \
    unsigned char m_readbuffer[4096]; \
    size_t m_bufferPos, m_bufferCount; \
    std::vector<wchar_t> m_convertedBuffer; \
    Name(const std::vector<unsigned char>& input, int encoding) : MemoryFile(input), \
        m_encoding(static_cast<enc>(encoding)), m_defaultencoding(m_encoding), \
        m_offset(0), m_bufferPos(0), m_bufferCount(0) {} \
    bool Open(LPCTSTR name); \
    ULONGLONG GetPosition() const; \
    ULONGLONG GetLength() const; \
    ULONGLONG Seek(LONGLONG offset, unsigned origin); \
    int NextChar(); \
    bool ReadLine(); \
};
