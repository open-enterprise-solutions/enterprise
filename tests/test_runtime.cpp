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

namespace {

bool TryCompile(ibCompileCode& cc, const wxString& src) {
	try {
		return cc.Compile(src);
	} catch (...) {
		return false;
	}
}

bool TryExecute(ibProcUnit& pu, const ibByteCode& bc) {
	try {
		pu.Execute(bc);
		return true;
	} catch (...) {
		return false;
	}
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
