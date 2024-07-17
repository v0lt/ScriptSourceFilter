/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "stdafx.h"
#include <InitGuid.h>
#include "ScriptSource.h"
#include "PropPage.h"

template <class T>
static CUnknown* WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT* phr)
{
	*phr = S_OK;
	CUnknown* punk = new(std::nothrow) T(lpunk, phr);
	if (punk == nullptr) {
		*phr = E_OUTOFMEMORY;
	}
	return punk;
}

const AMOVIESETUP_PIN sudpPins[] = {
	{(LPWSTR)L"Output", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, nullptr, 0, nullptr},
};

const AMOVIESETUP_FILTER sudFilter[] = {
	{&__uuidof(CScriptSource), L"MPC Script Source", MERIT_NORMAL, (UINT)std::size(sudpPins), sudpPins, CLSID_LegacyAmFilterCategory},
};

CFactoryTemplate g_Templates[] = {
	{sudFilter[0].strName, sudFilter[0].clsID, CreateInstance<CScriptSource>, nullptr, &sudFilter[0]},
	{L"InfoProp", &__uuidof(CSSInfoPPage), CreateInstance<CSSInfoPPage>, nullptr, nullptr},
};

int g_cTemplates = (int)std::size(g_Templates);

STDAPI DllRegisterServer()
{
	return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer()
{
	LPCWSTR strGuid = _CRT_WIDE(STR_CLSID_ScriptSource);
	DWORD type;
	WCHAR data[40];
	DWORD cbData;

	HKEY hKey;
	LONG ec = ::RegOpenKeyExW(HKEY_CLASSES_ROOT, L"Media Type\\Extensions\\.avs", 0, KEY_ALL_ACCESS, &hKey);
	if (ec == ERROR_SUCCESS) {
		ec = RegQueryValueExW(hKey, L"Source Filter", nullptr, &type, (LPBYTE)data, &cbData);
		if (ec == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(strGuid, data) == 0) {
			RegDeleteValueW(hKey, L"Source Filter");
		}
		::RegCloseKey(hKey);
	}

	ec = ::RegOpenKeyExW(HKEY_CLASSES_ROOT, L"Media Type\\Extensions\\.vpy", 0, KEY_ALL_ACCESS, &hKey);
	if (ec == ERROR_SUCCESS) {
		ec = RegQueryValueExW(hKey, L"Source Filter", nullptr, &type, (LPBYTE)data, &cbData);
		if (ec == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(strGuid, data) == 0) {
			RegDeleteValueW(hKey, L"Source Filter");
		}
		::RegCloseKey(hKey);
	}

	return AMovieDllRegisterServer2(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL WINAPI DllMain(HINSTANCE hDllHandle, DWORD dwReason, LPVOID pReserved)
{
	return DllEntryPoint(hDllHandle, dwReason, pReserved);
}
