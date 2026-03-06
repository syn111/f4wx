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
* See the License for the specific license governing permissions and
* limitations under the License.
*
*/

#pragma once

/**
 * Minimal C++20 WinHTTP sync GET wrapper. No ATL, no codecvt.
 * Uses WinHTTP only (winhttp.lib); system proxy and Windows SSL.
 */

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>
#include <winhttp.h>

namespace winhttp_get {

class Client {
public:
	Client();
	~Client();

	Client(const Client&) = delete;
	Client& operator=(const Client&) = delete;

	Client(Client&& other) noexcept;
	Client& operator=(Client&& other) noexcept;

	/** Set URL (UTF-8). Returns false if conversion fails. */
	[[nodiscard]] bool SetUrl(std::string_view url_utf8);

	/** Timeouts in milliseconds. 0 = use default. */
	void SetTimeouts(unsigned resolve_ms, unsigned connect_ms, unsigned send_ms, unsigned receive_ms);

	void SetRequireValidSslCertificates(bool require);

	/** Perform sync GET. Returns true on success (check status code for 200 etc.). */
	[[nodiscard]] bool SendRequest();

	/** HTTP status code (e.g. 200) or 0 if not available. */
	[[nodiscard]] int GetResponseStatusCode() const;

	/** Response body as UTF-8 string (for text/HTML). Empty if not requested or error. */
	[[nodiscard]] std::string GetResponseBodyUtf8() const;

	/** Raw response body. Valid until next SendRequest() or destruction. */
	[[nodiscard]] const std::byte* GetRawResponseContent() const;
	/** Number of bytes in raw response. */
	[[nodiscard]] size_t GetRawResponseReceivedContentLength() const;

	[[nodiscard]] DWORD GetLastError() const { return m_lastError; }

private:
	std::wstring m_url;
	unsigned m_resolveTimeout = 0;
	unsigned m_connectTimeout = 60000;
	unsigned m_sendTimeout = 30000;
	unsigned m_receiveTimeout = 30000;
	bool m_requireValidSsl = true;

	HINTERNET m_session = nullptr;
	std::vector<std::byte> m_responseRaw;
	std::string m_responseBodyUtf8;
	int m_statusCode = 0;
	DWORD m_lastError = 0;

	bool utf8_to_wide(std::string_view utf8, std::wstring& out);
	void clear_response();
};

} // namespace winhttp_get
