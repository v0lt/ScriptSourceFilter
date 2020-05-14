/*
* (C) 2020 see Authors.txt
*
* This file is part of MPC-BE.
*
* MPC-BE is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* MPC-BE is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
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

	REFERENCE_TIME m_AvgTimePerFrame = 0;
	REFERENCE_TIME m_rtSampleTime = 0;
	int m_CurrentFrame = 0;

	BOOL m_bDiscontinuity = FALSE;
	BOOL m_bFlushing = FALSE;

	FmtParams_t m_Format = {};
	UINT m_Width  = 0;
	UINT m_Height = 0;
	UINT m_Pitch  = 0;
	UINT m_PitchBuff  = 0;
	UINT m_BufferSize = 0;

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
