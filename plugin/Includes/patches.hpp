/*  Copyright (C) 2025 Pretendo Network contributors <pretendo.network>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 3.0 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <3ds.h>
#include "common.hpp"
#include "PatternManager.hpp"

// Title IDs for Animal Crossing: New Leaf
// List gathered from https://github.com/RedShyGuy/Vapecord-ACNL-Plugin/blob/3bb4023f149360eb25fe7337475eca92340d206c/Includes/Helpers/Address.hpp#L6
#define ACNL_TITLE_ID_USA    0x0004000000086300
#define ACNL_TITLE_ID_WA_USA 0x0004000000198E00
#define ACNL_TITLE_ID_EUR    0x0004000000086400
#define ACNL_TITLE_ID_WA_EUR 0x0004000000198F00
#define ACNL_TITLE_ID_JPN    0x0004000000086200
#define ACNL_TITLE_ID_WA_JPN 0x0004000000198D00
#define ACNL_TITLE_ID_KOR    0x0004000000086500
#define ACNL_TITLE_ID_WA_KOR 0x0004000000199000
#define ACNL_TITLE_ID_WL_EUR 0x00040000004C5700 // Welcome Luxury ROM hack

namespace CTRPluginFramework
{
    // Animal Crossing: New Leaf
    PatternStatus getACNLPatchesStatus();

    void initPatches(PatternManager& pm, u64 titleID);
    void enablePatches();
    void disablePatches();
    void finiPatches();

    // Miiverse session management
    void miiversePostCompletionHook();
}
