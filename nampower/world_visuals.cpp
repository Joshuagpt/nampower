#include "world_visuals.hpp"
#include "game.hpp"
#include "offsets.hpp"
#include "logging.hpp"

#include <hadesmem/patcher.hpp>
#include <hadesmem/process.hpp>

namespace Nampower {
    namespace {
        // Match nampower's thiscall-via-fastcall convention (dummy edx).
        using Weather_SetTypeT = void (__fastcall *)(void *self, void *dummy_edx, int type, float intensity,
                                                     bool unknown);
        using AddNameplateT = void (__fastcall *)(void *self, void *dummy_edx, void *unknown1, void *unknown2);
        using RemoveNameplateT = void (__thiscall *)(void *unit);
        using GetCreatureTypeT = int (__thiscall *)(void *self);
        using RenderWorldT = int (__fastcall *)(void *self, void *dummy_edx);

        std::unique_ptr<hadesmem::PatchDetour<Weather_SetTypeT> > gWeatherSetTypeDetour;
        std::unique_ptr<hadesmem::PatchDetour<AddNameplateT> > gAddNameplateDetour;
        std::unique_ptr<hadesmem::PatchDetour<RenderWorldT> > gRenderWorldDetour;

        constexpr int WEATHER_TYPE_RAIN = 1;
        constexpr int WEATHER_TYPE_SNOW = 2;
        constexpr int WEATHER_TYPE_SANDSTORM = 3;
        constexpr int CREATURE_TYPE_CRITTER = 8;

        bool ShouldBlockWeatherType(int type) {
            if (gUserSettings.noWeather) {
                return type == WEATHER_TYPE_RAIN || type == WEATHER_TYPE_SNOW ||
                       type == WEATHER_TYPE_SANDSTORM;
            }
            if (type == WEATHER_TYPE_RAIN && gUserSettings.noRain) {
                return true;
            }
            if (type == WEATHER_TYPE_SNOW && gUserSettings.noSnow) {
                return true;
            }
            if (type == WEATHER_TYPE_SANDSTORM && gUserSettings.noSandstorm) {
                return true;
            }
            return false;
        }

        // Active nameplate linked-list head (UnitXP_SP3 / 1.12.1)
        constexpr uintptr_t kNameplateListHead = 0x00C4D92C;
        constexpr uintptr_t kNameplateNextOffset = 0x4E0;
        constexpr uintptr_t kNameplateGuidOffset = 0x4E8;

        int ObjectTypeId(void *object) {
            if (!object) {
                return -1;
            }
            return *reinterpret_cast<int *>(reinterpret_cast<uintptr_t>(object) + 0x14);
        }

        int UnitCreatureType(void *unit) {
            if (!unit) {
                return -1;
            }
            auto const getCreatureType =
                reinterpret_cast<GetCreatureTypeT>(Offsets::CGUnit_C_GetCreatureType);
            return getCreatureType(unit);
        }

        // true = this unit should NOT show a nameplate under current settings
        bool ShouldHideCritterNameplate(void *unit) {
            if (!gUserSettings.hideCritterNameplates || !unit) {
                return false;
            }
            const uintptr_t unitAddr = reinterpret_cast<uintptr_t>(unit);
            if (unitAddr == 0 || (unitAddr & 1) != 0) {
                return false;
            }
            const int typeId = ObjectTypeId(unit);
            if (typeId != game::ID_UNIT && typeId != game::ID_PLAYER) {
                return false;
            }
            if (UnitCreatureType(unit) != CREATURE_TYPE_CRITTER) {
                return false;
            }
            // Hide only while not in combat (same as UnitXP)
            return !game::UnitIsInCombat(reinterpret_cast<uintptr_t *>(unit));
        }

        void RemoveNameplate(void *unit) {
            if (!unit) {
                return;
            }
            auto const remove =
                reinterpret_cast<RemoveNameplateT>(Offsets::CGUnit_C_RemoveNamePlate);
            remove(unit);
        }

        // Walk active nameplates and strip ones that should be hidden (e.g. critter left combat).
        void CleanupHiddenCritterNameplates() {
            if (!gUserSettings.hideCritterNameplates) {
                return;
            }

            uintptr_t nameplateItem = *reinterpret_cast<uintptr_t *>(kNameplateListHead);
            while (nameplateItem != 0 && (nameplateItem & 1) == 0) {
                // Save next before possible delete
                const uintptr_t nextItem =
                    *reinterpret_cast<uintptr_t *>(nameplateItem + kNameplateNextOffset);
                const uint64_t guid =
                    *reinterpret_cast<uint64_t *>(nameplateItem + kNameplateGuidOffset);

                auto *unit = game::GetObjectPtr(guid);
                if (unit && ShouldHideCritterNameplate(unit)) {
                    RemoveNameplate(unit);
                }

                nameplateItem = nextItem;
            }
        }

        void Weather_SetTypeHook(hadesmem::PatchDetourBase *detour, void *self, void *dummy_edx, int type,
                                 float intensity, bool unknown) {
            auto const original = detour->GetTrampolineT<Weather_SetTypeT>();
            if (ShouldBlockWeatherType(type)) {
                original(self, dummy_edx, 0, intensity, unknown);
                return;
            }
            original(self, dummy_edx, type, intensity, unknown);
        }

        void AddNameplateHook(hadesmem::PatchDetourBase *detour, void *self, void *dummy_edx, void *unknown1,
                              void *unknown2) {
            auto const original = detour->GetTrampolineT<AddNameplateT>();

            if (ShouldHideCritterNameplate(self)) {
                // Also try remove in case a plate already exists from combat
                RemoveNameplate(self);
                return;
            }

            original(self, dummy_edx, unknown1, unknown2);
        }

        int RenderWorldHook(hadesmem::PatchDetourBase *detour, void *self, void *dummy_edx) {
            // Before draw: drop nameplates that are no longer allowed (critter left combat, etc.)
            CleanupHiddenCritterNameplates();

            auto const original = detour->GetTrampolineT<RenderWorldT>();
            return original(self, dummy_edx);
        }
    } // namespace

    void InstallWorldVisualHooks(const hadesmem::Process &process) {
        DEBUG_LOG("Installing world visual hooks (Weather / HideCritterNameplates)");

        gWeatherSetTypeDetour = std::make_unique<hadesmem::PatchDetour<Weather_SetTypeT> >(
            process,
            reinterpret_cast<Weather_SetTypeT>(Offsets::Weather_SetType),
            &Weather_SetTypeHook);
        gWeatherSetTypeDetour->Apply();

        gAddNameplateDetour = std::make_unique<hadesmem::PatchDetour<AddNameplateT> >(
            process,
            reinterpret_cast<AddNameplateT>(Offsets::CGUnit_C_AddNamePlate),
            &AddNameplateHook);
        gAddNameplateDetour->Apply();

        // Needed so plates created while in combat are removed after combat ends
        gRenderWorldDetour = std::make_unique<hadesmem::PatchDetour<RenderWorldT> >(
            process,
            reinterpret_cast<RenderWorldT>(Offsets::RenderWorld),
            &RenderWorldHook);
        gRenderWorldDetour->Apply();
    }
}
