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

#include <Windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>

#include "f4wx_preview.h"
#include "units.h"

#include <cmath>
#include <numeric>

using std::vector;

namespace {

	struct PressureCenter {
		int x, y;       // Grid coordinates
		char type;      // 'H' or 'L'
		float pressure; // Value in hPa/mb
	};

	int calculate_synoptic_radius(double map_size_km, int grid_width) {
		if (map_size_km < 1.0) return 2;
		// R represents ~200 km in grid cells.
		// A 200km radius (400km diameter window) is a balanced choice for theater-sized maps (1024-2048km).
		// It filters mesoscale noise while allowing detection of systems that might be centered closer to the edges
		// or are part of a multi-center complex, improving detection on smaller grids like Korea (1024km).
		int R = static_cast<int>(std::round(grid_width * (200.0 / map_size_km)));
		return (std::max)(R, 2);
	}

	std::vector<PressureCenter> find_pressure_systems(const fmap& map, double map_size_km) {
		std::vector<PressureCenter> centers;
		int width = map.get_sizeX();
		int height = map.get_sizeY();

		if (width <= 0 || height <= 0) return centers;

		int R = calculate_synoptic_radius(map_size_km, width);

		// Helper to safely access data (fmap uses y, x)
		auto get_p = [&](int x, int y) { return map.get_pressure({ static_cast<unsigned int>(y), static_cast<unsigned int>(x) }); };

		// Boundary Safety: Do not process pixels within distance R of the grid edges.
		for (int y = R; y < height - R; ++y) {
			for (int x = R; x < width - R; ++x) {
				float val = get_p(x, y);

				// Step 1: Fast Fail (8 neighbors)
				bool is_local_max = true;
				bool is_local_min = true;

				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dy == 0) continue;
						float neighbor = get_p(x + dx, y + dy);
						if (neighbor >= val) is_local_max = false;
						if (neighbor <= val) is_local_min = false;
						if (!is_local_max && !is_local_min) break;
					}
					if (!is_local_max && !is_local_min) break;
				}

				if (!is_local_max && !is_local_min) continue;

				// Step 2: Synoptic Check (full window R)
				bool is_synoptic_max = is_local_max;
				bool is_synoptic_min = is_local_min;

				for (int dy = -R; dy <= R; ++dy) {
					for (int dx = -R; dx <= R; ++dx) {
						if (dx == 0 && dy == 0) continue;
						float neighbor = get_p(x + dx, y + dy);
						if (is_synoptic_max && neighbor > val) is_synoptic_max = false;
						if (is_synoptic_min && neighbor < val) is_synoptic_min = false;

						if (!is_synoptic_max && !is_synoptic_min) break;
					}
					if (!is_synoptic_max && !is_synoptic_min) break;
				}

				if (!is_synoptic_max && !is_synoptic_min) continue;

				// Step 3: Prominence/Hysteresis
				// Diff from average pressure of window's perimeter
				double perimeter_sum = 0.0;
				int perimeter_count = 0;

				for (int dx = -R; dx <= R; ++dx) {
					perimeter_sum += get_p(x + dx, y - R);
					perimeter_sum += get_p(x + dx, y + R);
					perimeter_count += 2;
				}
				// Left and Right columns (excluding corners already counted)
				for (int dy = -R + 1; dy <= R - 1; ++dy) {
					perimeter_sum += get_p(x - R, y + dy);
					perimeter_sum += get_p(x + R, y + dy);
					perimeter_count += 2;
				}

				float perimeter_avg = static_cast<float>(perimeter_sum / perimeter_count);
				
				// Threshold: 2.0 hPa (0.5 * standard isobar interval).
				// This catches significant centers even in weaker gradients while avoiding
				// flat field noise. 4.0 hPa was too strict for theater-scale detection.
				if (std::abs(val - perimeter_avg) < 2.0f) continue;

				centers.push_back({ x, y, is_synoptic_max ? 'H' : 'L', val });
			}
		}

		// Step 4: De-Clustering
		// If multiple candidates are found within distance R, keep most extreme.
		std::vector<bool> suppressed(centers.size(), false);
		float decluster_dist_sq = static_cast<float>(R * R);

		for (size_t i = 0; i < centers.size(); ++i) {
			if (suppressed[i]) continue;
			for (size_t j = i + 1; j < centers.size(); ++j) {
				if (suppressed[j]) continue;

				float dx = static_cast<float>(centers[i].x - centers[j].x);
				float dy = static_cast<float>(centers[i].y - centers[j].y);

				if (dx * dx + dy * dy <= decluster_dist_sq) {
					bool keep_i = true;
					if (centers[i].type == centers[j].type) {
						if (centers[i].type == 'H')
							keep_i = centers[i].pressure >= centers[j].pressure;
						else
							keep_i = centers[i].pressure <= centers[j].pressure;
					}
					// If types differ, we keep both (e.g. tight gradient) or just ignore collision.
					// Let's assume we handle same-type clustering primarily.

					if (keep_i) suppressed[j] = true;
					else suppressed[i] = true;
				}
			}
		}

		std::vector<PressureCenter> result;
		result.reserve(centers.size());
		for (size_t i = 0; i < centers.size(); ++i) {
			if (!suppressed[i]) result.push_back(centers[i]);
		}

		return result;
	}
}

f4wx_preview::f4wx_preview()
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&m_gdiplus_token, &gdiplusStartupInput, nullptr);
}

f4wx_preview::~f4wx_preview()
{
	m_graphics.reset();
	m_bitmap.reset();
	Gdiplus::GdiplusShutdown(m_gdiplus_token);
}

void f4wx_preview::set_background(Gdiplus::Bitmap* background)
{
	m_background = background;
	cleanup();
}

void f4wx_preview::cleanup()
{
	m_graphics.reset();
	m_bitmap.reset();

	if (m_background != nullptr) {
		m_bitmap.reset(m_background->Clone(0, 0, m_background->GetWidth(), m_background->GetHeight(), m_background->GetPixelFormat()));
		m_graphics.reset(Gdiplus::Graphics::FromImage(m_bitmap.get()));

		m_graphics->SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
		assert(m_bitmap != nullptr);
		assert(m_graphics != nullptr);
	}
}


void f4wx_preview::draw_clouds(const fmap& map)
{
	for (unsigned int y = 0; y < map.get_sizeY(); y++) {
		for (unsigned int x = 0; x < map.get_sizeX(); x++) {
			Gdiplus::Color col;
			switch (map.get_type({y, x})) {
			case WX_SUNNY:
				col = Gdiplus::Color::Transparent;
				break;
			case WX_FAIR:
				col = (Gdiplus::Color::Green & ~Gdiplus::Color::AlphaMask) | (0x7f << Gdiplus::Color::AlphaShift);
				break;
			case WX_POOR:
				col = (Gdiplus::Color::Yellow & ~Gdiplus::Color::AlphaMask) | (0x7f << Gdiplus::Color::AlphaShift);
				break;
			case WX_INCLEMENT:
				col = (Gdiplus::Color::Red & ~Gdiplus::Color::AlphaMask) | (0x7f << Gdiplus::Color::AlphaShift);
				break;
			default:
				assert(FALSE);
			}
			long px = (x * m_bitmap->GetWidth()) / map.get_sizeX();
			long py = (y * m_bitmap->GetHeight()) / map.get_sizeY();
			Gdiplus::SolidBrush brush(col);
			Gdiplus::Rect rect(px, py, ((x + 1) * m_bitmap->GetWidth()) / map.get_sizeX() - px, ((y + 1) * m_bitmap->GetHeight()) / map.get_sizeY() - py);
			m_graphics->FillRectangle(&brush, rect);
		}
	}
}

struct temperature_color {
	float temperature;
	Gdiplus::Color rgb;
};

static temperature_color temperature_gradients[] = {
	{ -40, Gdiplus::Color(0, 127, 255) },
	{ -12, Gdiplus::Color(0, 0, 255) },
	{ -9, Gdiplus::Color(0, 30, 255) },
	{ -6, Gdiplus::Color(0, 60, 255) },
	{ -5, Gdiplus::Color(20, 100, 255) },
	{ 0, Gdiplus::Color(40, 140, 255) },
	{ 3, Gdiplus::Color(0, 175, 255) },
	{ 6, Gdiplus::Color(0, 220, 225) },
	{ 9, Gdiplus::Color(0, 247, 176) },
	{ 12, Gdiplus::Color(0, 234, 156) },
	{ 15, Gdiplus::Color(130, 240, 89) },
	{ 18, Gdiplus::Color(240, 245, 3) },
	{ 21, Gdiplus::Color(255, 237, 0) },
	{ 24, Gdiplus::Color(255, 219, 0) },
	{ 27, Gdiplus::Color(255, 199, 0) },
	{ 30, Gdiplus::Color(255, 180, 0) },
	{ 33, Gdiplus::Color(255, 152, 0) },
	{ 36, Gdiplus::Color(255, 126, 0) },
	{ 39, Gdiplus::Color(247, 120, 0) },
	{ 42, Gdiplus::Color(236, 120, 0) },
	{ 45, Gdiplus::Color(228, 113, 30) },
	{ 48, Gdiplus::Color(224, 97, 40) },
	{ 51, Gdiplus::Color(220, 81, 50) },
	{ 54, Gdiplus::Color(213, 69, 60) },
	{ 57, Gdiplus::Color(205, 58, 70) },
	{ 60, Gdiplus::Color(190, 44, 80) },
	{ 63, Gdiplus::Color(180, 26, 90) },
	{ 66, Gdiplus::Color(170, 20, 100) },
	{ 70, Gdiplus::Color(150, 40, 120) },
	{ 75, Gdiplus::Color(140, 50, 140) },
};

// Interpolate value at x from values q1,q2 at coordinates x1 and x2 
inline double linear_interpolation(double q1, double q2, double x1, double x2, double x)
{
	double f = (x - x1) / (x2 - x1);
	return (q1 * (1.0f - f)) + (q2 * f);
}

static Gdiplus::Color get_temperature_color(float temp) {
	temp = std::clamp(temp, temperature_gradients[0].temperature, temperature_gradients[std::size(temperature_gradients) - 1].temperature);

	auto it = std::upper_bound(std::begin(temperature_gradients), std::end(temperature_gradients), temp,
		[](float t, const temperature_color& a) { return t < a.temperature; });
	size_t x2 = (it == std::end(temperature_gradients)) ? std::size(temperature_gradients) - 1 : static_cast<size_t>(it - std::begin(temperature_gradients));
	size_t x1 = (x2 == 0) ? 0 : x2 - 1;

	unsigned r, g, b;

	if (x2 == x1) {
		r = temperature_gradients[x1].rgb.GetR();
		g = temperature_gradients[x1].rgb.GetG();
		b = temperature_gradients[x1].rgb.GetB();
	}
	else {
		r = static_cast<unsigned int>(linear_interpolation(temperature_gradients[x1].rgb.GetR(), temperature_gradients[x2].rgb.GetR(), temperature_gradients[x1].temperature, temperature_gradients[x2].temperature, temp) + 0.5);
		g = static_cast<unsigned int>(linear_interpolation(temperature_gradients[x1].rgb.GetG(), temperature_gradients[x2].rgb.GetG(), temperature_gradients[x1].temperature, temperature_gradients[x2].temperature, temp) + 0.5);
		b = static_cast<unsigned int>(linear_interpolation(temperature_gradients[x1].rgb.GetB(), temperature_gradients[x2].rgb.GetB(), temperature_gradients[x1].temperature, temperature_gradients[x2].temperature, temp) + 0.5);
	}

	return Gdiplus::Color(0x6f, r, g, b);
}

void f4wx_preview::draw_temperature(const fmap& map)
{
	for (unsigned int y = 0; y < map.get_sizeY(); y++) {
		for (unsigned int x = 0; x < map.get_sizeX(); x++) {
			Gdiplus::Color col = get_temperature_color(map.get_temperature({y, x}));
			long px = (x * m_bitmap->GetWidth()) / map.get_sizeX();
			long py = (y * m_bitmap->GetHeight()) / map.get_sizeY();
			Gdiplus::SolidBrush brush(col);
			Gdiplus::Rect rect(px, py, ((x + 1) * m_bitmap->GetWidth()) / map.get_sizeX() - px, ((y + 1) * m_bitmap->GetHeight()) / map.get_sizeY() - py);
			m_graphics->FillRectangle(&brush, rect);
		}
	}

}

struct pressure_breakpoint {
	int pressure;
	vector<Gdiplus::Point> points;
};

// Removed obsolete helper functions (add_pressure_breakpoint, draw_pressure_line)
// Using Marching Squares in draw_pressure directly instead.

void f4wx_preview::draw_pressure(const fmap& map)
{
	// Marching Squares table
	// Edge 0: Top, 1: Right, 2: Bottom, 3: Left
	// -1 means end of line segments for this case
	static const int TABLE[16][5] = {
		{-1, -1, -1, -1, -1}, // 0000
		{ 3,  2, -1, -1, -1}, // 0001 (BL) -> Left to Bottom
		{ 2,  1, -1, -1, -1}, // 0010 (BR) -> Bottom to Right
		{ 3,  1, -1, -1, -1}, // 0011 (BL+BR) -> Left to Right
		{ 1,  0, -1, -1, -1}, // 0100 (TR) -> Right to Top
		{ 0,  3,  1,  2, -1}, // 0101 (TR+BL) -> Saddle
		{ 0,  2, -1, -1, -1}, // 0110 (TR+BR) -> Top to Bottom
		{ 3,  0, -1, -1, -1}, // 0111 (TR+BR+BL) -> Left to Top
		{ 0,  3, -1, -1, -1}, // 1000 (TL) -> Top to Left
		{ 0,  2, -1, -1, -1}, // 1001 (TL+BL) -> Top to Bottom
		{ 0,  1,  2,  3, -1}, // 1010 (TL+BR) -> Saddle
		{ 0,  1, -1, -1, -1}, // 1011 (TL+BR+BL) -> Top to Right
		{ 1,  3, -1, -1, -1}, // 1100 (TL+TR) -> Right to Left
		{ 1,  2, -1, -1, -1}, // 1101 (TL+TR+BL) -> Right to Bottom
		{ 2,  3, -1, -1, -1}, // 1110 (TL+TR+BR) -> Bottom to Left
		{-1, -1, -1, -1, -1}  // 1111
	};

	Gdiplus::Pen pen(Gdiplus::Color(255, 92, 64, 51));
	float scaleX = static_cast<float>(m_bitmap->GetWidth()) / map.get_sizeX();
	float scaleY = static_cast<float>(m_bitmap->GetHeight()) / map.get_sizeY();

	for (unsigned int y = 0; y < map.get_sizeY() - 1; y++) {
		for (unsigned int x = 0; x < map.get_sizeX() - 1; x++) {
			float q00 = map.get_pressure({ y, x });
			float q10 = map.get_pressure({ y, x + 1 });
			float q11 = map.get_pressure({ y + 1, x + 1 });
			float q01 = map.get_pressure({ y + 1, x });

			float minP = (std::min)({ q00, q10, q11, q01 });
			float maxP = (std::max)({ q00, q10, q11, q01 });

			int startLevel = static_cast<int>(ceil(minP));
			while (startLevel % 4 != 0) startLevel++;

			for (int level = startLevel; level <= maxP; level += 4) {
				int caseIdx = 0;
				if (q00 >= level) caseIdx |= 8;
				if (q10 >= level) caseIdx |= 4;
				if (q11 >= level) caseIdx |= 2;
				if (q01 >= level) caseIdx |= 1;

				if (caseIdx == 0 || caseIdx == 15) continue;

				const int* edges = TABLE[caseIdx];
				for (int i = 0; i < 5 && edges[i] != -1; i += 2) {
					int e1 = edges[i];
					int e2 = edges[i + 1];

					auto getPoint = [&](int edge) -> Gdiplus::PointF {
						float ix = 0.0f, iy = 0.0f;
						if (edge == 0) { // Top
							float t = (level - q00) / (q10 - q00);
							ix = x + t; iy = static_cast<float>(y);
						}
						else if (edge == 1) { // Right
							float t = (level - q10) / (q11 - q10);
							ix = x + 1.0f; iy = y + t;
						}
						else if (edge == 2) { // Bottom
							float t = (level - q01) / (q11 - q01);
							ix = x + t; iy = y + 1.0f;
						}
						else { // Left
							float t = (level - q00) / (q01 - q00);
							ix = static_cast<float>(x); iy = y + t;
						}
						return Gdiplus::PointF(ix * scaleX, iy * scaleY);
					};

					Gdiplus::PointF p1 = getPoint(e1);
					Gdiplus::PointF p2 = getPoint(e2);
					m_graphics->DrawLine(&pen, p1, p2);
				}
			}
		}
	}

	// Map size in km for pressure-system radius (CELLSIZE is ft per cell)
	double map_size_km = map.get_sizeX() * units::ft_to_km(CELLSIZE);
	auto centers = find_pressure_systems(map, map_size_km);

	Gdiplus::FontFamily fontFamily(L"Arial");
	Gdiplus::Font font(&fontFamily, 16, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush blueBrush(Gdiplus::Color(255, 0, 0, 255));
	Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 255, 0, 0));
	Gdiplus::StringFormat stringFormat;
	stringFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
	stringFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	for (const auto& center : centers) {
		Gdiplus::PointF point(center.x * scaleX, center.y * scaleY);
		if (center.type == 'H') {
			m_graphics->DrawString(L"H", -1, &font, point, &stringFormat, &blueBrush);
		}
		else {
			m_graphics->DrawString(L"L", -1, &font, point, &stringFormat, &redBrush);
		}
	}
}

static void draw_wind_symbol(Gdiplus::Graphics *m_graphics, float x, float y, float hdg, float spd, float linesize)
{
	float rad = units::deg_to_rad(hdg);
	float nor = units::deg_to_rad(hdg < 90.0f ? hdg - 90.0f : hdg - 90.0f + 360.0f);

	Gdiplus::Pen pen(Gdiplus::Color::Black, 1);
	float x1 = x - sinf(rad) * linesize;
	float y1 = y + cosf(rad) * linesize;
	float x2 = x + sinf(rad) * linesize;
	float y2 = y - cosf(rad) * linesize;

	m_graphics->DrawLine(&pen, x1, y1, x2, y2);

	while (spd > 0.0f) {
		if (spd >= 50.0f) {
			x1 = x2 - sinf(nor) * (linesize * 2.0f) / 3.0f;
			y1 = y2 + cosf(nor) * (linesize * 2.0f) / 3.0f;
			m_graphics->DrawLine(&pen, x2, y2, x1, y1);
			m_graphics->DrawLine(&pen, x2 + sinf(rad) * (linesize / 3.0f), y2 - cosf(rad) * (linesize / 3.0f), x1, y1);
			spd -= 50;
		}
		else if (spd >= 10.0f) {
			x1 = x2 - sinf(nor) * (linesize * 2.0f) / 3.0f;
			y1 = y2 + cosf(nor) * (linesize * 2.0f) / 3.0f;
			m_graphics->DrawLine(&pen, x2, y2, x1, y1);
			spd -= 10.0f;
		}
		else if (spd >= 5.0f) {
			x1 = x2 - sinf(nor) * linesize / 3.0f;
			y1 = y2 + cosf(nor) * linesize / 3.0f;
			m_graphics->DrawLine(&pen, x2, y2, x1, y1);
			spd -= 5.0f;
		}
		else
			break;

		x2 -= sinf(rad) * (linesize / 3.0f);
		y2 += cosf(rad) * (linesize / 3.0f);
	}
}

void f4wx_preview::draw_wind(const fmap& map, size_t level)
{
	float lineSize = m_bitmap->GetWidth() / 40.0f;

	for (unsigned int y = 0; y < map.get_sizeY(); y += (30 * map.get_sizeY()) / m_bitmap->GetHeight()) {
		for (unsigned int x = 1; x < map.get_sizeX(); x += (30 * map.get_sizeX()) / m_bitmap->GetWidth()) {
			float px = static_cast<float>((x * m_bitmap->GetWidth()) / map.get_sizeX()) + lineSize/2.0f;
			float py = static_cast<float>((y * m_bitmap->GetHeight()) / map.get_sizeY()) + lineSize;
			assert(level < NUM_ALOFT_BREAKPOINTS);
			draw_wind_symbol(m_graphics.get(), px, py, map.get_windDirection({y, x}, static_cast<int>(level)), map.get_windSpeed({y, x}, static_cast<int>(level)), lineSize);
		}
	}
}
