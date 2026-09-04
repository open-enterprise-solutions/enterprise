// =============================================================================
// OES Enterprise — the bytecode emission CONTRACT
//
// Step 0 of the compiler AST arc (docs/compiler-ast-arc.md §3e). This suite
// does not test that the compiler is CORRECT; test_compiler.cpp does that. It
// tests that emission does not CHANGE.
//
// The arc splits `Compile*` into `Parse*` (build a node) and `Emit*` (walk it),
// one construct at a time. The whole plan rests on a single falsifiable claim:
// both paths must produce the SAME ibByteCode. This file is what makes that
// claim checkable — a corpus of sources across the language surface, each
// rendered to a canonical text form and digested. A refactor that alters one
// opcode, one operand, one table entry or one line stamp fails here, naming the
// construct that moved.
//
// The rendering is deliberately WIDER than the interpreter reads: it includes
// m_numLine, because the debugger's breakpoints ride on it and a tree-driven
// emitter is exactly the kind of change that silently drops line stamps
// (docs/compiler-ast-arc.md §5).
//
// UPDATING A DIGEST. When a change is intended, the failure prints the full
// disassembly and the ready-to-paste literal. Paste it — but only after reading
// the diff and being able to say why emission moved. A digest updated without
// that reading turns this suite into a rubber stamp.
//
// RE-BASELINED 2026-08-10, and here is the reading that justifies it. The arc
// was reverted; these digests had been recorded against ITS emitter. All
// sixteen differed at once, which is the signature of a change in the module
// HEADER rather than in any construct: the rendering carries `start=`
// (m_lStartModule), and the two emitters disagree about it by design — the tree
// emitter pointed the start past the callables, while this one leaves it at 0
// and skips each OPER_FUNC…OPER_ENDFUNC block during the module-init walk
// (procUnit.cpp, the OPER_FUNC case). One global difference, sixteen failures.
//
// From here the suite guards THIS emitter: the next digest that moves is a real
// change in codegen, and the first candidate to suspect is the assignment fold
// in CompileBlock, which was given a destination check the same day.
//
// AND ONCE IT CAUGHT A DESIGN, not a bug (2026-09-04). The `Cached` modifier
// first landed as a call opcode, which renumbered every opcode after it and
// moved all sixteen digests at once. They were re-baselined — and then the
// design changed: the modifier belongs at the callee's ENTRY, where every road
// into a body arrives, not at a caller the emitter happens to see. The opcode
// went away, the numbering came back, and these values are the originals again.
// The suite had faithfully reported a global change; the right answer was to
// stop making it.
// =============================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileContext.h"   // CODE_VES / CODE_CES
#include "backend/compiler/codeDef.h"
#include "backend/compiler/byteCode.h"
#include "backend/compiler/value.h"

// The code style is process-global and defaults to CODE_CES; every source here
// is VES, same as the rest of this binary. test_compiler.cpp installs a global
// environment that forces CODE_VES for the whole process — this suite relies on
// it, and asserts the style below so a change there fails loudly rather than by
// mysteriously not parsing.

namespace {

// --- canonical rendering ----------------------------------------------------
//
// Deterministic text, no pointers and no addresses. Anything that could vary
// between two runs of the same binary would make the digest useless, so the
// only inputs are the compiler's own integers, enums and names.

void RenderParam(wxString& out, const wxChar* tag, const ibParamRunUnit& p)
{
	out << tag << wxT("(") << p.m_numArray << wxT(",") << p.m_numIndex << wxT(")");
}

wxString RenderConst(const ibValue& v)
{
	// Type tag first, so a constant that changes TYPE is caught even when its
	// printed form happens to match (the empty string and an empty value both
	// render as nothing).
	wxString out;
	out << wxT("t") << (int)v.GetType();
	switch (v.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN:
		out << wxT(":") << (v.GetBoolean() ? wxT("true") : wxT("false"));
		break;
	case ibValueTypes::TYPE_NUMBER:
	case ibValueTypes::TYPE_STRING:
	case ibValueTypes::TYPE_DATE:
		out << wxT(":") << v.GetString();
		break;
	default:
		break;   // objects / references have no stable printed form; the tag is enough
	}
	return out;
}

wxString RenderByteCode(const ibByteCode& bc)
{
	wxString out;

	out << wxT("module=")   << bc.m_strModuleName
	    << wxT(" vars=")    << bc.m_lVarCount
	    << wxT(" start=")   << bc.m_lStartModule
	    << wxT(" compiled=")<< (bc.m_bCompile ? 1 : 0) << wxT("\n");

	out << wxT("code ") << (long)bc.m_listCode.size() << wxT("\n");
	for (size_t i = 0; i < bc.m_listCode.size(); i++) {
		const ibByteUnit& u = bc.m_listCode[i];
		out << wxT("  ") << (long)i
		    << wxT(" op=")   << u.m_numOper
		    << wxT(" line=") << u.m_numLine;      // breakpoints ride on this
		RenderParam(out, wxT(" p1"), u.m_param1);
		RenderParam(out, wxT(" p2"), u.m_param2);
		RenderParam(out, wxT(" p3"), u.m_param3);
		RenderParam(out, wxT(" p4"), u.m_param4);
		if (!u.m_strModuleName.IsEmpty()) out << wxT(" mod=") << u.m_strModuleName;
		out << wxT("\n");
	}

	out << wxT("const ") << (long)bc.m_listConst.size() << wxT("\n");
	for (size_t i = 0; i < bc.m_listConst.size(); i++)
		out << wxT("  ") << (long)i << wxT(" ") << RenderConst(bc.m_listConst[i]) << wxT("\n");

	out << wxT("vars ") << (long)bc.m_listVar.size() << wxT("\n");
	for (size_t i = 0; i < bc.m_listVar.size(); i++) {
		const auto& v = bc.m_listVar[i];
		out << wxT("  ") << (long)i
		    << wxT(" kind=")   << (int)v.m_kind
		    << wxT(" name=")   << v.m_strRealName
		    << wxT(" slot=")   << v.m_slotIndex   // frame slot — step 1 moves the allocation
		    << wxT(" clsid=")  << (wxLongLong_t)v.m_clsid
		    << wxT(" parent=") << v.m_parentRef
		    << wxT(" depth=")  << v.m_scopeDepth
		    << wxT("\n");
	}

	out << wxT("func ") << (long)bc.m_listFunc.size() << wxT("\n");
	for (size_t i = 0; i < bc.m_listFunc.size(); i++) {
		const auto& f = bc.m_listFunc[i];
		out << wxT("  ") << (long)i
		    << wxT(" kind=")   << (int)f.m_kind
		    << wxT(" entry=")  << f.m_lCodeLine
		    << wxT(" ret=")    << (f.m_bCodeRet ? 1 : 0)
		    << wxT(" vars=")   << f.m_lVarCount
		    << wxT(" heap=")   << (f.m_needsHeapFrame ? 1 : 0)
		    << wxT(" params=") << (long)f.m_listParam.size()
		    << wxT(" locals=") << (long)f.m_listLocals.size()
		    << wxT("\n");
	}

	return out;
}

// FNV-1a over the UTF-8 bytes of the rendering. Not cryptography — a compact
// name for a text, so the expected value fits on one line.
uint64_t Digest(const wxString& text)
{
	const wxScopedCharBuffer utf8 = text.utf8_str();
	uint64_t h = 1469598103934665603ULL;
	for (size_t i = 0; i < utf8.length(); i++) {
		h ^= (uint64_t)(unsigned char)utf8.data()[i];
		h *= 1099511628211ULL;
	}
	return h;
}

// --- the gate ---------------------------------------------------------------

// A digest of 0 means "not recorded yet": the check fails and prints the
// literal to paste. Same path serves a new corpus entry and an intended change,
// so there is one workflow instead of two.
constexpr uint64_t kUnrecorded = 0;

void ExpectEmission(const wxChar* name, const wxString& src, uint64_t expected)
{
	ASSERT_EQ(ibCompileCode::GetCodeStyle(), CODE_VES)
		<< "this suite's sources are VES; the global style environment did not run";

	ibCompileCode cc(wxT("contract"), wxT("memory"), false);

	// The compiler knows WHY it refused; throwing that away costs a manual re-run
	// per red line. Keep the message and put it in the report.
	bool     compiled = false;
	wxString strWhy;
	try {
		compiled = cc.Compile(src);
		if (!compiled)
			strWhy = wxT("Compile() returned false without raising");
	} catch (const ibBackendException& err) {
		strWhy = err.GetErrorDescription();
	} catch (...) {
		strWhy = wxT("unknown exception");
	}
	ASSERT_TRUE(compiled) << name << ": source failed to compile — fix the corpus entry, "
	                                 "it is meant to be valid. " << strWhy.ToStdString();

	const wxString rendered = RenderByteCode(cc.m_cByteCode);
	const uint64_t actual   = Digest(rendered);

	if (actual == expected)
		return;

	// One failure message carrying everything needed to decide: what moved, and
	// the line to paste once the move is understood and wanted.
	ADD_FAILURE()
		<< (expected == kUnrecorded
		        ? "emission not recorded yet for "
		        : "EMISSION CHANGED for ")
		<< name << "\n"
		<< "  expected digest: " << expected << "\n"
		<< "  actual   digest: " << actual   << "ULL\n"
		<< "  paste:           " << actual   << "ULL\n"
		<< "--- actual emission ---\n"
		<< rendered.ToStdString();
}

// --- the corpus -------------------------------------------------------------
//
// ✅ RE-READ AND RE-PASTED 2026-08-09, after the tree. Two signatures account for
// every row, and both were checked in the disassembly before any literal moved:
//
//   * the FUSION — `i = i + 1` is `ADD p1(0,1) p2(0,1) p3(const)`, one opcode
//     writing the variable itself, no temp and no LET after it. `s = s + "b"`
//     lands with p1 == p2, which is what lets the runtime append in place.
//   * the REAL ARGUMENT COUNT — a call carries what the caller WROTE in m_param4
//     beside what the callee declares in m_param3: `arr.Add(1)` reads
//     `p3(1,2) p4(0,1)`, `arr.Count()` reads `p3(0,2) p4(0,0)`.
//
// A row that fails from here on is a genuine move again. Do not paste one
// without reading its disassembly — sixteen unread red digests taught the suite
// to be ignored for a whole arc, which is the failure mode this file has.
//
// (Historical note: they predated the tree and were expected to fail on the
// first run against it. That is what this suite is FOR: emission moved from
// a reader-and-emitter fused in one pass to a walk of the tree, and the shapes
// that moved with it are exactly the ones worth looking at one by one —
//
//   * temporaries are allocated in a different order, so slot indices shift;
//   * a write goes to its target (OPER_SET_A / SET_ARRAY where a GET + LET pair
//     used to stand);
//   * a query block is pipeline calls, not an inline foreach with an array;
//   * the `shortLet` fusion survives the move but stops being a peephole: the
//     destination travels DOWN into the emitter, so `x = a + b` is one ADD into
//     `x` because it was emitted that way, not because a pass looked back and
//     rewrote it.
//
// The rule for updating a line is unchanged and is the whole point: read the
// printed emission, decide the move is INTENDED, then paste the new digest. A
// digest updated without reading what changed buys nothing.
//
// Chosen from what the arc will actually touch, not from what reads nicely.
// Expressions first (step 1 rewrites exactly these), then the constructs whose
// emission has hidden structure: the compound-assignment peephole, loops with
// several exits, forward references, lambdas and LINQ.)

struct Case {
	const wxChar* name;
	const wxChar* src;
	uint64_t      digest;
};

} // namespace

// ===========================================================================
// Expressions — step 1 rewrites every one of these
// ===========================================================================

TEST(CompilerContract, ArithmeticPrecedence) {
	ExpectEmission(wxT("ArithmeticPrecedence"),
		wxT("var a; var b; var c;\n")
		wxT("a = 1 + 2 * 3 - 4 / 2;\n")
		wxT("b = (1 + 2) * (3 - 4);\n")
		wxT("c = 10 % 3 + a * b;\n"),
		8637331872626389594ULL);
}

TEST(CompilerContract, UnaryAndNot) {
	ExpectEmission(wxT("UnaryAndNot"),
		wxT("var a; var b;\n")
		wxT("a = -5;\n")
		wxT("b = 2 * -a;\n")
		wxT("b = Not (a > b);\n"),
		12881780156291619749ULL);
}

TEST(CompilerContract, ComparisonAndLogical) {
	ExpectEmission(wxT("ComparisonAndLogical"),
		wxT("var a; var b; var r;\n")
		wxT("a = 1; b = 2;\n")
		wxT("r = a < b And b > 0 Or a = b;\n")
		wxT("r = a <> b;\n")
		wxT("r = a <= b And a >= b;\n"),
		16427525471423789782ULL);
}

// `x = a op b` emits ONE opcode writing `x`, not an opcode into a temp and a LET
// copying it out (compiler-pipeline.md §3.1). It has died silently twice — once
// to a macro-precedence bug, once to the reader rewrite — because a peephole
// that matches nothing looks exactly like a peephole that has nothing to match.
// This pins the fused shape, which is the only thing that catches either death.
TEST(CompilerContract, CompoundAssignmentFusion) {
	ExpectEmission(wxT("CompoundAssignmentFusion"),
		wxT("var s; var i;\n")
		wxT("i = 0;\n")
		wxT("i = i + 1;\n")
		wxT("i = i * 2;\n")
		wxT("s = \"a\";\n")
		wxT("s = s + \"b\";\n"),
		14445145491289715289ULL);
}

TEST(CompilerContract, NestedExpressionTemporaries) {
	ExpectEmission(wxT("NestedExpressionTemporaries"),
		wxT("var a; var b; var c; var d; var r;\n")
		wxT("a = 1; b = 2; c = 3; d = 4;\n")
		wxT("r = (a + b) * (c - d) + (a * c) / (b + 1);\n"),
		10059689396912253845ULL);
}

// ===========================================================================
// Control flow — the loop-exit tables live here (compileContext.h
// FinishLoopList), and TWO exits of a kind is the case that was broken.
// ===========================================================================

TEST(CompilerContract, IfElseIfElse) {
	ExpectEmission(wxT("IfElseIfElse"),
		wxT("var a; var r;\n")
		wxT("a = 1;\n")
		wxT("If a > 2 Then\n")
		wxT("  r = 1;\n")
		wxT("ElseIf a > 1 Then\n")
		wxT("  r = 2;\n")
		wxT("Else\n")
		wxT("  r = 3;\n")
		wxT("EndIf;\n"),
		4705527781584554419ULL);
}

TEST(CompilerContract, WhileWithTwoBreaksAndTwoContinues) {
	ExpectEmission(wxT("WhileWithTwoBreaksAndTwoContinues"),
		wxT("var i;\n")
		wxT("i = 0;\n")
		wxT("While i < 100 Do\n")
		wxT("  i = i + 1;\n")
		wxT("  If i = 10 Then Continue; EndIf;\n")
		wxT("  If i = 20 Then Continue; EndIf;\n")
		wxT("  If i = 30 Then Break; EndIf;\n")
		wxT("  If i = 40 Then Break; EndIf;\n")
		wxT("EndDo;\n"),
		11728157248793000939ULL);
}

TEST(CompilerContract, ForLoop) {
	ExpectEmission(wxT("ForLoop"),
		wxT("var i; var s;\n")
		wxT("s = 0;\n")
		wxT("For i = 1 To 10 Do\n")
		wxT("  s = s + i;\n")
		wxT("EndDo;\n"),
		7274276515444987384ULL);
}

TEST(CompilerContract, ForeachLoop) {
	ExpectEmission(wxT("ForeachLoop"),
		wxT("var arr; var it; var s;\n")
		wxT("arr = New Array;\n")
		wxT("s = 0;\n")
		wxT("Foreach it In arr Do\n")
		wxT("  s = s + 1;\n")
		wxT("EndDo;\n"),
		17806177553391291511ULL);
}

TEST(CompilerContract, TryExcept) {
	ExpectEmission(wxT("TryExcept"),
		wxT("var r;\n")
		wxT("Try\n")
		wxT("  r = 1 / 0;\n")
		wxT("Except\n")
		wxT("  r = 0;\n")
		wxT("EndTry;\n"),
		13876428176927115487ULL);
}

// ===========================================================================
// Declarations and calls — the deferred-resolution backlog (ibCallFunction)
// is what the tree removes, so a forward reference is pinned here first.
// ===========================================================================

TEST(CompilerContract, FunctionsAndForwardReference) {
	ExpectEmission(wxT("FunctionsAndForwardReference"),
		wxT("Function Caller(x)\n")
		wxT("  Return Callee(x) + 1;\n")     // forward — resolved in the second pass
		wxT("EndFunction\n")
		wxT("Function Callee(y)\n")
		wxT("  Return y * 2;\n")
		wxT("EndFunction\n")
		wxT("Procedure Entry()\n")
		wxT("  var r; r = Caller(3);\n")
		wxT("EndProcedure\n"),
		8150511625823305062ULL);
}

TEST(CompilerContract, TypedParametersAndLocals) {
	ExpectEmission(wxT("TypedParametersAndLocals"),
		wxT("Procedure Handle(Boolean cancel, Number depth)\n")
		wxT("  String label;\n")               // a declared type takes no `var`
		wxT("  label = \"x\";\n")
		wxT("  If cancel Then depth = depth + 1; EndIf;\n")
		wxT("EndProcedure\n"),
		5320152434076089702ULL);
}

TEST(CompilerContract, MethodCallChain) {
	ExpectEmission(wxT("MethodCallChain"),
		wxT("Function Build() Public\n")
		wxT("  var arr; arr = New Array;\n")
		wxT("  arr.Add(1); arr.Add(2);\n")
		wxT("  Return arr.Count();\n")
		wxT("EndFunction\n"),
		6304013196489271024ULL);
}

// ===========================================================================
// Lambdas and LINQ — the most entangled emission, and the arc's real target
// ===========================================================================

TEST(CompilerContract, LambdaWithCapture) {
	ExpectEmission(wxT("LambdaWithCapture"),
		wxT("Function MakeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n"),
		16191912890559523292ULL);
}

TEST(CompilerContract, LambdaWithoutCapture) {
	ExpectEmission(wxT("LambdaWithoutCapture"),
		wxT("Function MakeDoubler()\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x * 2;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n"),
		10339020335899397534ULL);
}

TEST(CompilerContract, MethodStyleLinqPipeline) {
	ExpectEmission(wxT("MethodStyleLinqPipeline"),
		wxT("Function Pipe(arr) Public\n")
		wxT("  Return arr.Where(Function(x) Return x > 100 EndFunction)")
		wxT(".Select(Function(x) Return x * 2 EndFunction).Count();\n")
		wxT("EndFunction\n"),
		7548915054214959683ULL);
}
