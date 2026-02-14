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

#include <array>
#include <cassert>

#include "f4wx_config.h"
#include "resource.h"

namespace {

	// Order must match resource.rc theater RCDATA entries and IDB_THEATER_* in resource.h.
	constexpr std::array<theater_def, 16> s_theaters = {{
		{ "Korea",   64,    540, 43.f, 34.f, 123.f, 132.f, IDB_THEATER_KOREA },
		{ "Aegean",  64,    120, 43.f, 33.f,  21.f,  34.f, IDB_THEATER_AEGEAN },
		{ "Balkans", 64,    120, 46.f, 37.f,  11.f,  20.f, IDB_THEATER_BALKANS },
		{ "EMF",     128,   120, 47.f, 29.f,  17.f,  39.f, IDB_THEATER_EMF },
		{ "HTO",     64,    120, 42.f, 33.f,  19.f,  31.f, IDB_THEATER_HTO },
		{ "Israel",  64,    120, 35.f, 25.f,  30.f,  40.f, IDB_THEATER_ISRAEL },
		{ "Kurile",  64,    660, 52.f, 42.f, 138.f, 150.f, IDB_THEATER_KURILE },
		{ "Kuwait",  64,    180, 34.f, 25.f,  43.f,  52.f, IDB_THEATER_KUWAIT },
		{ "MidEast", 128,   180, 39.f, 20.f,  37.f,  57.f, IDB_THEATER_MIDEAST },
		{ "Nevada",  64,   -540, 43.f, 33.f,-121.f,-109.f, IDB_THEATER_NEVADA },
		{ "Nordic",  64,    120, 72.f, 63.f,  11.f,  34.f, IDB_THEATER_NORDIC },
		{ "Ostsee",  64,    120, 61.f, 52.f,   4.f,  20.f, IDB_THEATER_OSTSEE },
		{ "Panama",  64,   -300, 13.f,  3.f, -85.f, -76.f, IDB_THEATER_PANAMA },
		{ "POH",     128,    60, 44.f, 26.f, -20.f,   4.f, IDB_THEATER_POH },
		{ "Taiwan",  64,    480, 30.f, 20.f, 115.f, 125.f, IDB_THEATER_TAIWAN },
		{ "Vietnam", 128,   420, 26.f,  8.f,  96.f, 114.f, IDB_THEATER_VIETNAM },
	}};

	static void fill_theater_data(const theater_def& def, f4wx_theater_data* result)
	{
		assert(result != nullptr);
		result->name = def.name;
		result->timezone = def.timezone;
		result->size = static_cast<WORD>(def.size);
		result->tlat = def.tlat;
		result->blat = def.blat;
		result->llon = def.llon;
		result->rlon = def.rlon;
		result->imagesize = 0;
	}
}

size_t f4wx_config::get_theater_count() const
{
	return s_theaters.size();
}

int f4wx_config::get_theater_header(size_t idx, f4wx_theater_data* result) const
{
	assert(result != nullptr);
	if (idx >= s_theaters.size())
		return 1;
	fill_theater_data(s_theaters[idx], result);
	return 0;
}

int f4wx_config::get_theater_resource_id(size_t idx) const
{
	if (idx >= s_theaters.size())
		return 0;
	return s_theaters[idx].resource_id;
}
