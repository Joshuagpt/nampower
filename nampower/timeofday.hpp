#pragma once
// Client-side time of day for 1.12 (ported from comfyatmos timeofday).
// Modes: 0 = server (do not write), 1 = computer local clock, 2 = fixed hour.

namespace nampower {
namespace timeofday {

// CVar-driven settings (updated from main::updateFromCvar).
struct Settings {
    int   mode = 0;           // 0 server, 1 local, 2 fixed
    float hour = 12.0f;       // fixed hour 0..24
    // Absolute addresses as in stock 1.12 client (image base 0x00400000).
    unsigned addrMinutes  = 0x00CE9B60; // uint32 minutes since midnight
    unsigned addrFraction = 0x00CE9B64; // float day fraction
    unsigned addrMinutesF = 0x00CE8574; // float minutes (optional, 0 = skip)
};

extern Settings g_settings;

// Call once after CVars are registered (optional validate).
void Init();

// Call every frame from ISceneEnd (or equivalent). Writes clock when mode != 0.
void Apply();

} // namespace timeofday
} // namespace nampower
