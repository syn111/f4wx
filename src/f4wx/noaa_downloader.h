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

/**
 * NOAA NOMADS GFS download: list GFS runs, fetch GRIB subsets by lat/lon and
 * forecast file name (filter_gfs_0p25.pl), and provide run metadata.
 */

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <ctime>

#include <winerror.h>

#include "config.h"

namespace winhttp_get { class Client; }

inline constexpr const char* NOMADS_GFS025_STORE_URI = "https://nomads.ncep.noaa.gov/pub/data/nccf/com/gfs/prod/";

/** std::format template for GRIB filter URL (args: file, leftlon, rightlon, toplat, bottomlat, dir_name). */
inline constexpr const char* NOMADS_GFS025_FILTER_URI_FMT = "https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl?file={}&lev_100_mb=on&lev_150_mb=on&lev_200_mb=on&lev_300_mb=on&lev_400_mb=on&lev_500_mb=on&lev_700_mb=on&lev_850_mb=on&lev_925_mb=on&lev_2_m_above_ground=on&lev_10_m_above_ground=on&lev_convective_cloud_layer=on&lev_high_cloud_layer=on&lev_low_cloud_layer=on&lev_mean_sea_level=on&lev_middle_cloud_layer=on&lev_surface=on&lev_convective_cloud_bottom_level=on&lev_convective_cloud_top_level=on&lev_high_cloud_bottom_level=on&lev_high_cloud_top_level=on&lev_low_cloud_bottom_level=on&lev_low_cloud_top_level=on&lev_middle_cloud_bottom_level=on&lev_middle_cloud_top_level=on&var_ACPCP=on&var_APCP=on&var_PRATE=on&var_PRMSL=on&var_TCDC=on&var_TMP=on&var_UGRD=on&var_VGRD=on&var_VIS=on&var_PRES=on&subregion=&leftlon={}&rightlon={}&toplat={}&bottomlat={}&dir=%2F{}%2Fatmos";

inline constexpr size_t NOMADS_GFS_RUN_FILENAME_SIZE = 14;
inline constexpr size_t NOMADS_GFS_RUN_FORECAST_FILENAME_SIZE = 24;

inline constexpr size_t NOMADS_GFS_RUN_FILENAME_NEWFORMAT_SIZE_FIRST = 12;
inline constexpr size_t NOMADS_GFS_RUN_FILENAME_NEWFORMAT_SIZE_SECOND = 2;

/** GFS run directory name (e.g. gfs.2016112118); parses date/hour for display and URLs. */
class noaa_gfsrun_filename
{
public:
	explicit noaa_gfsrun_filename(std::string_view str) : m_str(str) { }

	bool is_valid() const { return m_str.size() == NOMADS_GFS_RUN_FILENAME_SIZE; }

	std::string get_year() const { return m_str.substr(4, 4); }
	std::string get_month() const { return m_str.substr(8, 2); }
	std::string get_day() const { return m_str.substr(10, 2); }
	std::string get_hour() const { return m_str.substr(12, 2); }

	std::string get_pretty_name() const { return is_valid() ? "GFS " + get_year() + '-' + get_month() + '-' + get_day() + ' ' + get_hour() + ":00Z" : get_filename(); }

	bool operator==(const noaa_gfsrun_filename& rhs) const { return m_str == rhs.m_str; }
	bool operator!=(const noaa_gfsrun_filename& rhs) const { return m_str != rhs.m_str; }

	/** New format: run dir has subdir (e.g. gfs.2025010112/00). */
	explicit noaa_gfsrun_filename(std::string_view strOld, std::string_view strNew);
	bool is_new() const { return m_isNew; }

	std::string get_filename() const { 
		if (!is_new() || !is_valid()) {
			return m_str;
		}
		else {
			std::string ret = m_str.substr(0, 12) + '/' + get_hour();
			return ret;
		}
	}

	std::string get_encoded_filename() const {
		if (!is_new() || !is_valid()) {
			return m_str;
		}
		else {
			std::string ret = m_str.substr(0, 12) + "%2F" + get_hour();
			return ret;
		}
	}

private:
	std::string m_str;

	bool m_isNew = false;
};

using noaa_gfsrun_filename_list = std::vector<noaa_gfsrun_filename>;

/** Forecast GRIB file name (e.g. gfs.t06z.pgrb2.0p25.f015). */
class noaa_gfsrun_forecastfilename
{
public:
	noaa_gfsrun_forecastfilename() = default;
	explicit noaa_gfsrun_forecastfilename(std::string_view str) : m_str(str) { }

	bool is_valid() const { return m_str.size() == NOMADS_GFS_RUN_FORECAST_FILENAME_SIZE; }
	std::string get_hour() const { return m_str.substr(m_str.size() - 3); }
	const std::string& get_filename() const { return m_str; }

	bool operator==(const noaa_gfsrun_forecastfilename& rhs) const { return m_str == rhs.m_str; }
	bool operator!=(const noaa_gfsrun_forecastfilename& rhs) const { return m_str != rhs.m_str; }


private:
	std::string m_str;
};

using noaa_gfsrun_forecastfilename_list = std::vector<noaa_gfsrun_forecastfilename>;


class noaa_grib_buffer
{
public:
	noaa_grib_buffer() = default;

	size_t get_size() const { return m_buffer.size(); }
	const unsigned char* get_data() const { return m_buffer.data(); }

	/** Write buffer to path (binary). Returns 0 on success; open failure returns errno, write failure returns EIO. */
	[[nodiscard]] int save(const std::filesystem::path& path) const;

	friend class noaa_downloader;

protected:
	std::vector<unsigned char> m_buffer;

	inline void allocate(size_t size) {
		m_buffer.resize(size);
	}

	/** Mutable pointer for writing (used by noaa_downloader). */
	inline unsigned char* data() { return m_buffer.data(); }

	noaa_grib_buffer(const noaa_grib_buffer&) = delete;
};

class noaa_downloader
{
public:

	noaa_downloader();

	// Query all avaiable GFS runs from NOAA HTTP datastore
	[[nodiscard]] int get_gfs_runs(noaa_gfsrun_filename_list& result);

	// Query new format GFS runs
	[[nodiscard]] int get_gfs_new_format_subruns(noaa_gfsrun_filename_list& result, std::string_view firstPart);

	// Query all forecast GRIB files for a given GFS run
	[[nodiscard]] int get_gfs_run_forecasts(const noaa_gfsrun_filename& gfsrun, noaa_gfsrun_forecastfilename_list& result);

	// Download a given forecast file
	[[nodiscard]] int get_gfs_grib(const noaa_gfsrun_filename& gfsrun, const noaa_gfsrun_forecastfilename& forecasttime, int leftlon, int rightlon, int toplat, int bottomlat, noaa_grib_buffer& buffer);

private:
	std::unique_ptr<winhttp_get::Client> m_client;
};