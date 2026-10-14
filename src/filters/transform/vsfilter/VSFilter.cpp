/* 
 *	Copyright (C) 2003-2006 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include "VSFilter.h"

// The DirectShow base classes' DllEntryPoint walks the factory template list;
// this DLL only exports the CSRI API, so the list is empty.
CFactoryTemplate g_Templates[1] = {};
int g_cTemplates = 0;

BEGIN_MESSAGE_MAP(CVSFilterApp, CWinApp)
END_MESSAGE_MAP()

CVSFilterApp::CVSFilterApp()
{
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
#if ENABLE_XY_LOG
    LPTSTR  strDLLPath = DEBUG_NEW TCHAR[_MAX_PATH];
    ::GetModuleFileName( reinterpret_cast<HINSTANCE>(&__ImageBase), strDLLPath, _MAX_PATH);
    CString dllPath = strDLLPath;
    dllPath += ".properties";
    xy_logger::doConfigure( dllPath.GetString() );
    delete [] strDLLPath;
#endif
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL CVSFilterApp::InitInstance()
{
	if(!CWinApp::InitInstance())
		return FALSE;

	DllEntryPoint(AfxGetInstanceHandle(), DLL_PROCESS_ATTACH, 0); // "DllMain" of the dshow baseclasses

	return TRUE;
}

int CVSFilterApp::ExitInstance()
{
	DllEntryPoint(AfxGetInstanceHandle(), DLL_PROCESS_DETACH, 0); // "DllMain" of the dshow baseclasses

	return CWinApp::ExitInstance();
}

CVSFilterApp theApp;
