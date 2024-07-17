/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

#include <atltypes.h>
#include <thread>
#include "Helper.h"
#include "IScriptSource.h"
#include "../Include/FilterInterfacesImpl.h"

#define STR_CLSID_ScriptSource "{7D3BBD5A-880D-4A30-A2D1-7B8C2741AFEF}"

class __declspec(uuid(STR_CLSID_ScriptSource))
	CScriptSource
	: public CSource
	, public IFileSourceFilter
	, public IAMFilterMiscFlags
	, public ISpecifyPropertyPages
	, public IScriptSource
	, public CExFilterConfigImpl
{
private:
	friend class CAviSynthStream;
	friend class CVapourSynthStream;

	std::wstring m_fn;
	std::wstring m_StreamInfo;

public:
	CScriptSource(LPUNKNOWN lpunk, HRESULT* phr);
	~CScriptSource();

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// IFileSourceFilter
	STDMETHODIMP Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE* pmt);
	STDMETHODIMP GetCurFile(LPOLESTR* ppszFileName, AM_MEDIA_TYPE* pmt);

	// IAMFilterMiscFlags
	STDMETHODIMP_(ULONG) GetMiscFlags();

	// ISpecifyPropertyPages
	STDMETHODIMP GetPages(CAUUID* pPages);

	// IScriptSource
	STDMETHODIMP_(bool) GetActive();
	STDMETHODIMP GetScriptInfo(std::wstring& str);

	// IExFilterConfig
	STDMETHODIMP Flt_GetInt64(LPCSTR field, __int64* value) override;
};
