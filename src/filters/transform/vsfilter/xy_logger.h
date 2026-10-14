/************************************************************************/
/* author: xy                                                           */
/* date: 20110511                                                       */
/************************************************************************/
#ifndef __XY_LOGGER_H_3716A27F_2940_4636_8BC9_C764648362AE__
#define __XY_LOGGER_H_3716A27F_2940_4636_8BC9_C764648362AE__

#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <ostream>
#include <sstream>
#include <string>

namespace xy_logger
{
#ifdef UNICODE
  typedef std::wstring tstring;
  typedef std::wostringstream tostringstream;
#else
  typedef std::string tstring;
  typedef std::ostringstream tostringstream;
#endif

  enum LogLevel
  {
    TRACE_LOG_LEVEL,
    DEBUG_LOG_LEVEL,
    INFO_LOG_LEVEL,
    WARN_LOG_LEVEL,
    ERROR_LOG_LEVEL,
    FATAL_LOG_LEVEL,
    OFF_LOG_LEVEL
  };

  bool IsEnabledFor(LogLevel level);
  void Write(LogLevel level, const tstring& msg, const char* file, int line);

  class TimingLogger
  {
  public:
    TimingLogger(const tstring& msg, const char* file=NULL, int line=-1)
      : m_msg(msg), m_file(file), m_line(line), m_start(0)
    {
      if(IsEnabledFor(TRACE_LOG_LEVEL))
      {
        tostringstream buff;
        m_start = GetTickCount();
        buff<<_T("ENTER at ")<<m_start<<_T(" ms: ")<<m_msg;
        Write(TRACE_LOG_LEVEL, buff.str(), m_file, m_line);
      }
    }

    ~TimingLogger()
    {
      if(IsEnabledFor(TRACE_LOG_LEVEL))
      {
        tostringstream buff;
        DWORD end = GetTickCount();
        buff<<_T("EXIT after ")<<end-m_start<<_T(" at ")<<end<<_T(" ms:  ")<<m_msg;
        Write(TRACE_LOG_LEVEL, buff.str(), m_file, m_line);
      }
    }
  private:
    tstring m_msg;
    const char* m_file;
    int m_line;
    DWORD m_start;
  };
}

#if ENABLE_XY_LOG

#  define XY_LOG_WRITE(level, msg) do {                               \
    if(xy_logger::IsEnabledFor(level)) {                              \
      xy_logger::tostringstream xy_log_buff;                          \
      xy_log_buff<<msg;                                               \
      xy_logger::Write(level, xy_log_buff.str(), __FILE__, __LINE__); \
    }                                                                 \
} while(0)

#  define XY_LOG_VAR_2_STR(var) " "#var"='"<<(var)<<"' "
#  define XY_LOG_TRACE(msg)     XY_LOG_WRITE(xy_logger::TRACE_LOG_LEVEL, __FUNCTION__<<"\t:"<<msg)
#  define XY_LOG_DEBUG(msg)     XY_LOG_WRITE(xy_logger::DEBUG_LOG_LEVEL, __FUNCTION__<<"\t:"<<msg)
#  define XY_LOG_INFO(msg)      XY_LOG_WRITE(xy_logger::INFO_LOG_LEVEL, __FUNCTION__<<"\t:"<<msg)
#  define XY_LOG_WARN(msg)      XY_LOG_WRITE(xy_logger::WARN_LOG_LEVEL, __FUNCTION__<<"\t:"<<msg)
#  define XY_LOG_ERROR(msg)     XY_LOG_WRITE(xy_logger::ERROR_LOG_LEVEL, __FUNCTION__<<"\t:"<<msg)
#  define XY_LOG_FATAL(msg)     XY_LOG_WRITE(xy_logger::FATAL_LOG_LEVEL, __FUNCTION__<<"\t:"<<msg)

#  define CHECK_N_LOG(hr, msg) if(FAILED(hr)) XY_LOG_ERROR(msg)

#  define XY_RAND_VAR2(v,line) v ## line
#  define XY_RAND_VAR(v,line) XY_RAND_VAR2(v,line)
#  define XY_AUTO_TIMING(msg) xy_logger::TimingLogger XY_RAND_VAR(t,__LINE__)( \
    [&]{ xy_logger::tostringstream xy_log_buff; xy_log_buff<<msg; return xy_log_buff.str(); }(), \
    __FILE__, __LINE__)

#  define XY_LOG_ONCE2(msg) {                    \
    static bool entered=false;                   \
    if(!entered) {                               \
      entered=true;                              \
      XY_LOG_WRITE(xy_logger::INFO_LOG_LEVEL, msg); \
    }                                            \
  }

#  define XY_DO_ONCE(expr) do { \
    static bool entered=false;\
    if(!entered) {            \
        entered = true;       \
        {expr;}               \
    }                         \
} while(0)


#  define DO_FOR(num, expr) do {  \
    static int repeat_num=(num);  \
    if (repeat_num>0)             \
    {                             \
        repeat_num--;             \
        expr;                     \
    }                             \
} while(0)

#else //ENABLE_XY_LOG

#  define XY_LOG_VAR_2_STR(var)
#  define XY_LOG_TRACE(msg)
#  define XY_LOG_DEBUG(msg)
#  define XY_LOG_INFO(msg)
#  define XY_LOG_WARN(msg)
#  define XY_LOG_ERROR(msg)
#  define XY_LOG_FATAL(msg)

#  define CHECK_N_LOG(hr, msg)

#  define XY_AUTO_TIMING(msg)
#  define XY_DO_ONCE(expr)
#  define DO_FOR(num, expr)

#endif

std::ostream& operator<<(std::ostream& os, const POINT& obj);
std::wostream& operator<<(std::wostream& os, const POINT& obj);

std::ostream& operator<<(std::ostream& os, const SIZE& obj);
std::wostream& operator<<(std::wostream& os, const SIZE& obj);

std::ostream& operator<<(std::ostream& os, const RECT& obj);
std::wostream& operator<<(std::wostream& os, const RECT& obj);

CString XyUuidToString(const UUID& in_uuid);

namespace xy_logger
{

// Reads a configuration file of "key=value" lines ('#' starts a comment):
//   level=TRACE|DEBUG|INFO|WARN|ERROR|FATAL|OFF   (default: TRACE)
//   file=<path>    also append log lines to this file (UTF-8)
// Log lines always go to OutputDebugString.
bool doConfigure(const tstring& configFilename);

void write_file(const char * filename, const void * buff, int size);

void DumpPackBitmap2File(POINT pos, SIZE size, LPCVOID pixels, int pitch, const char *filename);

void XyDisplayType(LPTSTR label, const AM_MEDIA_TYPE *pmtIn);

void XyDumpGraph(IFilterGraph *pGraph, DWORD dwLevel);

} //namespace xy

#endif // end of __XY_LOGGER_H_3716A27F_2940_4636_8BC9_C764648362AE__
