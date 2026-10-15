/************************************************************************/
/* author: xy                                                           */
/* date: 20110512                                                       */
/************************************************************************/
#include "stdafx.h"
#include "xy_logger.h"
#include <mutex>

std::ostream& operator<<(std::ostream& os, const POINT& obj)
{
    return os<<"("<<obj.x<<","<<obj.y<<")";
}

std::wostream& operator<<(std::wostream& os, const POINT& obj)
{
    return os<<L"("<<obj.x<<L","<<obj.y<<L")";
}

std::ostream& operator<<(std::ostream& os, const SIZE& obj)
{
    return os<<"("<<obj.cx<<","<<obj.cy<<")";
}

std::wostream& operator<<( std::wostream& os, const SIZE& obj )
{
    return os<<L"("<<obj.cx<<L","<<obj.cy<<L")";
}

std::ostream& operator<<(std::ostream& os, const RECT& obj)
{
    return os<<"("<<obj.left<<","<<obj.top<<","<<obj.right<<","<<obj.bottom<<")";
}

std::wostream& operator<<( std::wostream& os, const RECT& obj )
{
    return os<<L"("<<obj.left<<L","<<obj.top<<L","<<obj.right<<L","<<obj.bottom<<L")";
}


namespace xy_logger
{

namespace
{
    const TCHAR* const g_level_names[] = {
        _T("TRACE"), _T("DEBUG"), _T("INFO"), _T("WARN"), _T("ERROR"), _T("FATAL"), _T("OFF")
    };

    LogLevel g_level = TRACE_LOG_LEVEL;
    FILE* g_file = NULL;
    std::mutex g_mutex;

    tstring Trim(const tstring& s)
    {
        const TCHAR* ws = _T(" \t\r\n");
        size_t b = s.find_first_not_of(ws);
        if (b == tstring::npos)
            return tstring();
        return s.substr(b, s.find_last_not_of(ws) - b + 1);
    }
}

bool IsEnabledFor(LogLevel level)
{
    return level >= g_level && level < OFF_LOG_LEVEL;
}

void Write(LogLevel level, const tstring& msg, const char* file, int line)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    TCHAR prefix[64];
    _stprintf_s(prefix, _T("%02u:%02u:%02u.%03u [%lu] %s "), st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds, GetCurrentThreadId(), g_level_names[level]);

    tostringstream out;
    out<<prefix<<msg;
    if (file)
        out<<_T(" (")<<file<<_T(":")<<line<<_T(")");
    out<<_T("\n");
    const tstring s = out.str();

    std::lock_guard<std::mutex> lock(g_mutex);
    OutputDebugString(s.c_str());
    if (g_file)
    {
        _fputts(s.c_str(), g_file);
        fflush(g_file);
    }
}

bool doConfigure(const tstring& configFilename)
{
    FILE* config = NULL;
    if (_tfopen_s(&config, configFilename.c_str(), _T("r")) != 0 || !config)
        return false;

    TCHAR buff[1024];
    while (_fgetts(buff, _countof(buff), config))
    {
        tstring line = Trim(buff);
        size_t eq = line.find(_T('='));
        if (line.empty() || line[0] == _T('#') || eq == tstring::npos)
            continue;
        tstring key = Trim(line.substr(0, eq));
        tstring value = Trim(line.substr(eq + 1));

        if (!_tcsicmp(key.c_str(), _T("level")))
        {
            for (int i = TRACE_LOG_LEVEL; i <= OFF_LOG_LEVEL; i++)
            {
                if (!_tcsicmp(value.c_str(), g_level_names[i]))
                    g_level = static_cast<LogLevel>(i);
            }
        }
        else if (!_tcsicmp(key.c_str(), _T("file")))
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_file)
                fclose(g_file);
            if (_tfopen_s(&g_file, value.c_str(), _T("a, ccs=UTF-8")) != 0)
                g_file = NULL;
        }
    }
    fclose(config);
    return true;
}

} //namespace xy_logger

