// =============================================================================
// OES Enterprise — ibString vs wxString micro-benchmark
//
// DISABLED by default (benchmarks are noisy / machine-dependent — keep the
// normal suite fast). Run explicitly:
//
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*IbStringBench*
//
// Reads the printed table: per-op nanoseconds for ibString vs wxString and the
// ib/wx ratio (<1 => ibString faster). These are RELATIVE figures on this
// build/CPU, not absolute truth — re-run to compare after allocator changes.
// =============================================================================

#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <iostream>
#include <iomanip>

#include "backend/fstring.h"

namespace {

volatile size_t g_sink = 0;   // written through volatile so loops aren't elided

template <class F>
double TimeNsPerOp(int iters, F&& f) {
    for (int i = 0; i < iters / 10 + 1; ++i) f(i);            // warmup
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) f(i);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
}

void Row(const char* name, double ib, double wx) {
    std::cout << "  " << std::left << std::setw(20) << name << std::right
              << "  ib=" << std::setw(8) << std::fixed << std::setprecision(1) << ib << "ns"
              << "  wx=" << std::setw(8) << wx << "ns"
              << "  x"   << std::setprecision(2) << (wx > 0 ? ib / wx : 0.0) << "\n";
}

} // namespace

TEST(IbStringBench, DISABLED_Compare) {
    const int N = 200000;
    // 54 wchar, pure ASCII (encoding-safe) — well past std::wstring's 7-char SSO,
    // so both types hit the heap (ibString -> pool, wxString -> malloc).
    const wchar_t kLong[] = L"the quick brown fox jumps over the lazy dog 0123456789";

    std::cout << "\n[ ibString vs wxString | N=" << N
              << " | sizeof ib=" << sizeof(ibString) << " wx=" << sizeof(wxString)
              << " | x = ib/wx, <1 = ibString faster ]\n";

    // 1) construct + destruct a heap string — pool vs general allocator
    {
        const double ib = TimeNsPerOp(N, [&](int){ ibString s(kLong); g_sink += s.Len(); });
        const double wx = TimeNsPerOp(N, [&](int){ wxString  s(kLong); g_sink += s.length(); });
        Row("ctor+dtor (heap)", ib, wx);
    }
    // 2) copy a heap string
    {
        const ibString ibSrc(kLong); const wxString wxSrc(kLong);
        const double ib = TimeNsPerOp(N, [&](int){ ibString c(ibSrc); g_sink += c.Len(); });
        const double wx = TimeNsPerOp(N, [&](int){ wxString  c(wxSrc); g_sink += c.length(); });
        Row("copy (heap)", ib, wx);
    }
    // 3) concat
    {
        const ibString ibA(kLong), ibB(L"-suffix");
        const wxString wxA(kLong), wxB(L"-suffix");
        const double ib = TimeNsPerOp(N, [&](int){ ibString c = ibA + ibB; g_sink += c.Len(); });
        const double wx = TimeNsPerOp(N, [&](int){ wxString  c = wxA + wxB; g_sink += c.length(); });
        Row("concat", ib, wx);
    }
    // 4) UTF-8 decode (cached on neither side — fair) — "Привет" x4 = 24 wchar
    {
        std::string u8;
        for (int i = 0; i < 4; ++i) u8 += "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
        const char* const p = u8.data();
        const size_t      n = u8.size();
        const double ib = TimeNsPerOp(N, [&](int){ ibString s; s.SetUtf8(p, n);             g_sink += s.Len(); });
        const double wx = TimeNsPerOp(N, [&](int){ wxString s = wxString::FromUTF8(p, n);   g_sink += s.length(); });
        Row("utf8 decode", ib, wx);
    }
    // 5) find + mid (parity expected — shared std::wstring core)
    {
        const ibString ibS(kLong), ibPat(L"brown");
        const wxString wxStr(kLong), wxPat(L"brown");   // not 'wxS' — that's a wxWidgets macro
        const double ib = TimeNsPerOp(N, [&](int){ g_sink += ibS.Find(ibPat); g_sink += ibS.Mid(4, 9).Len(); });
        const double wx = TimeNsPerOp(N, [&](int){ g_sink += (size_t)wxStr.Find(wxPat); g_sink += wxStr.Mid(4, 9).length(); });
        Row("find+mid", ib, wx);
    }

    EXPECT_NE(g_sink, 0xFFFFFFFFu);   // observe the sink
    SUCCEED();
}
