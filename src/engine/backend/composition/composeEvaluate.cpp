////////////////////////////////////////////////////////////////////////////
//	Description : evaluating a composition parameter where the data is
////////////////////////////////////////////////////////////////////////////

#include "backend/composition/composeEvaluate.h"

#include "backend/session/session.h"             // the session whose root this evaluates against
#include "backend/moduleManager/moduleManager.h" // …where its ProcUnit lives
#include "backend/compiler/procUnit.h"           // …and Evaluate itself
#include "backend/compiler/procUnitState.h"      // GetCurrentRunContext — the caller's own frame
#include "backend/compiler/procContext.h"        // ibRunContext — the frame it is given

bool ibEvaluateInRoot(const wxString& expression, ibValue& produced, const ibMetaData* metaData)
{
	produced = ibValue();
	if (expression.IsEmpty())
		return true;   // nothing to evaluate is not a failure

	// INSIDE RUNNING CODE THE CALLER'S OWN FRAME IS THE TRUTHFUL ONE — the same rule the built-in
	// Evaluate follows. An expression written on a report opened from a module means what it means
	// where that module stands.
	if (ibProcUnitState* const state = ibSession::GetPUState()) {
		if (ibRunContext* const current = state->GetCurrentRunContext())
			return ibProcUnit::Evaluate(expression, current, produced, false);
	}

	ibSession* const session = ibSession::Current();

	// ⭐⭐ ASKED OF THE SESSION, WHICH OWNS THE ROOT. `EvaluateInRoot` runs the expression against the
	// root's own frame and answers with a value; the frame itself never comes out here (session.h
	// carries the why). That scope is the whole point of a computed parameter: `Catalogs`,
	// `Documents`, every common module and the platform's own functions are names on the root, and
	// none of them is expressible in a query.
	//
	// 🛑 TWO WRONG SHAPES WERE TRIED HERE FIRST, and they fail at opposite ends. A BARE ibRunContext
	// carrying the root's ProcUnit has the bytecode and no slots — every global lands in a frame of
	// none ("Outer frame not bound at depth 1 / idx 7 (the frame holds 0 slots)", and the count was
	// describing the caller's own empty frame). The session's LAMBDA RUNTIME, which borrows the
	// root's scope, has the slots and no bytecode of its own — `ibCompileEval` reads the host
	// bytecode straight off the context, so nothing compiled at all and every expression came back
	// "compile failed", including one made of built-ins. An evaluation needs both halves, and only
	// the root's own frame has them.
	if (session != nullptr && session->GetManagerModule() != nullptr)
		return session->EvaluateInRoot(expression, produced);

	// ⭐⭐ AND IN THE DESIGNER THE ROOT IS THE EDIT MANAGER. There is no runtime root there — and no
	// lambda runtime either, since that is built by CompileRoot — and this used to answer "true,
	// produced nothing": success with an empty value, which the caller cannot tell from an expression
	// that legitimately evaluated to nothing. So a composer's parameter expression was silently
	// ignored for everyone building a report, which is precisely where it is written (Max,
	// 2026-09-01: *"look at how the expression in a report works — there is exactly the same
	// problem"*).
	std::shared_ptr<ibProcUnit> editUnit;
	if (ibValueModuleManager* const editManager = ibSession::EditModuleManagerFor(metaData))
		editUnit = editManager->GetProcUnit();

	// ⚠ NO RUNTIME, NO PRETENDING. Answering "true, produced nothing" is indistinguishable from an
	// expression that legitimately evaluated to empty — which is how a computed parameter comes to be
	// silently ignored and the report merely look wrong.
	if (!editUnit) {
		produced = ibValue(_("there is no runtime in this process to evaluate against"));
		return false;
	}

	ibRunContext editFrame;
	editFrame.SetProcUnit(editUnit.get());
	return ibProcUnit::Evaluate(expression, &editFrame, produced, false);
}
