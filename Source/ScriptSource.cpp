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
#include "PropPage.h"
#include "AviSynthStream.h"
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
		if (!(new CAviSynthStream(pszFileName, this, &hr))) {
			return E_OUTOFMEMORY;
		}
	}
	else if (ext == L".vpy") {
		if (!(new CVapourSynthStream(pszFileName, this, &hr))) {
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

	pPages->pElems[0] = __uuidof(CSSMainPPage);

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

STDMETHODIMP CScriptSource::GetInt64(LPCSTR field, __int64 *value)
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
