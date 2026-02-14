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



#include <vector>
#include <string>
#include <regex>
#include <format>
#include <fstream>
#include <cstring>
#include <cerrno>

#include "f4wx.h"
#include "noaa_downloader.h"
#include "winhttp_get.h"

namespace {

/** Href pattern: href="x" or href='x' or href=x.
 * Parsing depends on NOAA NOMADS directory listing HTML format; changes to that format may break link extraction. */
const std::regex& link_regex()
{
	static const std::regex re(R"(<a\s+(?:[^>]*?\s+)?href\s*=\s*(?:["']([^"']*)["']|([^\s>]+)))", std::regex::icase);
	return re;
}

/** Extract link targets from HTML; optionally only those starting with prefix. */
std::vector<std::string> extract_links(std::string_view content, std::string_view prefix = {})
{
	std::vector<std::string> out;
	std::string s(content);
	auto begin = std::sregex_iterator(s.begin(), s.end(), link_regex());
	auto end = std::sregex_iterator();
	for (auto i = begin; i != end; ++i) {
		const std::smatch& m = *i;
		std::string link = m[1].length() ? m[1].str() : m[2].str();
		if (prefix.empty() || link.starts_with(prefix))
			out.push_back(std::move(link));
	}
	return out;
}

} // namespace

int noaa_grib_buffer::save(const std::filesystem::path& path) const
{
	std::ofstream out(path, std::ios::binary);
	if (!out)
		return static_cast<int>(errno);
	if (!m_buffer.empty() && !out.write(reinterpret_cast<const char*>(m_buffer.data()), static_cast<std::streamsize>(m_buffer.size())))
		return static_cast<int>(EIO);
	return 0;
}

int noaa_downloader::get_gfs_runs(noaa_gfsrun_filename_list& result)
{
	result.clear();
	if (!m_client->SetUrl(NOMADS_GFS025_STORE_URI) || !m_client->SendRequest())
		return static_cast<int>(m_client->GetLastError());

	std::string response = m_client->GetResponseBodyUtf8();
	for (const std::string& link : extract_links(response, "gfs.")) {
		if (link.size() == NOMADS_GFS_RUN_FILENAME_SIZE + 1) {
			noaa_gfsrun_filename fn(link.substr(0, link.size() - 1));
			if (fn.is_valid())
				result.push_back(fn);
		} else if (link.size() == NOMADS_GFS_RUN_FILENAME_NEWFORMAT_SIZE_FIRST + 1) {
			(void)get_gfs_new_format_subruns(result, std::string_view(link).substr(0, link.size() - 1));
		}
	}
	return !result.empty() ? 0 : static_cast<int>(ERROR_INVALID_DATA);
}

int noaa_downloader::get_gfs_run_forecasts(const noaa_gfsrun_filename& gfsrun, noaa_gfsrun_forecastfilename_list& result)
{
	std::string url = std::string(NOMADS_GFS025_STORE_URI) + gfsrun.get_filename();
	if (url.back() != '/')
		url += '/';
	url += "atmos/";

	if (!m_client->SetUrl(url) || !m_client->SendRequest())
		return static_cast<int>(m_client->GetLastError());

	std::string filter = "gfs.t" + gfsrun.get_hour() + "z.pgrb2.0p25.f";
	for (const std::string& link : extract_links(m_client->GetResponseBodyUtf8(), filter)) {
		noaa_gfsrun_forecastfilename fn(link);
		if (fn.is_valid())
			result.push_back(fn);
	}
	return !result.empty() ? 0 : static_cast<int>(ERROR_INVALID_DATA);
}

int noaa_downloader::get_gfs_grib(const noaa_gfsrun_filename& gfsrun, const noaa_gfsrun_forecastfilename& forecasttime, int leftlon, int rightlon, int toplat, int bottomlat, noaa_grib_buffer& buffer)
{
	std::string url = std::format(NOMADS_GFS025_FILTER_URI_FMT,
		forecasttime.get_filename(),
		leftlon, rightlon, toplat, bottomlat,
		gfsrun.get_encoded_filename());
	DPRINT("GET: {}\n", url);

	if (!m_client->SetUrl(url) || !m_client->SendRequest())
		return static_cast<int>(m_client->GetLastError());
	if (m_client->GetResponseStatusCode() != 200)
		return static_cast<int>(ERROR_INVALID_DATA);

	buffer.allocate(m_client->GetRawResponseReceivedContentLength());
	unsigned char* p = buffer.data();
	const std::byte* raw = m_client->GetRawResponseContent();
	if (raw)
		std::memcpy(p, raw, buffer.get_size());
	return buffer.get_size() > 0 ? 0 : static_cast<int>(ERROR_INVALID_DATA);
}


noaa_downloader::noaa_downloader()
	: m_client(std::make_unique<winhttp_get::Client>())
{
	m_client->SetRequireValidSslCertificates(true);
}


// New format stuff

noaa_gfsrun_filename::noaa_gfsrun_filename(std::string_view strOld, std::string_view strNew)
{
	m_str = std::string(strOld) + std::string(strNew);
	m_isNew = true;
}

int noaa_downloader::get_gfs_new_format_subruns(noaa_gfsrun_filename_list& result, std::string_view firstPart)
{
	std::string url = std::string(NOMADS_GFS025_STORE_URI) + std::string(firstPart) + '/';
	if (!m_client->SetUrl(url) || !m_client->SendRequest())
		return static_cast<int>(m_client->GetLastError());

	for (const std::string& link : extract_links(m_client->GetResponseBodyUtf8())) {
		if (link.size() == NOMADS_GFS_RUN_FILENAME_NEWFORMAT_SIZE_SECOND + 1) {
			noaa_gfsrun_filename fn(firstPart, std::string_view(link).substr(0, link.size() - 1));
			if (fn.is_valid())
				result.push_back(fn);
		}
	}
	return !result.empty() ? 0 : static_cast<int>(ERROR_INVALID_DATA);
}
