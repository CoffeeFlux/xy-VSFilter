#include "stdafx.h"
#include "text.h"

DWORD CharSetToCodePage(DWORD dwCharSet)
{
	if(dwCharSet == CP_UTF8) return CP_UTF8;
	if(dwCharSet == CP_UTF7) return CP_UTF7;
	CHARSETINFO cs={0};
	::TranslateCharsetInfo((DWORD *)dwCharSet, &cs, TCI_SRCCHARSET);
	return cs.ciACP;
}

CString ExtractTag(CString tag, CMapStringToString& attribs, bool& fClosing)
{
	tag.Trim();
	attribs.RemoveAll();

	fClosing = !tag.IsEmpty() ? tag[0] == '/' : false;
	tag.TrimLeft('/');

	int i = tag.Find(' ');
	if(i < 0) i = tag.GetLength();
	CString type = tag.Left(i).MakeLower();
	tag = tag.Mid(i).Trim();

	while((i = tag.Find('=')) > 0)
	{
		CString attrib = tag.Left(i).Trim().MakeLower();
		tag = tag.Mid(i+1);
		for(i = 0; i < tag.GetLength() && _istspace(tag[i]); i++);
		tag = i < tag.GetLength() ? tag.Mid(i) : _T("");
		if(!tag.IsEmpty() && tag[0] == '\"') {tag = tag.Mid(1); i = tag.Find('\"');}
		else i = tag.Find(' ');
		if(i < 0) i = tag.GetLength();
		CString param = tag.Left(i).Trim();
		if(!param.IsEmpty())
			attribs[attrib] = param;
		tag = i+1 < tag.GetLength() ? tag.Mid(i+1) : _T("");
	}

	return(type);
}
