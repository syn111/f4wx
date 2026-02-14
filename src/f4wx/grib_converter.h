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
 * GRIB-to-fmap converter: decodes GRIB2 data and produces BMS weather maps (fmap)
 * with pressure, temperature, wind, clouds, visibility, and contrail levels.
 */

#include <vector>
#include <memory>
#include <string>
#include <cassert>
#include <algorithm>
#include <array>
#include <iterator>
#include <span>
#include <string_view>
#include <format>

#include "grib_decoder.h"
#include "fmap.h"
#include "grib_constants.h"

inline constexpr float GC_MIN_CUMULUS_LAYER_ALT = 1200.0f;
inline constexpr float GC_MAX_CUMULUS_LAYER_ALT = 22000.0f;
inline constexpr float GC_MIN_THICKNESS_FOR_TCU = 5000.0f;
inline constexpr float GC_MAX_LAYER_DIFF_TO_MERGE = 2000.0f;
inline constexpr float GC_MAX_SUNNY_CUMULUSZ = 8000.0f;	// Convergence layer upper limit

inline constexpr bool GC_VIZ_SCALE_ALL = false;			//	if true it scales all visibilities to GC_MAX_BMS_VIZ, otherwise extrapolates values above GC_MAX_GRIB_VIZ only
inline constexpr float GC_MAX_GRIB_VIZ = 24100.0f;		// DEPRECATED: taken from GRIB files
inline constexpr float GC_MAX_BMS_VIZ = 60000.0f;
inline constexpr float GC_VIZ_SMOOTH_BAND_M = 500.0f;		// band (m) below grid max for visibility smoothing

inline constexpr float WIND_DIR_MET_TO_DISPLAY_DEG = 180.0f;		// add to atan2(uwnd,vwnd) for meteorological -> display direction
inline constexpr int FMAP_WIND_LEVEL_FOR_MAP_AVG = 4;		// wind level index used for map-wide average

/* BMS cumulus mapping (thickness ft -> size index 0-5, density bands) */
inline constexpr float BMS_CUMULUS_THICKNESS_MIN_FT = 2500.0f;	/* humilis (size 5) */
inline constexpr float BMS_CUMULUS_THICKNESS_MAX_FT = 11000.0f;	/* congestus (size 0) */
inline constexpr float BMS_CUMULUS_SIZE_INDEX_MAX = 5.0f;

/** BMS cloud density indices (1, 5, 9, 13). Implicitly converts to int for fmap/APIs. */
enum bms_density : int {
	BMS_DENSITY_FEW = 1,
	BMS_DENSITY_SCATTERED = 5,
	BMS_DENSITY_BROKEN = 9,
	BMS_DENSITY_OVERCAST = 13
};
inline constexpr float TCC_BAND_OFFSET = 10.0f;		/* for tcc2density band index: (tcc + TCC_BAND_OFFSET) * ... / 100 */

/* Fallback cumulus base when no cloud in cell: temp range (deg C) */
inline constexpr float GC_CUMULUS_FALLBACK_TEMP_MIN_C = -20.0f;
inline constexpr float GC_CUMULUS_FALLBACK_TEMP_MAX_C = 50.0f;
inline constexpr float GC_CUMULUS_FALLBACK_TEMP_OFFSET_C = 20.0f;

static_assert(GC_MIN_CUMULUS_LAYER_ALT < GC_MAX_CUMULUS_LAYER_ALT, "GC_MIN_CUMULUS_LAYER_ALT must be less than GC_MAX_CUMULUS_LAYER_ALT");
static_assert(GC_MAX_SUNNY_CUMULUSZ >= GC_MIN_CUMULUS_LAYER_ALT, "GC_MAX_SUNNY_CUMULUSZ must be greater than GC_MIN_CUMULUS_LAYER_ALT");

constexpr unsigned long FORECAST_HOURS_NO_LIMIT = static_cast<unsigned long>(-1);

/** Options for GRIB conversion: weather thresholds and forecast range. */
struct grib_converter_options {
	float cloud_fair;
	float precipitation_fair;
	float cloud_poor;
	float precipitation_poor;
	float cloud_inclement;
	float precipitation_inclement;
	unsigned long interval_minutes;
	unsigned long max_forecast_hours;
};

/** Owning container for a sequence of fmaps (one per time step). */
class fmap_list
{
public:
	fmap_list() {}
	~fmap_list() { clear(); }

	/** Number of fmaps in the list. */
	size_t get_count() const { return m_fmaps.size(); }
	/** Returns the n-th fmap (0-based). */
	const fmap* get_fmap(size_t n) const { assert(n < get_count()); return m_fmaps[n].get(); }

	/** Clears the list (fmaps are destroyed). */
	void clear() { m_fmaps.clear(); }

private:
	std::vector<std::unique_ptr<fmap>> m_fmaps;

	fmap_list(const fmap_list&);

	friend class grib_converter;
};

/** Single time-step grid of values for one GRIB parameter (e.g. temperature). */
struct grib_breakpoint {
	grib_breakpoint(size_t sizeY, size_t sizeX) : values(sizeY * sizeX) {}

	long startStep;  /**< Forecast step start (e.g. hours from ref). */
	long endStep;    /**< Forecast step end. */

	float max = -FLT_MAX;
	float avg = 0;
	float min = FLT_MAX;

	std::vector<float> values;
};

inline constexpr std::array<float, 9> aloft_breakpoints_hpa = { 925,  850,  700,  500,   400,   300,   200,   150,   100 };
inline constexpr std::array<float, 9> aloft_breakpoints_ft = { 2498, 4781, 9882, 18289, 23574, 30065, 38662, 44647, 53083 };

/** TCC pressure levels (hPa) for get_cloud_cover; order matches GBP_TCC200 .. GBP_TCC925. */
inline constexpr std::array<float, 7> TCC_PRESSURE_LEVELS_HPA = { GRIB2_LEVEL_200_HPA, GRIB2_LEVEL_300_HPA, GRIB2_LEVEL_400_HPA, GRIB2_LEVEL_500_HPA, GRIB2_LEVEL_700_HPA, GRIB2_LEVEL_850_HPA, GRIB2_LEVEL_925_HPA };

static_assert(std::size(aloft_breakpoints_hpa) == std::size(aloft_breakpoints_ft), "size mismatch");

/** Converts GRIB2 data into BMS fmap weather grids. */
class grib_converter {
	grib_decoder m_decoder;

public:

	grib_converter(size_t breakpoints_sizeY, size_t breakpoints_sizeX);
	~grib_converter();

	/**
	 * Decode GRIB binary and register all recognized parameters.
	 * @param data GRIB file bytes. Must be valid: if data.size() > 0, data.data() must be non-null
	 *             (same precondition as the former (void*, size_t) API).
	 * @return 0 on success; non-zero on error (call get_last_error() for message).
	 */
	[[nodiscard]] int add_grib(std::span<std::byte> data);

	/** Convert all time steps into fmaps; clears and fills the list. */
	[[nodiscard]] int convert_all(grib_converter_options& options, fmap_list& fmaps);
	/** Convert a single time step into the given map. */
	[[nodiscard]] int convert_single(grib_converter_options& options, fmap& map, unsigned long forecastminutes);
	/** Convert a single time step and append the new fmap to the list. */
	[[nodiscard]] int convert_single(grib_converter_options& options, fmap_list& fmaps, unsigned long forecastminutes);

	/** Clear breakpoints and set grid size; use (0,0) to free only. */
	void reset(size_t breakpoints_sizeY, size_t breakpoints_sizeX);

	/** Maximum forecast step for which all parameters have data. */
	[[nodiscard]] unsigned long get_max_possible_forecast() const;

	/** GRIB reference date/time (from first loaded message). */
	inline int get_grib_day() const { return m_grib_day; }
	inline int get_grib_month() const { return m_grib_month; }
	inline int get_grib_year() const { return m_grib_year; }
	inline int get_grib_hour() const { return m_grib_hour; }

	/** GRIB grid longitude/latitude bounds (degrees). */
	inline float get_grib_llon() const { return m_grib_llon; }
	inline float get_grib_rlon() const { return m_grib_rlon; }
	inline float get_grib_tlat() const { return m_grib_tlat; }
	inline float get_grib_blat() const { return m_grib_blat; }

	/**
	 * Last error message when add_grib or convert_* returns non-zero.
	 * The returned pointer is valid only until the next setLastError() or until the converter is destroyed.
	 * Copy the string (e.g. into std::string) if you need to keep it longer.
	 */
	[[nodiscard]] inline const char *get_last_error() const { return m_lastError.c_str(); }

private:

	using grib_breakpoint_list = std::vector<std::unique_ptr<grib_breakpoint>>;

	// Important!! must keep the order in cloud layers: LO, MID, HI, CONVECTIVE!
	enum class grib_breakpoint_type {
		GBP_TEMPERATURE,
		GBP_10U,
		GBP_10V,
		GBP_PRMSL,

		GBP_TCC200,
		GBP_TCC300,
		GBP_TCC400,
		GBP_TCC500,
		GBP_TCC700,
		GBP_TCC850,
		GBP_TCC925,

		GBP_TCC_CONV,
		GBP_PRATE,

		GBP_UALOFT0,
		GBP_UALOFT1,
		GBP_UALOFT2,
		GBP_UALOFT3,
		GBP_UALOFT4,
		GBP_UALOFT5,
		GBP_UALOFT6,
		GBP_UALOFT7,
		GBP_UALOFT8,

		GBP_VALOFT0,
		GBP_VALOFT1,
		GBP_VALOFT2,
		GBP_VALOFT3,
		GBP_VALOFT4,
		GBP_VALOFT5,
		GBP_VALOFT6,
		GBP_VALOFT7,
		GBP_VALOFT8,

		GBP_VISIBILITY,

		GBP_CLOUD_BASE_LO,
		GBP_CLOUD_BASE_MID,
		GBP_CLOUD_BASE_HI,
		GBP_CLOUD_BASE_CONV,
		GBP_CLOUD_TOP_LO,
		GBP_CLOUD_TOP_MID,
		GBP_CLOUD_TOP_HI,
		GBP_CLOUD_TOP_CONV,

		NUM_GRIB_BREAKPOINTS
	};

	static_assert(std::size(aloft_breakpoints_hpa) == static_cast<size_t>(grib_breakpoint_type::GBP_UALOFT8) - static_cast<size_t>(grib_breakpoint_type::GBP_UALOFT0) + 1, "size mismatch");
	static_assert(std::size(aloft_breakpoints_hpa) == static_cast<size_t>(grib_breakpoint_type::GBP_VALOFT8) - static_cast<size_t>(grib_breakpoint_type::GBP_VALOFT0) + 1, "size mismatch");

	enum class cloud_layer_type {
		CLD_LOW,
		CLD_MID,
		CLD_HIGH,
		CLD_CONVECTIVE
	};

	struct cloud_layer_data {
		float base;
		float top;
		float tcc;
	};
	void get_cloud_layer_data(cloud_layer_type layer, unsigned int y, unsigned int x, float t, cloud_layer_data*, float slpHpa = 1013.25, float sltC = 15.0);
	void select_cumulus_layer(cloud_layer_data *res, cloud_layer_data* d1, cloud_layer_data* d2);
	float get_cloud_cover(float base, unsigned int y, unsigned int x, float t);

	/** Contrail formation level (ft AMSL) from surface data and high cloud. See implementation for pilot-facing description. */
	float get_contrail_formation_level_ft(float slpHpa, float surfTempC, float highCloudBaseFt, float highCloudTcc, float avgLatitudeDeg);

	static constexpr size_t num_grib_breakpoints = static_cast<size_t>(grib_breakpoint_type::NUM_GRIB_BREAKPOINTS);
	grib_breakpoint_list m_breakpoints[num_grib_breakpoints];

	size_t m_sizeY;
	size_t m_sizeX;

	/** @param file_max_step If >= 0, endStep is set to max(message endStep, file_max_step) so the file's nominal forecast extends to file_max_step (NCEP packs multiple validity times per file). */
	void add_breakpoint(grib_breakpoint_list& list, const grib_field& m, long file_max_step = -1);
	double interpolate_data(size_t fy, size_t fx, std::span<double> src, size_t srcrows, size_t srccols, std::span<double> lats, std::span<double> lons);
	float interpolate_breakpoint(const grib_breakpoint_list& list, size_t y, size_t x, float t);

	bool m_hasdata = false;

	// Build 39 - Keep track of grib data timestamp and location
	int m_grib_day;
	int m_grib_month;
	int m_grib_year;
	int m_grib_hour;
	float m_grib_llon;
	float m_grib_rlon;
	float m_grib_tlat;
	float m_grib_blat;

	std::string m_lastError = "no error";
	void setLastError(std::string_view msg) {
		if (!msg.empty())
			m_lastError = msg;
		else
			m_lastError = "";
	}

#ifdef FMAP_DEBUG
	class fmap_debug_data
		: public fmap::debug_data {
	public:
		fmap_debug_data(float prate, cloud_layer_data& lodata, cloud_layer_data& midata, cloud_layer_data& hidata, cloud_layer_data& convdata, cloud_layer_data& cumulusData, float fogLayerZ)
			:
			m_prate(prate), m_lodata(lodata), m_midata(midata), m_hidata(hidata), m_convdata(convdata), m_cumulusdata(cumulusData),
			m_fogLayerZ(fogLayerZ)
		{
		}
		~fmap_debug_data() override
		{
		}
		std::string str() const override {
			return std::format(
				"prate:\t{:.2f}"
				"\r\nlbase:\t{:.0f}\r\nltop:\t{:.0f}\r\nlcov:\t{:.0f}"
				"\r\nmbase:\t{:.0f}\r\nmtop:\t{:.0f}\r\nmcov:\t{:.0f}"
				"\r\nhbase:\t{:.0f}\r\nhtop:\t{:.0f}\r\nhcov:\t{:.0f}"
				"\r\ncbase:\t{:.0f}\r\nctop:\t{:.0f}\r\nccov:\t{:.0f}"
				"\r\nCuBase:\t{:.0f}\r\nCuTop:\t{:.0f}\r\nCuCov:\t{:.0f}"
				"\r\nCuZIpl:\t{}"
				"\r\nfogZ:\t{:.0f} ft",
				m_prate,
				m_lodata.base, m_lodata.top, m_lodata.tcc,
				m_midata.base, m_midata.top, m_midata.tcc,
				m_hidata.base, m_hidata.top, m_hidata.tcc,
				m_convdata.base, m_convdata.top, m_convdata.tcc,
				m_cumulusdata.base, m_cumulusdata.top, m_cumulusdata.tcc,
				m_zinterpolated,
				m_fogLayerZ);
		}

		inline void set_zinterpolated(int z) { m_zinterpolated = z; }
		inline void set_fogLayerZ(float z) { m_fogLayerZ = z; }

	protected:
		float m_prate;
		cloud_layer_data m_lodata;
		cloud_layer_data m_midata;
		cloud_layer_data m_hidata;
		cloud_layer_data m_convdata;
		cloud_layer_data m_cumulusdata;
		int m_zinterpolated = 0;
		float m_fogLayerZ = 0.0f;
	};
#endif
};