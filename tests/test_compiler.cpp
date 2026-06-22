// =============================================================================
// OES Enterprise — compiler pipeline tests
//
// Covers the lex → compile → bytecode emission stages without running the
// interpreter. Tests do NOT require appData / activeMetaData / database —
// they construct ibCompileCode directly and inspect the produced bytecode
// (m_cByteCode field).
//
// Runtime execution is exercised in test_runtime.cpp.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileContext.h"   // CODE_VES / CODE_CES
#include "backend/compiler/codeDef.h"
#include "backend/compiler/byteCode.h"
#include "backend/compiler/value.h"

// Every OES script source in this binary's compiling suites (CompilerTest /
// LexerTest / ClosureCapture / CompilerAOT / RuntimeTest) is written in VES
// (Function...EndFunction, If...Then...EndIf). The compiler's code style is a
// PROCESS-GLOBAL (compileCode.cpp gs_codeStyle) that defaults to CODE_CES;
// under CES the VES block-fence keywords (Then/Do/EndIf/EndFunction/...) are
// filtered out (ibTranslateCode::IsAllowedKey) and these sources fail to parse.
// Force VES once for the whole test binary so the suites are deterministic
// regardless of test order or which tests --gtest_filter selects. (The global
// env's SetUp runs even under a filter, so single-test runs get it too.)
namespace {
class VesCodeStyleEnvironment : public ::testing::Environment {
public:
	void SetUp() override { ibCompileCode::SetCodeStyle(CODE_VES); }
};
const ::testing::Environment* const g_vesCodeStyleEnv =
	::testing::AddGlobalTestEnvironment(new VesCodeStyleEnvironment);
} // namespace

namespace {

bool TryCompile(ibCompileCode& cc, const wxString& src) {
	try {
		return cc.Compile(src);
	} catch (...) {
		return false;
	}
}

// Find an opcode in the bytecode body. Returns the count of matches.
size_t CountOpcode(const ibByteCode& bc, short oper) {
	size_t n = 0;
	for (const auto& u : bc.m_listCode)
		if (u.m_numOper == oper) ++n;
	return n;
}

// Same, but type-tier agnostic. Opcodes are typed-specialized: a numeric /
// string / date / boolean operand shifts the base opcode by k*TYPE_DELTA1
// (codeDef.h, AddTypeSet). E.g. an If on a boolean condition emits
// OPER_IF + TYPE_DELTA4, not bare OPER_IF. Strip the delta via % TYPE_DELTA1
// (base opcodes are all < TYPE_DELTA1) so the base is matched in any tier.
size_t CountOpcodeAnyType(const ibByteCode& bc, short baseOper) {
	// NB: TYPE_DELTA1 expands to `1 * (OPER_END + 1)` WITHOUT outer parens, so
	// `x % TYPE_DELTA1` would parse as `(x % 1) * (OPER_END+1)` == 0. Bind the
	// value first so the modulo divides by the real tier stride (OPER_END+1).
	const short delta1 = TYPE_DELTA1;
	size_t n = 0;
	for (const auto& u : bc.m_listCode)
		if ((u.m_numOper % delta1) == baseOper) ++n;
	return n;
}

} // namespace

// ===========================================================================
// Lexer (ibTranslateCode::PrepareLexem)
// ===========================================================================

TEST(LexerTest, EmptySourceProducesEndProgramOnly) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT(""));
	ASSERT_TRUE(cc.PrepareLexem());
	// At least the ENDPROGRAM sentinel — exact number depends on lexer
	// implementation, but it must be non-empty.
	EXPECT_GE(cc.GetLexemCount(), 1u);
}

TEST(LexerTest, SimpleAssignmentTokenizes) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT("a = 1;"));
	ASSERT_TRUE(cc.PrepareLexem());
	// Identifier 'a', '=', number '1', ';', ENDPROGRAM — at least 5.
	EXPECT_GE(cc.GetLexemCount(), 4u);
}

TEST(LexerTest, StringLiteralLexes) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT("s = \"hello\";"));
	ASSERT_TRUE(cc.PrepareLexem());
	EXPECT_GE(cc.GetLexemCount(), 4u);
}

// ===========================================================================
// Compile — bytecode emission shape (does not execute)
// ===========================================================================

TEST(CompilerTest, EmptyModuleCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc, wxT("")));
	// Emitter typically appends an OPER_RET at module exit.
	// Empty module => zero instructions OR a single trailing return.
	EXPECT_LE(cc.m_cByteCode.m_listCode.size(), 2u);
}

TEST(CompilerTest, SimpleAssignmentEmitsBytecode) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("a = 1 + 2;")));
	EXPECT_GT(cc.m_cByteCode.m_listCode.size(), 0u);
	// Constant 1 and 2 should land in the const pool. Either both as
	// distinct const entries, or shared via dedup — either way at
	// least one numeric constant is emitted.
	EXPECT_GE(cc.m_cByteCode.m_listConst.size(), 1u);
}

TEST(CompilerTest, AssignmentRegistersVariable) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("a = 42;")));
	// Variable 'a' must appear in m_listVar (either as Local or Export).
	bool foundA = false;
	for (const auto& v : cc.m_cByteCode.m_listVar) {
		if (v.m_strRealName.IsSameAs(wxT("a"), false)) {
			foundA = true;
			EXPECT_TRUE(v.IsLocal() || v.IsExport());
			break;
		}
	}
	EXPECT_TRUE(foundA) << "variable 'a' not found in m_listVar";
}

TEST(CompilerTest, ExportVariableMarkedAsExport) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("var publicVar public;")));
	bool found = false;
	for (const auto& v : cc.m_cByteCode.m_listVar) {
		if (v.m_strRealName.IsSameAs(wxT("publicVar"), false)) {
			found = true;
			EXPECT_TRUE(v.IsExport()) << "expected ibVarKind::Export";
			break;
		}
	}
	EXPECT_TRUE(found) << "publicVar not found";
}

TEST(CompilerTest, FunctionDeclarationRegistersInListFunc) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Add(x, y)\n")
		wxT("  Return x + y;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	bool foundAdd = false;
	for (const auto& fn : cc.m_cByteCode.m_listFunc) {
		if (fn.m_strRealName.IsSameAs(wxT("Add"), false)) {
			foundAdd = true;
			EXPECT_TRUE(fn.m_bCodeRet) << "Function (not Procedure) expected";
			EXPECT_EQ(fn.m_listParam.size(), 2u);
			EXPECT_EQ(fn.m_listParam[0].m_strName, wxT("x"));
			EXPECT_TRUE(fn.IsLocal() || fn.IsExport());
			break;
		}
	}
	EXPECT_TRUE(foundAdd) << "function 'Add' missing from m_listFunc";
}

TEST(CompilerTest, ExportFunctionFlagged) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function PublicFn() Public\n")
		wxT("  Return 1;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	bool found = false;
	for (const auto& fn : cc.m_cByteCode.m_listFunc) {
		if (fn.m_strRealName.IsSameAs(wxT("PublicFn"), false)) {
			found = true;
			EXPECT_TRUE(fn.IsExport()) << "expected ibFnKind::Export";
			break;
		}
	}
	EXPECT_TRUE(found) << "PublicFn missing";
}

TEST(CompilerTest, ProcedureRecognized) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Procedure DoIt()\n")
		wxT("  a = 1;\n")
		wxT("EndProcedure\n");
	ASSERT_TRUE(TryCompile(cc, src));

	bool found = false;
	for (const auto& fn : cc.m_cByteCode.m_listFunc) {
		if (fn.m_strRealName.IsSameAs(wxT("DoIt"), false)) {
			found = true;
			EXPECT_FALSE(fn.m_bCodeRet) << "Procedure (no return)";
			break;
		}
	}
	EXPECT_TRUE(found) << "procedure 'DoIt' missing";
}

TEST(CompilerTest, IfStatementEmitsBranch) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("a = 0;\n")
		wxT("If a > 0 Then\n")
		wxT("  a = 1;\n")
		wxT("EndIf;\n");
	ASSERT_TRUE(TryCompile(cc, src));
	// Conditional bytecode → at least one OPER_IF (typed-specialized for the
	// boolean condition, so match any type tier).
	EXPECT_GE(CountOpcodeAnyType(cc.m_cByteCode, OPER_IF), 1u);
}

TEST(CompilerTest, WhileLoopEmitsGoto) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("a = 0;\n")
		wxT("While a < 10 Do\n")
		wxT("  a = a + 1;\n")
		wxT("EndDo;\n");
	ASSERT_TRUE(TryCompile(cc, src));
	EXPECT_GE(CountOpcodeAnyType(cc.m_cByteCode, OPER_IF),   1u);
	EXPECT_GE(CountOpcodeAnyType(cc.m_cByteCode, OPER_GOTO), 1u);
}

TEST(CompilerTest, SyntaxErrorReturnsFalse) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// Unterminated assignment — expect compile to fail (return false
	// or throw). TryCompile collapses both into false.
	EXPECT_FALSE(TryCompile(cc, wxT("a = ")));
}

// ===========================================================================
// Compile + AOT round-trip — checks the AOT layer with real compiler output
// ===========================================================================

#include "backend/fileSystem/fs.h"

TEST(CompilerAOT, RealCompileOutputRoundTrips) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// Var declaration + function declaration. NB: the parameter is named
	// `delta`, not `by` — `By` is a reserved LINQ keyword (group/order ... by),
	// so using it as an identifier fails to lex ("Identifier expected").
	const wxString src =
		wxT("var counter public;\n")
		wxT("Function Increment(delta)\n")
		wxT("  counter = counter + delta;\n")
		wxT("  Return counter;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const ibByteCode& src_bc = cc.m_cByteCode;

	ibWriterMemory w;
	ASSERT_TRUE(src_bc.SerializeAOT(w));
	wxMemoryBuffer blob = w.buffer();
	EXPECT_GT(blob.GetDataLen(), 0u);

	ibReaderMemory r(blob);
	ibByteCode dst;
	ASSERT_TRUE(dst.DeserializeAOT(r));

	EXPECT_EQ(dst.m_listCode.size(), src_bc.m_listCode.size());
	EXPECT_EQ(dst.m_listConst.size(), src_bc.m_listConst.size());
	EXPECT_EQ(dst.m_listVar.size(),  src_bc.m_listVar.size());
	EXPECT_EQ(dst.m_listFunc.size(), src_bc.m_listFunc.size());

	bool foundCounter = false, foundIncrement = false;
	for (const auto& v : dst.m_listVar)
		if (v.m_strRealName.IsSameAs(wxT("counter"), false)) {
			foundCounter = true;
			EXPECT_TRUE(v.IsExport());
			break;
		}
	for (const auto& fn : dst.m_listFunc)
		if (fn.m_strRealName.IsSameAs(wxT("Increment"), false)) {
			foundIncrement = true;
			EXPECT_EQ(fn.m_listParam.size(), 1u);
			break;
		}
	EXPECT_TRUE(foundCounter)   << "exported 'counter' lost in AOT round-trip";
	EXPECT_TRUE(foundIncrement) << "function 'Increment' lost in AOT round-trip";
}

// ===========================================================================
// Closure capture — Phase A (compile-side emit only; runtime not wired)
// ===========================================================================
//
// Verifies:
//   * Lambda body references outer-fn local — compile succeeds (was
//     ERROR_VAR_NOT_FOUND pre-Phase-A).
//   * Outer fn's ibByteFunction.m_needsHeapFrame = true after lambda
//     captures one of its locals.
//   * Bytecode dump shows OPER_GET inside lambda body reading at
//     m_numArray ≥ 1 (m_pppArrayList depth past lambda's own frame).
//   * Functions with NO inner lambda stay at m_needsHeapFrame = false.

namespace {

// Find an ibByteFunction by real name (case-insensitive).
const ibByteCode::ibByteFunction* FindFnByName(const ibByteCode& bc, const wxString& name) {
	for (const auto& fn : bc.m_listFunc)
		if (fn.m_strRealName.IsSameAs(name, false))
			return &fn;
	return nullptr;
}

// Find a lambda (kind == Lambda) — returns first match.
const ibByteCode::ibByteFunction* FindFirstLambda(const ibByteCode& bc) {
	for (const auto& fn : bc.m_listFunc)
		if (fn.IsLambda())
			return &fn;
	return nullptr;
}

} // namespace

TEST(ClosureCapture, LambdaReferencingOuterLocalCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function makeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	EXPECT_TRUE(TryCompile(cc, src)) << "lambda capturing outer 'n' should compile";
}

TEST(ClosureCapture, OuterFnMarkedNeedsHeapFrameAfterCapture) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function makeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const auto* outer = FindFnByName(cc.m_cByteCode, wxT("makeAdder"));
	ASSERT_NE(outer, nullptr) << "makeAdder missing from m_listFunc";
	EXPECT_TRUE(outer->m_needsHeapFrame)
		<< "makeAdder must be marked: its 'n' is captured by inner lambda";
}

TEST(ClosureCapture, NoCaptureLeavesNeedsHeapFrameFalse) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// No inner lambda → no capture → no heap-frame requirement.
	const wxString src =
		wxT("Function plainAdd(a, b)\n")
		wxT("  Return a + b;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const auto* fn = FindFnByName(cc.m_cByteCode, wxT("plainAdd"));
	ASSERT_NE(fn, nullptr);
	EXPECT_FALSE(fn->m_needsHeapFrame)
		<< "fn without inner lambda must NOT be heap-promoted";
}

TEST(ClosureCapture, LambdaBodyEmitsDepthGreaterOrEqualOneForCapturedVar) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function makeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const auto* lambda = FindFirstLambda(cc.m_cByteCode);
	ASSERT_NE(lambda, nullptr) << "lambda missing from m_listFunc";

	// Walk lambda body opcodes [m_lCodeLine .. m_lCodeLine + (range)].
	// EmitFunctionBody stamps body IPs into m_listCode; the lambda's
	// reference to outer 'n' must produce an opcode whose source-slot
	// parameter has m_numArray ≥ 1 (= depth past own frame).
	const long startIp  = lambda->m_lCodeLine;
	const long lastCode = (long)cc.m_cByteCode.m_listCode.size();
	bool sawDepthGEOne = false;
	for (long ip = startIp; ip < lastCode; ++ip) {
		const ibByteUnit& u = cc.m_cByteCode.m_listCode[ip];
		if (u.m_numOper == OPER_ENDLFUNC) break;
		// Any of the four params reading from an outer frame (depth ≥ 1)
		// counts; OPER_ADD's source operands sit on m_param2/m_param3.
		if (u.m_param1.m_numArray >= 1 ||
		    u.m_param2.m_numArray >= 1 ||
		    u.m_param3.m_numArray >= 1 ||
		    u.m_param4.m_numArray >= 1) {
			sawDepthGEOne = true;
			break;
		}
	}
	EXPECT_TRUE(sawDepthGEOne)
		<< "lambda body must reference captured outer at depth ≥ 1";
}
