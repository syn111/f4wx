/*
* (C) Copyright 2016-2026 Ahmed
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/

#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <ctime>
#include <climits>

#include <Windows.h>

/** Clamp value to [lo, hi]. */
template<typename T>
constexpr T between(T val, T lo, T hi)
{
	if (val < lo) return lo;
	if (val > hi) return hi;
	return val;
}

/** Return value if >= min_val, else min_val. */
template<typename T>
constexpr T at_or_above(T val, T min_val)
{
	return val < min_val ? min_val : val;
}

/**
 * UTF-8 / UTF-16 conversion helpers for WinAPI boundaries.
 * Internal logic uses std::string (UTF-8); convert to std::wstring at WinAPI call sites.
 */

/** Convert UTF-8 string to UTF-16 (std::wstring) for WinAPI. */
inline std::wstring to_wide(std::string_view utf8)
{
	if (utf8.empty())
		return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (len <= 0)
		return {};
	std::wstring out(static_cast<size_t>(len), L'\0');
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
	if (n <= 0)
		return {};
	return out;
}

/** Convert UTF-16 string to UTF-8 (std::string) from WinAPI. */
inline std::string to_utf8(std::wstring_view wide)
{
	if (wide.empty())
		return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return {};
	std::string out(static_cast<size_t>(len), '\0');
	int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
	if (n <= 0)
		return {};
	return out;
}

/** Parse wide string to float; returns nullopt if invalid or empty. */
inline std::optional<float> parse_wide_float(std::wstring_view s)
{
	if (s.empty()) return std::nullopt;
	std::wstring nul(s);
	wchar_t* end = nullptr;
	float v = static_cast<float>(std::wcstod(nul.c_str(), &end));
	if (end == nul.c_str()) return std::nullopt;
	return v;
}

/** Parse wide string to int; returns nullopt if invalid or empty. */
inline std::optional<int> parse_wide_int(std::wstring_view s)
{
	if (s.empty()) return std::nullopt;
	std::wstring nul(s);
	wchar_t* end = nullptr;
	long v = std::wcstol(nul.c_str(), &end, 10);
	if (end == nul.c_str() || v < INT_MIN || v > INT_MAX) return std::nullopt;
	return static_cast<int>(v);
}

/** UTC std::tm from time_t; returns nullopt on error. */
inline std::optional<std::tm> gmtime_utc(std::time_t t)
{
	std::tm tb{};
	if (gmtime_s(&tb, &t) != 0) return std::nullopt;
	return tb;
}
