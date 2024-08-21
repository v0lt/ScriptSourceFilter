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

 //
 // CAviSynthFile
 //

class CAviSynthFile
{
	friend class CAviSynthVideoStream;
	friend class CAviSynthAudioStream;

	HMODULE m_hAviSynthDll = nullptr;

	IScriptEnvironment* m_ScriptEnvironment = nullptr;
	AVSValue            m_AVSValue;
	const AVS_Linkage*  m_Linkage = nullptr;

	std::wstring m_FileInfo;

public:
	CAviSynthFile(const WCHAR* filepath, CSource* pParent, HRESULT* phr);
	~CAviSynthFile();

	std::wstring_view GetInfo() { return m_FileInfo; }
};

//
// CAviSynthVideoStream
//

class CAviSynthVideoStream
	: public CSourceStream
	, public CSourceSeeking
{
private:
	CCritSec m_cSharedState;

	const CAviSynthFile* m_pAviSynthFile;

	BOOL m_bDiscontinuity = FALSE;
	BOOL m_bFlushing = FALSE;

	PVideoFrame m_Frame;
	int         m_Planes[4] = {};

	std::unique_ptr<BYTE[]> m_BitmapError;

	REFERENCE_TIME m_AvgTimePerFrame = 0;
	int m_FrameCounter = 0;
	int m_CurrentFrame = 0;

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

	std::wstring m_StreamInfo;

public:
	CAviSynthVideoStream(CAviSynthFile* pAviSynthFile, CSource* pParent, HRESULT* phr);
	CAviSynthVideoStream(std::wstring_view error_str, CSource* pParent, HRESULT* phr);
	virtual ~CAviSynthVideoStream();

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

	std::wstring_view GetInfo() { return m_StreamInfo; }

private:
	HRESULT OnThreadCreate() override;
	HRESULT OnThreadStartPlay() override;

	void UpdateFromSeek();

	// IMediaSeeking
	STDMETHODIMP SetRate(double dRate) override;

	HRESULT ChangeStart() override;
	HRESULT ChangeStop() override;
	HRESULT ChangeRate() override { return S_OK; }

public:
	HRESULT DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties) override;
	HRESULT FillBuffer(IMediaSample* pSample) override;
	HRESULT CheckMediaType(const CMediaType* pMediaType) override;
	HRESULT SetMediaType(const CMediaType* pMediaType) override;
	HRESULT GetMediaType(int iPosition, CMediaType* pmt) override;

	// IQualityControl
	STDMETHODIMP Notify(IBaseFilter* pSender, Quality q) override { return E_NOTIMPL; }
};

//
// CAviSynthAudioStream
//

class CAviSynthAudioStream
	: public CSourceStream
	, public CSourceSeeking
{
private:
	CCritSec m_cSharedState;

	const CAviSynthFile* m_pAviSynthFile;

	BOOL m_bDiscontinuity = FALSE;
	BOOL m_bFlushing = FALSE;

	GUID m_Subtype = {};
	int m_Channels = 0;
	int m_SampleRate = 0;
	int m_BytesPerSample = 0; // for all audio channels
	int m_BitDepth = 0;
	AvsSampleType m_SampleType = (AvsSampleType)0;

	int m_BufferSamples = 0;

	int64_t m_SampleCounter = 0;
	int64_t m_CurrentSample = 0;
	int64_t m_NumSamples = 0;

	std::wstring m_StreamInfo;

public:
	CAviSynthAudioStream(CAviSynthFile* pAviSynthFile, CSource* pParent, HRESULT* phr);
	virtual ~CAviSynthAudioStream();

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

	std::wstring_view GetInfo() { return m_StreamInfo; }

private:
	HRESULT OnThreadCreate() override;
	HRESULT OnThreadStartPlay() override;
	void UpdateFromSeek();

	// IMediaSeeking
	STDMETHODIMP SetRate(double dRate) override;

	HRESULT ChangeStart() override;
	HRESULT ChangeStop() override;
	HRESULT ChangeRate() override { return S_OK; }

public:
	HRESULT DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties) override;
	HRESULT FillBuffer(IMediaSample* pSample) override;
	HRESULT CheckMediaType(const CMediaType* pMediaType) override;
	HRESULT SetMediaType(const CMediaType* pMediaType) override;
	HRESULT GetMediaType(int iPosition, CMediaType* pmt) override;

	// IQualityControl
	STDMETHODIMP Notify(IBaseFilter* pSender, Quality q) override { return E_NOTIMPL; }
};
