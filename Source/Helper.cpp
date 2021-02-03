/*
* (C) 2020-2021 see Authors.txt
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
#include "../Include/Version.h"

#ifndef __AVISYNTH_7_H__
#include "../Include/avisynth.h"
#endif
#ifndef VSSCRIPT_H
#include "../Include/VSScript.h"
#endif

#include "Helper.h"



std::wstring GetVersionStr()
{
	std::wstring version = _CRT_WIDE(VERSION_STR);
#if MPCIS_RELEASE != 1
	version += fmt::format(L" (git-{}-{})",
		_CRT_WIDE(_CRT_STRINGIZE(REV_DATE)),
		_CRT_WIDE(_CRT_STRINGIZE(REV_HASH))
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

static const FmtParams_t s_FormatTable[] = {
	// fourcc                   |   subtype                | ASformat                | VSformat      | str    |Packsize|buffCoeff|CDepth|planes|bitCount
	{DWORD(-1),                  GUID_NULL,                 0,                        0,              nullptr,        0, 0,       0,     0,     0},
	// YUV packed
	{FCC('YUY2'),                MEDIASUBTYPE_YUY2,         VideoInfo::CS_YUY2,       pfCompatYUY2,  L"YUY2",         2, 2,       8,     1,     16},
	// YUV planar
	{FCC('YV12'),                MEDIASUBTYPE_YV12,         VideoInfo::CS_I420,       0,             L"I420",         1, 3,       8,     3,     12},
	{FCC('YV12'),                MEDIASUBTYPE_YV12,         VideoInfo::CS_YV12,       pfYUV420P8,    L"YV12",         1, 3,       8,     3,     12},
	{MAKEFOURCC('Y','3',11,10),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV420P10,  pfYUV420P10,   L"YUV420P10",    2, 3,       10,    3,     24},
	{MAKEFOURCC('Y','3',11,12),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV420P12,  pfYUV420P12,   L"YUV420P12",    2, 3,       12,    3,     24},
	{MAKEFOURCC('Y','3',11,14),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV420P14,  pfYUV420P14,   L"YUV420P14",    2, 3,       14,    3,     24},
	{MAKEFOURCC('Y','3',11,16),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV420P16,  pfYUV420P16,   L"YUV420P16",    2, 3,       16,    3,     24},
	{FCC('YV16'),                MEDIASUBTYPE_YV16,         VideoInfo::CS_YV16,       pfYUV422P8,    L"YV16",         1, 4,       8,     3,     16},
	{MAKEFOURCC('Y','3',10,10),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV422P10,  pfYUV422P10,   L"YUV422P10",    2, 4,       10,    3,     32},
	{MAKEFOURCC('Y','3',10,12),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV422P12,  pfYUV422P12,   L"YUV422P12",    2, 4,       12,    3,     32},
	{MAKEFOURCC('Y','3',10,14),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV422P14,  pfYUV422P14,   L"YUV422P14",    2, 4,       14,    3,     32},
	{MAKEFOURCC('Y','3',10,16),  MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV422P16,  pfYUV422P16,   L"YUV422P16",    2, 4,       16,    3,     32},
	{FCC('YV24'),                MEDIASUBTYPE_YV24,         VideoInfo::CS_YV24,       pfYUV444P8,    L"YV24",         1, 6,       8,     3,     24},
	{MAKEFOURCC('Y','3',0,10),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV444P10,  pfYUV444P10,   L"YUV444P10",    2, 6,       10,    3,     48},
	{MAKEFOURCC('Y','3',0,12),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV444P12,  pfYUV444P12,   L"YUV444P12",    2, 6,       12,    3,     48},
	{MAKEFOURCC('Y','3',0,14),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV444P14,  pfYUV444P14,   L"YUV444P14",    2, 6,       14,    3,     48},
	{MAKEFOURCC('Y','3',0,16),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUV444P16,  pfYUV444P16,   L"YUV444P16",    2, 6,       16,    3,     48},
	// YUV planar whith alpha
	{MAKEFOURCC('Y','4',0,8),    MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUVA444,    0,             L"YUVA444P8",    1, 8,       8,     4,     32},
	{MAKEFOURCC('Y','4',0,10),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUVA444P10, 0,             L"YUVA444P10",   2, 8,       10,    4,     64},
	{MAKEFOURCC('Y','4',0,16),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_YUVA444P16, 0,             L"YUVA444P16",   2, 8,       16,    4,     64},
	// RGB packed
	{BI_RGB,                     MEDIASUBTYPE_RGB24,        VideoInfo::CS_BGR24,      0,             L"RGB24",        3, 2,       8,     1,     24},
	{BI_RGB,                     MEDIASUBTYPE_RGB32,        VideoInfo::CS_BGR32,      pfCompatBGR32, L"RGB32",        4, 2,       8,     1,     32},
	{BI_RGB,                     MEDIASUBTYPE_ARGB32,       0,                        0,             L"ARGB32",       4, 2,       8,     1,     32},
	{MAKEFOURCC('B','G','R',48), MEDIASUBTYPE_RGB48,        VideoInfo::CS_BGR48,      0,             L"RGB48",        6, 2,       16,    1,     48},
	{MAKEFOURCC('B','R','A',64), MEDIASUBTYPE_ARGB64,       VideoInfo::CS_BGR64,      0,             L"ARGB64",       8, 2,       16,    1,     64},
	// RGB planar
	{MAKEFOURCC('G','3',0,8),    MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBP,       pfRGB24,       L"RGBP8",        1, 6,       8,     3,     24},
	{MAKEFOURCC('G','3',0,10),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBP10,     pfRGB30,       L"RGBP10",       2, 6,       10,    3,     48},
	{MAKEFOURCC('G','3',0,16),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBP16,     pfRGB48,       L"RGBP16",       2, 6,       16,    3,     48},
	// RGB planar whith alpha
	{MAKEFOURCC('G','4',0,8),    MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBAP,      0,             L"RGBAP8",       1, 8,       8,     4,     32},
	{MAKEFOURCC('G','4',0,10),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBAP10,    0,             L"RGBAP10",      2, 8,       10,    4,     64},
	{MAKEFOURCC('G','4',0,16),   MEDIASUBTYPE_LAV_RAWVIDEO, VideoInfo::CS_RGBAP16,    0,             L"RGBAP16",      2, 8,       16,    4,     64},
	// grayscale
	{FCC('Y800'),                MEDIASUBTYPE_Y800,         VideoInfo::CS_Y8,         pfGray8,       L"Y8",           1, 2,       8,     1,     8},
	{MAKEFOURCC('Y','1',0,16),   MEDIASUBTYPE_Y16,          VideoInfo::CS_Y16,        pfGray16,      L"Y16",          2, 2,       16,    1,     16},
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

std::unique_ptr<BYTE[]> GetBitmapWithText(const std::wstring text, const long width, const long height)
{
	HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE,
		FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		FIXED_PITCH, L"Consolas");
	if (!hFont) {
		return nullptr;
	}

	BOOL ret = 0;
	HDC hDC = CreateCompatibleDC(nullptr);
	SetMapMode(hDC, MM_TEXT);

	HFONT hFontOld = (HFONT)SelectObject(hDC, hFont);

	SIZE size;
	ret = GetTextExtentPoint32W(hDC, L"_", 1, &size);

	// Prepare to create a bitmap
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = width;
	bmi.bmiHeader.biHeight      = -height;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biBitCount    = 32;

	HGDIOBJ hBitmapOld = nullptr;
	void* pBitmapBits = nullptr;
	HBITMAP hBitmap = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, &pBitmapBits, nullptr, 0);
	if (pBitmapBits) {
		HGDIOBJ hBitmapOld = SelectObject(hDC, hBitmap);

		SetTextColor(hDC, RGB(255, 255, 255));
		SetBkColor(hDC, RGB(64, 64, 64));
		RECT rect = {0, 0, width, height};
		HBRUSH hBrush = CreateSolidBrush(RGB(64, 64, 64));
		FillRect(hDC, &rect, hBrush);
		DrawTextW(hDC, text.c_str(), text.size(), &rect, DT_LEFT|DT_WORDBREAK);

		GdiFlush();
		DeleteObject(hBrush);

		const long len = width * height * 4;
		std::unique_ptr<BYTE[]> bitmapData(new(std::nothrow) BYTE[len]);

		if (bitmapData) {
			memcpy(bitmapData.get(), pBitmapBits, len);
		}

		SelectObject(hDC, hBitmapOld);
		DeleteObject(hBitmap);

		SelectObject(hDC, hFontOld);
		DeleteObject(hFont);
		DeleteDC(hDC);

		return bitmapData;
	}

	SelectObject(hDC, hFontOld);
	DeleteObject(hFont);
	DeleteDC(hDC);

	return nullptr;
}
