/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

interface __declspec(uuid("1B3DA9DF-63CA-46ED-8572-1615AA187662"))
IScriptSource : public IUnknown {
	STDMETHOD_(bool, GetActive()) PURE;
	STDMETHOD(GetScriptInfo) (std::wstring& str) PURE;
};
