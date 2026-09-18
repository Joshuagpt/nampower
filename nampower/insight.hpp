//
// Line-of-sight / camera visibility helpers ported from UnitXP_SP3 ideas.
// Exposes: UnitInSight(unitA, unitB)  -- true if clear LOS (and frustum when unitA is "camera")
//

#pragma once

#include <Windows.h>
#include "main.hpp"

namespace Nampower {
    // Lua: UnitInSight(unitA, unitB) -> boolean or nil on error
    // unitA may be "camera" to test camera -> unitB visibility (best for 3D portraits)
    uint32_t Script_UnitInSight(uintptr_t *luaState);
}
