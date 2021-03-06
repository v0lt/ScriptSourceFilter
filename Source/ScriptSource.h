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

#include <atltypes.h>
#include <thread>
#include "Helper.h"
#include "IScriptSource.h"
#include "../Include/FilterInterfacesImpl.h"

class __declspec(uuid("7D3BBD5A-880D-4A30-A2D1-7B8C2741AFEF"))
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
	STDMETHODIMP GetInt64(LPCSTR field, __int64* value) override;
};
