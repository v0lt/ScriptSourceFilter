/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

#ifndef __AVISYNTH_7_H__
#include "../Include/avisynth.h"
#endif
#include "Helper.h"

class CAviSynthStream
	: public CSourceStream
	, public CSourceSeeking
{
private:
	CCritSec m_cSharedState;

	HMODULE m_hAviSynthDll = nullptr;

	IScriptEnvironment* m_ScriptEnvironment = nullptr;
	AVSValue            m_AVSValue;
	PVideoFrame         m_Frame;
	const AVS_Linkage*  m_Linkage = nullptr;
	int                 m_Planes[4] = {};

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
	unsigned m_fpsNum = 1;
	unsigned m_fpsDen = 1;

	HRESULT OnThreadStartPlay();
	HRESULT OnThreadCreate();

	void UpdateFromSeek();
	STDMETHODIMP SetRate(double dRate);

	HRESULT ChangeStart();
	HRESULT ChangeStop();
	HRESULT ChangeRate() { return S_OK; }

public:
	CAviSynthStream(const WCHAR* name, CSource* pParent, HRESULT* phr);
	virtual ~CAviSynthStream();

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	HRESULT DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties);
	HRESULT FillBuffer(IMediaSample* pSample);
	HRESULT CheckMediaType(const CMediaType* pMediaType);
	HRESULT SetMediaType(const CMediaType* pMediaType);
	HRESULT GetMediaType(int iPosition, CMediaType* pmt);

	STDMETHODIMP Notify(IBaseFilter* pSender, Quality q);
};
