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

/**
 * Update notifier: fetches GitHub API latest release, compares to local version.
 * Does not download updates; only notifies and offers link to releases page.
 */

#include <span>
#include <string_view>

/** Result of version check */
enum f4wx_notifier_result {
	F4WX_NOTIFIER_ERROR = -1,   /**< Network, rate limit, or parse error; fail silently */
	F4WX_NOTIFIER_UP_TO_DATE = 0,
	F4WX_NOTIFIER_NEWER_AVAILABLE = 1
};

/**
 * Fetches GitHub API releases/latest from url (JSON), reads tag_name, compares to local major.minor.revision.
 * If remote version is newer, copies the version string (e.g. "2.0.1") into version_out and returns F4WX_NOTIFIER_NEWER_AVAILABLE.
 * On non-200 (e.g. 403 rate limit, 404 no releases), or parse error, returns F4WX_NOTIFIER_ERROR and does not notify.
 */
[[nodiscard]] int f4wx_notifier_check(std::string_view url,
	int local_major, int local_minor, int local_revision,
	std::span<char> version_out);