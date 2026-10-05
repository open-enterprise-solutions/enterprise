#ifndef __IB_COMPILE_STATE_H__
#define __IB_COMPILE_STATE_H__

// Per-session state of the COMPILER — the twin of ibProcUnitState, the interpreter's. What every compile of
// one session shares lives here: today the code style. A session holds one (ibSession::GetCompileState); a
// thread without a session — codeRunner, a test — gets its own, as it does an interpreter state.
//
// Meant to grow (Max, 2026-10-05): modules will be GENERATED, and how all of them compile is then said once,
// for the session, here — not per module. Not a hot path; a slot of its own for the symmetry with the
// interpreter's.

#include "backend/backend_core.h"   // ibProgramSyntax

struct ibCompileState {
	// The syntax the session's modules are written in — its configuration's, taken when its runtime is built
	// (ibSession::CompileRoot); what ibCompileCode::GetCodeStyle answers. A process may hold bases written in
	// different syntaxes, so it is not the process's.
	ibProgramSyntax m_codeStyle = ibProgramSyntax::syntax_ces;
};

#endif
