/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "stdafx.h"

#include "../Include/Version.h"
#include "PropPage.h"
#include "VapourSynthStream.h"

#include "ScriptSource.h"

#define OPT_REGKEY_ScriptSource L"Software\\MPC-BE Filters\\MPC Script Source"

//
// CScriptSource
//

CScriptSource::CScriptSource(LPUNKNOWN lpunk, HRESULT* phr)
	: CSource(L"MPC Script Source", lpunk, __uuidof(this))
{
#ifdef _DEBUG
	DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
	DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
#endif

	DLog(L"Windows {}", GetWindowsVersion());
	DLog(GetNameAndVersion());

	HRESULT hr = S_OK;

	if (phr) {
		*phr = hr;
	}

	return;
}

CScriptSource::~CScriptSource()
{
	DLog(L"~CScriptSource()");
}

STDMETHODIMP CScriptSource::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	return
		QI(IFileSourceFilter)
		QI(IAMFilterMiscFlags)
		QI(ISpecifyPropertyPages)
		QI(IScriptSource)
		QI(IExFilterConfig)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// IFileSourceFilter

STDMETHODIMP CScriptSource::Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt)
{
	// TODO: destroy any already existing pins and create new, now we are just going die nicely instead of doing it :)
	if (GetPinCount() > 0) {
		return VFW_E_ALREADY_CONNECTED;
	}

	std::wstring fn = pszFileName;
	std::wstring ext = fn.substr(fn.find_last_of('.'));
	str_tolower(ext);

	HRESULT hr = S_OK;
	if (ext == L".avs") {
		m_pAviSynthFile.reset(new(std::nothrow) CAviSynthFile(pszFileName, this, &hr));
	}
	else if (ext == L".vpy") {
		if (!(new(std::nothrow) CVapourSynthStream(pszFileName, this, &hr))) {
			return E_OUTOFMEMORY;
		}
	}
	else {
		return E_INVALIDARG;
	}

	if (FAILED(hr)) {
		return hr;
	}

	m_fn = fn;

	return S_OK;
}

STDMETHODIMP CScriptSource::GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt)
{
	CheckPointer(ppszFileName, E_POINTER);

	size_t nCount = m_fn.size() + 1;
	*ppszFileName = (LPOLESTR)CoTaskMemAlloc(nCount * sizeof(WCHAR));
	if (!(*ppszFileName)) {
		return E_OUTOFMEMORY;
	}

	wcscpy_s(*ppszFileName, nCount, m_fn.c_str());

	return S_OK;
}

// IAMFilterMiscFlags

STDMETHODIMP_(ULONG) CScriptSource::GetMiscFlags()
{
	return AM_FILTER_MISC_FLAGS_IS_SOURCE;
}

// ISpecifyPropertyPages
STDMETHODIMP CScriptSource::GetPages(CAUUID* pPages)
{
	CheckPointer(pPages, E_POINTER);

	pPages->cElems = 1;
	pPages->pElems = reinterpret_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID) * pPages->cElems));
	if (pPages->pElems == nullptr) {
		return E_OUTOFMEMORY;
	}

	pPages->pElems[0] = __uuidof(CSSInfoPPage);

	return S_OK;
}

// IScriptSource

STDMETHODIMP_(bool) CScriptSource::GetActive()
{
	return (GetPinCount() > 0);
}

STDMETHODIMP CScriptSource::GetScriptInfo(std::wstring& str)
{
	if (GetActive()) {
		str.assign(m_StreamInfo);
		return S_OK;
	} else {
		str.assign(L"filter is not active");
		return S_FALSE;
	}
}

// IExFilterConfig

STDMETHODIMP CScriptSource::Flt_GetInt64(LPCSTR field, __int64 *value)
{
	CheckPointer(value, E_POINTER);

	if (!strcmp(field, "version")) {
		*value  = ((uint64_t)VER_MAJOR << 48)
				| ((uint64_t)VER_MINOR << 32)
				| ((uint64_t)VER_BUILD << 16)
				| ((uint64_t)REV_NUM);
		return S_OK;
	}

	return E_INVALIDARG;
}
