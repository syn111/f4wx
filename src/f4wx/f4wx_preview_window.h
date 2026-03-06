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
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/

#pragma once

#include "config.h"

#include <Windows.h>
#include <objidl.h>
#include <wrl/client.h>
#include <gdiplus.h>
#include <Commctrl.h>
#include <filesystem>
#include <memory>
#include <string>

#include "fmap.h"
#include "f4wx_preview.h"

enum class preview_mode : unsigned {
	PM_CLOUDS = 0x1,
	PM_PRESSURE = 0x2,
	PM_TEMPERATURE = 0x4,
	PM_WIND = 0x8
};


class f4wx_preview_window {
public:

	f4wx_preview_window();

	~f4wx_preview_window();

	bool open(RECT& r, HWND hParent);
	void close();

	void redraw();

	void set_fmap(fmap *map);

	void delete_background();

	void load_background(IStream *pstream);

	void load_background(Gdiplus::Bitmap *bm);

	int load_background(HINSTANCE hinst, const wchar_t* name, const wchar_t* type);

	int load_background(const std::filesystem::path& path);

	int load_background(void *buf, size_t size);

	inline Gdiplus::Bitmap* get_background() { return m_background.get(); }

	inline int get_mode() const { return m_previewMode; }

	void set_mode(int m);

	void toggle_mode(preview_mode m);

	inline const f4wx_preview& get_preview() const { return m_preview; }

	inline Gdiplus::Status save(const std::filesystem::path& path, const CLSID* clsidEncoder, const Gdiplus::EncoderParameters *encoderParams) const { return m_preview.save(path, clsidEncoder, encoderParams); }

	inline bool is_valid() { return m_preview.is_valid(); }

	inline HWND get_hwnd() { return m_hwnd; }

	void set_windLevel(int level);
	inline int get_windLevel() const { return m_windLevel; }

#ifdef FMAP_DEBUG
	inline void set_latlons(float northLat, float southLat, float leftLon, float rightLon) {
		m_northLat = northLat;
		m_southLat = southLat;
		m_leftLon = leftLon;
		m_rightLon = rightLon;
	}
#endif

private:

	HWND m_hwnd = nullptr;

	fmap *m_map = nullptr;

	f4wx_preview m_preview;

	std::unique_ptr<Gdiplus::Bitmap> m_background;
	/** Stream backing m_background when loaded via FromStream; kept alive for Bitmap lifetime. */
	Microsoft::WRL::ComPtr<IStream> m_background_stream;

	int m_previewMode = 0;

	int m_windLevel = 0;

	/** RAII HGLOBAL buffer for stream-backed background; allocate then use data()/get(), release when done. */
	struct preview_background_buffer {
		HGLOBAL h = nullptr;
		void* p = nullptr;

		~preview_background_buffer() { release(); }

		/** Allocates and locks; releases any previous. Returns true on success. */
		bool allocate(size_t size) {
			release();
			h = GlobalAlloc(GMEM_MOVEABLE, size);
			if (!h) return false;
			p = GlobalLock(h);
			if (!p) { GlobalFree(h); h = nullptr; return false; }
			return true;
		}

		void release() {
			if (p) { GlobalUnlock(h); p = nullptr; }
			if (h) { GlobalFree(h); h = nullptr; }
		}

		void* data() const { return p; }
		HGLOBAL get() const { return h; }
	};
	preview_background_buffer m_preview_background_buffer;

	// WxInfo tooltip (member buffer so lpszText stays valid for TTM_SETTOOLINFOW)
	std::wstring m_tooltipText;
	HWND m_hTooltip = nullptr;
	TOOLINFOW m_toolItem;
	bool m_trackingMouse = FALSE;
	POINT m_lastMousePt = { 0 };

	static LRESULT CALLBACK wndproc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	INT_PTR on_paint();

	LRESULT on_mousemove(POINT& pt);
	LRESULT on_mouseleave();

	void _load_background(IStream *pstream);

	void _load_background(Gdiplus::Bitmap *bm);

	void update_tooltip(bool forced = false);
	void hide_tooltip();

	static constexpr int PREVIEW_CELL_INVALID = -1;

	int m_lastCellX = PREVIEW_CELL_INVALID;
	int m_lastCellY = PREVIEW_CELL_INVALID;

#ifdef FMAP_DEBUG
	float m_northLat = 0;
	float m_southLat = 0;
	float m_leftLon = 0;
	float m_rightLon = 0;
#endif
};