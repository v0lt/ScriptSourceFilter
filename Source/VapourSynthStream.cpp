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

		auto FmtParams = GetFormatParamsAS(m_vsInfo->format->id);

		if (FmtParams.cformat == CF_NONE) {
			throw std::exception(fmt::format("Unsuported pixel type {}", m_vsInfo->format->name).c_str());
		}

		m_subtype = FmtParams.Subtype;
		DWORD fourcc = (m_subtype == MEDIASUBTYPE_RGB24 || m_subtype == MEDIASUBTYPE_RGB32 || m_subtype == MEDIASUBTYPE_ARGB32)
			? BI_RGB : m_subtype.Data1;

		const VSFrameRef* frame = m_vsAPI->getFrame(0, m_vsNode, m_vsErrorMessage, sizeof(m_vsErrorMessage));
		if (!frame) {
			error = ConvertUtf8ToWide(m_vsErrorMessage);
			throw std::exception("Failed to call getFrame(0)");
		}
		const UINT pitch = m_vsAPI->getStride(frame, 0);
		m_vsAPI->freeFrame(frame);

		m_Width       = m_vsInfo->width;
		m_Height      = m_vsInfo->height;
		m_NumFrames   = m_vsInfo->numFrames;
		m_fpsNum      = m_vsInfo->fpsNum;
		m_fpsDen      = m_vsInfo->fpsDen;
		m_BufferSize  = pitch * m_Height;
		m_AvgTimePerFrame = UNITS * m_fpsDen / m_fpsNum;
		m_rtDuration  = m_rtStop = UNITS * m_NumFrames * m_fpsNum / m_fpsDen;

		DLog(L"Open clip %S %dx%d %.03f fps", FmtParams.str, m_Width, m_Height, (double)m_fpsNum/m_fpsDen);

		m_mt.InitMediaType();
		m_mt.SetType(&MEDIATYPE_Video);
		m_mt.SetSubtype(&m_subtype);
		m_mt.SetFormatType(&FORMAT_VideoInfo2);
		m_mt.SetTemporalCompression(FALSE);
		m_mt.SetSampleSize(m_BufferSize);

		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)m_mt.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2));
		memset(vih2, 0, sizeof(VIDEOINFOHEADER2));
		vih2->rcSource = { 0, 0, (long)m_Width, (long)m_Height };
		vih2->rcTarget = vih2->rcSource;
		vih2->AvgTimePerFrame         = m_AvgTimePerFrame;
		vih2->bmiHeader.biSize        = sizeof(vih2->bmiHeader);
		vih2->bmiHeader.biWidth       = pitch / m_vsInfo->format->bytesPerSample;
		vih2->bmiHeader.biHeight      = m_Height;
		vih2->bmiHeader.biPlanes      = 1;
		vih2->bmiHeader.biBitCount    = m_vsInfo->format->bitsPerSample;
		vih2->bmiHeader.biCompression = fourcc;
		vih2->bmiHeader.biSizeImage   = m_BufferSize;

		*phr = S_OK;
	}
	catch (std::exception& e) {
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
		m_rtSampleTime = 0;
		m_rtPosition = m_rtStart;
	}

	UpdateFromSeek();

	return S_OK;
}

HRESULT CVapourSynthStream::ChangeStop()
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

HRESULT CVapourSynthStream::OnThreadCreate()
{
	CAutoLock cAutoLockShared(&m_cSharedState);

	m_rtSampleTime = 0;
	m_rtPosition = m_rtStart;

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

		if (m_rtPosition >= m_rtStop) {
			return S_FALSE;
		}

		int framenum = (int)(m_rtPosition * m_fpsNum / (m_fpsDen * UNITS));
		const VSFrameRef* frame = m_vsAPI->getFrame(framenum, m_vsNode, m_vsErrorMessage, sizeof(m_vsErrorMessage));
		if (!frame) {
			std::wstring error = ConvertUtf8ToWide(m_vsErrorMessage);
			DLog(error.c_str());
			return E_FAIL;
		}

		const BYTE* src_data = m_vsAPI->getReadPtr(frame, 0);
		if (!src_data) {
			DLog(L"VapourSynthServer m_vsAPI->getReadPtr returned NULL");
			return E_FAIL;
		}

		const UINT src_pitch = m_vsAPI->getStride(frame, 0);

		if (m_vsFrame) {
			m_vsAPI->freeFrame(m_vsFrame);
		}
		m_vsFrame = frame;

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
		&& pmt->subtype == m_subtype
		&& pmt->formattype == FORMAT_VideoInfo2) {

		VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->Format();
		if (vih2->bmiHeader.biWidth >= (long)m_Width && vih2->bmiHeader.biHeight == m_Height) {
			return S_OK;
		}
	}

	return E_INVALIDARG;
}

HRESULT CVapourSynthStream::SetMediaType(const CMediaType* pMediaType)
{
	HRESULT hr = __super::SetMediaType(pMediaType);

	DLogIf(SUCCEEDED(hr), L"SetMediaType with subtype %s", GUIDtoWString(m_mt.subtype).c_str());

	return hr;
}

STDMETHODIMP CVapourSynthStream::Notify(IBaseFilter* pSender, Quality q)
{
	return E_NOTIMPL;
}
