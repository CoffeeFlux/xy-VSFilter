// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#endif
#include "xy_rust_ellipse.h"
#define ASSERT(x) ((void)0)
static uint64_t ellipseHandled=0,ellipseFallback=0;
namespace XyRust {
inline bool ObservedEllipseArc(int* cells,int rx,int ry){bool ok=GenerateEllipseArc(cells,rx,ry);if(ok)++ellipseHandled;else ++ellipseFallback;return ok;}
}
