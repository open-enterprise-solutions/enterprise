#include "backend/utils/dataVersion.hpp"

#include <wx/datetime.h>
#include <wx/longlong.h>

#include <atomic>
#include <cstdint>

namespace ibDataVersion {

namespace {

// base-62 alphabet [0-9A-Za-z] — URL-safe and case-stable across the
// drivers we support (no collation surprises).
constexpr char kAlphabet[63] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
constexpr int  kStampLen     = 12;

// Per-process rolling counter. Sized to 16 bits so it can be packed
// next to a 56-bit millisecond timestamp in 72 bits — fits 12 base-62
// chars (62^12 > 2^71). std::memory_order_relaxed is fine; we only
// care that no two concurrent calls return the same value on the same
// process, not about ordering against other memory.
std::atomic<std::uint16_t> g_counter{0};

inline std::uint16_t NextCounter() {
    return g_counter.fetch_add(1, std::memory_order_relaxed);
}

// Encode a non-negative 72-bit value packed into a low + high uint64_t
// pair into exactly kStampLen base-62 chars, zero-padded on the left.
// We split 72 bits as: high 8 bits in `hi`, low 64 bits in `lo`.
wxString Encode(std::uint64_t hi, std::uint64_t lo) {
    char buf[kStampLen + 1];
    buf[kStampLen] = '\0';

    // Long division by 62 over the 72-bit number. Goes right-to-left.
    for (int i = kStampLen - 1; i >= 0; --i) {
        // Combine hi:lo, divide by 62. We can use 128-bit math
        // emulated via two 64-bit halves — but 72 bits is tiny: fold
        // hi into the high end of lo by carrying across.
        std::uint64_t carry = hi;
        // (carry, lo) treated as 128-bit. Quotient = ((carry << 64) | lo) / 62.
        // Do high half first.
        std::uint64_t hiQuot = carry / 62;
        std::uint64_t hiRem  = carry % 62;

        // Low half: (hiRem << 64 | lo) / 62. We can't do the shift
        // directly, but since the divisor is 62 we can fold step-wise.
        // Simpler: process lo in two halves (high 32 + low 32).
        std::uint64_t combinedHi = (hiRem << 32) | (lo >> 32);
        std::uint64_t qH = combinedHi / 62;
        std::uint64_t rH = combinedHi % 62;

        std::uint64_t combinedLo = (rH << 32) | (lo & 0xFFFFFFFFULL);
        std::uint64_t qL = combinedLo / 62;
        std::uint64_t rL = combinedLo % 62;

        buf[i] = kAlphabet[rL];
        lo = (qH << 32) | qL;
        hi = hiQuot;
    }

    return wxString::FromAscii(buf);
}

} // namespace

wxString NewStamp() {
    // Wall-clock milliseconds since Unix epoch. wxLongLong gives us a
    // portable 64-bit value across the supported toolchains.
    const wxLongLong ms = wxGetUTCTimeMillis();
    const std::uint64_t msU = static_cast<std::uint64_t>(ms.GetValue());

    const std::uint64_t counter = NextCounter();

    // Pack (ms << 16) | counter as a 72-bit value.
    // High 8 bits = top 8 bits of ms (ms is <= 56 bits for realistic
    // dates — year 4271 fits).
    const std::uint64_t hi = (msU >> 56) & 0xFFULL;
    const std::uint64_t lo = ((msU & 0x00FFFFFFFFFFFFFFULL) << 16) | (counter & 0xFFFFULL);

    return Encode(hi, lo);
}

bool IsValidStamp(const wxString& str) {
    if (str.length() != static_cast<size_t>(kStampLen))
        return false;
    for (size_t i = 0; i < str.length(); ++i) {
        const wxChar c = str.GetChar(i);
        const bool digit  = (c >= '0' && c <= '9');
        const bool upper  = (c >= 'A' && c <= 'Z');
        const bool lower  = (c >= 'a' && c <= 'z');
        if (!digit && !upper && !lower)
            return false;
    }
    return true;
}

} // namespace ibDataVersion
