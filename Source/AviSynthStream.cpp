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

#include "VUIOptions.h"
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

	IScriptEnvironment* (WINAPI *CreateScriptEnvironment)(int version) =
		(IScriptEnvironment * (WINAPI *)(int)) GetProcAddress(m_hAviSynthDll, "CreateScriptEnvironment");

	if (!CreateScriptEnvironment) {
		DLog(L"Cannot resolve AviSynth+ CreateScriptEnvironment2 function");
		*phr = E_FAIL;
		return;
	}

	m_ScriptEnvironment = CreateScriptEnvironment(6);
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
	catch ([[maybe_unused]] const AvisynthError& e) {
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
	UINT bitdepth = VInfo.BitsPerPixel();

	m_Format = GetFormatParamsAviSynth(VInfo.pixel_type);

	if (m_Format.fourcc == DWORD(-1)) {
		DLog(L"Unsuported pixel_type");
		*phr = E_FAIL;
		return;
	}

	auto VFrame = Clip->GetFrame(0, m_ScriptEnvironment);
	m_Pitch = VFrame->GetPitch();

	m_Width      = VInfo.width;
	m_Height     = VInfo.height;
	m_PitchBuff  = m_Pitch;
	m_BufferSize = m_PitchBuff * m_Height * m_Format.buffCoeff / 2;

	m_fpsNum     = VInfo.fps_numerator;
	m_fpsDen     = VInfo.fps_denominator;
	m_NumFrames  = VInfo.num_frames;
	m_AvgTimePerFrame = UNITS * m_fpsDen / m_fpsNum; // no need any MulDiv here
	m_rtDuration = m_rtStop = llMulDiv(UNITS * m_NumFrames, m_fpsDen, m_fpsNum, 0);

	if (VInfo.IsPlanar()) {
		if (VInfo.IsYUV()) {
			m_Planes[0] = PLANAR_Y;
			if (VInfo.IsVPlaneFirst()) {
				m_Planes[1] = PLANAR_U; // Yes, that’s right, because the output is YV12, YV16, YV24.
				m_Planes[2] = PLANAR_V;
			} else {
				m_Planes[1] = PLANAR_V;
				m_Planes[2] = PLANAR_U;
			}
		} else if (VInfo.IsRGB()) {
			m_Planes[0] = PLANAR_G;
			m_Planes[1] = PLANAR_B;
			m_Planes[2] = PLANAR_R;
		}
	}

	DLog(L"Open clip %S %dx%d %.03f fps", m_Format.str, m_Width, m_Height, (double)m_fpsNum/m_fpsDen);

	m_mt.InitMediaType();
	m_mt.SetType(&MEDIATYPE_Video);
	m_mt.SetSubtype(&m_Format.subtype);
	m_mt.SetFormatType(&FORMAT_VideoInfo2);
	m_mt.SetTemporalCompression(FALSE);
	m_mt.SetSampleSize(m_BufferSize);

	VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2));
	memset(vih2, 0, sizeof(VIDEOINFOHEADER2));
	vih2->rcSource                = { 0, 0, (long)m_Width, (long)m_Height};
	vih2->rcTarget                = vih2->rcSource;
	vih2->AvgTimePerFrame         = m_AvgTimePerFrame;
	vih2->bmiHeader.biSize        = sizeof(vih2->bmiHeader);
	vih2->bmiHeader.biWidth       = m_PitchBuff / m_Format.Packsize;
	vih2->bmiHeader.biHeight      = (m_Format.fourcc == BI_RGB) ? -(long)m_Height : m_Height;
	vih2->bmiHeader.biPlanes      = 1;
	vih2->bmiHeader.biBitCount    = m_Format.bitCount;
	vih2->bmiHeader.biCompression = m_Format.fourcc;
	vih2->bmiHeader.biSizeImage   = m_BufferSize;

	vih2->dwControlFlags = GetColorInfoFromVUIOptions(ansiFile.c_str());

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
		m_FrameCounter = 0;
		m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CAviSynthStream::ChangeStop()
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

HRESULT CAviSynthStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_FrameCounter = 0;
	m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down

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

		auto Clip = m_AVSValue.AsClip();
		auto VInfo = Clip->GetVideoInfo();
		auto VFrame = Clip->GetFrame(m_CurrentFrame, m_ScriptEnvironment);

		UINT DataLength = 0;
		const int num_planes = m_Format.planes;
		for (int i = 0; i < num_planes; i++) {
			const int plane = m_Planes[i];
			const BYTE* src_data = VFrame->GetReadPtr(plane);
			int src_pitch = VFrame->GetPitch(plane);
			const UINT height    = VFrame->GetHeight(plane);
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
		&& pmt->subtype == m_Format.subtype
		&& pmt->formattype == FORMAT_VideoInfo2) {

		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->Format();
		if (vih2->bmiHeader.biWidth >= (long)m_Width && abs(vih2->bmiHeader.biHeight) == (long)m_Height) {
			return S_OK;
		}
	}

	return E_INVALIDARG;
}

HRESULT CAviSynthStream::SetMediaType(const CMediaType* pMediaType)
{
	HRESULT hr = __super::SetMediaType(pMediaType);

	if (SUCCEEDED(hr)) {
		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pMediaType->Format();
		m_PitchBuff = m_Format.Packsize * vih2->bmiHeader.biWidth;
		ASSERT(m_PitchBuff >= m_Pitch);
		m_BufferSize = m_PitchBuff * abs(vih2->bmiHeader.biHeight) * m_Format.buffCoeff / 2;

		DLog(L"SetMediaType with subtype %s", GUIDtoWString(m_mt.subtype).c_str());
	}

	return hr;
}

STDMETHODIMP CAviSynthStream::Notify(IBaseFilter* pSender, Quality q)
{
	return E_NOTIMPL;
}
