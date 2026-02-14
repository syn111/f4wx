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

#include "winhttp_get.h"
#include "utils.h"

#include <winhttp.h>

#include <cstring>
#include <iterator>
#include <vector>
#include <utility>

#pragma comment(lib, "winhttp.lib")

namespace winhttp_get {

namespace {

constexpr wchar_t USER_AGENT[] = L"F4Wx/1.0";
constexpr size_t READ_CHUNK = 32 * 1024;

} // namespace

Client::Client() = default;

Client::~Client() {
	if (m_session) {
		WinHttpCloseHandle(m_session);
		m_session = nullptr;
	}
}

Client::Client(Client&& other) noexcept {
	*this = std::move(other);
}

Client& Client::operator=(Client&& other) noexcept {
	if (this != &other) {
		if (m_session) WinHttpCloseHandle(m_session);
		m_session = other.m_session;
		other.m_session = nullptr;
		m_url = std::move(other.m_url);
		m_resolveTimeout = other.m_resolveTimeout;
		m_connectTimeout = other.m_connectTimeout;
		m_sendTimeout = other.m_sendTimeout;
		m_receiveTimeout = other.m_receiveTimeout;
		m_requireValidSsl = other.m_requireValidSsl;
		m_responseRaw = std::move(other.m_responseRaw);
		m_responseBodyUtf8 = std::move(other.m_responseBodyUtf8);
		m_statusCode = other.m_statusCode;
		m_lastError = other.m_lastError;
	}
	return *this;
}

bool Client::utf8_to_wide(std::string_view utf8, std::wstring& out) {
	if (utf8.empty()) {
		out.clear();
		return true;
	}
	const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (len <= 0)
		return false;
	out.resize(static_cast<size_t>(len));
	return MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len) == len;
}

bool Client::SetUrl(std::string_view url_utf8) {
	return utf8_to_wide(url_utf8, m_url);
}

void Client::SetTimeouts(unsigned resolve_ms, unsigned connect_ms, unsigned send_ms, unsigned receive_ms) {
	m_resolveTimeout = resolve_ms;
	m_connectTimeout = connect_ms;
	m_sendTimeout = send_ms;
	m_receiveTimeout = receive_ms;
}

void Client::SetRequireValidSslCertificates(bool require) {
	m_requireValidSsl = require;
}

void Client::clear_response() {
	m_responseRaw.clear();
	m_responseBodyUtf8.clear();
	m_statusCode = 0;
}

bool Client::SendRequest() {
	clear_response();
	m_lastError = 0;

	if (m_url.empty()) {
		m_lastError = ERROR_PATH_NOT_FOUND;
		return false;
	}

	if (!m_session) {
		m_session = WinHttpOpen(USER_AGENT,
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!m_session) {
			m_lastError = GetLastError();
			return false;
		}
	}

	if (!WinHttpSetTimeouts(m_session,
			m_resolveTimeout,
			m_connectTimeout,
			m_sendTimeout,
			m_receiveTimeout)) {
		m_lastError = GetLastError();
		return false;
	}

	wchar_t hostName[256] = {};
	wchar_t urlPath[2048] = {};
	URL_COMPONENTS urlComp = {};
	urlComp.dwStructSize = sizeof(urlComp);
	urlComp.lpszHostName = hostName;
	urlComp.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
	urlComp.lpszUrlPath = urlPath;
	urlComp.dwUrlPathLength = static_cast<DWORD>(std::size(urlPath));
	urlComp.dwSchemeLength = 1;

	if (!WinHttpCrackUrl(m_url.c_str(), static_cast<DWORD>(m_url.size()), 0, &urlComp)) {
		m_lastError = GetLastError();
		return false;
	}

	HINTERNET hConnect = WinHttpConnect(m_session, hostName, urlComp.nPort, 0);
	if (!hConnect) {
		m_lastError = GetLastError();
		return false;
	}

	const DWORD openFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect,
		L"GET",
		urlPath,
		nullptr,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		openFlags);
	if (!hRequest) {
		m_lastError = GetLastError();
		WinHttpCloseHandle(hConnect);
		return false;
	}

	if (!m_requireValidSsl && urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
		DWORD flags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID
			| SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
			| SECURITY_FLAG_IGNORE_UNKNOWN_CA;
		WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
	}

	bool ok = false;
	if (WinHttpSendRequest(hRequest,
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0,
			WINHTTP_NO_REQUEST_DATA,
			0,
			0,
			0)) {
		if (WinHttpReceiveResponse(hRequest, nullptr)) {
			wchar_t statusBuf[16] = {};
			DWORD statusLen = static_cast<DWORD>(std::size(statusBuf)) * sizeof(wchar_t);
			if (WinHttpQueryHeaders(hRequest,
					WINHTTP_QUERY_STATUS_CODE,
					WINHTTP_HEADER_NAME_BY_INDEX,
					statusBuf,
					&statusLen,
					WINHTTP_NO_HEADER_INDEX)) {
				m_statusCode = parse_wide_int(statusBuf).value_or(0);
			}

			std::vector<std::byte> body;
			DWORD avail = 0;
			bool readError = false;
			do {
				if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
					break;
				const size_t prev = body.size();
				body.resize(prev + static_cast<size_t>(avail));
				DWORD read = 0;
				if (!WinHttpReadData(hRequest, body.data() + prev, avail, &read)) {
					m_lastError = GetLastError();
					body.resize(prev);
					readError = true;
					break;
				}
				if (read < avail)
					body.resize(prev + static_cast<size_t>(read));
			} while (avail > 0);

			if (!readError) {
				m_responseRaw = std::move(body);
				m_responseBodyUtf8.assign(
					reinterpret_cast<const char*>(m_responseRaw.data()),
					m_responseRaw.size());
				ok = true;
			}
		} else {
			m_lastError = GetLastError();
		}
	} else {
		m_lastError = GetLastError();
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	return ok;
}

int Client::GetResponseStatusCode() const {
	return m_statusCode;
}

std::string Client::GetResponseBodyUtf8() const {
	return m_responseBodyUtf8;
}

const std::byte* Client::GetRawResponseContent() const {
	return m_responseRaw.empty() ? nullptr : m_responseRaw.data();
}

size_t Client::GetRawResponseReceivedContentLength() const {
	return m_responseRaw.size();
}

} // namespace winhttp_get
