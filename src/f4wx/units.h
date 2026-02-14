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
 * Centralized unit conversion constants and helpers.
 * Single source of truth for length, speed, temperature, pressure, angle, and time.
 */

namespace units {

/* ---- Length ---- */
/** Feet per metre (exact factor). */
inline constexpr double FT_PER_M = 3.28084;

inline constexpr double ft_to_m(double x) { return x / FT_PER_M; }
inline constexpr float ft_to_m(float x) { return static_cast<float>(x / FT_PER_M); }

inline constexpr double m_to_km(double x) { return x / 1000.0; }
inline constexpr float m_to_km(float x) { return x / 1000.0f; }
inline constexpr double ft_to_km(double x) { return ft_to_m(x) / 1000.0; }

/* ---- Speed ---- */
/** Metres per second to knots. */
inline constexpr float MPS_TO_KT_FACTOR = 1.943844f;
inline constexpr float mps_to_kts(float x) { return x * MPS_TO_KT_FACTOR; }

/* ---- Temperature ---- */
inline constexpr float KELVIN_TO_CELSIUS = 273.15f;
inline constexpr float k_to_c(float x) { return x - KELVIN_TO_CELSIUS; }
inline constexpr float c_to_k(float x) { return x + KELVIN_TO_CELSIUS; }

/* ---- Pressure ---- */
inline constexpr float PA_TO_HPA_FACTOR = 100.0f;
inline constexpr float pa_to_hpa(float x) { return x / PA_TO_HPA_FACTOR; }
inline constexpr float hpa_to_pa(float x) { return x * PA_TO_HPA_FACTOR; }

/* ---- Angle ---- */
inline constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
inline constexpr float DEG_TO_RAD_F = static_cast<float>(DEG_TO_RAD);
inline constexpr double RAD_TO_DEG_FACTOR = 57.2957795130823208768;  // 180/pi
inline constexpr float rad_to_deg(float x) { return x * static_cast<float>(RAD_TO_DEG_FACTOR); }
inline constexpr float deg_to_rad(float x) { return x * DEG_TO_RAD_F; }

/* ---- Time / rate ---- */
inline constexpr float SEC_PER_HOUR = 3600.0f;
/** mm/s to mm/h. */
inline constexpr float mmsec_to_mmhr(float x) { return x * SEC_PER_HOUR; }
}
