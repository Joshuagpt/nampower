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
        using GetCreatureTypeT = int (__thiscall *)(void *self);

        std::unique_ptr<hadesmem::PatchDetour<Weather_SetTypeT> > gWeatherSetTypeDetour;
        std::unique_ptr<hadesmem::PatchDetour<AddNameplateT> > gAddNameplateDetour;

        constexpr int WEATHER_TYPE_RAIN = 1;
        constexpr int CREATURE_TYPE_CRITTER = 8;

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

        void Weather_SetTypeHook(hadesmem::PatchDetourBase *detour, void *self, void *dummy_edx, int type,
                                 float intensity, bool unknown) {
            auto const original = detour->GetTrampolineT<Weather_SetTypeT>();
            if (gUserSettings.noRain && type == WEATHER_TYPE_RAIN) {
                original(self, dummy_edx, 0, intensity, unknown);
                return;
            }
            original(self, dummy_edx, type, intensity, unknown);
        }

        void AddNameplateHook(hadesmem::PatchDetourBase *detour, void *self, void *dummy_edx, void *unknown1,
                              void *unknown2) {
            auto const original = detour->GetTrampolineT<AddNameplateT>();

            if (gUserSettings.hideCritterNameplates && self) {
                const uintptr_t unitAddr = reinterpret_cast<uintptr_t>(self);
                if (unitAddr != 0 && (unitAddr & 1) == 0) {
                    const int typeId = ObjectTypeId(self);
                    if (typeId == game::ID_UNIT || typeId == game::ID_PLAYER) {
                        if (UnitCreatureType(self) == CREATURE_TYPE_CRITTER) {
                            auto *unit = reinterpret_cast<uintptr_t *>(self);
                            if (!game::UnitIsInCombat(unit)) {
                                return;
                            }
                        }
                    }
                }
            }

            original(self, dummy_edx, unknown1, unknown2);
        }
    } // namespace

    void InstallWorldVisualHooks(const hadesmem::Process &process) {
        DEBUG_LOG("Installing world visual hooks (NoRain / HideCritterNameplates)");

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
    }
}
