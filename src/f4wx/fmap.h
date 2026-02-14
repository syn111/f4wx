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
 * BMS weather map (fmap): grid of cells with pressure, temperature, wind,
 * clouds, visibility; map-level wind/stratus/contrail; and binary save format.
 */

#include "config.h"
#include "types.h"
#include "units.h"

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

/** Weather condition band used for thresholds and BMS output. */
enum fmap_wxtype
{
	WX_SUNNY,
	WX_FAIR,
	WX_POOR,
	WX_INCLEMENT,

	NUM_WEATHER_TYPES
};

static const char* fmap_wxtype_text[] = {
	"Sunny",
	"Fair",
	"Poor",
	"Inclement"
};

// BMS Constants
inline constexpr double CELLSIZE = 57344.0;

inline constexpr float fmap_aloft_breakpoints[] = {
	0.0f,
	3000.0f,
	6000.0f,
	9000.0f,
	12000.0f,
	18000.0f,
	24000.0f,
	30000.0f,
	40000.0f,
	50000.0f
};

inline constexpr std::size_t NUM_ALOFT_BREAKPOINTS = std::size(fmap_aloft_breakpoints);

/** Single time-step weather grid for BMS: cells + map-level fields; supports save(). */
class fmap {
public:
	/** @param sizeY Number of rows (BMS TheaterYCells). @param sizeX Number of columns (BMS TheaterXCells). */
	fmap(unsigned int sizeY, unsigned int sizeX)
		: m_sizeY(sizeY), m_sizeX(sizeX), m_cells(static_cast<size_t>(sizeY) * sizeX)
	{
	}

	~fmap() = default;

	bool save(const std::filesystem::path& path, bool overwrite = false) const
	{
		if (!overwrite && std::filesystem::exists(path))
			return false;

		std::ofstream out(path, std::ios::binary);
		if (!out)
			return false;

		auto write = [&out](const void* buf, std::size_t size) {
			return static_cast<bool>(out.write(static_cast<const char*>(buf), static_cast<std::streamsize>(size)));
		};

		if (!write(&c_version, sizeof(c_version)) ||
			!write(&m_sizeY, sizeof(m_sizeY)) ||
			!write(&m_sizeX, sizeof(m_sizeX)) ||
			!write(&m_mapWindHeading, sizeof(m_mapWindHeading)) ||
			!write(&m_mapWindSpeed, sizeof(m_mapWindSpeed)) ||
			!write(&m_mapStratusZFair, sizeof(m_mapStratusZFair)) ||
			!write(&m_mapStratusZInc, sizeof(m_mapStratusZInc)) ||
			!write(&m_mapContrailLayer, sizeof(m_mapContrailLayer)))
			return false;

		for (unsigned int y = 0; y < m_sizeY; y++) {
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				int n = m_cells[_index(c)].basicCondition + 1;
				if (!write(&n, sizeof(m_cells[_index(c)].basicCondition)))
					return false;
			}
		}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].pressure, sizeof(m_cells[_index(c)].pressure)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].temperature, sizeof(m_cells[_index(c)].temperature)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].windSpeed, sizeof(m_cells[_index(c)].windSpeed)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].windDir, sizeof(m_cells[_index(c)].windDir)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].cumulusBase, sizeof(m_cells[_index(c)].cumulusBase)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].cumulusDensity, sizeof(m_cells[_index(c)].cumulusDensity)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].cumulusSize, sizeof(m_cells[_index(c)].cumulusSize)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].hasTowerCumulus, sizeof(m_cells[_index(c)].hasTowerCumulus)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].hasShowerCumulus, sizeof(m_cells[_index(c)].hasShowerCumulus)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].fogEndBelowLayerMapData, sizeof(m_cells[_index(c)].fogEndBelowLayerMapData)))
					return false;
			}

		for (unsigned int y = 0; y < m_sizeY; y++)
			for (unsigned int x = 0; x < m_sizeX; x++) {
				cell_index c{ y, x };
				if (!write(&m_cells[_index(c)].fogLayerZ, sizeof(m_cells[_index(c)].fogLayerZ)))
					return false;
			}

		return true;
	}


	// Global getters/setters

	/** Number of rows (BMS TheaterYCells). */
	inline unsigned get_sizeY() const { return m_sizeY; }
	/** Number of columns (BMS TheaterXCells). */
	inline unsigned get_sizeX() const { return m_sizeX; }

	inline int get_mapWindHeading() const { return m_mapWindHeading; }
	inline void set_mapWindHeading(int h) { m_mapWindHeading = h; }

	inline float get_mapWindSpeed() const { return m_mapWindSpeed; }
	inline void set_mapWindSpeed(float s) { m_mapWindSpeed = s; }

	inline int get_mapStratusZFair() const { return m_mapStratusZFair; }
	inline void set_mapStratusZFair(int z) { m_mapStratusZFair = z; }

	inline int get_mapStratusZInc() const { return m_mapStratusZInc; }
	inline void set_mapStratusZInc(int z) { m_mapStratusZInc = z; }


	// Cell getters/setters (cell_index: y = row, x = column; row-major index = y * sizeX + x)

	inline fmap_wxtype get_type(cell_index c) const { return (fmap_wxtype)m_cells[_index(c)].basicCondition; }
	inline void set_type(cell_index c, fmap_wxtype val) { m_cells[_index(c)].basicCondition = (unsigned)val; }

	inline fmap_wxtype get_basicCondition(cell_index c) const { return get_type(c); }
	inline void set_basicCondition(cell_index c, fmap_wxtype val) { set_type(c, val); }

	inline float get_pressure(cell_index c) const { return m_cells[_index(c)].pressure; }
	inline void set_pressure(cell_index c, float val) { m_cells[_index(c)].pressure = val; }

	inline float get_temperature(cell_index c) const { return m_cells[_index(c)].temperature; }
	inline void set_temperature(cell_index c, float val) { m_cells[_index(c)].temperature = val; }

	inline float get_windDirection(cell_index c, int k = 0) const { return m_cells[_index(c)].windDir[k]; }
	inline void set_windDirection(cell_index c, float val, int k = 0) { m_cells[_index(c)].windDir[k] = val; }

	inline float get_windSpeed(cell_index c, int k = 0) const { return m_cells[_index(c)].windSpeed[k]; }
	inline void set_windSpeed(cell_index c, float val, int k = 0) { m_cells[_index(c)].windSpeed[k] = val; }

	inline float get_cumulusBase(cell_index c) const { return m_cells[_index(c)].cumulusBase; }
	inline void set_cumulusBase(cell_index c, float val) { m_cells[_index(c)].cumulusBase = val; }

	inline int get_cumulusDensity(cell_index c) const { return m_cells[_index(c)].cumulusDensity; }
	inline void set_cumulusDensity(cell_index c, int val) { m_cells[_index(c)].cumulusDensity = val; }

	inline float get_cumulusSize(cell_index c) const { return m_cells[_index(c)].cumulusSize; }
	inline void set_cumulusSize(cell_index c, float val) { m_cells[_index(c)].cumulusSize = val; }

	inline int get_hasTowerCumulus(cell_index c) const { return m_cells[_index(c)].hasTowerCumulus; }
	inline void set_hasTowerCumulus(cell_index c, int val) { m_cells[_index(c)].hasTowerCumulus = val; }

	inline int get_hasShowerCumulus(cell_index c) const { return m_cells[_index(c)].hasShowerCumulus; }
	inline void set_hasShowerCumulus(cell_index c, int val) { m_cells[_index(c)].hasShowerCumulus = val; }

	inline float get_visibility(cell_index c) const { return m_cells[_index(c)].fogEndBelowLayerMapData; }
	inline void set_visibility(cell_index c, float val) { m_cells[_index(c)].fogEndBelowLayerMapData = val; }

	inline float get_fogLayerZ(cell_index c) const { return m_cells[_index(c)].fogLayerZ; }
	inline void set_fogLayerZ(cell_index c, float val) { m_cells[_index(c)].fogLayerZ = val; }

	inline int get_contrailLayer(fmap_wxtype wxtype) const { return m_mapContrailLayer[wxtype]; }
	inline void set_contrailLayer(fmap_wxtype wxtype, int val) { m_mapContrailLayer[wxtype] = val; }

#ifdef FMAP_DEBUG
	class debug_data {
	public:
		debug_data() {}
		virtual ~debug_data() = default;
		virtual std::string str() const = 0;
	};

	inline const debug_data* get_debugData(cell_index c) const { return m_cells[_index(c)].debugData.get(); }
	inline void set_debugData(cell_index c, std::unique_ptr<debug_data> data) { m_cells[_index(c)].debugData = std::move(data); }
#endif

protected:

	inline size_t _index(cell_index c) const { return m_sizeX * c.y + c.x; }

	static constexpr int c_version = 8;
	unsigned int m_sizeY;
	unsigned int m_sizeX;

	// "Global" fields

	int m_mapWindHeading = 0;
	float m_mapWindSpeed = 0;

	int m_mapStratusZFair = 30000;
	int m_mapStratusZInc = 27000;

	int m_mapContrailLayer[NUM_WEATHER_TYPES] = { 33000, 30000, 27000, 25000 };

	// Per-cell fields

	struct fmap_cell {
		int basicCondition = WX_SUNNY;
		float pressure = 1013.25f;
		float temperature = 15.0f;
		float windSpeed[NUM_ALOFT_BREAKPOINTS] = { 0.0f };
		float windDir[NUM_ALOFT_BREAKPOINTS] = { 0.0f };
		float cumulusBase = 0.0f;
		int cumulusDensity = 0;
		float cumulusSize = 0.0f;
		int hasTowerCumulus = 0;
		int hasShowerCumulus = 0;
		float fogEndBelowLayerMapData = 60.0f;
		float fogLayerZ = 10000.0f;

#ifdef FMAP_DEBUG
		std::unique_ptr<debug_data> debugData;
#endif
	};

	std::vector<fmap_cell> m_cells;
};
