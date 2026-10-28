// =============================================================================
// OES Enterprise — ibString vs wxString vs std::wstring micro-benchmark
//
// DISABLED by default (benchmarks are noisy / machine-dependent — keep the
// normal suite fast). Run explicitly:
//
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*IbStringBench*
//
// Reads the printed table: per-op nanoseconds for ibString, wxString and
// std::wstring (what wxString wraps, wxUSE_STL_BASED_WXSTRING) and the ib/wx,
// ib/std ratios (<1 => ibString faster). These are RELATIVE figures on this
// build/CPU, not absolute truth — re-run to compare after allocator changes.
// =============================================================================

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>

#include "core/fstring.h"

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

// …on `threads` threads at once, each doing `iters`: the wall time per op of one of them — what they share (a count,
// an allocator) is in it. `f` returns what it read; each thread sums its own, a cache line from the next.
template <class F>
double TimeNsPerOp(int threads, int iters, F&& f) {
    constexpr size_t kApart = 64 / sizeof(size_t);
    std::vector<size_t> sums(threads * kApart);
    std::atomic<bool> go{ false };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            while (!go.load(std::memory_order_acquire)) {}
            size_t sum = 0;
            for (int i = 0; i < iters; ++i) sum += f(i);
            sums[t * kApart] = sum;
        });
    const auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    for (std::thread& thread : pool) thread.join();
    const auto t1 = std::chrono::steady_clock::now();
    for (const size_t sum : sums) g_sink += sum;
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
}

// `stl` < 0 — the standard library has no such op.
void Row(const char* name, double ib, double wx, double stl) {
    std::cout << "  " << std::left << std::setw(20) << name << std::right << std::fixed << std::setprecision(1)
              << "  ib=" << std::setw(8) << ib << "ns"
              << "  wx=" << std::setw(8) << wx << "ns";
    if (stl >= 0)
        std::cout << "  std=" << std::setw(8) << stl << "ns";
    else
        std::cout << "  std=" << std::setw(8) << "-" << "  ";
    std::cout << "  ib/wx x" << std::setprecision(2) << (wx > 0 ? ib / wx : 0.0);
    if (stl > 0)
        std::cout << "  ib/std x" << ib / stl;
    std::cout << "\n";
}

// Two columns: ibString against std::u16string, the same character width.
void Row(const char* name, double ib, double u16) {
    std::cout << "  " << std::left << std::setw(20) << name << std::right << std::fixed << std::setprecision(1)
              << "  ib=" << std::setw(8) << ib << "ns"
              << "  u16=" << std::setw(8) << u16 << "ns"
              << "  ib/u16 x" << std::setprecision(2) << (u16 > 0 ? ib / u16 : 0.0) << "\n";
}

} // namespace

TEST(IbStringBench, DISABLED_Compare) {
    const int N = 200000;
    // 54 wchar, pure ASCII (encoding-safe) — well past std::wstring's 7-char SSO,
    // so both types hit the heap (ibString -> pool, wxString -> malloc).
    const wchar_t kLong[] = L"the quick brown fox jumps over the lazy dog 0123456789";

    std::cout << "\n[ ibString vs wxString vs std::wstring | N=" << N
              << " | sizeof ib=" << sizeof(ibString) << " wx=" << sizeof(wxString) << " std=" << sizeof(std::wstring)
              << " | x = ib/other, <1 = ibString faster ]\n";

    // 1) construct + destruct a heap string — pool vs general allocator
    {
        const double ib  = TimeNsPerOp(N, [&](int){ ibString     s(kLong); g_sink += s.Len(); });
        const double wx  = TimeNsPerOp(N, [&](int){ wxString     s(kLong); g_sink += s.length(); });
        const double stl = TimeNsPerOp(N, [&](int){ std::wstring s(kLong); g_sink += s.size(); });
        Row("ctor+dtor (heap)", ib, wx, stl);
    }
    // 2) construct + destruct a short string — 4 wchar: std::wstring keeps it inside itself (SSO), ibString takes a
    //    block of the pool
    {
        const wchar_t kShort[] = L"Name";
        const double ib  = TimeNsPerOp(N, [&](int){ ibString     s(kShort); g_sink += s.Len(); });
        const double wx  = TimeNsPerOp(N, [&](int){ wxString     s(kShort); g_sink += s.length(); });
        const double stl = TimeNsPerOp(N, [&](int){ std::wstring s(kShort); g_sink += s.size(); });
        Row("ctor+dtor (short)", ib, wx, stl);
    }
    // 3) copy a heap string
    {
        const ibString ibSrc(kLong); const wxString wxSrc(kLong); const std::wstring stlSrc(kLong);
        const double ib  = TimeNsPerOp(N, [&](int){ ibString     c(ibSrc);  g_sink += c.Len(); });
        const double wx  = TimeNsPerOp(N, [&](int){ wxString     c(wxSrc);  g_sink += c.length(); });
        const double stl = TimeNsPerOp(N, [&](int){ std::wstring c(stlSrc); g_sink += c.size(); });
        Row("copy (heap)", ib, wx, stl);
    }
    // 4) copy ONE heap string from 8 threads at once — ibString's copies meet on its block's count, the others' on the
    //    allocator
    {
        const int kThreads = 8;
        const ibString ibSrc(kLong); const wxString wxSrc(kLong); const std::wstring stlSrc(kLong);
        const double ib  = TimeNsPerOp(kThreads, N, [&](int){ const ibString     c(ibSrc);  return c.Len(); });
        const double wx  = TimeNsPerOp(kThreads, N, [&](int){ const wxString     c(wxSrc);  return c.length(); });
        const double stl = TimeNsPerOp(kThreads, N, [&](int){ const std::wstring c(stlSrc); return c.size(); });
        Row("copy x8 threads", ib, wx, stl);
    }
    // 5) concat
    {
        const ibString     ibA(kLong),  ibB(L"-suffix");
        const wxString     wxA(kLong),  wxB(L"-suffix");
        const std::wstring stlA(kLong), stlB(L"-suffix");
        const double ib  = TimeNsPerOp(N, [&](int){ ibString     c = ibA + ibB;   g_sink += c.Len(); });
        const double wx  = TimeNsPerOp(N, [&](int){ wxString     c = wxA + wxB;   g_sink += c.length(); });
        const double stl = TimeNsPerOp(N, [&](int){ std::wstring c = stlA + stlB; g_sink += c.size(); });
        Row("concat", ib, wx, stl);
    }
    // 6) UTF-8 decode (cached on neither side — fair) — a six-letter Cyrillic word x4 = 24 wchar; the standard library has none
    //    (std::wstring_convert is deprecated)
    {
        std::string u8;
        for (int i = 0; i < 4; ++i) u8 += "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
        const char* const p = u8.data();
        const size_t      n = u8.size();
        const double ib = TimeNsPerOp(N, [&](int){ ibString s; s.SetUtf8(p, n);             g_sink += s.Len(); });
        const double wx = TimeNsPerOp(N, [&](int){ wxString s = wxString::FromUTF8(p, n);   g_sink += s.length(); });
        Row("utf8 decode", ib, wx, -1);
    }
    // 7) find + mid (parity expected — shared std::wstring core)
    {
        const ibString     ibS(kLong),   ibPat(L"brown");
        const wxString     wxStr(kLong), wxPat(L"brown");   // not 'wxS' — that's a wxWidgets macro
        const std::wstring stlS(kLong),  stlPat(L"brown");
        const double ib  = TimeNsPerOp(N, [&](int){ g_sink += ibS.Find(ibPat); g_sink += ibS.Mid(4, 9).Len(); });
        const double wx  = TimeNsPerOp(N, [&](int){ g_sink += (size_t)wxStr.Find(wxPat); g_sink += wxStr.Mid(4, 9).length(); });
        const double stl = TimeNsPerOp(N, [&](int){ g_sink += stlS.find(stlPat); g_sink += stlS.substr(4, 9).size(); });
        Row("find+mid", ib, wx, stl);
    }

    EXPECT_NE(g_sink, 0xFFFFFFFFu);   // observe the sink
    SUCCEED();
}

// The same operations from an ibT literal against std::u16string from the same literal — one character width on
// both sides, so what is left is the copy, the allocator and the count. The table above starts from wide text:
// where wchar_t is four bytes ibString narrows it on the way in, and its first rows are the road from wx.
TEST(IbStringBench, DISABLED_CompareU16) {
    const int N = 200000;
    const ibChar kLong[]  = ibT("the quick brown fox jumps over the lazy dog 0123456789");
    const ibChar kShort[] = ibT("Name");

    std::cout << "\n[ ibString vs std::u16string | N=" << N
              << " | sizeof ib=" << sizeof(ibString) << " u16=" << sizeof(std::u16string)
              << " | x = ib/u16, <1 = ibString faster ]\n";

    {
        const double ib  = TimeNsPerOp(N, [&](int){ ibString       s(kLong); g_sink += s.Len(); });
        const double u16 = TimeNsPerOp(N, [&](int){ std::u16string s(kLong); g_sink += s.size(); });
        Row("ctor+dtor (heap)", ib, u16);
    }
    {   // …the length handed in: what is left is the block and the copy, and the row above less this one is the scan
        const size_t n = sizeof(kLong) / sizeof(kLong[0]) - 1;
        const double ib  = TimeNsPerOp(N, [&](int){ ibString       s(kLong, n); g_sink += s.Len(); });
        const double u16 = TimeNsPerOp(N, [&](int){ std::u16string s(kLong, n); g_sink += s.size(); });
        Row("ctor+dtor (heap, n)", ib, u16);
    }
    {   // …many alive at once, as a list's rows hold them: a freed block goes back to the pool while the thread's
        // cache has room for it, and past that the CRT makes and takes every one
        const size_t n = sizeof(kLong) / sizeof(kLong[0]) - 1;
        for (const size_t live : { size_t(1000), size_t(10000), size_t(100000) }) {
            std::vector<ibString>       ibs;  ibs.reserve(live);
            std::vector<std::u16string> u16s; u16s.reserve(live);
            const double ib  = TimeNsPerOp(100, [&](int){ for (size_t k = 0; k < live; ++k) ibs.emplace_back(kLong, n);  ibs.clear(); }) / live;
            const double u16 = TimeNsPerOp(100, [&](int){ for (size_t k = 0; k < live; ++k) u16s.emplace_back(kLong, n); u16s.clear(); }) / live;
            Row(("heap, " + std::to_string(live) + " alive").c_str(), ib, u16);
        }
    }
    {
        const double ib  = TimeNsPerOp(N, [&](int){ ibString       s(kShort); g_sink += s.Len(); });
        const double u16 = TimeNsPerOp(N, [&](int){ std::u16string s(kShort); g_sink += s.size(); });
        Row("ctor+dtor (short)", ib, u16);
    }
    {
        const ibString ibSrc(kLong); const std::u16string u16Src(kLong);
        const double ib  = TimeNsPerOp(N, [&](int){ ibString       c(ibSrc);  g_sink += c.Len(); });
        const double u16 = TimeNsPerOp(N, [&](int){ std::u16string c(u16Src); g_sink += c.size(); });
        Row("copy (heap)", ib, u16);
    }
    {
        const int kThreads = 8;
        const ibString ibSrc(kLong); const std::u16string u16Src(kLong);
        const double ib  = TimeNsPerOp(kThreads, N, [&](int){ const ibString       c(ibSrc);  return c.Len(); });
        const double u16 = TimeNsPerOp(kThreads, N, [&](int){ const std::u16string c(u16Src); return c.size(); });
        Row("copy x8 threads", ib, u16);
    }
    {
        const ibString       ibA(kLong),  ibB(ibT("-suffix"));
        const std::u16string u16A(kLong), u16B(ibT("-suffix"));
        const double ib  = TimeNsPerOp(N, [&](int){ ibString       c = ibA + ibB;   g_sink += c.Len(); });
        const double u16 = TimeNsPerOp(N, [&](int){ std::u16string c = u16A + u16B; g_sink += c.size(); });
        Row("concat", ib, u16);
    }
    {
        const ibString       ibS(kLong),  ibPat(ibT("brown"));
        const std::u16string u16S(kLong), u16Pat(ibT("brown"));
        const double ib  = TimeNsPerOp(N, [&](int){ g_sink += ibS.Find(ibPat); g_sink += ibS.Mid(4, 9).Len(); });
        const double u16 = TimeNsPerOp(N, [&](int){ g_sink += u16S.find(u16Pat); g_sink += u16S.substr(4, 9).size(); });
        Row("find+mid", ib, u16);
    }

    EXPECT_NE(g_sink, 0xFFFFFFFFu);   // observe the sink
    SUCCEED();
}
