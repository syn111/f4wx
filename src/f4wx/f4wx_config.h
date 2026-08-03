/*
* (C) Copyright 2016-2026 syn111
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

/**
 * Built-in theater definitions (name, grid size, timezone, lat/lon bounds, resource ID for map image).
 */

#include <Windows.h>
#include <cstddef>
#include <string>

/** Single theater record; matches layout expected by UI and converter. */
struct f4wx_theater_data {
	std::string name;
	int timezone = +540;
	WORD size = 64;
	float tlat = 43;
	float blat = 34;
	float llon = 123;
	float rlon = 132;
	DWORD imagesize = 0;  /* unused when using embedded resources */
};

/** One theater definition in the built-in table (C++20 designated initializers). */
struct theater_def {
	const char* name;
	unsigned size;
	int timezone;
	float tlat;
	float blat;
	float llon;
	float rlon;
	int resource_id;  /* RCDATA resource ID for the theater map PNG */
};

/** Read-only access to built-in theater list (no external .dat file). */
class f4wx_config
{
public:
	/** Number of built-in theaters. */
	[[nodiscard]] size_t get_theater_count() const;

	/** Fill result with the idx-th theater (0-based). Returns 0 on success. */
	[[nodiscard]] int get_theater_header(size_t idx, f4wx_theater_data* result) const;

	/** Resource ID for the idx-th theater's map image (for LoadResource/FindResource with RT_RCDATA). */
	[[nodiscard]] int get_theater_resource_id(size_t idx) const;
};
