#pragma once

// ATL basics used throughout (CString, CRect/CPoint/CSize, the collection
// classes) and the MFC-style debugging macros the code was written with.

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlcoll.h>

#ifndef ASSERT
#define ASSERT(expr) ATLASSERT(expr)
#endif
#ifndef VERIFY
#define VERIFY(expr) ATLVERIFY(expr)
#endif
#ifndef DEBUG_NEW
#define DEBUG_NEW new
#endif
