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
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

// NAMES ITS FAILURE. A benchmark that will not compile printed "Actual: false"
// and nothing else — which is how DISABLED_LinqJoin sat red in CI while the
// numbers around it were read as fine. The compiler knows why; keep the message.
::testing::AssertionResult Build(ibCompileCode& cc, const wxString& src) {
    try {
        if (cc.Compile(src))
            return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure() << "Compile() returned false without raising";
    } catch (const ibBackendException& err) {
        return ::testing::AssertionFailure() << err.GetErrorDescription().ToStdString();
    } catch (...) {
        return ::testing::AssertionFailure() << "unknown exception";
    }
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

// A LAMBDA INSIDE A PIPELINE LAMBDA, on the tape.
//
// The runtime side of the capture was fixed by making a heap-promoted frame OWN
// its arguments; a green test alone does not prove the two halves agree, because
// the right answer can come out of the wrong layout. This dump is the other half:
// it says whether the compiler flagged the OUTER lambda `m_needsHeapFrame`, and
// at what DEPTH the inner body reads the outer parameter — the depth has to line
// up with where ibValueFunction::Execute installs m_capturedFrames (layers
// [1..N], everything pre-existing shifted to [N+1..]).
//
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*DumpNestedLambda*
TEST(RuntimeBench, DISABLED_DumpNestedLambda) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var a public; var r public;\n")
        wxT("a = New Array;\n")
        wxT("a.Add(10); a.Add(20);\n")
        wxT("r = a.SelectMany(Function(x)\n")
        wxT("      Return a.Where(Function(y) Return y > x EndFunction);\n")
        wxT("    EndFunction).Count();\n")));

    const auto& bc = cc.m_cByteCode;

    std::cout << "=== functions (" << bc.m_listFunc.size() << ") ===\n";
    for (size_t i = 0; i < bc.m_listFunc.size(); ++i) {
        const auto& fn = bc.m_listFunc[i];
        std::cout << std::right << std::setw(3) << i
                  << "  name='" << (const char*)fn.m_strRealName.ToUTF8() << "'"
                  << "  params=" << fn.m_listParam.size()
                  << "  vars=" << fn.m_lVarCount
                  << "  line=" << fn.m_lCodeLine
                  << "  heapFrame=" << (fn.m_needsHeapFrame ? "YES" : "no")
                  << "  lambda=" << (fn.IsLambda() ? "yes" : "no")
                  << "\n";
    }

    const auto& code = bc.m_listCode;
    std::cout << "=== bytecode (" << code.size() << " ops)  "
                 "[p1(array,index) — array is the FRAME DEPTH for a variable read] ===\n";
    for (size_t ip = 0; ip < code.size(); ++ip) {
        const auto& c = code[ip];
        const int raw  = (int)c.m_numOper;
        const int base = ((raw % TYPE_DELTA1) + TYPE_DELTA1) % TYPE_DELTA1;
        std::cout << std::right << std::setw(3) << ip << "  " << std::left << std::setw(12) << OpName(base)
                  << "p1(" << (long long)c.m_param1.m_numArray << "," << (long long)c.m_param1.m_numIndex << ") "
                  << "p2(" << (long long)c.m_param2.m_numArray << "," << (long long)c.m_param2.m_numIndex << ") "
                  << "p3(" << (long long)c.m_param3.m_numArray << "," << (long long)c.m_param3.m_numIndex << ") "
                  << "p4(" << (long long)c.m_param4.m_numArray << "," << (long long)c.m_param4.m_numIndex << ")"
                  << std::right << "\n";
    }
    std::cout.flush();
    SUCCEED();
}

// THE THIN LAMBDA, on the tape — the exact source DISABLED_LinqOneLambda runs.
//
// A profile of that bench put `operator new` at 11.7% of the run with 96% of the
// calls coming from CallLambdaWithArgs, i.e. the FRAME is reaching the heap on
// every invocation. Two things there can allocate and they are told apart by two
// printed numbers, not by reading:
//   vars > MAX_STATIC_VAR (25 on x64) -> SetLocalCount spills to `new ibValue[]`
//   heapFrame = YES                   -> make_shared, one per call
// A lambda with no inner lambda must show neither.
TEST(RuntimeBench, DISABLED_DumpThinLambda) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var arr public;\n")
        wxT("Procedure Fill(n) Public\n")
        wxT("  arr = New Array; var i; i = 0;\n")
        wxT("  While i < n Do arr.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Function Pipe() Public\n")
        wxT("  Return arr.Where(Function(x) Return x > 100 EndFunction).Count();\n")
        wxT("EndFunction\n")));

    const auto& bc = cc.m_cByteCode;
    std::cout << "=== MAX_STATIC_VAR=" << (long long)MAX_STATIC_VAR
              << "  functions (" << bc.m_listFunc.size() << ") ===\n";
    for (size_t i = 0; i < bc.m_listFunc.size(); ++i) {
        const auto& fn = bc.m_listFunc[i];
        std::cout << std::right << std::setw(3) << i
                  << "  name='" << (const char*)fn.m_strRealName.ToUTF8() << "'"
                  << "  params=" << fn.m_listParam.size()
                  << "  vars=" << fn.m_lVarCount
                  << "  heapFrame=" << (fn.m_needsHeapFrame ? "YES" : "no")
                  << "  lambda=" << (fn.IsLambda() ? "yes" : "no")
                  << "  spills=" << (fn.m_lVarCount > MAX_STATIC_VAR ? "YES" : "no")
                  << "\n";
    }
    std::cout.flush();
    SUCCEED();
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

// --- the PIPELINE alone: ns per element, with the source already built ----
//
// The row above is a BLEND — building the array costs one `arr.Add()` per
// element (a method call on a wide surface, ~230 ns by the row below) plus the
// loop's own arithmetic, and that is most of it. A blended number cannot say
// whether the pipeline is slow, so it cannot say what to optimise: the answer
// came out as "two lambda invocations", and only a split row shows that.
//
// The array is built ONCE, into a module-level export, and the timed function
// only pipes over it.
TEST(RuntimeBench, DISABLED_LinqPipeOnly) {
    const long n = 10000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var arr public;\n")
        wxT("Procedure Fill(n) Public\n")
        wxT("  arr = New Array; var i; i = 0;\n")
        wxT("  While i < n Do arr.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Function Pipe() Public\n")
        wxT("  Return arr.Where(Function(x) Return x > 100 EndFunction)")
        wxT(".Select(Function(x) Return x * 2 EndFunction).Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());

    ibValue argN((int)n), ret;
    pu.CallAsProc(wxT("Fill"), argN);

    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Pipe"), ret); g_sink += (uint64_t)ret.GetInteger(); });
    // TWO lambdas — Where AND Select. The old label said only "pipe only", and it
    // gets quoted as "what a pipeline costs"; at 198.8 against 100.8 for the
    // one-lambda row it is exactly twice, which is the whole point.
    RowOes("LINQ pipe 2 lambdas (ns/el)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// --- ONE lambda over the same source: the unit the row above is made of ---
//
// Where+Select is two invocations per element; this is one. The difference
// between the two rows is the price of a lambda call inside a pipeline, which
// is the thing to attack — the pipeline states themselves are lazy and hold a
// couple of shared_ptrs.
TEST(RuntimeBench, DISABLED_LinqOneLambda) {
    const long n = 10000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var arr public;\n")
        wxT("Procedure Fill(n) Public\n")
        wxT("  arr = New Array; var i; i = 0;\n")
        wxT("  While i < n Do arr.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Function Pipe() Public\n")
        wxT("  Return arr.Where(Function(x) Return x > 100 EndFunction).Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());

    ibValue argN((int)n), ret;
    pu.CallAsProc(wxT("Fill"), argN);

    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Pipe"), ret); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("LINQ one lambda (ns/el)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// --- a query-block JOIN: ns per output row --------------------------------
//
// There was no row for this because a query-block join did not EXECUTE: the
// compiler emitted `Join(inner, onCond)` and the runtime wants
// `(inner, leftKey, rightKey, projection)`, so it raised at the first row. It
// runs as of 2026-08-09, and what it costs is now a question that can be asked.
//
// What to expect from the parts already measured: three lambda invocations per
// row (leftKey, rightKey, projection) at ~150 ns each, plus one ibValueStructure
// built per row — whose fields live in a std::map whose comparator uppercases
// BOTH sides into fresh strings on every comparison. That comparator was
// theoretical while nothing put a Structure on the pipeline; the composite row
// puts it there.
TEST(RuntimeBench, DISABLED_LinqJoin) {
    const long n = 2000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var outer public; var inner public;\n")
        wxT("Procedure Fill(n) Public\n")
        wxT("  outer = New Array; inner = New Array; var i; i = 0;\n")
        wxT("  While i < n Do outer.Add(i); inner.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        // THROUGH A VARIABLE, because a method call on a PARENTHESISED expression
        // is not part of this language: `(expr).Method()` fails to parse whatever
        // the expression is — `(1 + 2).ToString()` and `(a).Count()` die the same
        // way ("Keyword or identifier expected"). Postfix applies to an
        // identifier, not to an arbitrary primary. The query itself is fine; only
        // the shorthand was, and it came from a compiler that is no longer here.
        wxT("Function Pipe() Public\n")
        wxT("  var q; q = from a in outer join b in inner on a equals b select a;\n")
        wxT("  Return q.Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());

    ibValue argN((int)n), ret;
    pu.CallAsProc(wxT("Fill"), argN);

    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Pipe"), ret); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("LINQ join (ns/row)", oesTot / double(n), "ns", oesTot);

    // THE SHAPE OF THE COST, not another guess about it.
    //
    // Decomposition by arithmetic left ~2900 of the 3973 ns/row unexplained, and
    // three explanations fit that number equally well: the index is a
    // std::map (a red-black tree, despite the name `m_hash`), so lookups are
    // O(log N) ibValue comparisons; or the per-row constant work dominates
    // (three lambda calls + one composite Structure + the ibValue copies); or
    // something in the path is worse than logarithmic.
    //
    // n tells them apart, and nothing else has to be true for the reading to
    // mean something:
    //   flat            -> per-row constant work; the container is innocent
    //   +~log N         -> the tree comparisons are real and unordered_map pays
    //   linear in n     -> something is O(N) per row and THAT is the bug
    for (long rows : { 250L, 1000L, 4000L, 16000L }) {
        ibValue argRows((int)rows), retScale;
        pu.CallAsProc(wxT("Fill"), argRows);
        const double tot = BestTotalNs(3, [&]{
            pu.CallAsFunc(wxT("Pipe"), retScale); g_sink += (uint64_t)retScale.GetInteger(); });
        std::ostringstream label;
        label << "LINQ join n=" << rows << " (ns/row)";
        RowOes(label.str().c_str(), tot / double(rows), "ns", tot);
    }
    SUCCEED();
}

// --- a query block with NO join, at scale — isolates the base pipeline ------
//
// `from a in outer select a` — one foreach over the source, one Add per row,
// no index, no probe, no bucket. If THIS is linear-in-n per row too, the
// quadratic the join bench shows is NOT the join: it is the block's own
// per-row machinery (the source iterator or the result Array.Add), and the
// join is innocent. If this is flat, the join adds the quadratic. The cut the
// join scale bench above could not make on its own.
TEST(RuntimeBench, DISABLED_LinqBlockSelectScale) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var outer public;\n")
        wxT("Procedure Fill(n) Public\n")
        wxT("  outer = New Array; var i; i = 0;\n")
        wxT("  While i < n Do outer.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Function Pipe() Public\n")
        wxT("  var q; q = from a in outer select a;\n")
        wxT("  Return q.Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue ret;
    for (long rows : { 250L, 1000L, 4000L, 16000L }) {
        ibValue argRows((int)rows), retScale;
        pu.CallAsProc(wxT("Fill"), argRows);
        const double tot = BestTotalNs(3, [&]{
            pu.CallAsFunc(wxT("Pipe"), retScale); g_sink += (uint64_t)retScale.GetInteger(); });
        std::ostringstream label;
        label << "block select n=" << rows << " (ns/row)";
        RowOes(label.str().c_str(), tot / double(rows), "ns", tot);
    }
    SUCCEED();
}

// --- join with a FIXED inner, varying outer — build-once vs rebuild ---------
//
// The base pipeline is flat and the map probe is ~log N, so a build-once index
// makes the join O(n log n); the join scale bench measures O(n^2). The missing
// O(N) per row can only be a per-outer-row REBUILD of the index. This isolates
// it: inner is fixed at 2000, only outer grows, and the row is per OUTER row.
//   build once  -> the 2000-row build amortises over more outer rows as outer
//                  grows, so ns/outer-row FALLS.
//   rebuild     -> every outer row pays the whole 2000-row build, so
//                  ns/outer-row stays FLAT and high, independent of outer.
TEST(RuntimeBench, DISABLED_LinqJoinFixedInner) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var outer public; var inner public;\n")
        wxT("Procedure FillInner(n) Public\n")
        wxT("  inner = New Array; var i; i = 0;\n")
        wxT("  While i < n Do inner.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Procedure FillOuter(n) Public\n")
        wxT("  outer = New Array; var i; i = 0;\n")
        wxT("  While i < n Do outer.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Function Pipe() Public\n")
        wxT("  var q; q = from a in outer join b in inner on a equals b select a;\n")
        wxT("  Return q.Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argInner((int)2000), ret;
    pu.CallAsProc(wxT("FillInner"), argInner);
    for (long outerN : { 250L, 1000L, 4000L, 16000L }) {
        ibValue argOuter((int)outerN), retScale;
        pu.CallAsProc(wxT("FillOuter"), argOuter);
        const double tot = BestTotalNs(3, [&]{
            pu.CallAsFunc(wxT("Pipe"), retScale); g_sink += (uint64_t)retScale.GetInteger(); });
        std::ostringstream label;
        label << "join inner=2000 outer=" << outerN << " (ns/outer-row)";
        RowOes(label.str().c_str(), tot / double(outerN), "ns", tot);
    }
    SUCCEED();
}

// --- a query block GROUP BY at high cardinality — the same index, verified --
//
// group-by builds the SAME kind of key->bucket hash the join does (per-row
// Property + Insert), so it carried the same O(N^2) member-table thrash. One
// distinct group per row is the worst case (every row inserts, every insert
// invalidates the surface, every next row rebuilds it). If the @Index fix took,
// this is flat per row; a plain Container makes it grow linearly with n.
TEST(RuntimeBench, DISABLED_LinqGroupByScale) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var src public;\n")
        wxT("Procedure Fill(n) Public\n")
        wxT("  src = New Array; var i; i = 0;\n")
        wxT("  While i < n Do src.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Function Pipe() Public\n")
        wxT("  var q; q = from a in src group a by a into g select g;\n")
        wxT("  Return q.Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue ret;
    for (long rows : { 250L, 1000L, 4000L, 16000L }) {
        ibValue argRows((int)rows), retScale;
        pu.CallAsProc(wxT("Fill"), argRows);
        const double tot = BestTotalNs(3, [&]{
            pu.CallAsFunc(wxT("Pipe"), retScale); g_sink += (uint64_t)retScale.GetInteger(); });
        std::ostringstream label;
        label << "group by n=" << rows << " (ns/row)";
        RowOes(label.str().c_str(), tot / double(rows), "ns", tot);
    }
    SUCCEED();
}

// --- the join INDEX on its own, with no interpreter above it --------------
//
// ibValueJoinState calls its index `m_hash`, but it is a
// `std::map<ibValue, std::vector<ibValue>, KeyCmp>` — a red-black tree. Every
// probe is O(log N) virtual `CompareValueLS` calls, and every distinct key
// allocates a map node AND a vector.
//
// Reading that off the script-level number is impossible: three lambda calls, a
// composite Structure and the pipeline machinery sit on top of it. So the
// container is measured ALONE here, in the exact shape the join builds, against
// the same container keyed by a plain long. The subtraction is the answer:
//
//   long map          -> what a red-black tree of this size costs, full stop
//   ibValue map       -> the same tree paying ibValue construction, copying and
//                        virtual three-way comparison
//
// If the two are close, the key type is innocent and the tree is the cost (swap
// in unordered_map). If ibValue is several times the long, the comparison and
// the copies are the cost and the container choice is secondary.
TEST(RuntimeBench, DISABLED_JoinIndexContainer) {
    struct KeyCmp {   // byte-for-byte the comparator ibValueJoinState uses
        bool operator()(const ibValue& a, const ibValue& b) const { return a < b; }
    };

    for (long n : { 2000L, 16000L }) {

        std::map<ibValue, std::vector<ibValue>, KeyCmp> mapValue;
        const double buildValue = BestTotalNs(3, [&]{
            mapValue.clear();
            for (long i = 0; i < n; ++i) {
                ibValue key((int)i);
                mapValue[key].push_back(key);
            }
            g_sink += mapValue.size();
        });

        // Two probe rows, because the first version of this measured BOTH the
        // lookup and the construction of the key it looks up with, then reported
        // the sum as "the comparison". The join pays both — its leftKey lambda
        // hands over a fresh ibValue every row — but they are fixed in different
        // places, so they are separated here.
        std::vector<ibValue> listKey;
        listKey.reserve((size_t)n);
        for (long i = 0; i < n; ++i) listKey.emplace_back((int)i);

        const double probeValue = BestTotalNs(5, [&]{
            for (long i = 0; i < n; ++i) {
                auto it = mapValue.find(listKey[(size_t)i]);   // key already built
                if (it != mapValue.end()) g_sink += it->second.size();
            }
        });

        const double makeKey = BestTotalNs(5, [&]{
            for (long i = 0; i < n; ++i) {
                const ibValue key((int)i);
                g_sink += (uint64_t)key.GetInteger();
            }
        });

        std::map<long, std::vector<long>> mapLong;
        const double buildLong = BestTotalNs(3, [&]{
            mapLong.clear();
            for (long i = 0; i < n; ++i) mapLong[i].push_back(i);
            g_sink += mapLong.size();
        });

        const double probeLong = BestTotalNs(5, [&]{
            for (long i = 0; i < n; ++i) {
                auto it = mapLong.find(i);
                if (it != mapLong.end()) g_sink += it->second.size();
            }
        });

        std::ostringstream l1, l2, l3, l4, l5;
        l1 << "index build  ibValue n=" << n << " (ns/key)";
        l2 << "index probe   ibValue n=" << n << " (ns/probe)";
        l3 << "index build  long    n=" << n << " (ns/key)";
        l4 << "index probe   long    n=" << n << " (ns/probe)";
        l5 << "make one ibValue key n=" << n << " (ns)";
        RowOes(l1.str().c_str(), buildValue / double(n), "ns", buildValue);
        RowOes(l2.str().c_str(), probeValue / double(n), "ns", probeValue);
        RowOes(l3.str().c_str(), buildLong  / double(n), "ns", buildLong);
        RowOes(l4.str().c_str(), probeLong  / double(n), "ns", probeLong);
        RowOes(l5.str().c_str(), makeKey    / double(n), "ns", makeKey);
    }
    SUCCEED();
}

// --- Structure: reading a field by name ----------------------------------
//
// A Structure is what a LINQ row IS on the database side (valueQueryable's
// RowValue builds one per row) and what a composite pipeline row WOULD be. Its
// fields live in a std::map keyed by ibValue, and the comparator uppercases BOTH
// sides into fresh wxStrings on every comparison — so one field read is
// O(log n) comparisons x two allocations. This row is what says whether that
// costs anything worth removing.
TEST(RuntimeBench, DISABLED_StructFieldRead) {
    const long n = 200000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Read(n) Public\n")
        wxT("  var s; s = New Structure(\"Alpha, Beta, Gamma\", 1, 2, 3);\n")
        wxT("  var i; var t; i = 0; t = 0;\n")
        wxT("  While i < n Do t = t + s.Beta; i = i + 1; EndDo;\n")
        wxT("  Return t;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Read"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("struct field read (ns)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// --- Structure: building one ---------------------------------------------
// Every Insert calls m_members.Invalidate(), so a 3-field structure rebuilds
// its member table three times before anyone reads it.
TEST(RuntimeBench, DISABLED_StructBuild) {
    const long n = 100000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Make(n) Public\n")
        wxT("  var i; var s; i = 0;\n")
        wxT("  While i < n Do s = New Structure(\"Alpha, Beta, Gamma\", i, i, i); i = i + 1; EndDo;\n")
        wxT("  Return i;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Make"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("struct build 3 fields (ns)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// --- Array: append and index ---------------------------------------------
// `arr.Add(i)` is the other half of the LINQ blend row. Splitting it from the
// loop's arithmetic says whether the cost is the STORAGE or the method
// DISPATCH — the surface has 15 methods, above the hash-index threshold.
TEST(RuntimeBench, DISABLED_ArrayAdd) {
    const long n = 200000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Fill(n) Public\n")
        wxT("  var arr; arr = New Array; var i; i = 0;\n")
        wxT("  While i < n Do arr.Add(i); i = i + 1; EndDo;\n")
        wxT("  Return arr.Count();\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Fill"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("array Add (ns)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

TEST(RuntimeBench, DISABLED_ArrayIndex) {
    const long n = 200000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Walk(n) Public\n")
        wxT("  var arr; arr = New Array; var i; i = 0;\n")
        wxT("  While i < 1000 Do arr.Add(i); i = i + 1; EndDo;\n")
        // NO `%` IN THE TIMED LOOP. `i % 1000` looked like a harmless way to stay
        // in range, but ibNumber's immediate fast path covers + - * / and compare
        // and NOT the remainder, so the modulo goes through exact long division
        // and dominates the row — the measurement would have been named "array
        // index" and have reported the cost of `%`. A counter reset costs a
        // compare and an assignment, both already priced elsewhere.
        wxT("  var t; var k; t = 0; i = 0; k = 0;\n")
        wxT("  While i < n Do\n")
        wxT("    t = t + arr[k]; k = k + 1;\n")
        wxT("    If k > 999 Then k = 0; EndIf;\n")
        wxT("    i = i + 1;\n")
        wxT("  EndDo;\n")
        wxT("  Return t;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Walk"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("array [i] read (ns)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// --- the SOURCE walked with no lambda at all: the pipeline's own floor ----
//
// `arr.Count()` after a `Take` that keeps everything: iterator machinery, no
// user code. Whatever this costs is what a pipeline charges before the first
// script line runs, and it is the number that says whether the states are worth
// touching at all.
TEST(RuntimeBench, DISABLED_LinqNoLambda) {
    const long n = 10000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("var arr public;\n")
        wxT("Procedure Fill(n) Public\n")
        wxT("  arr = New Array; var i; i = 0;\n")
        wxT("  While i < n Do arr.Add(i); i = i + 1; EndDo;\n")
        wxT("EndProcedure\n")
        wxT("Function Pipe() Public\n")
        // The count comes FROM n. It used to be the literal 10000 in the script
        // while the row divided by `n` — equal today, and silently wrong for
        // everyone the moment somebody edits one of the two. Same shape as the
        // row that was called "call frame" and measured no frame.
        + wxString::Format(wxT("  Return arr.Take(%ld).Count();\n"), n) +
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());

    ibValue argN((int)n), ret;
    pu.CallAsProc(wxT("Fill"), argN);

    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Pipe"), ret); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("LINQ no lambda (ns/el)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// --- method resolve on a WIDE surface: ns per obj.Method() ----------------
// Every scenario above resolves names against a tiny surface (host->script
// looks one function up in a table of one), so all of them take
// ibMemberTable's linear path. Array surfaces 15 methods — above
// kFindIndexMin (12) — so this one goes through the hash INDEX instead, which
// is where a lookup used to pay `name.Upper().ToStdWstring()`. Without this
// row the suite cannot see a change to name resolution at all.
TEST(RuntimeBench, DISABLED_MethodResolve) {
    const long n = 200000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Resolve(n) Public\n")
        // Get, NOT Count: Count is an ibLinqMethod, so the compiler emits
        // OPER_CALL_LINQ for it and the runtime dispatches on an enum id with
        // no name lookup at all — the opposite of what this bench is for. Get
        // is an ordinary method, so it goes OPER_CALL_METHOD -> FindMethod ->
        // the hash index (Array surfaces 15 methods, above kFindIndexMin).
        wxT("  var arr; arr = New Array; arr.Add(1);\n")
        wxT("  var i; i = 0; var s; s = 0;\n")
        wxT("  While i < n Do s = arr.Get(0); i = i + 1; EndDo;\n")
        wxT("  Return i;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    ibValue argN((int)n), ret;
    const double oesTot = BestTotalNs(5, [&]{ pu.CallAsFunc(wxT("Resolve"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
    RowOes("method resolve (ns/call)", oesTot / double(n), "ns", oesTot);
    SUCCEED();
}

// --- a shape closer to GENERATED code -------------------------------------
// The five scenarios above are microbenchmarks: a tight arithmetic loop, a
// recursion, a concatenation. They were chosen for what stresses the
// interpreter, not for what real configuration code looks like — and least of
// all for what a model writes.
//
// Generated code has a different profile: it builds records rather than
// querying them, walks collections in memory rather than grouping in SQL, and
// reaches fields through the dot far more often than it does arithmetic. This
// row is that shape — build N structures, then walk them summing two fields —
// so the mix is New + method calls + OPER_GET_A + arithmetic, in the
// proportions generated code actually produces.
//
// Reported per ROW, so the number is "what one record costs to build and
// process", which is the unit a configuration author thinks in.
TEST(RuntimeBench, DISABLED_RecordWalk) {
    const long n = 20000;
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ASSERT_TRUE(Build(cc,
        wxT("Function Walk(n) Public\n")
        wxT("  var rows; rows = New Array;\n")
        wxT("  var i; i = 0;\n")
        wxT("  While i < n Do\n")
        wxT("    var row; row = New Structure;\n")
        wxT("    row.Insert(\"Qty\", i);\n")
        wxT("    row.Insert(\"Price\", 2);\n")
        wxT("    rows.Add(row);\n")
        wxT("    i = i + 1;\n")
        wxT("  EndDo;\n")
        wxT("  var total; total = 0; var j; j = 0;\n")
        wxT("  While j < n Do\n")
        wxT("    var r; r = rows.Get(j);\n")
        wxT("    total = total + r.Qty * r.Price;\n")
        wxT("    j = j + 1;\n")
        wxT("  EndDo;\n")
        wxT("  Return total;\n")
        wxT("EndFunction\n")));
    ibProcUnit pu; ASSERT_TRUE([&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }());
    // Scaling probe: per-row cost must stay FLAT as n grows. If it climbs with
    // n, something in the path is quadratic — a growing collection copied per
    // append, or a linear lookup per access.
    for (long rows : { 2500L, 5000L, 10000L, 20000L }) {
        ibValue argN((int)rows), ret;
        const double tot = BestTotalNs(3, [&]{ pu.CallAsFunc(wxT("Walk"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
        std::ostringstream label;
        label << "record build+walk n=" << rows << " (ns/row)";
        RowOes(label.str().c_str(), tot / double(rows), "ns", tot);
    }
    SUCCEED();
}

// --- breaking the record walk into its parts ------------------------------
// RecordWalk came out at ~35 us per row while each of its ~8 operations
// measures ~200 ns elsewhere — an 18x discrepancy that no single opcode
// explains. Rather than guess, each step is timed on its own here, at the same
// n, so the expensive one names itself.
TEST(RuntimeBench, DISABLED_RecordParts) {
    const long n = 5000;
    struct Part { const char* name; const wxChar* body; };
    const Part parts[] = {
        { "New Structure only",
          wxT("Function P(n) Public\n var i; i = 0;\n While i < n Do var s; s = New Structure; i = i + 1; EndDo;\n Return i;\nEndFunction\n") },
        { "New + 2 Insert",
          wxT("Function P(n) Public\n var i; i = 0;\n While i < n Do var s; s = New Structure; s.Insert(\"Qty\", i); s.Insert(\"Price\", 2); i = i + 1; EndDo;\n Return i;\nEndFunction\n") },
        { "Array.Add only",
          wxT("Function P(n) Public\n var a; a = New Array; var i; i = 0;\n While i < n Do a.Add(i); i = i + 1; EndDo;\n Return i;\nEndFunction\n") },
        { "Array.Get only",
          wxT("Function P(n) Public\n var a; a = New Array; a.Add(1); var i; i = 0; var v;\n While i < n Do v = a.Get(0); i = i + 1; EndDo;\n Return i;\nEndFunction\n") },
        { "dot access on Structure",
          wxT("Function P(n) Public\n var s; s = New Structure; s.Insert(\"Qty\", 3); var i; i = 0; var v;\n While i < n Do v = s.Qty; i = i + 1; EndDo;\n Return i;\nEndFunction\n") },
    };
    for (const Part& p : parts) {
        ibCompileCode cc(wxT("test"), wxT("memory"), false);
        if (!Build(cc, p.body)) { std::cout << "  (compile failed: " << p.name << ")\n"; continue; }
        ibProcUnit pu;
        if (![&]{ try { pu.Execute(cc.m_cByteCode); return true; } catch (...) { return false; } }()) {
            std::cout << "  (execute failed: " << p.name << ")\n"; continue;
        }
        ibValue argN((int)n), ret;
        const double tot = BestTotalNs(3, [&]{ pu.CallAsFunc(wxT("P"), ret, argN); g_sink += (uint64_t)ret.GetInteger(); });
        RowOes(p.name, tot / double(n), "ns", tot);
    }
    SUCCEED();
}

// --- what a call frame costs, on its own ----------------------------------
// recursion measures 318 ns/call while an arithmetic opcode is ~15 ns, so the
// call is worth ~21 opcodes and nothing in the suite says why. ibRunContextSmall
// carries `ibValue m_cLocVars[MAX_STATIC_VAR]` (25) plus a pointer row of the
// same length, and ibValue has a virtual destructor — so entering ANY function
// value-initialises 25 objects and leaving it runs 25 destructors, whether the
// function declares three locals or twenty-five.
//
// The baseline column is the honest counterfactual: the same work if only the
// slots actually used were built. The gap between the two IS the upper bound on
// what a zero-cost-frame rewrite (raw storage + placement new, the CPython 3.11
// move) could return — measured, not argued.
TEST(RuntimeBench, DISABLED_FrameCost) {
    const long n = 500000;

    // ⚠ THIS ROW DOES NOT MEASURE A CALL FRAME, and it used to say it did.
    //
    // `ibRunContextSmall`'s dtor is not exported from backend.dll, so a frame
    // cannot be built from a test TU. What runs below is 25 `ibValue` constructed
    // and destroyed in a local array against 3 — the SLOT COST alone, with no
    // frame, no call, no interpreter.
    //
    // It was named "call frame" and captioned "what every call builds today", and
    // both stopped being true on 2026-08-09 when the frame started sizing itself
    // to the function's real local count. The name then actively misled: on
    // 2026-08-10 removing a per-frame `std::map` dropped `recursion` −20% and
    // `host->script` −15% while this row did not move at all — correctly, because
    // it exercises none of that. A benchmark whose name promises more than it
    // measures costs an hour the first time someone trusts it.
    //
    // The real cost of a call is `recursion` and `host->script`; this row is only
    // the slot arithmetic that motivated sizing frames exactly.
    const double slots25 = TimeNsPerOp(n, [&](long i){
        ibValue slots[MAX_STATIC_VAR];             // the frame's inline capacity
        g_sink += (uint64_t)(i & 1);
        (void)slots;
    });

    const double slots3 = TimeNsPerOp(n, [&](long i){
        ibValue slots[3];                          // what a typical function declares
        g_sink += (uint64_t)(i & 1);
        (void)slots;
    });

    Row("25 vs 3 ibValue slots (ns)", slots25, slots3, "ns",
        slots25 * double(n), slots3 * double(n));
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
