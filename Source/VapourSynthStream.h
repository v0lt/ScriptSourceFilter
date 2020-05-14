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

#ifndef VSSCRIPT_H
#include "../Include/VSScript.h"
#endif
#include "Helper.h"

class CVapourSynthStream
	: public CSourceStream
	, public CSourceSeeking
{
private:
	CCritSec m_cSharedState;

	HMODULE m_hVSScriptDll = nullptr;

	int          (__stdcall* vs_init)           (void);
	int          (__stdcall* vs_finalize)       (void);
	int          (__stdcall* vs_evaluateScript) (VSScript** handle, const char* script, const char* errorFilename, int flags);
	int          (__stdcall* vs_evaluateFile)   (VSScript** handle, const char* scriptFilename, int flags);
	void         (__stdcall* vs_freeScript)     (VSScript*  handle);
	const char*  (__stdcall* vs_getError)       (VSScript*  handle);
	VSNodeRef*   (__stdcall* vs_getOutput)      (VSScript*  handle, int index);
	void         (__stdcall* vs_clearOutput)    (VSScript*  handle, int index);
	VSCore*      (__stdcall* vs_getCore)        (VSScript*  handle);
	const VSAPI* (__stdcall* vs_getVSApi)       (void);

	int                m_vsInit = 0;
	const VSAPI*       m_vsAPI    = nullptr;
	VSScript*          m_vsScript = nullptr;
	VSNodeRef*         m_vsNode   = nullptr;
	const VSFrameRef*  m_vsFrame  = nullptr;
	const VSVideoInfo* m_vsInfo   = nullptr;
	char               m_vsErrorMessage[1024];
	int                m_Planes[4] = {0, 1, 2, 3};

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
