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
#pragma once

#include <Windows.h>
#include <filesystem>
#include <gdiplus.h>
#include <memory>

#include "fmap.h"


class f4wx_preview
{
public:

	f4wx_preview();

	~f4wx_preview();

	void set_background(Gdiplus::Bitmap* background);

	void cleanup();

	void draw_clouds(const fmap& map);

	void draw_temperature(const fmap& map);

	void draw_pressure(const fmap& map);

	void draw_wind(const fmap& map, size_t level = 0);

	inline bool is_valid() const { return m_bitmap != nullptr; }
	
	inline UINT get_height() const { return m_bitmap->GetHeight(); }

	inline UINT get_width() const { return m_bitmap->GetWidth(); }

	inline Gdiplus::Status get_hbitmap(Gdiplus::Color color_background, HBITMAP *hreturn) { return m_bitmap->GetHBITMAP(color_background, hreturn); }

	inline Gdiplus::Status save(const std::filesystem::path& path, const CLSID* clsidEncoder, const Gdiplus::EncoderParameters *encoderParams) const { return m_bitmap->Save(path.c_str(), clsidEncoder, encoderParams); }

private:

	Gdiplus::Bitmap* m_background = nullptr;  // not owned
	std::unique_ptr<Gdiplus::Bitmap> m_bitmap;
	std::unique_ptr<Gdiplus::Graphics> m_graphics;

	ULONG_PTR m_gdiplus_token;
};