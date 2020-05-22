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

#include "Utils/Util.h"
#include "Utils/StringUtil.h"

LPCWSTR GetNameAndVersion();

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
	int         bitCount;
};

const FmtParams_t& GetFormatParamsAviSynth(const int asFormat);
const FmtParams_t& GetFormatParamsVapourSynth(const int vsFormat);

std::unique_ptr<BYTE[]> GetBitmapWithText(const std::wstring text, const long width, const long height);
