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
* See the License for the specific
* language governing permissions and
* limitations under the License.
*
*/

#pragma once

/** Debug print: DPRINT active in _DEBUG (OutputDebugStringA); no-op in release. */

#define DPRINT_BUFLEN	2048

#ifdef _DEBUG
#include <windows.h>

#if defined(__cplusplus) && __cplusplus >= 202002L
#include <format>
#include <string>

/** Debug: std::format to OutputDebugStringA (C++20). */
template<typename... Args>
inline void dprint_fmt(std::format_string<Args...> fmt, Args&&... args)
{
	std::string s = std::format(fmt, std::forward<Args>(args)...);
	OutputDebugStringA(s.c_str());
}
#define DPRINT(...) dprint_fmt(__VA_ARGS__)

#else
#define DPRINT(fmt, ...) dprint(fmt, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

/** Debug: printf-style to OutputDebugStringA. Returns 0. */
int dprint(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif

#else
#define DPRINT(fmt, ...)
#endif