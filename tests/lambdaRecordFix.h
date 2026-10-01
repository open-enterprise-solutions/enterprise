// =============================================================================
// OES Enterprise — record a lambda BODY into the L4 query AST, from a test
//
// Three suites asked the same question of the recorder and each kept its own
// copy of the way to ask it (test_lambdaRecorder, test_queryLINQExec,
// test_queryComputedServer). When the recorder changed, all three broke the
// same way — so the way to ask lives here now, once.
//
// ⭐⭐ WHAT CHANGED, AND WHY THE HELPER IS NOT A ONE-LINE RENAME. The recorder
// used to read LEXEMES: text was tokenized and the token span handed over, with
// no compiler run at all. It now reads INSTRUCTIONS — the body has to be
// COMPILED, and the recorder is pointed at the lambda's own entry in
// m_listFunc. The lexeme reader is gone rather than kept as a second opinion,
// because its grammar was WIDER than the language: it accepted `x.Code in (…)`,
// which the compiler answers with "Symbol expected ')'", and a reader that can
// record predicates the program does not contain is worse than no second
// opinion (compileCode.cpp, at the recorder callsite).
//
// ⚠ SO "DOES NOT COMPILE" IS NOW ONE OF THE ANSWERS. A body outside the
// language never reaches the recorder at all, and this returns null for it —
// which is the same verdict the bail-out tests were always asserting, arrived
// at one step earlier. That is a strengthening, not a loosening: the compiler
// is the authority on what the language accepts.
// =============================================================================

#ifndef _IB_TEST_LAMBDA_RECORD_FIX_H_
#define _IB_TEST_LAMBDA_RECORD_FIX_H_

#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileContext.h"   // CODE_CES
#include "backend/compiler/lambdaQueryAST.h"
#include "backend/query/queryAST.h"

#include <iostream>
#include <memory>

// ⭐ A NULL WITH NO REASON COSTS A REBUILD TO INVESTIGATE. Every road out of the
// helper says WHY on the way, so a red line in the report carries its own
// diagnosis — the recorder's own refusal text where there is one, and the
// compiler's message where the body never reached the recorder at all.
inline std::shared_ptr<ibQueryAstExpr> Refused(const wxString& source, const wxString& why)
{
	std::cerr << "    [not recorded] " << why.ToStdString()
		<< "\n        source: " << source.ToStdString() << std::endl;
	return nullptr;
}

// Wrap a lambda BODY in a real one-parameter lambda, in whichever dialect the
// suite has selected, and record what the compiler emitted for it.
//
// The body is taken as the suites already write it: braced (`{ return …; }`) in
// the brace dialect, and `Return …;` with or without its own `EndFunction` in
// the other. Anything the compiler refuses answers null.
// `outerNames` are declared BEFORE the lambda, comma-separated — the names a body captures. A
// capture is not decoration here: the compiler refuses a body that reads an undeclared name
// ("Var is not found"), so a test about capturing has to have something to capture.
inline std::shared_ptr<ibQueryAstExpr> ibTestRecordLambda(
	const wxString& body, const wxString& rowParam = wxT("x"),
	const wxString& outerNames = wxEmptyString)
{
	wxString lambdaBody = body;
	lambdaBody.Trim(true).Trim(false);

	const wxString preamble = outerNames.IsEmpty()
		? wxString() : (wxT("var ") + outerNames + wxT(";\n"));

	wxString source;
	if (ibCompileCode::GetCodeStyle() == CODE_CES) {
		if (!lambdaBody.StartsWith(wxT("{")))
			lambdaBody = wxT("{ ") + lambdaBody + wxT(" }");
	}
	else if (!lambdaBody.Lower().Contains(wxT("endfunction"))) {
		lambdaBody += wxT(" EndFunction");
	}
	source = preamble + wxT("var f = Function(") + rowParam + wxT(") ") + lambdaBody + wxT(";");

	ibCompileCode compiler(wxT("test"), wxT("memory"), false);
	try {
		if (!compiler.Compile(source))
			return Refused(source, wxT("Compile() returned false"));
	}
	catch (const ibBackendException& err) {
		// A body the language does not accept — see the header comment: that is
		// an answer of its own, and it is null.
		return Refused(source, err.GetErrorDescription());
	}
	catch (...) {
		return Refused(source, wxT("unknown exception"));
	}

	// The lambda's own entry. A module compiled from the text above holds
	// exactly one, and it is the only kind the recorder takes.
	long funcIndex = -1;
	for (size_t i = 0; i < compiler.m_cByteCode.m_listFunc.size(); i++) {
		if (compiler.m_cByteCode.m_listFunc[i].m_kind == ibFnKind::Lambda) {
			funcIndex = (long)i;
			break;
		}
	}
	if (funcIndex < 0)
		return Refused(source, wxT("the compile produced no lambda entry"));

	// ⭐ THE ONE FACT THE INSTRUCTIONS DO NOT CARRY: the NAME of a captured outer
	// local. The compiler cannot read it off the bytecode because the enclosing
	// function is still being compiled at that moment, so it answers from its
	// live compile context. A test asks AFTER the compile, when the very same
	// table has landed in the bytecode — so the answer is read there instead,
	// which is also how any post-compile caller would get it.
	const auto nameOfOuter = [&compiler](long framesOut, long slot) -> wxString {
		if (framesOut != 1)   // 1 = the module body the lambda was written in
			return wxString();
		for (const auto& var : compiler.m_cByteCode.m_listVar)
			if ((long)var.m_slotIndex == slot)
				return var.m_strRealName;
		return wxString();
	};

	wxString refusal;
	const std::shared_ptr<ibQueryAstExpr> tree =
		ibBuildLambdaQueryAstFromCode(compiler.m_cByteCode, funcIndex, &refusal, nameOfOuter);
	if (tree == nullptr)
		return Refused(source, refusal.IsEmpty() ? wxT("(the recorder gave no reason)") : refusal);
	return tree;
}

#endif
