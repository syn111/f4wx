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

#include "version.h"

// Enable/Disable the update notifier (checks GitHub API for latest release; notifies if newer release available.)
// In _DEBUG the update check is not invoked (see call site in f4wx.cpp); remove the #ifndef _DEBUG there to re-enable for debugging.
#define F4WX_ENABLE_UPDATE_CHECK

// GitHub API: latest release (one request, one release; 403/404 → fail silently, user gets notification later).
inline constexpr char F4WX_RELEASES_API_LATEST_URL[] = "https://api.github.com/repos/syn111/f4wx/releases/latest";

// URL of the latest release
inline constexpr char F4WX_RELEASES_URL[] = "https://github.com/syn111/f4wx/releases/latest";

// Print additional debug information on preview window
#define FMAP_DEBUG

// Preview window size
#define PREVIEW_HEIGHT 350
#define PREVIEW_WIDTH 350


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dprint.h"

#if !defined(_DEBUG) && (!defined(_TEST) || defined(_TEST_PUBLIC))
#ifndef F4WX_ENABLE_UPDATE_CHECK
#error "F4WX_ENABLE_UPDATE_CHECK must be defined for RELEASE build!!"
#endif
#if defined(FMAP_DEBUG) && !defined(_TEST_PUBLIC)
#pragma message("FMAP_DEBUG is defined for release build!! Disabling!")
#undef FMAP_DEBUG
#endif
#endif