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
#include "SubPicQueueImpl.h"
#include "../DSUtil/DSUtil.h"

#include "SubPicProviderExWrapper.h"
#include "SimpleSubPicWrapper.h"

//
// CSubPicQueueImpl
//

CSubPicQueueImpl::CSubPicQueueImpl(ISubPicExAllocator* pAllocator, HRESULT* phr, const int *prefered_colortype/*=NULL*/, int prefered_colortype_num/*=0*/)
	: CUnknown(NAME("CSubPicQueueImpl"), NULL)
	, m_pAllocator(pAllocator)
	, m_rtNow(0)
	, m_rtNowLast(0)
	, m_fps(25.0)
{
	if(phr) {
		*phr = S_OK;
	}

	if(!m_pAllocator) {
		if(phr) {
			*phr = E_FAIL;
		}
		return;
	}
    for (int i=0;i<prefered_colortype_num;i++)
    {
        m_prefered_colortype.AddTail(prefered_colortype[i]);
    }
    if(m_prefered_colortype.IsEmpty())
    {
        m_prefered_colortype.AddTail(MSP_AYUV_PLANAR);
        m_prefered_colortype.AddTail(MSP_AYUV);
        m_prefered_colortype.AddTail(MSP_XY_AUYV);
        m_prefered_colortype.AddTail(MSP_RGBA);
    }
}

CSubPicQueueImpl::~CSubPicQueueImpl()
{
}

STDMETHODIMP CSubPicQueueImpl::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	return
		QI(ISubPicQueue)
        QI(ISimpleSubPicProvider)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicQueue

STDMETHODIMP CSubPicQueueImpl::SetSubPicProvider(ISubPicProvider* pSubPicProvider)
{
    if (pSubPicProvider!=NULL)
    {
        return SetSubPicProviderEx( CSubPicProviderExWrapper::GetSubPicProviderExWrapper(pSubPicProvider) );
    }
    else
    {
        return SetSubPicProviderEx( NULL );
    }
}

STDMETHODIMP CSubPicQueueImpl::GetSubPicProvider(ISubPicProvider** pSubPicProvider)
{
	if(!pSubPicProvider) {
		return E_POINTER;
	}

	CAutoLock cAutoLock(&m_csSubPicProvider);

	if(m_pSubPicProviderEx) {
		*pSubPicProvider = m_pSubPicProviderEx;
		(*pSubPicProvider)->AddRef();
	}

	return !!*pSubPicProvider ? S_OK : E_FAIL;
}

STDMETHODIMP CSubPicQueueImpl::SetFPS(double fps)
{
	m_fps = fps;

	return S_OK;
}

STDMETHODIMP CSubPicQueueImpl::SetTime(REFERENCE_TIME rtNow)
{
	m_rtNow = rtNow;

	return S_OK;
}

// ISubPicQueueEx

STDMETHODIMP CSubPicQueueImpl::SetSubPicProvider( IUnknown* subpic_provider )
{
    if (subpic_provider!=NULL)
    {
        CComQIPtr<ISubPicProviderEx> tmp = subpic_provider;
        if (tmp)
        {
            return SetSubPicProviderEx(tmp);
        }
        else
        {
            CComQIPtr<ISubPicProvider> tmp2 = subpic_provider;
            if (tmp2)
            {
                return SetSubPicProvider(tmp);
            }
        }
        return E_NOTIMPL;
    }
    else
    {
        return SetSubPicProviderEx(NULL);
    }
}

STDMETHODIMP CSubPicQueueImpl::GetSubPicProvider( IUnknown** subpic_provider )
{
    if(!subpic_provider) {
        return E_POINTER;
    }

    CAutoLock cAutoLock(&m_csSubPicProvider);

    if(m_pSubPicProviderEx) {
        *subpic_provider = m_pSubPicProviderEx;
        (*subpic_provider)->AddRef();
    }

    return !!*subpic_provider ? S_OK : E_FAIL;
}

// private

HRESULT CSubPicQueueImpl::SetSubPicProviderEx( ISubPicProviderEx* pSubPicProviderEx )
{
    //	if(m_pSubPicProvider != pSubPicProvider)
    {
        CAutoLock cAutoLock(&m_csSubPicProvider);
        m_pSubPicProviderEx = pSubPicProviderEx;

        if(m_pSubPicProviderEx!=NULL && m_pAllocator!=NULL)
        {
            POSITION pos = m_prefered_colortype.GetHeadPosition();
            while(pos!=NULL)
            {
                int color_type = m_prefered_colortype.GetNext(pos);
                if( m_pSubPicProviderEx->IsColorTypeSupported( color_type ) &&
                    m_pAllocator->IsSpdColorTypeSupported( color_type ) )
                {
                    m_pAllocator->SetSpdColorType(color_type);
                    break;
                }            
            }
        }
    }
    Invalidate();

    return S_OK;
}

HRESULT CSubPicQueueImpl::GetSubPicProviderEx( ISubPicProviderEx** pSubPicProviderEx )
{
    if(!pSubPicProviderEx) {
        return E_POINTER;
    }

    CAutoLock cAutoLock(&m_csSubPicProvider);

    if(m_pSubPicProviderEx) {
        *pSubPicProviderEx = m_pSubPicProviderEx;
        (*pSubPicProviderEx)->AddRef();
    }

    return !!*pSubPicProviderEx ? S_OK : E_FAIL;
}

HRESULT CSubPicQueueImpl::RenderTo(ISubPicEx* pSubPic, REFERENCE_TIME rtStart, REFERENCE_TIME rtStop, double fps, BOOL bIsAnimated)
{
	HRESULT hr = E_FAIL;

	if(!pSubPic) {
		return hr;
	}

	CComPtr<ISubPicProviderEx> pSubPicProviderEx;
	if(FAILED(GetSubPicProviderEx(&pSubPicProviderEx)) || !pSubPicProviderEx) {
		return hr;
    }
	
	if(FAILED(pSubPicProviderEx->Lock())) {
		return hr;
	}

	SubPicDesc spd;
	if(SUCCEEDED(pSubPic->Lock(spd)))
	{
		DWORD color = 0xFF000000;
//		if(spd.type == MSP_YUY2 || spd.type == MSP_YV12 || spd.type == MSP_IYUV)
//		{
//			color = 0xFF801080;
//			//color = 0xFF000000;
//		}
//		else if(spd.type == MSP_AYUV)
//		{
//			color = 0xFF108080;
//		}
//		else
//		{
//			color = 0xFF000000;
//		}
		if(SUCCEEDED(pSubPic->ClearDirtyRect(color)))
		{
			CAtlList<CRect> rectList;

			hr = pSubPicProviderEx->RenderEx(spd, bIsAnimated ? rtStart : ((rtStart+rtStop)/2), fps, rectList);

			DbgLog((LOG_TRACE, 3, "CSubPicQueueImpl::RenderTo => GetStartPosition"));
			POSITION pos = pSubPicProviderEx->GetStartPosition(rtStart, fps);

			DbgLog((LOG_TRACE, 3, "CSubPicQueueImpl::RenderTo => GetStartStop rtStart:%lu", (ULONG)rtStart/10000));
			pSubPicProviderEx->GetStartStop(pos, fps, rtStart, rtStop);
			DbgLog((LOG_TRACE, 3, "rtStart:%lu", (ULONG)rtStart/10000));

			//DbgLog((LOG_TRACE, 3, TEXT("rtStart=%lu rtStop=%lu pos:%x fps:%f"), (ULONG)rtStart/10000, (ULONG)rtStop/10000, pos, fps));

			pSubPic->SetStart(rtStart);
			pSubPic->SetStop(rtStop);

			pSubPic->UnlockEx(&rectList);
		}
	}

	pSubPicProviderEx->Unlock();

	return hr;
}

//
// CSubPicQueueNoThread
//

CSubPicQueueNoThread::CSubPicQueueNoThread(ISubPicExAllocator* pAllocator, HRESULT* phr, const int *prefered_colortype/*=NULL*/, int prefered_colortype_num/*=0*/)
	: CSubPicQueueImpl(pAllocator, phr, prefered_colortype, prefered_colortype_num)
{
}

CSubPicQueueNoThread::~CSubPicQueueNoThread()
{
}

// ISubPicQueue

STDMETHODIMP CSubPicQueueNoThread::Invalidate(REFERENCE_TIME rtInvalidate)
{
	CAutoLock cQueueLock(&m_csLock);
    
    if( m_pSubPic && m_pSubPic->GetStop() >= rtInvalidate)
    {
	    m_pSubPic = NULL;
    }
	return S_OK;
}

STDMETHODIMP_(bool) CSubPicQueueNoThread::LookupSubPic(REFERENCE_TIME rtNow, ISubPic** ppSubPic)
{
    if(ppSubPic!=NULL)
    {
        ISubPicEx* temp;
        bool result = LookupSubPicEx(rtNow, &temp);
        *ppSubPic = temp;
        return result;
    }
    else
    {
        return LookupSubPicEx(rtNow, NULL);
    }
}

STDMETHODIMP_(bool) CSubPicQueueNoThread::LookupSubPic( REFERENCE_TIME now /*[in]*/, ISimpleSubPic** output_subpic /*[out]*/ )
{    
    if(output_subpic!=NULL)
    {
        CComPtr<ISubPicEx> temp;
        bool result = LookupSubPicEx(now, &temp);
        (*output_subpic = DEBUG_NEW SimpleSubPicWrapper(temp))->AddRef();
        return result;
    }
    else
    {
        return LookupSubPicEx(now, NULL);
    }
}


bool CSubPicQueueNoThread::LookupSubPicEx(REFERENCE_TIME rtNow, ISubPicEx** ppSubPic)
{
    if(!ppSubPic)
        return(false);
    
    *ppSubPic = NULL;

    CComPtr<ISubPicEx> pSubPic;

    {
        CAutoLock cAutoLock(&m_csLock);

        if(!m_pSubPic)
        {
            if(FAILED(m_pAllocator->AllocDynamicEx(&m_pSubPic))) {
                return false;
            }
        }

        pSubPic = m_pSubPic;
    }

    if(pSubPic->GetStart() <= rtNow && rtNow < pSubPic->GetStop())
    {
        (*ppSubPic = pSubPic)->AddRef();
    }
    else
    {
        CComPtr<ISubPicProvider> pSubPicProvider;
        if(SUCCEEDED(GetSubPicProvider(&pSubPicProvider)) && pSubPicProvider
            && SUCCEEDED(pSubPicProvider->Lock()))
        {
            double fps = m_fps;

            if(POSITION pos = pSubPicProvider->GetStartPosition(rtNow, fps))
            {
                REFERENCE_TIME rtStart = pSubPicProvider->GetStart(pos, fps);
                REFERENCE_TIME rtStop = pSubPicProvider->GetStop(pos, fps);

                if(rtStart <= rtNow && rtNow < rtStop)
                {
                    SIZE	MaxTextureSize, VirtualSize;
                    POINT	VirtualTopLeft;
                    HRESULT	hr2;
                    if (SUCCEEDED (hr2 = pSubPicProvider->GetTextureSize(pos, MaxTextureSize, VirtualSize, VirtualTopLeft))) {
                        m_pAllocator->SetMaxTextureSize(MaxTextureSize);
                    }
                    if(m_pAllocator->IsDynamicWriteOnly())
                    {
                        CComPtr<ISubPicEx> pStatic;
                        if(SUCCEEDED(m_pAllocator->GetStaticEx(&pStatic))
                            && SUCCEEDED(RenderTo(pStatic, rtNow, rtNow+1, fps, true))
                            && SUCCEEDED(pStatic->CopyTo(pSubPic)))
                            (*ppSubPic = pSubPic)->AddRef();
                    }
                    else
                    {
                        if(SUCCEEDED(RenderTo(m_pSubPic, rtNow, rtNow+1, fps, true)))
                            (*ppSubPic = pSubPic)->AddRef();
                    }
                }
            }

            pSubPicProvider->Unlock();

            if(*ppSubPic)
            {
                CAutoLock cAutoLock(&m_csLock);

                m_pSubPic = *ppSubPic;
            }
        }
    }
    return(!!*ppSubPic);
}

STDMETHODIMP CSubPicQueueNoThread::GetStats(int& nSubPics, REFERENCE_TIME& rtNow, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	CAutoLock cAutoLock(&m_csLock);

	nSubPics = 0;
	rtNow = m_rtNow;
	rtStart = rtStop = 0;

	if(m_pSubPic)
	{
		nSubPics = 1;
		rtStart = m_pSubPic->GetStart();
		rtStop = m_pSubPic->GetStop();
	}

	return S_OK;
}

STDMETHODIMP CSubPicQueueNoThread::GetStats(int nSubPic, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	CAutoLock cAutoLock(&m_csLock);

	if(!m_pSubPic || nSubPic != 0)
		return E_INVALIDARG;

	rtStart = m_pSubPic->GetStart();
	rtStop = m_pSubPic->GetStop();

	return S_OK;
}
