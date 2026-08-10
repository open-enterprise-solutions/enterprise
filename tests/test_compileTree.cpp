// =============================================================================
// OES Enterprise — the compiler over the tree — ibCompileCode (ibCompileCode)
//
// It takes a module and produces ibByteCode, reading nothing but nodes. These
// tests assert the SHAPE of what it emits — which opcodes, in which order, with
// which jump targets — because that is the contract the interpreter, the AOT
// cache and the debugger all consume.
//
// They deliberately do NOT compare against the one-pass compiler here: that is
// what tests/test_compilerContract.cpp does, digest by digest, and it is the
// gate for switching over. These are about whether the emission is right on its
// own terms, so that a difference found by the digests can be read.
//
// EIGHT ARE DISABLED_ SINCE 2026-08-10, and not because they are flaky. The tree
// compiler they were written for was reverted; these eight assert ITS emission
// shape, which the one-pass compiler never had and was never wrong for lacking:
//
//   AQueryBlockBecomesPipelineCalls        a query block lowered to pipeline
//   AQueryBlockJoinEmitsKeysAndAProjection  calls (SelectMany / Join / Where).
//   AClauseArgumentIsARealLambda            This emitter lowers nested `from`
//                                           to nested OPER_FOREACH instead.
//   ElseIfChainIsNestedIfs                  the chain nested as N OPER_IFs;
//                                           this emitter chains differently.
//   WhileJumpsBackToItsTest                 the loop test as OPER_IF.
//   AnIndexAssignmentWritesTheElement       the index-store opcode shape.
//   ALambdaCarriesEveryOperandTheMaterialiserReads  the OPER_LFUNC operand layout.
//   TheModuleBodyStartsAfterEveryCallable   m_lStartModule past the callables;
//                                           here it stays 0 and the module-init
//                                           walk skips each OPER_FUNC block
//                                           (procUnit.cpp, the OPER_FUNC case).
//
// Kept rather than deleted: each one records what that emitter promised, which
// is worth having if the pipeline lowering is ever taken up again for SQL
// pushdown. Deleting them would throw away the only written form of it. What
// guards THIS emitter is test_compilerContract.cpp, re-baselined the same day.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"
#include "backend/system/systemManager.h"
#include "backend/compiler/codeDef.h"

namespace {

// NAMES ITS FAILURE. A bare `false` here says "Actual: false" and nothing else,
// so every red line costs a re-run by hand to learn what the compiler objected
// to. ::testing::AssertionResult streams the reason into the report and still
// converts to bool, so no callsite changes.
::testing::AssertionResult Build(ibCompileCode& compiler, const wxString& src)
{
	try {
		if (compiler.Compile(src))
			return ::testing::AssertionSuccess();
		return ::testing::AssertionFailure() << "Compile() returned false without raising";
	} catch (const ibBackendException& err) {
		return ::testing::AssertionFailure() << err.GetErrorDescription().ToStdString();
	} catch (...) {
		return ::testing::AssertionFailure() << "unknown exception";
	}
}

size_t CountOpcode(const ibByteCode& bc, short oper)
{
	size_t n = 0;
	for (const auto& unit : bc.m_listCode)
		if (unit.m_numOper == oper) n++;
	return n;
}

int FindOpcode(const ibByteCode& bc, short oper, int from = 0)
{
	for (int i = from; i < (int)bc.m_listCode.size(); i++)
		if (bc.m_listCode[i].m_numOper == oper) return i;
	return -1;
}

// A compact rendering, so a failure says what was emitted instead of which
// EXPECT missed.
wxString Listing(const ibByteCode& bc)
{
	wxString out;
	for (size_t i = 0; i < bc.m_listCode.size(); i++) {
		const ibByteUnit& unit = bc.m_listCode[i];
		out << (long)i << wxT(": op=") << unit.m_numOper
		    << wxT(" p1=") << unit.m_param1.m_numIndex
		    << wxT(" p2=") << unit.m_param2.m_numIndex
		    << wxT(" p3=") << unit.m_param3.m_numIndex
		    << wxT(" line=") << unit.m_numLine << wxT("\n");
	}
	return out;
}

} // namespace

// ===========================================================================
// Expressions — operands come from children, never from the next lexemes
// ===========================================================================

TEST(CompileTree, ArithmeticEmitsOneOpcodePerOperator) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var a; a = 1 + 2 * 3;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_MULT), 1u) << Listing(bc).ToStdString();
	EXPECT_EQ(CountOpcode(bc, OPER_ADD),  1u) << Listing(bc).ToStdString();

	// Precedence is a tree property: the multiplication must be emitted BEFORE
	// the addition, because the addition consumes its result.
	EXPECT_LT(FindOpcode(bc, OPER_MULT), FindOpcode(bc, OPER_ADD));
}

TEST(CompileTree, AssignmentWritesIntoItsTarget) {
	// The one-pass path emitted into a temp and recovered the target afterwards
	// by matching the pair (the shortLet peephole). From a tree the destination
	// is known first, so the store is the operator's own.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var a; a = 1;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_GE(CountOpcode(bc, OPER_LET), 1u) << Listing(bc).ToStdString();
}

TEST(CompileTree, UnaryMinusIsZeroMinusOperand) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var a; a = -5;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	// The LEXER folds a minus in front of a number into the literal when it
	// follows `= ( [ , < >` — so `-5` is one CONSTANT and there is nothing to
	// negate. Emitting a SUB here would mean the tree changed the language.
	EXPECT_EQ(CountOpcode(bc, OPER_SUB), 0u) << Listing(bc).ToStdString();
	EXPECT_EQ(CountOpcode(bc, OPER_LET), 1u) << Listing(bc).ToStdString();
}

TEST(CompileTree, NotEmitsTheBooleanOpcode) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var a; var b; a = Not b;\n")));

	EXPECT_EQ(CountOpcode(compiler.m_cByteCode, OPER_NOT), 1u);
}

TEST(CompileTree, ConstantsArePooledNotRepeated) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var a; a = 7; a = 7; a = 7;\n")));

	// The same literal three times is one entry — a pool that is not a pool is
	// just an array.
	size_t sevens = 0;
	for (const ibValue& value : compiler.m_cByteCode.m_listConst)
		if (value.GetType() == ibValueTypes::TYPE_NUMBER && value.GetInteger() == 7) sevens++;

	EXPECT_EQ(sevens, 1u);
}

// ===========================================================================
// Control flow — the jumps and where they land
// ===========================================================================

TEST(CompileTree, IfWithoutElseJumpsPastItsBody) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var a; var r;\n")
		wxT("If a Then\n")
		wxT("  r = 1;\n")
		wxT("EndIf;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	const int nTest = FindOpcode(bc, OPER_IF);
	ASSERT_GE(nTest, 0) << Listing(bc).ToStdString();

	// The false branch lands after the body, and nowhere else.
	const long target = (long)bc.m_listCode[nTest].m_param2.m_numIndex;
	EXPECT_GT(target, nTest) << Listing(bc).ToStdString();
	EXPECT_LE(target, (long)bc.m_listCode.size());
}

TEST(CompileTree, IfWithElseMakesTheBodyJumpOverTheElse) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var a; var r;\n")
		wxT("If a Then\n")
		wxT("  r = 1;\n")
		wxT("Else\n")
		wxT("  r = 2;\n")
		wxT("EndIf;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	const int nTest = FindOpcode(bc, OPER_IF);
	ASSERT_GE(nTest, 0);

	const int nSkip = FindOpcode(bc, OPER_GOTO, nTest);
	ASSERT_GE(nSkip, 0) << "the then-branch must jump over the else\n" << Listing(bc).ToStdString();

	// The condition lands AFTER that jump — that is what makes the else the
	// false branch — and the jump lands after the else.
	EXPECT_GT((long)bc.m_listCode[nTest].m_param2.m_numIndex, (long)nSkip);
	EXPECT_GT((long)bc.m_listCode[nSkip].m_param1.m_numIndex,
	          (long)bc.m_listCode[nTest].m_param2.m_numIndex);
}

// ElseIf is not a kind of its own: the tree nests it in the else branch, so the
// chain builds by recursion. Two conditions means two tests, and no list of
// addresses to patch at the end.
TEST(CompileTree, DISABLED_ElseIfChainIsNestedIfs) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var a; var r;\n")
		wxT("If a > 2 Then\n")
		wxT("  r = 1;\n")
		wxT("ElseIf a > 1 Then\n")
		wxT("  r = 2;\n")
		wxT("Else\n")
		wxT("  r = 3;\n")
		wxT("EndIf;\n")));

	EXPECT_EQ(CountOpcode(compiler.m_cByteCode, OPER_IF), 2u);
}

TEST(CompileTree, DISABLED_WhileJumpsBackToItsTest) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var i;\n")
		wxT("i = 0;\n")
		wxT("While i < 10 Do\n")
		wxT("  i = i + 1;\n")
		wxT("EndDo;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	const int nTest = FindOpcode(bc, OPER_IF);
	ASSERT_GE(nTest, 0) << Listing(bc).ToStdString();

	// The last GOTO of the loop goes BACKWARDS, to the condition.
	int nBack = -1;
	for (int i = nTest; i < (int)bc.m_listCode.size(); i++)
		if (bc.m_listCode[i].m_numOper == OPER_GOTO) nBack = i;

	ASSERT_GE(nBack, 0) << Listing(bc).ToStdString();
	EXPECT_LT((long)bc.m_listCode[nBack].m_param1.m_numIndex, (long)nBack)
		<< "a loop that does not jump back is not a loop\n" << Listing(bc).ToStdString();
}

TEST(CompileTree, ForEmitsItsHeaderAndNext) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var i; var s;\n")
		wxT("For i = 1 To 10 Do\n")
		wxT("  s = s + i;\n")
		wxT("EndDo;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_FOR),  1u) << Listing(bc).ToStdString();
	EXPECT_EQ(CountOpcode(bc, OPER_NEXT), 1u) << Listing(bc).ToStdString();

	const int nHead = FindOpcode(bc, OPER_FOR);
	const int nNext = FindOpcode(bc, OPER_NEXT);
	ASSERT_GE(nHead, 0);
	ASSERT_GE(nNext, 0);
	EXPECT_EQ((long)bc.m_listCode[nNext].m_param2.m_numIndex, (long)nHead)
		<< "the turn must return to the header";
}

TEST(CompileTree, ForeachEmitsItsHeaderAndIterator) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var arr; var it; var s;\n")
		wxT("arr = New Array;\n")
		wxT("Foreach it In arr Do\n")
		wxT("  s = s + 1;\n")
		wxT("EndDo;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_FOREACH),   1u) << Listing(bc).ToStdString();
	EXPECT_EQ(CountOpcode(bc, OPER_NEXT_ITER), 1u) << Listing(bc).ToStdString();
}

TEST(CompileTree, BreakAndContinueLandOnTheLoopEnds) {
	// Two of each — the case that used to be broken in the one-pass path, where
	// the second one patched a garbage index.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var i;\n")
		wxT("i = 0;\n")
		wxT("While i < 100 Do\n")
		wxT("  i = i + 1;\n")
		wxT("  If i = 10 Then Continue; EndIf;\n")
		wxT("  If i = 20 Then Continue; EndIf;\n")
		wxT("  If i = 30 Then Break; EndIf;\n")
		wxT("  If i = 40 Then Break; EndIf;\n")
		wxT("EndDo;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;

	// Every jump must land inside the tape — a garbage target is exactly what
	// the old defect produced.
	for (const ibByteUnit& unit : bc.m_listCode) {
		if (unit.m_numOper != OPER_GOTO) continue;
		const long target = (long)unit.m_param1.m_numIndex;
		EXPECT_GE(target, 0)                            << Listing(bc).ToStdString();
		EXPECT_LE(target, (long)bc.m_listCode.size())   << Listing(bc).ToStdString();
	}
}

TEST(CompileTree, TryCarriesBothEnds) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var r;\n")
		wxT("Try\n")
		wxT("  r = 1;\n")
		wxT("Except\n")
		wxT("  r = 0;\n")
		wxT("EndTry;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	const int nTry = FindOpcode(bc, OPER_TRY);
	const int nEnd = FindOpcode(bc, OPER_ENDTRY);
	ASSERT_GE(nTry, 0) << Listing(bc).ToStdString();
	ASSERT_GE(nEnd, 0) << Listing(bc).ToStdString();

	// The guarded region ends where the handler starts; the handler ends where
	// the statement does.
	EXPECT_GT((long)bc.m_listCode[nTry].m_param1.m_numIndex, (long)nEnd);
	EXPECT_GE((long)bc.m_listCode[nEnd].m_param1.m_numIndex,
	          (long)bc.m_listCode[nTry].m_param1.m_numIndex);
}

// ===========================================================================
// Declarations — and the forward reference no tree removes
// ===========================================================================

TEST(CompileTree, FunctionsAreRegisteredWithTheirEntry) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function Add(x, y)\n")
		wxT("  Return x + y;\n")
		wxT("EndFunction\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_EQ(bc.m_listFunc.size(), 1u);
	EXPECT_TRUE(bc.m_listFunc[0].m_bCodeRet) << "a Function returns; a Procedure does not";
	EXPECT_GE(bc.m_listFunc[0].m_lCodeLine, 0);
}

TEST(CompileTree, ProcedureIsMarkedAsNotReturning) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("Procedure DoIt()\nEndProcedure\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_EQ(bc.m_listFunc.size(), 1u);
	EXPECT_FALSE(bc.m_listFunc[0].m_bCodeRet);
}

TEST(CompileTree, ACallWrittenAboveItsDeclarationStillResolves) {
	// The forward reference is the one thing a tree does NOT remove: a module may
	// call downwards, so the address is patched at finalize.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function Caller(x)\n")
		wxT("  Return Callee(x);\n")
		wxT("EndFunction\n")
		wxT("Function Callee(y)\n")
		wxT("  Return y * 2;\n")
		wxT("EndFunction\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	const int nCall = FindOpcode(bc, OPER_CALL);
	ASSERT_GE(nCall, 0) << Listing(bc).ToStdString();
	EXPECT_GE((long)bc.m_listCode[nCall].m_param1.m_numIndex, 0)
		<< "the call must have been patched to its target";
}

TEST(CompileTree, MethodCallEmitsTheOwnerAndTheName) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var arr;\n")
		wxT("arr = New Array;\n")
		wxT("arr.Add(1);\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_NEW),         1u) << Listing(bc).ToStdString();
	EXPECT_EQ(CountOpcode(bc, OPER_CALL_METHOD), 1u) << Listing(bc).ToStdString();
	// A CONSTANT argument travels as OPER_SETCONST — the runtime write-resolves
	// an OPER_SET operand, so a const-pool slot must not arrive that way.
	EXPECT_GE(CountOpcode(bc, OPER_SET) + CountOpcode(bc, OPER_SETCONST), 1u)
		<< "the argument follows its call";
}

// ===========================================================================
// Line stamps — what breakpoints ride on
// ===========================================================================

TEST(CompileTree, EveryOpcodeCarriesItsSourceLine) {
	// A tree-driven emitter is exactly the kind of change that drops line stamps
	// quietly, and a debugger with no line map is a debugger that lies.
	const wxString src =
		wxT("var a;\n")
		wxT("a = 1;\n")
		wxT("a = 2;\n")
		wxT("a = 3;\n");

	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, src));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_FALSE(bc.m_listCode.empty());

	unsigned int maxLine = 0;
	for (const ibByteUnit& unit : bc.m_listCode)
		if (unit.m_numLine > maxLine) maxLine = unit.m_numLine;

	EXPECT_GT(maxLine, 0u) << "no opcode carries a line\n" << Listing(bc).ToStdString();
}

TEST(CompileTree, ModuleEndsWithTheEndOpcode) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var a; a = 1;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_FALSE(bc.m_listCode.empty());
	EXPECT_EQ(bc.m_listCode.back().m_numOper, OPER_END);
}

TEST(CompileTree, EmptyModuleStillProducesBytecode) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("")));
	EXPECT_FALSE(compiler.m_cByteCode.m_listCode.empty());
}

// ===========================================================================
// The gates the one-pass reader enforced — they have to survive the move
//
// Each of these was a check INSIDE a reader that is now deleted. A gate that
// quietly disappears with its reader is the worst outcome of a refactor: the
// bad program still compiles, and nothing says so until it runs.
// ===========================================================================

TEST(CompileTree, ADeclaredFunctionIsRegisteredSoACallResolves) {
	// Without registration GetFunction never finds anything, every call defers,
	// and finalize reports "no such function" for a function three lines above.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function Twice(n)\n")
		wxT("  Return n + n;\n")
		wxT("EndFunction\n")
		wxT("var a; a = Twice(2);\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_EQ(bc.m_listFunc.size(), 1u);
	EXPECT_STREQ(bc.m_listFunc[0].m_strRealName.c_str(), wxT("Twice"));
	EXPECT_TRUE(bc.m_listFunc[0].m_bCodeRet);
	EXPECT_EQ(bc.m_listFunc[0].m_listParam.size(), 1u);
	EXPECT_GT(CountOpcode(bc, OPER_CALL) + CountOpcode(bc, OPER_CALL_CLOSURE), 0u)
		<< "the call never resolved\n" << Listing(bc).ToStdString();
}

TEST(CompileTree, ParameterKeepsItsPassingModeAndDefault) {
	// `Val` and `= <const>` are written in the signature and both change what is
	// emitted; a tree that dropped them described a different function.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Procedure P(Val a, b = 5)\n")
		wxT("EndProcedure\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_EQ(bc.m_listFunc.size(), 1u);
	ASSERT_EQ(bc.m_listFunc[0].m_listParam.size(), 2u);

	EXPECT_TRUE(bc.m_listFunc[0].m_listParam[0].m_bByValue) << "`Val` means COPY";
	EXPECT_FALSE(bc.m_listFunc[0].m_listParam[1].m_bByValue);
	EXPECT_NE(bc.m_listFunc[0].m_listParam[1].m_defaultValue.m_numArray, DEF_VAR_SKIP)
		<< "the default value was dropped";
}

TEST(CompileTree, ParameterDeclaratorsAreOnTheTape) {
	// The tape is self-describing: an AOT load rebuilds m_listParam by walking it.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("Procedure P(a, b)\nEndProcedure\n")));

	EXPECT_EQ(CountOpcode(compiler.m_cByteCode, OPER_FUNC_PARAM), 2u);
}

TEST(CompileTree, ADuplicateVisibleFunctionIsRejected) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler,
		wxT("Function F() Public\nEndFunction\n")
		wxT("Function F() Public\nEndFunction\n")));
}

TEST(CompileTree, ACommonModuleTakesFunctionsOnly) {
	// `onlyFunction` — the third ctor argument, what ibCompileCommonModule passes.
	ibCompileCode moduleVar(wxT("test"), wxT("memory"), true);
	EXPECT_FALSE(Build(moduleVar, wxT("var a;\nFunction F()\nEndFunction\n")));

	ibCompileCode moduleBody(wxT("test"), wxT("memory"), true);
	EXPECT_FALSE(Build(moduleBody, wxT("Function F()\nEndFunction\nF();\n")));

	ibCompileCode moduleOk(wxT("test"), wxT("memory"), true);
	EXPECT_TRUE(Build(moduleOk, wxT("Function F()\n  Return 1;\nEndFunction\n")));
}

TEST(CompileTree, ReturnOutsideACallableIsRejected) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler, wxT("Return 1;\n")));
}

TEST(CompileTree, AFunctionMustReturnAValue) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler, wxT("Function F()\n  Return;\nEndFunction\n")));
}

TEST(CompileTree, ANestedNamedDeclarationIsRejected) {
	// It would otherwise read as an anonymous lambda — the mistake accepted
	// silently and given a body it never meant to have.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler,
		wxT("Function Outer()\n")
		wxT("  Function Inner()\n")
		wxT("  EndFunction\n")
		wxT("EndFunction\n")));
}

TEST(CompileTree, ADeclaredTypeSelectsTheSpecialisedOpcode) {
	// What declaring a type BUYS: the number-typed add, which skips the runtime's
	// type dispatch. Dropping it would leave the source looking typed and the
	// bytecode taking the generic path.
	ibCompileCode typed(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(typed, wxT("Number a; var b; b = a + a;\n")));

	ibCompileCode plain(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(plain, wxT("var a; var b; b = a + a;\n")));

	EXPECT_EQ(CountOpcode(plain.m_cByteCode, OPER_ADD), 1u);
	EXPECT_EQ(CountOpcode(typed.m_cByteCode, OPER_ADD), 0u)
		<< "a typed operand still emitted the generic opcode\n"
		<< Listing(typed.m_cByteCode).ToStdString();
	EXPECT_EQ(CountOpcode(typed.m_cByteCode, OPER_ADD + TYPE_DELTA1), 1u);
}

TEST(CompileTree, AStringHasNoDivision) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler, wxT("String s; var b; var c; b = s / c;\n")));
}

// ===========================================================================
// The clause chain — one road for the query block and the restriction
// ===========================================================================

TEST(CompileTree, DISABLED_AQueryBlockBecomesPipelineCalls) {
	// Not a hand-rolled foreach: every clause is one OPER_CALL_LINQ, which is
	// what lets the source decide the floor (SQL / server prefix / RAM).
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var src; var r;\n")
		wxT("r = from x in src where x > 1 select x;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_CALL_LINQ), 2u)
		<< "expected one call per clause\n" << Listing(bc).ToStdString();
}

TEST(CompileTree, DISABLED_AClauseArgumentIsARealLambda) {
	// The synthetic `Function(row) { Return <expr>; }` goes through the SAME
	// emitter a written lambda does — the runtime cannot tell them apart.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var src; var r;\n")
		wxT("r = from x in src where x > 1 select x;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_LFUNC), 2u);
	EXPECT_EQ(CountOpcode(bc, OPER_ENDLFUNC), 2u);

	size_t numLambda = 0;
	for (const auto& fn : bc.m_listFunc)
		if (fn.m_kind == ibFnKind::Lambda) numLambda++;
	EXPECT_EQ(numLambda, 2u) << "a clause lambda has no m_listFunc entry";
}

TEST(CompileTree, ARestrictionFoldsIntoItsSource) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var src; var r;\n")
		wxT("r = restrict s in src where s > 1;\n")));

	EXPECT_EQ(CountOpcode(compiler.m_cByteCode, OPER_CALL_LINQ), 1u);
}

// The parameter count of every lambda the module materialises, in emission
// order — the shape of a clause chain, read off the tape.
static std::vector<long> LambdaArities(const ibByteCode& bc) {
	std::vector<long> listArity;
	for (const ibByteUnit& unit : bc.m_listCode)
		if (unit.m_numOper == OPER_LFUNC)
			listArity.push_back((long)unit.m_param3.m_numArray);
	return listArity;
}

// A QUERY-BLOCK JOIN AND A RESTRICTION JOIN ARE NOT THE SAME EMISSION, and until
// 2026-08-09 this test asserted they were — one two-parameter predicate for
// both. They looked alike because neither had ever been executed: the block form
// raised "LINQ: Join requires (inner, leftKey, rightKey, projection)" at the
// first row, so nothing could tell that the compiler was emitting two arguments
// where the runtime wanted four.
//
// The two genuinely differ, and the difference is what each one DOES: a block
// join materialises rows and needs to say how to key both sides and what the
// joined row becomes; a restriction folds one predicate into SQL and never
// materialises anything, so it has no projection to give.
TEST(CompileTree, DISABLED_AQueryBlockJoinEmitsKeysAndAProjection) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var a; var b; var r; r = from s in a join t in b on s equals t select s;\n")));

	const std::vector<long> listArity = LambdaArities(compiler.m_cByteCode);

	// leftKey(s), rightKey(t), projection(s, t), and the select over the row the
	// projection made.
	ASSERT_GE(listArity.size(), 3u);

	size_t numOne = 0, numTwo = 0;
	for (const long arity : listArity) {
		if (arity == 1) numOne++;
		else if (arity == 2) numTwo++;
	}

	EXPECT_GE(numOne, 2u) << "each side of `on a equals b` is keyed on its own row";
	EXPECT_GE(numTwo, 1u) << "the projection takes BOTH rows — it is what merges them";
}

TEST(CompileTree, ARestrictionJoinEmitsOneTwoParameterPredicate) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var a; var b; var r; r = restrict s in a join t in b on s = t;\n")));

	const std::vector<long> listArity = LambdaArities(compiler.m_cByteCode);

	ASSERT_FALSE(listArity.empty());
	EXPECT_EQ(listArity.front(), 2)
		<< "a restriction hands the decorator ONE predicate over both rows";
}

TEST(CompileTree, AMethodCallThatIsNotAPipelineOpStaysAMethodCall) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var a; a.Add(1);\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_CALL_METHOD), 1u);
	EXPECT_EQ(CountOpcode(bc, OPER_CALL_LINQ), 0u);
}

TEST(CompileTree, APipelineMethodBecomesThePipelineOpcode) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var q; var r; r = q.Where(Function(x) Return x; EndFunction);\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_CALL_LINQ), 1u)
		<< "the pipeline op fell back to a method dispatch\n" << Listing(bc).ToStdString();
}

// ===========================================================================
// A write goes to the target
// ===========================================================================

TEST(CompileTree, AMemberAssignmentWritesTheMember) {
	// Emitting the target as an expression and LET-ing into the result would
	// write the COPY — the field would never change.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var o; o.Field = 5;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_SET_A), 1u)
		<< "the member write became a read\n" << Listing(bc).ToStdString();
	EXPECT_EQ(CountOpcode(bc, OPER_GET_A), 0u);
}

TEST(CompileTree, DISABLED_AnIndexAssignmentWritesTheElement) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("var o; o[1] = 5;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_SET_ARRAY), 1u);
	EXPECT_EQ(CountOpcode(bc, OPER_GET_ARRAY), 0u);
}

// ===========================================================================
// The lambda's operands — what the materialiser reads
//
// OPER_LFUNC's operands are not decoration: p1 is the slot the value lands in,
// p2 the IP to jump past the body, p3 the INDEX of its m_listFunc entry (this
// line's comment said "varCount" for a long time and cost a wrong emission),
// p3.m_numArray the parameter count, p4 function-vs-procedure.
// ===========================================================================

TEST(CompileTree, DISABLED_ALambdaCarriesEveryOperandTheMaterialiserReads) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var f;\n")
		wxT("f = Function(a, b)\n")
		wxT("  Return a + b;\n")
		wxT("EndFunction;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;

	const int at = FindOpcode(bc, OPER_LFUNC);
	ASSERT_GE(at, 0) << Listing(bc).ToStdString();

	const ibByteUnit& lfunc = bc.m_listCode[at];

	const long numFunc = (long)lfunc.m_param3.m_numIndex;
	ASSERT_GE(numFunc, 0);
	ASSERT_LT((size_t)numFunc, bc.m_listFunc.size())
		<< "p3 indexes m_listFunc — a frame size there is a wrong emission";

	EXPECT_EQ(lfunc.m_param3.m_numArray, 2) << "two parameters were written";
	EXPECT_EQ(lfunc.m_param4.m_numIndex, 1) << "Function returns; Procedure does not";

	const long endIp = (long)lfunc.m_param2.m_numIndex;
	ASSERT_GT(endIp, at) << "the jump target must be past the body";
	ASSERT_LT((size_t)endIp, bc.m_listCode.size());
	EXPECT_EQ(bc.m_listCode[endIp].m_numOper, OPER_ENDLFUNC);

	EXPECT_EQ(bc.m_listFunc[numFunc].m_kind, ibFnKind::Lambda);
	EXPECT_EQ(bc.m_listFunc[numFunc].m_listParam.size(), 2u);
	EXPECT_TRUE(bc.m_listFunc[numFunc].m_bCodeRet);
}

TEST(CompileTree, AnAnonymousProcedureIsMarkedAsNotReturning) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var f;\n")
		wxT("f = Procedure(a)\n")
		wxT("  b = a;\n")
		wxT("EndProcedure;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	const int at = FindOpcode(bc, OPER_LFUNC);
	ASSERT_GE(at, 0);
	EXPECT_EQ(bc.m_listCode[at].m_param4.m_numIndex, 0);
}

TEST(CompileTree, ALambdaInsideAFunctionDoesNotEndItsHost) {
	// OPER_ENDLFUNC and OPER_ENDFUNC are distinct opcodes precisely so the
	// module-init skip over a named body cannot stop at a nested lambda.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function MakeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	EXPECT_EQ(CountOpcode(bc, OPER_FUNC),     1u);
	EXPECT_EQ(CountOpcode(bc, OPER_ENDFUNC),  1u);
	EXPECT_EQ(CountOpcode(bc, OPER_LFUNC),    1u);
	EXPECT_EQ(CountOpcode(bc, OPER_ENDLFUNC), 1u);
}

TEST(CompileTree, DISABLED_TheModuleBodyStartsAfterEveryCallable) {
	// m_lStartModule is where execution begins; a function body before it must
	// not be walked into.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function F()\n  Return 1;\nEndFunction\n")
		wxT("var a; a = 1;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;

	const int endFunc = FindOpcode(bc, OPER_ENDFUNC);
	ASSERT_GE(endFunc, 0);
	EXPECT_GT(bc.m_lStartModule, endFunc)
		<< "the module body starts inside a function body\n" << Listing(bc).ToStdString();
}

// ===========================================================================
// Statement emission — the jumps have to land
// ===========================================================================

TEST(CompileTree, NestedLoopsPatchTheirOwnExits) {
	// Break in the inner loop must not land on the outer one's end.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("var a; var b;\n")
		wxT("While a Do\n")
		wxT("  While b Do\n")
		wxT("    Break;\n")
		wxT("  EndDo;\n")
		wxT("  Break;\n")
		wxT("EndDo;\n")));

	const ibByteCode& bc = compiler.m_cByteCode;

	// Every jump target must be inside the tape and never 0 by accident.
	for (size_t i = 0; i < bc.m_listCode.size(); i++) {
		if (bc.m_listCode[i].m_numOper != OPER_GOTO) continue;
		const long target = (long)bc.m_listCode[i].m_param1.m_numIndex;
		EXPECT_GT(target, 0) << "an unpatched jump at " << i << "\n" << Listing(bc).ToStdString();
		EXPECT_LT((size_t)target, bc.m_listCode.size()) << "a jump past the tape at " << i;
	}
}

TEST(CompileTree, ALabelResolvesToItsPosition) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		// A LABEL IS DEFINED BY THE COLON. The `~` is decoration the LEXER throws
		// away outright (translateCode.cpp: "skip the auxiliary symbol of the
		// mark"), so `~again` and `again` are the same identifier — what makes the
		// line a definition rather than an expression statement is the `:` after
		// it. Without the colon the reader takes it as an expression and stops on
		// the missing `=`; with the colon but no definition reached, the Goto
		// reports "Label 'AGAIN' not defined" and blames the jump instead.
		wxT("Procedure P()\n")
		wxT("  ~again:\n")
		wxT("  Goto ~again;\n")
		wxT("EndProcedure\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	const int at = FindOpcode(bc, OPER_GOTO);
	ASSERT_GE(at, 0);
	EXPECT_GE((long)bc.m_listCode[at].m_param1.m_numIndex, 0)
		<< "the label was never resolved\n" << Listing(bc).ToStdString();
}

TEST(CompileTree, ATypedLocalDeclarationEmitsItsGate) {
	// `Number total;` inside a body — the declared type has to reach the slot,
	// or it survives at module level and is lost one line in.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Procedure P()\n")
		wxT("  Number total = 0;\n")
		wxT("EndProcedure\n")));

	EXPECT_GT(CountOpcode(compiler.m_cByteCode, OPER_SET_TYPE), 0u)
		<< "a typed local emitted no type gate";
}

TEST(CompileTree, ATypedParameterEmitsItsGate) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler, wxT("Procedure P(Number n)\nEndProcedure\n")));

	EXPECT_GT(CountOpcode(compiler.m_cByteCode, OPER_SET_TYPE), 0u);
}

TEST(CompileTree, LocalsAreMirroredOntoTheFunctionEntry) {
	// What the debugger's Locals and eval read. Temps are skipped: they have no
	// name to show.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function F(p)\n")
		wxT("  var a; var b;\n")
		wxT("  a = p; b = a;\n")
		wxT("  Return b;\n")
		wxT("EndFunction\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_EQ(bc.m_listFunc.size(), 1u);
	EXPECT_GE(bc.m_listFunc[0].m_listLocals.size(), 3u)
		<< "the parameter and both locals must be nameable";
	EXPECT_GE(bc.m_listFunc[0].m_lVarCount, 3);
}

TEST(CompileTree, AnExportedFunctionIsMarkedCrossBcVisible) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		// The access modifier goes AFTER the signature on a callable — a leading
		// `Public Function …` is not read at all (CompileModule's declaration loop
		// breaks on the keyword and the body reader meets it as a statement).
		wxT("Function Shared() Public\n  Return 1;\nEndFunction\n")
		wxT("Function Private_()\n  Return 1;\nEndFunction\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_EQ(bc.m_listFunc.size(), 2u);
	EXPECT_EQ(bc.m_listFunc[0].m_kind, ibFnKind::Export);
	EXPECT_EQ(bc.m_listFunc[1].m_kind, ibFnKind::Local);
}

TEST(CompileTree, ARecursiveCallIsDeferredNotEmittedInline) {
	// A self-call cannot be expanded where it stands: the frame size is not
	// settled until the body it is in has finished, temps included.
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function Fact(n)\n")
		wxT("  If n <= 1 Then\n")
		wxT("    Return 1;\n")
		wxT("  EndIf;\n")
		wxT("  Return n * Fact(n - 1);\n")
		wxT("EndFunction\n")));

	const ibByteCode& bc = compiler.m_cByteCode;
	ASSERT_EQ(bc.m_listFunc.size(), 1u);
	EXPECT_GT(bc.m_listFunc[0].m_lVarCount, 0);
	EXPECT_GT(CountOpcode(bc, OPER_CALL) + CountOpcode(bc, OPER_CALL_CLOSURE), 0u)
		<< "the recursive call never resolved\n" << Listing(bc).ToStdString();
}

TEST(CompileTree, ACallToNothingIsRejected) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler, wxT("var a; a = NoSuchFunction(1);\n")));
}

TEST(CompileTree, TooManyArgumentsAreRejected) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler,
		wxT("Function F(a)\n  Return a;\nEndFunction\n")
		wxT("var r; r = F(1, 2);\n")));
}

TEST(CompileTree, AnOmittedArgumentUsesItsDefault) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	ASSERT_TRUE(Build(compiler,
		wxT("Function F(a, b = 5)\n  Return a + b;\nEndFunction\n")
		wxT("var r; r = F(1);\n")))
		<< "a default is what makes the short call legal";

	EXPECT_GT(CountOpcode(compiler.m_cByteCode, OPER_SETCONST), 0u)
		<< "the default was not substituted";
}

TEST(CompileTree, AProcedureUsedAsAFunctionIsRejected) {
	ibCompileCode compiler(wxT("test"), wxT("memory"));
	EXPECT_FALSE(Build(compiler,
		wxT("Procedure P()\nEndProcedure\n")
		wxT("var r; r = P();\n")));
}

// A LAMBDA AS AN ARGUMENT, VES-style and with no terminators — the shape the
// script corpus dies on (`Message(nums.Sum(Function(x) Return x * 2 EndFunction))`).
// Reduced to one line so the next failure is two seconds away instead of a
// nineteen-file sweep.
TEST(CompileTree, ALambdaArgumentClosesWithEndFunction) {
	const short styleSaved = ibCompileCode::GetCodeStyle();
	ibCompileCode::SetCodeStyle(CODE_VES);

	ibCompileCode compiler(wxT("test"), wxT("memory"));

	// `Message` is a GLOBAL, and a host provides it — codeRunner binds the
	// system functions as a transparent scope and so does this. Rewriting the
	// source to avoid the name would have hidden the shape the corpus dies on.
	ibValueSystemFunction valueSystem;
	compiler.AddContextVariable(wxT("System"), &valueSystem, true);
	const bool bOk = Build(compiler,
		wxT("var nums\n")
		wxT("nums = New Array\n")
		wxT("Message(nums.Sum(Function(x) Return x * 2 EndFunction))\n"));

	ibCompileCode::SetCodeStyle(styleSaved);

	EXPECT_TRUE(bOk) << Listing(compiler.m_cByteCode).ToStdString();
}

// The same shape WITHOUT the nesting, to tell "lambda as an argument" apart from
// "lambda inside a call inside a call".
TEST(CompileTree, ALambdaIsAnOrdinaryArgument) {
	const short styleSaved = ibCompileCode::GetCodeStyle();
	ibCompileCode::SetCodeStyle(CODE_VES);

	ibCompileCode compiler(wxT("test"), wxT("memory"));
	const bool bOk = Build(compiler,
		wxT("var nums\n")
		wxT("nums = New Array\n")
		wxT("var s\n")
		wxT("s = nums.Sum(Function(x) Return x * 2 EndFunction)\n"));

	ibCompileCode::SetCodeStyle(styleSaved);

	EXPECT_TRUE(bOk) << Listing(compiler.m_cByteCode).ToStdString();
}
