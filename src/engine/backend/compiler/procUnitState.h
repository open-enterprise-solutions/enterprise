#ifndef __IB_PROC_UNIT_STATE_H__
#define __IB_PROC_UNIT_STATE_H__

// Per-session container for ibProcUnit's interpreter state. Today the
// state lives as `thread_local` file-statics in procUnit.cpp (one
// session per thread, so thread_local == per-session); the worker pool
// refactor will swap the contents in/out at session boundaries to
// allow N sessions to share M workers. This struct is the swap target.
//
// See docs/worker-pool-tls-audit.md for the migration plan. Step 1 is
// to provide this struct on ibSession with no behaviour change — the
// interpreter still reads/writes its TLS, the swap helpers come later.

#include <map>
#include <utility>
#include <vector>

#include <wx/defs.h>   // wxNOT_FOUND

#include "value.h"     // ibValue — m_cacheProbe holds them by value

class ibProcUnit;
struct ibRunContext;
struct ibByteCode;

// Where the most recently-raised script exception originated. Mirrored
// from procUnit.cpp where the file-static `s_errorPlace` lives; the
// definition is published here so the worker boundary save/restore can
// move the value between session and TLS.
struct ibErrorPlace {
	long              m_errorLine    = wxNOT_FOUND;
	const ibByteCode* m_byteCode     = nullptr;
	const ibByteCode* m_skipByteCode = nullptr;

	bool IsEmpty() const { return m_errorLine == wxNOT_FOUND; }
	void Reset() {
		m_byteCode     = nullptr;
		m_skipByteCode = nullptr;
		m_errorLine    = wxNOT_FOUND;
	}
};

// Lambda metadata moved into ibByteCode::m_listFunc with kind = Lambda
// — same path named functions use. ibValueFunction stores
// (parentBc, funcIndex) and resolves shape/names/defaults via
// parentBc->m_listFunc[funcIndex]. No separate descriptor struct.

struct ibProcUnitState {
	// Currently-executing module. Read by every opcode dispatch site
	// to resolve "which module's bytecode are we in".
	ibProcUnit*                 m_currentRunModule = nullptr;

	// Script call stack. Pushed by ibProcStackGuard ctor on every
	// frame entry, popped by dtor.
	std::vector<ibRunContext*>  m_runContext;

	// Site of the last raised exception; used by ProcessError to
	// format the rethrow.
	ibErrorPlace                m_errorPlace;

	// Recursion-depth counter — gates against runaway scripts via
	// MAX_REC_COUNT in procUnit.cpp.
	short                       m_recCount = 0;

	// Scratch buffer the OPER_FUNC entry builds a `Cached` call's argument tuple
	// in before looking it up. It belongs HERE, beside the call stack, for the
	// same reason the call stack does: it is interpreter state, so when the
	// worker boundary swaps a session's state in and out it travels with the
	// rest rather than staying behind on whichever thread happened to run.
	// Reused rather than built per call — a cache hit is meant to cost a hash
	// and a compare, not a heap allocation. Filled and read inside a single
	// instruction, so a nested cached call refills it after the outer one is
	// finished with it.
	std::vector<ibValue>        m_cacheProbe;

	// Resolves the lambda executor for this state. Primary path:
	// session's m_lambdaRuntime (allocated alongside m_root, parent =
	// root's procUnit). Fallback (no session): currently-dispatching
	// ProcUnit — used by codeRunner sandbox runs where ibSession
	// isn't bound. OPER_CALL_LAMBDA caller swaps m_pByteCode for the
	// dispatch and restores after; Execute snapshots m_pByteCode at
	// entry so nested lambda calls don't clobber an outer view.
	ibProcUnit* GetLambdaRuntime();

	// --- accessor methods (mirror the old static API on ibProcUnit) ---
	// ibProcUnit's static forwarders (procUnit.cpp) delegate here on the
	// thread_local instance; the slot stored on ibSession exposes the
	// same operations for save/restore code paths that don't touch TLS.
	ibProcUnit*   GetCurrentRunModule()   const { return m_currentRunModule; }
	void          SetCurrentRunModule(ibProcUnit* u) { m_currentRunModule = u; }
	void          ClearCurrentRunModule()       { m_currentRunModule = nullptr; }

	void          AddRunContext(ibRunContext* r) { m_runContext.push_back(r); }
	unsigned int  GetCountRunContext()    const { return static_cast<unsigned int>(m_runContext.size()); }
	ibRunContext* GetPrevRunContext()     const {
		return m_runContext.size() < 2 ? nullptr : m_runContext[m_runContext.size() - 2];
	}
	ibRunContext* GetCurrentRunContext()  const {
		return m_runContext.empty() ? nullptr : m_runContext.back();
	}
	ibRunContext* GetRunContext(unsigned int idx) const {
		return m_runContext.size() <= idx ? nullptr : m_runContext[idx];
	}
	void          BackRunContext()              { m_runContext.pop_back(); }

	// Convenience: bytecode of the currently-executing frame (top of
	// the runContext stack). Out-of-line because ibRunContext::GetByteCode
	// needs the full type and the forward decl above isn't enough.
	const ibByteCode* GetCurrentByteCode() const;

	// Compound: reset error site + remember the byteCode the throw
	// originated in (for the cross-module rethrow check in Execute's
	// catch block). Public — ibBackendException::ProcessError calls
	// this through GetPUState() before the throw propagates.
	void          Raise();
};

#endif
