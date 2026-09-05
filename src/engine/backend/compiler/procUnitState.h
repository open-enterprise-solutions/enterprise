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

#include <memory>

#include "value.h"     // ibValue — m_cacheProbe holds them by value, m_runStack by block

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

// THE LOCALS OF EVERY FRAME ON THE CALL STACK, in one place instead of inside each
// frame. A frame reserves a run of slots on entry and releases it on exit, so the
// runs nest exactly as the calls do.
//
// A frame used to carry `ibValue m_cLocStorage[MAX_STATIC_VAR]` inline — ten slots
// on x86, twenty-five on x64. Measured against real code, that reserve fitted
// neither kind of frame: argument frames never needed more than five, while a real
// application procedure needs sixteen to ninety and so reached `new ibValue[]` on
// every call anyway. Reserving here removes that allocation rather than adding one,
// and takes ~1.2 KB out of every frame on x64 — which is what makes the recursion
// guard reachable, since ibProcUnit::Execute reserves 9 376 bytes of stack per
// interpreted level there. docs/runtime-perf.md §10.
//
// ⚠ THE DISCIPLINE IS LIFO, AND IT IS THE CALLERS' PROPERTY, not a hope: only a
// frame whose life IS one call reserves here. A frame that can outlive its call is
// the case the compiler already marks (ibByteFunction::m_needsHeapFrame → the frame
// is heap-promoted for a lambda to capture), and so is the context embedded in an
// ibProcUnit; both own their slots instead.
struct ibRunStack {

	// Where a frame's slots are, and where the top stood before it took them.
	// `m_mark == kNoRun` is what a frame that owns its slots carries.
	static const unsigned int kNoRun = 0xFFFFFFFFu;

	struct ibRun {
		ibValue*     m_vals = nullptr;
		ibValue**    m_refs = nullptr;
		unsigned int m_block = 0;
		unsigned int m_mark = kNoRun;
	};

	// Hands out `count` slots, empty and with the pointer row filled. FALSE when the
	// request is wider than one block — a function with hundreds of locals gets the
	// heap, which is the honest answer rather than a reason to grow a block nobody
	// else can use.
	bool Reserve(const long count, ibRun& outRun);

	// Returns the newest run, emptying its slots on the way out: a slot holding a
	// reference lets go of it HERE, when the frame ends, rather than whenever some
	// later call happens to reuse the memory.
	void Release(const ibRun& run, const long count);

	// Blocks are kept — a session allocates its stack once and lives on it.
	void Rewind() { m_currentBlock = 0; m_top = 0; }

private:

	// Wide enough that ordinary nesting never leaves a block half-used, small enough
	// that a session running one shallow script does not pay for much.
	static const long kBlockSlots = 256;

	// Constructed ONCE per block and reused by every run that lands there. Held
	// behind pointers so a block never moves while a frame points into it.
	struct ibBlock {
		ibBlock() : m_vals(new ibValue[kBlockSlots]), m_refs(new ibValue * [kBlockSlots]) {}
		std::unique_ptr<ibValue[]>    m_vals;
		std::unique_ptr<ibValue * []> m_refs;
	};

	std::vector<std::unique_ptr<ibBlock>> m_blocks;
	unsigned int                          m_currentBlock = 0;
	long                                  m_top = 0;
};

struct ibProcUnitState {
	// Currently-executing module. Read by every opcode dispatch site
	// to resolve "which module's bytecode are we in".
	ibProcUnit*                 m_currentRunModule = nullptr;

	// Script call stack. Pushed by ibProcStackGuard ctor on every
	// frame entry, popped by dtor.
	std::vector<ibRunContext*>  m_runContext;

	// The locals of every frame on that stack. Beside the stack it mirrors, and a
	// MEMBER rather than a thread_local for the reason the whole struct exists: the
	// worker boundary will run N sessions on M threads, and a session that moves to
	// another thread must find its own values, not that thread's.
	ibRunStack                  m_runStack;

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
