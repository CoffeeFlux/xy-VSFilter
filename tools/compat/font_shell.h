// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal test stand-ins for Windows/ATL storage, not for the decoding algorithm.
#include <cstdint>
#include <vector>
typedef unsigned char BYTE;
typedef uint16_t TCHAR;
typedef const TCHAR* LPCTSTR;
class CString {
    const std::vector<uint16_t>& data;
public:
    explicit CString(const std::vector<uint16_t>& input) : data(input) {}
    int GetLength() const { return static_cast<int>(data.size()); }
    operator const TCHAR*() const { return data.data(); }
};
template<typename T> class CAutoVectorPtr {
    std::vector<T> data;
public:
    bool Allocate(int size) { data.resize(size); return true; }
    operator T*() { return data.data(); }
};
