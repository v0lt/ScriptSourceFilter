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

#pragma once

#ifndef FCC
#define FCC(ch4) ((((DWORD)(ch4) & 0xFF) << 24) |     \
                  (((DWORD)(ch4) & 0xFF00) << 8) |    \
                  (((DWORD)(ch4) & 0xFF0000) >> 8) |  \
                  (((DWORD)(ch4) & 0xFF000000) >> 24))
#endif

#ifdef _DEBUG
#define DLog(...) DbgLogInfo(LOG_TRACE, 3, __VA_ARGS__)
#define DLogIf(f,...) {if (f) DbgLogInfo(LOG_TRACE, 3, __VA_ARGS__);}
#define DLogError(...) DbgLogInfo(LOG_ERROR, 3, __VA_ARGS__)
#else
#define DLog(...) __noop
#define DLogIf(f,...) __noop
#define DLogError(...) __noop
#endif

#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p) = nullptr; } }
#define SAFE_CLOSE_HANDLE(p) { if (p) { if ((p) != INVALID_HANDLE_VALUE) ASSERT(CloseHandle(p)); (p) = nullptr; } }
#define SAFE_DELETE(p)       { if (p) { delete (p); (p) = nullptr; } }

#define QI(i) (riid == __uuidof(i)) ? GetInterface((i*)this, ppv) :

#define ALIGN(x, a)           __ALIGN_MASK(x,(decltype(x))(a)-1)
#define __ALIGN_MASK(x, mask) (((x)+(mask))&~(mask))

DEFINE_GUID(MEDIASUBTYPE_Y8,           0x20203859, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(MEDIASUBTYPE_Y16,          0x10003159, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71); // Y1[0][16]
DEFINE_GUID(MEDIASUBTYPE_YV16,         0x36315659, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(MEDIASUBTYPE_YV24,         0x34325659, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(MEDIASUBTYPE_RGB48,        0x30424752, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71); // BGR[48] (BGR0)
DEFINE_GUID(MEDIASUBTYPE_ARGB64,       0x40415242, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71); // BRA[64] (BRA@)
DEFINE_GUID(MEDIASUBTYPE_LAV_RAWVIDEO, 0xd80fa03c, 0x35c1, 0x4fa1, 0x8c, 0x8e, 0x37, 0x5c, 0x86, 0x67, 0x16, 0x6e);

template <typename T>
// If the specified value is out of range, set to default values.
inline T discard(T const& val, T const& def, T const& lo, T const& hi)
{
	return (val > hi || val < lo) ? def : val;
}

template <typename T>
inline T round_pow2(T number, T pow2)
{
	ASSERT(pow2 > 0);
	ASSERT(!(pow2 & (pow2 - 1)));
	--pow2;
	if (number < 0) {
		return (number - pow2) & ~pow2;
	} else {
		return (number + pow2) & ~pow2;
	}
}

LPCWSTR GetWindowsVersion();
LPCWSTR GetNameAndVersion();

inline std::wstring GUIDtoWString(const GUID& guid)
{
	WCHAR buff[40];
	if (StringFromGUID2(guid, buff, 39) <= 0) {
		StringFromGUID2(GUID_NULL, buff, 39);
	}
	return std::wstring(buff);
}

std::wstring HR2Str(const HRESULT hr);

void CopyFrameAsIs(const UINT height, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);

HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid);

struct FmtParams_t {
	DWORD       fourcc;
	GUID        subtype;
	int         ASformat;
	int         VSformat;
	const char* str;
	int         Packsize;
	int         buffCoeff;
	int         CDepth;
	int         planes;
};

const FmtParams_t& GetFormatParamsAviSynth(const int asFormat);
const FmtParams_t& GetFormatParamsVapourSynth(const int vsFormat);
