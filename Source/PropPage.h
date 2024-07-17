/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

#include "IScriptSource.h"

// CSSInfoPPage

class __declspec(uuid("D36E542D-8645-4F2C-A758-55C6FDF67E50"))
	CSSInfoPPage : public CBasePropertyPage, public CWindow
{
	HFONT m_hMonoFont = nullptr;
	CComQIPtr<IScriptSource> m_pScriptSource;

public:
	CSSInfoPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CSSInfoPPage();

private:
	void SetControls();

	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite) {
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
	HRESULT OnApplyChanges() override;
};
