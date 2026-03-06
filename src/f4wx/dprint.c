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

#ifdef _DEBUG

#include <Windows.h>
#include <stdio.h>

#include "dprint.h"

/** Internal: formats with vsnprintf and sends to OutputDebugStringA. Thread-safe: per-thread buffer. */
static int vdprint(const char *fmt, va_list arg)
{
	static __declspec(thread) char buf[DPRINT_BUFLEN];
	int rv = vsnprintf(buf, ARRAYSIZE(buf), fmt, arg);
	if (rv > 0)
		OutputDebugStringA(buf);
	return rv;
}

/** Debug: printf to OutputDebugStringA. */
int dprint(const char *fmt, ...) 
{
	int rv;

	va_list arg;
	va_start(arg, fmt);
	rv = vdprint(fmt, arg);
	va_end(arg);
	return rv;
}

#endif