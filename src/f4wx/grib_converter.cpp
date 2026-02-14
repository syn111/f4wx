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


#include <Windows.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <exception>
#include <cassert>
#include <numbers>
#include <ranges>
#include <cmath>

#include "f4wx.h"
#include "fmap.h"
#include "grib_converter.h"
#include "units.h"

#ifdef _DEBUG
inline double assert_not_nan(double f) { assert(!std::isnan(f)); return f; }
inline float assert_not_nan(float f) { assert(!std::isnan(f)); return f; }
#else
inline double assert_not_nan(double f) { return f; }
inline float assert_not_nan(float f) { return f; }
#endif

static constexpr float GC_MAX_BMS_VIZ_KM = units::m_to_km(GC_MAX_BMS_VIZ);

/**
 * Bilinear interpolation: returns the value at (x, y) from the four corner
 * values q11(x1,y1), q12(x1,y2), q21(x2,y1), q22(x2,y2).
 */
inline double bilinear_interpolation(double q11, double q12, double q21, double q22, double x1, double x2, double y1, double y2, double x, double y)
{
	double x_norm = (x - x1) / (x2 - x1);
	double y_norm = (y - y1) / (y2 - y1);
	double r1 = std::lerp(q11, q21, x_norm);
	double r2 = std::lerp(q12, q22, x_norm);
	return assert_not_nan(std::lerp(r1, r2, y_norm));
}

/**
 * Linear interpolation: returns the value at x given values q1 at x1 and q2 at x2.
 */
inline double linear_interpolation(double q1, double q2, double x1, double x2, double x)
{
	double f = (x - x1) / (x2 - x1);
	return assert_not_nan(std::lerp(q1, q2, f));
}

/**
 * Constructs the converter and initializes the breakpoint grid.
 * @param breakpoints_sizeY Number of grid rows (BMS TheaterYCells).
 * @param breakpoints_sizeX Number of grid columns (BMS TheaterXCells).
 */
grib_converter::grib_converter(size_t breakpoints_sizeY, size_t breakpoints_sizeX)
{
	reset(breakpoints_sizeY, breakpoints_sizeX);
}

/** Destroys the converter and frees all breakpoint data. */
grib_converter::~grib_converter()
{
	reset(0, 0);
}

/**
 * Clears all loaded GRIB breakpoints and sets the grid dimensions.
 * Call with (0, 0) to free resources only.
 */
void grib_converter::reset(size_t breakpoints_sizeY, size_t breakpoints_sizeX)
{
	for (auto& list : m_breakpoints) {
		list.clear();
	}

	m_sizeY = breakpoints_sizeY;
	m_sizeX = breakpoints_sizeX;

	// Build 39
	m_hasdata = false;
}

/**
 * Interpolates a value at breakpoint grid cell (fy, fx) from source grid data.
 * Uses bilinear or linear interpolation depending on alignment.
 * @param fy Row index in the breakpoint grid.
 * @param fx Column index in the breakpoint grid.
 * @param src Source data array (row-major, srcrows * srccols).
 * @param srcrows Source grid row count.
 * @param srccols Source grid column count.
 * @param lats Not used (reserved for lat/lon-aware interpolation).
 * @param lons Not used (reserved for lat/lon-aware interpolation).
 * @return Interpolated value at (fy, fx).
 */
double grib_converter::interpolate_data(size_t fy, size_t fx, std::span<double> src, size_t srcrows, size_t srccols, std::span<double> lats, std::span<double> lons)
{
	double sy, sx;
	unsigned y0, y1, x0, x1;

	sy = ((srcrows - 1) * fy) / static_cast<double>(m_sizeY - 1);
	sx = ((srccols - 1) * fx) / static_cast<double>(m_sizeX - 1);

	y0 = static_cast<unsigned>(sy);
	y1 = static_cast<unsigned>(sy) + 1;
	x0 = static_cast<unsigned>(sx);
	x1 = static_cast<unsigned>(sx) + 1;

	if (y1 >= srcrows)
		y1--;
	if (x1 >= srccols)
		x1--;

	if (y0 == y1 && x0 == x1)
		return src[y0*srccols + x0];
	else if (y0 == y1)
		return linear_interpolation(src[y0*srccols + x0], src[y1*srccols + x1], static_cast<double>(x0), static_cast<double>(x1), sx);
	else if (x0 == x1)
		return linear_interpolation(src[y0*srccols + x0], src[y1*srccols + x1], static_cast<double>(y0), static_cast<double>(y1), sy);
	else
		return bilinear_interpolation(src[y0*srccols + x0], src[y0*srccols + x1], src[y1*srccols + x0], src[y1*srccols + x1], static_cast<double>(y0), static_cast<double>(y1), static_cast<double>(x0), static_cast<double>(x1), sy, sx);
}

class grib_converter_exception
	: public std::exception
{
public:
	explicit grib_converter_exception(const char *msg)
		: m_msg(msg ? msg : "")
	{}

	const char* what() const noexcept override {
		return m_msg.c_str();
	}
protected:
	std::string m_msg;
	grib_converter_exception() = default;
};

class grib_converter_decode_exception
	: public grib_converter_exception {
public:
	/** User-facing message (e.g. "Weather data could not be read"). */
	explicit grib_converter_decode_exception(const char* msg) : grib_converter_exception(msg) {}
	/** Legacy: numeric error code; message is still user-friendly. */
	explicit grib_converter_decode_exception(int err) : grib_converter_exception("Weather data could not be loaded") {
		(void)err;
	}
};

/**
 * Decodes a single GRIB message into a breakpoint and appends it to the given list
 * in time order. Validates grid geometry and reference date against previously
 * loaded messages.
 * @param list Target breakpoint list (e.g. one of m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_*]).
 * @param m Decoded GRIB message (lat/lon, steps, values).
 * @throws grib_converter_exception on coordinate or timestamp mismatch.
 * @throws grib_converter_decode_exception on decode failure.
 */
void grib_converter::add_breakpoint(grib_breakpoint_list& list, const grib_field& m, long file_max_step)
{
	long s1 = m.start_step();
	long s2 = m.end_step();
	if (file_max_step >= 0 && file_max_step > s2)
		s2 = file_max_step;  /* NCEP: same file can have messages at different steps; use file's max as validity cap. */
	double blat = m.latitude_first();
	double tlat = m.latitude_last();
	double llon = m.longitude_first();
	double rlon = m.longitude_last();
	long year = m.year();
	long month = m.month();
	long day = m.day();
	long hour = m.hour();

	// Normalize GRIB longitude to -180..180
	if (llon > GRIB_LON_HALF_RANGE)
		llon -= GRIB_LON_NORMALIZE;

	if (rlon > GRIB_LON_HALF_RANGE)
		rlon -= GRIB_LON_NORMALIZE;

	// Normalize GRIB latitude to -90..90 (edge case)
	if (tlat > GRIB_LAT_MAX)
		tlat -= GRIB_LON_HALF_RANGE;

	if (blat > GRIB_LAT_MAX)
		blat -= GRIB_LON_HALF_RANGE;

	// Build 39 - Keep track of grib data timestamp and location
	if (m_hasdata == false) {
		m_hasdata = true;
		m_grib_llon = static_cast<float>(llon);
		m_grib_rlon = static_cast<float>(rlon);
		m_grib_tlat = static_cast<float>(tlat);
		m_grib_blat = static_cast<float>(blat);
		m_grib_year = year;
		m_grib_month = month;
		m_grib_day = day;
		m_grib_hour = hour;
	}
	else {
		if (m_grib_llon != llon ||
			m_grib_rlon != rlon ||
			m_grib_tlat != tlat ||
			m_grib_blat != blat ||
			m_grib_year != year ||
			m_grib_month != month ||
			m_grib_day != day ||
			m_grib_hour != hour
			) {
			throw grib_converter_exception("GRIB files have different coordinates or timestamp");
		}

	}



	if (tlat < blat || GRIB_LON_HALF_RANGE + llon > GRIB_LON_HALF_RANGE + rlon)
		throw grib_converter_exception("GRIB coordinates are invalid");

	/* Use Ni/Nj from GDS (points along parallel / meridian) for grid dimensions. */
	long numlon = m.ni();
	long numlat = m.nj();
	if (numlat <= 0 || numlon <= 0)
		throw grib_converter_decode_exception("Invalid grid in weather data");

	long pts = m.number_of_points();
	if (pts != numlat * numlon)
		throw grib_converter_decode_exception("Invalid grid in weather data");

	std::vector<double> lats(static_cast<size_t>(pts)), lons(static_cast<size_t>(pts)), values(static_cast<size_t>(pts));
	if (m.get_grid_data(lats, lons, values) != 0)
		throw grib_converter_decode_exception("Weather data could not be read");

	auto brk = std::make_unique<grib_breakpoint>(m_sizeY, m_sizeX);
	for (size_t y = 0; y < m_sizeY; y++) {
		for (size_t x = 0; x < m_sizeX; x++) {
			size_t i;
			if (numlat > 0 && lats[numlon] > lats[0])
				i = (m_sizeY - y - 1) * m_sizeX + x;
			else
				i = y * m_sizeX + x;

			float val = static_cast<float>(interpolate_data(y, x, values, numlat, numlon, lats, lons));
			brk->values[i] = val;
			if (val > brk->max)
				brk->max = val;
			if (val < brk->min)
				brk->min = val;
			brk->avg = (brk->avg * (y * m_sizeX + x) + val) / (y * m_sizeX + x + 1);
		}
	}

	brk->startStep = s1;
	brk->endStep = s2;

	// Insert by time order
	auto it = std::ranges::find_if(list, [&brk](const std::unique_ptr<grib_breakpoint>& bp) {
		return brk->startStep < bp->startStep || (brk->startStep == bp->startStep && brk->endStep < bp->endStep);
	});
	list.insert(it, std::move(brk));
}

/**
 * Decodes GRIB binary data and registers all recognized parameters as breakpoints.
 * Supported parameters include 2m temperature, 10m wind, MSLP, cloud layers,
 * precipitation rate, visibility, and winds aloft at standard pressure levels.
 * @param data GRIB file bytes (span). Precondition: data.size() > 0 implies data.data() != nullptr.
 * @return 0 on success; 1 on error (call get_last_error() for message).
 */
int grib_converter::add_grib(std::span<std::byte> data)
{
	grib_file file;

	try {
		int rv = m_decoder.decode(data, &file);
		if (rv != 0)
			throw grib_converter_decode_exception("Weather data could not be loaded");

		/* NCEP GRIB files can contain messages with different forecast times (e.g. f009 has both step=6 and step=9).
		 * Use the file's max step as the validity cap so we don't limit range when one parameter has an earlier step. */
		long file_max_step = -1;
		for (size_t k = 0; k < file.get_message_count(); k++) {
			long step = file.get_message(k).start_step();
			if (step > file_max_step)
				file_max_step = step;
		}

		for (size_t i = 0; i < file.get_message_count(); i++) {
			const grib_field& m = file.get_message(i);
			int param = m.param_id();
			if (param != 0) {
				try {
				switch (param) {
					case GRIB2_PARAM_2M_TEMP:
						add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TEMPERATURE)], m, file_max_step);
						break;
					case GRIB2_PARAM_10M_UWIND:
						add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_10U)], m, file_max_step);
						break;
					case GRIB2_PARAM_10M_VWIND:
						add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_10V)], m, file_max_step);
						break;
					case GRIB2_PARAM_PRMSL:
						add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_PRMSL)], m, file_max_step);
						break;
					case GRIB2_PARAM_TCC:
					{
						int sfc = m.type_of_first_fixed_surface();
						switch (sfc)
							{
								case GRIB2_SFC_ISOBARIC:
								{
									long level = m.level();
									switch (level)
									{
										case GRIB2_LEVEL_100_HPA:
										case GRIB2_LEVEL_150_HPA:
											break;
										case GRIB2_LEVEL_200_HPA:
											add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC200)], m, file_max_step);
											break;
										case GRIB2_LEVEL_300_HPA:
											add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC300)], m, file_max_step);
											break;
										case GRIB2_LEVEL_400_HPA:
											add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC400)], m, file_max_step);
											break;
										case GRIB2_LEVEL_500_HPA:
											add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC500)], m, file_max_step);
											break;
										case GRIB2_LEVEL_700_HPA:
											add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC700)], m, file_max_step);
											break;
										case GRIB2_LEVEL_850_HPA:
											add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC850)], m, file_max_step);
											break;
										case GRIB2_LEVEL_925_HPA:
											add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC925)], m, file_max_step);
											break;
										default:
											DPRINT("unknown level: {}\n", static_cast<int>(level));
									}
									break;
								}
								case GRIB2_SFC_CONVECTIVE_CLOUD_LAYER:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC_CONV)], m, file_max_step);
									break;
								default:
									DPRINT("unknown typeOfFirstFixedSurface: {}\n", sfc);
							}
					}
					break;
					case GRIB2_PARAM_PRATE:
						add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_PRATE)], m, file_max_step);
						break;
/* ENABLE_WINDS_ALOFT */
					case GRIB2_PARAM_UGRD:
					{
						long level = m.level();
						for (size_t j = 0; j < std::size(aloft_breakpoints_hpa); j++) {
							if (aloft_breakpoints_hpa[j] == level) {
								add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_UALOFT0) + j], m, file_max_step);
								level = -1;
								break;
							}
							DPRINT("unknown level: {}\n", static_cast<int>(level));
						}
						break;
					}
					case GRIB2_PARAM_VGRD:
					{
						long level = m.level();
						for (size_t j = 0; j < std::size(aloft_breakpoints_hpa); j++) {
							if (aloft_breakpoints_hpa[j] == level) {
								add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_VALOFT0) + j], m, file_max_step);
								level = -1;
								break;
							}
							DPRINT("unknown level: {}\n", static_cast<int>(level));
						}
						break;
					}
					case GRIB2_PARAM_VIS:
						add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_VISIBILITY)], m, file_max_step);
						break;

					case GRIB2_PARAM_PRES:
					{
						int sfc = m.type_of_first_fixed_surface();
						switch (sfc)
							{
								case GRIB2_SFC_GROUND:
									/* Ground or water surface pressure – not used for cloud layers; skip. */
									break;
								case GRIB2_SFC_CLOUD_BASE_CONV:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_BASE_CONV)], m, file_max_step);
									break;
								case GRIB2_SFC_CLOUD_BASE_LO:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_BASE_LO)], m, file_max_step);
									break;
								case GRIB2_SFC_CLOUD_BASE_MID:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_BASE_MID)], m, file_max_step);
									break;
								case GRIB2_SFC_CLOUD_BASE_HI:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_BASE_HI)], m, file_max_step);
									break;
								case GRIB2_SFC_CLOUD_TOP_CONV:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_TOP_CONV)], m, file_max_step);
									break;
								case GRIB2_SFC_CLOUD_TOP_LO:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_TOP_LO)], m, file_max_step);
									break;
								case GRIB2_SFC_CLOUD_TOP_MID:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_TOP_MID)], m, file_max_step);
									break;
								case GRIB2_SFC_CLOUD_TOP_HI:
									add_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_TOP_HI)], m, file_max_step);
									break;

								default:
									DPRINT("unknown typeOfFirstFixedSurface: {}\n", sfc);
							}
					}
					break;

					default:
						DPRINT("unknown parameter: {}\n", param);
						break;
				}
				}
				catch (grib_converter_exception&) {
					/* Skip this message and continue with the next (e.g. grid mismatch on one product). */
				}
			}
		}
	}
	catch (grib_converter_exception& e) {
		setLastError(e.what());
		return 1;
	}
	return 0;
}

/**
 * Interpolates the scalar value at grid cell (y, x) and forecast time t from
 * the breakpoint list. Uses the list's startStep/endStep to find the two
 * bracketing time steps and linearly interpolates.
 * @param list Breakpoint list for one parameter (e.g. temperature, wind).
 * @param y Row index.
 * @param x Column index.
 * @param t Forecast time in hours (from reference).
 * @return Interpolated value at (y, x, t).
 * @throws grib_converter_exception if list is empty or t is out of range.
 */
float grib_converter::interpolate_breakpoint(const grib_breakpoint_list& list, size_t y, size_t x, float t)
{
	if (list.empty())
		throw grib_converter_exception("No GRIB files loaded");

	long s1, s2;
	constexpr int INDEX_INVALID = -1;
	int i1 = 0, i2 = INDEX_INVALID;	// index values in the list to use on interpolation
	double f, f1, f2;	// to calculate average step time

	for (size_t i = 0; i<list.size(); i++) {
		s1 = list[i]->startStep;
		s2 = list[i]->endStep;

		if (s1 != s2) {
			if (i == 0)						// use startStep if no previous data
				f = s1;
			else if (i == list.size() - 1)	// use endStep if no after data
				f = s2;
			else							// use mean time
				f = (s1 + s2) / 2.0f;
		}
		else {
			f = s1;
		}

		if (f < t) {
			i1 = static_cast<int>(i);
			f1 = f;
		}
		else {
			i2 = static_cast<int>(i);
			f2 = f;
			break;
		}
	}
	if (i2 < i1)
		throw grib_converter_exception("Attempted to interpolate invalid time");

	size_t idx = y * m_sizeX + x;
	if (i2 == i1)
		return list[i1]->values[idx];
	else
		return static_cast<float>(linear_interpolation(list[i1]->values[idx], list[i2]->values[idx], f1, f2, t));
}

/** Barometric formula with ISA lapse: h = (T0/L)*(1 - (P/P_slp)^(R*L/(g*M))).
 * Reference: ICAO Doc 7488 (ISA), hydrostatic equation with constant lapse L.
 * Inputs: p = pressure at point (Pa), slpHpa = sea-level pressure (hPa), sltC = surface temp (deg C).
 * Returns geometric altitude in feet.
 */
static constexpr float BARO_LAPSE_KPM = 0.0065f;       // ISA lapse rate (K/m)
static constexpr float BARO_R = 8.31446f;               // molar gas constant J/(mol*K)
static constexpr float BARO_G = 9.80665f;               // standard gravity m/s^2
static constexpr float BARO_M = 0.0289644f;            // molar mass of dry air kg/mol
static constexpr float BARO_EXP = (BARO_R * BARO_LAPSE_KPM) / (BARO_G * BARO_M);  // ~0.190
static float get_altitude_isa_corrected(float p, float slpHpa, float sltC) {
	const float pSlpPa = units::hpa_to_pa(slpHpa);
	if (p <= 0.0f || pSlpPa <= 0.0f || p >= pSlpPa)
		return 0.0f;
	const float t0K = units::c_to_k(sltC);
	if (t0K <= 0.0f)
		return 0.0f;
	const double ratio = assert_not_nan(static_cast<double>(p) / static_cast<double>(pSlpPa));
	const double hM = assert_not_nan(static_cast<double>(t0K / BARO_LAPSE_KPM) * (1.0 - pow(ratio, static_cast<double>(BARO_EXP))));
	if (hM <= 0.0)
		return 0.0f;
	return static_cast<float>(assert_not_nan(hM * units::FT_PER_M));
}

/**
 * Returns total cloud cover (TCC) at a given pressure level as the maximum of
 * the two TCC values at the pressure breakpoints bracketing base (200-925 hPa).
 * @param base Pressure at the level of interest (hPa).
 * @param y Grid row index.
 * @param x Grid column index.
 * @param t Forecast time (hours).
 * @return Cloud cover [0-100] at that level.
 */
float grib_converter::get_cloud_cover(float base, unsigned int y, unsigned int x, float t)
{
	// Find bracket: TCC_PRESSURE_LEVELS_HPA[i] <= base < TCC_PRESSURE_LEVELS_HPA[i+1]
	size_t i = 0;
	for (; i < std::size(TCC_PRESSURE_LEVELS_HPA) - 1; i++) {
		if (TCC_PRESSURE_LEVELS_HPA[i + 1] > base)
			break;
	}
	// Use the two levels that bracket base (i and i+1), not i and i-1
	return std::max(
		interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC200) + i], y, x, t),
		interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC200) + i + 1], y, x, t));
}

/**
 * Fills cloud base/top altitudes (ft) and TCC for one layer (low/mid/high/convective).
 * Base and top are computed from GRIB pressure levels via barometric altitude;
 * TCC is derived from the pressure-level TCC breakpoints at the layer base.
 * @param layer Which layer (CLD_LOW, CLD_MID, CLD_HIGH, CLD_CONVECTIVE).
 * @param y Grid row index.
 * @param x Grid column index.
 * @param t Forecast time (hours).
 * @param data Output: base, top (ft), tcc (0-100). base/top are 0 when layer missing (GRIB_MISSING_VALUE).
 * @param slpHpa Sea-level pressure (hPa) for altitude correction.
 * @param sltC Surface temperature (deg C) for altitude correction.
 */
void grib_converter::get_cloud_layer_data(cloud_layer_type layer, unsigned int y, unsigned int x, float t, cloud_layer_data* data, float slpHpa, float sltC)
{
	float f;
	f = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_BASE_LO) + static_cast<size_t>(layer)], y, x, t);
	if (f == GRIB_MISSING_VALUE || f <= 0.001f || f > 200000.0f)
		data->base = 0;
	else
		data->base = get_altitude_isa_corrected(f, slpHpa, sltC);

	float base = f;

	f = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_CLOUD_TOP_LO) + static_cast<size_t>(layer)], y, x, t);
	if (f == GRIB_MISSING_VALUE || f <= 0.001f || f > 200000.0f)
		data->top = 0;
	else
		data->top = get_altitude_isa_corrected(f, slpHpa, sltC);

	// Sanity check
	if (data->base == 0)
		data->tcc = 0;
	else
		data->tcc = get_cloud_cover(units::pa_to_hpa(base), y, x, t);
}

/**
 * Contrail formation level (ft AMSL) from surface pressure, temperature, high cloud, and latitude.
 *
 * Pilot-facing: This value is an estimated altitude (ft) where persistent contrails are most
 * likely. It is based on surface pressure, surface temperature, high cloud, and latitude. In
 * high-pressure (fair) conditions the contrail layer tends to sit higher; in low-pressure
 * (inclement) conditions it sits lower. Colder surface temperatures generally lower the level,
 * with a typical floor in the mid-to-high 20s (thousands of ft) in mid-latitudes; warmer
 * surface temperatures raise it into the high 30s or low 40s. When high cloud (e.g. cirrus
 * or cirrostratus) is present, the estimate is biased toward the base of that layer, where
 * the air is cold and moist enough for contrails to form and persist. The method is tuned
 * for mid-latitudes; in polar regions contrails can form lower (floor can be as low as the
 * low teens); in the tropics the layer is usually in the upper 30s to 40s (floor ~34k ft).
 *
 * Technical reasoning (surface-data only; no upper-air T/RH):
 *
 * (1) Schmidt-Appleman criterion (SAC): contrails form when the mixture of engine exhaust and
 *     ambient air reaches liquid water saturation; the intersection of the mixing line with the
 *     saturation curve defines a critical ambient temperature T_crit. If T_amb < T_crit,
 *     condensation (and then freezing) occurs. We approximate the contrail level as the
 *     geometric altitude where the ambient temperature equals T_crit.
 *
 * (2) T_crit: For standard jet fuel at cruise, T_crit lies roughly between -40 C and -55 C
 *     depending on ambient RH. We use -42 C (dry/clear aloft) and -39 C when high cloud is
 *     present (moist; high cloud implies ice supersaturation). Tuned for mid-latitude visibility.
 *
 * (3) Vertical temperature profile: We assume a linear lapse from the surface, z_raw = (T_surf -
 *     T_crit) / Gamma. Lapse rate Gamma from SLP: low SLP -> steeper (0.00220 C/ft); high SLP ->
 *     shallower (0.00160 C/ft); standard 0.00198 C/ft (6.5 K/km). Smooth blend over 1000--1025 hPa.
 *
 * (4) Cloud proxy fusion: High-level cloud base is a direct proxy for the cold, moist (RH_i >= 100%)
 *     layer where contrails persist. When high cloud is present we set T_crit to -39 C and blend
 *     the thermodynamic estimate with the observed base: 0.7*base + 0.3*z_raw if |z_raw - base|
 *     < 5000 ft, else 0.5/0.5, so the observation anchors the result when the column is decoupled.
 *
 * (5) Floor and cap: Climatological min (no high cloud) and tropopause max; both latitude-dependent.
 *     Anchors: equator 34k/56k ft, pole 10k/32k ft. Weight w = cos^2(|lat|) so mid-lat (45 deg)
 *     gets 50/50 blend (~22k min, ~44k max), allowing winter contrails at 20-24k ft.
 *
 * References (overview): SAC and T_crit from Schmidt/Appleman and subsequent DLR, NASA, IPCC
 * aviation work; lapse rate and stability from NWS/SKYbrary and atmospheric stability literature;
 * high cloud and ISSR as proxy for persistence from ACP/AMS contrail and cirrus studies;
 * barometric/hypsometry from standard aeronautical references (e.g. PDA hydro).
 *
 * @param slpHpa Sea-level pressure (hPa).
 * @param surfTempC Surface temperature (deg C).
 * @param highCloudBaseFt High cloud layer base (ft); 0 if no high cloud.
 * @param highCloudTcc High cloud layer total cloud cover (0-100); 0 if no high cloud.
 * @param avgLatitudeDeg Average latitude of the grid (degrees), e.g. from GRIB first/last point.
 * @return Estimated contrail formation level in feet. If surfTempC is at terrain (e.g. 2m T),
 *         the lapse integration yields height above that level (AGL); the caller must add
 *         terrain elevation if the renderer expects MSL (e.g. over high terrain).
 */
float grib_converter::get_contrail_formation_level_ft(float slpHpa, float surfTempC, float highCloudBaseFt, float highCloudTcc, float avgLatitudeDeg)
{
	// Tuned for mid-latitude accuracy; extremes for tropical/polar climatology.
	static constexpr float LAPSE_STD_FT      = 0.00198f;  // deg C/ft, ISA 6.5 K/km
	static constexpr float LAPSE_UNSTABLE_FT = 0.00220f;  // steeper for storms (colder aloft)
	static constexpr float LAPSE_STABLE_FT   = 0.00160f;  // shallower for high P (subsidence)
	static constexpr float T_CRIT_DRY   = -42.0f;  // deg C, dry/clear; -40 to -42 typical for visible formation
	static constexpr float T_CRIT_MOIST = -39.0f;  // deg C, high cloud present; slightly cooler than -38 to reduce false positives
	// Latitude: two anchors (equator, pole), cos(|lat|) interpolation.
	static constexpr float H_MIN_EQUATOR = 34000.0f;  // ft, tropical -40 C isotherm rarely below FL340
	static constexpr float H_MIN_POLE    = 10000.0f;  // ft, allow low contrails in polar winter
	static constexpr float H_MAX_EQUATOR = 56000.0f;  // ft, tropical tropopause
	static constexpr float H_MAX_POLE    = 32000.0f;  // ft, polar tropopause often 25-30k ft
	static constexpr float FUSION_CLOSE = 5000.0f;   // ft, threshold for weighted vs 50/50 blend

	double abs_lat_rad = fabs(static_cast<double>(avgLatitudeDeg)) * (std::numbers::pi / 180.0);
	if (abs_lat_rad > std::numbers::pi * 0.5)
		abs_lat_rad = std::numbers::pi * 0.5;
	float cos_lat = static_cast<float>(cos(abs_lat_rad));
	/* cos^2(lat): at 45 deg w = 0.5 (50/50 blend). Lowers mid-lat floor to ~22k ft so winter
	   -40 C isotherm (often 20-24k ft) is not over-clamped; cos(lat) alone gave ~27k. */
	float w = cos_lat * cos_lat;
	float h_min_clear = H_MIN_POLE + w * (H_MIN_EQUATOR - H_MIN_POLE);
	float h_max       = H_MAX_POLE + w * (H_MAX_EQUATOR - H_MAX_POLE);

	bool has_high_cloud = (highCloudTcc > 0 && highCloudBaseFt > 0);
	float high_cloud_base = highCloudBaseFt;
	float t_tgt = has_high_cloud ? T_CRIT_MOIST : T_CRIT_DRY;

	// Lapse rate: smooth blend over SLP (slightly widened bands for continuity).
	static constexpr float SLP_UNSTABLE = 1000.0f;
	static constexpr float SLP_STD_MID  = 1013.25f;
	static constexpr float SLP_STABLE   = 1025.0f;
	float lapse_ft;
	if (slpHpa <= SLP_UNSTABLE) {
		lapse_ft = LAPSE_UNSTABLE_FT;
	} else if (slpHpa >= SLP_STABLE) {
		lapse_ft = LAPSE_STABLE_FT;
	} else {
		float t = (slpHpa <= SLP_STD_MID)
			? (slpHpa - SLP_UNSTABLE) / (SLP_STD_MID - SLP_UNSTABLE)
			: (slpHpa - SLP_STD_MID) / (SLP_STABLE - SLP_STD_MID);
		lapse_ft = (slpHpa <= SLP_STD_MID)
			? LAPSE_UNSTABLE_FT + t * (LAPSE_STD_FT - LAPSE_UNSTABLE_FT)
			: LAPSE_STD_FT + t * (LAPSE_STABLE_FT - LAPSE_STD_FT);
	}

	float delta_t = surfTempC - t_tgt;
	float h_calc = (delta_t <= 0.0f) ? 0.0f : assert_not_nan(delta_t / lapse_ft);

	float h_final;
	if (has_high_cloud) {
		float delta = assert_not_nan(std::fabs(h_calc - high_cloud_base));
		if (delta < FUSION_CLOSE)
			h_final = 0.7f * high_cloud_base + 0.3f * h_calc;
		else
			h_final = 0.5f * high_cloud_base + 0.5f * h_calc;
	} else {
		h_final = std::max(h_calc, h_min_clear);
	}
	h_final = std::min(h_final, h_max);
	/* No pressure-altitude term: output is geometric ft MSL; lapse already reflects column state. */
	if (h_final < 0.0f)
		h_final = 0.0f;
	return assert_not_nan(h_final);
}

/**
 * Chooses the dominant cumulus layer between two candidates (e.g. convective vs low).
 * If layers are close in altitude they are merged; otherwise the layer with
 * higher coverage or lower base is selected based on a simple heuristic.
 * @param res Output: the selected (or merged) layer.
 * @param d1 First candidate layer.
 * @param d2 Second candidate layer.
 */
void grib_converter::select_cumulus_layer(cloud_layer_data *res, cloud_layer_data* d1, cloud_layer_data* d2)
{
	cloud_layer_data *hi = (d1->base > d2->base) ? d1 : d2;
	cloud_layer_data *lo = (d1->base > d2->base) ? d2 : d1;

	if (lo->base == 0) {
		*res = *hi;
	}
	else {
		if (abs(hi->base - lo->top) < GC_MAX_LAYER_DIFF_TO_MERGE) {	// Combine layers if they are the same
			res->base = lo->base;
			res->top = std::max(hi->top, lo->top);
			res->tcc = std::max(hi->tcc, lo->tcc);
		}
		else {
			if (lo->tcc > hi->tcc  * 1.5f * (lo->base / hi->base)) {
				*res = *lo;
			}
			else {
				*res = *hi;
			}
		}
	}
}

/**
 * Maps cumulus layer thickness (ft) to BMS cumulus size index.
 * BMS uses m_fCumulusSize 0-5: 0 = congestus (thick, ~11000 ft), 5 = humilis (thin, ~2500 ft).
 * Linear mapping between 2500 ft (size 5) and 11000 ft (size 0); clamped to [0, 5].
 * @param thickness Cloud thickness in feet (top - base).
 * @return BMS cumulus size in [0, 5].
 */
inline float thickness2size(float thickness) {
	return std::max(0.0f, std::min(static_cast<float>(BMS_CUMULUS_SIZE_INDEX_MAX),
		static_cast<float>(BMS_CUMULUS_SIZE_INDEX_MAX) - static_cast<float>(BMS_CUMULUS_SIZE_INDEX_MAX) * (thickness - BMS_CUMULUS_THICKNESS_MIN_FT) / (BMS_CUMULUS_THICKNESS_MAX_FT - BMS_CUMULUS_THICKNESS_MIN_FT)));
}

/**
 * Maps total cloud cover (TCC) to BMS cumulus density index.
 * BMS density 1 = few, 13 = overcast. We use four bands (1, 5, 9, 13) from
 * TCC percent; the size parameter is reserved for future use.
 * @param tcc Total cloud cover 0-100.
 * @param size BMS cumulus size (unused; reserved for future TCC/size interaction).
 * @return BMS density index in { 1, 5, 9, 13 }.
 */
inline int tcc2density(float tcc, float size) {
	(void)size;
	static constexpr bms_density densities[] = { BMS_DENSITY_FEW, BMS_DENSITY_SCATTERED, BMS_DENSITY_BROKEN, BMS_DENSITY_OVERCAST };
	constexpr int num = static_cast<int>(std::size(densities));
	float clamped = std::clamp(tcc, 0.f, 100.f);
	int i = static_cast<int>(((clamped + TCC_BAND_OFFSET) * (num - 1)) / 100);
	i = std::clamp(i, 0, num - 1);
	return densities[i];
}

/** Running average: add samples with add(), read current average with get() or operator T. */
template <class T>
class averager {
public:
	inline void add(T val) { avg = avg + (val - avg)/(++count); }
	inline T get() { return avg; }

	inline operator T() { return get(); }
protected:
	T avg = 0;
	size_t count = 0;
};

/**
 * Converts GRIB breakpoints to a single fmap at the given forecast time.
 * Fills pressure, temperature, wind (sfc + aloft), visibility, cloud layers,
 * cumulus base/size/density, weather type, stratus and contrail levels.
 * Performs post-pass: cumulus base interpolation for clear cells, visibility
 * scaling when GRIB viz exceeds BMS cap, and map-wide wind average.
 * @param o Conversion options (thresholds, interval, max hours).
 * @param map Output weather map; must have same grid size as converter.
 * @param forecastminutes Forecast time in minutes from reference.
 * @return 0 on success; 1 on error (get_last_error()).
 */
int grib_converter::convert_single(grib_converter_options& o, fmap& map, unsigned long forecastminutes)
{
	unsigned int y, x;
	float f;
	float t = forecastminutes / 60.0f;
	float prate = 0;
	float uwnd, vwnd;
	float slp, temp;
	fmap_wxtype wxtype;

	averager<float> stratuszfair;
	averager<float> stratuszinc;

	averager<float> contraillevels[NUM_WEATHER_TYPES];

	float maxViz = 0;

	try {
		if (m_sizeY != map.get_sizeY() || m_sizeX != map.get_sizeX())
			throw grib_converter_exception("converter and map size mismatch");

		float avg_lat = (get_grib_blat() + get_grib_tlat()) * 0.5f;

		for (y = 0; y < m_sizeY; y++) {
			for (x = 0; x < m_sizeX; x++) {

				prate = units::mmsec_to_mmhr(interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_PRATE)], y, x, t));

				slp = units::pa_to_hpa(interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_PRMSL)], y, x, t));
				map.set_pressure({y, x}, slp);

				temp = units::k_to_c(interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TEMPERATURE)], y, x, t));
				map.set_temperature({y, x}, temp);

				cloud_layer_data convdata, lodata, midata, hidata;
				get_cloud_layer_data(cloud_layer_type::CLD_CONVECTIVE, y, x, t, &convdata, slp, temp);
				get_cloud_layer_data(cloud_layer_type::CLD_MID, y, x, t, &midata, slp, temp);
				get_cloud_layer_data(cloud_layer_type::CLD_LOW, y, x, t, &lodata, slp, temp);
				get_cloud_layer_data(cloud_layer_type::CLD_HIGH, y, x, t, &hidata, slp, temp);

				// Cumulus Layer
				cloud_layer_data cumulus_layer = convdata;
				int hasTcu = cumulus_layer.top - cumulus_layer.base > GC_MIN_THICKNESS_FOR_TCU;

				select_cumulus_layer(&cumulus_layer, &cumulus_layer, &midata);
				select_cumulus_layer(&cumulus_layer, &cumulus_layer, &lodata);

				float tcc = cumulus_layer.tcc;

				// Don't use high cloud layer data as it creates too much cloud
				// f = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC_HI), y, x, t);
				// if (f > tcc) 
				//	tcc = f;
				f = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_TCC_CONV)], y, x, t);
				if (f > tcc)
					tcc = f;

				if (prate >= o.precipitation_inclement && tcc >= o.cloud_inclement)
					wxtype = WX_INCLEMENT;
				else if (prate >= o.precipitation_poor && tcc >= o.cloud_poor)
					wxtype = WX_POOR;
				else if (prate >= o.precipitation_fair && tcc >= o.cloud_fair)
					wxtype = WX_FAIR;
				else
					wxtype = WX_SUNNY;

				map.set_basicCondition({y, x}, wxtype);

				uwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_10U)], y, x, t);
				vwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_10V)], y, x, t);
				float wind_steady_ms = sqrtf(uwnd*uwnd + vwnd*vwnd);
				map.set_windDirection({y, x}, WIND_DIR_MET_TO_DISPLAY_DEG + units::rad_to_deg(atan2f(uwnd, vwnd)));
				map.set_windSpeed({y, x}, units::mps_to_kts(wind_steady_ms));

/* ENABLE_WINDS_ALOFT */
				// Precondition: GRIB aloft levels bracket fmap aloft levels (i.e. aloft_breakpoints_ft[0] <=
				// fmap_aloft_breakpoints[1] and aloft_breakpoints_ft[last] >= fmap_aloft_breakpoints[last]).
				// Both arrays are ascending by height.
				for (size_t i = 1; i < NUM_ALOFT_BREAKPOINTS; i++) {		// 0 (sfc) is done above
					size_t below = 0;
					size_t above = 0;

					if (aloft_breakpoints_ft[below] == fmap_aloft_breakpoints[i]) {
						uwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_UALOFT0) + below], y, x, t);
						vwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_VALOFT0) + below], y, x, t);
					}

					else {
						while (aloft_breakpoints_ft[above] < fmap_aloft_breakpoints[i]) {
							if (above == std::size(aloft_breakpoints_ft) - 1)
								throw grib_converter_exception("winds aloft interpolation error");
							above++;
						}

						if (aloft_breakpoints_ft[above] == fmap_aloft_breakpoints[i]) {
							uwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_UALOFT0) + above], y, x, t);
							vwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_VALOFT0) + above], y, x, t);
						}
						else {
							if (above == 0)
								throw grib_converter_exception("winds aloft interpolation error");
							below = above - 1;

							float uwnd2, vwnd2;
							uwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_UALOFT0) + below], y, x, t);
							vwnd = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_VALOFT0) + below], y, x, t);

							uwnd2 = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_UALOFT0) + above], y, x, t);
							vwnd2 = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_VALOFT0) + above], y, x, t);

							uwnd = static_cast<float>(linear_interpolation(uwnd, uwnd2, aloft_breakpoints_ft[below], aloft_breakpoints_ft[above], fmap_aloft_breakpoints[i]));
							vwnd = static_cast<float>(linear_interpolation(vwnd, vwnd2, aloft_breakpoints_ft[below], aloft_breakpoints_ft[above], fmap_aloft_breakpoints[i]));
						}
					}
					map.set_windDirection({y, x}, WIND_DIR_MET_TO_DISPLAY_DEG + units::rad_to_deg(atan2f(uwnd, vwnd)), static_cast<int>(i));
					map.set_windSpeed({y, x}, units::mps_to_kts(sqrtf(uwnd*uwnd + vwnd*vwnd)), static_cast<int>(i));
				}

				// Visibility
				float viz = interpolate_breakpoint(m_breakpoints[static_cast<size_t>(grib_breakpoint_type::GBP_VISIBILITY)], y, x, t);
#if GC_VIZ_SCALE_ALL
				viz = std::max(viz, viz * GC_MAX_BMS_VIZ / GC_MAX_GRIB_VIZ);
#else
				maxViz = std::max(maxViz, viz);
#endif
				map.set_visibility({y, x}, units::m_to_km(std::min(viz, GC_MAX_BMS_VIZ)));

				map.set_cumulusBase({y, x},
					cumulus_layer.base < GC_MIN_CUMULUS_LAYER_ALT || cumulus_layer.base > GC_MAX_CUMULUS_LAYER_ALT ? 0	// HACK: Set to 0 to be interpolated later but calculate size and density
					: cumulus_layer.base);

				float cumulusSize = thickness2size(cumulus_layer.top - cumulus_layer.base);
				int cumulusDensity = tcc2density(cumulus_layer.tcc, cumulusSize);

				// BMS limits
				if (wxtype >= WX_POOR) {
					cumulusSize = 0.0f;
					cumulusDensity = std::max(static_cast<int>(BMS_DENSITY_BROKEN), cumulusDensity);
				}
				else {
					cumulusDensity = std::max(static_cast<int>(BMS_DENSITY_FEW), cumulusDensity);
				}

				map.set_cumulusSize({y, x}, cumulusSize);
				map.set_cumulusDensity({y, x}, cumulusDensity);
				map.set_hasTowerCumulus({y, x}, hasTcu);
				map.set_hasShowerCumulus({y, x}, (hasTcu && prate >= o.precipitation_inclement) ? 1 : 0);

				// Fog layer altitude (v8): set in post-process after final stratus Z and cumulus base smoothing.
				map.set_fogLayerZ({y, x}, 0.0f);
				float fogLayerZ = 0.0f;

				// TODO: This is disabled as it caused incongruences every 3hrs
				//if (cumulus_layer.tcc > 0 && wxtype < WX_FAIR)
				//	map.set_basicCondition({y, x}, WX_FAIR);

				// Stratus Layer
				cloud_layer_data& stratus_layer = hidata;
				if (stratus_layer.tcc != 0) {
					if (wxtype <= WX_FAIR)
						stratuszfair.add(stratus_layer.base);
					else
						stratuszinc.add(stratus_layer.base);
				}

				f = get_contrail_formation_level_ft(slp, temp, stratus_layer.base, stratus_layer.tcc, avg_lat);
				contraillevels[wxtype].add(f);
					
#ifdef FMAP_DEBUG
				map.set_debugData({y, x}, std::make_unique<fmap_debug_data>(prate, lodata, midata, hidata, convdata, cumulus_layer, fogLayerZ));
#endif
			}
		}

		if (stratuszfair != 0)
			map.set_mapStratusZFair(static_cast<int>(stratuszfair));
		if (stratuszinc != 0)
			map.set_mapStratusZInc(static_cast<int>(stratuszinc));

		for (int i = 0; i < NUM_WEATHER_TYPES; i++)
			if (contraillevels[i] != 0)
				map.set_contrailLayer(static_cast<fmap_wxtype>(i), static_cast<int>(contraillevels[i]));

		averager<float> avgWindDir;
		averager<float> avgWindSpeed;

		// HACK: Decrease max visibility slightly (GC_VIZ_SMOOTH_BAND_M) below the grid maximum.
		// This ensures that cells near the maximum visibility are included in the smoothing logic below,
		// preventing patchy artifacts where interpolation would otherwise create sharp transitions 
		// between max-viz and near-max-viz cells in BMS.
		maxViz -= GC_VIZ_SMOOTH_BAND_M;
		maxViz = units::m_to_km(maxViz);

		// Do additional post-processing of cells

		for (y = 0; y < map.get_sizeY(); y++) {
			for (x = 0; x < map.get_sizeX(); x++) {

				// Calculate map wind averages
				float windir = map.get_windDirection({y, x}, FMAP_WIND_LEVEL_FOR_MAP_AVG);
				if (windir > 180)
					windir -= 360;
				avgWindDir.add(windir);
				avgWindSpeed.add(map.get_windSpeed({y, x}, FMAP_WIND_LEVEL_FOR_MAP_AVG));

#if !GC_VIZ_SCALE_ALL					
				// Adjust visibility
				if (maxViz > 0) {
					float viz = map.get_visibility({y, x});
					averager<float> avgViz;
						
					if (viz >= maxViz) {
						if (y > 0) {
							f = map.get_visibility({y - 1, x});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						if (y + 1 < map.get_sizeY()) {
							f = map.get_visibility({y + 1, x});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						if (x > 0) {
							f = map.get_visibility({y, x - 1});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						if (x + 1 < map.get_sizeX()) {
							f = map.get_visibility({y, x + 1});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						if (y > 0 && x > 0) {
							f = map.get_visibility({y - 1, x - 1});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						if (y > 0 && x + 1 < map.get_sizeX()) {
							f = map.get_visibility({y - 1, x + 1});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						if (y + 1 < map.get_sizeY() && x > 0) {
							f = map.get_visibility({y + 1, x - 1});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						if (y + 1 < map.get_sizeY() && x + 1 < map.get_sizeX()) {
							f = map.get_visibility({y + 1, x + 1});
							avgViz.add(f >= maxViz ? GC_MAX_BMS_VIZ_KM : f);
						}
						map.set_visibility({y, x}, std::max(viz, avgViz.get()));
					}
				}
#endif

				// BMS requires cumulus base value even for cells not containing clouds. Interpolate around.
				float z = map.get_cumulusBase({y, x});
				wxtype = map.get_basicCondition({y, x});
				if (z == 0 || (wxtype == WX_SUNNY && z > GC_MAX_SUNNY_CUMULUSZ)) {
					z = 0;
					for (int d = 1;; d++) {
						const unsigned int ud = static_cast<unsigned int>(d);
						struct {
							float z2;
							int zcount = 0;
							int validcount = 0;

							inline void update(float& z, fmap& map, fmap_wxtype wxtype, unsigned int yy, unsigned int xx) {
								z2 = map.get_cumulusBase({yy, xx});
								zcount++;
								if (z2 != 0 && (wxtype > WX_SUNNY || z2 <= GC_MAX_SUNNY_CUMULUSZ)) {
									z += z2;
									validcount++;
								}
							}
						} counter ;

						if (y >= ud)
							counter.update(z, map, wxtype, y - d, x);
						
						if (x >= ud) 
							counter.update(z, map, wxtype, y, x - d);

						if (y + d < map.get_sizeY()) 
							counter.update(z, map, wxtype, y + d, x);
						
						if (x + d < map.get_sizeX()) 
							counter.update(z, map, wxtype, y, x + d);

						if (y >= ud && x >= ud)
							counter.update(z, map, wxtype, y - d, x - d);

						if (y >= ud && x + d < map.get_sizeX())
							counter.update(z, map, wxtype, y - d, x + d);

						if (y + d < map.get_sizeY() && x >= ud) 
							counter.update(z, map, wxtype, y + d, x - d);

						if (y + d < map.get_sizeY() && x + d < map.get_sizeX()) 
							counter.update(z, map, wxtype, y + d, x + d);

						if (counter.validcount > 0)
							z /= counter.validcount;

						if (z != 0 || counter.zcount == 0)
							break;
					}

					if (z == 0) {
						// No valid neighbor data: approximate cumulus base from surface temperature (warmer → lower base, consistent with convective lapse).
						z = (std::min(GC_CUMULUS_FALLBACK_TEMP_MAX_C, std::max(GC_CUMULUS_FALLBACK_TEMP_MIN_C, map.get_temperature({y, x}))) + GC_CUMULUS_FALLBACK_TEMP_OFFSET_C) / (GC_CUMULUS_FALLBACK_TEMP_MAX_C + GC_CUMULUS_FALLBACK_TEMP_OFFSET_C) * (GC_MAX_SUNNY_CUMULUSZ - GC_MIN_CUMULUS_LAYER_ALT) + GC_MIN_CUMULUS_LAYER_ALT;
#ifdef FMAP_DEBUG
						dynamic_cast<fmap_debug_data*>(const_cast<fmap::debug_data*>(map.get_debugData({y, x})))->set_zinterpolated(2);
#endif
					}
#ifdef FMAP_DEBUG
					else {
						dynamic_cast<fmap_debug_data*>(const_cast<fmap::debug_data*>(map.get_debugData({y, x})))->set_zinterpolated(1);
					}
#endif
					map.set_cumulusBase({y, x}, z);
				}

				// FogLayerZ (v8): cumulus base (always populated after interpolation).
				// TODO: Same as default BMS behavior until we find a better way of calculating proper fogLayerZ from NOAA GRIB data (PGBL or equivalent).
				float fogLayerZ = map.get_cumulusBase({y, x});
				map.set_fogLayerZ({y, x}, fogLayerZ);

#ifdef FMAP_DEBUG
				if (const fmap::debug_data* dbg = map.get_debugData({y, x})) {
					if (auto* gdbg = dynamic_cast<fmap_debug_data*>(const_cast<fmap::debug_data*>(dbg)))
						gdbg->set_fogLayerZ(fogLayerZ);
				}
#endif
			}
		}

		int avgWindHdg = static_cast<int>(avgWindDir);
		if (avgWindHdg < 0)
			avgWindHdg += 360;
		map.set_mapWindHeading(avgWindHdg);
		map.set_mapWindSpeed(avgWindSpeed);
	}
	catch (grib_converter_exception& e) {
		setLastError(e.what());
		return 1;
	}
	return 0;
}

/**
 * Converts GRIB to a single fmap at time t and appends it to fmaps.
 * Allocates the fmap and delegates to convert_single(o, *map, t).
 * @param o Conversion options.
 * @param fmaps List to append the new fmap to.
 * @param t Forecast time in minutes.
 * @return 0 on success; 1 on error (map is not added on error).
 */
int grib_converter::convert_single(grib_converter_options& o, fmap_list& fmaps, unsigned long t)
{
	auto map = std::make_unique<fmap>(static_cast<unsigned int>(m_sizeY), static_cast<unsigned int>(m_sizeX));
	int rv = convert_single(o, *map, t);
	if (rv != 0)
		return rv;
	fmaps.m_fmaps.push_back(std::move(map));
	return 0;
}

/**
 * Converts GRIB to a sequence of fmaps at the configured interval up to max_forecast_hours.
 * Clears fmaps and fills it with one fmap per time step.
 * @param o Conversion options (interval_minutes, max_forecast_hours, etc.).
 * @param fmaps Output list of fmaps; cleared before use.
 * @return 0 if all steps succeeded; 1 on first failure.
 */
int grib_converter::convert_all(grib_converter_options& o, fmap_list& fmaps)
{
	int rv = 0;
	unsigned long t;

	fmaps.clear();

	for (t = 0; t <= o.max_forecast_hours * 60; t += o.interval_minutes) {
		rv = convert_single(o, fmaps, t);
		if (rv != 0)
			break;
	}
	return rv;
}

/**
 * Returns the maximum forecast time (in steps) for which all parameter
 * breakpoint lists have data. Used to cap the available forecast range.
 * endStep is in hours; min over all lists.
 * @return Maximum endStep across all breakpoint lists, or 0 if any list is empty.
 */
unsigned long grib_converter::get_max_possible_forecast() const
{
	unsigned long rv = FORECAST_HOURS_NO_LIMIT;
	for (size_t i = 0; i < num_grib_breakpoints; i++) {
		const auto& list = m_breakpoints[i];
		if (list.empty())
			return 0;
		rv = std::min(rv, static_cast<unsigned long>(list.back()->endStep));
	}
	return rv;
}