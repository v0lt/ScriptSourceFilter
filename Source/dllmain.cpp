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
	{L"Output", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, nullptr, 0, nullptr},
};

const AMOVIESETUP_FILTER sudFilter[] = {
	{&__uuidof(CScriptSource), L"MPC Script Source", MERIT_NORMAL, std::size(sudpPins), sudpPins, CLSID_LegacyAmFilterCategory},
};

CFactoryTemplate g_Templates[] = {
	{sudFilter[0].strName, sudFilter[0].clsID, CreateInstance<CScriptSource>, nullptr, &sudFilter[0]},
	{L"MainProp", &__uuidof(CISMainPPage), CreateInstance<CISMainPPage>, nullptr, nullptr},
};

int g_cTemplates = std::size(g_Templates);

STDAPI DllRegisterServer()
{
	LPWSTR strGuid = L"{7D3BBD5A-880D-4A30-A2D1-7B8C2741AFEF}";

	HKEY hKey;
	LONG ec = ::RegCreateKeyExW(HKEY_CLASSES_ROOT, L"Media Type\\Extensions\\.avs", 0, 0, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, 0, &hKey, 0);
	if (ec == ERROR_SUCCESS) {
		ec = ::RegSetValueExW(hKey, L"Source Filter", 0, REG_SZ,
			reinterpret_cast<BYTE*>(strGuid),
			(DWORD)(wcslen(strGuid) + 1) * sizeof(WCHAR));
		::RegCloseKey(hKey);
	}

	return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer()
{
	HKEY hKey;
	LONG ec = ::RegOpenKeyExW(HKEY_CLASSES_ROOT, L"Media Type\\Extensions", 0, KEY_ALL_ACCESS, &hKey);
	if (ec == ERROR_SUCCESS) {
		ec = ::RegDeleteKeyW(hKey, L".avs");
		::RegCloseKey(hKey);
	}

	return AMovieDllRegisterServer2(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL WINAPI DllMain(HINSTANCE hDllHandle, DWORD dwReason, LPVOID pReserved)
{
	return DllEntryPoint(hDllHandle, dwReason, pReserved);
}
