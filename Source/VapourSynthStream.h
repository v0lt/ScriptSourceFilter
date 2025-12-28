/*
 * Copyright (C) 2020-2025 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

#ifndef VSSCRIPT_H
#include "../Include/VSScript4.h"
#endif
#include "Helper.h"

 //
 // CVapourSynthFile
 //

class CVapourSynthFile
{
	friend class CVapourSynthVideoStream;
	friend class CVapourSynthAudioStream;

	HMODULE m_hVSScriptDll = nullptr;

	const VSAPI* m_vsAPI = nullptr;
	const VSSCRIPTAPI* m_vsScriptAPI = nullptr;
	VSScript* m_vsScript = nullptr;
	VSNode* m_vsNodeVideo = nullptr;
	VSNode* m_vsNodeAudio = nullptr;

	std::wstring m_FileInfo;

	void SetVSNodes();

public:
	CVapourSynthFile(const WCHAR* filepath, CSource* pParent, HRESULT* phr);
	~CVapourSynthFile();

	std::wstring_view GetInfo() { return m_FileInfo; }
};

//
// CVapourSynthVideoStream
//

class CVapourSynthVideoStream
	: public CSourceStream
	, public CSourceSeeking
{
private:
	CCritSec m_cSharedState;

	const CVapourSynthFile* m_pVapourSynthFile;

	const VSVideoInfo* m_vsVideoInfo  = nullptr;
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

	char m_vsErrorMessage[1024] = {};
	std::wstring m_StreamInfo;

public:
	CVapourSynthVideoStream(CVapourSynthFile* pVapourSynthFile, CSource* pParent, HRESULT* phr);
	virtual ~CVapourSynthVideoStream();

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

	void InitVideoMediaType();

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
// CVapourSynthAudioStream
//

class CVapourSynthAudioStream
	: public CSourceStream
	, public CSourceSeeking
{
private:
	CCritSec m_cSharedState;

	const CVapourSynthFile* m_pVapourSynthFile;

	const VSAudioInfo* m_vsAudioInfo = nullptr;

	BOOL m_bDiscontinuity = FALSE;
	BOOL m_bFlushing = FALSE;

	GUID m_Subtype = {};
	int m_Channels = 0;
	int m_SampleRate = 0;
	int m_BytesPerSample = 0; // for all audio channels
	int m_BitDepth = 0;
	VSSampleType m_SampleType = {};

	int m_FrameSamples = 0;
	int m_NumFrames = 0;
	int64_t m_NumSamples = 0;

	int m_FrameCounter = 0;
	int m_CurrentFrame = 0;

	char m_vsErrorMessage[1024] = {};
	std::wstring m_StreamInfo;

public:
	CVapourSynthAudioStream(CVapourSynthFile* pVapourSynthFile, CSource* pParent, HRESULT* phr);
	virtual ~CVapourSynthAudioStream();

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
