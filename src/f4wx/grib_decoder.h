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

/**
 * GRIB2 decode layer: wraps NCEPLIBS-g2c to parse GRIB2 messages and expose
 * field metadata and grid data for the converter.
 */

#include <vector>
#include <cassert>
#include <memory>
#include <span>

#include "config.h"
#include "grib_constants.h"

struct gribfield;

/** Single GRIB2 field (one parameter, one level, one time step). Thin wrapper over g2c gribfield. */
class grib_field
{
public:
	/** Takes ownership of gfld; must not be null. */
	explicit grib_field(gribfield* gfld);
	~grib_field();

	grib_field(const grib_field&) = delete;
	grib_field& operator=(const grib_field&) = delete;

	bool is_valid() const;

	/** Parameter id (same values as grib_constants.h GRIB2_PARAM_*). 0 if unknown. */
	int param_id() const;
	/** Type of first fixed surface (Table 4.5). */
	int type_of_first_fixed_surface() const;
	/** Level value (e.g. hPa for isobaric). */
	long level() const;
	/** Forecast start step (hours or minutes depending on template). */
	long start_step() const;
	/** Forecast end step. */
	long end_step() const;

	/** Reference time. */
	long year() const;
	long month() const;
	long day() const;
	long hour() const;

	/** Grid geometry (degrees). */
	double latitude_first() const;
	double latitude_last() const;
	double longitude_first() const;
	double longitude_last() const;
	double i_direction_increment() const;
	double j_direction_increment() const;
	/** Number of points along a parallel (longitude direction). Only valid for lat/lon grids (GDT 0,1,2,3). */
	long ni() const;
	/** Number of points along a meridian (latitude direction). Only valid for lat/lon grids (GDT 0,1,2,3). */
	long nj() const;
	long number_of_points() const;

	/** Fill lats, lons, values. All three spans must have size >= number_of_points(). Returns 0 on success. */
	[[nodiscard]] int get_grid_data(std::span<double> lats, std::span<double> lons, std::span<double> values) const;

private:
	gribfield* m_gfld;
};

/** Collection of GRIB2 fields (e.g. from one or more GRIB2 messages). */
class grib_file
{
public:
	grib_file();
	~grib_file();

	size_t get_message_count() const { return m_fields.size(); }
	const grib_field& get_message(size_t n) const { assert(n < get_message_count()); return *m_fields[n]; }

private:
	std::vector<std::unique_ptr<grib_field>> m_fields;
	friend class grib_decoder;
};

/** Decodes GRIB2 binary data into a grib_file of fields. */
class grib_decoder
{
public:
	grib_decoder();
	~grib_decoder();

	/** Parse GRIB data and append all fields to result. Returns 0 on success. */
	[[nodiscard]] int decode(std::span<std::byte> data, grib_file* result);
};
