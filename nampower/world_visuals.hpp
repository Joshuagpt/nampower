#pragma once

#include "main.hpp"

namespace Nampower {
    // Optional world/visual QoL from UnitXP_SP3:
    // - NP_NoRain: block weather type rain
    // - NP_HideCritterNameplates: skip nameplates for non-combat critters (creature type 8)
    void InstallWorldVisualHooks(const hadesmem::Process &process);
}
