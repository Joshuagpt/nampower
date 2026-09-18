//
// Minimal LOS / camera-sight implementation for WoW 1.12.1.
// Core ray test uses the client's CWorld_Intersect (same approach as UnitXP_SP3).
//

#include "insight.hpp"
#include "offsets.hpp"
#include "game.hpp"
#include "helper.hpp"
#include "logging.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace Nampower {
namespace {

    constexpr uint32_t CWORLD_INTERSECT_ADDR = 0x672170;
    constexpr uint32_t GET_ACTIVE_CAMERA_ADDR = 0x4818F0;
    constexpr uint32_t INTERSECT_FLAG = 0x100111;
    constexpr float DISTANCE_GUARD = 150.0f;
    constexpr float FLT_TOL = 1e-4f;
    constexpr float CAMERA_HEIGHT_OFFSET = 2.1f; // approx human eye / model head

    using CWorld_IntersectT = bool (__fastcall *)(
        const game::C3Vector *p1,
        const game::C3Vector *p2,
        int ignored,
        game::C3Vector *intersectPoint,
        float *distance,
        uint32_t queryFlags);

    using GetActiveCameraT = uint32_t (__fastcall *)();

    float VecDist(const game::C3Vector &a, const game::C3Vector &b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool WorldIntersect(const game::C3Vector &p1, const game::C3Vector &p2,
                        game::C3Vector *outHit, float *outDist) {
        *outDist = 1.0f;
        if (outHit) {
            outHit->x = outHit->y = outHit->z = 0.0f;
        }

        const float d = VecDist(p1, p2);
        if (d > DISTANCE_GUARD) {
            // Same guard as UnitXP_SP3: treat very long rays as blocked mid-point
            *outDist = 0.5f;
            if (outHit) {
                outHit->x = (p1.x + p2.x) * 0.5f;
                outHit->y = (p1.y + p2.y) * 0.5f;
                outHit->z = (p1.z + p2.z) * 0.5f;
            }
            return true;
        }
        if (d <= FLT_TOL) {
            return false;
        }

        auto fn = reinterpret_cast<CWorld_IntersectT>(CWORLD_INTERSECT_ADDR);
        return fn(&p1, &p2, 0, outHit, outDist, INTERSECT_FLAG);
    }

    // Returns true if LOS is clear between the two points (no solid between them).
    bool HasClearLos(game::C3Vector a, game::C3Vector b) {
        game::C3Vector hit{};
        float dist = 1.0f;
        const bool hitSomething = WorldIntersect(a, b, &hit, &dist);
        if (!hitSomething) {
            return true;
        }
        // distance in [0,1] means intersection is between the endpoints
        return !(dist >= 0.0f && dist <= 1.0f);
    }

    uint32_t GetCameraPtr() {
        auto getCam = reinterpret_cast<GetActiveCameraT>(GET_ACTIVE_CAMERA_ADDR);
        return getCam ? getCam() : 0;
    }

    game::C3Vector GetCameraPosition(uint32_t camera) {
        game::C3Vector pos{};
        if (!camera) {
            return pos;
        }
        // CGCamera + 0x8 = position (UnitXP_SP3 / common knowledge)
        auto *p = reinterpret_cast<float *>(camera + 0x8);
        pos.x = p[0];
        pos.y = p[1];
        pos.z = p[2];
        return pos;
    }

    game::C3Vector GetCameraForward(uint32_t camera) {
        game::C3Vector f{};
        if (!camera) {
            return f;
        }
        auto *mat = reinterpret_cast<float *>(camera + 0x14);
        f.x = mat[0];
        f.y = mat[1];
        f.z = mat[2];
        return f;
    }

    // Simple forward-cone test; checkCone=2.0 matches game FoV style (UnitXP_SP3)
    bool InViewingFrustum(const game::C3Vector &camPos, const game::C3Vector &camFwd,
                          const game::C3Vector &objPos, float checkCone) {
        game::C3Vector to{
            objPos.x - camPos.x,
            objPos.y - camPos.y,
            objPos.z - camPos.z
        };
        const float len = VecDist(camPos, objPos);
        if (len <= FLT_TOL) {
            return true;
        }
        to.x /= len;
        to.y /= len;
        to.z /= len;

        const float dot = to.x * camFwd.x + to.y * camFwd.y + to.z * camFwd.z;
        // Larger checkCone => narrower cone; 2.0 ~ full FoV
        const float threshold = 1.0f / checkCone;
        return dot >= threshold;
    }

    uintptr_t *ResolveUnitObject(const char *token) {
        if (!token || !*token) {
            return nullptr;
        }
        const uint64_t guid = GetUnitGuidFromString(token);
        if (guid == 0) {
            return nullptr;
        }
        return game::GetObjectPtr(guid);
    }

    // unit <-> unit LOS (height-adjusted like UnitXP_SP3)
    int UnitUnitInSight(uintptr_t *unit0, uintptr_t *unit1) {
        if (!unit0 || !unit1) {
            return -1;
        }
        if (unit0 == unit1) {
            return 1;
        }

        game::C3Vector pos0 = game::UnitGetPosition(unit0);
        game::C3Vector pos1 = game::UnitGetPosition(unit1);

        // Raise both to ~eye height for first pass
        pos0.z += CAMERA_HEIGHT_OFFSET;
        pos1.z += CAMERA_HEIGHT_OFFSET;

        if (HasClearLos(pos0, pos1)) {
            return 1;
        }
        return 0;
    }

    // camera -> unit (frustum + LOS) — best match for 3D portrait usability
    int CameraUnitInSight(uintptr_t *unit) {
        if (!unit) {
            return -1;
        }
        const uint32_t camera = GetCameraPtr();
        if (!camera) {
            return -1;
        }

        const game::C3Vector camPos = GetCameraPosition(camera);
        const game::C3Vector camFwd = GetCameraForward(camera);
        game::C3Vector unitPos = game::UnitGetPosition(unit);
        unitPos.z += CAMERA_HEIGHT_OFFSET;

        if (!InViewingFrustum(camPos, camFwd, unitPos, 2.0f)) {
            return 0;
        }
        if (!HasClearLos(camPos, unitPos)) {
            return 0;
        }
        return 1;
    }

} // namespace

uint32_t Script_UnitInSight(uintptr_t *luaState) {
    luaState = GetLuaStatePtr();

    if (!lua_isstring(luaState, 1) || !lua_isstring(luaState, 2)) {
        lua_pushnil(luaState);
        return 1;
    }

    const char *a = lua_tostring(luaState, 1);
    const char *b = lua_tostring(luaState, 2);
    if (!a || !b) {
        lua_pushnil(luaState);
        return 1;
    }

    int result = -1;

    // UnitInSight("camera", unit) or UnitInSight("player", unit) etc.
    if (_stricmp(a, "camera") == 0) {
        uintptr_t *unitB = ResolveUnitObject(b);
        result = CameraUnitInSight(unitB);
    } else {
        uintptr_t *unitA = ResolveUnitObject(a);
        uintptr_t *unitB = ResolveUnitObject(b);
        result = UnitUnitInSight(unitA, unitB);
    }

    if (result < 0) {
        lua_pushnil(luaState);
    } else {
        lua_pushboolean(luaState, result != 0);
    }
    return 1;
}

} // namespace Nampower
