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

/**
 * F4Wx main UI: dialog-based app for GRIB download, conversion, and fmap
 * export; uses f4wx_dialog base, grib_converter, noaa_downloader, preview.
 */

#include "config.h"

#include <Windows.h>
#include <gdiplus.h>
#include <memory>
#include <vector>
#include <string_view>
#include <optional>
#include <cassert>
#include <Shobjidl.h>

#include <filesystem>
#include <thread>
#include <stop_token>
#include <condition_variable>
#include <queue>
#include <functional>

#include "noaa_downloader.h"
#include "grib_converter.h"
#include "utils.h"
#include "f4wx_config.h"
#include "f4wx_preview_window.h"
#include "f4wx_preview.h"

inline constexpr unsigned int DOWNLOAD_MAX_UNLIMITED = UINT_MAX;
inline constexpr int WIND_LEVEL_NONE = -1;

inline constexpr int MAX_EDIT_LENGTH = 8;

/** Default weather thresholds for UI (initialize_controls). */
inline constexpr float DEFAULT_CLOUD_COVER_FAIR = 12.5f;
inline constexpr float DEFAULT_CLOUD_COVER_POOR = 62.5f;
inline constexpr float DEFAULT_CLOUD_COVER_INCLEMENT = 62.5f;
inline constexpr float DEFAULT_PRECIPIT_FAIR = 0.0f;
inline constexpr float DEFAULT_PRECIPIT_POOR = 0.0f;
inline constexpr float DEFAULT_PRECIPIT_INCLEMENT = 2.0f;

/** Subfolder name for saved preview images (save_previews). */
inline constexpr const char F4WX_PREVIEW_SUBDIR[] = "Weather Maps Previews";

extern HWND f4wx_hcurrentdialog;

class f4wx_selectgfs;
class f4wx_selectwind;
class f4wx_custom;
class f4wx_about;

/** Base for modal/modeless dialogs: window proc, handler list, init_dialog. */
class f4wx_dialog
{
public:
	virtual ~f4wx_dialog();

protected:
	HWND m_hwnd = nullptr;

	static INT_PTR CALLBACK static_dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	virtual INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	int init_dialog(int id, HWND hparent = nullptr, bool stealfocus = true);

	using message_handler_fn = std::function<INT_PTR(f4wx_dialog*, HWND, UINT, WPARAM, LPARAM)>;
	struct handler_info {
		UINT msg;
		message_handler_fn handler;
	};
	using handler_list = std::vector<handler_info>;
	handler_list m_handlers;

	bool register_handler(UINT msg, message_handler_fn handler);
	void unregister_handler(UINT msg);
};


class f4wx_progressbar
	: public f4wx_dialog
{
public:

	f4wx_progressbar(HWND hparent, ITaskbarList3 *pTL, ULONGLONG total);
	virtual ~f4wx_progressbar() override;

	void set_position(size_t pos);
	void set_text(const char* str);
	inline void set_text(const std::string& str) { set_text(str.c_str()); }
	inline void set_text(std::string_view sv) { set_text(std::string(sv).c_str()); }
	void set_marquee(bool enable = true, unsigned long interval = 10);

	inline HWND get_hwnd() { return m_hwnd; }

	inline void set_total(ULONGLONG total);

private:

	static void CALLBACK text_animation_timer(HWND hwnd, UINT umsg, UINT_PTR id, DWORD dwtime);


	enum class timers : UINT_PTR {
		PBTI_TEXTANIM
	};

	ITaskbarList3 *m_pTL;
	ULONGLONG m_total;
};


/** Main application window: GRIB download, theater/config, conversion, preview, save. */
class f4wx :
	public f4wx_dialog
{
public:
	enum f4wx_window_messages {
		WMU_GFSLIST_DOWNLOADED = (WM_USER + 0x01),	// wp: error code
		WMU_GFS_SELECTED,							// wp: ptr to f4wx_selectgfs
		WMU_GFSRUN_INDEX_DOWNLOADED,				// wp: error code -- lp: idx
		WMU_GFSRUN_FILES_DOWNLOADED,				// wp: error code 
		WMU_GFSRUN_DECODED,							// wp: error code
		WMU_CONVERSION_END,							// wp: error code
		WMU_CUSTOM_SELECTED,						// wp: param to fwx_custom -- lp: theater_data

		WMU_WIND_SELECTED,
		WMU_ABOUT_CLOSED,

		WMU_OPENPB_THREAD,
		WMU_CLOSEPB_THREAD,
		WMU_UPDATE_AVAILABLE		// lp: heap-allocated version string (caller frees)
	};


	f4wx();
	virtual ~f4wx() override;

	[[nodiscard]] int update_gfs_runs();

	void clear_gribfiles();

	inline size_t get_gfsrun_count() const { return m_gfsruns.size(); }
	inline const noaa_gfsrun_filename& get_gfsrun_filename(size_t n) const { assert(n < get_gfsrun_count()); return m_gfsruns[n]; }

	inline size_t get_gfsruns_forecastfilename_count() const { return m_gfsrun_forecastfilenames.size(); }
	inline const noaa_gfsrun_forecastfilename& get_get_gfsruns_forecastfilename(size_t n) const { assert(n < get_gfsruns_forecastfilename_count()); return m_gfsrun_forecastfilenames[n]; }

	inline size_t get_gribfile_count() const { return m_gribfiles.size(); }
	inline size_t get_gribfile_data_size(size_t pos) const { assert(pos < get_gribfile_count()); return m_gribfiles[pos]->buffer->get_size(); }
	inline const unsigned char* get_gribfile_data(size_t pos) const { assert(pos < get_gribfile_count()); return m_gribfiles[pos]->buffer->get_data(); }
	
	inline HWND get_hwnd() const { return m_hwnd; }

	[[nodiscard]] int convert_grib_files(bool updateCount = false);
	inline size_t get_fmap_count() const { return m_fmapcount; }
	[[nodiscard]] inline int get_fmap(size_t n, fmap& out) { assert(n < get_fmap_count()); return m_converter.convert_single(m_converter_options, out, static_cast<unsigned long>(n * m_converter_options.interval_minutes)) == 0 ? 0 : -1; }
	inline const fmap* get_current_fmap() const { return m_current_map.get(); }

	[[nodiscard]] int run_ui();

private:

	noaa_gfsrun_filename_list m_gfsruns;

	noaa_gfsrun_forecastfilename_list m_gfsrun_forecastfilenames;

	/** GRIB buffer that takes ownership of data by copying (caller may free buf after). */
	class loadfile_grib_buffer : public noaa_grib_buffer
	{
	public:
		loadfile_grib_buffer(unsigned char *buf, size_t size) {
			m_buffer.assign(buf, buf + size);
		}
	};
	struct grib_file {
		explicit grib_file(const noaa_gfsrun_forecastfilename& file) : filename(file.get_filename()), buffer(std::make_unique<noaa_grib_buffer>()) {}
		explicit grib_file(const std::string& file, unsigned char *buf, size_t bufsize) : filename(file), buffer(std::make_unique<loadfile_grib_buffer>(buf, bufsize)) {}

		std::unique_ptr<noaa_grib_buffer> buffer;
		std::string		filename;
	};
	
	using grib_filelist = std::vector<std::unique_ptr<grib_file>>;

	grib_filelist	m_gribfiles;
	grib_converter	m_converter;
	grib_converter_options	m_converter_options;

	size_t	m_current_map_idx = 0;

	// Main dialog ui
	virtual INT_PTR CALLBACK dialog_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

	enum class ui_weather_source {
		none,
		noaa,
		file
	};
	ui_weather_source m_ui_source = ui_weather_source::none;

	enum class ui_save_mode {
		none,
		single,
		sequence
	};
	ui_save_mode m_ui_save = ui_save_mode::none;

	unsigned long m_ui_initial_time_day = 1;
	unsigned long m_ui_initial_time_hour = 9;
	unsigned long m_ui_initial_time_minute = 0;

	bool m_ui_sync_with_real = true;
	bool m_ui_start_from_current = false;
	bool m_ui_save_previews = false;

	inline int messagebox(const char *title, const char *msg, int flags = 0) {
		std::wstring wtitle = to_wide(title);
		std::wstring wmsg = to_wide(msg);
		return MessageBoxW(f4wx_hcurrentdialog, wmsg.c_str(), wtitle.c_str(), flags);
	}
	inline int messagebox(const char *msg, int flags = 0) {
		return messagebox("F4Wx", msg, flags);
	}
	inline void errorbox(const char *msg, int errcode = 0) {
		messagebox("F4Wx Error", msg, MB_ICONERROR);
	}
	inline void errorbox(const std::string& msg, int errcode = 0) {
		errorbox(msg.c_str(), errcode);
	}
	inline void errorbox(std::string_view msg, int errcode = 0) {
		messagebox("F4Wx Error", std::string(msg).c_str(), MB_ICONERROR);
	}
	inline void infobox(const char *msg) {
		messagebox(msg, MB_ICONINFORMATION);
	}
	inline void infobox(const std::string& msg) {
		infobox(msg.c_str());
	}
	inline void infobox(std::string_view msg) {
		infobox(std::string(msg).c_str());
	}
	[[nodiscard]] inline int questionbox(const char *msg) {
		return messagebox(msg, MB_YESNO | MB_ICONQUESTION);
	}

	void initialize_controls();
	void set_save_mode(ui_save_mode mode);

	void show_warning(int id, bool show);

	INT_PTR on_command(WPARAM wparam, LPARAM lparam);
	void on_save();
	void on_load_file();
	void on_download();
	void on_save_grib_files();
	void on_theater_change(int idx);
	void on_custom_theater();
	INT_PTR on_hotkey(WPARAM wparam, LPARAM lparam);

	f4wx_theater_data	m_current_theater;

	std::unique_ptr<f4wx_progressbar> m_bar;
	std::unique_ptr<f4wx_selectgfs> m_gfs_dialog;
	std::unique_ptr<f4wx_selectwind> m_wind_dialog;
	std::unique_ptr<f4wx_custom> m_custom_dialog;
	std::unique_ptr<f4wx_about> m_about_dialog;

	inline void open_progressbar(ULONGLONG total = 100) { assert(!m_bar); SendMessage(m_hwnd, WMU_OPENPB_THREAD, static_cast<WPARAM>(total), 0);  }
	inline void close_progressbar() { assert(m_bar); SendMessage(m_hwnd, WMU_CLOSEPB_THREAD, 0, 0);  }
	inline bool is_progressbar_open() { return m_bar != nullptr; }

	std::queue<std::function<void()>> m_tasks;
	std::mutex m_queue_mutex;
	std::condition_variable_any m_queue_cv;
	std::jthread m_worker;
	/** Set by worker before running a task, cleared after; allows long-running tasks to check stop. */
	std::stop_token m_worker_stop_token;

	bool queue_task(std::function<void()> task);

	void threaded_update_gfs_list();
	INT_PTR on_gfslist_downloaded(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	INT_PTR on_gfsrun_selected(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	void ui_download_gfs_run(size_t idx);
	void threaded_get_gfsrun_index(size_t idx);
	INT_PTR on_gfsrun_index_downloaded(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	void threaded_download_gfsrun_files(size_t idx);
	INT_PTR on_gfsrun_files_downloaded(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	INT_PTR on_conversion_finished(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	[[nodiscard]] int ui_decode_grib_files();
	[[nodiscard]] int ui_load_grib_file(const std::filesystem::path& dir, const std::filesystem::path& filename);

	void ui_save_sequence();
	void ui_save_single();

	void ui_set_cloud_fair(float val);
	void ui_set_cloud_poor(float val);
	void ui_set_cloud_inclement(float val);
	void ui_set_preipit_fair(float val);
	void ui_set_preipit_poor(float val);
	void ui_set_preipit_inclement(float val);
	void ui_set_bms_day(int val);
	void ui_set_bms_hour(int val);
	void ui_set_bms_minute(int val);
	void ui_set_fmap_interval(int val);
	void ui_set_fmap_maxforecast(int val);

	void ui_set_sync_timezone(bool val);
	void ui_set_start_current(bool val);

	void ui_set_save_previews(bool val);

	void ui_set_slider(size_t pos);

	void clear_fmaps();

	noaa_gfsrun_filename m_last_gfsrun{""};

	enum class f4wx_hotkeys : int {
		HK_SAVE_GRIB_FILES
	};

	void ui_slider_set_range(size_t range);
	INT_PTR on_slider_scroll(WPARAM wparam, LPARAM lparam);

	// Preview map
	INT_PTR on_paint(HWND hwnd);

	ULONG_PTR m_gdiplus_token;

	INT_PTR on_preview_right_click(HWND hwnd, POINT& pt);

	f4wx_preview_window m_previewWindow;
	static LRESULT CALLBACK previewHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	WNDPROC m_previewHookOrigProc;

	void ui_save_preview();
	void ui_load_preview_background();

	void get_preview_rect(RECT& r);

	f4wx_config m_config;
	[[nodiscard]] int config_load_theater(size_t n);


	bool m_play_state = false;
	void do_play(bool play);
	void on_play_timer();

	enum class f4wx_timers : UINT_PTR {
		F4WX_TIMER_PLAY
	};

#ifdef F4WX_ENABLE_UPDATE_CHECK
	std::jthread m_update_thread;
	void check_for_updates(bool in_background);
	void do_update_check(bool in_background);
#endif

	unsigned m_download_interval = 3;
	unsigned m_download_max = DOWNLOAD_MAX_UNLIMITED;

	void get_bms_time(int minuteoffset, int& day, int& hr, int &min);
	void get_real_time(int minuteoffset, int& year, int& mon, int& day, int& hr, int &min);
	void ui_update_status_times();
	void set_current_fmap(size_t pos, bool override = false);

	size_t get_sync_min_pos();
	void correct_for_timezone_sync(size_t &pos);

	void threaded_save_sequence(std::filesystem::path path);
	[[nodiscard]] int save_sequence(const std::filesystem::path& path);

	[[nodiscard]] int save_previews(const std::filesystem::path& path);

	bool using_download_weather() { return m_last_gfsrun.is_valid(); }

	INT_PTR on_custom_closed(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	INT_PTR on_about_closed(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	enum class state : unsigned long long {
		S_IDLE,
		S_DLGFSIDX,
		S_DLGFSGRB,
		S_DECODE,
		S_CONVERT,
		S_PROCESS,
		S_SAVE,
		S_UPDATE
	};
	unsigned long long m_state = static_cast<unsigned long long>(state::S_IDLE);
	inline bool isidle() { return m_state == static_cast<unsigned long long>(state::S_IDLE); }
	inline unsigned long long getstate() { return m_state;  }
	inline void setstate(state s) { assert(s != state::S_IDLE || m_state != static_cast<unsigned long long>(state::S_IDLE)); m_state = static_cast<unsigned long long>(s); }
	inline void setstate_wait(state s, DWORD ms = 100) { while (InterlockedCompareExchange((unsigned long long*)&m_state, static_cast<unsigned long long>(s), static_cast<unsigned long long>(state::S_IDLE)) != static_cast<unsigned long long>(state::S_IDLE)) Sleep(ms); }
	inline bool isupdating() { return m_state == static_cast<unsigned long long>(state::S_UPDATE); }

	size_t m_fmapcount = 0;
	std::unique_ptr<fmap> m_current_map;

	INT_PTR on_wind_selected(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	struct taskbar_list_holder;
	std::unique_ptr<taskbar_list_holder> m_spTaskbarList;
};


class f4wx_selectgfs
	: public f4wx_dialog
{
public:
	f4wx_selectgfs(HWND hparent, const noaa_gfsrun_filename_list& gfsruns, unsigned download_interval, unsigned download_max);
	virtual ~f4wx_selectgfs() override;

	[[nodiscard]] std::optional<int> get_pos() const { return m_pos == LB_ERR ? std::nullopt : std::optional<int>(m_pos); }
	inline unsigned get_download_interval() { return m_download_interval;  }
	inline unsigned get_download_max() { return m_download_max;  }

private:

	virtual INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) override;

	void notify_selection(int selection);

	int m_pos = LB_ERR;
	unsigned m_download_interval = 1;
	unsigned m_download_max = DOWNLOAD_MAX_UNLIMITED;
};


class f4wx_about
	: public f4wx_dialog
{
public:

	explicit f4wx_about(HWND hparent);
	virtual ~f4wx_about() override;

private:
	virtual INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) override;
};

class f4wx_custom
	: public f4wx_dialog
{
public:
	f4wx_custom(HWND hparent, f4wx_theater_data& td);
	virtual ~f4wx_custom() override;

	/** Result when user clicked OK; empty when cancelled. */
	const std::optional<f4wx_theater_data>& get_result() const { return m_result; }

private:
	virtual INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) override;

	void on_close(bool ok);

	std::optional<f4wx_theater_data> m_result;
};

class f4wx_selectwind
	: public f4wx_dialog
{
public:
	f4wx_selectwind(HWND hparent, size_t currentLevel);
	virtual ~f4wx_selectwind() override;

private:
	virtual INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) override;
};
