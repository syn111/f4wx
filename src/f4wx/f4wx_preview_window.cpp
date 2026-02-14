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
#include <windowsx.h>
#include <Commctrl.h>
#include <cassert>
#include <memory>
#include <filesystem>
#include <format>

#include "f4wx_preview_window.h"
#include "utils.h"


static HWND CreateTrackingToolTip(HWND hDlg, WCHAR* pText, TOOLINFOW& toolItem, int maxWidth = 0)
{
	HINSTANCE g_hInst = GetModuleHandle(nullptr);

	HWND hwndTT = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		hDlg, nullptr, g_hInst, nullptr);

	if (!hwndTT)
		return nullptr;

	toolItem.cbSize = sizeof(TOOLINFOW);
	toolItem.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
	toolItem.hwnd = hDlg;
	toolItem.hinst = g_hInst;
	toolItem.lpszText = pText;
	toolItem.uId = reinterpret_cast<UINT_PTR>(hDlg);

	GetClientRect(hDlg, &toolItem.rect);

	SendMessageW(hwndTT, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolItem));

	// Make it multiline
	if(maxWidth)
		SendMessageW(hwndTT, TTM_SETMAXTIPWIDTH, 0, maxWidth);

	return hwndTT;
}


f4wx_preview_window::f4wx_preview_window()
{
	WNDCLASSW wc = { 0 };
	wc.lpfnWndProc = wndproc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BACKGROUND));
	wc.lpszClassName = L"f4wx_preview_window";
	wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
	RegisterClassW(&wc);
}

f4wx_preview_window::~f4wx_preview_window()
{
	if (m_hTooltip) {
		DestroyWindow(m_hTooltip);
		m_hTooltip = nullptr;
	}
	if(m_hwnd) {
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
	}

	delete_background();
}

bool f4wx_preview_window::open(RECT& r,  HWND hParent)
{
	assert(m_hwnd == nullptr);

	DWORD dwStyle = (hParent ? WS_CHILD : WS_OVERLAPPEDWINDOW);
	m_hwnd = CreateWindowW(L"f4wx_preview_window", L"", dwStyle | WS_VISIBLE, r.left, r.top, r.right - r.left, r.bottom - r.top, hParent, 0, nullptr, this);

	if (!m_hwnd)
		return false;

	m_hTooltip = CreateTrackingToolTip(m_hwnd, nullptr, m_toolItem, 160);
	return true;
}

void f4wx_preview_window::close()
{
	DestroyWindow(m_hwnd);
	m_hwnd = nullptr;
}

LRESULT CALLBACK f4wx_preview_window::wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	f4wx_preview_window *me = reinterpret_cast<f4wx_preview_window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	switch (msg) {
		case WM_CREATE: {
			me = reinterpret_cast<f4wx_preview_window*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(me));
			return 0;
		}
		case WM_PAINT: {
			return me->on_paint();
		}
		case WM_MOUSEMOVE: {
			POINT p;
			p.x = GET_X_LPARAM(lParam);
			p.y = GET_Y_LPARAM(lParam);
			return me->on_mousemove(p);
		}
		case WM_MOUSELEAVE:
		case WM_RBUTTONDOWN: { 
			return me->on_mouseleave(); // The mouse pointer has left our window. Deactivate the tooltip.
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT f4wx_preview_window::on_mouseleave()
{
	hide_tooltip();
	return FALSE;
}

LRESULT f4wx_preview_window::on_mousemove(POINT& pt)
{
	bool newTrack = false;

	if (!m_trackingMouse && m_map != nullptr)   // The mouse has just entered the window.
	{                       // Request notification when the mouse leaves.

		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
		tme.hwndTrack = m_hwnd;
		tme.dwFlags = TME_LEAVE;

		TrackMouseEvent(&tme);

		m_trackingMouse = TRUE;

		newTrack = true;
	}

	if (m_lastMousePt.x != pt.x || m_lastMousePt.y != pt.y) {
		m_lastMousePt = pt;
		update_tooltip(
#ifdef FMAP_DEBUG
		true	// Update always to show correct Lat/lon but do not update in release build!
#endif
		);
	}

	if (newTrack) {
		// Activate the tooltip.
		SendMessageW(m_hTooltip, TTM_TRACKACTIVATE, static_cast<WPARAM>(TRUE), reinterpret_cast<LPARAM>(&m_toolItem));
	}
	return 0;
}

void f4wx_preview_window::update_tooltip(bool forced)
{
	if (m_trackingMouse) {	
		if (m_map != nullptr) {
			long cell_y, cell_x;

			RECT rect;
			GetClientRect(m_hwnd, &rect);

			cell_y = (m_lastMousePt.y * m_map->get_sizeY()) / (rect.bottom - rect.top);
			cell_x = (m_lastMousePt.x * m_map->get_sizeX()) / (rect.right - rect.left);

			if (m_lastCellX != cell_x || m_lastCellY != cell_y || forced) {
				cell_index cell = { static_cast<unsigned int>(cell_y), static_cast<unsigned int>(cell_x) };

				fmap_wxtype wxtype = m_map->get_type(cell);

				m_tooltipText = std::format(L"Wx:\t{}\r\nSLP:\t{:.1f} hPa\r\nT:\t{:.1f} \u00B0C\r\nW:\t{:03.0f}@{:.0f} kts",
					to_wide(fmap_wxtype_text[wxtype]),
					m_map->get_pressure(cell),
					m_map->get_temperature(cell),
					m_map->get_windDirection(cell),
					m_map->get_windSpeed(cell)
				);

					float cubase = m_map->get_cumulusBase(cell);
					int cudensity = m_map->get_cumulusDensity(cell);
					float cusize = m_map->get_cumulusSize(cell);
					float cudensitymod = cudensity * (1 + (5 - cusize) / 5);

					m_tooltipText += std::format(
					L"\r\n@3k:\t{:03.0f}@{:.0f} kts\r\n@6k:\t{:03.0f}@{:.0f} kts\r\n@9k:\t{:03.0f}@{:.0f} kts\r\n@12k:\t{:03.0f}@{:.0f} kts\r\n@18k:\t{:03.0f}@{:.0f} kts\r\n@24k:\t{:03.0f}@{:.0f} kts\r\n@30k:\t{:03.0f}@{:.0f} kts\r\n@40k:\t{:03.0f}@{:.0f} kts\r\n@50k:\t{:03.0f}@{:.0f} kts"
					L"\r\nCloud:\t{}\r\nContr:\tFL{}\r\nVis:\t{:.1f} km",
					m_map->get_windDirection(cell, 1), m_map->get_windSpeed(cell, 1),
					m_map->get_windDirection(cell, 2), m_map->get_windSpeed(cell, 2),
					m_map->get_windDirection(cell, 3), m_map->get_windSpeed(cell, 3),
					m_map->get_windDirection(cell, 4), m_map->get_windSpeed(cell, 4),
					m_map->get_windDirection(cell, 5), m_map->get_windSpeed(cell, 5),
					m_map->get_windDirection(cell, 6), m_map->get_windSpeed(cell, 6),
					m_map->get_windDirection(cell, 7), m_map->get_windSpeed(cell, 7),
					m_map->get_windDirection(cell, 8), m_map->get_windSpeed(cell, 8),
					m_map->get_windDirection(cell, 9), m_map->get_windSpeed(cell, 9),
					(wxtype < WX_FAIR ? std::wstring(L"CLR") : std::format(L"{}{:03.0f}",
						cudensitymod < 6 ? L"FEW" : cudensitymod < 14 ? L"SCT" : cudensitymod < 24 ? L"BKN" : L"OVC",
						cubase / 100))
						+ (m_map->get_hasShowerCumulus(cell) ? L"CB" : m_map->get_hasTowerCumulus(cell) ? L"TCU" : L""),
					m_map->get_contrailLayer(wxtype) / 100,
					m_map->get_visibility(cell)
				);
#ifdef FMAP_DEBUG
				m_tooltipText += std::format(
					L"\r\nDBG\r\n----\r\n[{}, {}]\r\n[La={:.2f} Lo={:.2f}]"
					L"\r\n{}"
					L"\r\nCuD:\t{}\r\nCuS:\t{:.1f}\r\nCuZ:\t{:.0f} ft\r\nFogZ:\t{:.0f} ft\r\nStZ:\t{} ft\r\nMapW:\t{:03d}@{:.0f} kts",
					cell.y, cell.x,
					m_northLat - (m_lastMousePt.y * (m_northLat - m_southLat)) / (rect.bottom - rect.top),
					m_leftLon + (m_lastMousePt.x * (m_rightLon - m_leftLon)) / (rect.right - rect.left),
					m_map->get_debugData(cell) ? to_wide(m_map->get_debugData(cell)->str()) : L"",
					cudensity,
					cusize,
					cubase,
					m_map->get_fogLayerZ(cell),
					wxtype <= WX_FAIR ? m_map->get_mapStratusZFair() : m_map->get_mapStratusZInc(),
					m_map->get_mapWindHeading(),
					m_map->get_mapWindSpeed()
				);
#endif
				m_toolItem.lpszText = m_tooltipText.data();
				SendMessageW(m_hTooltip, TTM_SETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&m_toolItem));
			}
			
			m_lastCellX = cell_x;
			m_lastCellY = cell_y;

			// Position the tooltip. The coordinates are adjusted so that the tooltip does not overlap the mouse pointer.

			RECT tr;
			GetWindowRect(m_hTooltip, &tr);

			RECT dr;
			GetWindowRect(GetDesktopWindow(), &dr);

			POINT pt = m_lastMousePt;
			ClientToScreen(m_hwnd, &pt);

			pt.x = (pt.x + tr.right - tr.left + 20 > dr.right) ? pt.x - 20 - (tr.right - tr.left): pt.x + 20;
			pt.y = (pt.y + tr.bottom - tr.top > dr.bottom) ? pt.y - (pt.y + tr.bottom - tr.top - dr.bottom) : pt.y;

			SendMessageW(m_hTooltip, TTM_TRACKPOSITION, 0, static_cast<LPARAM>(MAKELONG(pt.x, pt.y)));
		}
		else {
			hide_tooltip();
		}
	}
}

void f4wx_preview_window::hide_tooltip()
{
	SendMessageW(m_hTooltip, TTM_TRACKACTIVATE, static_cast<WPARAM>(FALSE), reinterpret_cast<LPARAM>(&m_toolItem));
	m_trackingMouse = FALSE;
}

void f4wx_preview_window::set_fmap(fmap *map)
{
	m_map = map;
	redraw();
}

INT_PTR f4wx_preview_window::on_paint()
{
	// Draw preview bitmap

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(m_hwnd, &ps);
	if (!m_preview.is_valid()) {
		EndPaint(m_hwnd, &ps);
		return 0;
	}
	HBITMAP hBitmap = nullptr;
	if (m_preview.get_hbitmap(Gdiplus::Color::Gray, &hBitmap) != Gdiplus::Ok || !hBitmap) {
		EndPaint(m_hwnd, &ps);
		return 0;
	}
	HDC pSource = CreateCompatibleDC(hdc);
	if (!pSource) {
		DeleteObject(hBitmap);
		EndPaint(m_hwnd, &ps);
		return 0;
	}
	HGDIOBJ pOrig = SelectObject(pSource, hBitmap);
	RECT rect;
	GetClientRect(m_hwnd, &rect);
	SetStretchBltMode(hdc, HALFTONE);
	StretchBlt(hdc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
		pSource, 0, 0, m_preview.get_width(), m_preview.get_height(),
		SRCCOPY);
	SelectObject(pSource, pOrig);
	DeleteDC(pSource);
	DeleteObject(hBitmap);
	EndPaint(m_hwnd, &ps);
	return 0;
}

void f4wx_preview_window::redraw()
{
	m_preview.cleanup();
	if (m_map != nullptr) {
		if (static_cast<unsigned>(get_mode()) & static_cast<unsigned>(preview_mode::PM_TEMPERATURE))
			m_preview.draw_temperature(*m_map);

		if (static_cast<unsigned>(get_mode()) & static_cast<unsigned>(preview_mode::PM_CLOUDS))
			m_preview.draw_clouds(*m_map);

		if (static_cast<unsigned>(get_mode()) & static_cast<unsigned>(preview_mode::PM_PRESSURE))
			m_preview.draw_pressure(*m_map);

		if (static_cast<unsigned>(get_mode()) & static_cast<unsigned>(preview_mode::PM_WIND))
			m_preview.draw_wind(*m_map, m_windLevel);
	}
	update_tooltip(true);

	RECT r;
	GetClientRect(m_hwnd, &r);
	InvalidateRect(m_hwnd, &r, FALSE);
}


void f4wx_preview_window::set_mode(int m)
{
	m_previewMode = m;
	redraw();
}

void f4wx_preview_window::toggle_mode(preview_mode m)
{
	const unsigned um = static_cast<unsigned>(m);
	if (static_cast<unsigned>(m_previewMode) & um)
		m_previewMode = static_cast<int>(static_cast<unsigned>(m_previewMode) & ~um);
	else
		m_previewMode = static_cast<int>(static_cast<unsigned>(m_previewMode) | um);

	redraw();
}

/** Destruction order must remain: Bitmap may hold references to the stream; stream uses the
 * HGLOBAL buffer. Reordering can cause use-after-free or failed GDI+ teardown. */
void f4wx_preview_window::delete_background()
{
	m_preview.set_background(nullptr);
	m_background.reset();
	m_background_stream.Reset();
	m_preview_background_buffer.release();
}

void f4wx_preview_window::load_background(IStream *pstream)
{
	if (pstream == nullptr)
		return;
	if (pstream == m_background_stream.Get()) {
		delete_background();
		return;
	}
	delete_background();
	m_background_stream = pstream;
	_load_background(pstream);
}

void f4wx_preview_window::load_background(Gdiplus::Bitmap *bm)
{
	delete_background();
	_load_background(bm);
}

/** Caller must set m_background_stream (and keep it alive) before calling; Bitmap::FromStream requires stream lifetime. */
void f4wx_preview_window::_load_background(IStream *pstream)
{
	Gdiplus::Bitmap* bm = Gdiplus::Bitmap::FromStream(pstream);
	if (!bm || bm->GetLastStatus() != Gdiplus::Ok) {
		delete bm;
		m_background.reset();
	} else {
		m_background.reset(bm);
	}
	m_preview.set_background(m_background.get());
	redraw();
}

void f4wx_preview_window::_load_background(Gdiplus::Bitmap *bm)
{
	m_background.reset(bm);
	m_preview.set_background(m_background.get());
	redraw();
}


int f4wx_preview_window::load_background(HINSTANCE hinst, const wchar_t* name, const wchar_t* type)
{
	int rv = 1;

	delete_background();

	HRSRC hres = FindResourceW(hinst, name, type);
	if (!hres)
		return GetLastError();

	DWORD imageSize = SizeofResource(hinst, hres);
	if (!imageSize)
		return GetLastError();

	const void* pdata = LockResource(LoadResource(hinst, hres));
	if (!pdata)
		return GetLastError();

	if (m_preview_background_buffer.allocate(imageSize)) {
		CopyMemory(m_preview_background_buffer.data(), pdata, imageSize);
		IStream* pstream = nullptr;
		if (CreateStreamOnHGlobal(m_preview_background_buffer.get(), FALSE, &pstream) == S_OK) {
			m_background_stream.Attach(pstream);
			_load_background(pstream);
			if (m_background != nullptr && m_background->GetLastStatus() == Gdiplus::Ok)
				rv = 0;
			else {
				m_background_stream.Reset();
				m_preview_background_buffer.release();
			}
		}
		else
			m_preview_background_buffer.release();
	}
	return rv;
}

int f4wx_preview_window::load_background(const std::filesystem::path& path)
{
	int rv = 0;

	delete_background();

	Gdiplus::Bitmap b(path.c_str());
	rv = b.GetLastStatus();
	if (rv != 0)
		return rv;


	UINT oheight = b.GetHeight();
	UINT owidth = b.GetWidth();
	INT nwidth = PREVIEW_WIDTH;
	INT nheight = PREVIEW_HEIGHT;
	double ratio = static_cast<double>(owidth) / static_cast<double>(oheight);
	if (owidth > oheight)
		nheight = static_cast<INT>(static_cast<double>(nwidth) / ratio);
	else
		nwidth = static_cast<INT>(nheight * ratio);

	auto newbitmap = std::make_unique<Gdiplus::Bitmap>(PREVIEW_WIDTH, PREVIEW_HEIGHT, b.GetPixelFormat());
	Gdiplus::Graphics graphics(newbitmap.get());
	rv = graphics.DrawImage(&b, 0, 0, nwidth, nheight);

	if (rv != Gdiplus::Ok)
		return rv;

	_load_background(newbitmap.release());  // _load_background takes ownership

	return (m_background != nullptr) ? 0 : 1;
}

int f4wx_preview_window::load_background(void *buf, size_t size)
{
	int rv = 1;

	delete_background();

	if (m_preview_background_buffer.allocate(size)) {
		CopyMemory(m_preview_background_buffer.data(), buf, size);
		IStream* pstream = nullptr;
		if (CreateStreamOnHGlobal(m_preview_background_buffer.get(), FALSE, &pstream) == S_OK) {
			m_background_stream.Attach(pstream);
			_load_background(pstream);
			if (m_background != nullptr && m_background->GetLastStatus() == Gdiplus::Ok)
				rv = 0;
			else {
				m_background_stream.Reset();
				m_preview_background_buffer.release();
			}
		}
		else
			m_preview_background_buffer.release();
	}
	return rv;
}

void f4wx_preview_window::set_windLevel(int level)
{
	assert(level < NUM_ALOFT_BREAKPOINTS);
	if (level != m_windLevel) {
		m_windLevel = level;
		if (static_cast<unsigned>(m_previewMode) & static_cast<unsigned>(preview_mode::PM_WIND))
			redraw();
	}
}