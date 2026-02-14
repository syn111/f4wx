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

#include <cstddef>

/**
 * GRIB2 and GRIB-related numeric constants (paramId, Table 4.5 surface types,
 * pressure levels, missing value, coordinate bounds). References: WMO GRIB2,
 * WMO GRIB2 and NCEPLIBS-g2c (missingValue).
 */

/* ---- GRIB missing value (definitions/grib2/boot.def: transient missingValue = 9999) ---- */
inline constexpr int GRIB_MISSING_VALUE = 9999;

/* ---- GRIB coordinate bounds (degrees) ---- */
inline constexpr int GRIB_LON_HALF_RANGE = 180;
inline constexpr int GRIB_LAT_MAX = 90;
inline constexpr int GRIB_LON_NORMALIZE = 360;

/* ---- GRIB2 parameter IDs (paramId; WMO) ---- */
inline constexpr int GRIB2_PARAM_2M_TEMP = 167;       /* T, 2 m temperature */
inline constexpr int GRIB2_PARAM_10M_UWIND = 165;   /* 10u, 10 m u-wind */
inline constexpr int GRIB2_PARAM_10M_VWIND = 166;    /* 10v, 10 m v-wind */
inline constexpr int GRIB2_PARAM_PRMSL = 260074;     /* Mean sea level pressure */
inline constexpr int GRIB2_PARAM_TCC = 228164;       /* Total cloud cover */
inline constexpr int GRIB2_PARAM_PRATE = 3059;      /* Precipitation rate */
inline constexpr int GRIB2_PARAM_UGRD = 131;        /* U wind (aloft) */
inline constexpr int GRIB2_PARAM_VGRD = 132;        /* V wind (aloft) */
inline constexpr int GRIB2_PARAM_VIS = 3020;         /* Visibility */
inline constexpr int GRIB2_PARAM_PRES = 54;         /* Pressure */

/* ---- GRIB2 Table 4.5: typeOfFirstFixedSurface (NCEP local 192-254) ---- */
/* Ref: https://www.nco.ncep.noaa.gov/pmb/docs/grib2/grib2_doc/grib2_table4-5.shtml */
inline constexpr int GRIB2_SFC_ISOBARIC = 100;               /* Isobaric surface (Pa); Table 4.5 code 100 */
inline constexpr int GRIB2_SFC_GROUND = 1;                    /* Ground or water surface */
inline constexpr int GRIB2_SFC_HEIGHT_ABOVE_GROUND = 103;     /* Height above ground (m) */
/* NCEP local Table 4.5: cloud layer base/top (Pressure cat=3,num=0; TCC cat=6,num=1 sfc=244) */
inline constexpr int GRIB2_SFC_CONVECTIVE_CLOUD_LAYER = 244; /* Convective cloud layer (TCC) */
inline constexpr int GRIB2_SFC_CLOUD_BASE_CONV = 242;         /* Convective cloud bottom level */
inline constexpr int GRIB2_SFC_CLOUD_BASE_LO = 212;          /* Low cloud bottom level */
inline constexpr int GRIB2_SFC_CLOUD_BASE_MID = 222;         /* Middle cloud bottom level */
inline constexpr int GRIB2_SFC_CLOUD_BASE_HI = 232;          /* High cloud bottom level */
inline constexpr int GRIB2_SFC_CLOUD_TOP_CONV = 243;         /* Convective cloud top level */
inline constexpr int GRIB2_SFC_CLOUD_TOP_LO = 213;           /* Low cloud top level */
inline constexpr int GRIB2_SFC_CLOUD_TOP_MID = 223;          /* Middle cloud top level */
inline constexpr int GRIB2_SFC_CLOUD_TOP_HI = 233;           /* High cloud top level */

/* ---- Height levels (m) for 2m/10m parameters ---- */
inline constexpr long GRIB2_LEVEL_2M = 2;
inline constexpr long GRIB2_LEVEL_10M = 10;

/* ---- GRIB2 isobaric levels (hPa) used for TCC and winds aloft ---- */
inline constexpr int GRIB2_LEVEL_100_HPA = 100;
inline constexpr int GRIB2_LEVEL_150_HPA = 150;
inline constexpr int GRIB2_LEVEL_200_HPA = 200;
inline constexpr int GRIB2_LEVEL_300_HPA = 300;
inline constexpr int GRIB2_LEVEL_400_HPA = 400;
inline constexpr int GRIB2_LEVEL_500_HPA = 500;
inline constexpr int GRIB2_LEVEL_700_HPA = 700;
inline constexpr int GRIB2_LEVEL_850_HPA = 850;
inline constexpr int GRIB2_LEVEL_925_HPA = 925;

/** True if hpa is one of the supported isobaric levels for winds aloft (100–925 hPa). */
inline constexpr bool grib2_is_aloft_level_hpa(long hpa)
{
	return hpa == GRIB2_LEVEL_100_HPA || hpa == GRIB2_LEVEL_150_HPA || hpa == GRIB2_LEVEL_200_HPA
		|| hpa == GRIB2_LEVEL_300_HPA || hpa == GRIB2_LEVEL_400_HPA || hpa == GRIB2_LEVEL_500_HPA
		|| hpa == GRIB2_LEVEL_700_HPA || hpa == GRIB2_LEVEL_850_HPA || hpa == GRIB2_LEVEL_925_HPA;
}

/* ---- GRIB2 Table 4.4: forecast time unit (indicatorOfUnitOfTimeRange) ---- */
inline constexpr int GRIB2_TIME_UNIT_MINUTE = 0;
inline constexpr int GRIB2_TIME_UNIT_HOUR = 1;
inline constexpr int GRIB2_TIME_UNIT_DAY = 2;
inline constexpr int GRIB2_TIME_UNIT_3H = 10;
inline constexpr int GRIB2_TIME_UNIT_6H = 11;
inline constexpr int GRIB2_TIME_UNIT_12H = 12;
