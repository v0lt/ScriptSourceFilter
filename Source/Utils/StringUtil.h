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

#include <cctype>

inline void str_tolower(std::string& s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); } );
	// char for std::tolower should be converted to unsigned char
}

inline void str_toupper(std::string& s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); } );
	// char for std::toupper should be converted to unsigned char
}

inline void str_tolower(std::wstring& s)
{
	std::transform(s.begin(), s.end(), s.begin(), std::tolower);
}

inline void str_toupper(std::wstring& s)
{
	std::transform(s.begin(), s.end(), s.begin(), std::toupper);
}

inline void str_tolower_all(std::wstring& s)
{
	const std::ctype<wchar_t>& f = std::use_facet<std::ctype<wchar_t>>(std::locale());
	f.tolower(&s[0], &s[0] + s.size());
}

inline void str_toupper_all(std::wstring& s)
{
	const std::ctype<wchar_t>& f = std::use_facet<std::ctype<wchar_t>>(std::locale());
	f.toupper(&s[0], &s[0] + s.size());
}

void str_split(const std::string& str, std::vector<std::string>& tokens, char delim);

void str_split(const std::wstring& wstr, std::vector<std::wstring>& tokens, wchar_t delim);

inline const std::wstring A2WStr(std::string& s)
{
	return std::wstring(s.begin(), s.end());
}

inline const std::wstring A2WStr(std::string_view& s)
{
	return std::wstring(s.begin(), s.end());
}

std::string ConvertWideToANSI(const std::wstring& wstr);

std::wstring ConvertAnsiToWide(const std::string& str);

std::string ConvertWideToUtf8(const std::wstring& wstr);

std::wstring ConvertUtf8ToWide(const std::string& str);
