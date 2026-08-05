// =============================================================================
// OES Enterprise — runtime / number / parser micro-benchmarks
//
// DISABLED by default (benchmarks are noisy / machine-dependent — keep the
// normal suite fast). Build in RELEASE for relevant figures, then run:
//
//   cmake --build build --config Release --target oes_tests
//   build/bin/Release/oes_tests --gtest_also_run_disabled_tests
//       --gtest_filter=*Bench*
//   (the two lines are one shell command; the continuation backslash is left out
//    because a trailing \ inside a // comment splices the next line into it)
//
// Three groups:
//   RuntimeBench  — the ibProcUnit bytecode interpreter (dispatch, calls,
//                   branches, strings, LINQ). The "runtime" number.
//   NumberBench   — ibNumber arithmetic directly (immediate vs heap tier,
//                   ToString / FromString), with an int64 / double baseline.
//   ParserBench   — ibCompileCode::Compile throughput (ns/compile, lines/s).
//
// Every OES figure is printed next to a native-C++ baseline and the ratio
// (oes/base) so the numbers read as an *overhead factor*, not raw nanoseconds
// that only mean something on this exact CPU/build.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/byteCode.h"
#include "backend/compiler/codeDef.h"
#include "backend/compiler/value.h"
#include "backend/fnumber.h"

namespace {

// Written through volatile so the optimizer can't elide the timed loops.
volatile uint64_t g_sink = 0;

using Clock = std::chrono::steady_clock;

// Per-op timing for cheap, uniform operations (number ops, host->script calls):
// warmup, then time `iters` calls, return ns/op.
template <class F>
double TimeNsPerOp(long iters, F&& f) {
    for (long i = 0; i < iters / 10 + 1; ++i) f(i);                 // warmup
    const auto t0 = Clock::now();
    for (long i = 0; i < iters; ++i) f(i);
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / double(iters);
}

// Best (min) total ns over `repeats` runs of a single heavy unit of work
// (a whole-script call that loops/recurses internally). Min discards
// scheduler noise far better than mean for this shape.
template <class F>
double BestTotalNs(int repeats, F&& f) {
    f();                                                            // warmup
    double best = 1e300;
    for (int r = 0; r < repeats; ++r) {
        const auto t0 = Clock::now();
        f();
        const auto t1 = Clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return best;
}

// Format a nanosecond duration in human units (ns / us / ms / s).
std::string FmtWall(double ns) {
    std::ostringstream o; o << std::fixed << std::setprecision(2);
    if (ns < 1e3)      o << ns       << "ns";
    else if (ns < 1e6) o << ns / 1e3 << "us";
    else if (ns < 1e9) o << ns / 1e6 << "ms";
    else               o << ns / 1e9 << "s";
    return o.str();
}

// One labelled row: OES figure, native baseline, ratio (oes/base). When wall
// totals are given (>0), also print how long one full run of the scenario
// actually took — the "in seconds" view next to the per-op overhead factor.
void Row(const char* name, double oes, double base, const char* unit,
         double oesWallNs = 0, double baseWallNs = 0) {
    std::cout << "  " << std::left << std::setw(26) << name << std::right
              << "  oes=" << std::setw(10) << std::fixed << std::setprecision(1) << oes << unit
              << "  native=" << std::setw(10) << base << unit;
    if (base > 0)
        std::cout << "  x" << std::setprecision(1) << (oes / base);
    if (oesWallNs > 0)
        std::cout << "   [run: oes=" << FmtWall(oesWallNs)
                  << (baseWallNs > 0 ? "  native=" + FmtWall(baseWallNs) : std::string()) << "]";
    std::cout << "\n";
}

// OES-only row (no meaningful native equivalent, e.g. 200-digit decimal).
void RowOes(const char* name, double oes, const char* unit, double oesWallNs = 0) {
    std::cout << "  " << std::left << std::setw(26) << name << std::right
              << "  oes=" << std::setw(10) << std::fixed << std::setprecision(1) << oes << unit;
    if (oesWallNs > 0)
        std::cout << "   [run: oes=" << FmtWall(oesWallNs) << "]";
    std::cout << "\n";
}

bool Build(ibCompileCode& cc, const wxString& src) {
    try { return cc.Compile(src); } catch (...) { return false; }
}

} // namespace

// ===========================================================================
// RuntimeBench — the bytecode interpreter
// ===========================================================================

// Split into one test per scenario so a crash in one (SEH AV is not catchable
// by C++ try/catch) is isolated to that scenario and can be located by running
// a single --gtest_filter, instead of taking the whole group down.

// --- tight arithmetic loop: ns per loop-iteration -------------------------
// SumTo(n): While i<n { s += i; i += 1 } — per iter ~ compare + 2 adds +
// 2 stores + branch. Sum to 1e6 (~5e11) stays in ibNumber's immediate tier.
TEST(RuntimeBench, DISABLED_ArithLoop) {
    const long n = 1000000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function SumTo(n) Public\n")
        wxT("  var s; var i; s = 0; i = 0;\n")
        wxT("  While i < n Do\n")
        wxT("    s = s + i; i = i + 1;\n")
        wxT("  EndDo;\n")
        wxT("  Return s;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("SumTo"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });

    volatile int64_t s = 0;
    const double baseTot = BestTotalNs(5, [&]{ s = 0; for (long i = 0; i < n; ++i) s += i; g_sink += (uint64_t)s; });
    Row("arith loop (ns/iter)", oesTot / double(n), baseTot / double(n), "ns", oesTot, baseTot);
    SUCCEED();
}

// --- call-heavy recursion: ns per script call -----------------------------
// Fib(28): ~1.03M calls; the canonical interpreter call-overhead probe.
TEST(RuntimeBench, DISABLED_Recursion) {
    const int N = 28;
    const long calls = 1028457; // 2*Fib(29)-1
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Fib(n) Public\n")
        wxT("  If n < 2 Then Return n; EndIf;\n")
        wxT("  Return Fib(n - 1) + Fib(n - 2);\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN(N), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Fib"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });

    std::function<int64_t(int)> fib = [&](int n) -> int64_t { return n < 2 ? n : fib(n - 1) + fib(n - 2); };
    const double baseTot = BestTotalNs(5, [&]{ g_sink += (uint64_t)fib(N); });
    Row("recursion (ns/call)", oesTot / double(calls), baseTot / double(calls), "ns", oesTot, baseTot);
    SUCCEED();
}

// --- host->script call marshalling: ns per CallAsFunc ---------------------
// Trivial body so the figure is dominated by name lookup + frame setup +
// arg/return marshalling on the C++ <-> script boundary.
TEST(RuntimeBench, DISABLED_HostCall) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Inc(x) Public\n")
        wxT("  Return x + 1;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue arg(0), ret;
    const long iters = 200000;
    const double oes = TimeNsPerOp(iters, [&](long i){ arg = ibValue((int)i); pu.CallAsFunc(wxT("Inc"), ret, arg); g_sink += (uint64_t)ret.GetInteger(); });

    auto inc = [](int64_t x){ return x + 1; };
    const double base = TimeNsPerOp(iters, [&](long i){ g_sink += (uint64_t)inc(i); });
    Row("host->script (ns/call)", oes, base, "ns", oes * double(iters), base * double(iters));
    SUCCEED();
}

// --- string concat in a loop: ns per append -------------------------------
// `s = s + "x"` is O(n^2) (the whole string is copied each append), so n stays
// modest — measures per-append cost. This loop also pinned a runtime AV: AddValue
// pre-stamped the result type to STRING before SetString, so SetString's Reset()
// freed a stale union member when the result slot was a reused loop temp. Fixed
// in procUnit.cpp's AddValue; this stays as the regression guard.
TEST(RuntimeBench, DISABLED_StringConcat) {
    const long n = 2000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Cat(n) Public\n")
        wxT("  var s; var i; s = \"\"; i = 0;\n")
        wxT("  While i < n Do\n")
        wxT("    s = s + \"x\"; i = i + 1;\n")
        wxT("  EndDo;\n")
        wxT("  Return s;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Cat"), ret, argN); g_sink += (uint64_t)ret.GetString().length(); });
    EXPECT_EQ((long)ret.GetString().length(), n);   // "" + n*"x"

    const double baseTot = BestTotalNs(5, [&]{ std::string s; for (long i = 0; i < n; ++i) s += 'x'; g_sink += s.size(); });
    Row("string concat (ns/app)", oesTot / double(n), baseTot / double(n), "ns", oesTot, baseTot);
    SUCCEED();
}

// --- bytecode dump: SEE what `s = s + "x"` actually compiled to -------------
static const char* OpName(int base) {
    switch (base) {
        case OPER_NOP: return "NOP";     case OPER_ADD: return "ADD";
        case OPER_SUB: return "SUB";     case OPER_MULT: return "MULT";
        case OPER_DIV: return "DIV";     case OPER_MOD: return "MOD";
        case OPER_LET: return "LET";     case OPER_CONST: return "CONST";
        case OPER_CONSTN: return "CONSTN"; case OPER_IF: return "IF";
        case OPER_GOTO: return "GOTO";   case OPER_NEXT: return "NEXT";
        case OPER_FOR: return "FOR";     case OPER_FOREACH: return "FOREACH";
        case OPER_RET: return "RET";     case OPER_FUNC: return "FUNC";
        case OPER_ENDFUNC: return "ENDFUNC";
        case OPER_FUNC_PARAM: return "FUNC_PARAM";
        case OPER_FUNC_LOCAL: return "FUNC_LOCAL";
        case OPER_CTX_BEGIN: return "CTX_BEGIN"; case OPER_CTX_END: return "CTX_END";
        case OPER_CALL: return "CALL";
        case OPER_GT: return "GT"; case OPER_EQ: return "EQ"; case OPER_LS: return "LS";
        case OPER_GE: return "GE"; case OPER_LE: return "LE"; case OPER_NE: return "NE";
        case OPER_NOT: return "NOT"; case OPER_AND: return "AND"; case OPER_OR: return "OR";
        default: return "?";
    }
}

TEST(RuntimeBench, DISABLED_DumpBytecode) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Cat(n) Public\n")
        wxT("  var s; var i; s = \"\"; i = 0;\n")
        wxT("  While i < n Do\n")
        wxT("    s = s + \"x\"; i = i + 1;\n")
        wxT("  EndDo;\n")
        wxT("  Return s;\n")
        wxT("EndFunction\n")));
    const auto& code = cc.m_cByteCode.m_listCode;
    std::cout << "=== bytecode dump (" << code.size() << " ops)  TYPE_DELTA1=" << (int)TYPE_DELTA1
              << "  OPER_ADD=" << (int)OPER_ADD << " OPER_LET=" << (int)OPER_LET
              << "  [p=(array,index)] ===\n";
    for (size_t ip = 0; ip < code.size(); ++ip) {
        const auto& c = code[ip];
        const int raw  = (int)c.m_numOper;
        const int base = ((raw % TYPE_DELTA1) + TYPE_DELTA1) % TYPE_DELTA1;
        const int tier = (int)(c.m_numOper / TYPE_DELTA1);
        std::cout << std::right << std::setw(3) << ip << "  raw" << std::setw(5) << raw << "  "
                  << std::left << std::setw(10) << OpName(base)
                  << (tier == 0 ? "   " : tier == 1 ? "+n " : tier == 2 ? "+s " : tier == 3 ? "+d " : "+b ")
                  << "p1(" << (long long)c.m_param1.m_numArray << "," << (long long)c.m_param1.m_numIndex << ") "
                  << "p2(" << (long long)c.m_param2.m_numArray << "," << (long long)c.m_param2.m_numIndex << ") "
                  << "p3(" << (long long)c.m_param3.m_numArray << "," << (long long)c.m_param3.m_numIndex << ") "
                  << "p4(" << (long long)c.m_param4.m_numArray << "," << (long long)c.m_param4.m_numIndex << ")"
                  << std::right << "\n";
    }
    std::cout.flush();
    SUCCEED();
}

// --- LINQ pipeline: ns per element through Where+Select+Count -------------
TEST(RuntimeBench, DISABLED_LinqPipe) {
    const long n = 10000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Pipe(n) Public\n")
        wxT("  var arr; arr = New Array; var i; i = 0;\n")
        wxT("  While i < n Do arr.Add(i); i = i + 1; EndDo;\n")
        wxT("  Return arr.Where(Function(x) Return x > 100 EndFunction)")
        wxT(".Select(Function(x) Return x * 2 EndFunction).Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Pipe"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("LINQ build+pipe (ns/el)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// ===========================================================================
// NumberBench — ibNumber directly, vs int64 / double baselines
// ===========================================================================

TEST(NumberBench, DISABLED_Arithmetic) {
    std::cout << "\n[ NumberBench | ibNumber | x = oes/native overhead ]\n";
    const long N = 1000000;

    // --- immediate tier (stays inline, no heap) ---------------------------
    {
        const ibNumber a(123456), b(789);
        const double oes = TimeNsPerOp(N, [&](long){ ibNumber c = a; c += b; g_sink += (uint64_t)c.IsHeap(); });
        volatile int64_t x = 123456, y = 789;
        const double base = TimeNsPerOp(N, [&](long){ volatile int64_t c = x + y; g_sink += (uint64_t)c; });
        Row("add immediate", oes, base, "ns");
    }
    {
        const ibNumber a(123456), b(789);
        const double oes = TimeNsPerOp(N, [&](long){ ibNumber c = a; c *= b; g_sink += (uint64_t)c.IsHeap(); });
        volatile int64_t x = 123456, y = 789;
        const double base = TimeNsPerOp(N, [&](long){ volatile int64_t c = x * y; g_sink += (uint64_t)c; });
        Row("mul immediate", oes, base, "ns");
    }
    {
        const ibNumber a(123456), b(123457);
        const double oes = TimeNsPerOp(N, [&](long){ g_sink += (uint64_t)(a < b); });
        volatile int64_t x = 123456, y = 123457;
        const double base = TimeNsPerOp(N, [&](long){ g_sink += (uint64_t)(x < y); });
        Row("compare immediate", oes, base, "ns");
    }
    {
        // Non-exact: 10^6 / 7 is non-terminating -> full exact-decimal long division.
        const ibNumber a(1000000), b(7);
        const double oes = TimeNsPerOp(N / 2, [&](long){ ibNumber c = a; c /= b; g_sink += (uint64_t)c.IsHeap(); });
        volatile double x = 1000000.0, y = 7.0;
        const double base = TimeNsPerOp(N / 2, [&](long){ volatile double c = x / y; g_sink += (uint64_t)c; });
        Row("div non-exact (10^6/7)", oes, base, "ns");
    }
    {
        // Exact integer divide: 10^6 / 8 = 125000 -> immediate fast path.
        const ibNumber a(1000000), b(8);
        const double oes = TimeNsPerOp(N, [&](long){ ibNumber c = a; c /= b; g_sink += (uint64_t)c.IsHeap(); });
        volatile int64_t x = 1000000, y = 8;
        const double base = TimeNsPerOp(N, [&](long){ volatile int64_t c = x / y; g_sink += (uint64_t)c; });
        Row("div exact int (fast)", oes, base, "ns");
    }

    // --- heap / big-decimal tier (no native equivalent) -------------------
    {
        const ibNumber a(wxString(wxT("123456789012345678901234567890")));
        const ibNumber b(wxString(wxT("987654321098765432109876543210")));
        const double oes = TimeNsPerOp(N / 10, [&](long){ ibNumber c = a; c *= b; g_sink += (uint64_t)c.IsHeap(); });
        RowOes("mul 30x30 digits (heap)", oes, "ns");
    }
    {
        // 200 fractional digits — the high-precision tier accounting work needs.
        wxString big(wxT("0.")); for (int i = 0; i < 200; ++i) big += wxChar(wxT('0') + (i % 9) + 1);
        const ibNumber a(big), b(wxString(wxT("3")));
        const double oes = TimeNsPerOp(N / 20, [&](long){ ibNumber c = a; c *= b; g_sink += (uint64_t)c.IsHeap(); });
        RowOes("mul 200-frac-digit", oes, "ns");
    }

    // --- ToString / FromString round-trip ---------------------------------
    {
        const ibNumber a(wxString(wxT("1234567.89")));
        const double oes = TimeNsPerOp(N / 5, [&](long){ g_sink += (uint64_t)a.ToString().length(); });
        volatile double x = 1234567.89;
        const double base = TimeNsPerOp(N / 5, [&](long){ g_sink += std::to_string((double)x).length(); });
        Row("ToString", oes, base, "ns");
    }
    {
        const double oes = TimeNsPerOp(N / 5, [&](long){ ibNumber n; n.FromString(wxT("1234567.89")); g_sink += (uint64_t)n.IsHeap(); });
        const double base = TimeNsPerOp(N / 5, [&](long){ volatile double d = std::stod("1234567.89"); g_sink += (uint64_t)d; });
        Row("FromString (parse)", oes, base, "ns");
    }

    EXPECT_NE(g_sink, 0xFFFFFFFFFFFFFFFFull);
    SUCCEED();
}

// ===========================================================================
// ParserBench — ibCompileCode::Compile throughput
// ===========================================================================

TEST(ParserBench, DISABLED_CompileThroughput) {
    std::cout << "\n[ ParserBench | ibCompileCode::Compile ]\n";

    auto benchModule = [](const char* label, const wxString& src) {
        long lines = 1; for (size_t i = 0; i < src.length(); ++i) if (src[i] == wxT('\n')) ++lines;
        const double bytes = double(src.length());
        const double nsPer = BestTotalNs(20, [&]{
            ibCompileCode cc(wxT("test"), wxT("memory"), false);
            try { g_sink += cc.Compile(src) ? 1u : 0u; } catch (...) {}
        });
        std::cout << "  " << std::left << std::setw(26) << label << std::right
                  << "  " << std::setw(8) << std::fixed << std::setprecision(1) << (nsPer / 1000.0) << "us"
                  << "   " << std::setw(8) << std::setprecision(0) << (lines * 1e9 / nsPer) << " lines/s"
                  << "   " << std::setw(8) << (bytes * 1e9 / nsPer / 1024.0) << " KB/s\n";
    };

    // small — a handful of declarations + control flow (typical handler size).
    benchModule("small (~15 lines)",
        wxT("var total public;\n")
        wxT("Function Square(x) Public Return x * x; EndFunction\n")
        wxT("Function Clamp(v, lo, hi) Public\n")
        wxT("  If v < lo Then Return lo; EndIf;\n")
        wxT("  If v > hi Then Return hi; EndIf;\n")
        wxT("  Return v;\n")
        wxT("EndFunction\n")
        wxT("total = 0;\n")
        wxT("var i; i = 0;\n")
        wxT("While i < 10 Do total = total + Square(i); i = i + 1; EndDo;\n"));

    // large — a synthetic module of repeated functions (parser/codegen scaling).
    {
        wxString big;
        for (int k = 0; k < 200; ++k) {
            big += wxString::Format(
                wxT("Function F%d(a, b, c) Public\n")
                wxT("  var r; r = a + b * c;\n")
                wxT("  If r > 100 Then r = r - 100; Else r = r + 1; EndIf;\n")
                wxT("  Return r;\n")
                wxT("EndFunction\n"), k);
        }
        benchModule("large (200 funcs)", big);
    }

    EXPECT_NE(g_sink, 0xFFFFFFFFFFFFFFFFull);
    SUCCEED();
}
