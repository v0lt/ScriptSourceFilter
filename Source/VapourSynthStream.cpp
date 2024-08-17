/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "stdafx.h"
#include "VUIOptions.h"
#include "ScriptSource.h"

#include "VapourSynthStream.h"

#include <mmreg.h>

 //
 // CVapourSynthFile
 //

CVapourSynthFile::CVapourSynthFile(const WCHAR* name, CSource* pParent, HRESULT* phr)
{
	try {
		m_hVSScriptDll = LoadLibraryW(L"vsscript.dll");
		if (!m_hVSScriptDll) {
			throw std::exception("Failed to load VapourSynt");
		}

#ifdef _WIN64
		const VSSCRIPTAPI* (WINAPI * getVSScriptAPI)(int version) =
			(const VSSCRIPTAPI * (WINAPI*)(int))GetProcAddress(m_hVSScriptDll, "getVSScriptAPI");
#else
		const VSSCRIPTAPI* (WINAPI * getVSScriptAPI)(int version) =
			(const VSSCRIPTAPI * (WINAPI*)(int))GetProcAddress(m_hVSScriptDll, "_getVSScriptAPI@4");
#endif

		m_vsScriptAPI = getVSScriptAPI(VSSCRIPT_API_VERSION);
		if (!m_vsScriptAPI) {
			throw std::exception("Failed to get VSScriptAPI");
		}

		m_vsAPI = m_vsScriptAPI->getVSAPI(VAPOURSYNTH_API_VERSION);
		ASSERT(m_vsAPI);
	}
	catch (const std::exception& e) {
		DLog(A2WStr(e.what()));
		*phr = E_FAIL;
		return;
	}

	HRESULT hr;
	std::wstring error;

	try {
		m_vsScript = m_vsScriptAPI->createScript(nullptr);
		//m_vsScriptAPI->evalSetWorkingDir(m_vsScript, 1);

		std::string utf8file = ConvertWideToUtf8(name);
		if (m_vsScriptAPI->evaluateFile(m_vsScript, utf8file.c_str())) {
			error = ConvertUtf8ToWide(m_vsScriptAPI->getError(m_vsScript));
			throw std::exception("Failed to call VapourSynth evaluateFile");
		}

		SetVSNodes();

		if (m_vsNodeVideo) {
			auto pVideoStream = new CVapourSynthVideoStream(this, pParent, &hr);
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

		if (m_vsNodeAudio) {
			auto pAudioStream = new CVapourSynthAudioStream(this, pParent, &hr);
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
		}
		else {
			hr = E_FAIL;
		}
	}

	*phr = hr;
}

CVapourSynthFile::~CVapourSynthFile()
{
	if (m_vsNodeVideo) {
		m_vsAPI->freeNode(m_vsNodeVideo);
		m_vsNodeVideo = nullptr;
	}
	if (m_vsNodeAudio) {
		m_vsAPI->freeNode(m_vsNodeAudio);
		m_vsNodeAudio = nullptr;
	}

	if (m_vsScript) {
		m_vsScriptAPI->freeScript(m_vsScript);
		m_vsScript = nullptr;
	}

	m_vsScriptAPI = nullptr;

	if (m_hVSScriptDll) {
		FreeLibrary(m_hVSScriptDll);
	}
}

void CVapourSynthFile::SetVSNodes()
{
	VSNode* vsNode = m_vsScriptAPI->getOutputNode(m_vsScript, 0);
	if (!vsNode) {
		throw std::exception("Failed to get VapourSynth output node 0");
	}

	auto vi = m_vsAPI->getVideoInfo(vsNode);

	if (vi && vi->format.colorFamily != cfUndefined && vi->width > 0 && vi->height > 0) {
		const auto& vf = vi->format;
		const auto videoID = ((vf.colorFamily << 28) | (vf.sampleType << 24) | (vf.bitsPerSample << 16) | (vf.subSamplingW << 8) | (vf.subSamplingH << 0));
		auto Format = GetFormatParamsVapourSynth(videoID);
		if (Format.fourcc == DWORD(-1)) {
			char formatname[32] = {};
			m_vsAPI->getVideoFormatName(&vi->format, formatname);

			m_vsAPI->freeNode(vsNode);
			vsNode = nullptr;

			throw std::exception(std::format("Unsuported pixel type {}", formatname).c_str());
		}

		m_vsNodeVideo = vsNode;
		vsNode = nullptr;
	}

	if (!vsNode) {
		vsNode = m_vsScriptAPI->getOutputNode(m_vsScript, 1);
	}

	auto ai = m_vsAPI->getAudioInfo(vsNode);

	if (ai && ai->format.numChannels > 0 && ai->format.numChannels <= 32) {
		m_vsNodeAudio = vsNode;
		vsNode = nullptr;
	}

	if (vsNode) {
		m_vsAPI->freeNode(vsNode);
		vsNode = nullptr;
	}
}

//
// CVapourSynthVideoStream
//

CVapourSynthVideoStream::CVapourSynthVideoStream(CVapourSynthFile* pVapourSynthFile, CSource* pParent, HRESULT* phr)
	: CSourceStream(L"Audio", phr, pParent, L"Audio")
	, CSourceSeeking(L"Audio", (IPin*)this, phr, &m_cSharedState)
	, m_pVapourSynthFile(pVapourSynthFile)
{
	CAutoLock cAutoLock(&m_cSharedState);

	HRESULT hr;
	std::wstring error;

	try {
		m_vsVideoInfo = m_pVapourSynthFile->m_vsAPI->getVideoInfo(m_pVapourSynthFile->m_vsNodeVideo);

		const auto& vf = m_vsVideoInfo->format;
		const auto videoID = ((vf.colorFamily << 28) | (vf.sampleType << 24) | (vf.bitsPerSample << 16) | (vf.subSamplingW << 8) | (vf.subSamplingH << 0));
		m_Format = GetFormatParamsVapourSynth(videoID);

		char formatname[32] = {};
		m_pVapourSynthFile->m_vsAPI->getVideoFormatName(&m_vsVideoInfo->format, formatname);

		if (m_Format.fourcc == DWORD(-1) || m_Format.planes != m_vsVideoInfo->format.numPlanes) {
			throw std::exception(std::format("Unsuported pixel type {}", formatname).c_str());
		}

		m_Width = m_vsVideoInfo->width;
		m_Height = m_vsVideoInfo->height;
		m_fpsNum = m_vsVideoInfo->fpsNum;
		m_fpsDen = m_vsVideoInfo->fpsDen;
		m_NumFrames = m_vsVideoInfo->numFrames;
		m_AvgTimePerFrame = llMulDiv(UNITS, m_fpsDen, m_fpsNum, 0);
		m_rtDuration = m_rtStop = llMulDiv(UNITS * m_NumFrames, m_fpsDen, m_fpsNum, 0);

		UINT color_info = 0;
		m_StreamInfo = std::format(
			L"Script type : VapourSynth\n"
			L"Video stream: {} {}x{} {:.3f} fps",
			m_Format.str, m_Width, m_Height, (double)m_fpsNum / m_fpsDen
		);

		const VSFrame* frame = m_pVapourSynthFile->m_vsAPI->getFrame(0, m_pVapourSynthFile->m_vsNodeVideo, m_vsErrorMessage, sizeof(m_vsErrorMessage));
		if (!frame) {
			error = ConvertUtf8ToWide(m_vsErrorMessage);
			throw std::exception("Failed to call getFrame(0)");
		}
		m_Pitch = m_pVapourSynthFile->m_vsAPI->getStride(frame, 0);
		m_PitchBuff = m_Pitch;
		m_BufferSize = m_PitchBuff * m_Height * m_Format.buffCoeff / 2;

		const VSMap* vsMap = m_pVapourSynthFile->m_vsAPI->getFramePropertiesRO(frame);
		if (vsMap) {
			int numKeys = m_pVapourSynthFile->m_vsAPI->mapNumKeys(vsMap);
			if (numKeys > 0) {
				m_StreamInfo += std::format(L"\nProperties [{}]:", numKeys);
			}

			for (int i = 0; i < numKeys; i++) {
				const char* keyName = m_pVapourSynthFile->m_vsAPI->mapGetKey(vsMap, i);
				if (keyName && keyName[0]) {
					int64_t val_Int = 0;
					double val_Float = 0;
					const char* val_Data = 0;
					int err = 0;
					const char keyType = m_pVapourSynthFile->m_vsAPI->mapGetType(vsMap, keyName);

					m_StreamInfo += std::format(L"\n{:>2}: ", i);

					switch (keyType) {
					case ptInt:
						val_Int = m_pVapourSynthFile->m_vsAPI->mapGetInt(vsMap, keyName, 0, &err);
						if (!err) {
							m_StreamInfo += std::format(L"<i> '{}' = {}", A2WStr(keyName), val_Int);
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
					case ptFloat:
						val_Float = m_pVapourSynthFile->m_vsAPI->mapGetFloat(vsMap, keyName, 0, &err);
						if (!err) {
							m_StreamInfo += std::format(L"<f> '{}' = {:.3f}", A2WStr(keyName), val_Float);
						}
						break;
					case ptData:
						val_Data = m_pVapourSynthFile->m_vsAPI->mapGetData(vsMap, keyName, 0, &err);
						if (!err) {
							const int dataSize = m_pVapourSynthFile->m_vsAPI->mapGetDataSize(vsMap, keyName, 0, &err);
							if (!err) {
								if (dataSize == 1 && strcmp(keyName, "_PictType") == 0) {
									m_StreamInfo += std::format(L"<b> '{}' = {}", A2WStr(keyName), val_Data[0]);
								}
								else {
									m_StreamInfo += std::format(L"<b> '{}', {} bytes", A2WStr(keyName), dataSize);
								}
							}
						}
						break;
					}
				}
			}
		}
		m_pVapourSynthFile->m_vsAPI->freeFrame(frame);

		if (m_vsVideoInfo->format.colorFamily == cfRGB) {
			// planar RGB
			m_Planes[0] = 1;
			m_Planes[1] = 2;
			m_Planes[2] = 0;
		}
		else if (m_vsVideoInfo->format.colorFamily == cfYUV) {
			if (m_Format.CDepth == 8) {
				// swap U and V for YV12, YV16, YV24.
				m_Planes[0] = 0;
				m_Planes[1] = 2;
				m_Planes[2] = 1;
			}
			else {
				m_Planes[0] = 0;
				m_Planes[1] = 1;
				m_Planes[2] = 2;
			}
		}

		if (color_info) {
			m_ColorInfo = color_info | (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT);
		}

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

		DLog(m_StreamInfo);

		hr = S_OK;
	}
	catch (const std::exception& e) {
		DLog(L"{}\n{}", A2WStr(e.what()), error);

		hr = E_FAIL;
	}

	*phr = hr;
}

CVapourSynthVideoStream::~CVapourSynthVideoStream()
{
}

STDMETHODIMP CVapourSynthVideoStream::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return (riid == IID_IMediaSeeking) ? CSourceSeeking::NonDelegatingQueryInterface(riid, ppv)
		: CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CVapourSynthVideoStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_FrameCounter = 0;
	m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down

	return CSourceStream::OnThreadCreate();
}

HRESULT CVapourSynthVideoStream::OnThreadStartPlay()
{
	m_bDiscontinuity = TRUE;
	return DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
}

void CVapourSynthVideoStream::UpdateFromSeek()
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

HRESULT CVapourSynthVideoStream::SetRate(double dRate)
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

HRESULT CVapourSynthVideoStream::ChangeStart()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_FrameCounter = 0;
		m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CVapourSynthVideoStream::ChangeStop()
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

HRESULT CVapourSynthVideoStream::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
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

HRESULT CVapourSynthVideoStream::FillBuffer(IMediaSample* pSample)
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
			const VSFrame* frame = m_pVapourSynthFile->m_vsAPI->getFrame(m_CurrentFrame, m_pVapourSynthFile->m_vsNodeVideo, m_vsErrorMessage, sizeof(m_vsErrorMessage));
			if (!frame) {
				DLog(ConvertUtf8ToWide(m_vsErrorMessage));
				return E_FAIL;
			}

			const int num_planes = m_vsVideoInfo->format.numPlanes;
			for (int i = 0; i < num_planes; i++) {
				const int plane = m_Planes[i];
				const BYTE* src_data = m_pVapourSynthFile->m_vsAPI->getReadPtr(frame, plane);
				int src_pitch = m_pVapourSynthFile->m_vsAPI->getStride(frame, plane);
				const UINT height = m_pVapourSynthFile->m_vsAPI->getFrameHeight(frame, plane);
				UINT dst_pitch = m_PitchBuff;
				if (i > 0) {
					dst_pitch >>= m_vsVideoInfo->format.subSamplingW;
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
			rtStop = static_cast<REFERENCE_TIME>(rtStop / m_dRateSeeking);
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

HRESULT CVapourSynthVideoStream::CheckMediaType(const CMediaType* pmt)
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

HRESULT CVapourSynthVideoStream::SetMediaType(const CMediaType* pMediaType)
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

HRESULT CVapourSynthVideoStream::GetMediaType(int iPosition, CMediaType* pmt)
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
// CVapourSynthAudioStream
//

CVapourSynthAudioStream::CVapourSynthAudioStream(CVapourSynthFile* pVapourSynthFile, CSource* pParent, HRESULT* phr)
	: CSourceStream(L"Audio", phr, pParent, L"Audio")
	, CSourceSeeking(L"Audio", (IPin*)this, phr, &m_cSharedState)
	, m_pVapourSynthFile(pVapourSynthFile)
{
	CAutoLock cAutoLock(&m_cSharedState);

	HRESULT hr;
	std::wstring error;

	try {
		m_vsAudioInfo = m_pVapourSynthFile->m_vsAPI->getAudioInfo(m_pVapourSynthFile->m_vsNodeAudio);

		m_Channels       = m_vsAudioInfo->format.numChannels;
		m_SampleRate     = m_vsAudioInfo->sampleRate;
		m_BytesPerSample = m_vsAudioInfo->format.bytesPerSample * m_Channels;
		m_BitDepth       = m_vsAudioInfo->format.bitsPerSample;
		m_SampleType     = (VSSampleType)m_vsAudioInfo->format.sampleType;
		m_NumSamples     = m_vsAudioInfo->numSamples;
		m_NumFrames      = m_vsAudioInfo->numFrames;

		WORD wFormatTag;
		if (m_SampleType == stFloat) {
			wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			m_Subtype = MEDIASUBTYPE_IEEE_FLOAT;
		}
		else if (m_SampleType == stInteger) {
			wFormatTag = WAVE_FORMAT_PCM;
			m_Subtype = MEDIASUBTYPE_PCM;
		}
		else {
			throw std::exception("Invalid audio sample type");
		}

		const VSFrame* frame = m_pVapourSynthFile->m_vsAPI->getFrame(0, m_pVapourSynthFile->m_vsNodeAudio, m_vsErrorMessage, sizeof(m_vsErrorMessage));
		if (!frame) {
			error = ConvertUtf8ToWide(m_vsErrorMessage);
			throw std::exception("Failed to call getFrame(0)");
		}
		m_FrameSamples = m_pVapourSynthFile->m_vsAPI->getFrameLength(frame);
		m_pVapourSynthFile->m_vsAPI->freeFrame(frame);

		m_rtDuration = m_rtStop = llMulDiv(m_NumSamples, UNITS, m_SampleRate, 0);

		m_mt.InitMediaType();
		m_mt.SetType(&MEDIATYPE_Audio);
		m_mt.SetSubtype(&m_Subtype);
		m_mt.SetFormatType(&FORMAT_WaveFormatEx);
		m_mt.SetTemporalCompression(FALSE);
		m_mt.SetSampleSize(m_BytesPerSample);

		WAVEFORMATEX* wfe = (WAVEFORMATEX*)m_mt.AllocFormatBuffer(sizeof(WAVEFORMATEX));
		wfe->wFormatTag = wFormatTag;
		wfe->nChannels = m_Channels;
		wfe->nSamplesPerSec = m_SampleRate;
		wfe->nAvgBytesPerSec = m_BytesPerSample * m_SampleRate;
		wfe->nBlockAlign = m_BytesPerSample;
		wfe->wBitsPerSample = m_BitDepth;
		wfe->cbSize = 0;

		m_StreamInfo = std::format(L"Audio stream: {} channels, {} Hz, ", m_Channels, m_SampleRate);
		if (m_SampleType == stFloat) {
			m_StreamInfo.append(L"float");
		}
		else if (m_SampleType == stInteger) {
			m_StreamInfo.append(L"int");
		}
		m_StreamInfo.append(std::to_wstring(m_BitDepth));
		DLog(m_StreamInfo);

		hr = S_OK;
	}
	catch (const std::exception& e) {
		DLog(L"{}\n{}", A2WStr(e.what()), error);

		hr = E_FAIL;
	}

	*phr = hr;
}

CVapourSynthAudioStream::~CVapourSynthAudioStream()
{
}

STDMETHODIMP CVapourSynthAudioStream::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return (riid == IID_IMediaSeeking) ? CSourceSeeking::NonDelegatingQueryInterface(riid, ppv)
		: CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CVapourSynthAudioStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_FrameCounter = 0;
	m_CurrentFrame = (int)llMulDiv(m_rtStart, m_SampleRate, m_FrameSamples * UNITS, 0); // round down

	return CSourceStream::OnThreadCreate();
}

HRESULT CVapourSynthAudioStream::OnThreadStartPlay()
{
	m_bDiscontinuity = TRUE;
	return DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
}

void CVapourSynthAudioStream::UpdateFromSeek()
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

HRESULT CVapourSynthAudioStream::SetRate(double dRate)
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

HRESULT CVapourSynthAudioStream::ChangeStart()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_FrameCounter = 0;
		m_CurrentFrame = (int)llMulDiv(m_rtStart, m_SampleRate, m_FrameSamples * UNITS, 0); // round down
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CVapourSynthAudioStream::ChangeStop()
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

HRESULT CVapourSynthAudioStream::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
{
	//CAutoLock cAutoLock(m_pFilter->pStateLock());

	ASSERT(pAlloc);
	ASSERT(pProperties);

	HRESULT hr = NOERROR;

	pProperties->cBuffers = 1;
	pProperties->cbBuffer = m_FrameSamples * m_BytesPerSample;

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

HRESULT CVapourSynthAudioStream::FillBuffer(IMediaSample* pSample)
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

		if (m_mt.formattype != FORMAT_WaveFormatEx) {
			return S_FALSE;
		}

		BYTE* dst_data = nullptr;
		HRESULT hr = pSample->GetPointer(&dst_data);
		if (FAILED(hr) || !dst_data) {
			return S_FALSE;
		}

		long buffSize = pSample->GetSize();

		const VSFrame* frame = m_pVapourSynthFile->m_vsAPI->getFrame(m_CurrentFrame, m_pVapourSynthFile->m_vsNodeAudio, m_vsErrorMessage, sizeof(m_vsErrorMessage));
		if (!frame) {
			DLog(ConvertUtf8ToWide(m_vsErrorMessage));
			return E_FAIL;
		}
		const int frameSamples = m_pVapourSynthFile->m_vsAPI->getFrameLength(frame);
		int frameSize = frameSamples * m_BytesPerSample;

		std::vector<const uint8_t*> frameptrs(m_Channels, nullptr);
		for (int ch = 0; ch < m_Channels; ch++) {
			frameptrs[ch] = m_pVapourSynthFile->m_vsAPI->getReadPtr(frame, ch);
			if (!frameptrs[ch]) {
				frameSize = 0;
				break;
			}
		}

		if (!frameSize || buffSize < (long)(frameSize)) {
			m_pVapourSynthFile->m_vsAPI->freeFrame(frame);
			return S_FALSE;
		}

		switch (m_BitDepth) {
		case 8:
		{
			uint8_t* dst8 = dst_data;
			for (int i = 0; i < frameSamples; i++) {
				for (int ch = 0; ch < m_Channels; ch++) {
					*dst8++ = *frameptrs[ch]++;
				}
			}
			break;
		}
		case 16:
		{
			uint16_t* dst16 = (uint16_t*)dst_data;
			for (int i = 0; i < frameSamples; i++) {
				for (int ch = 0; ch < m_Channels; ch++) {
					*dst16++ = *(uint16_t*)frameptrs[ch];
					frameptrs[ch] += sizeof(uint16_t);
				}
			}
			break;
		}
		case 32:
		{
			uint32_t* dst32 = (uint32_t*)dst_data;
			for (int i = 0; i < frameSamples; i++) {
				for (int ch = 0; ch < m_Channels; ch++) {
					*dst32++ = *(uint32_t*)frameptrs[ch];
					frameptrs[ch] += sizeof(uint32_t);
				}
			}
			break;
		}
		}

		m_pVapourSynthFile->m_vsAPI->freeFrame(frame);

		pSample->SetActualDataLength(frameSize);

		// Sample time
		REFERENCE_TIME rtStart = llMulDiv(m_FrameCounter,     UNITS * m_FrameSamples, m_SampleRate, 0);
		REFERENCE_TIME rtStop  = llMulDiv(m_FrameCounter + 1, UNITS * m_FrameSamples, m_SampleRate, 0);

		// The sample times are modified by the current rate.
		if (m_dRateSeeking != 1.0) {
			rtStart = static_cast<REFERENCE_TIME>(rtStart / m_dRateSeeking);
			rtStop = static_cast<REFERENCE_TIME>(rtStop / m_dRateSeeking);
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

HRESULT CVapourSynthAudioStream::CheckMediaType(const CMediaType* pmt)
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

HRESULT CVapourSynthAudioStream::SetMediaType(const CMediaType* pMediaType)
{
	HRESULT hr = __super::SetMediaType(pMediaType);

	if (SUCCEEDED(hr)) {
		DLog(L"SetMediaType with subtype {}", GUIDtoWString(m_mt.subtype));
	}

	return hr;
}

HRESULT CVapourSynthAudioStream::GetMediaType(int iPosition, CMediaType* pmt)
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