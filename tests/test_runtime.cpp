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

#include <wx/debug.h>

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

class ScopedCodeStyle {
public:
	explicit ScopedCodeStyle(short style)
		: m_prev(ibCompileCode::GetCodeStyle())
	{
		wxDisableAsserts();
		ibCompileCode::SetCodeStyle(style);
	}
	~ScopedCodeStyle() {
		ibCompileCode::SetCodeStyle(m_prev);
	}
private:
	short m_prev;
};

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
	ASSERT_TRUE(TryCompile(cc, wxT("var a export; a = 42;")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	EXPECT_TRUE(pu.GetPropVal(wxT("a"), val));
	EXPECT_EQ(val.GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(val.GetInteger(), 42);
}

TEST(RuntimeTest, ArithmeticAddition) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("var a export; a = 1 + 2;")));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue val;
	EXPECT_TRUE(pu.GetPropVal(wxT("a"), val));
	EXPECT_EQ(val.GetInteger(), 3);
}

TEST(RuntimeTest, ArithmeticAllOps) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var sum    export;\n")
		wxT("var diff   export;\n")
		wxT("var prod   export;\n")
		wxT("var quot   export;\n")
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
	ASSERT_TRUE(TryCompile(cc, wxT("var s export; s = \"hello, \" + \"world\";")));

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
		wxT("var lt export; var gt export; var eq export;\n")
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
		wxT("var a export;\n")
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
		wxT("var a export;\n")
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
		wxT("var a export;\n")
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
		wxT("var i export;\n")
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
		wxT("Function Square(x) Export\n")
		wxT("  Return x * x;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret;
	ibValue arg(7);
	ibValue* params[] = { &arg, nullptr };
	EXPECT_TRUE(pu.CallAsFunc(wxT("Square"), ret, params, 1));
	EXPECT_EQ(ret.GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(ret.GetInteger(), 49);
}

TEST(RuntimeTest, CallFunctionWithMultipleParams) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Add3(a, b, c) Export\n")
		wxT("  Return a + b + c;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret, a(10), b(20), c(30);
	ibValue* params[] = { &a, &b, &c, nullptr };
	EXPECT_TRUE(pu.CallAsFunc(wxT("Add3"), ret, params, 3));
	EXPECT_EQ(ret.GetInteger(), 60);
}

TEST(RuntimeTest, RecursiveFactorial) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Fact(n) Export\n")
		wxT("  If n <= 1 Then\n")
		wxT("    Return 1;\n")
		wxT("  EndIf;\n")
		wxT("  Return n * Fact(n - 1);\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue ret, n(6);
	ibValue* params[] = { &n, nullptr };
	EXPECT_TRUE(pu.CallAsFunc(wxT("Fact"), ret, params, 1));
	EXPECT_EQ(ret.GetInteger(), 720);
}

TEST(RuntimeTest, LambdaCapturesOuterParameter) {
	ScopedCodeStyle style(CODE_VES);
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function MakeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue add5;
	ibValue n(5);
	ibValue* params[] = { &n, nullptr };
	ASSERT_TRUE(pu.CallAsFunc(wxT("MakeAdder"), add5, params, 1));

	ibValue x(7);
	ibValue ret;
	ASSERT_TRUE(InvokeLambdaWithArg(add5, x, ret));
	EXPECT_EQ(ret.GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(ret.GetInteger(), 12);
}

TEST(RuntimeTest, LambdaCapturesMutableOuterLocal) {
	ScopedCodeStyle style(CODE_VES);
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function MakeCounter(start)\n")
		wxT("  var value;\n")
		wxT("  value = start;\n")
		wxT("  Return Function(step)\n")
		wxT("           value = value + step;\n")
		wxT("           Return value;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue counter;
	ibValue start(10);
	ibValue* params[] = { &start, nullptr };
	ASSERT_TRUE(pu.CallAsFunc(wxT("MakeCounter"), counter, params, 1));

	ibValue step2(2);
	ibValue ret;
	ASSERT_TRUE(InvokeLambdaWithArg(counter, step2, ret));
	EXPECT_EQ(ret.GetInteger(), 12);

	ibValue step3(3);
	ASSERT_TRUE(InvokeLambdaWithArg(counter, step3, ret));
	EXPECT_EQ(ret.GetInteger(), 15);
}

// ===========================================================================
// Module-level state survives across calls — global variable mutated by
// procedure persists into the next call.
// ===========================================================================

TEST(RuntimeTest, ModuleVariablePersistsAcrossCalls) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("var counter export;\n")
		wxT("counter = 0;\n")
		wxT("Procedure Bump() Export\n")
		wxT("  counter = counter + 1;\n")
		wxT("EndProcedure\n");
	ASSERT_TRUE(TryCompile(cc, src));

	ibProcUnit pu;
	ASSERT_TRUE(TryExecute(pu, cc.m_cByteCode));

	ibValue* params[] = { nullptr };
	EXPECT_TRUE(pu.CallAsProc(wxT("Bump"), params, 0));
	EXPECT_TRUE(pu.CallAsProc(wxT("Bump"), params, 0));
	EXPECT_TRUE(pu.CallAsProc(wxT("Bump"), params, 0));

	ibValue v;
	ASSERT_TRUE(pu.GetPropVal(wxT("counter"), v));
	EXPECT_EQ(v.GetInteger(), 3);
}
