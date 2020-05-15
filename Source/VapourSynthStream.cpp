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

#include "StringHelper.h"

#include "VapourSynthStream.h"


//
// CVapourSynthStream
//

CVapourSynthStream::CVapourSynthStream(const WCHAR* name, CSource* pParent, HRESULT* phr)
	: CSourceStream(name, phr, pParent, L"Output")
	, CSourceSeeking(name, (IPin*)this, phr, &m_cSharedState)
{
	CAutoLock cAutoLock(&m_cSharedState);

	std::wstring error;

	try {
		m_hVSScriptDll = LoadLibraryW(L"vsscript.dll");
		if (!m_hVSScriptDll) {
			throw std::exception("Failed to load VapourSynt");
		}

		struct extfunc {
			void** adress;
			char* name;
		};
#ifdef _WIN64
		extfunc vsfuncs[] = {
			{(void**)&vs_init,           "vsscript_init"          },
			{(void**)&vs_finalize,       "vsscript_finalize"      },
			{(void**)&vs_evaluateScript, "vsscript_evaluateScript"},
			{(void**)&vs_evaluateFile,   "vsscript_evaluateFile"  },
			{(void**)&vs_freeScript,     "vsscript_freeScript"    },
			{(void**)&vs_getError,       "vsscript_getError"      },
			{(void**)&vs_getOutput,      "vsscript_getOutput"     },
			{(void**)&vs_clearOutput,    "vsscript_clearOutput"   },
			{(void**)&vs_getCore,        "vsscript_getCore"       },
			{(void**)&vs_getVSApi,       "vsscript_getVSApi"      }
		};
#else
		extfunc vsfuncs[] = {
			// TODO
			{(void**)&vs_init,           "_vsscript_init@0"           },
			{(void**)&vs_finalize,       "_vsscript_finalize@0"       },
			{(void**)&vs_evaluateScript, "_vsscript_evaluateScript@16"},
			{(void**)&vs_evaluateFile,   "_vsscript_evaluateFile@12"  },
			{(void**)&vs_freeScript,     "_vsscript_freeScript@4"     },
			{(void**)&vs_getError,       "_vsscript_getError@4"       },
			{(void**)&vs_getOutput,      "_vsscript_getOutput@8"      },
			{(void**)&vs_clearOutput,    "_vsscript_clearOutput@8"    },
			{(void**)&vs_getCore,        "_vsscript_getCore@4"        },
			{(void**)&vs_getVSApi,       "_vsscript_getVSApi@0"       }
		};
#endif
		for (auto& vsfunc : vsfuncs) {
			*(vsfunc.adress) = GetProcAddress(m_hVSScriptDll, vsfunc.name);
			if (nullptr == *(vsfunc.adress)) {
				throw std::exception(fmt::format("Cannot resolve VapourSynth {} function", vsfunc.name).c_str());
			}
		}

		m_vsInit = vs_init();
		if (!m_vsInit) {
			throw std::exception("Failed to initialize VapourSynth");
		}

		m_vsAPI = vs_getVSApi();
		if (!m_vsAPI) {
			throw std::exception("Failed to call VapourSynth vsscript_getVSApi");
		}

		std::string utf8file = ConvertWideToUtf8(name);
		if (vs_evaluateFile(&m_vsScript, utf8file.c_str(), 0)) {
			error = ConvertUtf8ToWide(vs_getError(m_vsScript));
			throw std::exception("Failed to call VapourSynth vsscript_evaluateFile");
		}

		m_vsNode = vs_getOutput(m_vsScript, 0);
		if (!m_vsNode) {
			throw std::exception("Failed to get VapourSynth output");
		}

		m_vsInfo = m_vsAPI->getVideoInfo(m_vsNode);
		if (!m_vsInfo) {
			throw std::exception("Failed to get VapourSynth info");
		}

		m_Format = GetFormatParamsVapourSynth(m_vsInfo->format->id);

		if (m_Format.fourcc == DWORD(-1) || m_Format.planes != m_vsInfo->format->numPlanes) {
			throw std::exception(fmt::format("Unsuported pixel type {}", m_vsInfo->format->name).c_str());
		}

		const VSFrameRef* frame = m_vsAPI->getFrame(0, m_vsNode, m_vsErrorMessage, sizeof(m_vsErrorMessage));
		if (!frame) {
			error = ConvertUtf8ToWide(m_vsErrorMessage);
			throw std::exception("Failed to call getFrame(0)");
		}
		m_Pitch = m_vsAPI->getStride(frame, 0);
		m_vsAPI->freeFrame(frame);

		m_Width      = m_vsInfo->width;
		m_Height     = m_vsInfo->height;
		m_PitchBuff  = m_Pitch;
		m_BufferSize = m_PitchBuff * m_Height * m_Format.buffCoeff / 2;

		m_fpsNum     = m_vsInfo->fpsNum;
		m_fpsDen     = m_vsInfo->fpsDen;
		m_NumFrames  = m_vsInfo->numFrames;
		m_AvgTimePerFrame = llMulDiv(UNITS, m_fpsDen, m_fpsNum, 0);
		m_rtDuration = m_rtStop = llMulDiv(UNITS * m_NumFrames, m_fpsDen, m_fpsNum, 0);

		int k = m_vsInfo->format->id / cmGray;
		if (k == (cmYUV / cmGray)) {
			// swap U and V for YV12, YV16, YV24.
			m_Planes[0] = 0;
			m_Planes[1] = 2;
			m_Planes[2] = 1;
		}
		else if (k == (cmRGB / cmGray)) {
			// planar RGB
			m_Planes[0] = 1;
			m_Planes[1] = 2;
			m_Planes[2] = 0;
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
		vih2->rcSource = { 0, 0, (long)m_Width, (long)m_Height };
		vih2->rcTarget = vih2->rcSource;
		vih2->AvgTimePerFrame         = m_AvgTimePerFrame;
		vih2->bmiHeader.biSize        = sizeof(vih2->bmiHeader);
		vih2->bmiHeader.biWidth       = m_PitchBuff / m_Format.Packsize;
		vih2->bmiHeader.biHeight      = (m_Format.fourcc == BI_RGB) ? -(long)m_Height : m_Height;
		vih2->bmiHeader.biPlanes      = 1;
		vih2->bmiHeader.biBitCount    = m_Format.CDepth * m_Format.buffCoeff / 2;
		vih2->bmiHeader.biCompression = m_Format.fourcc;
		vih2->bmiHeader.biSizeImage   = m_BufferSize;

		*phr = S_OK;
	}
	catch (const std::exception& e) {
		VapourSynthFree();
		DLog(L"%S\n%s", e.what(), error.c_str());
		*phr = E_FAIL;
	}
}

CVapourSynthStream::~CVapourSynthStream()
{
	CAutoLock cAutoLock(&m_cSharedState);

	if (m_hVSScriptDll) {
		FreeLibrary(m_hVSScriptDll);
	}
}

void CVapourSynthStream::VapourSynthFree()
{
	if (m_vsAPI) {
		if (m_vsFrame) {
			m_vsAPI->freeFrame(m_vsFrame);
			m_vsFrame = nullptr;
		}

		if (m_vsNode) {
			m_vsAPI->freeNode(m_vsNode);
			m_vsNode = nullptr;
		}
	}

	if (m_vsScript) {
		vs_freeScript(m_vsScript);
		m_vsScript = nullptr;
	}

	if (m_vsInit) {
		vs_finalize();
		m_vsInit = 0;
	}
}

STDMETHODIMP CVapourSynthStream::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return (riid == IID_IMediaSeeking) ? CSourceSeeking::NonDelegatingQueryInterface(riid, ppv)
		: CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

void CVapourSynthStream::UpdateFromSeek()
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

HRESULT CVapourSynthStream::SetRate(double dRate)
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

HRESULT CVapourSynthStream::OnThreadStartPlay()
{
	m_bDiscontinuity = TRUE;
	return DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
}

HRESULT CVapourSynthStream::ChangeStart()
{
	{
		CAutoLock lock(CSourceSeeking::m_pLock);
		m_FrameCounter = 0;
		m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CVapourSynthStream::ChangeStop()
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

HRESULT CVapourSynthStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_FrameCounter = 0;
	m_CurrentFrame = (int)llMulDiv(m_rtStart, m_fpsNum, m_fpsDen * UNITS, 0); // round down

	return CSourceStream::OnThreadCreate();
}

HRESULT CVapourSynthStream::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties)
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

HRESULT CVapourSynthStream::FillBuffer(IMediaSample* pSample)
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

		const VSFrameRef* frame = m_vsAPI->getFrame(m_CurrentFrame, m_vsNode, m_vsErrorMessage, sizeof(m_vsErrorMessage));
		if (!frame) {
			std::wstring error = ConvertUtf8ToWide(m_vsErrorMessage);
			DLog(error.c_str());
			return E_FAIL;
		}

		UINT DataLength = 0;
		const int num_planes = m_vsInfo->format->numPlanes;
		for (int i = 0; i < num_planes; i++) {
			const int plane = m_Planes[i];
			const BYTE* src_data = m_vsAPI->getReadPtr(frame, plane);
			int src_pitch = m_vsAPI->getStride(frame, plane);
			const UINT height    = m_vsAPI->getFrameHeight(frame, plane);
			UINT dst_pitch = m_PitchBuff;
			if (i > 0) {
				dst_pitch >>= m_vsInfo->format->subSamplingW;
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

HRESULT CVapourSynthStream::GetMediaType(int iPosition, CMediaType* pmt)
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

HRESULT CVapourSynthStream::CheckMediaType(const CMediaType* pmt)
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

HRESULT CVapourSynthStream::SetMediaType(const CMediaType* pMediaType)
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

STDMETHODIMP CVapourSynthStream::Notify(IBaseFilter* pSender, Quality q)
{
	return E_NOTIMPL;
}
