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
#include <memory>
#include "../Include/Version.h"
#include "Helper.h"

#ifndef __AVISYNTH_7_H__
#include "../Include/avisynth.h"
#endif
#ifndef VSSCRIPT_H
#include "../Include/VSScript.h"
#endif


// missing GUIDs for Win8.1 SDK
//DEFINE_GUID(GUID_ContainerFormatAdng, 0xf3ff6d0d, 0x38c0, 0x41c4, 0xb1, 0xfe, 0x1f, 0x38, 0x24, 0xf1, 0x7b, 0x84);
//DEFINE_GUID(GUID_ContainerFormatHeif, 0xe1e62521, 0x6787, 0x405b, 0xa3, 0x39, 0x50, 0x07, 0x15, 0xb5, 0x76, 0x3f);
//DEFINE_GUID(GUID_ContainerFormatWebp, 0xe094b0e2, 0x67f2, 0x45b3, 0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3);
//DEFINE_GUID(GUID_ContainerFormatRaw, 0xfe99ce60, 0xf19c, 0x433c, 0xa3, 0xae, 0x00, 0xac, 0xef, 0xa9, 0xca, 0x21);

#ifndef _WIN32_WINNT_WINTHRESHOLD
#define _WIN32_WINNT_WINTHRESHOLD 0x0A00
VERSIONHELPERAPI IsWindows10OrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WINTHRESHOLD), LOBYTE(_WIN32_WINNT_WINTHRESHOLD), 0);
}
#endif

LPCWSTR GetWindowsVersion()
{
	if (IsWindows10OrGreater()) {
		return L"10";
	}
	else if (IsWindows8Point1OrGreater()) {
		return L"8.1";
	}
	else if (IsWindows8OrGreater()) {
		return L"8";
	}
	else if (IsWindows7SP1OrGreater()) {
		return L"7 SP1";
	}
	else if (IsWindows7OrGreater()) {
		return L"7";
	}
	return L"Vista or older";
}

std::wstring GetVersionStr()
{
	std::wstring version = _CRT_WIDE(MPCSS_VERSION_STR);
#if MPCIS_RELEASE != 1
	version += fmt::format(L" (git-{}-{})",
		_CRT_WIDE(_CRT_STRINGIZE(MPCSS_REV_DATE)),
		_CRT_WIDE(_CRT_STRINGIZE(MPCSS_REV_HASH))
	);
#endif
#ifdef _WIN64
	version.append(L" x64");
#endif
#ifdef _DEBUG
	version.append(L" DEBUG");
#endif
	return version;
}

LPCWSTR GetNameAndVersion()
{
	static std::wstring version = L"MPC Script Source " + GetVersionStr();

	return version.c_str();
}

std::wstring HR2Str(const HRESULT hr)
{
	std::wstring str;
#define UNPACK_VALUE(VALUE) case VALUE: str = L#VALUE; break;
#define UNPACK_HR_WIN32(VALUE) case (((VALUE) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000): str = L#VALUE; break;
	switch (hr) {
		// common HRESULT values https://docs.microsoft.com/en-us/windows/desktop/seccrypto/common-hresult-values
		UNPACK_VALUE(S_OK);
		UNPACK_VALUE(S_FALSE);
		UNPACK_VALUE(E_NOTIMPL);
		UNPACK_VALUE(E_NOINTERFACE);
		UNPACK_VALUE(E_POINTER);
		UNPACK_VALUE(E_ABORT);
		UNPACK_VALUE(E_FAIL);
		UNPACK_VALUE(E_UNEXPECTED);
		UNPACK_VALUE(E_ACCESSDENIED);
		UNPACK_VALUE(E_HANDLE);
		UNPACK_VALUE(E_OUTOFMEMORY);
		UNPACK_VALUE(E_INVALIDARG);
		// some System Error Codes
		UNPACK_HR_WIN32(ERROR_INVALID_WINDOW_HANDLE);
	default:
		str = fmt::format(L"{:#010x}", hr);
	};
#undef UNPACK_VALUE
#undef UNPACK_HR_WIN32
	return str;
}

void CopyFrameAsIs(const UINT height, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch)
{
	if (dst_pitch == src_pitch) {
		memcpy(dst, src, dst_pitch * height);
		return;
	}

	const UINT linesize = std::min((UINT)abs(src_pitch), dst_pitch);

	for (UINT y = 0; y < height; ++y) {
		memcpy(dst, src, linesize);
		src += src_pitch;
		dst += dst_pitch;
	}
}

HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid)
{
	static const HMODULE hModule = (HMODULE)&__ImageBase;

	HRSRC hrsrc = FindResourceW(hModule, MAKEINTRESOURCEW(resid), L"FILE");
	if (!hrsrc) {
		return E_INVALIDARG;
	}
	HGLOBAL hGlobal = LoadResource(hModule, hrsrc);
	if (!hGlobal) {
		return E_FAIL;
	}
	size = SizeofResource(hModule, hrsrc);
	if (!size) {
		return E_FAIL;
	}
	data = LockResource(hGlobal);
	if (!data) {
		return E_FAIL;
	}

	return S_OK;
}

static const FmtParams_t s_FormatTable[] = {
	// fourcc                   |   subtype                | ASformat               | VSformat     | str    |Packsize|buffCoeff|CDepth|planes
	{DWORD(-1),                  GUID_NULL,                 0,                       0,             nullptr,        0, 0,       0,     0},
	// YUV packed
	{FCC('YUY2'),                MEDIASUBTYPE_YUY2,         VideoInfo::CS_YUY2,      pfCompatYUY2,  "YUY2",         2, 2,       8,     1},
	// YUV planar
	{FCC('YV12'),                MEDIASUBTYPE_YV12,         VideoInfo::CS_I420,      0,             "I420",         1, 3,       8,     3}, // for tests
	{FCC('YV12'),                MEDIASUBTYPE_YV12,         VideoInfo::CS_YV12,      pfYUV420P8,    "YV12",         1, 3,       8,     3},
	{FCC('YV16'),                MEDIASUBTYPE_YV16,         VideoInfo::CS_YV16,      pfYUV422P8,    "YV16",         1, 4,       8,     3},
	{FCC('YV24'),                MEDIASUBTYPE_YV24,         VideoInfo::CS_YV24,      pfYUV444P8,    "YV24",         1, 6,       8,     3},
	{MAKEFOURCC('Y','3',11,10),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV420P10, pfYUV420P10,   "YUV420P16",    2, 3,       10,    3},
	{MAKEFOURCC('Y','3',11,16),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV420P16, pfYUV420P16,   "YUV420P16",    2, 3,       16,    3},
	// RGB packed
	{BI_RGB,                     MEDIASUBTYPE_RGB24,        VideoInfo::CS_BGR24,     0,             "RGB24",        3, 2,       8,     1},
	{BI_RGB,                     MEDIASUBTYPE_RGB32,        VideoInfo::CS_BGR32,     pfCompatBGR32, "RGB32",        4, 2,       8,     1},
	{BI_RGB,                     MEDIASUBTYPE_ARGB32,       0,                       0,             "ARGB32",       4, 2,       8,     1},
	{MAKEFOURCC('B','G','R',48), MEDIASUBTYPE_RGB48,        VideoInfo::CS_BGR48,     0,             "RGB48",        6, 2,       16,    1},
	{MAKEFOURCC('B','R','A',64), MEDIASUBTYPE_ARGB64,       VideoInfo::CS_BGR64,     0,             "ARGB64",       8, 2,       16,    1},
	// RGB planar
	{MAKEFOURCC('G','3',0,8),    MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBP,      pfRGB24,       "RGBP24",       1, 6,       8,     3},
	{MAKEFOURCC('G','3',0,16),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBP16,    pfRGB48,       "RGBP48",       2, 6,       16,    3},
	// grayscale
	{FCC('Y8  '),                MEDIASUBTYPE_Y8,           VideoInfo::CS_Y8,        pfGray8,       "Y8",           1, 2,       8,     1},
	{MAKEFOURCC('Y','1',0,16),   MEDIASUBTYPE_Y16,          VideoInfo::CS_Y16,       pfGray16,      "Y16",          2, 2,       16,    1},
};

const FmtParams_t& GetFormatParamsAviSynth(const int asFormat)
{
	for (const auto& f : s_FormatTable) {
		if (f.ASformat == asFormat) {
			return f;
		}
	}
	return s_FormatTable[0];
}

const FmtParams_t& GetFormatParamsVapourSynth(const int vsFormat)
{
	for (const auto& f : s_FormatTable) {
		if (f.VSformat == vsFormat) {
			return f;
		}
	}
	return s_FormatTable[0];
}
