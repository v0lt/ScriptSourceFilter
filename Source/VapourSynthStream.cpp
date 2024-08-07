/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "stdafx.h"
#include "VUIOptions.h"
#include "ScriptSource.h"

#include "VapourSynthStream.h"

//
// CVapourSynthStream
//

CVapourSynthStream::CVapourSynthStream(const WCHAR* name, CSource* pParent, HRESULT* phr)
	: CSourceStream(name, phr, pParent, L"Output")
	, CSourceSeeking(name, (IPin*)this, phr, &m_cSharedState)
{
	CAutoLock cAutoLock(&m_cSharedState);

	HRESULT hr;
	std::wstring error;

	try {
		m_hVSScriptDll = LoadLibraryW(L"vsscript.dll");
		if (!m_hVSScriptDll) {
			throw std::exception("Failed to load VapourSynt");
		}

		struct extfunc {
			void** adress;
			const char* name;
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
				throw std::exception(std::format("Cannot resolve VapourSynth {} function", vsfunc.name).c_str());
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
			throw std::exception(std::format("Unsuported pixel type {}", m_vsInfo->format->name).c_str());
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
		UINT color_info = 0;

		int k = m_vsInfo->format->id / cmGray;
		if (k == (cmYUV / cmGray)) {
			if (m_Format.CDepth == 8) {
				// swap U and V for YV12, YV16, YV24.
				m_Planes[0] = 0;
				m_Planes[1] = 2;
				m_Planes[2] = 1;
			} else {
				m_Planes[0] = 0;
				m_Planes[1] = 1;
				m_Planes[2] = 2;
			}
		}
		else if (k == (cmRGB / cmGray)) {
			// planar RGB
			m_Planes[0] = 1;
			m_Planes[1] = 2;
			m_Planes[2] = 0;
		}

		std::wstring streamInfo = std::format(
			L"Script type : VapourSynth\n"
			L"Video stream: {} {}x{} {:.3f} fps",
			m_Format.str, m_Width, m_Height, (double)m_fpsNum / m_fpsDen
		);

		const VSMap* vsMap = m_vsAPI->getFramePropsRO(frame);
		if (vsMap) {
			int numKeys = m_vsAPI->propNumKeys(vsMap);
			if (numKeys > 0) {
				streamInfo += std::format(L"\nProperties [{}]:", numKeys);
			}

			for (int i = 0; i < numKeys; i++) {
				const char* keyName = m_vsAPI->propGetKey(vsMap, i);
				if (keyName) {
					int64_t val_Int = 0;
					double val_Float = 0;
					const char* val_Data = 0;
					int err = 0;
					const char keyType = m_vsAPI->propGetType(vsMap, keyName);

					streamInfo += std::format(L"\n{:>2}: <{}> '{}'", i, keyType, A2WStr(keyName));

					switch (keyType) {
					case ptInt:
						val_Int = m_vsAPI->propGetInt(vsMap, keyName, 0, &err);
						if (!err) {
							streamInfo += std::format(L" = {}", val_Int);
							streamInfo += std::format(L" = {}", val_Int);
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
						val_Float = m_vsAPI->propGetFloat(vsMap, keyName, 0, &err);
						if (!err) {
							streamInfo += std::format(L" = {:.3f}", val_Float);
						}
						break;
					case ptData:
						val_Data = m_vsAPI->propGetData(vsMap, keyName, 0, &err);
						if (!err) {
							const int dataSize = m_vsAPI->propGetDataSize(vsMap, keyName, 0, &err);
							if (!err) {
								if (dataSize == 1 && strcmp(keyName, "_PictType") == 0) {
									streamInfo += std::format(L" = {}", val_Data[0]);
								}
								else {
									streamInfo += std::format(L", {} bytes", dataSize);
								}
							}
						}
						break;
					}
				}
			}
		}

		SetColorInfoFromVUIOptions(color_info, name);
		if (color_info) {
			m_ColorInfo = color_info | (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT);
		}

		DLog(streamInfo);
		static_cast<CScriptSource*>(pParent)->m_StreamInfo = streamInfo;

		hr = S_OK;
	}
	catch (const std::exception& e) {
		VapourSynthFree();
		DLog(L"{}\n{}", A2WStr(e.what()), error);

		m_Format     = GetFormatParamsVapourSynth(pfCompatBGR32);

		m_Width      = 640;
		m_Height     = 360;
		m_Pitch      = m_Width * 4;
		m_PitchBuff  = m_Pitch;
		m_BufferSize = m_PitchBuff * m_Height * m_Format.buffCoeff / 2;
		m_Sar = {};

		m_fpsNum     = 1;
		m_fpsDen     = 1;
		m_NumFrames  = 10;
		m_AvgTimePerFrame = UNITS;
		m_rtDuration = m_rtStop = UNITS * m_NumFrames;

		std::wstring str = A2WStr(e.what()) + L"\n\n" + error;
		m_BitmapError = GetBitmapWithText(str, m_Width, m_Height);

		if (m_BitmapError) {
			hr = S_FALSE;
		} else {
			hr = E_FAIL;
		}
	}

	if (SUCCEEDED(hr)) {
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

	*phr = hr;
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
			const VSFrameRef* frame = m_vsAPI->getFrame(m_CurrentFrame, m_vsNode, m_vsErrorMessage, sizeof(m_vsErrorMessage));
			if (!frame) {
				DLog(ConvertUtf8ToWide(m_vsErrorMessage));
				return E_FAIL;
			}

			const int num_planes = m_vsInfo->format->numPlanes;
			for (int i = 0; i < num_planes; i++) {
				const int plane = m_Planes[i];
				const BYTE* src_data = m_vsAPI->getReadPtr(frame, plane);
				int src_pitch = m_vsAPI->getStride(frame, plane);
				const UINT height = m_vsAPI->getFrameHeight(frame, plane);
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

		DLog(L"SetMediaType with subtype {}", GUIDtoWString(m_mt.subtype));
	}

	return hr;
}

STDMETHODIMP CVapourSynthStream::Notify(IBaseFilter* pSender, Quality q)
{
	return E_NOTIMPL;
}
