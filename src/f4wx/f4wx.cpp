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
#include <gdiplus.h>
#include <Windowsx.h>
#include <algorithm>
#include <memory>
#include <iterator>
#include <vector>
#include <string>
#include <future>
#include <mutex>
#include <deque>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <CommCtrl.h>
#include <Shlobj.h>
#include <ctime>
#include <format>
#include <ranges>
#include <span>
#include <cmath>

#include "f4wx.h"
#include "noaa_downloader.h"
#include "winhttp_get.h"
#include "utils.h"
#include "resource.h"
#include "f4wx_update_notifier.h"

#include <Shobjidl.h>

struct f4wx::taskbar_list_holder {
	ITaskbarList3* p = nullptr;
	~taskbar_list_holder() { if (p) p->Release(); }
};

#if defined(UNICODE) || defined(_UNICODE)
/** Wide string to UTF-8 (replaces deprecated std::wstring_convert/codecvt_utf8). */
static std::string narrow_from_wide(const wchar_t* wstr)
{
	if (!wstr || !*wstr) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) return {};
	std::string out(static_cast<size_t>(len), '\0');
	int n = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), len, nullptr, nullptr);
	if (n <= 0) return {};
	out.resize(static_cast<size_t>(n - 1));
	return out;
}
#endif

using std::vector;
using std::string;

using namespace Gdiplus;

/** Number of fmap grid cells for a theater size in segments (64 or 128). 1 segment = 16 nm for formula. */
inline unsigned int fmap_cells_from_size(double s)
{
	const double size_nm = s * 16.0;  /* segments to legacy nm unit for cell formula */
	return static_cast<unsigned int>(std::round(std::floor((size_nm * 1000) * units::FT_PER_M / static_cast<double>(CELLSIZE) + 1)));
}

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
	using namespace Gdiplus;
	UINT num = 0;
	UINT size = 0;
	GetImageEncodersSize(&num, &size);
	if (size == 0)
		return -1;
	std::vector<BYTE> buf(size);
	ImageCodecInfo* pImageCodecInfo = reinterpret_cast<ImageCodecInfo*>(buf.data());
	GetImageEncoders(num, size, pImageCodecInfo);
	for (UINT j = 0; j < num; ++j)
	{
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
		{
			*pClsid = pImageCodecInfo[j].Clsid;
			return static_cast<int>(j);
		}
	}
	return -1;
}

f4wx::f4wx()
	:
	m_converter(59, 59)
{
	memset(&m_converter_options, 0, sizeof(m_converter_options));

	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&m_gdiplus_token, &gdiplusStartupInput, nullptr);

	m_current_map.reset();

	init_dialog(IDD_F4WX_MAIN);

	m_spTaskbarList = std::make_unique<taskbar_list_holder>();
	if (SUCCEEDED(::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
		__uuidof(ITaskbarList3), reinterpret_cast<void**>(&m_spTaskbarList->p))))
	{
		m_spTaskbarList->p->HrInit();
	}

	on_theater_change(0);

	initialize_controls();

	RECT pr;
	get_preview_rect(pr);
	m_previewWindow.open(pr, m_hwnd);

	// Sublcass it for right click menu
	m_previewHookOrigProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(m_previewWindow.get_hwnd(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previewHook)));
	SetProp(m_previewWindow.get_hwnd(), L"f4wx", this);

	m_worker = std::jthread([this](std::stop_token stoken) {
		while (true) {
			std::function<void()> task;
			{
				std::unique_lock lock(m_queue_mutex);
				m_queue_cv.wait(lock, stoken, [this, &stoken] { return !m_tasks.empty() || stoken.stop_requested(); });
				if (stoken.stop_requested())
					return;
				if (m_tasks.empty())
					continue;
				task = std::move(m_tasks.front());
				m_tasks.pop();
			}
			m_worker_stop_token = stoken;
			task();
			m_worker_stop_token = std::stop_token();
		}
	});

#ifdef F4WX_ENABLE_UPDATE_CHECK
#ifndef _DEBUG
	check_for_updates(true);
#endif
#endif
}

f4wx::~f4wx()
{
	// Avoid deadlock: worker may be blocked in SendMessage(WMU_OPENPB_THREAD/...) waiting for
	// this thread. Request stop and pump messages until the worker exits so its SendMessage is processed.
	m_worker.request_stop();
	if (void* h = m_worker.native_handle()) {
		while (WaitForSingleObject(static_cast<HANDLE>(h), 0) == WAIT_TIMEOUT) {
			MSG msg;
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
				if (msg.message != WM_QUIT)
					DispatchMessage(&msg);
			}
			Sleep(5);
		}
	}
	m_worker = std::jthread();
#ifdef F4WX_ENABLE_UPDATE_CHECK
	m_update_thread.request_stop();
	if (void* uh = m_update_thread.native_handle()) {
		while (WaitForSingleObject(static_cast<HANDLE>(uh), 0) == WAIT_TIMEOUT) {
			MSG msg;
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
				if (msg.message != WM_QUIT)
					DispatchMessage(&msg);
			}
			Sleep(5);
		}
	}
	m_update_thread = std::jthread();
#endif
	clear_gribfiles();

	GdiplusShutdown(m_gdiplus_token);

	m_current_map.reset();
}

int f4wx::update_gfs_runs()
{
	noaa_downloader dl;
	return dl.get_gfs_runs(m_gfsruns);
}

void f4wx::clear_gribfiles()
{
	m_last_gfsrun = noaa_gfsrun_filename("");
	m_gribfiles.clear();
}

void f4wx::clear_fmaps()
{
	m_fmapcount = 0;
	m_current_map.reset();
	ui_slider_set_range(0);
	set_current_fmap(0, true);
}

bool f4wx::queue_task(std::function<void()> task)
{
	try {
		std::lock_guard lock(m_queue_mutex);
		if (!m_tasks.empty())
			return false;
		m_tasks.push(std::move(task));
	}
	catch (...) {
		return false;
	}
	m_queue_cv.notify_one();
	return true;
}

int f4wx::convert_grib_files(bool updateCount)
{
	int rv = 0;

	setstate(state::S_CONVERT);
	do_play(false);

	register_handler(WMU_CONVERSION_END, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_conversion_finished(hwnd, msg, wp, lp); });

	if (updateCount) {
		clear_fmaps();
		m_fmapcount = m_converter.get_max_possible_forecast() == 0 ? 0 : m_converter.get_max_possible_forecast() * 60 / m_converter_options.interval_minutes + 1;
		on_conversion_finished(0, 0, 0, 0);
	}
	else {
		on_conversion_finished(0, 0, 0, 0);
	}

	return rv;
}

INT_PTR f4wx::on_conversion_finished(HWND, UINT, WPARAM, LPARAM)
{
	unregister_handler(WMU_CONVERSION_END);

	ui_slider_set_range(get_fmap_count());

	set_current_fmap(std::min(get_fmap_count() - 1, std::max(get_sync_min_pos(), m_current_map_idx)), true);

	if (GetForegroundWindow() != m_hwnd)
		FlashWindow(m_hwnd, TRUE);

	setstate(state::S_IDLE);
	return 0;
}

void f4wx::ui_slider_set_range(size_t range)
{
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_SLIDER, TBM_SETRANGEMAX, TRUE, static_cast<LPARAM>(range - 1));
}

////////////////////////////////////////////////////////////////////////// UI stuff //////////////////////////////////////////////////////////////////////////

/** Dialog small icon; loaded in WM_INITDIALOG, destroyed in WM_DESTROY to avoid leak. */
static HICON g_dialog_small_icon = nullptr;

/** Tooltip windows created by create_tooltip; destroyed explicitly in WM_DESTROY. */
static vector<HWND> g_tooltip_windows;

INT_PTR CALLBACK f4wx::dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
		case WM_INITDIALOG: {
			// Set Icon (LoadImage result must be destroyed in WM_DESTROY)
			if (g_dialog_small_icon != nullptr)
				DestroyIcon(g_dialog_small_icon);
			g_dialog_small_icon = reinterpret_cast<HICON>(LoadImage(GetModuleHandle(nullptr),
				MAKEINTRESOURCE(IDI_ICON1),
				IMAGE_ICON,
				GetSystemMetrics(SM_CXSMICON),
				GetSystemMetrics(SM_CYSMICON),
				0));
			if (g_dialog_small_icon != nullptr)
				SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_dialog_small_icon));

			std::string title = std::format("F4Wx v{}{}{}", 
				F4WX_VERSION_FULL,
#if defined(_DEBUG)
				" -- DEBUG Build ",
#elif defined(_TEST)
				" -- TEST Build ",
#else
				"",
#endif
#if defined(_DEBUG) || defined(_TEST)
				std::format("[{}{}]",
#ifdef F4WX_ENABLE_UPDATE_CHECK	
					" UP",
#else
					"",
#endif
#ifdef FMAP_DEBUG	
					" FMAP_DEBUG"
#else
					""
#endif
				)
#else
				""
#endif
			);
			SetWindowTextW(hwnd, to_wide(title).c_str());
			return TRUE;
		}

		case WM_DESTROY: {
			if (g_dialog_small_icon != nullptr) {
				DestroyIcon(g_dialog_small_icon);
				g_dialog_small_icon = nullptr;
			}
			for (HWND h : g_tooltip_windows) {
				if (IsWindow(h))
					DestroyWindow(h);
			}
			g_tooltip_windows.clear();
			break;
		}
		case WM_CLOSE: {
			PostQuitMessage(0);
			return 0;
		}
		case WM_PAINT: {
			return on_paint(hwnd);
		}
		case WM_COMMAND: {
			return on_command(wparam, lparam);
		}
		case WM_HOTKEY: {
			return on_hotkey(wparam, lparam);
		}
		case WM_HSCROLL: {
			return on_slider_scroll(wparam, lparam);
		}
		case WM_TIMER: {
			on_play_timer();
			break;
		}
		case WMU_OPENPB_THREAD: {
			assert(!m_bar);
			m_bar = std::make_unique<f4wx_progressbar>(f4wx_hcurrentdialog, m_spTaskbarList->p, wparam);
			break;
		}
		case WMU_CLOSEPB_THREAD: {
			assert(m_bar);
			m_bar.reset();
			break;
		}
#ifdef F4WX_ENABLE_UPDATE_CHECK
		case WMU_UPDATE_AVAILABLE: {
			std::unique_ptr<char, void(*)(void*)> version_str(reinterpret_cast<char*>(lparam), &free);
			if (version_str) {
				setstate_wait(state::S_UPDATE);
				std::string msg = std::format("A new version of F4Wx ({}) is available.\n\nOpen the download page?", version_str.get());
				int opt = messagebox("F4Wx Update", msg.c_str(), MB_ICONINFORMATION | MB_YESNO);
				if (opt == IDYES)
					ShellExecuteA(nullptr, nullptr, F4WX_RELEASES_URL, nullptr, nullptr, SW_SHOW);
				setstate(state::S_IDLE);
			}
			return TRUE;
		}
#endif
	}
	return FALSE;
}

static void create_tooltip(HWND hwnd, HWND hctrl, const wchar_t* msg)
{
	HWND htt = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
		WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		hwnd, nullptr,
		GetModuleHandle(nullptr), 0);

	TOOLINFOW ti { sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, hwnd, reinterpret_cast<UINT_PTR>(hctrl), { 0, 0, 0, 0 }, nullptr, const_cast<wchar_t*>(msg) };
	SendMessageW(htt, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
	g_tooltip_windows.push_back(htt);
}

static inline void set_text_control_int(HWND hwnd, int dlgitem, int val, const wchar_t* fmt = L"{}")
{
	std::wstring s = std::vformat(fmt, std::make_wformat_args(val));
	SendDlgItemMessageW(hwnd, dlgitem, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(s.c_str()));
}

static inline void set_text_control_float(HWND hwnd, int dlgitem, float val, const wchar_t* fmt = L"{:.2f}")
{
	std::wstring s = std::vformat(fmt, std::make_wformat_args(val));
	SendDlgItemMessageW(hwnd, dlgitem, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(s.c_str()));
}


void f4wx::ui_set_cloud_fair(float val)
{
	val = at_or_above(val, 0.f);
	set_text_control_float(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_FAIR, val);
	if (m_converter_options.cloud_fair != val) {
		m_converter_options.cloud_fair = val;
		if (get_fmap_count() > 0)
			(void)convert_grib_files();
	}
}

void f4wx::ui_set_cloud_poor(float val)
{
	val = at_or_above(val, 0.f);
	set_text_control_float(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_POOR, val);
	if (m_converter_options.cloud_poor != val) {
		m_converter_options.cloud_poor = val;
		if (get_fmap_count() > 0)
			(void)convert_grib_files();
	}
}

void f4wx::ui_set_cloud_inclement(float val)
{
	val = at_or_above(val, 0.f);
	set_text_control_float(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_INCLEMENT, val);
	if (m_converter_options.cloud_inclement != val) {
		m_converter_options.cloud_inclement = val;
		if (get_fmap_count() > 0)
			(void)convert_grib_files();
	}
}

void f4wx::ui_set_preipit_fair(float val)
{
	val = at_or_above(val, 0.f);
	set_text_control_float(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_FAIR, val);
	if (m_converter_options.precipitation_fair != val) {
		m_converter_options.precipitation_fair = val;
		if (get_fmap_count() > 0)
			(void)convert_grib_files();
	}
}

void f4wx::ui_set_preipit_poor(float val)
{
	val = at_or_above(val, 0.f);
	set_text_control_float(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_POOR, val);
	if (m_converter_options.precipitation_poor != val) {
		m_converter_options.precipitation_poor = val;
		if (get_fmap_count() > 0)
			(void)convert_grib_files();
	}
}

void f4wx::ui_set_preipit_inclement(float val)
{
	val = at_or_above(val, 0.f);
	set_text_control_float(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_INCLEMENT, val);
	if (m_converter_options.precipitation_inclement != val) {
		m_converter_options.precipitation_inclement = val;
		if (get_fmap_count() > 0)
			(void)convert_grib_files();
	}
}

void f4wx::ui_set_bms_day(int val)
{
	// Build 39: fixed day input being ignored and day 0 now invalid value
	m_ui_initial_time_day = at_or_above(val, 1);
	set_text_control_int(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_DAY, m_ui_initial_time_day);
	set_current_fmap(m_current_map_idx, true);
}

void f4wx::ui_set_bms_hour(int val)
{
	m_ui_initial_time_hour = between(val, 0, 23);
	set_text_control_int(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_HOUR, m_ui_initial_time_hour, L"{:02}");
	set_current_fmap(m_current_map_idx, true);
}

void f4wx::ui_set_bms_minute(int val)
{
	m_ui_initial_time_minute = between(val, 0, 59);
	set_text_control_int(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_MINUTE, m_ui_initial_time_minute, L"{:02}");
	set_current_fmap(m_current_map_idx, true);
}

void f4wx::ui_set_sync_timezone(bool val)
{
	m_ui_sync_with_real = val;
	CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_SYNC_TIMEZONE, val);
	set_current_fmap(m_current_map_idx, true);
	if (val == false)
		show_warning(IDC_F4WX_MAIN_WARN_SYNC, false);
}

void f4wx::ui_set_start_current(bool val)
{
	m_ui_start_from_current = val;
	CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_START_CURRENT, val);

	// Build 39 -- force re-evaluating the slider positions
	set_current_fmap(m_current_map_idx, true);
}

void f4wx::ui_set_save_previews(bool val)
{
	m_ui_save_previews = val;
	CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_SAVE_PREVIEWS, val);
}

void f4wx::ui_set_fmap_interval(int val)
{
	val = at_or_above(val, 0);
	if (val != m_converter_options.interval_minutes) {
		m_converter_options.interval_minutes = val;
		set_text_control_int(m_hwnd, IDC_F4WX_MAIN_TIME_INTERVAL, val);
		if (get_fmap_count() > 0)
			(void)convert_grib_files(true);
	}

	show_warning(IDC_F4WX_MAIN_WARN_INTERVAL, m_converter_options.interval_minutes < 60);
}

void f4wx::ui_set_fmap_maxforecast(int val)
{
	unsigned long old = m_converter_options.max_forecast_hours;
	if (val <= 0) {
		SendDlgItemMessageW(m_hwnd, IDC_F4WX_MAIN_FORECAST_TIME, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"Max"));
		m_converter_options.max_forecast_hours = FORECAST_HOURS_NO_LIMIT;
	}
	else {
		set_text_control_int(m_hwnd, IDC_F4WX_MAIN_FORECAST_TIME, val);
		m_converter_options.max_forecast_hours = val;
	}

	if (old != m_converter_options.max_forecast_hours) {

		unsigned long maxpossible = m_converter.get_max_possible_forecast();
		show_warning(IDC_F4WX_MAIN_WARN_FORECAST, m_converter_options.max_forecast_hours != FORECAST_HOURS_NO_LIMIT && m_converter_options.max_forecast_hours > maxpossible);
	}
}

void f4wx::initialize_controls()
{
	m_ui_source = ui_weather_source::none;
	CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_DOWNLOAD, 0);
	CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_LOAD_FILE, 0);

	m_previewWindow.set_mode(static_cast<int>(static_cast<unsigned>(preview_mode::PM_CLOUDS)));
	CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_PREVIEW_CLOUDS, 1);

	ui_set_cloud_fair(DEFAULT_CLOUD_COVER_FAIR);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_FAIR, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_FAIR), L"Minimum total cloud cover percentage to create fair weather. (default: 12.5%)");

	ui_set_cloud_poor(DEFAULT_CLOUD_COVER_POOR);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_POOR, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_POOR), L"Minimum total cloud cover percentage to create poor weather. (default: 62.5%)");

	ui_set_cloud_inclement(DEFAULT_CLOUD_COVER_INCLEMENT);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_INCLEMENT, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CLOUD_COVER_INCLEMENT), L"Minimum total cloud cover percentage to create inclement weather. (default: 62.5%)");

	ui_set_preipit_fair(DEFAULT_PRECIPIT_FAIR);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_FAIR, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_FAIR), L"Minimum precipitation (mm/hr) to create fair weather. (default: 0)");

	ui_set_preipit_poor(DEFAULT_PRECIPIT_POOR);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_POOR, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_POOR), L"Minimum precipitation (mm/hr) to create poor weather. (default: 0)");

	ui_set_preipit_inclement(DEFAULT_PRECIPIT_INCLEMENT);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_INCLEMENT, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_PRECIPIT_INCLEMENT), L"Minimum precipitation (mm/hr) to create inclement weather. (default: 2.0)");

	ui_set_fmap_interval(60);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_TIME_INTERVAL, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_TIME_INTERVAL), L"Time interval in minutes to generate FMAP files. (default: 60)");

	ui_set_fmap_maxforecast(-1);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_FORECAST_TIME, EM_SETLIMITTEXT, 5, 0);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_FORECAST_TIME), L"Maximum time in hours up to which FMAP files are generated. (default: max)");

	ui_set_bms_day(1);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_DAY, EM_SETLIMITTEXT, 3, 0);

	ui_set_bms_hour(9);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_HOUR, EM_SETLIMITTEXT, 2, 0);

	ui_set_bms_minute(0);
	m_ui_initial_time_minute = 0;
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_MINUTE, EM_SETLIMITTEXT, 2, 0);

	// Disable custom save options
	set_save_mode(ui_save_mode::single);
	CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_SAVE_SINGLE, TRUE);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SAVE_SINGLE), L"Save only the currently previewed weather.");
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SAVE_SEQUENCE), L"Save a sequence of files according to the parameters below.");

	ui_set_sync_timezone(false);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SYNC_TIMEZONE), L"Adjust FMAP generation so that local-time in BMS matches real-time of the forecast.");

	ui_set_start_current(false);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_START_CURRENT), L"Start sequence from the currently previewed position.");

	ui_set_save_previews(false);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SAVE_PREVIEWS), L"Save preview images together with the FMAP files.");

	show_warning(IDC_F4WX_MAIN_WARN_INTERVAL, false);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_WARN_INTERVAL), L"Warning! Low interval values are not recommended for multiplayer flights.");

	show_warning(IDC_F4WX_MAIN_WARN_FORECAST, false);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_WARN_FORECAST), L"Warning! The GRIB data loaded is insufficient to generate that much forecast. Not all FMAP files will be generated.");

	show_warning(IDC_F4WX_MAIN_WARN_SYNC, false);
	create_tooltip(m_hwnd, GetDlgItem(m_hwnd, IDC_F4WX_MAIN_WARN_SYNC), L"Warning! With the currently selected options real-time time cannot be matched.");

	// Register the save grib files hotkey
	RegisterHotKey(m_hwnd, static_cast<int>(f4wx_hotkeys::HK_SAVE_GRIB_FILES), MOD_CONTROL | MOD_SHIFT, 0x53);

	ui_slider_set_range(0);

	// Theater list (built-in definitions only)
	HWND hselector = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_THEATER_SELECTOR);
	f4wx_theater_data td;
	for (size_t i : std::views::iota(size_t{0}, m_config.get_theater_count())) {
		if (m_config.get_theater_header(i, &td) == 0)
			SendMessageA(hselector, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(td.name.c_str()));
	}

	SendMessageA(hselector, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Custom..."));
	SendMessage(hselector, CB_SETCURSEL, static_cast<WPARAM>(0), static_cast<LPARAM>(0));

	do_play(false);
	ui_update_status_times();
}

void f4wx::set_save_mode(ui_save_mode mode)
{
	HWND hdlg;
	m_ui_save = mode;

	if (mode == ui_save_mode::sequence && !IsDlgButtonChecked(m_hwnd, IDC_F4WX_MAIN_SAVE_SEQUENCE))
		return;

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_DAY);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_HOUR);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CAMPAIGN_MINUTE);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SYNC_TIMEZONE);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_START_CURRENT);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SAVE_PREVIEWS);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_TIME_INTERVAL);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_FORECAST_TIME);
	EnableWindow(hdlg, mode == ui_save_mode::sequence);

	hdlg = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SAVE);
	EnableWindow(hdlg, mode != ui_save_mode::none);
}

void f4wx::show_warning(int id, bool show)
{
	HWND hwarn = GetDlgItem(m_hwnd, id);
	ShowWindow(hwarn, show);
}

INT_PTR f4wx::on_command(WPARAM wparam, LPARAM lparam)
{
	switch (LOWORD(wparam)) {
		case IDC_F4WX_MAIN_SAVE_SINGLE:
			set_save_mode(ui_save_mode::single);
			break;

		case IDC_F4WX_MAIN_SAVE_SEQUENCE:
			set_save_mode(ui_save_mode::sequence);
			break;

		case IDC_F4WX_MAIN_SAVE:
			on_save();
			break;

		case IDC_F4WX_MAIN_LOAD_FILE:
			on_load_file();
			break;

		case IDC_F4WX_MAIN_DOWNLOAD:
			on_download();
			break;

		case IDC_F4WX_MAIN_PREVIEW_CLOUDS:
			m_previewWindow.toggle_mode(preview_mode::PM_CLOUDS);
			break;

		case IDC_F4WX_MAIN_PREVIEW_PRESSURE:
			m_previewWindow.toggle_mode(preview_mode::PM_PRESSURE);
			break;

		case IDC_F4WX_MAIN_PREVIEW_TEMPERATURE:
			m_previewWindow.toggle_mode(preview_mode::PM_TEMPERATURE);
			break;

		case IDC_F4WX_MAIN_PREVIEW_WINDS:
			if (GetKeyState(VK_SHIFT) & 0x8000) {
				CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_PREVIEW_WINDS, TRUE);
				m_wind_dialog = std::make_unique<f4wx_selectwind>(m_hwnd, m_previewWindow.get_windLevel());
				register_handler(WMU_WIND_SELECTED, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_wind_selected(hwnd, msg, wp, lp); });
			}
			else {
				m_previewWindow.toggle_mode(preview_mode::PM_WIND);
			}
			break;

		case IDC_F4WX_MAIN_SYNC_TIMEZONE:
			ui_set_sync_timezone(!m_ui_sync_with_real);
			break;

		case IDC_F4WX_MAIN_START_CURRENT:
			ui_set_start_current(!m_ui_start_from_current);
			break;

		case IDC_F4WX_MAIN_SAVE_PREVIEWS:
			ui_set_save_previews(!m_ui_save_previews);
			break;

#define HANDLE_EDIT_INT(idc, func) \
	case idc: \
		if (HIWORD(wparam) == EN_KILLFOCUS) { \
			wchar_t buf[MAX_EDIT_LENGTH]; \
			buf[0] = L'\0'; \
			GetWindowTextW(reinterpret_cast<HWND>(lparam), buf, static_cast<int>(std::size(buf))); \
			func(parse_wide_int(buf).value_or(0)); \
			} \
		break;
#define HANDLE_EDIT_FLOAT(idc, func) \
	case idc: \
		if (HIWORD(wparam) == EN_KILLFOCUS) { \
			wchar_t buf[MAX_EDIT_LENGTH]; \
			buf[0] = L'\0'; \
			GetWindowTextW(reinterpret_cast<HWND>(lparam), buf, static_cast<int>(std::size(buf))); \
			func(parse_wide_float(buf).value_or(0.0f)); \
			} \
		break;

			HANDLE_EDIT_INT(IDC_F4WX_MAIN_FORECAST_TIME, ui_set_fmap_maxforecast);
			HANDLE_EDIT_INT(IDC_F4WX_MAIN_TIME_INTERVAL, ui_set_fmap_interval);
			HANDLE_EDIT_INT(IDC_F4WX_MAIN_CAMPAIGN_DAY, ui_set_bms_day);
			HANDLE_EDIT_INT(IDC_F4WX_MAIN_CAMPAIGN_HOUR, ui_set_bms_hour);
			HANDLE_EDIT_INT(IDC_F4WX_MAIN_CAMPAIGN_MINUTE, ui_set_bms_minute);
			HANDLE_EDIT_FLOAT(IDC_F4WX_MAIN_PRECIPIT_INCLEMENT, ui_set_preipit_inclement);
			HANDLE_EDIT_FLOAT(IDC_F4WX_MAIN_PRECIPIT_POOR, ui_set_preipit_poor);
			HANDLE_EDIT_FLOAT(IDC_F4WX_MAIN_PRECIPIT_FAIR, ui_set_preipit_fair);
			HANDLE_EDIT_FLOAT(IDC_F4WX_MAIN_CLOUD_COVER_INCLEMENT, ui_set_cloud_inclement);
			HANDLE_EDIT_FLOAT(IDC_F4WX_MAIN_CLOUD_COVER_POOR, ui_set_cloud_poor);
			HANDLE_EDIT_FLOAT(IDC_F4WX_MAIN_CLOUD_COVER_FAIR, ui_set_cloud_fair);

		case IDC_F4WX_MAIN_ABOUT: {
			register_handler(WMU_ABOUT_CLOSED, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_about_closed(hwnd, msg, wp, lp); });
			m_about_dialog = std::make_unique<f4wx_about>(m_hwnd);
			break;
		}

		case IDC_F4WX_MAIN_THEATER_SELECTOR: {
			if (HIWORD(wparam) == CBN_SELCHANGE) {
				on_theater_change(static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lparam), CB_GETCURSEL, 0, 0)));
			}
			break;
		}
		case IDC_F4WX_MAIN_PLAY:
			do_play(!m_play_state);
			break;

		default:
			return TRUE;
	}
	return FALSE;
}

int f4wx::save_sequence(const std::filesystem::path& path)
{
	size_t i = m_ui_start_from_current ? m_current_map_idx : 0;
	size_t basepos = get_sync_min_pos();

	if (m_ui_sync_with_real == true && i < basepos)
		i = basepos;

	int rv = 0;
	size_t startpos = i;

	setstate(state::S_SAVE);

	// Build 39: progress bar here
	open_progressbar();
	m_bar->set_text("Saving files");
	bool err = false;

	fmap tmpmap(fmap_cells_from_size(m_current_theater.size), fmap_cells_from_size(m_current_theater.size));

	size_t maxcount = std::min(get_fmap_count(), static_cast<size_t>(1 + i + (m_converter_options.max_forecast_hours * 60) / m_converter_options.interval_minutes));

	m_bar->set_total(maxcount - startpos);

	for (; i < maxcount; i++) {
		int day, hr, mn;
		get_bms_time(static_cast<int>((i - basepos) * m_converter_options.interval_minutes), day, hr, mn);
		std::filesystem::path filepath = path / std::format("{}{:02d}{:02d}.fmap", day, hr, mn);

		(void)get_fmap(i, tmpmap);
		err = !tmpmap.save(filepath, true);
		if (err)
			break;

		rv++;

		m_bar->set_position(rv);
	}

	close_progressbar();

	if (err)
		errorbox("Error saving file");

	if (m_ui_save_previews)
		(void)save_previews(path);

	infobox(std::to_string(rv) + " FMAPs saved");
	setstate(state::S_IDLE);
	return rv;
}

int f4wx::save_previews(const std::filesystem::path& path)
{
	size_t i = m_ui_start_from_current ? m_current_map_idx : 0;
	size_t basepos = get_sync_min_pos();

	if (m_ui_sync_with_real == true && i < basepos)
		i = basepos;

	int rv = 0;
	size_t startpos = i;

	// Build 39: progress bar here
	open_progressbar();
	m_bar->set_text("Saving preview images");
	bool err = false;

	f4wx_preview tmp_preview;
	tmp_preview.set_background(m_previewWindow.get_background());

	std::filesystem::path preview_dir = path / F4WX_PREVIEW_SUBDIR;
	if (!std::filesystem::exists(preview_dir)) {
		std::error_code ec;
		if (!std::filesystem::create_directories(preview_dir, ec)) {
			errorbox("Error creating preview file directory");
			return 0;
		}
	}

	CLSID pngClsid;
	GetEncoderClsid(L"image/png", &pngClsid);

	fmap tmpmap(fmap_cells_from_size(m_current_theater.size), fmap_cells_from_size(m_current_theater.size));

	size_t maxcount = std::min(get_fmap_count(), static_cast<size_t>(1 + i + (m_converter_options.max_forecast_hours * 60) / m_converter_options.interval_minutes));
	m_bar->set_total(maxcount - startpos);
	for (; i < maxcount; i++) {
		int day, hr, mn;
		get_bms_time(static_cast<int>((i - basepos) * m_converter_options.interval_minutes), day, hr, mn);
		std::filesystem::path filepath = preview_dir / std::format("{}{:02d}{:02d}.png", day, hr, mn);

		tmp_preview.cleanup();

		(void)get_fmap(i, tmpmap);
		fmap *map = &tmpmap;

		if (static_cast<unsigned>(m_previewWindow.get_mode()) & static_cast<unsigned>(preview_mode::PM_TEMPERATURE))
			tmp_preview.draw_temperature(*map);

		if (static_cast<unsigned>(m_previewWindow.get_mode()) & static_cast<unsigned>(preview_mode::PM_CLOUDS))
			tmp_preview.draw_clouds(*map);

		if (static_cast<unsigned>(m_previewWindow.get_mode()) & static_cast<unsigned>(preview_mode::PM_PRESSURE))
			tmp_preview.draw_pressure(*map);

		if (static_cast<unsigned>(m_previewWindow.get_mode()) & static_cast<unsigned>(preview_mode::PM_WIND))
			tmp_preview.draw_wind(*map);

		err = tmp_preview.save(filepath, &pngClsid, 0) != Status::Ok;
		if (err)
			break;

		rv++;
		m_bar->set_position(rv);
	}

	close_progressbar();

	if (err)
		errorbox("Error saving file");
	return rv;
}


static int CALLBACK BrowseCallbackProc(HWND hwnd,
	UINT uMsg,
	LPARAM lp,
	LPARAM pData)
{
	wchar_t szDir[MAX_PATH + 1];

	switch (uMsg)
	{
		case BFFM_INITIALIZED:
			if (GetCurrentDirectoryW(static_cast<DWORD>(std::size(szDir)), szDir))
			{
				SendMessageW(hwnd, BFFM_SETSELECTION, TRUE, reinterpret_cast<LPARAM>(szDir));
			}
			break;

		case BFFM_SELCHANGED:
			if (SHGetPathFromIDListW(reinterpret_cast<LPITEMIDLIST>(lp), szDir))
			{
				SendMessageW(hwnd, BFFM_SETSTATUSTEXT, 0, reinterpret_cast<LPARAM>(szDir));
			}
			break;
	}
	return 0;
}


void f4wx::threaded_save_sequence(std::filesystem::path path)
{
	(void)save_sequence(path);
}

void f4wx::ui_save_sequence()
{
	if (get_fmap_count() == 0) {
		errorbox("There is nothing to save!");
		return;
	}

	std::vector<wchar_t> buf(MAX_PATH + 1);

	BROWSEINFOW bi { 
		.hwndOwner = m_hwnd,
		.pidlRoot = nullptr,
		.pszDisplayName = buf.data(),
		.lpszTitle = L"Select FMAP Destination Folder",
		.ulFlags = BIF_USENEWUI,
		.lpfn = BrowseCallbackProc,
		.lParam = 0,
		.iImage = 0
	};

	LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
	if (idl != nullptr) {
		SHGetPathFromIDListW(idl, buf.data());
		CoTaskMemFree(idl);
		// SetCurrentDirectoryW(buf.data()); // Removed: Don't change process CWD
		std::filesystem::path chosen(buf.data());
		if (!queue_task([chosen, this]() { threaded_save_sequence(chosen); })) {
			errorbox("Error starting save thread");
		}
	}
}

void f4wx::ui_save_single()
{
	const fmap *f = get_current_fmap();

	if (f == nullptr) {
		errorbox("There is nothing to save!");
		return;
	}

	int day, hr, mn;
	get_bms_time(static_cast<int>((m_current_map_idx - get_sync_min_pos()) * m_converter_options.interval_minutes), day, hr, mn);
	std::wstring filename = std::format(L"{}{:02d}{:02d}.fmap", day, hr, mn);
	filename.resize(_MAX_PATH + 1, L'\0');

	OPENFILENAMEW ofn { };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = L"BMS Weather Maps (*.fmap)\0*.fmap;*.f???\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = filename.data();
	ofn.nMaxFile = static_cast<DWORD>(filename.size());
	ofn.lpstrDefExt = L"fmap";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_OVERWRITEPROMPT;

	if (GetSaveFileNameW(&ofn) == TRUE) {
		bool rv = f->save(std::filesystem::path(filename.c_str()), true);
		if (rv != true) {
			errorbox("Error saving file");
		}
	}
}


void f4wx::on_save()
{
	if (m_ui_save == ui_save_mode::sequence)
		ui_save_sequence();
	else
		ui_save_single();
}

void f4wx::on_load_file()
{
	wchar_t buf[16394];

	OPENFILENAMEW ofn { };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = L"GRIB Files (*.grb;*.f???)\0*.grb;*.f???\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = std::size(buf);
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
	ofn.lpstrFile[0] = L'\0';

	if (GetOpenFileNameW(&ofn) == TRUE && !isupdating()) {
		clear_gribfiles();

		if (ofn.nFileExtension == 0) { // Multiple files
			wchar_t* p = buf;
			while (*p++ != L'\0');
			std::filesystem::path dir_path(buf);
			for (;;) {
				if (*p == L'\0')
					break;

				int rv = ui_load_grib_file(dir_path, std::filesystem::path(p));
				if (rv != 0) {
					errorbox("Error loading file");
					break;
				}
				while (*p++ != L'\0');
			}
		}
		else {	// single file
			std::filesystem::path dir_path;
			std::filesystem::path file_path;
			if (ofn.nFileOffset > 0) {
				buf[ofn.nFileOffset - 1] = L'\0';
				dir_path = buf;
				file_path = buf + ofn.nFileOffset;
			} else {
				dir_path = L".";
				file_path = buf;
			}
			int rv = ui_load_grib_file(dir_path, file_path);
			if (rv != 0) {
				errorbox("Error loading file");
			}
		}
		(void)ui_decode_grib_files();
	}
}


int f4wx::ui_load_grib_file(const std::filesystem::path& dir, const std::filesystem::path& filename)
{
	std::filesystem::path full_path = dir / filename;

	std::ifstream in(full_path, std::ios::binary);
	if (!in)
		return static_cast<int>(errno);

	in.seekg(0, std::ios::end);
	std::streamsize size = in.tellg();
	in.seekg(0, std::ios::beg);

	if (size <= 0 || size > 0x7FFFFFFF)
		return static_cast<int>(errno);

	std::vector<unsigned char> buf(static_cast<size_t>(size));
	if (!in.read(reinterpret_cast<char*>(buf.data()), size))
		return static_cast<int>(errno);

	std::string filename_utf8 = to_utf8(filename.filename().native());
	auto f = std::make_unique<grib_file>(filename_utf8, buf.data(), buf.size());
	m_gribfiles.push_back(std::move(f));
	return 0;
}

void f4wx::on_save_grib_files()
{
	std::vector<wchar_t> buf(MAX_PATH + 1);

	BROWSEINFOW bi { };
	bi.hwndOwner = m_hwnd;
	bi.pszDisplayName = buf.data();
	bi.lpszTitle = L"Select GRIB Destination Folder";
	bi.ulFlags = BIF_USENEWUI;
	bi.lpfn = BrowseCallbackProc;

	LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
	if (idl != nullptr) {
		SHGetPathFromIDListW(idl, buf.data());
		CoTaskMemFree(idl);
		// SetCurrentDirectoryW(buf.data()); // Removed: Don't change process CWD
		std::filesystem::path dir(buf.data());
		size_t count = 0;
		for (const auto& gf : m_gribfiles) {
			// Build path from UTF-16 on Windows so non-ASCII theater/filename are correct
			std::wstring filename_wide = to_wide(std::format("{}-{}", m_current_theater.name, gf->filename));
			std::filesystem::path filepath = dir / filename_wide;
			int rv = gf->buffer->save(filepath);
			if (rv != 0) {
				errorbox("Error saving file");
				break;
			}
			count++;
		}
		infobox(std::to_string(count) + " files saved");
	}
}

INT_PTR f4wx::on_hotkey(WPARAM wparam, LPARAM lparam)
{
	switch (wparam) {
		case static_cast<WPARAM>(static_cast<int>(f4wx_hotkeys::HK_SAVE_GRIB_FILES)):
			on_save_grib_files();
			break;
	}
	return 0;
}

void f4wx::set_current_fmap(size_t pos, bool override)
{
	if ((override || pos != m_current_map_idx) && pos < get_fmap_count()) {
		correct_for_timezone_sync(pos);
		m_current_map_idx = pos;

		if (!m_current_map || (m_current_map->get_sizeY() != fmap_cells_from_size(m_current_theater.size) || m_current_map->get_sizeX() != fmap_cells_from_size(m_current_theater.size))) {
			m_current_map = std::make_unique<fmap>(fmap_cells_from_size(m_current_theater.size), fmap_cells_from_size(m_current_theater.size));
		}
		(void)get_fmap(pos, *m_current_map);
	}
	m_previewWindow.set_fmap(m_current_map.get());
#ifdef FMAP_DEBUG
	m_previewWindow.set_latlons(m_converter.get_grib_tlat(), m_converter.get_grib_blat(), m_converter.get_grib_llon(), m_converter.get_grib_rlon());
#endif
	ui_set_slider(m_current_map_idx);
	ui_update_status_times();
}

void f4wx::ui_set_slider(size_t pos)
{
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_SLIDER, TBM_SETPOS, TRUE, static_cast<LPARAM>(pos));
}

INT_PTR f4wx::on_slider_scroll(WPARAM wparam, LPARAM lparam)
{
	if (reinterpret_cast<HWND>(lparam) == GetDlgItem(m_hwnd, IDC_F4WX_MAIN_SLIDER)) {
		switch (LOWORD(wparam)) {
			case SB_THUMBTRACK:
			case SB_THUMBPOSITION: {
				set_current_fmap(static_cast<size_t>(HIWORD(wparam)));
				break;
			}
			default: {
				size_t pos = static_cast<size_t>(SendMessage(reinterpret_cast<HWND>(lparam), TBM_GETPOS, 0, 0));
				set_current_fmap(pos);
			}
		}
	}
	return 0;
}

void f4wx::on_download()
{
	if (!is_progressbar_open()) {
		setstate(state::S_DLGFSIDX);

		register_handler(WMU_GFSLIST_DOWNLOADED, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_gfslist_downloaded(hwnd, msg, wp, lp); });
		if (!queue_task([this]() { threaded_update_gfs_list(); })) {
			errorbox("Error starting download thread");
		}
	}
}

void f4wx::threaded_update_gfs_list()
{
	open_progressbar();
	m_bar->set_marquee();
	m_bar->set_text("Downloading GFS data");

	int rv = update_gfs_runs();

	close_progressbar();

	PostMessage(m_hwnd, WMU_GFSLIST_DOWNLOADED, rv, 0);
}


INT_PTR f4wx::on_gfslist_downloaded(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	int rv = static_cast<int>(wparam);
	unregister_handler(WMU_GFSLIST_DOWNLOADED);
	setstate(state::S_IDLE);

	if (rv != 0) {
		errorbox("Error downloading GFS data");
		return 0;
	}

	register_handler(WMU_GFS_SELECTED, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_gfsrun_selected(hwnd, msg, wp, lp); });

	m_gfs_dialog = std::make_unique<f4wx_selectgfs>(m_hwnd, m_gfsruns, m_download_interval, m_download_max);
	return 0;
}

INT_PTR f4wx::on_gfsrun_selected(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	f4wx_selectgfs* gfs = reinterpret_cast<f4wx_selectgfs*>(wparam);
	unregister_handler(WMU_GFS_SELECTED);
	auto pos = gfs->get_pos();
	m_download_max = gfs->get_download_max();
	m_download_interval = gfs->get_download_interval();
	if (m_gfs_dialog.get() == gfs)
		m_gfs_dialog.reset();
	if (pos && !isupdating()) {
		ui_download_gfs_run(m_gfsruns.size() - *pos);
	}
	return 0;
}



f4wx_progressbar::f4wx_progressbar(HWND hParent, ITaskbarList3 *pTL, ULONGLONG total)
	: m_pTL(pTL)
{
	init_dialog(IDD_F4WX_PROGRESS, hParent);
	set_total(total);
	if (m_pTL != nullptr) {
		m_pTL->SetProgressState(hParent, TBPF_NORMAL);
	}
}

f4wx_progressbar::~f4wx_progressbar()
{
	if (m_pTL != nullptr) {
		m_pTL->SetProgressState(GetParent(m_hwnd), TBPF_NOPROGRESS);
	}
}

void f4wx_progressbar::set_position(size_t pos)
{
	HWND hbar = GetDlgItem(m_hwnd, IDC_F4WX_PROGRESS_BAR);
	SendMessage(hbar, PBM_SETPOS, static_cast<WPARAM>(pos), 0);
	if (m_pTL != nullptr) {
		m_pTL->SetProgressValue(GetParent(m_hwnd), pos, m_total);
	}
}

void f4wx_progressbar::set_total(ULONGLONG total)
{
	m_total = total;
	HWND hbar = GetDlgItem(m_hwnd, IDC_F4WX_PROGRESS_BAR);
	SendMessage(hbar, PBM_SETRANGE, 0, MAKELPARAM(0, total));
}


void CALLBACK f4wx_progressbar::text_animation_timer(HWND hwnd, UINT umsg, UINT_PTR id, DWORD dwtime) {
	wchar_t buf[128];
	HWND htext = GetDlgItem(hwnd, IDC_F4WX_PROGRESS_TEXT);
	int n = GetWindowTextW(htext, buf, static_cast<int>(std::size(buf)));
	int lastnodot = n;
	if (n > 0) {
		while (buf[--lastnodot] == L'.');

		if (n - lastnodot > 3) {
			if (++lastnodot < static_cast<int>(std::size(buf)) - 1)
				buf[lastnodot] = L'\0';
		}
		else {
			if (n < static_cast<int>(std::size(buf)) - 2) {
				buf[n] = L'.';
				buf[n + 1] = L'\0';
			}
		}

		SetWindowTextW(htext, buf);
	}
	SetTimer(hwnd, static_cast<UINT_PTR>(f4wx_progressbar::timers::PBTI_TEXTANIM), 500, text_animation_timer);
}

void f4wx_progressbar::set_text(const char *str)
{
	HWND htext = GetDlgItem(m_hwnd, IDC_F4WX_PROGRESS_TEXT);
	SetWindowTextW(htext, to_wide(str).c_str());
	//	SetTimer(m_hwnd, PBTI_TEXTANIM, 500, text_amimation_Timer);

}

void f4wx_progressbar::set_marquee(bool enable, unsigned long interval)
{
	HWND hbar = GetDlgItem(m_hwnd, IDC_F4WX_PROGRESS_BAR);
	if (enable) {
		SetWindowLongPtr(hbar, GWL_STYLE, GetWindowLongPtr(hbar, GWL_STYLE) | PBS_MARQUEE);
		SendMessage(hbar, PBM_SETMARQUEE, 1, interval);
		if (m_pTL != nullptr) {
			m_pTL->SetProgressState(GetParent(m_hwnd), TBPF_INDETERMINATE);
		}
	}
	else {
		SendMessage(hbar, PBM_SETMARQUEE, 0, 0);
		SetWindowLongPtr(hbar, GWL_STYLE, GetWindowLongPtr(hbar, GWL_STYLE) & ~PBS_MARQUEE);
		if (m_pTL != nullptr) {
			m_pTL->SetProgressState(GetParent(m_hwnd), TBPF_NORMAL);
		}
	}
}


HWND f4wx_hcurrentdialog = nullptr;

INT_PTR CALLBACK f4wx_dialog::static_dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	f4wx_dialog *ptr = nullptr;

	if (msg == WM_INITDIALOG) {
		ptr = reinterpret_cast<f4wx_dialog*>(lparam);
		ptr->m_hwnd = hwnd;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, lparam);
	}
	else {
		if (msg == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE)
			f4wx_hcurrentdialog = hwnd;

		ptr = reinterpret_cast<f4wx_dialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	}
	if (ptr != nullptr) {
		for (const auto& h : ptr->m_handlers)
			if (h.msg == msg)
				return h.handler(ptr, hwnd, msg, wparam, lparam);

		return ptr->dialog_proc(hwnd, msg, wparam, lparam);
	}

	return 0;
}

INT_PTR CALLBACK f4wx_dialog::dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	return 0;
}


// need this, if done in constructor the dialog gets created before the inherited class gets initialized
int f4wx_dialog::init_dialog(int id, HWND hparent, bool stealfocus)
{
	m_hwnd = CreateDialogParam(GetModuleHandle(nullptr), MAKEINTRESOURCE(id), hparent, static_dialog_proc, reinterpret_cast<LPARAM>(this));
	if (m_hwnd != nullptr) {
		if (hparent != nullptr) {
			RECT pr, mr;
			GetWindowRect(hparent, &pr);
			GetWindowRect(m_hwnd, &mr);
			SetWindowPos(m_hwnd, HWND_TOP, (pr.left + pr.right) / 2 - (mr.right - mr.left) / 2, (pr.top + pr.bottom) / 2 - (mr.bottom - mr.top) / 2, mr.right - mr.left, mr.bottom - mr.top, 0);
			if (stealfocus) {
				EnableWindow(hparent, FALSE);
			}
		}
		ShowWindow(m_hwnd, SW_SHOW);
	}
	return GetLastError();
}

f4wx_dialog::~f4wx_dialog()
{
	HWND hparent = GetParent(m_hwnd);
	SetWindowLongPtr(m_hwnd, GWLP_USERDATA, 0);
	if (hparent != nullptr) {
		EnableWindow(hparent, TRUE);
		SetFocus(hparent);
	}
	DestroyWindow(m_hwnd);

}

bool f4wx_dialog::register_handler(UINT msg, message_handler_fn handler)
{
	for (const auto& h : m_handlers)
		if (h.msg == msg)
			return false;

	handler_info hi;
	hi.msg = msg;
	hi.handler = handler;
	m_handlers.push_back(hi);
	return true;
}
void f4wx_dialog::unregister_handler(UINT msg)
{
	if (auto it = std::ranges::find_if(m_handlers, [msg](const auto& h) { return h.msg == msg; }); it != m_handlers.end())
		m_handlers.erase(it);
}

f4wx_selectgfs::f4wx_selectgfs(HWND hparent, const noaa_gfsrun_filename_list& gfsruns, unsigned download_interval, unsigned download_max)
	: m_download_interval(download_interval),
	m_download_max(download_max)
{
	init_dialog(IDD_F4WX_GFS_SELECTOR, hparent);

	HWND hlist = GetDlgItem(m_hwnd, IDC_F4WX_GFS_LIST);
	for (auto it = gfsruns.rbegin(); it != gfsruns.rend(); ++it) {
		SendMessageA(hlist, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(it->get_pretty_name().c_str()));
	}
	SendMessage(hlist, LB_SETCURSEL, 0, 0);

	if (GetForegroundWindow() != m_hwnd)
		FlashWindow(hparent, TRUE);
}

f4wx_selectgfs::~f4wx_selectgfs()
{
}

void f4wx_selectgfs::notify_selection(int pos)
{
	m_pos = pos;
	PostMessage(GetParent(m_hwnd), f4wx::WMU_GFS_SELECTED, reinterpret_cast<LPARAM>(this), 0);
}


INT_PTR CALLBACK f4wx_selectgfs::dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
		case WM_INITDIALOG: {
			set_text_control_int(hwnd, IDC_F4WX_GFS_INTERVAL, m_download_interval);
			SendDlgItemMessage(m_hwnd, IDC_F4WX_GFS_INTERVAL, EM_SETLIMITTEXT, 5, 0);
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_GFS_INTERVAL), L"Values higher than 1 skip downloading some files, meaning less data to download but lower weather accuracy. (default: 3 hrs)");

				if (m_download_max == DOWNLOAD_MAX_UNLIMITED) {
				SendDlgItemMessageW(m_hwnd, IDC_F4WX_GFS_FORECAST, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"Max"));
				SendDlgItemMessage(m_hwnd, IDC_F4WX_GFS_FORECAST, EM_SETLIMITTEXT, 5, 0);
			}
			else {
				set_text_control_int(hwnd, IDC_F4WX_GFS_FORECAST, m_download_max);
				SendDlgItemMessage(m_hwnd, IDC_F4WX_GFS_FORECAST, EM_SETLIMITTEXT, 5, 0);
			}
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_GFS_FORECAST), L"Limits the number of files by setting a limit in hours on the forecast. (default: max)");
			break;
		}
		case WM_CLOSE: {
			notify_selection(LB_ERR);
			return TRUE;
			break;
		}
		case WM_COMMAND: {
			switch (LOWORD(wparam))
			{
				case IDC_F4WX_GFS_LIST:
					if (HIWORD(wparam) != LBN_DBLCLK)
						break;

				case IDOK:
					notify_selection(static_cast<int>(SendDlgItemMessageW(hwnd, IDC_F4WX_GFS_LIST, LB_GETCURSEL, 0, 0)));
					return TRUE;

				case IDCANCEL:
					notify_selection(LB_ERR);
					return TRUE;

				case IDC_F4WX_GFS_FORECAST:
					if (HIWORD(wparam) == EN_KILLFOCUS) {
						wchar_t buf[MAX_EDIT_LENGTH + 1];
						GetWindowTextW(reinterpret_cast<HWND>(lparam), buf, static_cast<int>(std::size(buf)));
						m_download_max = parse_wide_int(buf).value_or(0);
						if (static_cast<int>(m_download_max) <= 0) {
							m_download_max = DOWNLOAD_MAX_UNLIMITED;
							SetWindowTextW(reinterpret_cast<HWND>(lparam), L"Max");
						}
						else {
							SetWindowTextW(reinterpret_cast<HWND>(lparam), std::format(L"{}", m_download_max).c_str());
						}
					}
					break;

				case IDC_F4WX_GFS_INTERVAL:
					if (HIWORD(wparam) == EN_KILLFOCUS) {
						wchar_t buf[MAX_EDIT_LENGTH + 1];
						GetWindowTextW(reinterpret_cast<HWND>(lparam), buf, static_cast<int>(std::size(buf)));
						m_download_interval = parse_wide_int(buf).value_or(1);
						if (static_cast<int>(m_download_interval) < 1)
							m_download_interval = 1;
						SetWindowTextW(reinterpret_cast<HWND>(lparam), std::format(L"{}", m_download_interval).c_str());
					}
					break;
			}
		}
						 break;
	}
	return 0;
}

f4wx_about::f4wx_about(HWND hparent)
{
	init_dialog(IDD_F4WX_ABOUT, hparent);
}

f4wx_about::~f4wx_about()
{
}


INT_PTR CALLBACK f4wx_about::dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
		case WM_INITDIALOG: {
			HWND htext = GetDlgItem(hwnd, IDC_F4WX_ABOUT_TEXT);
			std::wstring about = to_wide(std::format("F4Wx v{}", F4WX_VERSION));
			SetWindowTextW(htext, about.c_str());
			break;
		}
		case WM_CLOSE:
		case WM_COMMAND:
			PostMessageW(GetParent(hwnd), f4wx::WMU_ABOUT_CLOSED, 0, 0);
			return TRUE;
	}
	return 0;
}

INT_PTR f4wx::on_about_closed(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	unregister_handler(WMU_ABOUT_CLOSED);
	m_about_dialog.reset();
	return 0;
}

void f4wx::ui_download_gfs_run(size_t idx)
{
	--idx;

	if (m_gfsruns[idx] != m_last_gfsrun) {
		clear_gribfiles();
		m_last_gfsrun = m_gfsruns[idx];		// If it is different clear here. In the threaded function we also check for number of files changing
	}

	setstate(state::S_DLGFSGRB);

	register_handler(WMU_GFSRUN_INDEX_DOWNLOADED, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_gfsrun_index_downloaded(hwnd, msg, wp, lp); });

	if (!queue_task([this, idx]() { threaded_get_gfsrun_index(idx); })) {
		setstate(state::S_IDLE);
		errorbox("Error starting download thread");
	}
}

void f4wx::threaded_get_gfsrun_index(size_t idx)
{
	open_progressbar();
	m_bar->set_marquee();
	m_bar->set_text("Downloading file index");

	m_gfsrun_forecastfilenames.clear();
	noaa_downloader dl;
	int rv = dl.get_gfs_run_forecasts(m_gfsruns[idx], m_gfsrun_forecastfilenames);

	close_progressbar();
	PostMessage(m_hwnd, WMU_GFSRUN_INDEX_DOWNLOADED, rv, static_cast<LPARAM>(idx));
}

INT_PTR f4wx::on_gfsrun_index_downloaded(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	unregister_handler(WMU_GFSRUN_INDEX_DOWNLOADED);

	int rv = static_cast<int>(wparam);
	if (rv != 0) {
		errorbox("Error downloading file index.\n\nThis may be caused by NOAA still being in the process of uploading files. Please try with a different GFS run.");
		setstate(state::S_IDLE);
		return 0;
	}

	register_handler(WMU_GFSRUN_FILES_DOWNLOADED, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_gfsrun_files_downloaded(hwnd, msg, wp, lp); });

	if (!queue_task([this, idx = static_cast<size_t>(lparam)]() { threaded_download_gfsrun_files(idx); })) {
		errorbox("Error starting download thread");
		setstate(state::S_IDLE);
		return 1;
	}

	return 0;
}

void f4wx::threaded_download_gfsrun_files(size_t idx)
{
	GetAsyncKeyState(VK_ESCAPE);

	/* Copy stop token so async workers can see app shutdown and exit; avoids hang on close. */
	std::stop_token stoken = m_worker_stop_token;

	open_progressbar();

	// Remove files that do not belong to this GFS run
	std::erase_if(m_gribfiles, [this](const std::unique_ptr<grib_file>& gf) {
		bool found = std::ranges::any_of(m_gfsrun_forecastfilenames, [&gf](const auto& fn) { return fn.get_filename() == gf->filename; });
		if (!found) {
			return true;
		}
		return false;
	});

	std::deque<noaa_gfsrun_forecastfilename> queue;
	int nextdownload = 0;

	// Prepare download queue
	for (const auto& fn : m_gfsrun_forecastfilenames) {
		if (!fn.is_valid())
			continue;
		int thishour = std::stoi(fn.get_hour());

		if (static_cast<unsigned>(thishour) > m_download_max)
			break;

		if (thishour >= nextdownload) {
			// Dont download file if we already have it
			bool found = std::ranges::any_of(m_gribfiles, [&fn](const auto& gf) { return gf->filename == fn.get_filename(); });
			if (!found) {
				queue.push_back(fn);
			}
			nextdownload += m_download_interval;
		}
	}

	size_t total_downloads = queue.size();
	m_bar->set_total(total_downloads);

	if (total_downloads == 0) {
		close_progressbar();
		PostMessage(m_hwnd, WMU_GFSRUN_FILES_DOWNLOADED, 0, 0);
		return;
	}

	std::mutex mtx;
	std::atomic<bool> stop_download = false;
	int global_rv = 0;
	size_t processed_count = 0;

	auto worker = [&, stoken]() {
		noaa_downloader dl;
		while (true) {
			noaa_gfsrun_forecastfilename fn;
			{
				std::lock_guard<std::mutex> lock(mtx);
				if (queue.empty() || stop_download) return;
				if (stoken.stop_requested()) {
					stop_download = true;
					m_bar->set_text("Cancelling...");
					return;
				}
				fn = queue.front();
				queue.pop_front();

				if (GetAsyncKeyState(VK_ESCAPE) != 0 && GetForegroundWindow() == m_bar->get_hwnd()) {
					stop_download = true;
					global_rv = -1; // user abort
					m_bar->set_text("Cancelling...");
					return;
				}

				std::string msg = "Downloading Hour " + fn.get_hour() + "...";
				m_bar->set_text(msg.c_str());
			}

			auto f = std::make_unique<grib_file>(fn);
			int rv = dl.get_gfs_grib(m_gfsruns[idx], fn,
				static_cast<int>(m_current_theater.llon),
				static_cast<int>(m_current_theater.rlon),
				static_cast<int>(m_current_theater.tlat),
				static_cast<int>(m_current_theater.blat),
				*f->buffer);

			{
				std::lock_guard<std::mutex> lock(mtx);
				if (stop_download) { // Check if stopped while we were downloading
					return;
				}
				if (stoken.stop_requested()) {
					stop_download = true;
					return;
				}

				if (rv != 0) {
					if (global_rv == 0) global_rv = rv;
					stop_download = true;
					return;
				}

				m_gribfiles.push_back(std::move(f));
				processed_count++;
				m_bar->set_position(processed_count);
			}
		}
	};

	// Parallel download with 4 threads
	std::vector<std::future<void>> futures;
	unsigned int n_threads = 4;
	for (unsigned int i = 0; i < n_threads; ++i) {
		futures.push_back(std::async(std::launch::async, worker));
	}

	for (auto& f : futures) {
		f.wait();
	}

	close_progressbar();

	// If aborted (global_rv == -1), we treat it as success (partial) to match original behavior logic
	if (global_rv == -1) global_rv = 0;

	PostMessage(m_hwnd, WMU_GFSRUN_FILES_DOWNLOADED, global_rv, 0);
}

int f4wx::ui_decode_grib_files()
{
	int rv = 0;

	setstate(state::S_DECODE);
	open_progressbar(get_gribfile_count());
	m_bar->set_text("Decoding weather data");
	m_converter.reset(fmap_cells_from_size(m_current_theater.size), fmap_cells_from_size(m_current_theater.size));

	for (size_t i = 0; i < get_gribfile_count();) {
		// grib-api does not take const. But we won't modify the data, and grib-api would make a copy in that case anyway.
		auto ptr = reinterpret_cast<std::byte*>(const_cast<unsigned char*>(get_gribfile_data(i)));
		size_t size = get_gribfile_data_size(i);
		rv = m_converter.add_grib(std::span<std::byte>(ptr, size));
		if (rv != 0)
			break;

		i++;

		m_bar->set_position(i);
	}
	close_progressbar();

	// Build 39: return on error
	if (rv != 0) {
		errorbox(std::string("Error decoding weather data:") + m_converter.get_last_error());
		setstate(state::S_IDLE);
		return rv;
	}

	if (m_converter.get_grib_llon() != m_current_theater.llon ||
		m_converter.get_grib_rlon() != m_current_theater.rlon ||
		m_converter.get_grib_tlat() != m_current_theater.tlat ||
		m_converter.get_grib_blat() != m_current_theater.blat) {

		int opt = questionbox("This GRIB data is from a different geographical location than the selected theater. Do you want to continue?");

		if (opt == IDCANCEL || opt == IDNO)
			return 0;
	}

	show_warning(IDC_F4WX_MAIN_WARN_FORECAST, m_converter_options.max_forecast_hours != FORECAST_HOURS_NO_LIMIT && m_converter_options.max_forecast_hours > m_converter.get_max_possible_forecast());

	rv = convert_grib_files(true);
	return rv;
}


INT_PTR f4wx::on_gfsrun_files_downloaded(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	unregister_handler(WMU_GFSRUN_FILES_DOWNLOADED);

	int rv = static_cast<int>(wparam);
	if (rv != 0) {
		errorbox("Error downloading file index.");
		return 0;
	}

	(void)ui_decode_grib_files();

	return 0;
}



HWND g_hwndTrackingTT = nullptr;
TOOLINFO g_toolItem;
BOOL g_TrackingMouse = FALSE;


HWND CreateTrackingToolTip(HWND hDlg, WCHAR* pText)
{
	HINSTANCE g_hInst = GetModuleHandle(nullptr);

	// Create a tooltip.
	HWND hwndTT = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		hDlg, nullptr, g_hInst, 0);

	if (!hwndTT)
	{
		return nullptr;
	}

	// Set up the tool information. In this case, the "tool" is the entire parent window.

	g_toolItem.cbSize = sizeof(TOOLINFO);
	g_toolItem.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
	g_toolItem.hwnd = hDlg;
	g_toolItem.hinst = g_hInst;
	g_toolItem.lpszText = pText;
	g_toolItem.uId = reinterpret_cast<UINT_PTR>(hDlg);

	GetClientRect(hDlg, &g_toolItem.rect);

	// Associate the tooltip with the tool window.

	SendMessage(hwndTT, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&g_toolItem));

	return hwndTT;
}

INT_PTR f4wx::on_preview_right_click(HWND hwnd, POINT& pt)
{
	HMENU hmenu = LoadMenu(nullptr, MAKEINTRESOURCE(IDM_F4WX_PREVIEW_MENU));
	if (!hmenu)
		return 0;

	HMENU hpup = GetSubMenu(hmenu, 0);

	if (m_current_theater.name.starts_with("custom-")) {
		AppendMenuW(hpup, MF_ENABLED, IDM_F4WX_PREVIEW_MENU_LOAD, L"Load Background");
	}
	EnableMenuItem(hpup, IDM_F4WX_PREVIEW_MENU_SAVE, MF_BYCOMMAND | (m_play_state ? MF_DISABLED : MF_ENABLED));
	ClientToScreen(hwnd, &pt);
	int opt = TrackPopupMenu(hpup, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, m_hwnd, nullptr);
	switch (opt) {
		case IDM_F4WX_PREVIEW_MENU_SAVE:
			ui_save_preview();
			break;
		case IDM_F4WX_PREVIEW_MENU_LOAD:
			ui_load_preview_background();
			break;
	}
	DestroyMenu(hmenu);

	return 0;
}

void f4wx::ui_save_preview()
{
	if (!m_previewWindow.is_valid()) {
		errorbox("There is nothing to save!");
		return;
	}

	int day, hr, mn;
	get_bms_time(static_cast<int>((m_current_map_idx - get_sync_min_pos()) * m_converter_options.interval_minutes), day, hr, mn);
	std::wstring filename = std::format(L"{}{:02d}{:02d}.png", day, hr, mn);
	filename.resize(_MAX_PATH + 1, L'\0');

	OPENFILENAMEW ofn { };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = filename.data();
	ofn.nMaxFile = static_cast<DWORD>(filename.size());
	ofn.lpstrDefExt = L"png";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_OVERWRITEPROMPT;

	if (GetSaveFileNameW(&ofn) == TRUE) {
		CLSID pngClsid;
		GetEncoderClsid(L"image/png", &pngClsid);
		Gdiplus::Status rv = m_previewWindow.save(std::filesystem::path(filename), &pngClsid, 0);
		if (rv != Gdiplus::Status::Ok) {
			errorbox("Error saving file");
		}
	}
}

void f4wx::ui_load_preview_background()
{
	wchar_t buf[MAX_PATH + 1];
	OPENFILENAMEW ofn { };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hwnd;
	ofn.lpstrFilter = L"Image Files (*.png;*.jpg;*.bmp)\0*.png;*.jpg;*.bmp\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = std::size(buf);
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
	ofn.lpstrFile[0] = L'\0';

	if (GetOpenFileNameW(&ofn) == TRUE) {
		int rv = m_previewWindow.load_background(std::filesystem::path(ofn.lpstrFile));
		if (rv != 0) {
			errorbox("Error loading file");
		}
	}
}


////////////////////////////////////////////////////////////////////////// PREVIEW //////////////////////////////////////////////////////////////////////////


void f4wx::get_preview_rect(RECT& r)
{
	HWND hp = GetDlgItem(m_hwnd, IDC_F4WX_MAIN_PREVIEW);
	RECT r1, r2;
	GetWindowRect(m_hwnd, &r1);
	GetWindowRect(hp, &r2);
	r.left = r2.left - r1.left;
	r.top = r2.top - r1.top - 15;
	r.right = r.left + r2.right - r2.left - 15;
	r.bottom = r.top + r2.bottom - r2.top - 25;
}


INT_PTR f4wx::on_paint(HWND hwnd)
{
	// Paint is handled by default dialog procedure (DefDlgProc) for standard controls.
	// This handler is reserved for future custom painting if needed.
	return 0;
}

void f4wx::correct_for_timezone_sync(size_t &pos)
{
	size_t minpos = get_sync_min_pos();
	if (pos < minpos)
		pos = minpos;

	// Build 39: if sync with real and save from current only allow to select positions that comply both
	if (m_ui_start_from_current == true && m_ui_sync_with_real) {
		float perday = 1440.0f / static_cast<float>(m_converter_options.interval_minutes);
		float carry = fmod(static_cast<float>(pos - minpos), perday);
		pos -= static_cast<size_t>(carry);
	}
}

size_t f4wx::get_sync_min_pos()
{
	size_t pos = 0;

	if (m_ui_sync_with_real == true) {
		if (get_fmap_count() > 0) {
			int gfsminutes = m_converter.get_grib_hour() * 60 + m_current_theater.timezone;
			int bmsminutes = m_ui_initial_time_hour * 60 + m_ui_initial_time_minute;
			int minpos = (bmsminutes - gfsminutes) / static_cast<int>(m_converter_options.interval_minutes);
			if (minpos < 0)
				minpos += (24 * 60) / m_converter_options.interval_minutes;
			if (minpos > 0 && static_cast<size_t>(minpos) > pos)
				pos = std::min(static_cast<size_t>(minpos), get_fmap_count() - 1);
		}
	}
	return pos;
}


void f4wx::get_real_time(int minuteoffset, int& year, int& mon, int& day, int& hr, int &min)
{
	std::tm tmi = {};
	tmi.tm_year = m_converter.get_grib_year() - 1900;
	tmi.tm_mon = m_converter.get_grib_month() - 1;
	tmi.tm_mday = m_converter.get_grib_day();
	tmi.tm_hour = m_converter.get_grib_hour();
	tmi.tm_isdst = -1;

	std::time_t tt = _mkgmtime(&tmi);
	tt += minuteoffset * 60;
	std::optional<std::tm> opt = gmtime_utc(tt);
	if (!opt) {
		day = 1; mon = 1; year = 1970; hr = 0; min = 0;
		return;
	}
	day = opt->tm_mday;
	mon = opt->tm_mon + 1;
	year = opt->tm_year + 1900;
	hr = opt->tm_hour;
	min = opt->tm_min;
}

void f4wx::ui_update_status_times()
{
	bool showwarning = false;
	const fmap *map = get_current_fmap();
	if (map != nullptr) {
		// Current Time
		int day, mon, year, hr, min;
		get_real_time(static_cast<int>(m_current_map_idx * m_converter_options.interval_minutes), year, mon, day, hr, min);
		auto current_time_str = std::format(L"{:02d}/{:02d}/{:02d} {:02d}:{:02d} UTC", day, mon, year % 100, hr, min);
		SetWindowTextW(GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CURRENT_TIME), current_time_str.c_str());

		int bmsday, bmshr, bmsmin;
		get_bms_time(static_cast<int>((m_current_map_idx - get_sync_min_pos()) * m_converter_options.interval_minutes), bmsday, bmshr, bmsmin);

		int tzhr = m_current_theater.timezone / 60, tzmin = m_current_theater.timezone % 60;

		auto bms_time_str = tzmin > 0 
			? std::format(L"Day {} {:02d}:{:02d} (UTC{}{}:{})", bmsday, bmshr, bmsmin, tzhr >= 0 ? L'+' : L'-', abs(tzhr), abs(tzmin))
			: std::format(L"Day {} {:02d}:{:02d} (UTC{}{})", bmsday, bmshr, bmsmin, tzhr >= 0 ? L'+' : L'-', abs(tzhr));
		
		SendDlgItemMessageW(m_hwnd, IDC_F4WX_MAIN_BMS_TIME, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(bms_time_str.c_str()));

		// Build 39: Show timezone warning
		if (m_ui_sync_with_real == true) {
			bmshr = (bmshr - tzhr) % 24;
			bmsmin -= tzmin;
			if (bmsmin < 0) {
				bmshr--;
				bmsmin += 60;
			}
			if (bmshr < 0)
				bmshr += 24;
			showwarning = (hr != bmshr || min != bmsmin);
		}
	}
	ShowWindow(GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CURRENT_TIME), map != nullptr ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(m_hwnd, IDC_F4WX_MAIN_CURRENT_TIME_PREFIX), map != nullptr ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(m_hwnd, IDC_F4WX_MAIN_NODATA), map != nullptr ? SW_HIDE : SW_SHOW);
	ShowWindow(GetDlgItem(m_hwnd, IDC_F4WX_MAIN_BMS_TIME), map != nullptr ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(m_hwnd, IDC_F4WX_MAIN_BMS_TIME_PREFFIX), map != nullptr ? SW_SHOW : SW_HIDE);
	show_warning(IDC_F4WX_MAIN_WARN_SYNC, showwarning);

}

void f4wx::get_bms_time(int minuteoffset, int& day, int& hr, int &min)
{
	// Build 39 - start from current new behavior
	if (m_ui_start_from_current == true)
		minuteoffset -= static_cast<int>((m_current_map_idx - get_sync_min_pos()) * m_converter_options.interval_minutes);

	day = m_ui_initial_time_day;
	hr = m_ui_initial_time_hour + minuteoffset / 60;
	min = m_ui_initial_time_minute + minuteoffset % 60;

	while (min > 59) {
		hr++;
		min -= 60;
	}

	while (hr > 23) {
		day++;
		hr -= 24;
	}
}

void f4wx::on_theater_change(int idx)
{
	clear_gribfiles();
	clear_fmaps();
	// config_load_theater returns non-zero when no built-in theater (e.g. custom or invalid idx), so show custom-theater UI.
	if (config_load_theater(idx) != 0) {
		on_custom_theater();
	}
}


void f4wx::on_custom_theater()
{
	register_handler(WMU_CUSTOM_SELECTED, [this](f4wx_dialog*, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return on_custom_closed(hwnd, msg, wp, lp); });

	m_previewWindow.load_background(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDB_F4WX_CUSTOM_MAP), L"PNG");

	m_custom_dialog = std::make_unique<f4wx_custom>(m_hwnd, m_current_theater);
}

INT_PTR f4wx::on_custom_closed(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	unregister_handler(WMU_CUSTOM_SELECTED);
	f4wx_custom* dlg = reinterpret_cast<f4wx_custom*>(wparam);
	if (m_custom_dialog.get() == dlg && dlg->get_result()) {
		m_current_theater = *dlg->get_result();
	}
	if (m_custom_dialog.get() == dlg)
		m_custom_dialog.reset();
	return 0;
}

f4wx_custom::f4wx_custom(HWND hparent, f4wx_theater_data &data)
{
	init_dialog(IDD_F4WX_CUSTOM, hparent);

	set_text_control_float(m_hwnd, IDC_F4WX_CUSTOM_TIMEZONE, data.timezone/60.0f, L"{:+2.2f}");
	SendDlgItemMessage(m_hwnd, IDC_F4WX_CUSTOM_TIMEZONE, EM_SETLIMITTEXT, 7, 0);

	set_text_control_float(m_hwnd, IDC_F4WX_CUSTOM_BLAT, data.blat, L"{:+02.2f}");
	set_text_control_float(m_hwnd, IDC_F4WX_CUSTOM_TLAT, data.tlat, L"{:+02.2f}");
	set_text_control_float(m_hwnd, IDC_F4WX_CUSTOM_RLON, data.rlon, L"{:+03.2f}");
	set_text_control_float(m_hwnd, IDC_F4WX_CUSTOM_LLON, data.llon, L"{:+03.2f}");

	SendDlgItemMessage(m_hwnd, IDC_F4WX_CUSTOM_BLAT, EM_SETLIMITTEXT, 7, 0);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_CUSTOM_TLAT, EM_SETLIMITTEXT, 7, 0);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_CUSTOM_LLON, EM_SETLIMITTEXT, 7, 0);
	SendDlgItemMessage(m_hwnd, IDC_F4WX_CUSTOM_RLON, EM_SETLIMITTEXT, 7, 0);

	SendDlgItemMessageW(m_hwnd, IDC_F4WX_CUSTOM_THEATER_SIZE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"64"));
	SendDlgItemMessageW(m_hwnd, IDC_F4WX_CUSTOM_THEATER_SIZE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"128"));
	SendDlgItemMessage(m_hwnd, IDC_F4WX_CUSTOM_THEATER_SIZE, CB_SETCURSEL, data.size / 64 - 1, 0);
}

f4wx_custom::~f4wx_custom()
{
}

INT_PTR CALLBACK f4wx_custom::dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
		case WM_INITDIALOG: {
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_CUSTOM_TLAT), L"Enter theater topmost latitude. (+N/-S)");
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_CUSTOM_BLAT), L"Enter theater bottommost latitude. (+N/-S)");
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_CUSTOM_LLON), L"Enter theater leftmost longitude. (+E/-W)");
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_CUSTOM_RLON), L"Enter theater rightmost longitude. (+E/-W)");
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_CUSTOM_TIMEZONE), L"Enter theater timezone.");
			create_tooltip(hwnd, GetDlgItem(hwnd, IDC_F4WX_CUSTOM_THEATER_SIZE), L"Enter theater segment size. (default: 64)");

			break;
		}
		case WM_CLOSE: {
			on_close(false);
			return TRUE;
			break;
		}
		case WM_COMMAND: {
			switch (LOWORD(wparam))
			{
				case IDOK:
					on_close(true);
					return TRUE;

				case IDCANCEL:
					on_close(false);
					return TRUE;

				case IDC_F4WX_CUSTOM_TIMEZONE:
					if (HIWORD(wparam) == EN_KILLFOCUS) {
						wchar_t buf[MAX_EDIT_LENGTH + 1];
						GetWindowTextW(reinterpret_cast<HWND>(lparam), buf, static_cast<int>(std::size(buf)));
						set_text_control_float(m_hwnd, IDC_F4WX_CUSTOM_TIMEZONE, between(parse_wide_float(buf).value_or(0.0f), -12.0f, +14.0f), L"{:+2.2f}");
					}
					break;

				case IDC_F4WX_CUSTOM_TLAT:
				case IDC_F4WX_CUSTOM_BLAT:
					if (HIWORD(wparam) == EN_KILLFOCUS) {
						wchar_t buf[MAX_EDIT_LENGTH + 1];
						GetWindowTextW(reinterpret_cast<HWND>(lparam), buf, static_cast<int>(std::size(buf)));
						set_text_control_float(m_hwnd, LOWORD(wparam), between(static_cast<float>(round(parse_wide_float(buf).value_or(0.0f))), -90.0f, +90.0f), L"{:+02.2f}");
					}
					break;
				case IDC_F4WX_CUSTOM_LLON:
				case IDC_F4WX_CUSTOM_RLON:
					if (HIWORD(wparam) == EN_KILLFOCUS) {
						wchar_t buf[MAX_EDIT_LENGTH + 1];
						GetWindowTextW(reinterpret_cast<HWND>(lparam), buf, static_cast<int>(std::size(buf)));
						set_text_control_float(m_hwnd, LOWORD(wparam), between(static_cast<float>(round(parse_wide_float(buf).value_or(0.0f))), -180.0f, +180.0f), L"{:+03.2f}");
					}
					break;
				case IDC_F4WX_CUSTOM_THEATER_SIZE:
					if (HIWORD(wparam) == CBN_KILLFOCUS) {
						if (SendMessage(reinterpret_cast<HWND>(lparam), CB_GETCURSEL, static_cast<WPARAM>(0), static_cast<LPARAM>(0)) == CB_ERR)
							SendMessage(reinterpret_cast<HWND>(lparam), CB_SETCURSEL, static_cast<WPARAM>(0), static_cast<LPARAM>(0));
					}
					break;
			}
		}
	}
	return 0;
}

void f4wx_custom::on_close(bool ok)
{
	if (ok) {
		f4wx_theater_data td{};
		wchar_t buf[MAX_EDIT_LENGTH + 1];

		GetWindowTextW(GetDlgItem(m_hwnd, IDC_F4WX_CUSTOM_TLAT), buf, static_cast<int>(std::size(buf)));
		td.tlat = parse_wide_float(buf).value_or(0.0f);
		GetWindowTextW(GetDlgItem(m_hwnd, IDC_F4WX_CUSTOM_BLAT), buf, static_cast<int>(std::size(buf)));
		td.blat = parse_wide_float(buf).value_or(0.0f);
		GetWindowTextW(GetDlgItem(m_hwnd, IDC_F4WX_CUSTOM_LLON), buf, static_cast<int>(std::size(buf)));
		td.llon = parse_wide_float(buf).value_or(0.0f);
		GetWindowTextW(GetDlgItem(m_hwnd, IDC_F4WX_CUSTOM_RLON), buf, static_cast<int>(std::size(buf)));
		td.rlon = parse_wide_float(buf).value_or(0.0f);

		GetWindowTextW(GetDlgItem(m_hwnd, IDC_F4WX_CUSTOM_TIMEZONE), buf, static_cast<int>(std::size(buf)));
		td.timezone = static_cast<int>(parse_wide_float(buf).value_or(0.0f) * 60);  // stored in minutes

		td.size = static_cast<WORD>(64 * (1 + SendDlgItemMessage(m_hwnd, IDC_F4WX_CUSTOM_THEATER_SIZE, CB_GETCURSEL, static_cast<WPARAM>(0), static_cast<LPARAM>(0))));

		float tmp;
		if (td.tlat < td.blat) {
			tmp = td.tlat;
			td.tlat = td.blat;
			td.blat = tmp;
		}
		if (td.rlon < td.llon) {
			tmp = td.rlon;
			td.rlon = td.llon;
			td.llon = tmp;
		}

		td.name = std::format("custom-{:02.0f}{}{:03.0f}{}{:02.0f}{}{:03.0f}{}",
			td.tlat, td.tlat >= 0 ? 'N' : 'S',
			td.llon, td.llon >= 0 ? 'E' : 'W',
			td.blat, td.blat >= 0 ? 'N' : 'S',
			td.rlon, td.rlon >= 0 ? 'E' : 'W');
		m_result = td;
	}
	PostMessage(GetParent(m_hwnd), f4wx::WMU_CUSTOM_SELECTED, reinterpret_cast<WPARAM>(this), 0);
}


//////////////////////////////////////////////////////////// Config ////////////////////////////////////////////////////////////
int f4wx::config_load_theater(size_t n)
{
	if (n >= m_config.get_theater_count())
		return 1;
	if (m_config.get_theater_header(n, &m_current_theater) != 0)
		return 1;
	int res_id = m_config.get_theater_resource_id(n);
	if (res_id == 0)
		return 1;
	return (m_previewWindow.load_background(GetModuleHandle(nullptr), MAKEINTRESOURCE(res_id), RT_RCDATA) == 0) ? 0 : 1;
}

void f4wx::do_play(bool play)
{
	HICON hi = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(play ? IDI_ICON_PAUSE : IDI_ICON_PLAY));
	SendDlgItemMessage(m_hwnd, IDC_F4WX_MAIN_PLAY, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(hi));
	m_play_state = play;
	if (play)
		SetTimer(m_hwnd, static_cast<UINT_PTR>(f4wx_timers::F4WX_TIMER_PLAY), 500, nullptr);
	else
		KillTimer(m_hwnd, static_cast<UINT_PTR>(f4wx_timers::F4WX_TIMER_PLAY));
}

void f4wx::on_play_timer()
{
	if (get_fmap_count() > 0) {
		size_t pos = m_current_map_idx + 1;
		if (pos >= get_fmap_count())
			pos = 0;

		set_current_fmap(pos);

		SetTimer(m_hwnd, static_cast<UINT_PTR>(f4wx_timers::F4WX_TIMER_PLAY), 500, nullptr);
	}
	else {
		do_play(false);
	}
}




///////////////////////////////////////////////////////// Update notifier ///////////////////////////////////////////////////
#ifdef F4WX_ENABLE_UPDATE_CHECK

void f4wx::check_for_updates(bool in_background)
{
	m_update_thread = std::jthread(&f4wx::do_update_check, this, in_background);
}

void f4wx::do_update_check(bool in_background)
{
	if (!in_background) {
		open_progressbar();
		m_bar->set_marquee();
		m_bar->set_text("Checking for updates");
	}

	char version_buf[64];
	int r = f4wx_notifier_check(F4WX_RELEASES_API_LATEST_URL,
		F4WX_VERSION_MAJOR, F4WX_VERSION_MINOR, F4WX_VERSION_REVISION,
		version_buf);

	if (!in_background && is_progressbar_open())
		close_progressbar();

	if (r == F4WX_NOTIFIER_NEWER_AVAILABLE) {
		char* version_copy = _strdup(version_buf);
		if (version_copy)
			PostMessage(m_hwnd, WMU_UPDATE_AVAILABLE, 0, reinterpret_cast<LPARAM>(version_copy));
	}
}
#endif

/////////////////// Message Loop ///////////////////

int f4wx::run_ui()
{
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!IsDialogMessage(f4wx_hcurrentdialog, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return static_cast<int>(msg.wParam);

}


////////////////////////////////// Preview Window Hook Proc //////////////////////////////////


LRESULT CALLBACK f4wx::previewHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	f4wx *me = reinterpret_cast<f4wx*>(GetProp(hWnd, L"f4wx"));
	if(me != nullptr) {
		switch (msg) {
			case WM_RBUTTONDOWN: {
				POINT p;
				p.x = GET_X_LPARAM(lParam);
				p.y = GET_Y_LPARAM(lParam);
				me->on_preview_right_click(hWnd, p);
			}
			case WM_HOTKEY: {
				me->on_hotkey(wParam, lParam);
			}
		}

		return CallWindowProc(me->m_previewHookOrigProc, hWnd, msg,
			wParam, lParam);
	}
	return 0;
}

////////////////////////////////// Select Wind Dialog //////////////////////////////////

f4wx_selectwind::f4wx_selectwind(HWND hparent, size_t currentLevel)
{
	init_dialog(IDD_F4WX_WINDLEVELS, hparent);
	CheckRadioButton(m_hwnd, IDC_F4WX_WIND0, IDC_F4WX_WIND9, IDC_F4WX_WIND0 + static_cast<int>(currentLevel));
}

f4wx_selectwind::~f4wx_selectwind()
{
}

INT_PTR CALLBACK f4wx_selectwind::dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
		case WM_DESTROY: {
			return 0;
		}
		case WM_CLOSE: {
			PostMessageW(GetParent(hwnd), f4wx::WMU_WIND_SELECTED, WIND_LEVEL_NONE, reinterpret_cast<LPARAM>(this));
			return 0;
		}
		case WM_COMMAND: {
			switch (LOWORD(wparam))
			{
				case IDOK: {
					for (size_t i = 0; i < NUM_ALOFT_BREAKPOINTS; i++) {
						if (IsDlgButtonChecked(hwnd, IDC_F4WX_WIND0 + static_cast<int>(i))) {
							PostMessageW(GetParent(hwnd), f4wx::WMU_WIND_SELECTED, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(this));
							return 0;
						}
					}
					assert(false);
				}
				case IDCANCEL:
					PostMessageW(GetParent(hwnd), f4wx::WMU_WIND_SELECTED, WIND_LEVEL_NONE, reinterpret_cast<LPARAM>(this));
					return TRUE;
			}
		}
	}
	return 0;
}

INT_PTR f4wx::on_wind_selected(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	unregister_handler(WMU_WIND_SELECTED);

	if (static_cast<int>(wparam) != WIND_LEVEL_NONE)
		m_previewWindow.set_windLevel(static_cast<int>(wparam));

	int mode = m_previewWindow.get_mode();
	if ((static_cast<unsigned>(mode) & static_cast<unsigned>(preview_mode::PM_WIND)) == 0) {
		m_previewWindow.set_mode(static_cast<int>(static_cast<unsigned>(mode) | static_cast<unsigned>(preview_mode::PM_WIND)));
		CheckDlgButton(m_hwnd, IDC_F4WX_MAIN_PREVIEW_WINDS, TRUE);
	}
	if (m_wind_dialog.get() == reinterpret_cast<f4wx_selectwind*>(lparam))
		m_wind_dialog.reset();
	return 0;
}