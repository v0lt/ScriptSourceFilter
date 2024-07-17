/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

#include "Utils/Util.h"
#include "Utils/StringUtil.h"

LPCWSTR GetNameAndVersion();

struct FmtParams_t {
	DWORD          fourcc;
	GUID           subtype;
	int            ASformat;
	int            VSformat;
	const wchar_t* str;
	int            Packsize;
	int            buffCoeff;
	int            CDepth;
	int            planes;
	int            bitCount;
};

const FmtParams_t& GetFormatParamsAviSynth(const int asFormat);
const FmtParams_t& GetFormatParamsVapourSynth(const int vsFormat);

std::unique_ptr<BYTE[]> GetBitmapWithText(const std::wstring text, const long width, const long height);
