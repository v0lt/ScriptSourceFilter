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

#include "stdafx.h"

#include "Helper.h"
#include "StringHelper.h"

#include "AviSynthStream.h"

const AVS_Linkage* AVS_linkage = NULL;

//
// CAviSynthStream
//

CAviSynthStream::CAviSynthStream(const WCHAR* name, CSource* pParent, HRESULT* phr)
	: CSourceStream(name, phr, pParent, L"Output")
	, CSourceSeeking(name, (IPin*)this, phr, &m_cSharedState)
{
	CAutoLock cAutoLock(&m_cSharedState);

	m_hAviSynthDll = LoadLibraryW(L"Avisynth.dll");
	if (!m_hAviSynthDll) {
		DLog(L"Failed to load AviSynth+");
		*phr = E_FAIL;
		return;
	}

	IScriptEnvironment2* (*CreateScriptEnvironment2)(int version) =
		(IScriptEnvironment2 * (*)(int)) GetProcAddress(m_hAviSynthDll, "CreateScriptEnvironment2");
	if (!CreateScriptEnvironment2) {
		DLog(L"Cannot resolve AviSynth+ CreateScriptEnvironment2 function");
		*phr = E_FAIL;
		return;
	}


	m_ScriptEnvironment = CreateScriptEnvironment2(6);
	if (!m_ScriptEnvironment) {
		DLog(L"A newer AviSynth+ version is required");
		*phr = E_FAIL;
		return;
	}

	AVS_linkage = m_Linkage = m_ScriptEnvironment->GetAVSLinkage();

	std::string ansiFile = ConvertWideToANSI(name);
	AVSValue arg(ansiFile.c_str());
	try {
		m_AVSValue = m_ScriptEnvironment->Invoke("Import", AVSValue(&arg, 1));
	}
	catch ([[maybe_unused]] AvisynthError e) {
		DLog(L"Failure to open script file. AvisynthError: %S", e.msg);
		*phr = E_FAIL;
		return;
	}

	if (!m_AVSValue.IsClip()) {
		DLog(L"AviSynth+ script does not return a video clip");
		*phr = E_FAIL;
		return;
	}

	auto Clip = m_AVSValue.AsClip();

	auto VInfo = Clip->GetVideoInfo();
	DLog(L"Open clip %dx%d %.03f fps", VInfo.width, VInfo.height, (double)VInfo.fps_numerator/VInfo.fps_denominator);

	switch (VInfo.pixel_type) {
	case VideoInfo::CS_BGR24:
		m_subtype = MEDIASUBTYPE_RGB24;
		break;
	case VideoInfo::CS_BGR32:
		m_subtype = MEDIASUBTYPE_ARGB32;
		break;
	case VideoInfo::CS_BGR48:
		m_subtype = MEDIASUBTYPE_RGB48;
		break;
	case VideoInfo::CS_BGR64:
		m_subtype = MEDIASUBTYPE_ARGB64;
		break;
	case VideoInfo::CS_YUY2:
		m_subtype = MEDIASUBTYPE_YUY2;
		break;
	case VideoInfo::CS_YV12:
		m_subtype = MEDIASUBTYPE_YV12;
		break;
	case VideoInfo::CS_YV16:
		m_subtype = MEDIASUBTYPE_YV16;
		break;
	case VideoInfo::CS_YV24:
		m_subtype = MEDIASUBTYPE_YV24;
		break;
	case VideoInfo::CS_Y8:
		m_subtype = MEDIASUBTYPE_Y8;
		break;
	case VideoInfo::CS_Y16:
		m_subtype = MEDIASUBTYPE_Y116;
		break;
	default:
		DLog(L"Unsuported pixel_type");
		*phr = E_FAIL;
		return;
	}

	DWORD fourcc = (m_subtype == MEDIASUBTYPE_RGB24 || m_subtype == MEDIASUBTYPE_RGB32) ? BI_RGB : m_subtype.Data1;

	m_Width  = VInfo.width;
	m_Height = VInfo.height;
	m_NumFrames = VInfo.num_frames;
	m_AvgTimePerFrame = MulDiv(UNITS, VInfo.fps_denominator, VInfo.fps_numerator);

	UINT bitdepth = VInfo.BitsPerPixel();

	auto VFrame = Clip->GetFrame(0, m_ScriptEnvironment);
	int pitch = VFrame->GetPitch();
	m_BufferSize = pitch * m_Height;

	m_mt.InitMediaType();
	m_mt.SetType(&MEDIATYPE_Video);
	m_mt.SetSubtype(&m_subtype);
	m_mt.SetFormatType(&FORMAT_VideoInfo2);
	m_mt.SetTemporalCompression(FALSE);
	m_mt.SetSampleSize(m_BufferSize);

	VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2));
	memset(vih2, 0, sizeof(VIDEOINFOHEADER2));
	vih2->rcSource                = { 0, 0, (long)m_Width, (long)m_Height};
	vih2->rcTarget                = vih2->rcSource;
	vih2->AvgTimePerFrame         = m_AvgTimePerFrame;
	vih2->bmiHeader.biSize        = sizeof(vih2->bmiHeader);
	vih2->bmiHeader.biWidth       = m_Width;
	vih2->bmiHeader.biHeight      = m_Height;
	vih2->bmiHeader.biPlanes      = 1;
	vih2->bmiHeader.biBitCount    = bitdepth;
	vih2->bmiHeader.biCompression = fourcc;
	vih2->bmiHeader.biSizeImage   = m_BufferSize;

	m_rtDuration = m_rtStop = UNITS * m_NumFrames * VInfo.fps_denominator / VInfo.fps_numerator;

	*phr = S_OK;
}

CAviSynthStream::~CAviSynthStream()
{
	CAutoLock cAutoLock(&m_cSharedState);

	AVS_linkage = m_Linkage;

	m_AVSValue = 0;

	if (m_ScriptEnvironment) {
		m_ScriptEnvironment->DeleteScriptEnvironment();
		m_ScriptEnvironment = nullptr;
	}

	AVS_linkage = nullptr;
	m_Linkage   = nullptr;

	if (m_hAviSynthDll) {
		FreeLibrary(m_hAviSynthDll);
	}
}

STDMETHODIMP CAviSynthStream::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return (riid == IID_IMediaSeeking) ? CSourceSeeking::NonDelegatingQueryInterface(riid, ppv)
		: CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

void CAviSynthStream::UpdateFromSeek()
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

HRESULT CAviSynthStream::SetRate(double dRate)
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

HRESULT CAviSynthStream::OnThreadStartPlay()
{
	m_bDiscontinuity = TRUE;
	return DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
}

HRESULT CAviSynthStream::ChangeStart()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_rtSampleTime = 0;
		m_rtPosition = m_rtStart;
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthStream::ChangeStop()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		if (m_rtPosition < m_rtStop) {
			return S_OK;
		}
	}

	// We're already past the new stop time -- better flush the graph.
	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_rtSampleTime = 0;
	m_rtPosition = m_rtStart;

	return CSourceStream::OnThreadCreate();
}

HRESULT CAviSynthStream::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
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

HRESULT CAviSynthStream::FillBuffer(IMediaSample* pSample)
{
	{
		CAutoLock cAutoLockShared(&m_cSharedState);

		if (m_rtPosition >= m_rtStop) {
			return S_FALSE;
		}

		auto Clip = m_AVSValue.AsClip();

		auto VInfo = Clip->GetVideoInfo();

		int framenum = (int)(m_rtPosition * VInfo.fps_numerator / (VInfo.fps_denominator * UNITS));
		auto VFrame = Clip->GetFrame(framenum, m_ScriptEnvironment);
		const BYTE* src_data = VFrame->GetReadPtr();
		const UINT src_pitch = VFrame->GetPitch();

		HRESULT hr;
		BYTE* pDataOut = nullptr;
		if (!src_data || FAILED(hr = pSample->GetPointer(&pDataOut)) || !pDataOut) {
			return S_FALSE;
		}

		long outSize = pSample->GetSize();

		AM_MEDIA_TYPE* pmt;
		if (SUCCEEDED(pSample->GetMediaType(&pmt)) && pmt) {
			CMediaType mt(*pmt);
			SetMediaType(&mt);

			DeleteMediaType(pmt);
		}

		if (m_mt.formattype != FORMAT_VideoInfo2) {
			return S_FALSE;
		}

		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.Format();
		const UINT w = vih2->bmiHeader.biWidth;
		const UINT h = abs(vih2->bmiHeader.biHeight);
		const UINT bpp = vih2->bmiHeader.biBitCount;
		const UINT dst_pitch = w * bpp / 8;

		if (w < m_Width || h != m_Height || outSize < (long)vih2->bmiHeader.biSizeImage) {
			return S_FALSE;
		}

		UINT linesize = std::min(src_pitch, dst_pitch);

		if (src_pitch == dst_pitch) {
			memcpy(pDataOut, src_data, linesize * m_Height);
		}
		else {
			const BYTE* src = src_data;
			BYTE* dst = pDataOut;
			for (UINT y = 0; y < m_Height; y++) {
				memcpy(dst, src, linesize);
				src += src_pitch;
				dst += dst_pitch;
			}
		}

		pSample->SetActualDataLength(linesize * m_Height);

		REFERENCE_TIME rtStart, rtStop;
		// The sample times are modified by the current rate.
		rtStart = static_cast<REFERENCE_TIME>(m_rtSampleTime / m_dRateSeeking);
		rtStop  = rtStart + static_cast<int>(m_AvgTimePerFrame / m_dRateSeeking);
		pSample->SetTime(&rtStart, &rtStop);

		m_rtSampleTime += m_AvgTimePerFrame;
		m_rtPosition += m_AvgTimePerFrame;
	}

	pSample->SetSyncPoint(TRUE);

	if (m_bDiscontinuity) {
		pSample->SetDiscontinuity(TRUE);
		m_bDiscontinuity = FALSE;
	}

	return S_OK;
}

HRESULT CAviSynthStream::GetMediaType(int iPosition, CMediaType* pmt)
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

HRESULT CAviSynthStream::CheckMediaType(const CMediaType* pmt)
{
	if (pmt->majortype == MEDIATYPE_Video
		&& pmt->subtype == m_subtype
		&& pmt->formattype == FORMAT_VideoInfo2) {

		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->Format();
		if (vih2->bmiHeader.biWidth >= (long)m_Width && vih2->bmiHeader.biHeight == m_Height) {
			return S_OK;
		}
	}

	return E_INVALIDARG;
}

HRESULT CAviSynthStream::SetMediaType(const CMediaType* pMediaType)
{
	HRESULT hr = __super::SetMediaType(pMediaType);

	DLogIf(SUCCEEDED(hr), L"SetMediaType with subtype %s", GUIDtoWString(m_mt.subtype).c_str());

	return hr;
}

STDMETHODIMP CAviSynthStream::Notify(IBaseFilter* pSender, Quality q)
{
	return E_NOTIMPL;
}
