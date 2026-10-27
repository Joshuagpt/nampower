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

void Apply()
{
    if (g_settings.mode == 0)
        return; // server time — do not overwrite

    float hour = 0.0f;
    if (g_settings.mode == 1)
        hour = LocalHour();
    else
        hour = ClampHour(g_settings.hour);

    const uintptr_t slide =
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) - 0x00400000;

    auto* pMin  = reinterpret_cast<volatile uint32_t*>(g_settings.addrMinutes + slide);
    auto* pFrac = reinterpret_cast<volatile float*>(g_settings.addrFraction + slide);
    auto* pMinF = g_settings.addrMinutesF
                      ? reinterpret_cast<volatile float*>(g_settings.addrMinutesF + slide)
                      : nullptr;

    if (!Writable(const_cast<uint32_t*>(reinterpret_cast<volatile uint32_t*>(pMin)), 4) ||
        !Writable(const_cast<float*>(reinterpret_cast<volatile float*>(pFrac)), 4))
        return;

    const float minutes = hour * 60.0f;
    const uint32_t m = static_cast<uint32_t>(minutes) % 1440;
    const float frac = minutes / 1440.0f;

    *pMin = m;
    *pFrac = frac;
    if (pMinF && Writable(const_cast<float*>(reinterpret_cast<volatile float*>(pMinF)), 4))
        *pMinF = static_cast<float>(m);
}

} // namespace timeofday
} // namespace nampower
