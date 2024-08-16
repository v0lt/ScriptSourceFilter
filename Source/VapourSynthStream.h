/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

#ifndef VSSCRIPT_H
#include "../Include/VSScript4.h"
#endif
#include "Helper.h"

class CVapourSynthStream
	: public CSourceStream
	, public CSourceSeeking
{
private:
	CCritSec m_cSharedState;

	HMODULE m_hVSScriptDll = nullptr;

	const VSAPI*       m_vsAPI        = nullptr;
	const VSSCRIPTAPI* m_vsScriptAPI  = nullptr;
	VSScript*          m_vsScript     = nullptr;
	VSNode*            m_vsNode       = nullptr;
	const VSVideoInfo* m_vsVideoInfo  = nullptr;

	const VSFrame*     m_vsFrame  = nullptr;
	
	char               m_vsErrorMessage[1024];
	int                m_Planes[4] = { 0, 1, 2, 3 };

	std::unique_ptr<BYTE[]> m_BitmapError;

	REFERENCE_TIME m_AvgTimePerFrame = 0;
	int m_FrameCounter = 0;
	int m_CurrentFrame = 0;

	BOOL m_bDiscontinuity = FALSE;
	BOOL m_bFlushing = FALSE;

	FmtParams_t m_Format = {};
	UINT m_Width   = 0;
	UINT m_Height  = 0;
	UINT m_Pitch   = 0;
	UINT m_PitchBuff  = 0;
	UINT m_BufferSize = 0;

	UINT m_ColorInfo = 0;
	struct {
		int64_t num = 0;
		int64_t den = 0;
	} m_Sar;

	int m_NumFrames = 0;
	int64_t m_fpsNum = 1;
	int64_t m_fpsDen = 1;

	void VapourSynthFree();

	HRESULT OnThreadStartPlay();
	HRESULT OnThreadCreate();

	void UpdateFromSeek();
	STDMETHODIMP SetRate(double dRate);

	HRESULT ChangeStart();
	HRESULT ChangeStop();
	HRESULT ChangeRate() { return S_OK; }

public:
	CVapourSynthStream(const WCHAR* name, CSource* pParent, HRESULT* phr);
	virtual ~CVapourSynthStream();

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	HRESULT DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties);
	HRESULT FillBuffer(IMediaSample* pSample);
	HRESULT CheckMediaType(const CMediaType* pMediaType);
	HRESULT SetMediaType(const CMediaType* pMediaType);
	HRESULT GetMediaType(int iPosition, CMediaType* pmt);

	STDMETHODIMP Notify(IBaseFilter* pSender, Quality q);
};
