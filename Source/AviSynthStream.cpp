/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "stdafx.h"
#include "VUIOptions.h"
#include "ScriptSource.h"

#include "AviSynthStream.h"

#include <mmreg.h>

const AVS_Linkage* AVS_linkage = NULL;

//
// CAviSynthFile
//

CAviSynthFile::CAviSynthFile(const WCHAR* name, CSource* pParent, HRESULT* phr)
{
	try {
		m_hAviSynthDll = LoadLibraryW(L"Avisynth.dll");
		if (!m_hAviSynthDll) {
			throw std::exception("Failed to load AviSynth+");
		}

		IScriptEnvironment* (WINAPI* CreateScriptEnvironment)(int version) =
			(IScriptEnvironment * (WINAPI*)(int)) GetProcAddress(m_hAviSynthDll, "CreateScriptEnvironment");

		if (!CreateScriptEnvironment) {
			throw std::exception("Cannot resolve AviSynth+ CreateScriptEnvironment function");
		}

		m_ScriptEnvironment = CreateScriptEnvironment(6);
		if (!m_ScriptEnvironment) {
			throw std::exception("A newer AviSynth+ version is required");
		}

		AVS_linkage = m_Linkage = m_ScriptEnvironment->GetAVSLinkage();
	}
	catch (const std::exception& e) {
		DLog(A2WStr(e.what()));
		*phr = E_FAIL;
		return;
	}

	HRESULT hr;
	std::wstring error;

	try {
		std::string utf8file = ConvertWideToUtf8(name);
		AVSValue args[2] = { utf8file.c_str(), true };
		const char* const arg_names[2] = { 0, "utf8" };
		try {
			m_AVSValue = m_ScriptEnvironment->Invoke("Import", AVSValue(args, 2), arg_names);
		}
		catch (const AvisynthError& e) {
			error = ConvertUtf8ToWide(e.msg);
			throw std::exception("Failure to open Avisynth script file.");
		}

		if (!m_AVSValue.IsClip()) {
			throw std::exception("AviSynth+ script does not return a video clip");
		}

		auto Clip = m_AVSValue.AsClip();
		auto VInfo = Clip->GetVideoInfo();

		if (VInfo.HasVideo()) {
			auto& Format = GetFormatParamsAviSynth(VInfo.pixel_type);
			if (Format.fourcc == DWORD(-1)) {
				throw std::exception(std::format("Unsuported pixel_type {:#010x} ({})", (uint32_t)VInfo.pixel_type, VInfo.pixel_type).c_str());
			}

			auto pVideoStream = new CAviSynthVideoStream(this, pParent, &hr);
			if (FAILED(hr)) {
				pParent->RemovePin(pVideoStream);
				delete pVideoStream;
				throw std::exception("AviSynth+ script returned unsupported video");
			}
			else {
				m_FileInfo.append(pVideoStream->GetInfo());
				m_FileInfo += (L'\n');
			}
		}
		
		if (VInfo.HasAudio()) {
			auto pAudioStream = new CAviSynthAudioStream(this, pParent, &hr);
			if (FAILED(hr)) {
				DLog(L"AviSynth+ script returned unsupported audio");
			}
			else {
				m_FileInfo.append(pAudioStream->GetInfo());
				m_FileInfo += (L'\n');
			}
		}

		hr = S_OK;
	}
	catch (const std::exception& e) {
		DLog(L"{}\n{}", A2WStr(e.what()), error);

		new CAviSynthVideoStream(error, pParent, &hr);
		if (SUCCEEDED(hr)) {
			hr = S_FALSE;
		} else {
			hr = E_FAIL;
		}
	}

	*phr = hr;
}

CAviSynthFile::~CAviSynthFile()
{
	AVS_linkage = m_Linkage;

	m_AVSValue = 0;

	if (m_ScriptEnvironment) {
		m_ScriptEnvironment->DeleteScriptEnvironment();
		m_ScriptEnvironment = nullptr;
	}

	AVS_linkage = nullptr;
	m_Linkage = nullptr;

	if (m_hAviSynthDll) {
		FreeLibrary(m_hAviSynthDll);
	}
}

//
// CAviSynthVideoStream
//

CAviSynthVideoStream::CAviSynthVideoStream(CAviSynthFile* pAviSynthFile, CSource* pParent, HRESULT* phr)
	: CSourceStream(L"Video", phr, pParent, L"Video")
	, CSourceSeeking(L"Video", (IPin*)this, phr, &m_cSharedState)
	, m_pAviSynthFile(pAviSynthFile)
{
	CAutoLock cAutoLock(&m_cSharedState);

	HRESULT hr;
	std::wstring error;

	try {
		auto Clip = m_pAviSynthFile->m_AVSValue.AsClip();
		auto VInfo = Clip->GetVideoInfo();

		m_Format = GetFormatParamsAviSynth(VInfo.pixel_type);
		if (m_Format.fourcc == DWORD(-1)) {
			throw std::exception(std::format("Unsuported pixel_type {:#010x} ({})", (uint32_t)VInfo.pixel_type, VInfo.pixel_type).c_str());
		}

		UINT bitdepth = VInfo.BitsPerPixel();

		auto VFrame = Clip->GetFrame(0, m_pAviSynthFile->m_ScriptEnvironment);
		m_Pitch = VFrame->GetPitch();

		m_Width = VInfo.width;
		m_Height = VInfo.height;
		m_PitchBuff = m_Pitch;
		m_BufferSize = m_PitchBuff * m_Height * m_Format.buffCoeff / 2;

		m_fpsNum = VInfo.fps_numerator;
		m_fpsDen = VInfo.fps_denominator;
		m_NumFrames = VInfo.num_frames;
		m_AvgTimePerFrame = UNITS * m_fpsDen / m_fpsNum; // no need any MulDiv here
		m_rtDuration = m_rtStop = llMulDiv(UNITS * m_NumFrames, m_fpsDen, m_fpsNum, 0);

		if (VInfo.IsPlanar()) {
			if (VInfo.IsYUV()) {
				m_Planes[0] = PLANAR_Y;
				if (VInfo.IsVPlaneFirst()) {
					m_Planes[1] = PLANAR_U; // Yes, that's right, because the output is YV12, YV16, YV24.
					m_Planes[2] = PLANAR_V;
				}
				else {
					m_Planes[1] = PLANAR_V;
					m_Planes[2] = PLANAR_U;
				}
			}
			else if (VInfo.IsRGB()) {
				m_Planes[0] = PLANAR_G;
				m_Planes[1] = PLANAR_B;
				m_Planes[2] = PLANAR_R;
			}
			m_Planes[3] = PLANAR_A;
		}

		UINT color_info = 0;

		m_StreamInfo = std::format(
			L"Script type : AviSynth\n"
			L"Video stream: {} {}x{} {:.3f} fps",
			m_Format.str, m_Width, m_Height, (double)m_fpsNum/m_fpsDen
		);

		bool has_at_least_v9 = true;
		try {
			m_pAviSynthFile->m_ScriptEnvironment->CheckVersion(9);
		}
		catch (const AvisynthError&) {
			has_at_least_v9 = false;
		}

		if (has_at_least_v9) {
			auto& avsMap = VFrame->getConstProperties();
			int numKeys = m_pAviSynthFile->m_ScriptEnvironment->propNumKeys(&avsMap);
			if (numKeys > 0) {
				m_StreamInfo += std::format(L"\nProperties [{}]:", numKeys);
			}

			for (int i = 0; i < numKeys; i++) {
				const char* keyName = m_pAviSynthFile->m_ScriptEnvironment->propGetKey(&avsMap, i);
				if (keyName) {
					int64_t val_Int = 0;
					double val_Float = 0;
					const char* val_Data = 0;
					int err = 0;
					const char keyType = m_pAviSynthFile->m_ScriptEnvironment->propGetType(&avsMap, keyName);

					m_StreamInfo += std::format(L"\n{:>2}: <{}> '{}'", i, keyType, A2WStr(keyName));

					switch (keyType) {
					case PROPTYPE_INT:
						val_Int = m_pAviSynthFile->m_ScriptEnvironment->propGetInt(&avsMap, keyName, 0, &err);
						if (!err) {
							m_StreamInfo += std::format(L" = {}", val_Int);
							if (strcmp(keyName, "_SARNum") == 0) {
								m_Sar.num = val_Int;
							}
							else if (strcmp(keyName, "_SARDen") == 0) {
								m_Sar.den = val_Int;
							}
							else {
								SetColorInfoFromFrameFrops(color_info, keyName, val_Int);
							}
						}
						break;
					case PROPTYPE_FLOAT:
						val_Float = m_pAviSynthFile->m_ScriptEnvironment->propGetFloat(&avsMap, keyName, 0, &err);
						if (!err) {
							m_StreamInfo += std::format(L" = {:.3f}", val_Float);
						}
						break;
					case PROPTYPE_DATA:
						val_Data = m_pAviSynthFile->m_ScriptEnvironment->propGetData(&avsMap, keyName, 0, &err);
						if (!err) {
							const int dataSize = m_pAviSynthFile->m_ScriptEnvironment->propGetDataSize(&avsMap, keyName, 0, &err);
							if (!err) {
								if (dataSize == 1 && strcmp(keyName, "_PictType") == 0) {
									m_StreamInfo += std::format(L" = {}", val_Data[0]);
								} else {
									m_StreamInfo += std::format(L", {} bytes", dataSize);
								}
							}
						}
						break;
					}
				}
			}
		}

		if (color_info) {
			m_ColorInfo = color_info | (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT);
		}

		InitVideoMediaType();

		DLog(m_StreamInfo);

		hr = S_OK;
	}
	catch (const std::exception& e) {
		DLog(L"{}\n{}", A2WStr(e.what()), error);

		hr = E_FAIL;
	}

	*phr = hr;
}

CAviSynthVideoStream::CAviSynthVideoStream(std::wstring_view error_str, CSource* pParent, HRESULT* phr)
	: CSourceStream(L"Video", phr, pParent, L"Video")
	, CSourceSeeking(L"Video", (IPin*)this, phr, &m_cSharedState)
	, m_pAviSynthFile(nullptr)
{
	m_Format = GetFormatParamsAviSynth(VideoInfo::CS_BGR32);

	m_Width = 640;
	m_Height = 360;
	m_Pitch = m_Width * 4;
	m_PitchBuff = m_Pitch;
	m_BufferSize = m_PitchBuff * m_Height * m_Format.buffCoeff / 2;
	m_Sar = {};

	m_fpsNum = 1;
	m_fpsDen = 1;
	m_NumFrames = 10;
	m_AvgTimePerFrame = UNITS;
	m_rtDuration = m_rtStop = UNITS * m_NumFrames;

	std::wstring str(error_str);
	m_BitmapError = GetBitmapWithText(str, m_Width, m_Height);

	if (m_BitmapError) {
		*phr = S_FALSE;
		InitVideoMediaType();
	} else {
		*phr = E_FAIL;
	}
}

CAviSynthVideoStream::~CAviSynthVideoStream()
{
}

STDMETHODIMP CAviSynthVideoStream::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return (riid == IID_IMediaSeeking) ? CSourceSeeking::NonDelegatingQueryInterface(riid, ppv)
		: CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CAviSynthVideoStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_FrameCounter = 0;
	m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down

	return CSourceStream::OnThreadCreate();
}

HRESULT CAviSynthVideoStream::OnThreadStartPlay()
{
	m_bDiscontinuity = TRUE;
	return DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
}

void CAviSynthVideoStream::UpdateFromSeek()
{
	if (ThreadExists()) {
		// next time around the loop, the worker thread will
		// pick up the position change.
		// We need to flush all the existing data - we must do that here
		// as our thread will probably be blocked in GetBuffer otherwise

		m_bFlushing = TRUE;

		DeliverBeginFlush();
		// make sure we have stopped pushing
		Stop();
		// complete the flush
		DeliverEndFlush();

		m_bFlushing = FALSE;

		// restart
		Run();
	}
}

HRESULT CAviSynthVideoStream::SetRate(double dRate)
{
	if (dRate <= 0) {
		return E_INVALIDARG;
	}

	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_dRateSeeking = dRate;
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthVideoStream::ChangeStart()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_FrameCounter = 0;
		m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthVideoStream::ChangeStop()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		if (m_CurrentFrame < m_NumFrames) {
			return S_OK;
		}
	}

	// We're already past the new stop time -- better flush the graph.
	UpdateFromSeek();

	return S_OK;
}

void CAviSynthVideoStream::InitVideoMediaType()
{
	m_mt.InitMediaType();
	m_mt.SetType(&MEDIATYPE_Video);
	m_mt.SetSubtype(&m_Format.subtype);
	m_mt.SetFormatType(&FORMAT_VideoInfo2);
	m_mt.SetTemporalCompression(FALSE);
	m_mt.SetSampleSize(m_BufferSize);

	VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2));
	ZeroMemory(vih2, sizeof(VIDEOINFOHEADER2));
	vih2->rcSource = { 0, 0, (long)m_Width, (long)m_Height };
	vih2->rcTarget = vih2->rcSource;
	vih2->AvgTimePerFrame         = m_AvgTimePerFrame;
	vih2->bmiHeader.biSize        = sizeof(vih2->bmiHeader);
	vih2->bmiHeader.biWidth       = m_PitchBuff / m_Format.Packsize;
	vih2->bmiHeader.biHeight      = (m_Format.fourcc == BI_RGB) ? -(long)m_Height : m_Height;
	vih2->bmiHeader.biPlanes      = 1;
	vih2->bmiHeader.biBitCount    = m_Format.bitCount;
	vih2->bmiHeader.biCompression = m_Format.fourcc;
	vih2->bmiHeader.biSizeImage   = m_BufferSize;

	vih2->dwControlFlags = m_ColorInfo;

	if (m_Sar.num > 0 && m_Sar.den > 0 && m_Sar.num < INT16_MAX && m_Sar.den < INT16_MAX) {
		auto parX = m_Sar.num * m_Width;
		auto parY = m_Sar.den * m_Height;
		const auto gcd = std::gcd(parX, parY);
		parX /= gcd;
		parY /= gcd;
		vih2->dwPictAspectRatioX = parX;
		vih2->dwPictAspectRatioY = parY;
	}
}

HRESULT CAviSynthVideoStream::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
{
	//CAutoLock cAutoLock(m_pFilter->pStateLock());

	ASSERT(pAlloc);
	ASSERT(pProperties);

	HRESULT hr = NOERROR;

	pProperties->cBuffers = 1;
	pProperties->cbBuffer = m_BufferSize;

	ALLOCATOR_PROPERTIES Actual;
	if (FAILED(hr = pAlloc->SetProperties(pProperties, &Actual))) {
		return hr;
	}

	if (Actual.cbBuffer < pProperties->cbBuffer) {
		return E_FAIL;
	}
	ASSERT(Actual.cBuffers == pProperties->cBuffers);

	return NOERROR;
}

HRESULT CAviSynthVideoStream::FillBuffer(IMediaSample* pSample)
{
	{
		CAutoLock cAutoLockShared(&m_cSharedState);

		if (m_CurrentFrame >= m_NumFrames) {
			return S_FALSE;
		}

		AM_MEDIA_TYPE* pmt;
		if (SUCCEEDED(pSample->GetMediaType(&pmt)) && pmt) {
			CMediaType mt(*pmt);
			SetMediaType(&mt);
			DeleteMediaType(pmt);
		}

		if (m_mt.formattype != FORMAT_VideoInfo2) {
			return S_FALSE;
		}

		BYTE* dst_data = nullptr;
		HRESULT hr = pSample->GetPointer(&dst_data);
		if (FAILED(hr) || !dst_data) {
			return S_FALSE;
		}

		long buffSize = pSample->GetSize();
		if (buffSize < (long)m_BufferSize) {
			return S_FALSE;
		}

		UINT DataLength = 0;

		if (m_BitmapError) {
			DataLength = m_PitchBuff * m_Height;

			const BYTE* src_data = m_BitmapError.get();
			if (m_Pitch == m_PitchBuff) {
				memcpy(dst_data, src_data, DataLength);
			}
			else {
				UINT linesize = std::min(m_Pitch, m_PitchBuff);
				for (UINT y = 0; y < m_Height; y++) {
					memcpy(dst_data, src_data, linesize);
					src_data += m_Pitch;
					dst_data += m_PitchBuff;
				}
			}
		}
		else {
			auto Clip = m_pAviSynthFile->m_AVSValue.AsClip();
			auto VFrame = Clip->GetFrame(m_CurrentFrame, m_pAviSynthFile->m_ScriptEnvironment);

			const int num_planes = m_Format.planes;
			for (int i = 0; i < num_planes; i++) {
				const int plane = m_Planes[i];
				const BYTE* src_data = VFrame->GetReadPtr(plane);
				int src_pitch = VFrame->GetPitch(plane);
				const UINT height = VFrame->GetHeight(plane);
				UINT dst_pitch = m_PitchBuff;
				if (i > 0 && (m_Format.ASformat&VideoInfo::CS_Sub_Width_Mask) == VideoInfo::CS_Sub_Width_2) {
					dst_pitch /= 2;
				}

				if (m_Format.fourcc == BI_RGB) {
					src_data += src_pitch * (height - 1);
					src_pitch = -src_pitch;
				}

				if (src_pitch == dst_pitch) {
					memcpy(dst_data, src_data, dst_pitch * height);
					dst_data += dst_pitch * height;
				}
				else {
					UINT linesize = std::min((UINT)abs(src_pitch), dst_pitch);
					for (UINT y = 0; y < height; y++) {
						memcpy(dst_data, src_data, linesize);
						src_data += src_pitch;
						dst_data += dst_pitch;
					}
				}
				DataLength += dst_pitch * height;
			}
		}

		pSample->SetActualDataLength(DataLength);

		// Sample time
		REFERENCE_TIME rtStart = llMulDiv(UNITS * m_FrameCounter, m_fpsDen, m_fpsNum, 0);
		REFERENCE_TIME rtStop  = llMulDiv(UNITS * (m_FrameCounter+1), m_fpsDen, m_fpsNum, 0);
		// The sample times are modified by the current rate.
		if (m_dRateSeeking != 1.0) {
			rtStart = static_cast<REFERENCE_TIME>(rtStart / m_dRateSeeking);
			rtStop  = static_cast<REFERENCE_TIME>(rtStop / m_dRateSeeking);
		}
		pSample->SetTime(&rtStart, &rtStop);

		m_FrameCounter++;
		m_CurrentFrame++;
	}

	pSample->SetSyncPoint(TRUE);

	if (m_bDiscontinuity) {
		pSample->SetDiscontinuity(TRUE);
		m_bDiscontinuity = FALSE;
	}

	return S_OK;
}

HRESULT CAviSynthVideoStream::CheckMediaType(const CMediaType* pmt)
{
	if (pmt->majortype == MEDIATYPE_Video
		&& pmt->subtype == m_Format.subtype
		&& pmt->formattype == FORMAT_VideoInfo2) {

		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->Format();
		if (vih2->bmiHeader.biWidth >= (long)m_Width && abs(vih2->bmiHeader.biHeight) == (long)m_Height) {
			return S_OK;
		}
	}

	return E_INVALIDARG;
}

HRESULT CAviSynthVideoStream::SetMediaType(const CMediaType* pMediaType)
{
	HRESULT hr = __super::SetMediaType(pMediaType);

	if (SUCCEEDED(hr)) {
		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pMediaType->Format();
		m_PitchBuff = m_Format.Packsize * vih2->bmiHeader.biWidth;
		ASSERT(m_PitchBuff >= m_Pitch);
		m_BufferSize = m_PitchBuff * abs(vih2->bmiHeader.biHeight) * m_Format.buffCoeff / 2;

		DLog(L"SetMediaType with subtype {}", GUIDtoWString(m_mt.subtype));
	}

	return hr;
}

HRESULT CAviSynthVideoStream::GetMediaType(int iPosition, CMediaType* pmt)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	if (iPosition < 0) {
		return E_INVALIDARG;
	}
	if (iPosition >= 1) {
		return VFW_S_NO_MORE_ITEMS;
	}

	*pmt = m_mt;

	return S_OK;
}

//
// CAviSynthAudioStream
//

CAviSynthAudioStream::CAviSynthAudioStream(CAviSynthFile* pAviSynthFile, CSource* pParent, HRESULT* phr)
	: CSourceStream(L"Audio", phr, pParent, L"Audio")
	, CSourceSeeking(L"Audio", (IPin*)this, phr, &m_cSharedState)
	, m_pAviSynthFile(pAviSynthFile)
{
	CAutoLock cAutoLock(&m_cSharedState);

	HRESULT hr;
	std::wstring error;

	try {
		auto Clip = m_pAviSynthFile->m_AVSValue.AsClip();
		auto VInfo = Clip->GetVideoInfo();
	
		if (VInfo.HasAudio()) {
			m_Channels       = VInfo.AudioChannels();
			m_SampleRate     = VInfo.SamplesPerSecond();
			m_BytesPerSample = VInfo.BytesPerAudioSample();
			m_BitDepth       = m_BytesPerSample * 8 / m_Channels;
			m_SampleType     = (AvsSampleType)VInfo.SampleType();
			m_NumSamples     = VInfo.num_audio_samples;

			WORD wFormatTag;
			if (m_SampleType == SAMPLE_FLOAT) {
				wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
				m_Subtype = MEDIASUBTYPE_IEEE_FLOAT;
			} else {
				wFormatTag = WAVE_FORMAT_PCM;
				m_Subtype = MEDIASUBTYPE_PCM;
			}

			m_BufferSamples = m_SampleRate / 5; // 5 for 200 ms; 20 for 50 ms

			m_rtDuration = m_rtStop = llMulDiv(m_NumSamples, UNITS, m_SampleRate, 0);

			m_mt.InitMediaType();
			m_mt.SetType(&MEDIATYPE_Audio);
			m_mt.SetSubtype(&m_Subtype);
			m_mt.SetFormatType(&FORMAT_WaveFormatEx);
			m_mt.SetTemporalCompression(FALSE);
			m_mt.SetSampleSize(m_BytesPerSample);

			bool has_at_least_v10 = true;
			try {
				m_pAviSynthFile->m_ScriptEnvironment->CheckVersion(10);
			}
			catch (const AvisynthError&) {
				has_at_least_v10 = false;
			}

			UINT channelLayout = has_at_least_v10 ? VInfo.GetChannelMask() : 0;

			if (channelLayout) {
				WAVEFORMATEXTENSIBLE* wfex = (WAVEFORMATEXTENSIBLE*)m_mt.AllocFormatBuffer(sizeof(WAVEFORMATEXTENSIBLE));
				wfex->Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
				wfex->Format.nChannels            = m_Channels;
				wfex->Format.nSamplesPerSec       = m_SampleRate;
				wfex->Format.nAvgBytesPerSec      = m_BytesPerSample * m_SampleRate;
				wfex->Format.nBlockAlign          = m_BytesPerSample;
				wfex->Format.wBitsPerSample       = m_BitDepth;
				wfex->Format.cbSize               = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX); // 22
				wfex->Samples.wValidBitsPerSample = m_BitDepth;
				wfex->dwChannelMask               = channelLayout;
				wfex->SubFormat                   = m_Subtype;
			}
			else {
				WAVEFORMATEX* wfe = (WAVEFORMATEX*)m_mt.AllocFormatBuffer(sizeof(WAVEFORMATEX));
				wfe->wFormatTag      = wFormatTag;
				wfe->nChannels       = m_Channels;
				wfe->nSamplesPerSec  = m_SampleRate;
				wfe->nAvgBytesPerSec = m_BytesPerSample * m_SampleRate;
				wfe->nBlockAlign     = m_BytesPerSample;
				wfe->wBitsPerSample  = m_BitDepth;
				wfe->cbSize          = 0;
			}

			m_StreamInfo = std::format(L"Audio stream: {} channels, {} Hz, ", m_Channels, m_SampleRate);
			switch (m_SampleType) {
			case SAMPLE_INT8:  m_StreamInfo.append(L"int8"); break;
			case SAMPLE_INT16: m_StreamInfo.append(L"int16"); break;
			case SAMPLE_INT24: m_StreamInfo.append(L"int24"); break;
			case SAMPLE_INT32: m_StreamInfo.append(L"int32"); break;
			case SAMPLE_FLOAT: m_StreamInfo.append(L"float32"); break;
			}
			DLog(m_StreamInfo);

			hr = S_OK;
		}
	}
	catch (const std::exception& e) {
		DLog(L"{}\n{}", A2WStr(e.what()), error);

		hr = E_FAIL;
	}

	*phr = hr;
}

CAviSynthAudioStream::~CAviSynthAudioStream()
{
}

STDMETHODIMP CAviSynthAudioStream::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return (riid == IID_IMediaSeeking) ? CSourceSeeking::NonDelegatingQueryInterface(riid, ppv)
		: CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CAviSynthAudioStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_SampleCounter = 0;
	m_CurrentSample = (int)llMulDiv(m_rtStart, m_SampleRate, UNITS, 0); // round down

	return CSourceStream::OnThreadCreate();
}

HRESULT CAviSynthAudioStream::OnThreadStartPlay()
{
	m_bDiscontinuity = TRUE;
	return DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
}

void CAviSynthAudioStream::UpdateFromSeek()
{
	if (ThreadExists()) {
		// next time around the loop, the worker thread will
		// pick up the position change.
		// We need to flush all the existing data - we must do that here
		// as our thread will probably be blocked in GetBuffer otherwise

		m_bFlushing = TRUE;

		DeliverBeginFlush();
		// make sure we have stopped pushing
		Stop();
		// complete the flush
		DeliverEndFlush();

		m_bFlushing = FALSE;

		// restart
		Run();
	}
}

HRESULT CAviSynthAudioStream::SetRate(double dRate)
{
	if (dRate <= 0) {
		return E_INVALIDARG;
	}

	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_dRateSeeking = dRate;
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthAudioStream::ChangeStart()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_SampleCounter = 0;
		m_CurrentSample = (int)llMulDiv(m_rtStart, m_SampleRate, UNITS, 0); // round down
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthAudioStream::ChangeStop()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		if (m_CurrentSample < m_NumSamples) {
			return S_OK;
		}
	}

	// We're already past the new stop time -- better flush the graph.
	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthAudioStream::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
{
	//CAutoLock cAutoLock(m_pFilter->pStateLock());

	ASSERT(pAlloc);
	ASSERT(pProperties);

	HRESULT hr = NOERROR;

	pProperties->cBuffers = 1;
	pProperties->cbBuffer = m_BufferSamples * m_BytesPerSample;

	ALLOCATOR_PROPERTIES Actual;
	if (FAILED(hr = pAlloc->SetProperties(pProperties, &Actual))) {
		return hr;
	}

	if (Actual.cbBuffer < pProperties->cbBuffer) {
		return E_FAIL;
	}
	ASSERT(Actual.cBuffers == pProperties->cBuffers);

	return NOERROR;
}

HRESULT CAviSynthAudioStream::FillBuffer(IMediaSample* pSample)
{
	{
		CAutoLock cAutoLockShared(&m_cSharedState);

		if (m_CurrentSample >= m_NumSamples) {
			return S_FALSE;
		}

		AM_MEDIA_TYPE* pmt;
		if (SUCCEEDED(pSample->GetMediaType(&pmt)) && pmt) {
			CMediaType mt(*pmt);
			SetMediaType(&mt);
			DeleteMediaType(pmt);
		}

		if (m_mt.formattype != FORMAT_WaveFormatEx) {
			return S_FALSE;
		}

		BYTE* dst_data = nullptr;
		HRESULT hr = pSample->GetPointer(&dst_data);
		if (FAILED(hr) || !dst_data) {
			return S_FALSE;
		}

		long buffSize = pSample->GetSize();
		if (buffSize < (long)(m_BufferSamples * m_BytesPerSample)) {
			return S_FALSE;
		}

		auto Clip = m_pAviSynthFile->m_AVSValue.AsClip();
		int64_t count = std::min<int64_t>(m_BufferSamples, m_NumSamples - m_CurrentSample);
		Clip->GetAudio(dst_data, m_CurrentSample, count, m_pAviSynthFile->m_ScriptEnvironment);

		pSample->SetActualDataLength(count * m_BytesPerSample);

		// Sample time
		REFERENCE_TIME rtStart = llMulDiv(m_SampleCounter, UNITS, m_SampleRate, 0);
		REFERENCE_TIME rtStop  = llMulDiv(m_SampleCounter + count, UNITS, m_SampleRate, 0);
		// The sample times are modified by the current rate.
		if (m_dRateSeeking != 1.0) {
			rtStart = static_cast<REFERENCE_TIME>(rtStart / m_dRateSeeking);
			rtStop  = static_cast<REFERENCE_TIME>(rtStop / m_dRateSeeking);
		}
		pSample->SetTime(&rtStart, &rtStop);

		m_SampleCounter += count;
		m_CurrentSample += count;
	}

	pSample->SetSyncPoint(TRUE);

	if (m_bDiscontinuity) {
		pSample->SetDiscontinuity(TRUE);
		m_bDiscontinuity = FALSE;
	}

	return S_OK;
}

HRESULT CAviSynthAudioStream::CheckMediaType(const CMediaType* pmt)
{
	if (pmt->majortype == MEDIATYPE_Audio
		&& pmt->subtype == m_Subtype
		&& pmt->formattype == FORMAT_WaveFormatEx) {

		WAVEFORMATEX* wfe = (WAVEFORMATEX*)pmt->Format();
		if ((int)wfe->nChannels >= m_Channels
				&& (int)wfe->nSamplesPerSec == m_SampleRate
				&& (int)wfe->nBlockAlign == m_BytesPerSample
				&& (int)wfe->wBitsPerSample == m_BitDepth) {
			return S_OK;
		}
	}

	return E_INVALIDARG;
}

HRESULT CAviSynthAudioStream::SetMediaType(const CMediaType* pMediaType)
{
	HRESULT hr = __super::SetMediaType(pMediaType);

	if (SUCCEEDED(hr)) {
		DLog(L"SetMediaType with subtype {}", GUIDtoWString(m_mt.subtype));
	}

	return hr;
}

HRESULT CAviSynthAudioStream::GetMediaType(int iPosition, CMediaType* pmt)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	if (iPosition < 0) {
		return E_INVALIDARG;
	}
	if (iPosition >= 1) {
		return VFW_S_NO_MORE_ITEMS;
	}

	*pmt = m_mt;

	return S_OK;
}
