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

#pragma once

/**
 * Shared domain types for F4Wx (grid indices, etc.).
 */

/** Row (y) and column (x) index of a cell in the weather grid. Row-major index = y * sizeX + x. */
struct cell_index {
	unsigned int y;
	unsigned int x;
};
