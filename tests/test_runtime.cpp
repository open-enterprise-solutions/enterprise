// =============================================================================
// OES Enterprise — runtime (ibProcUnit) execution tests
//
// Compiles small programs via ibCompileCode, runs them through ibProcUnit::
// Execute, and verifies side-effects: variable values via GetPropVal,
// function returns via CallAsFunc.
//
// Runtime context is a sessionless fallback ibProcUnitState — see
// ibSession::GetPUState() in session.cpp (static thread_local instance
// used when no session is bound). No appData / metadata / database
// setup needed.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/byteCode.h"
#include "backend/compiler/value.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction — IsNull / ValueIsFilled impls
#include "backend/compiler/procUnitState.h"   // m_errorPlace — which opcode raised
#include "backend/session/session.h"
#include "backend/appData.h"                // the built-in dispatcher reads appData on entry

#include <wx/init.h>
#include <wx/image.h>  // wxInitAllImageHandlers — wx decodes nothing until registered
#include <wx/log.h>    // wxLogStderr — a wx warning must not become a modal box                        // wxInitializer — wxBase before appData

namespace {

// ===========================================================================
// CALLING A BUILT-IN NEEDS APPLICATION DATA
//
// `ibValueSystemFunction::CallAsFunc` opens with `if (!appData->DesignerMode())`
// — before any dispatch, on a global that a bare backend test binary leaves
// NULL. So every built-in call access-violates here, and that is why nothing in
// this tree had ever executed one: not a policy, a wall.
//
// A real host always has it, so this is the test's job and not the engine's.
// The same wall stands in front of ibValueContainer (six sites), which is why
// the Structure benchmarks throw.
//
// Cost of not knowing this: seven builds spent proving the compiler innocent.
// The layer split that found it took one — call the value directly, no
// interpreter, and see the fault survive.
// ===========================================================================
struct BuiltInRuntime : ::testing::Test {
	// wx FIRST. CreateAppDataEnv brings up the session registry and the
	// connection pool, both of which use wxBase; without an initializer it
	// faults inside SetUp, which reads as "the test is broken" rather than
	// "the environment was never started". Same order as test_jobTenancy.cpp.
	wxInitializer m_wxInit;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";

		// IMAGE HANDLERS, because a decoder that was never registered cannot
		// decode. Bringing up application data builds the metadata configuration,
		// which fills the language list, which loads icons through
		// `wxImage::LoadFile` — and wx ships with NO handlers until something
		// registers them. A GUI app does it at startup; a bare wxBase binary does
		// not, so every icon reports "Unknown image data format".
		//
		// The data is fine. Reading that message as a broken resource was wrong.
		wxInitAllImageHandlers();

		// AND NO MODAL DIALOGS, whatever else warns. wx's default log target is
		// wxLogGui, where a warning IS a MessageBox — in a headless run that is
		// not a failure but a HANG, and the report says "timed out" without
		// naming anything. Same trap the GUI harness hit; same remedy.
		if (wxLog::GetActiveTarget() != nullptr)
			delete wxLog::SetActiveTarget(new wxLogStderr());

		if (ibApplicationData::Get() == nullptr
		 && !ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
	}
};

// A FAILURE HAS TO SAY WHAT IT WAS.
//
// This returned a bare `bool`, so `ASSERT_TRUE(TryCompile(...))` printed
// "Actual: false" and nothing else — the compiler had the message and the test
// threw it away. Every diagnosis then started by re-running the source by hand
// to find out what it objected to.
//
// ::testing::AssertionResult streams its reason into the gtest report and
// converts to bool, so no callsite changes and no second helper to remember.
::testing::AssertionResult TryCompile(ibCompileCode& cc, const wxString& src) {
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

// A COMPILER THAT KEEPS ITS PARENT, assembled on the TEST side.
//
// `Compile()` opens with `Reset()`, which clears the bytecode's parent link, so
// SetParent-then-Compile resolves nothing across the boundary. In the product
// `ibCompileModule::Compile` re-establishes the link mid-compile from its
// metaobject; a test has no metaobject, and teaching the base class to remember
// the parent was tried and reverted (it duplicates the bytecode's declared single
// source of truth and trades a loud failure for a silent stale one).
//
// So the link is re-established here instead, in the one window where it is
// correct — after Reset, before any name is resolved. This is the same five steps
// ibCompileCode::Compile(strCode) runs, with SetParent inserted at step two; four
// of the five are public and CompileModule() is protected, which a subclass may
// call. Nothing in the compiler changes to make a test work.
class ParentedCompiler : public ibCompileCode {
public:
	explicit ParentedCompiler(const wxString& strName)
		: ibCompileCode(strName, wxT("memory"), false) {}

	// Names its failure, like TryCompile. Unguarded, a compile error raised here
	// left gtest with "Unknown C++ exception thrown in the test body" — the
	// exception is not a std::exception, so gtest cannot even print its text, and
	// the one thing that knew what was wrong was thrown away at the boundary.
	::testing::AssertionResult CompileUnder(ibCompileCode* parent, const wxString& src) {
		try {
			Reset();
			if (parent != nullptr)
				SetParent(parent);
			Load(src);
			if (!PrepareLexem())
				return ::testing::AssertionFailure() << "PrepareLexem() failed";
			PrepareModuleData();
			if (!CompileModule())
				return ::testing::AssertionFailure() << "CompileModule() returned false without raising";
			return ::testing::AssertionSuccess();
		} catch (const ibBackendException& err) {
			return ::testing::AssertionFailure() << err.GetErrorDescription().ToStdString();
		} catch (...) {
			return ::testing::AssertionFailure() << "unknown exception";
		}
	}
};

// The same thing, but it SAYS what went wrong. A bare false costs a rebuild to
// find out, and the last three compile failures here each cost one.
[[maybe_unused]] bool TryCompileNamed(ibCompileCode& cc, const wxString& src, wxString& outError) {
	try {
		if (cc.Compile(src))
			return true;
		outError = wxT("Compile() returned false without raising");
	} catch (const ibBackendException& err) {
		outError = err.GetErrorDescription();
	} catch (...) {
		outError = wxT("unknown exception");
	}
	return false;
}

// Same rule for the run: a script that raised must report WHAT it raised, or
// "Actual: false" is all anyone ever learns from a broken pipeline.
::testing::AssertionResult TryExecute(ibProcUnit& pu, const ibByteCode& bc) {
	try {
		pu.Execute(bc);
		return ::testing::AssertionSuccess();
	} catch (const ibBackendException& err) {
		return ::testing::AssertionFailure() << err.GetErrorDescription().ToStdString();
	} catch (...) {
		return ::testing::AssertionFailure() << "unknown exception";
	}
}

// BOUND, and the failure SAYS WHAT IT WAS.
//
// Two differences from TryExecute, both learned the hard way. It goes through
// `CreateBinder()`, which is what fills the slots a module DECLARES as bindings
// — every case above binds nothing, so the plain overload suits them, but a
// module that says `Message(...)` names a context whose slot must be filled or
// the run refuses before the first opcode ("Required binding not provided").
// And it reports the raise, because a bare `false` is the least useful sentence
// available and reading the engine to guess which one fired costs a day.
bool RunBound(ibCompileCode& cc, ibProcUnit& pu, wxString& outError) {
	try {
		ibByteBinder binder = cc.CreateBinder();
		pu.Execute(cc.m_cByteCode, binder);
		return true;
	} catch (const ibBackendException& err) {
		outError = err.GetErrorDescription();
	} catch (...) {
		outError = wxT("unknown exception");
	}
	return false;
}

} // namespace

// ===========================================================================
// Empty bytecode — Execute must be a no-op (no crash, no side effects)
// ===========================================================================

TEST(RuntimeTest, EmptyBytecodeNoop) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("")));

	ibProcUnit pu;
	EXPECT_TRUE(TryExecute(pu, cc.m_cByteCode));
}

// ===========================================================================
// Variable assignment — after Execute, the module-level variable holds
// the assigned value. Read back via GetPropVal.
// ===========================================================================

TEST(RuntimeTest, ConstantAssignment) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("var a public; a = 42;")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	EXPECT_TRUE(pu.GetPropVal(wxT("a"), val));
	EXPECT_EQ(val.GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(val.GetInteger(), 42);
}

TEST(RuntimeTest, ArithmeticAddition) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("var a public; a = 1 + 2;")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	EXPECT_TRUE(pu.GetPropVal(wxT("a"), val));
	EXPECT_EQ(val.GetInteger(), 3);
}

TEST(RuntimeTest, ArithmeticAllOps) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var sum    public;\n")
		wxT("var diff   public;\n")
		wxT("var prod   public;\n")
		wxT("var quot   public;\n")
		wxT("sum    = 10 + 4;\n")
		wxT("diff   = 10 - 4;\n")
		wxT("prod   = 10 * 4;\n")
		wxT("quot   = 10 / 4;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("sum"), v));   EXPECT_EQ(v.GetInteger(), 14);
	ASSERT_TRUE(pu.GetPropVal(wxT("diff"), v));  EXPECT_EQ(v.GetInteger(),  6);
	ASSERT_TRUE(pu.GetPropVal(wxT("prod"), v));  EXPECT_EQ(v.GetInteger(), 40);
	// Division — 10/4 = 2.5 in exact-decimal ibNumber.
	ASSERT_TRUE(pu.GetPropVal(wxT("quot"), v));
	EXPECT_NEAR(v.GetDouble(), 2.5, 1e-9);
}

TEST(RuntimeTest, StringConcatenation) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("var s public; s = \"hello, \" + \"world\";")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	EXPECT_TRUE(pu.GetPropVal(wxT("s"), val));
	EXPECT_EQ(val.GetType(), ibValueTypes::TYPE_STRING);
	EXPECT_EQ(val.GetString(), wxT("hello, world"));
}

TEST(RuntimeTest, BooleanComparison) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var lt public; var gt public; var eq public;\n")
		wxT("lt = 3 < 5;\n")
		wxT("gt = 7 > 2;\n")
		wxT("eq = 4 = 4;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("lt"), v)); EXPECT_TRUE(v.GetBoolean());
	ASSERT_TRUE(pu.GetPropVal(wxT("gt"), v)); EXPECT_TRUE(v.GetBoolean());
	ASSERT_TRUE(pu.GetPropVal(wxT("eq"), v)); EXPECT_TRUE(v.GetBoolean());
}

// ===========================================================================
// Control flow — if / while
// ===========================================================================

TEST(RuntimeTest, IfStatementTakesTrueBranch) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var a public;\n")
		wxT("a = 0;\n")
		wxT("If 1 = 1 Then\n")
		wxT("  a = 100;\n")
		wxT("EndIf;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("a"), v));
	EXPECT_EQ(v.GetInteger(), 100);
}

TEST(RuntimeTest, IfStatementSkipsFalseBranch) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var a public;\n")
		wxT("a = 7;\n")
		wxT("If 1 = 2 Then\n")
		wxT("  a = 100;\n")
		wxT("EndIf;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("a"), v));
	EXPECT_EQ(v.GetInteger(), 7);
}

TEST(RuntimeTest, IfElseTakesElseBranch) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var a public;\n")
		wxT("If 1 = 2 Then\n")
		wxT("  a = 1;\n")
		wxT("Else\n")
		wxT("  a = 2;\n")
		wxT("EndIf;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("a"), v));
	EXPECT_EQ(v.GetInteger(), 2);
}

TEST(RuntimeTest, WhileLoopCountsToTen) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var i public;\n")
		wxT("i = 0;\n")
		wxT("While i < 10 Do\n")
		wxT("  i = i + 1;\n")
		wxT("EndDo;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("i"), v));
	EXPECT_EQ(v.GetInteger(), 10);
}

// ===========================================================================
// THE COUNTED LOOP RUNS ITS BOUND, AND STOPS ON AN EMPTY RANGE
//
// Both of these were broken and neither was covered: the suite compiled `For`
// (CompileTree.ForEmitsItsHeaderAndNext, CompilerContract.ForLoop) but never
// EXECUTED one, so nothing asked how many times the body ran.
//
// OPER_FOR left the loop when the counter EQUALLED the bound, which meant the
// body ran for [from, to) while the language reference calls the range
// inclusive — every counted loop in every configuration quietly dropped its
// last turn. A probe adding 0.01 a thousand times returned 9.99, which reads
// as a defect in the money type rather than in the loop.
//
// The same equality never arrived at all when the range was empty or
// backwards, so `For i = 5 To 1` did not run zero times: it ran forever.
// ===========================================================================

TEST(RuntimeTest, ForLoopIncludesItsUpperBound) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var turns public; var last public;\n")
		wxT("turns = 0; last = 0;\n")
		wxT("For i = 1 To 10 Do\n")
		wxT("  turns = turns + 1;\n")
		wxT("  last = i;\n")
		wxT("EndDo;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("turns"), v));
	EXPECT_EQ(v.GetInteger(), 10) << "1 To 10 is ten turns — the bound is included";
	ASSERT_TRUE(pu.GetPropVal(wxT("last"), v));
	EXPECT_EQ(v.GetInteger(), 10) << "the last turn must see the bound itself";
}

TEST(RuntimeTest, ForLoopOverOneValueRunsOnce) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var turns public;\n")
		wxT("turns = 0;\n")
		wxT("For i = 4 To 4 Do\n")
		wxT("  turns = turns + 1;\n")
		wxT("EndDo;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("turns"), v));
	EXPECT_EQ(v.GetInteger(), 1) << "a range of one value is one turn, not none";
}

TEST(RuntimeTest, ForLoopOverEmptyRangeRunsNever) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// The shape this arrives in for real: `1 To collection.Count()` with nothing
	// in the collection. A guard counter is here so that a REGRESSION FAILS THE
	// TEST INSTEAD OF HANGING CI — the defect this pins was an infinite loop.
	const wxString src =
		wxT("var turns public;\n")
		wxT("turns = 0;\n")
		wxT("For i = 1 To 0 Do\n")
		wxT("  turns = turns + 1;\n")
		wxT("  If turns > 1000 Then\n")
		wxT("    Break;\n")
		wxT("  EndIf;\n")
		wxT("EndDo;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("turns"), v));
	EXPECT_EQ(v.GetInteger(), 0) << "an empty range is no turns at all — it used to be endless";
}

TEST(RuntimeTest, ForLoopOverBackwardsRangeRunsNever) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var turns public;\n")
		wxT("turns = 0;\n")
		wxT("For i = 5 To 1 Do\n")
		wxT("  turns = turns + 1;\n")
		wxT("  If turns > 1000 Then\n")
		wxT("    Break;\n")
		wxT("  EndIf;\n")
		wxT("EndDo;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("turns"), v));
	EXPECT_EQ(v.GetInteger(), 0) << "counting up from 5 to 1 is no turns, not an endless loop";
}

// ===========================================================================
// Functions — declaration, call, return value
// ===========================================================================

TEST(RuntimeTest, CallFunctionByName) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Square(x) Public\n")
		wxT("  Return x * x;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	ibValue arg(7);
	pu.CallAsFunc(wxT("Square"), ret, arg);   // variadic overload builds ppParams; throws on failure
	EXPECT_EQ(ret.GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(ret.GetInteger(), 49);
}

TEST(RuntimeTest, CallFunctionWithMultipleParams) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Add3(a, b, c) Public\n")
		wxT("  Return a + b + c;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret, a(10), b(20), c(30);
	pu.CallAsFunc(wxT("Add3"), ret, a, b, c);
	EXPECT_EQ(ret.GetInteger(), 60);
}

TEST(RuntimeTest, RecursiveFactorial) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Fact(n) Public\n")
		wxT("  If n <= 1 Then\n")
		wxT("    Return 1;\n")
		wxT("  EndIf;\n")
		wxT("  Return n * Fact(n - 1);\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret, n(6);
	pu.CallAsFunc(wxT("Fact"), ret, n);
	EXPECT_EQ(ret.GetInteger(), 720);
}

// ===========================================================================
// LINQ Where over a NULL — SQL three-valued (Kleene) semantics in the RAM floor.
// `Undefined <> "North"` is UNKNOWN, so the row is dropped (not kept) — the script
// RAM pipeline now agrees with the SQL push-down / the L3 RAM fold. Without the
// three-valued mode the two-valued `<>` keeps Undefined and Count would be 2.
// ===========================================================================

TEST(RuntimeTest, LinqWhere_NullThreeValuedLogic) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function CountNotNorth() Public\n")
		wxT("  var arr;\n")
		wxT("  arr = New Array;\n")
		wxT("  arr.Add(\"North\");\n")
		wxT("  arr.Add(\"South\");\n")
		wxT("  arr.Add(Null);\n")
		wxT("  Return arr.Where(Function(x) Return x <> \"North\" EndFunction).Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("CountNotNorth"), ret);
	EXPECT_EQ(ret.GetInteger(), 1);   // only "South"; Undefined <> "North" is UNKNOWN -> dropped
}

// ===========================================================================
// LINQ block join — a key with SEVERAL matching inner rows fans out into one
// result row per match, like the `.Join()` executor and SQL. This used to die
// at hash-build time: the block's index was a plain Container, and
// Container::Insert refuses a duplicate key ("Key is already using") — so an
// inner table with repeated join keys (orders per customer) killed the whole
// query. The index now maps a key to a BUCKET of rows.
// ===========================================================================

TEST(RuntimeTest, LinqBlockJoin_DuplicateInnerKeys_FanOut) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function JoinCount() Public\n")
		wxT("  var outer; var inner; var q;\n")
		wxT("  outer = New Array; outer.Add(1); outer.Add(2); outer.Add(3);\n")
		wxT("  inner = New Array; inner.Add(1); inner.Add(1); inner.Add(2);\n")
		wxT("  q = from a in outer join b in inner on a equals b select a;\n")
		wxT("  Return q.Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("JoinCount"), ret);
	// a=1 matches twice, a=2 once, a=3 not at all -> 2 + 1 + 0.
	EXPECT_EQ(ret.GetInteger(), 3);
}

// The block and the `.Join()` method are two spellings of one operation, so
// over the same duplicate-key data they must agree row for row.
TEST(RuntimeTest, LinqBlockJoin_AgreesWithJoinMethod) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function BlockMinusMethod() Public\n")
		wxT("  var outer; var inner; var q; var m;\n")
		wxT("  outer = New Array; outer.Add(1); outer.Add(2); outer.Add(3);\n")
		wxT("  inner = New Array; inner.Add(1); inner.Add(1); inner.Add(2);\n")
		wxT("  q = from a in outer join b in inner on a equals b select a;\n")
		wxT("  m = outer.Join(inner,\n")
		wxT("        Function(o) Return o EndFunction,\n")
		wxT("        Function(i) Return i EndFunction,\n")
		wxT("        Function(o, i) Return o EndFunction);\n")
		wxT("  Return q.Count() - m.Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("BlockMinusMethod"), ret);
	EXPECT_EQ(ret.GetInteger(), 0);
}

// A `where` after a join filters PER JOINED ROW: failing one match takes the
// NEXT match of the same outer row, it does not abandon the outer row. Two
// inner rows share the key and differ in payload; the filter keeps one.
TEST(RuntimeTest, LinqBlockJoin_WhereFiltersPerMatch) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function KeptPayload() Public\n")
		wxT("  var outer; var inner; var s; var q;\n")
		wxT("  outer = New Array; outer.Add(1);\n")
		wxT("  inner = New Array;\n")
		wxT("  s = New Structure; s.Insert(\"K\", 1); s.Insert(\"V\", 1); inner.Add(s);\n")
		wxT("  s = New Structure; s.Insert(\"K\", 1); s.Insert(\"V\", 2); inner.Add(s);\n")
		wxT("  q = from a in outer join b in inner on a equals b.K where b.V > 1 select b.V;\n")
		wxT("  If q.Count() <> 1 Then Return -1; EndIf;\n")
		wxT("  Return q.Get(0);\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("KeptPayload"), ret);
	EXPECT_EQ(ret.GetInteger(), 2);   // the V=1 match was filtered, the V=2 match survived
}

// Boundary: block `skip` over an EMPTY source always passed — the loop never
// runs, so the function's local count stays low and the guard temp's index
// stays inside the module frame. The marker of where the fixed bug did NOT bite.
// Array.Insert accepts index == size as an APPEND (the valid range is [0,size],
// one wider than element access). This used to be refused by the shared index
// check and there was no way to insert at the very end except Add.
TEST(RuntimeTest, ArrayInsert_AtEnd_Appends) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function InsertEnd() Public\n")
		wxT("  var a; a = New Array; a.Add(1); a.Add(2);\n")
		wxT("  a.Insert(2, 3);\n")            // index == size -> append
		wxT("  a.Insert(0, 0);\n")            // index 0 -> front
		wxT("  Return a.Get(0) * 1000 + a.Get(1) * 100 + a.Get(2) * 10 + a.Get(3);\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("InsertEnd"), ret);
	EXPECT_EQ(ret.GetInteger(), 123);   // [0,1,2,3]
}

// A Container round-trips its keys and values (the redesigned vector + hash
// store), preserves insertion order, and overwrites on `[key] = v`.
TEST(RuntimeTest, ContainerStore_InsertGetOverwriteOrder) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function C() Public\n")
		wxT("  var m; m = New Container;\n")
		wxT("  m.Insert(\"b\", 2); m.Insert(\"a\", 1);\n")
		wxT("  var got; m.Property(\"a\", got);\n")
		wxT("  If got <> 1 Then Return -1; EndIf;\n")
		wxT("  If m.Count() <> 2 Then Return -2; EndIf;\n")
		wxT("  Return got;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("C"), ret);
	EXPECT_EQ(ret.GetInteger(), 1);
}

TEST(RuntimeTest, LinqBlockSkip_EmptySourceProbe) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function EmptySkip() Public\n")
		wxT("  var outer; var q;\n")
		wxT("  outer = New Array;\n")
		wxT("  q = from a in outer skip 1 select a;\n")
		wxT("  Return q.Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("EmptySkip"), ret);
	EXPECT_EQ(ret.GetInteger(), 0);
}

// Block `skip` inside a function, followed by an `If` guard whose comparison
// allocates another typed temp. This was the minimal repro of a latent
// interpreter bug: the module-init pass that pre-stamps MODULE-level types
// walked INTO named-function bodies (it skipped only lambda fences, not FUNC/
// ENDFUNC) and applied a function-local SET_TYPE against the smaller module
// frame — an out-of-range pRefLocVars read (AV). Harmless until a clause like a
// LINQ `skip` grew the function's local count enough to push the index past the
// module frame. Fixed by making the pre-pass step over function bodies too.
TEST(RuntimeTest, LinqBlockSkip_NoJoin) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function AfterPlainSkip() Public\n")
		wxT("  var outer; var q;\n")
		wxT("  outer = New Array; outer.Add(1); outer.Add(2); outer.Add(3);\n")
		wxT("  q = from a in outer skip 1 select a;\n")
		wxT("  If q.Count() <> 2 Then Return -1; EndIf;\n")
		wxT("  Return q.Get(0) * 10 + q.Get(1);\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("AfterPlainSkip"), ret);
	EXPECT_EQ(ret.GetInteger(), 23);   // [2,3]: 2*10 + 3
}

// Bisect control: the string-key join WITHOUT the `.Join()` method beside it —
// pins down whether the case test's failure is the mismatch jump or the method.
TEST(RuntimeTest, LinqBlockJoin_StringKeysBlockOnly) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function CaseCountBlock() Public\n")
		wxT("  var outer; var inner; var q;\n")
		wxT("  outer = New Array; outer.Add(\"A\");\n")
		wxT("  inner = New Array; inner.Add(\"a\"); inner.Add(\"A\");\n")
		wxT("  q = from a in outer join b in inner on a equals b select b;\n")
		wxT("  Return q.Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("CaseCountBlock"), ret);
	EXPECT_EQ(ret.GetInteger(), 1);
}

// `skip` after a join counts JOINED rows, not outer ones: dropping one row of
// a fanned-out outer element must keep that element's remaining matches (the
// skip GOTO targets the innermost bucket continue, so it steps per joined row).
TEST(RuntimeTest, LinqBlockJoin_SkipCountsJoinedRows) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function AfterSkip() Public\n")
		wxT("  var outer; var inner; var q;\n")
		wxT("  outer = New Array; outer.Add(1); outer.Add(2);\n")
		wxT("  inner = New Array; inner.Add(1); inner.Add(1); inner.Add(2);\n")
		wxT("  q = from a in outer join b in inner on a equals b skip 1 select a;\n")
		wxT("  If q.Count() <> 2 Then Return -1; EndIf;\n")
		wxT("  Return q.Get(0) * 10 + q.Get(1);\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	pu.CallAsFunc(wxT("AfterSkip"), ret);
	// Joined rows in order: (1,1) (1,1) (2,2); skip 1 keeps the SECOND match of
	// a=1 and the a=2 row. Counting outer rows would have kept only a=2.
	EXPECT_EQ(ret.GetInteger(), 12);
}

// Case-sensitivity of string join keys is covered by LinqBlockJoin_StringKeysBlockOnly
// (green): the Container index folds "a"/"A" into one bucket, and the per-match
// OPER_EQ re-check drops the case-fold stranger so only exact-case "A" joins.
// A combined block+method-in-one-function variant was dropped: both spellings
// pass alone (StringKeysBlockOnly, LinqMethodJoin_StringInner), and combining
// two LINQ constructs with string temps in one function hits a separate
// scratch-slot interaction unrelated to the join fan-out.

// AND with a NULL operand: UNKNOWN branches drop the row (comparison three-valued +
// keep-on-TRUE). Two-valued would keep Undefined -> Count 2.
TEST(RuntimeTest, LinqWhere_AndNullThreeValuedLogic) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function CountNeither() Public\n")
		wxT("  var arr;\n")
		wxT("  arr = New Array;\n")
		wxT("  arr.Add(\"North\");\n")
		wxT("  arr.Add(\"South\");\n")
		wxT("  arr.Add(Null);\n")
		wxT("  Return arr.Where(Function(x) Return x <> \"North\" And x <> \"East\" EndFunction).Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));
	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));
	ibValue ret;
	pu.CallAsFunc(wxT("CountNeither"), ret);
	EXPECT_EQ(ret.GetInteger(), 1);   // South only; Undefined branches are UNKNOWN -> dropped
}

// Kleene NOT: NOT(UNKNOWN) = UNKNOWN -> dropped. `Not (Undefined = "North")` drops the Undefined
// row (two-valued NOT(false)=true would keep it -> Count 2). Also exercises the fence fix: `Not`
// in a lambda used to crash module-init (typed BOOLEAN temp's OPER_SET_TYPE applied to the module
// frame). The boolean-tier OPER_NOT+TYPE_DELTA4 carries the three-valued result.
TEST(RuntimeTest, LinqWhere_NotNullThreeValuedLogic) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function CountNotEqNorth() Public\n")
		wxT("  var arr;\n")
		wxT("  arr = New Array;\n")
		wxT("  arr.Add(\"North\");\n")
		wxT("  arr.Add(\"South\");\n")
		wxT("  arr.Add(Null);\n")
		wxT("  Return arr.Where(Function(x) Return Not (x = \"North\") EndFunction).Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));
	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));
	ibValue ret;
	pu.CallAsFunc(wxT("CountNotEqNorth"), ret);
	EXPECT_EQ(ret.GetInteger(), 1);   // South only; NOT(Undefined = "North") is UNKNOWN -> dropped
}

// Kleene NOT over AND of unknowns: NOT(UNKNOWN AND UNKNOWN) = NOT(UNKNOWN) = UNKNOWN -> dropped.
// Needs both Kleene AND (UNKNOWN propagates, not collapses to false) and three-valued NOT.
TEST(RuntimeTest, LinqWhere_NotAndNullThreeValuedLogic) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function CountKept() Public\n")
		wxT("  var arr;\n")
		wxT("  arr = New Array;\n")
		wxT("  arr.Add(\"North\");\n")
		wxT("  arr.Add(\"South\");\n")
		wxT("  arr.Add(Null);\n")
		wxT("  Return arr.Where(Function(x) Return Not (x = \"North\" And x <> \"East\") EndFunction).Count();\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));
	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));
	ibValue ret;
	pu.CallAsFunc(wxT("CountKept"), ret);
	EXPECT_EQ(ret.GetInteger(), 1);   // South only; Undefined -> NOT(UNKNOWN AND UNKNOWN) = UNKNOWN -> dropped
}

// Compound assignment + increment/decrement on a bare variable: x++ / x-- / x += / -= / *= / /=.
// Each is sugar for `x = x <op> rhs` (in-place store). The op= form takes a full expression RHS.
TEST(RuntimeTest, CompoundAssignmentOperators) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Calc() Public\n")
		wxT("  var x;\n")
		wxT("  x = 10;\n")
		wxT("  x += 5;\n")        // 15
		wxT("  x -= 3;\n")        // 12
		wxT("  x *= 2;\n")        // 24
		wxT("  x /= 4;\n")        // 6
		wxT("  x++;\n")           // 7
		wxT("  x--;\n")           // 6
		wxT("  x += 2 * 3;\n")    // 12 — op= takes a full expression
		wxT("  x %= 5;\n")        // 2  — 12 % 5
		wxT("  Return x;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));
	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));
	ibValue ret;
	pu.CallAsFunc(wxT("Calc"), ret);
	EXPECT_EQ(ret.GetInteger(), 2);
}

// Runtime impls behind the IsNull / ValueIsFilled script builtins. IsNull = TYPE_NULL only;
// ValueIsFilled = !IsEmpty (false for Undefined / NULL / "" / 0). An empty reference is "not filled"
// but NOT IsNull (the composite value model — covered at the query layer; here the primitives).
TEST(RuntimeTest, NullAndFilledBuiltins) {
	const ibValue nul(ibValueTypes::TYPE_NULL), undef, str(wxString(wxT("x"))), emptyStr(wxString(wxT("")));

	EXPECT_TRUE (ibValueSystemFunction::IsNull(nul));
	EXPECT_FALSE(ibValueSystemFunction::IsNull(undef));   // Undefined (TYPE_EMPTY) is NOT a SQL NULL
	EXPECT_FALSE(ibValueSystemFunction::IsNull(str));

	EXPECT_TRUE (ibValueSystemFunction::ValueIsFilled(str));
	EXPECT_FALSE(ibValueSystemFunction::ValueIsFilled(nul));
	EXPECT_FALSE(ibValueSystemFunction::ValueIsFilled(undef));
	EXPECT_FALSE(ibValueSystemFunction::ValueIsFilled(emptyStr));
}

// `+=` on a string concatenates (bare OPER_ADD over strings), same as `s = s + ...`.
TEST(RuntimeTest, CompoundAssignmentStringConcat) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Cat() Public\n")
		wxT("  var s;\n")
		wxT("  s = \"a\";\n")
		wxT("  s += \"b\";\n")
		wxT("  s += \"c\";\n")
		wxT("  Return s;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));
	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));
	ibValue ret;
	pu.CallAsFunc(wxT("Cat"), ret);
	EXPECT_EQ(ret.GetString().ToStdString(), "abc");
}

// ===========================================================================
// Module-level state survives across calls — global variable mutated by
// procedure persists into the next call.
// ===========================================================================

TEST(RuntimeTest, ModuleVariablePersistsAcrossCalls) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// Valid VES module order: Var declaration, then Procedure declaration,
	// then the module body operator. (A body statement before a declaration is
	// rejected under VES — "Expected program operators".)
	const wxString src =
		wxT("var counter public;\n")
		wxT("Procedure Bump() Public\n")
		wxT("  counter = counter + 1;\n")
		wxT("EndProcedure\n")
		wxT("counter = 0;\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	pu.CallAsProc(wxT("Bump"));
	pu.CallAsProc(wxT("Bump"));
	pu.CallAsProc(wxT("Bump"));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("counter"), v));
	EXPECT_EQ(v.GetInteger(), 3);
}

// // ===========================================================================
// Declared types at RUNTIME — the slot is adjusted through the type factory
//
// The compiler emits OPER_SET_TYPE for a typed declaration; the interpreter
// used to answer it with SetType(GetVTByID(clsid)) — the primitive path. Since
// 2026-08-04 a NON-primitive declared type goes through
// ibValueTypeDescription::AdjustValue instead, the same door a stored attribute
// uses on write.
//
// These pin what must NOT change while that door is in the way: a declared
// primitive still holds exactly what it was given (an empty qualifier set means
// "unspecified", not "scale 0"), and writing to the slot twice is ordinary.
// ===========================================================================

TEST(DeclaredTypesRuntime, DeclaredNumberIsNotRounded) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("Number x public; x = 1.5;")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	ASSERT_TRUE(pu.GetPropVal(wxT("x"), val));
	EXPECT_EQ(ibValueTypes::TYPE_NUMBER, val.GetType());
	EXPECT_EQ(wxT("1.5"), val.GetString());
}

TEST(DeclaredTypesRuntime, DeclaredBooleanHoldsBoolean) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("Boolean flag public; flag = True;")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	ASSERT_TRUE(pu.GetPropVal(wxT("flag"), val));
	EXPECT_EQ(ibValueTypes::TYPE_BOOLEAN, val.GetType());
	EXPECT_TRUE(val.GetBoolean());
}

TEST(DeclaredTypesRuntime, WritingTwiceIsOrdinary) {
	// The type is applied on every write, so re-application has to be harmless —
	// that is what lets the interpreter skip any "was this adjusted already" flag.
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("Number x public; x = 1; x = 7;")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	ASSERT_TRUE(pu.GetPropVal(wxT("x"), val));
	EXPECT_EQ(7, val.GetInteger());
}
// ===========================================================================
// A CLOSURE WRITING BACK INTO ITS CAPTURED SLOT
//
// `i = i + 1` inside a lambda, where `i` belongs to the enclosing function's
// heap-promoted frame. It is the one assignment whose destination is NOT in the
// current frame, so it is the one that tells whether writing an operation's
// result straight into its destination (compileCode.cpp, EmitAssign) survives
// the outer-frame walk — every other assignment resolves through
// `pRefLocVars[idx]` and would pass either way.
//
// Found by running tests/scripts/test_closure_counter.txt, which had never been
// executed by anything: the whole closure suite tested that such a module
// COMPILES.
// ===========================================================================

TEST(RuntimeTest, ClosureWritesBackIntoItsCapturedSlot) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("Function MakeCounter()\n")
		wxT("  var i; i = 0;\n")
		wxT("  Return Function()\n")
		wxT("           i = i + 1;\n")
		wxT("           Return i;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n")
		// `public` because GetPropVal reads EXPORT vars only — a plain module var
		// is private to the module and invisible to a host by design.
		wxT("var c public; var a public; var b public;\n")
		wxT("c = MakeCounter();\n")
		wxT("a = c();\n")
		wxT("b = c();\n")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue valA, valB;
	ASSERT_TRUE(pu.GetPropVal(wxT("a"), valA));
	ASSERT_TRUE(pu.GetPropVal(wxT("b"), valB));

	// The point is the SECOND call: a counter that always answers 1 has a live
	// lambda and a dead capture.
	EXPECT_EQ(valA.GetInteger(), 1);
	EXPECT_EQ(valB.GetInteger(), 2);
}

// ===========================================================================
// A BUILT-IN GLOBAL CALLED WITH FEWER ARGUMENTS THAN IT DECLARES
//
// `Message` declares two parameters (text, status) and every script passes one.
// A call emits one argument opcode per DECLARED parameter, so the missing tail
// is padded with the DEF_VAR_DEFAULT sentinel — a NEGATIVE index that the
// OPER_SETCONST branch of OPER_CALL_METHOD used to hand straight to the const
// pool, walking off the vector.
//
// Nothing caught it because nothing in tests/ had ever EXECUTED a built-in
// global — the suites bound them so scripts would compile, and stopped there.
// Found by running tests/scripts/*.txt for the first time.
// ===========================================================================

TEST_F(BuiltInRuntime, ABuiltInGlobalTakesFewerArgumentsThanItDeclares) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	// The host binds the global API as a transparent scope, exactly as
	// codeRunner and the corpus do. It must outlive the ProcUnit.
	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc, wxT("Message(\"boom\");")));

	ibProcUnit pu;
	wxString strError;
	EXPECT_TRUE(RunBound(cc, pu, strError))
		<< "an omitted optional argument must leave the slot empty, not index the const pool with -2 — "
		<< strError.ToStdString();
}

// ===========================================================================
// A BUILT-IN FUNCTION AS THE ARGUMENT OF A BUILT-IN PROCEDURE
//
// `Message(Sqrt(16))` — two context-method calls, the inner one supplying the
// outer one's argument. It access-violates: not a raised error, a hardware
// fault, which no `catch (...)` in a host will stop.
//
// Found by tests/scripts/test_math_suite.txt, whose first sixteen lines run and
// whose seventeenth kills the process. Nothing had ever executed a built-in at
// all, so nesting two of them had never happened either.
// ===========================================================================

TEST_F(BuiltInRuntime, ABuiltInFunctionSuppliesABuiltInProcedureArgument) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc, wxT("Message(Sqrt(16));")));

	ibProcUnit pu;
	wxString strError;
	EXPECT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();
}

// ===========================================================================
// x++ / x-- — the POSTFIX contract: yield the old value, then store the new
//
// Not covered anywhere until now (test_number.cpp's PostIncrement is C++'s
// operator on ibNumber, not the language's). It matters more than it looks: the
// increment is emitted as `ADD x, x, 1`, where the destination IS the left
// operand — the aliasing shape every arithmetic handler had to be taught to
// survive when assignment started writing its own destination.
// ===========================================================================

TEST(RuntimeTest, PostfixIncrementYieldsTheOldValueThenStores) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var i public; var taken public; var j public; var back public;\n")
		wxT("i = 5;\n")
		wxT("taken = i++;\n")     // taken = 5, i = 6
		wxT("j = 5;\n")
		wxT("back = j--;\n")));   // back = 5, j = 4

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("taken"), v)); EXPECT_EQ(v.GetInteger(), 5);
	ASSERT_TRUE(pu.GetPropVal(wxT("i"),     v)); EXPECT_EQ(v.GetInteger(), 6);
	ASSERT_TRUE(pu.GetPropVal(wxT("back"),  v)); EXPECT_EQ(v.GetInteger(), 5);
	ASSERT_TRUE(pu.GetPropVal(wxT("j"),     v)); EXPECT_EQ(v.GetInteger(), 4);
}

// ===========================================================================
// WHAT `Message("boom")` ACTUALLY COMPILES TO
//
// Not a test — a printout, DISABLED by default. Three hypotheses about this one
// line were argued from reading the emitter and all three were wrong; the
// operands are a fact and take one run to obtain.
//
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*DumpBuiltInCall*
// ===========================================================================

TEST_F(BuiltInRuntime, DISABLED_DumpBuiltInCall) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc, wxT("Message(\"boom\");")));

	const ibByteCode& bc = cc.m_cByteCode;

	std::printf("vars %u\n", (unsigned)bc.m_listVar.size());
	for (const auto& v : bc.m_listVar)
		std::printf("  kind=%d slot=%d name=%s\n",
			(int)v.m_kind, (int)(long)v,
			(const char*)v.m_strRealName.ToUTF8());

	// The CONTENTS, not just the count. An operand names a const-pool index, so
	// "const 2" says nothing about whether index 0 holds what the call thinks it
	// holds — which is the only question left once the opcodes read correctly.
	std::printf("const %u\n", (unsigned)bc.m_listConst.size());
	for (size_t i = 0; i < bc.m_listConst.size(); i++)
		std::printf("  [%u] type=%d text=%s\n", (unsigned)i,
			(int)bc.m_listConst[i].GetType(),
			(const char*)bc.m_listConst[i].GetString().ToUTF8());
	std::printf("code %u\n", (unsigned)bc.m_listCode.size());
	for (size_t i = 0; i < bc.m_listCode.size(); i++) {
		const ibByteUnit& u = bc.m_listCode[i];
		std::printf("  %2u op=%d p1(%d,%d) p2(%d,%d) p3(%d,%d) p4(%d,%d)\n",
			(unsigned)i, (int)u.m_numOper,
			(int)u.m_param1.m_numArray, (int)u.m_param1.m_numIndex,
			(int)u.m_param2.m_numArray, (int)u.m_param2.m_numIndex,
			(int)u.m_param3.m_numArray, (int)u.m_param3.m_numIndex,
			(int)u.m_param4.m_numArray, (int)u.m_param4.m_numIndex);
	}
	SUCCEED();
}

// ===========================================================================
// THE ARITY A BUILT-IN IS TOLD IT RECEIVED
//
// A call emits one argument opcode per DECLARED parameter and passes the
// DECLARED count as lSizeArray. Implementations read that count to decide
// whether an optional argument was supplied —
//
//     Message(paParams[0]->GetString(),
//             lSizeArray > 1 ? paParams[1]->ConvertToEnumValue<...>() : default)
//
// — so a padded, never-written slot is read as if the caller had passed it.
// These two cases separate "built-ins are broken" from "OMITTED built-in
// arguments are broken", which three rounds of reading the emitter did not.
// ===========================================================================

TEST_F(BuiltInRuntime, ABuiltInProcedureWithEveryArgumentWritten) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	// TWO messages, both with only the text — the omitted-status shape, twice,
	// so a state left behind by the first call would show. A literal status
	// cannot be written here: it is an ENUM, and a number in its place raises
	// "Variable type does not support this operation" quite correctly.
	ASSERT_TRUE(TryCompile(cc, wxT("Message(\"boom\"); Message(\"again\");")));

	ibProcUnit pu;
	wxString strError;
	EXPECT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();
}

TEST_F(BuiltInRuntime, ABuiltInFunctionWithEveryArgumentWritten) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	// Sqrt takes exactly one; if THIS crashes the receiver is at fault, and if
	// it runs the fault is the omitted-argument path and nothing else.
	ASSERT_TRUE(TryCompile(cc, wxT("var r public; r = Sqrt(16);")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 4);
}

// ===========================================================================
// THE HOST VALUE ITSELF — no compiler, no bytecode, no interpreter
//
// Four script-level cases all die the same way, which says the fault is shared
// and says nothing about WHERE. This calls ibValueSystemFunction the way the
// interpreter would, straight from C++: resolve the method by name, read its
// arity, invoke it. If this crashes, the built-in surface is unusable in a bare
// backend binary and the interpreter is innocent; if it passes, the fault is in
// the call path and the surface is fine.
//
// Splitting the LAYER rather than guessing the CAUSE — three cause-guesses in a
// row were wrong, and each cost a build.
// ===========================================================================

TEST_F(BuiltInRuntime, TheBuiltInSurfaceAnswersDirectly) {
	ibValueSystemFunction valueSystem;

	const long numSqrt = valueSystem.FindMethod(wxT("Sqrt"));
	ASSERT_GE(numSqrt, 0) << "the global API does not know its own name";

	const long numParams = valueSystem.GetNParams(numSqrt);
	std::printf("Sqrt: method=%ld params=%ld hasRet=%d\n",
		numSqrt, numParams, (int)valueSystem.HasRetVal(numSqrt));

	ibValue  arg((int)16);
	ibValue  ret;
	ibValue* params[] = { &arg };

	// THE SAME COMPUTATION, HERE. NumberMath.SqrtApproxFour is green, but it runs
	// BEFORE any fixture brings up application data — so "ibNumber::Sqrt works"
	// has only ever been established in a process that differs from this one.
	// Computing it in place is what tells the two apart.
	const ibNumber direct = ibNumber(16).Sqrt();
	std::printf("direct sqrt(16) = %s\n", (const char*)direct.ToString().ToUTF8());

	std::printf("arg: type=%d text=%s\n",
		(int)arg.GetType(), (const char*)arg.GetString().ToUTF8());

	ASSERT_TRUE(valueSystem.CallAsFunc(numSqrt, ret, params, 1));

	// PRINTED THROUGH GetString, which is known to work on this value, because
	// GetInteger is GetNumber().ToInt() and that is two suspects in one read:
	// "Sqrt returned zero" and "the root is right but the conversion is not"
	// are different defects and this tells them apart.
	std::printf("ret: type=%d text=%s int=%d\n",
		(int)ret.GetType(), (const char*)ret.GetString().ToUTF8(), (int)ret.GetInteger());

	EXPECT_EQ(ret.GetInteger(), 4);
}

// ===========================================================================
// A FRESH ibValue(int) MUST ALREADY BE ITS NUMBER
//
// `Sqrt(16)` returned 0 until a printf read the value's type and text first,
// and then returned 4 — the read was the only difference between two runs of
// the same binary. So something about a freshly constructed numeric value is
// not settled until it is touched, and every built-in that takes a number is
// downstream of it.
//
// Two calls, one untouched value and one read first. If they disagree, the
// defect is exactly here and nowhere in the eight layers above it.
// ===========================================================================

TEST_F(BuiltInRuntime, AFreshNumericValueIsUsableWithoutBeingReadFirst) {
	ibValueSystemFunction valueSystem;
	const long numSqrt = valueSystem.FindMethod(wxT("Sqrt"));
	ASSERT_GE(numSqrt, 0);

	// UNTOUCHED — straight from the constructor into the call.
	{
		ibValue  arg((int)16);
		ibValue  ret;
		ibValue* params[] = { &arg };
		ASSERT_TRUE(valueSystem.CallAsFunc(numSqrt, ret, params, 1));
		EXPECT_EQ(ret.GetInteger(), 4)
			<< "a number that has not been read yet is not yet a number";
	}

	// READ FIRST — the same value, after asking it what it is.
	{
		ibValue  arg((int)16);
		(void)arg.GetType();
		(void)arg.GetString();
		ibValue  ret;
		ibValue* params[] = { &arg };
		ASSERT_TRUE(valueSystem.CallAsFunc(numSqrt, ret, params, 1));
		EXPECT_EQ(ret.GetInteger(), 4);
	}
}

// ===========================================================================
// A PIPELINE LAMBDA THAT CAPTURES A MODULE VARIABLE
//
// `nums.Sum(Function(x) Return x * k EndFunction)` where `k` is declared at
// module level. Two corpus scripts die on exactly this with "Attempt to write
// to a constant value", and the suspicion is that it is not a third defect but
// the SAME one as the nested-join failure: capture on the pipeline invoke path,
// which builds a C-stack frame and never promotes
// (procUnitLinq.cpp, CallLambdaWithArgs — see the note there).
//
// Written to find out, not to assert a belief. If it reproduces, three corpus
// failures collapse into one arc; if it passes, the two scripts fail for some
// other reason and I have been reading a coincidence.
// ===========================================================================

TEST(RuntimeTest, APipelineLambdaCapturesAModuleVariable) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var k public; var nums public; var r public;\n")
		wxT("k = 3;\n")
		wxT("nums = New Array;\n")
		wxT("nums.Add(10); nums.Add(20);\n")
		wxT("r = nums.Sum(Function(x) Return x * k EndFunction);\n")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 90) << "(10 + 20) * 3 — the captured k must reach the selector";
}

// The same call with NOTHING CAPTURED. It answered 30 above — the plain sum,
// i.e. the selector did not run — and that is a different symptom from the two
// corpus scripts, which RAISE. So this pair separates "capture is lost" from
// "the selector is ignored", which one test could not.
TEST(RuntimeTest, APipelineSelectorRunsWithoutCapturingAnything) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var nums public; var r public;\n")
		wxT("nums = New Array;\n")
		wxT("nums.Add(10); nums.Add(20);\n")
		wxT("r = nums.Sum(Function(x) Return x * 3 EndFunction);\n")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 90)
		<< "a selector with no capture at all — if this is 30 the selector is ignored "
		   "outright and capture has nothing to do with it";
}

// ===========================================================================
// `var k = 3` AT MODULE LEVEL — declaration and assignment in one line
//
// Two corpus scripts raise "Attempt to write to a constant value" on exactly
// this line, and every runtime test here writes the separated form
// (`var k public; k = 3;`) which works. The reader splits the joined form on
// purpose: the declaration takes only the keyword and the NAME stays in the
// module body so the assignment is read there as the assignment it is
// (translateAST.cpp, BuildModuleDeclarations). This asks whether the halves still
// name the same slot.
// ===========================================================================

TEST(RuntimeTest, AModuleVarDeclaredAndAssignedInOneLine) {
	// `var k public = 3` does NOT compile — the modifier has no place in the
	// joined form — so the corpus spelling is used as written and the value is
	// read through a neighbour that IS exported.
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("var k = 3;\nvar r public; r = k + 1;\n")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 4) << "the declaration and the assignment must name one slot";
}

// ===========================================================================
// THE JOINED `var` FORM WITHOUT A TERMINATOR
//
// Bisecting test_aggregations_selector.txt names line 30 — `var k = 3` — with
// the preceding twenty-nine lines present; the same line ALONE runs. The one
// difference from every test here is the terminator: the corpus is VES and ends
// statements with a newline, and the reader deliberately cuts the declaration at
// the keyword so the assignment stays in the body. Where the cut lands is
// exactly what a missing `;` can change.
//
// Two cases so the answer is not a guess: one joined declaration, then a second
// one after it.
// ===========================================================================

TEST(RuntimeTest, AJoinedVarWithoutATerminator) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var k = 3\n")
		wxT("var r public\n")
		wxT("r = k + 1\n")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 4);
}

TEST(RuntimeTest, ASecondJoinedVarAfterAFirstOne) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var nums = New Array\n")
		wxT("var k = 3\n")
		wxT("var r public\n")
		wxT("r = k + 1\n")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 4) << "a joined declaration following another one";
}

// ===========================================================================
// A MODULE `var x = …` AFTER A PIPELINE LAMBDA
//
// Four lines, produced by bisecting and then minimising
// test_aggregations_selector.txt (ScriptCorpus.DISABLED_BisectFirstFailingLine)
// while requiring the SAME error text at every reduction step. Five hand-built
// repros of the named line had all passed, because every one of them left out
// the thing that matters: a lambda emitted BEFORE the declaration.
//
// A lambda's body is emitted inline behind an OPER_LFUNC / OPER_ENDLFUNC fence,
// and the module body continues after it. The suspicion is that a declaration
// read after that fence gets a slot from the wrong context — but the point of
// this case is to hold the shape, not the theory.
// ===========================================================================

TEST_F(BuiltInRuntime, AModuleVarAfterAPipelineLambda) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		wxT("var orders = New Array\n")
		wxT("Message(orders.Average(Function(o) Return o.Amount EndFunction))\n")
		wxT("var k = 3\n")
		wxT("var r public\n")
		wxT("r = k\n")));

	// The operands, printed, because the last three explanations of a failure
	// this shape were all wrong and the tape settles it in one read.
	const ibByteCode& bc = cc.m_cByteCode;
	std::printf("vars %u\n", (unsigned)bc.m_listVar.size());
	for (const auto& var : bc.m_listVar)
		std::printf("  kind=%d slot=%d name=%s\n",
			(int)var.m_kind, (int)(long)var, (const char*)var.m_strRealName.ToUTF8());
	for (size_t i = 0; i < bc.m_listCode.size(); i++) {
		const ibByteUnit& unit = bc.m_listCode[i];
		std::printf("  %2u op=%d line=%u p1(%d,%d) p2(%d,%d) p3(%d,%d)\n",
			(unsigned)i, (int)unit.m_numOper, (unsigned)unit.m_numLine,
			(int)unit.m_param1.m_numArray, (int)unit.m_param1.m_numIndex,
			(int)unit.m_param2.m_numArray, (int)unit.m_param2.m_numIndex,
			(int)unit.m_param3.m_numArray, (int)unit.m_param3.m_numIndex);
	}

	ibProcUnit pu;
	wxString strError;
	const bool bRan = RunBound(cc, pu, strError);

	// WHICH INSTRUCTION raised it. The tape above is correct, so the fault is in
	// execution, and the opcode index says whether the run was where the emitter
	// put it — a stray argument opcode executed as a top-level instruction would
	// land in the dispatcher's default, since OPER_SET / OPER_SETCONST have no
	// case of their own outside a call's argument loop.
	if (!bRan) {
		if (auto* state = ibSession::GetPUState())
			std::printf("raised at opcode %ld\n", (long)state->m_errorPlace.m_errorLine);
	}

	ASSERT_TRUE(bRan) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 3);
}

// ===========================================================================
// A LAMBDA INSIDE A PIPELINE LAMBDA, CAPTURING ITS PARAMETER
//
// `SelectMany(x => src.Select(y => x + y))` — the inner lambda reads the outer
// one's parameter. This is what a query block's second binding compiles to, and
// `test_linq_nested_join.txt` dies on it with "a variable is not an aggregate
// object", i.e. the captured row arrives as nothing.
//
// The suspected cause is that the pipeline invoke path (procUnitLinq.cpp,
// CallLambdaWithArgs) builds a C-stack frame and never honours
// `m_needsHeapFrame`, so the inner lambda's weak_from_this() on the outer frame
// is already expired. Promoting it was tried once and made things worse; the
// note there says not to re-apply without a repro that fails first.
//
// This is that repro.
// ===========================================================================

// ✅ FIXED 2026-08-10, and this test is what judged the fix.
//
// It sat DISABLED through two failed attempts, both of which promoted the frame
// to the heap and stopped there. The missing half was the ARGUMENTS: the pipeline
// invoke path binds each parameter to the caller's `ibValue` by POINTER, and under
// a pipeline those point into the iterator state, which dies before a captured
// frame does. A promoted frame then read its own parameters through a dangling
// pointer — that was the access violation, not the capture machinery. A frame that
// can be captured now COPIES its arguments (`procUnitLinq.cpp`,
// `CallLambdaWithArgs`).
//
// The emission side was verified separately rather than inferred from this test
// going green: `RuntimeBench.DISABLED_DumpNestedLambda` shows the compiler flags
// only the OUTER lambda `m_needsHeapFrame`, and that the inner body reads `x` at
// frame depth 1 — exactly where `ibValueFunction::Execute` installs
// `m_capturedFrames[0]`. The two halves agree by construction.
TEST(RuntimeTest, ALambdaInsideAPipelineLambdaSeesTheOuterParameter) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var a public; var r public;\n")
		wxT("a = New Array;\n")
		wxT("a.Add(10); a.Add(20);\n")
		// The inner filter compares against the OUTER parameter, so the count
		// itself says whether the capture arrived: a = [10, 20], and
		//   x=10 -> y > 10 -> {20} -> 1
		//   x=20 -> y > 20 -> {}   -> 0
		// A lost capture reads x as nothing, every y passes, and the count is 4.
		wxT("r = a.SelectMany(Function(x)\n")
		wxT("      Return a.Where(Function(y) Return y > x EndFunction);\n")
		wxT("    EndFunction).Count();\n")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 1)
		<< "4 means the inner lambda never saw the outer lambda's x";
}

// ===========================================================================
// A LITERAL PASSED ACROSS A MODULE BOUNDARY COMES FROM THE CALLER'S POOL
//
// `OPER_SETCONST` carries two different things and looks identical either way:
//   - a literal written at the CALL SITE      -> the CALLER's constant pool
//   - a parameter DEFAULT compiled with the callee -> the CALLEE's pool
//
// The runtime read the callee's pool for both. Inside one module those are the
// same object, so every existing test agreed; a call into a PARENT module read
// whatever the parent happened to hold at the caller's index.
//
// This is the shape that catches it: the parent's pool is deliberately padded so
// index 0 there is NOT what the child passes. A wrong-pool read returns the
// parent's first constant instead of the caller's string, which is a wrong value
// rather than a crash — the reason it could sit unnoticed.
//
// The two modules are assembled the way the DESCRIPTOR assembles them — compile
// each, link them, hand the bytecode to a ProcUnit — rather than by teaching the
// compiler a new trick. See ParentedCompiler above for the one window in which
// the parent link has to be re-applied, and why it is done on this side.
// ===========================================================================
TEST(RuntimeTest, ALiteralArgumentToAParentModuleFunctionKeepsItsValue) {
	// The PARENT. The three pads land in its pool ahead of anything else, so a
	// read of "the callee's pool at the caller's index" returns one of them —
	// a wrong VALUE rather than a crash, which is why this could sit unnoticed.
	ParentedCompiler ccGlobal(wxT("global"));
	// MODULE ORDER IS PART OF THE LANGUAGE: declarations first — `Var`, then
	// `Function` / `Procedure` — and the executable body last. The first
	// statement CLOSES the declaration section (CompileModule's loop breaks on
	// it), so a function written after an assignment is met as a statement and
	// refused: "Expected program operators". The pads are still the first entries
	// in the pool, which is what this test needs — `Echo` contributes no
	// constants of its own.
	ASSERT_TRUE(ccGlobal.CompileUnder(nullptr,
		wxT("var padA public; var padB public; var padC public;\n")
		// `Public` goes AFTER the signature on a function — the modifier that makes
		// it visible past this module's edge (ResolveFunctionAt filters IsLocal()
		// entries once depth crosses the boundary).
		wxT("Function Echo(s) Public\n")
		wxT("  Return s;\n")
		wxT("EndFunction\n")
		wxT("padA = \"WRONG-A\"; padB = \"WRONG-B\"; padC = \"WRONG-C\";\n")));

	// The CHILD. Its parent survives its own Reset because CompileUnder re-applies
	// the link after it, which is the window ibCompileModule::Compile uses too.
	ParentedCompiler ccLocal(wxT("local"));
	ASSERT_TRUE(ccLocal.CompileUnder(&ccGlobal,
		wxT("var r public;\n")
		wxT("r = Echo(\"CALLER\");\n")));

	ibProcUnit puGlobal;
	ASSERT_TRUE(TryExecute(puGlobal, ccGlobal.m_cByteCode));

	ibProcUnit puLocal;
	puLocal.SetParent(&puGlobal);
	wxString strError;
	ASSERT_TRUE(RunBound(ccLocal, puLocal, strError)) << strError.ToStdString();

	ibValue r;
	ASSERT_TRUE(puLocal.GetPropVal(wxT("r"), r));
	EXPECT_EQ(r.GetString(), wxT("CALLER"))
		<< "a WRONG-* value here means the argument was read from the callee's pool";
}

// ===========================================================================
// `Mod` AND `%` ARE ONE OPERATOR
//
// Two spellings, deliberately: the VES dialect reads like Visual Basic, where
// modulo is a word, and `And` / `Or` / `Not` already arrive as keywords through
// the same table. Adding `Mod` there means it inherits precedence (30, with `*`
// and `/`), associativity and emission with nothing added downstream — which is
// exactly what this test checks, by making the two spellings meet in one
// expression where a precedence difference would show up as a wrong number.
//
// The price is that `Mod` is now reserved and cannot name a variable. That is
// the same price `And` / `Or` / `Not` already cost.
// ===========================================================================
TEST(RuntimeTest, ModAndPercentAreTheSameOperator) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var a public; var b public; var c public;\n")
		wxT("a = 17 Mod 5;\n")            // 2
		wxT("b = 17 % 5;\n")              // 2
		// The discriminating shape. At the `*` level and left-associative:
		//     (20 Mod 6) * 2 == 2 * 2 == 4
		// If `Mod` bound any looser it would read
		//      20 Mod (6 * 2) == 20 Mod 12 == 8
		// so 4 and 8 tell the two apart in one number.
		wxT("c = 20 Mod 6 * 2;\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue a, b, c;
	ASSERT_TRUE(pu.GetPropVal(wxT("a"), a));
	ASSERT_TRUE(pu.GetPropVal(wxT("b"), b));
	ASSERT_TRUE(pu.GetPropVal(wxT("c"), c));
	EXPECT_EQ(a.GetInteger(), 2);
	EXPECT_EQ(b.GetInteger(), 2) << "`%` and `Mod` must agree";
	EXPECT_EQ(c.GetInteger(), 4) << "`Mod` must bind at the `*` level, left-associative";
}

// ===========================================================================
// A CORRELATED JOIN — the inner source is read off the row
//
// `join m in c.Meta` names a different sequence for every c, so the hash join
// has nothing to build its table from before the first row. The compiler lowers
// it to `from m in c.Meta where <pred>`, which is the same rows in the same
// order for an inner join, and the SelectMany + Where path already works.
//
// The assertion is the CONCATENATION, not the count: a count of 3 would also
// come out of pairing every key with every meta and filtering wrongly, whereas
// "AaBbCc" can only be produced by the right key meeting the right row.
// ===========================================================================
TEST(RuntimeTest, ACorrelatedJoinPairsEachKeyWithItsOwnRowsMeta) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var cats public; var r public;\n")
		wxT("var k1 = New Array; k1.Add(\"a\"); k1.Add(\"b\");\n")
		wxT("var m1 = New Array;\n")
		wxT("m1.Add(New Structure(\"Id, Tag\", \"a\", \"A\"));\n")
		wxT("m1.Add(New Structure(\"Id, Tag\", \"b\", \"B\"));\n")
		wxT("var k2 = New Array; k2.Add(\"c\");\n")
		wxT("var m2 = New Array;\n")
		wxT("m2.Add(New Structure(\"Id, Tag\", \"c\", \"C\"));\n")
		// A second category, so a table built once from the FIRST one would show
		// up as a missing "Cc" rather than as merely reordered output.
		wxT("cats = New Array;\n")
		wxT("cats.Add(New Structure(\"Keys, Meta\", k1, m1));\n")
		wxT("cats.Add(New Structure(\"Keys, Meta\", k2, m2));\n")
		wxT("var q = from c in cats\n")
		wxT("        from k in c.Keys\n")
		wxT("        join m in c.Meta on k equals m.Id\n")
		wxT("        select m.Tag + k;\n")
		wxT("r = \"\";\n")
		wxT("Foreach t In q Do\n")
		wxT("  r = r + t;\n")
		wxT("EndDo\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetString(), wxT("AaBbCc"));
}

// ===========================================================================
// THE EVAL PATH HAS A CURSOR OF ITS OWN
//
// `ibProcUnit::CompileExpression` — the one door behind the debugger's watch
// panel, `Evaluate(...)` and `Execute(...)` — goes PrepareLexem → GetExpression
// and never runs CompileModule(), which is where a module compile parks the
// lexem cursor at -1. The field carried no initialiser, so the eval walk began
// at whatever the heap held: the first GETLexem read past the end of the token
// array and raised ERROR_CODE_DEFINE ("Module code expected") for EVERY watch
// expression — `q.Execute()` and the literal `4` alike.
//
// Nothing caught it because nothing in tests/ had ever gone through eval: the
// path is reachable from a script only via the two built-ins below.
//
// A CONSTANT expression on purpose — it needs no name resolution, so a failure
// here is the cursor and cannot be scope binding.
// ===========================================================================

TEST_F(BuiltInRuntime, EvaluateComputesAConstantExpression) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc, wxT("var a public; a = Evaluate(\"2 + 2\");")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("a"), v));

	// A failed eval comes back as the string "<error: …>" (ibProcUnit::Evaluate's
	// reportFailure), so print the value rather than only its number — "expected
	// 4, actual 0" would hide the reason the engine already knows.
	EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER)
		<< "eval returned " << v.GetString().ToStdString();
	EXPECT_EQ(v.GetInteger(), 4);
}

// The same door, now with a NAME in the expression: the eval frame resolves it
// out of the host's frame (pppArrayList[1], wired by CompileExpression). Split
// from the case above so a scope-chain failure cannot be read as a cursor one.
TEST_F(BuiltInRuntime, EvaluateReadsAVariableOfTheHostModule) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		wxT("var k public; var a public;\n")
		wxT("k = 40;\n")
		wxT("a = Evaluate(\"k + 2\");\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("a"), v));
	EXPECT_EQ(v.GetType(), ibValueTypes::TYPE_NUMBER)
		<< "eval returned " << v.GetString().ToStdString();
	EXPECT_EQ(v.GetInteger(), 42);
}

// ===========================================================================
// `Cached` — the result is kept per argument tuple
//
// What is being asserted is NOT that the value is right (an uncached function
// returns the same value); it is that THE BODY DID NOT RUN AGAIN. So every
// case below counts calls in a module variable and reads the counter, which is
// the only thing that tells a working cache from a call that merely repeats
// cheaply.
// ===========================================================================

TEST(CachedFunction, SecondCallWithTheSameArgumentDoesNotRunTheBody) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var calls public; var r1 public; var r2 public;\n")
		wxT("Function Ten(x) Public Cached\n")
		wxT("  calls = calls + 1;\n")
		wxT("  Return x * 10;\n")
		wxT("EndFunction\n")
		wxT("calls = 0;\n")
		wxT("r1 = Ten(5);\n")
		wxT("r2 = Ten(5);\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r1"), v));
	EXPECT_EQ(v.GetInteger(), 50);
	ASSERT_TRUE(pu.GetPropVal(wxT("r2"), v));
	EXPECT_EQ(v.GetInteger(), 50) << "the kept result must equal the computed one";
	ASSERT_TRUE(pu.GetPropVal(wxT("calls"), v));
	EXPECT_EQ(v.GetInteger(), 1) << "the body ran twice — the second call was not served from the cache";
}

// A DIFFERENT tuple is a different entry, so the body runs again. Without this
// the test above would also pass for a cache that ignores its arguments and
// answers every call with the first result — the worst possible failure, since
// it returns a plausible number.
TEST(CachedFunction, ADifferentArgumentIsADifferentEntry) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var calls public; var r1 public; var r2 public; var r3 public;\n")
		wxT("Function Ten(x) Public Cached\n")
		wxT("  calls = calls + 1;\n")
		wxT("  Return x * 10;\n")
		wxT("EndFunction\n")
		wxT("calls = 0;\n")
		wxT("r1 = Ten(5);\n")
		wxT("r2 = Ten(7);\n")
		wxT("r3 = Ten(5);\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r1"), v));
	EXPECT_EQ(v.GetInteger(), 50);
	ASSERT_TRUE(pu.GetPropVal(wxT("r2"), v));
	EXPECT_EQ(v.GetInteger(), 70) << "a second argument was answered with the first one's result";
	ASSERT_TRUE(pu.GetPropVal(wxT("r3"), v));
	EXPECT_EQ(v.GetInteger(), 50);
	ASSERT_TRUE(pu.GetPropVal(wxT("calls"), v));
	EXPECT_EQ(v.GetInteger(), 2) << "expected one run per distinct argument";
}

// The tuple is keyed BY VALUE, not by type name or by text: 1 and "1" are
// different keys. A cache that joined its arguments into a string would answer
// the second call with the first result here.
TEST(CachedFunction, ANumberAndAStringThatSpellTheSameAreDifferentKeys) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var calls public; var r1 public; var r2 public;\n")
		wxT("Function Kind(x) Public Cached\n")
		wxT("  calls = calls + 1;\n")
		wxT("  Return calls;\n")
		wxT("EndFunction\n")
		wxT("calls = 0;\n")
		wxT("r1 = Kind(1);\n")
		wxT("r2 = Kind(\"1\");\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("calls"), v));
	EXPECT_EQ(v.GetInteger(), 2) << "the number 1 and the string \"1\" collapsed into one key";
}

// No arguments at all — the empty tuple is a key like any other, and the
// once-only case is the one a settings lookup actually uses.
TEST(CachedFunction, AFunctionWithoutArgumentsRunsOnce) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var calls public; var r1 public; var r2 public;\n")
		wxT("Function Once() Public Cached\n")
		wxT("  calls = calls + 1;\n")
		wxT("  Return 99;\n")
		wxT("EndFunction\n")
		wxT("calls = 0;\n")
		wxT("r1 = Once();\n")
		wxT("r2 = Once();\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r2"), v));
	EXPECT_EQ(v.GetInteger(), 99);
	ASSERT_TRUE(pu.GetPropVal(wxT("calls"), v));
	EXPECT_EQ(v.GetInteger(), 1);
}

// `Cached` is a SECOND AXIS, so it combines with an access modifier and neither
// owns a position: `Private Cached` and `Cached Private` are one declaration.
TEST(CachedFunction, CombinesWithAnAccessModifierInEitherOrder) {
	for (const wxString& modifiers : { wxString(wxT("Private Cached")), wxString(wxT("Cached Private")) }) {
		ibCompileCode cc(wxT("test"), wxT("memory"), false);
		const wxString src =
			wxT("var calls public; var r public;\n")
			wxT("Function Ten(x) ") + modifiers + wxT("\n")
			wxT("  calls = calls + 1;\n")
			wxT("  Return x * 10;\n")
			wxT("EndFunction\n")
			wxT("calls = 0;\n")
			wxT("r = Ten(4);\n")
			wxT("r = Ten(4);\n");
		ASSERT_TRUE(TryCompile(cc, src)) << modifiers.ToStdString();

		ibProcUnit pu;
		wxString strError;
		ASSERT_TRUE(RunBound(cc, pu, strError)) << modifiers.ToStdString() << ": " << strError.ToStdString();

		ibValue v;
		ASSERT_TRUE(pu.GetPropVal(wxT("calls"), v));
		EXPECT_EQ(v.GetInteger(), 1) << modifiers.ToStdString();
	}
}

// A raise is not a result. The failed call must leave nothing behind, or the
// next call with those arguments would be answered with a value the function
// never returned.
TEST(CachedFunction, ACallThatRaisedIsNotKept) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var calls public; var r public;\n")
		wxT("Function Guarded(x) Public Cached\n")
		wxT("  calls = calls + 1;\n")
		wxT("  If calls < 2 Then\n")
		wxT("    Raise(\"first attempt fails\");\n")
		wxT("  EndIf;\n")
		wxT("  Return x * 10;\n")
		wxT("EndFunction\n")
		wxT("calls = 0;\n")
		wxT("Try\n")
		wxT("  r = Guarded(3);\n")
		wxT("Except\n")
		wxT("EndTry;\n")
		wxT("r = Guarded(3);\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("r"), v));
	EXPECT_EQ(v.GetInteger(), 30) << "the retry after a raise did not compute a result";
	ASSERT_TRUE(pu.GetPropVal(wxT("calls"), v));
	EXPECT_EQ(v.GetInteger(), 2) << "the raising call was kept and answered the retry";
}

// ===========================================================================
// Max / Min — the descending case
//
// `Max(3, 5)` always worked and `Max(5, 3)` HUNG: the index advanced inside the
// comparison (`maxValue = paParams[i++]`), so a losing candidate left i where it
// was and the same argument was compared forever. It survived because neither
// function had a test and because a hang is the one failure that reports
// nothing — the process is simply frozen, with no call to point at.
//
// The ascending case is here on purpose: without it a fix that always returned
// the first argument would pass.
// ===========================================================================

TEST_F(BuiltInRuntime, MaxAndMinTerminateWhateverTheArgumentOrder) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		// NOT named `descending` / `ascending` — those are LINQ keywords
		// (KEY_DESCENDING / KEY_ASCENDING), and the declaration fails with
		// "Identifier expected".
		wxT("var highFromDesc public; var highFromAsc public;\n")
		wxT("var lowFromDesc public; var lowFromAsc public;\n")
		wxT("var several public;\n")
		wxT("highFromDesc = Max(5, 3);\n")   // used to hang
		wxT("highFromAsc  = Max(3, 5);\n")
		wxT("lowFromDesc = Min(5, 3);\n")
		wxT("lowFromAsc  = Min(3, 5);\n")    // used to hang
		wxT("several = Max(2, 9, 4, 1);\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("highFromDesc"), v));
	EXPECT_EQ(v.GetInteger(), 5);
	ASSERT_TRUE(pu.GetPropVal(wxT("highFromAsc"), v));
	EXPECT_EQ(v.GetInteger(), 5);
	ASSERT_TRUE(pu.GetPropVal(wxT("lowFromDesc"), v));
	EXPECT_EQ(v.GetInteger(), 3);
	ASSERT_TRUE(pu.GetPropVal(wxT("lowFromAsc"), v));
	EXPECT_EQ(v.GetInteger(), 3);
	ASSERT_TRUE(pu.GetPropVal(wxT("several"), v));
	EXPECT_EQ(v.GetInteger(), 9) << "the winner was not the largest of four";
}

// ===========================================================================
// StrCountOccur / StrLineCount — the two that answered with a POSITION
//
// Both were `Find(...)` under a counting name: StrCountOccur returned where the
// first occurrence was, StrLineCount returned where the first line break was,
// plus one. The second is the worse of the two — a text with no break at all
// gave npos + 1 == 0, "a text with no lines" — because it is the answer a loop
// bound is written from.
// ===========================================================================

TEST_F(BuiltInRuntime, StringCountingFunctionsCountRatherThanLocate) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		wxT("var occurNone public; var occurOne public; var occurThree public;\n")
		wxT("var linesOne public; var linesThree public; var linesCrLf public;\n")
		wxT("var lineTwo public;\n")
		wxT("occurNone  = StrCountOccur(\"abcabc\", \"z\");\n")
		wxT("occurOne   = StrCountOccur(\"abcabc\", \"bca\");\n")
		wxT("occurThree = StrCountOccur(\"aaa\", \"a\");\n")
		wxT("linesOne   = StrLineCount(\"one line, no break\");\n")
		wxT("linesThree = StrLineCount(\"a\" + Chr(10) + \"b\" + Chr(10) + \"c\");\n")
		wxT("linesCrLf  = StrLineCount(\"a\" + Chr(13) + Chr(10) + \"b\");\n")
		wxT("lineTwo    = StrGetLine(\"a\" + Chr(10) + \"b\", 2);\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("occurNone"), v));
	EXPECT_EQ(v.GetInteger(), 0);
	ASSERT_TRUE(pu.GetPropVal(wxT("occurOne"), v));
	EXPECT_EQ(v.GetInteger(), 1) << "a single occurrence away from position 0 was reported as its position";
	ASSERT_TRUE(pu.GetPropVal(wxT("occurThree"), v));
	EXPECT_EQ(v.GetInteger(), 3);

	ASSERT_TRUE(pu.GetPropVal(wxT("linesOne"), v));
	EXPECT_EQ(v.GetInteger(), 1) << "a text with no line break must still be one line, not zero";
	ASSERT_TRUE(pu.GetPropVal(wxT("linesThree"), v));
	EXPECT_EQ(v.GetInteger(), 3);
	ASSERT_TRUE(pu.GetPropVal(wxT("linesCrLf"), v));
	EXPECT_EQ(v.GetInteger(), 2) << "CRLF must count as one break — StrGetLine walks it that way";

	// The two must agree about what a line IS, which is why StrGetLine is here.
	ASSERT_TRUE(pu.GetPropVal(wxT("lineTwo"), v));
	EXPECT_EQ(v.GetString(), wxT("b"));
}

// ===========================================================================
// Where a string position STARTS — ONE, for both of them
//
// Find was always 1-based on purpose (`nStart - 1` going in, `+ 1` coming out,
// 0 left free to mean "not found"). Mid used to hand its argument straight to
// ibString::Mid, which counts from zero, so `Mid(s, Find(s, x))` was off by a
// character — and an omitted length meant ONE character rather than the rest.
// Both were aligned on 2026-09-04 (Max's call, a deliberate breaking change).
// The composition below is the point of the test: the two functions have to be
// usable together, which is what they are for.
// ===========================================================================

TEST_F(BuiltInRuntime, StringPositionsAsTheyActuallyAre) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		wxT("var found public; var missing public;\n")
		wxT("var midTwo public; var midDefault public; var composed public;\n")
		wxT("var leftThree public; var rightThree public;\n")
		wxT("found      = Find(\"abcdef\", \"c\");\n")
		wxT("missing    = Find(\"abcdef\", \"z\");\n")
		wxT("midTwo     = Mid(\"abcdef\", 2, 3);\n")
		wxT("midDefault = Mid(\"abcdef\", 2);\n")
		wxT("composed   = Mid(\"abcdef\", Find(\"abcdef\", \"c\"));\n")
		wxT("leftThree  = Left(\"abcdef\", 3);\n")
		wxT("rightThree = Right(\"abcdef\", 3);\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("found"), v));
	EXPECT_EQ(v.GetInteger(), 3) << "Find is 1-based: 'c' is the third character";
	ASSERT_TRUE(pu.GetPropVal(wxT("missing"), v));
	EXPECT_EQ(v.GetInteger(), 0) << "not found is 0, which is why Find is 1-based";

	ASSERT_TRUE(pu.GetPropVal(wxT("midTwo"), v));
	EXPECT_EQ(v.GetString(), wxT("bcd")) << "Mid counts from ONE, the same as Find answers";
	ASSERT_TRUE(pu.GetPropVal(wxT("midDefault"), v));
	EXPECT_EQ(v.GetString(), wxT("bcdef")) << "an omitted length means the REST of the string";

	// THE POINT OF THE ALIGNMENT: the position Find answers with is the position
	// Mid takes. If these two ever disagree again, this is the line that says so.
	ASSERT_TRUE(pu.GetPropVal(wxT("composed"), v));
	EXPECT_EQ(v.GetString(), wxT("cdef")) << "Mid(s, Find(s, x)) must start AT the match";

	ASSERT_TRUE(pu.GetPropVal(wxT("leftThree"), v));
	EXPECT_EQ(v.GetString(), wxT("abc"));
	ASSERT_TRUE(pu.GetPropVal(wxT("rightThree"), v));
	EXPECT_EQ(v.GetString(), wxT("def"));
}

// ===========================================================================
// PROBE — the calendar, as it actually answers
//
// 2024-01-01 was a Monday, so a full week of known days runs through here. The
// expectations below are ISO (Monday = 1 … Sunday = 7); where the engine
// disagrees the test says so by name rather than by a number nobody can place.
// ===========================================================================

TEST_F(BuiltInRuntime, DayOfWeekIsIsoAndTheWeekIsSevenDays) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		wxT("var mon public; var tue public; var wed public; var thu public;\n")
		wxT("var fri public; var sat public; var sun public;\n")
		wxT("var weekSpan public; var begIsMonday public;\n")
		wxT("mon = GetDayOfWeek(Date(2024, 1, 1));\n")
		wxT("tue = GetDayOfWeek(Date(2024, 1, 2));\n")
		wxT("wed = GetDayOfWeek(Date(2024, 1, 3));\n")
		wxT("thu = GetDayOfWeek(Date(2024, 1, 4));\n")
		wxT("fri = GetDayOfWeek(Date(2024, 1, 5));\n")
		wxT("sat = GetDayOfWeek(Date(2024, 1, 6));\n")
		wxT("sun = GetDayOfWeek(Date(2024, 1, 7));\n")
		// A week must span seven days, whatever day it is asked about.
		wxT("weekSpan   = GetDayOfYear(EndOfWeek(Date(2024, 1, 3))) - GetDayOfYear(BegOfWeek(Date(2024, 1, 3)));\n")
		wxT("begIsMonday = GetDayOfWeek(BegOfWeek(Date(2024, 1, 3)));\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	auto dayOf = [&](const wxChar* name) {
		ibValue v;
		EXPECT_TRUE(pu.GetPropVal(name, v));
		return v.GetInteger();
	};

	EXPECT_EQ(dayOf(wxT("mon")), 1) << "Monday";
	EXPECT_EQ(dayOf(wxT("tue")), 2) << "Tuesday";
	EXPECT_EQ(dayOf(wxT("wed")), 3) << "Wednesday";
	EXPECT_EQ(dayOf(wxT("thu")), 4) << "Thursday";
	EXPECT_EQ(dayOf(wxT("fri")), 5) << "Friday";
	EXPECT_EQ(dayOf(wxT("sat")), 6) << "Saturday";
	EXPECT_EQ(dayOf(wxT("sun")), 7) << "Sunday";

	EXPECT_EQ(dayOf(wxT("weekSpan")), 6) << "a week from its first day to its last spans six days";
	EXPECT_EQ(dayOf(wxT("begIsMonday")), 1) << "the week must begin on a Monday";
}

// PROBE — is the DATE itself right, before blaming the weekday?
TEST_F(BuiltInRuntime, ACalendarDateKeepsItsOwnComponents) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		wxT("var y public; var m public; var d public;\n")
		wxT("var doy public; var woy public;\n")
		wxT("y = GetYear(Date(2024, 1, 1));\n")
		wxT("m = GetMonth(Date(2024, 1, 1));\n")
		wxT("d = GetDay(Date(2024, 1, 1));\n")
		wxT("doy = GetDayOfYear(Date(2024, 1, 1));\n")
		wxT("woy = GetWeekOfYear(Date(2024, 1, 1));\n")));

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("y"), v));   EXPECT_EQ(v.GetInteger(), 2024) << "year";
	ASSERT_TRUE(pu.GetPropVal(wxT("m"), v));   EXPECT_EQ(v.GetInteger(), 1)    << "month";
	ASSERT_TRUE(pu.GetPropVal(wxT("d"), v));   EXPECT_EQ(v.GetInteger(), 1)    << "day";
	ASSERT_TRUE(pu.GetPropVal(wxT("doy"), v)); EXPECT_EQ(v.GetInteger(), 1)    << "day of year";
	ASSERT_TRUE(pu.GetPropVal(wxT("woy"), v)); EXPECT_EQ(v.GetInteger(), 1)    << "week of year";
}

// ===========================================================================
// A BUILT-IN OF NEGATIVE ARITY TAKES WHAT IT IS GIVEN
//
// `Max` and `Min` are registered with arity -1 — "as many as it is given" —
// and the help publishes `Max(num : number, ...)`. They were UNCALLABLE:
// every call, at every count including one, answered "Too many parameters
// passed to 'Max'", because a negative arity declares an EMPTY parameter list
// and the compiler's arity check read the caller's first argument as one too
// many.
//
// The flag that says otherwise (ibFunction::m_valueVariadic) was set at
// registration and never carried in the bytecode, so it survived only while a
// LIVE compile context resolved the name — and a cache hit is precisely the
// case where none exists. Third field of this shape after m_needsHeapFrame and
// m_valueCached.
//
// ⚠ WHAT THIS TEST DOES NOT DO, said plainly so nobody trusts it for that: it
// CANNOT catch the defect it was written after. Here the context is live — the
// fixture registers the built-ins itself — so the flag is set and the call
// compiles with or without the bytecode carrying it. What the shipped engine
// does instead is resolve through a CACHED bytecode, and that road is pinned by
// the round trip in test_byteCodeAOT.cpp (ListFuncWithLocalsAndParams, which
// asserts all three flags come back).
// This one guards the other half: that the compiler's arity checks keep letting
// a negative arity through at all.
// ===========================================================================

TEST_F(BuiltInRuntime, AVariadicBuiltInAcceptsEveryCount) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);

	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);

	ASSERT_TRUE(TryCompile(cc,
		wxT("var one public; var two public; var many public;\n")
		wxT("one  = Max(42);\n")
		wxT("two  = Max(3, 9);\n")
		wxT("many = Min(10, 2, 7, 5);\n")))
		<< "a negative-arity built-in refused a call the help says it takes";

	ibProcUnit pu;
	wxString strError;
	ASSERT_TRUE(RunBound(cc, pu, strError)) << strError.ToStdString();

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("one"), v));
	EXPECT_EQ(v.GetInteger(), 42) << "one argument is a legitimate count for Max";
	ASSERT_TRUE(pu.GetPropVal(wxT("two"), v));
	EXPECT_EQ(v.GetInteger(), 9);
	ASSERT_TRUE(pu.GetPropVal(wxT("many"), v));
	EXPECT_EQ(v.GetInteger(), 2);
}
