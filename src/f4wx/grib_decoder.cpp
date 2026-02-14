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

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <span>

#include "grib_decoder.h"
#include "grib_constants.h"

#include "grib2.h"

namespace {

/** GRIB2 Section 0: minimum length and offset of total message length (8-byte big-endian). */
constexpr size_t GRIB2_SECTION0_MIN = 16;
constexpr size_t GRIB2_SECTION0_LEN_OFFSET = 8;

/** Returns total GRIB2 message length from Section 0, or 0 if invalid. */
size_t get_grib2_message_length(std::span<const unsigned char> buf)
{
	if (buf.size() < GRIB2_SECTION0_MIN)
		return 0;
	if (std::memcmp(buf.data(), "GRIB", 4) != 0 || buf[7] != 2)
		return 0;
	uint64_t len = (static_cast<uint64_t>(buf[8]) << 56) |
		(static_cast<uint64_t>(buf[9]) << 48) |
		(static_cast<uint64_t>(buf[10]) << 40) |
		(static_cast<uint64_t>(buf[11]) << 32) |
		(static_cast<uint64_t>(buf[12]) << 24) |
		(static_cast<uint64_t>(buf[13]) << 16) |
		(static_cast<uint64_t>(buf[14]) << 8) |
		static_cast<uint64_t>(buf[15]);
	if (len == 0 || len > buf.size())
		return 0;
	return static_cast<size_t>(len);
}

/** Map (discipline, parameter category, parameter number) to F4Wx param_id. Returns 0 if unknown. */
int param_id_from_grib2(int discipline, int category, int number)
{
	if (discipline != 0) /* Meteorological */
		return 0;
	switch (category) {
	case 0: /* Temperature */
		if (number == 0) return GRIB2_PARAM_2M_TEMP; /* Temperature (K) - 2m from product */
		break;
	case 2: /* Momentum: NCEP uses (2,2)/(2,3) for both 10m and isobaric; (2,0)/(2,1) also possible */
		if (number == 0) return GRIB2_PARAM_UGRD;
		if (number == 1) return GRIB2_PARAM_VGRD;
		if (number == 2) return GRIB2_PARAM_UGRD; /* 10u only when sfc=103/10m; see param_id() */
		if (number == 3) return GRIB2_PARAM_VGRD; /* 10v only when sfc=103/10m */
		break;
	case 3: /* Mass */
		if (number == 0) return GRIB2_PARAM_PRES;   /* Pressure (Pa) */
		if (number == 1) return GRIB2_PARAM_PRMSL; /* Pressure reduced to MSL */
		break;
	case 1: /* Moisture */
		if (number == 7) return GRIB2_PARAM_PRATE; /* Precipitation rate */
		break;
	case 6: /* Cloud */
		if (number == 1) return GRIB2_PARAM_TCC; /* Total cloud cover */
		break;
	case 19: /* Physical atmospheric chemical constituents (NCEP: visibility here) */
		if (number == 0) return GRIB2_PARAM_VIS; /* Visibility */
		if (number == 7) return GRIB2_PARAM_PRATE;
		break;
	case 20: /* Atmospheric chemical constituents / Visibility */
		if (number == 0) return GRIB2_PARAM_VIS; /* Visibility */
		break;
	default:
		break;
	}
	return 0;
}

/** Scale factor for lat/lon in GDS template 3.0: degrees = value * (basic_angle / subdivision). If both 0, use 1e-6. */
double grid_scale_factor(long long basic_angle, long long subdivision)
{
	if (basic_angle <= 0 || subdivision <= 0)
		return 1e-6;
	return static_cast<double>(basic_angle) / static_cast<double>(subdivision);
}

} // namespace

/* ------------------------------------------------------------------------- */
/* grib_field                                                                 */
/* ------------------------------------------------------------------------- */

grib_field::grib_field(gribfield* gfld) : m_gfld(gfld)
{
}

grib_field::~grib_field()
{
	if (m_gfld)
		g2_free(m_gfld);
	m_gfld = nullptr;
}

bool grib_field::is_valid() const
{
	return m_gfld != nullptr && m_gfld->idsect != nullptr &&
		m_gfld->igdtmpl != nullptr && m_gfld->ipdtmpl != nullptr &&
		m_gfld->fld != nullptr;
}

int grib_field::param_id() const
{
	if (!m_gfld || !m_gfld->ipdtmpl || m_gfld->ipdtlen < 2)
		return 0;
	int disc = static_cast<int>(m_gfld->discipline);
	int cat = static_cast<int>(m_gfld->ipdtmpl[0]);
	int num = static_cast<int>(m_gfld->ipdtmpl[1]);
	int base = param_id_from_grib2(disc, cat, num);
	/* 2m/surface temperature: height 2 m (103/2) or ground (1/0) for legacy compatibility. */
	if (base == GRIB2_PARAM_2M_TEMP) {
		int sfc = type_of_first_fixed_surface();
		long lev = level();
		if ((sfc != GRIB2_SFC_HEIGHT_ABOVE_GROUND || lev != GRIB2_LEVEL_2M)
			&& (sfc != GRIB2_SFC_GROUND || lev != 0))
			return 0;
	}
	/* NCEP GFS uses (2,2)/(2,3) for both 10m wind and U/V at pressure levels; route by surface. */
	if (base == GRIB2_PARAM_UGRD && num == 2) {
		if (type_of_first_fixed_surface() == GRIB2_SFC_HEIGHT_ABOVE_GROUND && level() == GRIB2_LEVEL_10M)
			return GRIB2_PARAM_10M_UWIND;
	}
	if (base == GRIB2_PARAM_VGRD && num == 3) {
		if (type_of_first_fixed_surface() == GRIB2_SFC_HEIGHT_ABOVE_GROUND && level() == GRIB2_LEVEL_10M)
			return GRIB2_PARAM_10M_VWIND;
	}
	/* Winds aloft: only supported isobaric levels (100–925 hPa); surface U/V (sfc=1/level=0) are skipped. */
	if (base == GRIB2_PARAM_UGRD && num == 0) {
		if (type_of_first_fixed_surface() != GRIB2_SFC_ISOBARIC || !grib2_is_aloft_level_hpa(level()))
			return 0;
	}
	if (base == GRIB2_PARAM_VGRD && num == 1) {
		if (type_of_first_fixed_surface() != GRIB2_SFC_ISOBARIC || !grib2_is_aloft_level_hpa(level()))
			return 0;
	}
	return base;
}

int grib_field::type_of_first_fixed_surface() const
{
	if (!m_gfld || !m_gfld->ipdtmpl || m_gfld->ipdtlen <= 9)
		return 0;
	/* PDS 4.0 octet 23 = type of first fixed surface. */
	return static_cast<int>(m_gfld->ipdtmpl[9]);
}

long grib_field::level() const
{
	if (!m_gfld || !m_gfld->ipdtmpl || m_gfld->ipdtlen <= 11)
		return 0;
	/* PDS 4.0 octets 24 and 25-28: scale factor and scaled value. Level = scaled_value * 10^(-scale_factor). */
	long long scale = m_gfld->ipdtmpl[10];
	long long scaled = m_gfld->ipdtmpl[11];
	long long raw;
	if (scale == 0)
		raw = scaled;
	else if (scale > 0) {
		long long factor = 1;
		for (long long i = 0; i < scale; i++)
			factor *= 10;
		raw = scaled / factor;
	} else {
		/* Negative scale: value = scaled * 10^(-scale), e.g. scale=-1 => scaled*10 */
		long long factor = 1;
		for (long long i = 0; i > scale; i--)
			factor *= 10;
		raw = scaled * factor;
	}
	/* Table 4.5 type 100 = isobaric surface (Pa); we use hPa for level constants. */
	if (m_gfld->ipdtlen > 9 && static_cast<int>(m_gfld->ipdtmpl[9]) == GRIB2_SFC_ISOBARIC)
		return static_cast<long>(raw / 100);
	return static_cast<long>(raw);
}

/** Convert forecast time to hours using PDS 4.0 unit (Code Table 4.4). */
static long forecast_time_hours(const gribfield* gfld)
{
	if (!gfld || !gfld->ipdtmpl || gfld->ipdtlen <= 8)
		return 0;
	long raw = static_cast<long>(gfld->ipdtmpl[8]);
	int unit = (gfld->ipdtlen > 7) ? static_cast<int>(gfld->ipdtmpl[7]) : GRIB2_TIME_UNIT_HOUR;
	/* Table 4.4: convert to hours for fmap step count. */
	switch (unit) {
		case GRIB2_TIME_UNIT_MINUTE: return raw / 60;
		case GRIB2_TIME_UNIT_HOUR:   return raw;
		case GRIB2_TIME_UNIT_DAY:    return raw * 24;
		case GRIB2_TIME_UNIT_3H:    return raw * 3;
		case GRIB2_TIME_UNIT_6H:    return raw * 6;
		case GRIB2_TIME_UNIT_12H:   return raw * 12;
		default:                     return raw;  /* assume hour */
	}
}

long grib_field::start_step() const
{
	return forecast_time_hours(m_gfld);
}

long grib_field::end_step() const
{
	/* Template 4.0 has one forecast time; use same as start. */
	return start_step();
}

long grib_field::year() const
{
	if (!m_gfld || !m_gfld->idsect || m_gfld->idsectlen <= 8)
		return 0;
	return static_cast<long>(m_gfld->idsect[5]);
}

long grib_field::month() const
{
	if (!m_gfld || !m_gfld->idsect || m_gfld->idsectlen <= 8)
		return 0;
	return static_cast<long>(m_gfld->idsect[6]);
}

long grib_field::day() const
{
	if (!m_gfld || !m_gfld->idsect || m_gfld->idsectlen <= 8)
		return 0;
	return static_cast<long>(m_gfld->idsect[7]);
}

long grib_field::hour() const
{
	if (!m_gfld || !m_gfld->idsect || m_gfld->idsectlen <= 8)
		return 0;
	return static_cast<long>(m_gfld->idsect[8]);
}

/* GDS template 3.0 layout (NCEP): [0]=shape, [1-2]=radius, [3-4]=major, [5-6]=minor,
 * [7]=Ni, [8]=Nj, [9]=basic_angle, [10]=subdivision, [11]=La1, [12]=Lo1, [13]=resolution,
 * [14]=La2, [15]=Lo2, [16]=Di, [17]=Dj, [18]=scanning. */
static double gds_scale(const gribfield* gfld)
{
	if (!gfld || !gfld->igdtmpl || gfld->igdtlen <= 10)
		return 1e-6;
	return grid_scale_factor(gfld->igdtmpl[9], gfld->igdtmpl[10]);
}

static bool is_latlon_grid(const gribfield* gfld)
{
	return gfld && gfld->igdtnum >= 0 && gfld->igdtnum <= 3;
}

double grib_field::latitude_first() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 11 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<double>(m_gfld->igdtmpl[11]) * gds_scale(m_gfld);
}

double grib_field::latitude_last() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 14 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<double>(m_gfld->igdtmpl[14]) * gds_scale(m_gfld);
}

double grib_field::longitude_first() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 12 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<double>(m_gfld->igdtmpl[12]) * gds_scale(m_gfld);
}

double grib_field::longitude_last() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 15 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<double>(m_gfld->igdtmpl[15]) * gds_scale(m_gfld);
}

double grib_field::i_direction_increment() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 16 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<double>(std::abs(static_cast<long long>(m_gfld->igdtmpl[16]))) * gds_scale(m_gfld);
}

double grib_field::j_direction_increment() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 17 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<double>(std::abs(static_cast<long long>(m_gfld->igdtmpl[17]))) * gds_scale(m_gfld);
}

long grib_field::ni() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 8 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<long>(m_gfld->igdtmpl[7]);
}

long grib_field::nj() const
{
	if (!m_gfld || !m_gfld->igdtmpl || m_gfld->igdtlen <= 8 || !is_latlon_grid(m_gfld))
		return 0;
	return static_cast<long>(m_gfld->igdtmpl[8]);
}

long grib_field::number_of_points() const
{
	if (!m_gfld)
		return 0;
	return static_cast<long>(m_gfld->ngrdpts > 0 ? m_gfld->ngrdpts : m_gfld->ndpts);
}

int grib_field::get_grid_data(std::span<double> lats, std::span<double> lons, std::span<double> values) const
{
	if (!m_gfld || !m_gfld->fld)
		return -1;
	/* Use full grid size: after bitmap expansion fld has ngrdpts elements; ndpts may still be pre-expand count. */
	size_t n = static_cast<size_t>(m_gfld->ngrdpts > 0 ? m_gfld->ngrdpts : m_gfld->ndpts);
	if (lats.size() < n || lons.size() < n || values.size() < n)
		return -1;

	long ni = (m_gfld->igdtmpl && m_gfld->igdtlen > 7) ? static_cast<long>(m_gfld->igdtmpl[7]) : 0;
	long nj = (m_gfld->igdtmpl && m_gfld->igdtlen > 8) ? static_cast<long>(m_gfld->igdtmpl[8]) : 0;
	if (ni <= 0 || nj <= 0)
		return -1;
	if (n != static_cast<size_t>(ni * nj))
		return -1;

	/* GRIB2 Table 3.4 scanning mode (igdtmpl[18]): bit1=+i/-i, bit2=+j/-j, bit3=consecutive i/j. */
	int scan = (m_gfld->igdtlen > 18) ? static_cast<int>(m_gfld->igdtmpl[18]) : 0;
	int i_pos = (scan & 1) == 0;   /* 0 = +i (left to right) */
	int j_pos = (scan & 2) != 0;  /* 1 = +j (bottom to top) */
	int consec_i = (scan & 4) == 0; /* 0 = consecutive in i (row-major) */

	if (m_gfld->num_coord > 0 && m_gfld->coord_list && n * 2 <= static_cast<size_t>(m_gfld->num_coord)) {
		for (size_t i = 0; i < n; i++) {
			lats[i] = static_cast<double>(m_gfld->coord_list[i * 2]);
			lons[i] = static_cast<double>(m_gfld->coord_list[i * 2 + 1]);
		}
		/* coord_list order matches fld[]; reorder values into canonical (row 0 = la1, col 0 = lo1). */
		double la1 = latitude_first();
		double lo1 = longitude_first();
		for (long j = 0; j < nj; j++) {
			for (long i = 0; i < ni; i++) {
				long gr = j_pos ? j : (nj - 1 - j);
				long gc = i_pos ? i : (ni - 1 - i);
				size_t grib_idx = consec_i ? (static_cast<size_t>(gr) * ni + gc) : (static_cast<size_t>(gc) * nj + gr);
				if (grib_idx < n)
					values[j * ni + i] = static_cast<double>(m_gfld->fld[grib_idx]);
			}
		}
		/* Rebuild lats/lons in canonical order to match values. Table 3.4: -j means first point at our row nj-1. */
		double di = i_direction_increment();
		double dj = j_direction_increment();
		for (long j = 0; j < nj; j++) {
			for (long i = 0; i < ni; i++) {
				size_t idx = static_cast<size_t>(j * ni + i);
				lats[idx] = la1 + (j_pos ? j : (nj - 1 - j)) * dj;
				lons[idx] = lo1 + i * di;
			}
		}
	} else {
		/* Build lat/lon in canonical order. j_pos: +j => first point at row 0; -j => first point at row nj-1. */
		double la1 = latitude_first();
		double lo1 = longitude_first();
		double di = i_direction_increment();
		double dj = j_direction_increment();
		for (long j = 0; j < nj; j++) {
			for (long i = 0; i < ni; i++) {
				size_t idx = static_cast<size_t>(j * ni + i);
				lats[idx] = la1 + (j_pos ? j : (nj - 1 - j)) * dj;
				lons[idx] = lo1 + i * di;
			}
		}
		/* Map GRIB stream order (fld[]) to canonical (j*ni+i). Table 3.4: row/col order depends on scanning. */
		for (long j = 0; j < nj; j++) {
			for (long i = 0; i < ni; i++) {
				long gr = j_pos ? j : (nj - 1 - j);
				long gc = i_pos ? i : (ni - 1 - i);
				size_t grib_idx = consec_i ? (static_cast<size_t>(gr) * ni + gc) : (static_cast<size_t>(gc) * nj + gr);
				if (grib_idx < n)
					values[j * ni + i] = static_cast<double>(m_gfld->fld[grib_idx]);
			}
		}
		/* Rebuild lats/lons in canonical order to match values. */
		for (long j = 0; j < nj; j++) {
			for (long i = 0; i < ni; i++) {
				size_t idx = static_cast<size_t>(j * ni + i);
				lats[idx] = la1 + (j_pos ? j : (nj - 1 - j)) * dj;
				lons[idx] = lo1 + i * di;
			}
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------- */
/* grib_file                                                                  */
/* ------------------------------------------------------------------------- */

grib_file::grib_file() = default;

grib_file::~grib_file() = default;

/* ------------------------------------------------------------------------- */
/* grib_decoder                                                               */
/* ------------------------------------------------------------------------- */

grib_decoder::grib_decoder() = default;

grib_decoder::~grib_decoder() = default;

int grib_decoder::decode(std::span<std::byte> data, grib_file* result)
{
	if (data.empty() || !result || data.size() < GRIB2_SECTION0_MIN)
		return -1;

	unsigned char* d = reinterpret_cast<unsigned char*>(data.data());
	size_t remain = data.size();
	int decoded = 0;

	while (remain >= GRIB2_SECTION0_MIN) {
		std::span<const unsigned char> current_buf(d, remain);
		size_t msg_len = get_grib2_message_length(current_buf);
		if (msg_len == 0)
			break;

		g2int listsec0[3], listsec1[13];
		g2int numfields = 0, numlocal = 0;
		g2int ret = g2_info(d, listsec0, listsec1, &numfields, &numlocal);
		if (ret != 0 || numfields <= 0) {
			d += msg_len;
			remain -= msg_len;
			continue;
		}

		for (g2int ifld = 1; ifld <= numfields; ifld++) {
			gribfield* gfld = nullptr;
			ret = g2_getfld(d, ifld, 1, 1, &gfld);
			if (ret != 0 || !gfld) {
				if (gfld) g2_free(gfld);
				continue;
			}
			auto field = std::make_unique<grib_field>(gfld);
			if (field->is_valid())
				result->m_fields.push_back(std::move(field));
			else
				; /* field destructor frees gfld */
		}

		d += msg_len;
		remain -= msg_len;
		decoded++;
	}

	return (result->get_message_count() > 0) ? 0 : -1;
}
