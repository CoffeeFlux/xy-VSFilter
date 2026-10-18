// SPDX-License-Identifier: GPL-2.0-or-later
// SDK-owned ABI facts for the Windows-only Rust MSXML boundary.
#define CINTERFACE
#include <windows.h>
#include <oleauto.h>
#include <msxml.h>
#include <stddef.h>
#include <stdio.h>
#define FIELD(t,f) printf("\"" #t "." #f "\":%zu,",offsetof(t,f))
#define SIZE(t) printf("\"" #t ".size\":%zu,\"" #t ".align\":%zu,",sizeof(t),alignof(t))
int main() {
    printf("{\"pointer_bits\":%zu,",sizeof(void*)*8);
    SIZE(VARIANT);FIELD(VARIANT,vt);FIELD(VARIANT,bstrVal);FIELD(VARIANT,parray);FIELD(VARIANT,pvRecord);FIELD(VARIANT,pRecInfo);
    SIZE(SAFEARRAY);FIELD(SAFEARRAY,cDims);FIELD(SAFEARRAY,fFeatures);FIELD(SAFEARRAY,cbElements);FIELD(SAFEARRAY,cLocks);FIELD(SAFEARRAY,pvData);FIELD(SAFEARRAY,rgsabound);
    SIZE(SAFEARRAYBOUND);FIELD(SAFEARRAYBOUND,cElements);FIELD(SAFEARRAYBOUND,lLbound);SIZE(GUID);
    SIZE(IXMLDOMNodeVtbl);FIELD(IXMLDOMNodeVtbl,QueryInterface);FIELD(IXMLDOMNodeVtbl,AddRef);FIELD(IXMLDOMNodeVtbl,Release);
    FIELD(IXMLDOMNodeVtbl,get_nodeName);FIELD(IXMLDOMNodeVtbl,get_nodeValue);FIELD(IXMLDOMNodeVtbl,get_firstChild);FIELD(IXMLDOMNodeVtbl,get_nextSibling);FIELD(IXMLDOMNodeVtbl,get_attributes);FIELD(IXMLDOMNodeVtbl,get_text);FIELD(IXMLDOMNodeVtbl,get_xml);
    SIZE(IXMLDOMDocumentVtbl);FIELD(IXMLDOMDocumentVtbl,load);FIELD(IXMLDOMDocumentVtbl,put_async);
    SIZE(IXMLDOMNamedNodeMapVtbl);FIELD(IXMLDOMNamedNodeMapVtbl,nextNode);
    printf("\"complete\":true}\n");return 0;
}
