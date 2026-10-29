#include "timeofday.hpp"

#include <windows.h>
#include <cmath>
#include <cstdint>

namespace nampower {
namespace timeofday {

Settings g_settings;

namespace {

float LocalHour()
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    float hour = static_cast<float>(t.wHour) + t.wMinute / 60.0f + t.wSecond / 3600.0f;
    if (hour >= 24.0f)
        hour = std::nextafter(24.0f, 0.0f);
    return hour;
}

float ClampHour(float h)
{
    h = std::fmod(h, 24.0f);
    if (h < 0.0f)
        h += 24.0f;
    return h;
}

bool Writable(void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

} // namespace

void Init()
{
    // nothing yet; addresses are defaults for stock 1.12
}

namespace {
// Shadow clock: while we override the client clock (mode != 0) the client keeps
// advancing the variable from whatever we last wrote. By measuring that per-frame
// advance we keep tracking "server" time, so switching back to mode 0 can restore it.
bool   g_active     = false; // currently overriding
double g_shadowFrac = 0.0;   // tracked server day fraction [0,1)
float  g_lastFrac   = 0.0f;  // value we wrote last frame

double Wrap01(double v)
{
    v = std::fmod(v, 1.0);
    if (v < 0.0) v += 1.0;
    return v;
}

void WriteClock(volatile uint32_t* pMin, volatile float* pFrac, volatile float* pMinF, double frac)
{
    frac = Wrap01(frac);
    const float minutes = static_cast<float>(frac * 1440.0);
    const uint32_t m = static_cast<uint32_t>(minutes) % 1440;
    *pMin = m;
    *pFrac = static_cast<float>(frac);
    if (pMinF && Writable(const_cast<float*>(pMinF), 4))
        *pMinF = static_cast<float>(m);
}
} // namespace

void Apply()
{
    const uintptr_t slide =
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) - 0x00400000;

    auto* pMin  = reinterpret_cast<volatile uint32_t*>(g_settings.addrMinutes + slide);
    auto* pFrac = reinterpret_cast<volatile float*>(g_settings.addrFraction + slide);
    auto* pMinF = g_settings.addrMinutesF
                      ? reinterpret_cast<volatile float*>(g_settings.addrMinutesF + slide)
                      : nullptr;

    if (!Writable(const_cast<uint32_t*>(pMin), 4) ||
        !Writable(const_cast<float*>(pFrac), 4))
        return;

    if (g_settings.mode == 0) {
        // Back to server time: restore the tracked server clock once.
        if (g_active) {
            WriteClock(pMin, pFrac, pMinF, g_shadowFrac);
            g_active = false;
        }
        return;
    }

    // Track server time while overriding.
    if (!g_active) {
        g_shadowFrac = Wrap01(*pFrac); // first override frame: still the real value
        g_active = true;
    } else {
        double d = static_cast<double>(*pFrac) - static_cast<double>(g_lastFrac);
        if (d < -0.5) d += 1.0;
        else if (d > 0.5) d -= 1.0;
        g_shadowFrac = Wrap01(g_shadowFrac + d);
    }

    float hour = (g_settings.mode == 1) ? LocalHour() : ClampHour(g_settings.hour);
    WriteClock(pMin, pFrac, pMinF, hour / 24.0);
    g_lastFrac = *pFrac;
}

} // namespace timeofday
} // namespace nampower
