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

#include "f4wx_update_notifier.h"
#include "winhttp_get.h"

#include <string>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace {

constexpr size_t kMaxResponseSize = 64 * 1024;

/** Compare remote (r*) vs local (l*). Returns 1 if remote > local, 0 otherwise. */
static int is_newer(int rmaj, int rmin, int rrev, int lmaj, int lmin, int lrev)
{
	if (rmaj != lmaj) return rmaj > lmaj ? 1 : 0;
	if (rmin != lmin) return rmin > lmin ? 1 : 0;
	return rrev > lrev ? 1 : 0;
}

/** Parse "2.0.0" or "v2.0.0" into major, minor, revision. Returns true if at least major parsed. */
static bool parse_version(const char* str, int* major, int* minor, int* revision)
{
	*major = *minor = *revision = 0;
	const char* p = str;
	while (*p == ' ' || *p == '\t') p++;
	if (*p == 'v' || *p == 'V') p++;
	try {
		size_t pos = 0;
		std::string s(p);
		*major = std::stoi(s, &pos);
		p += pos;
		if (*p != '.') return true;
		p++;
		s = p;
		pos = 0;
		*minor = std::stoi(s, &pos);
		p += pos;
		if (*p != '.') return true;
		p++;
		s = p;
		pos = 0;
		*revision = std::stoi(s, &pos);
	} catch (...) {
		return true;
	}
	return true;
}

/**
 * Extract "tag_name" value from GitHub API JSON (minimal parse; no full JSON lib).
 * Matches the key "tag_name": so we do not match the substring inside another value.
 * Writes normalized version (no leading 'v') into out. Returns true if valid.
 */
static bool tag_name_from_json(const char* json, size_t json_len, std::span<char> out, int* major, int* minor, int* revision)
{
	if (json == nullptr || out.empty())
		return false;
	const char* end = json + json_len;
	/* Match key with colon to avoid matching "tag_name" inside a string value */
	const char key[] = "\"tag_name\":";
	const size_t key_len = sizeof(key) - 1;
	const char* p = json;
	while (p + key_len <= end) {
		if (memcmp(p, key, key_len) == 0)
			break;
		p = static_cast<const char*>(memchr(p + 1, '"', end - (p + 1)));
		if (p == nullptr)
			return false;
	}
	if (p + key_len > end)
		return false;
	p += key_len;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p >= end || *p != '"')
		return false;
	p++;
	const char* start = p;
	while (p < end && *p != '"' && *p != '\\') p++;
	if (p >= end)
		return false;
	size_t len = static_cast<size_t>(p - start);
	if (len == 0 || len >= out.size())
		return false;
	const char* src = start;
	if (*src == 'v' || *src == 'V') {
		src++;
		len--;
	}
	if (len == 0)
		return false;
	size_t copy_len = len;
	if (copy_len >= out.size())
		copy_len = out.size() - 1;
	memcpy(out.data(), src, copy_len);
	out[copy_len] = '\0';
	return parse_version(out.data(), major, minor, revision);
}

} // namespace

int f4wx_notifier_check(std::string_view url,
	int local_major, int local_minor, int local_revision,
	std::span<char> version_out)
{
	if (url.empty() || version_out.empty())
		return F4WX_NOTIFIER_ERROR;

	winhttp_get::Client client;
	if (!client.SetUrl(url))
		return F4WX_NOTIFIER_ERROR;
	client.SetTimeouts(5000, 5000, 5000, 10000);

	if (!client.SendRequest())
		return F4WX_NOTIFIER_ERROR;

	int status = client.GetResponseStatusCode();
	if (status != 200)
		return F4WX_NOTIFIER_ERROR; /* 403 rate limit, 404 no releases, etc. → fail silently */

	const size_t content_len = client.GetRawResponseReceivedContentLength();
	if (content_len == 0 || content_len > kMaxResponseSize)
		return F4WX_NOTIFIER_ERROR;

	const std::byte* content = client.GetRawResponseContent();
	if (content == nullptr)
		return F4WX_NOTIFIER_ERROR;

	std::vector<char> buf(content_len + 1);
	memcpy(buf.data(), content, content_len);
	buf[content_len] = '\0';

	char normalized[64];
	int rmaj, rmin, rrev;
	if (!tag_name_from_json(buf.data(), content_len, normalized, &rmaj, &rmin, &rrev))
		return F4WX_NOTIFIER_ERROR;

	if (rmaj == 0 && rmin == 0 && rrev == 0 && (normalized[0] != '0' || normalized[1] != '\0'))
		return F4WX_NOTIFIER_ERROR;

	if (is_newer(rmaj, rmin, rrev, local_major, local_minor, local_revision) == 0)
		return F4WX_NOTIFIER_UP_TO_DATE;

	size_t copy_len = strlen(normalized);
	if (copy_len >= version_out.size())
		copy_len = version_out.size() - 1;
	memcpy(version_out.data(), normalized, copy_len);
	version_out[copy_len] = '\0';

	return F4WX_NOTIFIER_NEWER_AVAILABLE;
}
