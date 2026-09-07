#include "byteCode.h"

#include "codeDef.h"        // the opcode vocabulary — FindCaret reads the declaration opcodes

#include <shared_mutex>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////
// ibByteCode registry — process-wide map of descriptor GUID → ibByteCode*.
//
// Sole consumer is dependency resolution: a freshly-loaded bytecode
// walks its m_dependencyIds, looks each up via Find, and points
// m_dependencies at the live targets. Until this resolution runs,
// dependent lookups can only see m_parent (which is wired separately
// by the descriptor's SetParent cascade).
//
// Lookup is read-mostly — every Compile call hits Find at least once
// per dep — so a shared_mutex pays for itself. Mutation only happens
// on Compile success / descriptor teardown.
////////////////////////////////////////////////////////////////////////

namespace {
	std::shared_mutex                          g_byteCodeRegistryMutex;
	std::unordered_map<wxString, ibByteCode*>  g_byteCodeRegistry;
}

void ibByteCode::Register(ibByteCode* bc)
{
	if (bc == nullptr) return;
	if (!bc->m_id.isValid()) return;
	std::unique_lock<std::shared_mutex> lk(g_byteCodeRegistryMutex);
	g_byteCodeRegistry[wxString(bc->m_id)] = bc;
}

void ibByteCode::Unregister(const ibGuid& descId)
{
	if (!descId.isValid()) return;
	std::unique_lock<std::shared_mutex> lk(g_byteCodeRegistryMutex);
	g_byteCodeRegistry.erase(wxString(descId));
}

ibByteCode* ibByteCode::Find(const ibGuid& descId)
{
	if (!descId.isValid()) return nullptr;
	std::shared_lock<std::shared_mutex> lk(g_byteCodeRegistryMutex);
	auto it = g_byteCodeRegistry.find(wxString(descId));
	return it != g_byteCodeRegistry.end() ? it->second : nullptr;
}

bool ibByteCode::ResolveAndVerifyDependencies()
{
	// Parallel-vector invariant — Save/Load preserve it; defensive in
	// case a deserialised blob is malformed.
	if (m_dependencyIds.size() != m_dependencyVersions.size())
		return false;

	// Build resolved list in a temp first so a partial walk doesn't
	// leave m_dependencies half-populated when we bail.
	std::vector<ibByteCode*> resolved;
	resolved.reserve(m_dependencyIds.size());
	for (size_t i = 0; i < m_dependencyIds.size(); ++i) {
		ibByteCode* dep = Find(m_dependencyIds[i]);
		if (dep == nullptr) return false;
		// m_version is an ibGuid — operator== exists; relying on it
		// here keeps the comparison opaque to the storage choice.
		if (!(dep->m_version == m_dependencyVersions[i])) return false;
		resolved.push_back(dep);
	}
	m_dependencies = std::move(resolved);
	return true;
}

////////////////////////////////////////////////////////////////////////
// ibByteBinder — per-execution binding session                       //
////////////////////////////////////////////////////////////////////////

// m_slots is sized to (max m_slotIndex among bindable entries) + 1 so SetVar /
// pre-flight indexing by slot is in-range. Bindable = External/Context (must-bind)
// plus plain Local (a bound local like a constant's Value). ContextProp is skipped
// — it goes through OPER_GET_A on parent, not a binder slot.
//
// Including ALL Local slots (not just bound ones) is REQUIRED, not cosmetic:
// SetVar writes m_slots[slotIndex] with NO bounds check, and the bytecode var
// info carries no "this local is bound" flag (the bind list lives in the compile
// module, not the bc), so the binder cannot tell a bound local from an ordinary
// one. Sizing to the max Local slot guarantees a bound local's SetVar is in-range.
// The cost is a few extra null slots for ordinary locals — they are never SetVar'd
// and the Execute pre-flight skips null. The vector is built once per Run, not per
// call, so the frame-sized allocation is negligible.
static size_t computeBinderSlotCount(const std::vector<ibByteCode::ibByteCodeVarInfo>& vars)
{
	long maxSlot = -1;
	for (const auto& v : vars) {
		if (!v.IsBindable()) continue;
		if (v.m_slotIndex > maxSlot) maxSlot = v.m_slotIndex;
	}
	return static_cast<size_t>(maxSlot + 1);
}

ibByteBinder::ibByteBinder(const std::vector<ibByteCode::ibByteCodeVarInfo>& vars, bool delta /*= true*/)
	: m_vars(vars),
	  m_delta(delta),
	  m_slots(computeBinderSlotCount(vars), nullptr)
{
}

void ibByteBinder::SetVar(const wxString& name, ibValue* value)
{
	for (const auto& v : m_vars) {
		if (!v.IsBindable()) continue;
		if (!stringUtils::CompareString(v.m_strRealName, name)) continue;
		m_slots[v.m_slotIndex] = value;
		return;
	}
	// Name not declared as a bindable (External/Context/Local) — silently
	// ignored. Caller may pass extras; the runtime only reads what bytecode
	// declared.
	//
	// Matching a plain Local by name is safe despite being broader than the old
	// External/Context-only match: var-table names are unique, and a bind name IS
	// its table entry (Pass 1b registers "Value" as the Local), so there is no
	// separate user local of the same name to collide with — SetVar only ever
	// runs with controlled bind names ("Value" / "ThisObject" / "Controls" / …).
}

////////////////////////////////////////////////////////////////////////
// ibByteCode ibByteCode ibByteCode ibByteCode						      //
////////////////////////////////////////////////////////////////////////

// All Find* functions read the unified m_listFunc. Visibility filter
// per kind:
//   - "Export*" variants: skip kind=Local (privates not visible).
//   - Plain (non-Export) variants: see all kinds.
// Function vs procedure split: m_bCodeRet (true = function with return,
// false = procedure).

long ibByteCode::FindMethod(const wxString& strMethodName) const
{
	auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
		[&](const auto& fn) { return stringUtils::CompareString(strMethodName, fn.m_strRealName); });
	if (iterator != m_listFunc.end())
		return (long)*iterator;
	return wxNOT_FOUND;
}

long ibByteCode::FindExportMethod(const wxString& strMethodName) const
{
	auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
		[&](const auto& fn) {
			if (fn.IsLocal() || fn.IsProtected()) return false; // Protected isn't a config-wide export
			return stringUtils::CompareString(strMethodName, fn.m_strRealName);
		});
	if (iterator != m_listFunc.end())
		return (long)*iterator;
	return wxNOT_FOUND;
}

long ibByteCode::FindFunction(const wxString& funcName) const
{
	auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
		[&](const auto& fn) { return fn.m_bCodeRet && stringUtils::CompareString(funcName, fn.m_strRealName); });
	if (iterator != m_listFunc.end())
		return (long)*iterator;
	return wxNOT_FOUND;
}

long ibByteCode::FindExportFunction(const wxString& funcName) const
{
	auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
		[&](const auto& fn) {
			if (fn.IsLocal() || fn.IsProtected()) return false; // Protected isn't a config-wide export
			return fn.m_bCodeRet && stringUtils::CompareString(funcName, fn.m_strRealName);
		});
	if (iterator != m_listFunc.end())
		return (long)*iterator;
	return wxNOT_FOUND;
}

long ibByteCode::FindProcedure(const wxString& procName) const
{
	auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
		[&](const auto& fn) { return !fn.m_bCodeRet && stringUtils::CompareString(procName, fn.m_strRealName); });
	if (iterator != m_listFunc.end())
		return (long)*iterator;
	return wxNOT_FOUND;
}

long ibByteCode::FindExportProcedure(const wxString& procName) const
{
	auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
		[&](const auto& fn) {
			if (fn.IsLocal() || fn.IsProtected()) return false; // Protected isn't a config-wide export
			return !fn.m_bCodeRet && stringUtils::CompareString(procName, fn.m_strRealName);
		});
	if (iterator != m_listFunc.end())
		return (long)*iterator;
	return wxNOT_FOUND;
}

// Bytecode-driven cross-module symbol resolution. Recursive walk:
// self (depth 0) → m_dependencies (depth+1) → m_parent chain
// (depth+1 per step). First match wins. depth = "frames up to root
// descriptor at runtime" — feeds m_numArray on the caller's
// emitted opcode.
//
// Visibility budget — fullVisDepth: at depth ≤ budget, all entries
// visible (Local + Export + ContextMethod). At depth > budget, only
// non-Local (Export + ContextMethod). Default budget 0 = only own
// bytecode sees privates.

namespace {

ibByteCode::ResolvedFunc ResolveFunctionAt(const ibByteCode* bc, const wxString& name, int depth, int fullVisDepth)
{
	if (bc == nullptr) return {};
	auto it = std::find_if(bc->m_listFunc.begin(), bc->m_listFunc.end(),
		[&](const auto& fn) {
			if (depth > fullVisDepth && fn.IsLocal()) return false;
			return stringUtils::CompareString(name, fn.m_strRealName);
		});
	if (it != bc->m_listFunc.end())
		return { &(*it), depth };
	for (const ibByteCode* dep : bc->m_dependencies) {
		if (auto r = ResolveFunctionAt(dep, name, depth + 1, fullVisDepth)) return r;
	}
	if (bc->m_parent != nullptr)
		return ResolveFunctionAt(bc->m_parent, name, depth + 1, fullVisDepth);
	return {};
}

} // anonymous

ibByteCode::ResolvedFunc ibByteCode::ResolveFunction(const wxString& funcName, int fullVisDepth) const
{
	return ResolveFunctionAt(this, funcName, 0, fullVisDepth);
}
// WHERE A CARET STANDS — see the note on ibCaretPoint.
//
// One walk of the instruction stream, because both answers come off the same pass: the last
// instruction at or before the position, and the function that was open when it was emitted. The
// stream is in emission order, so "open" is a depth counter over the declaration opcodes — depth,
// not a boolean, because a lambda body sits inside a function body and closes with its own opcode.
//
// ⚠ THE LAST INSTRUCTION AT OR BEFORE, not the nearest. A caret sits in TEXT, and the text between
// two instructions is what is being typed right now; the instruction before it is the last thing
// the compiler understood, which is exactly the context the caret inherits.
bool ibByteCode::FindCaret(ibCaretPoint& point) const
{
	if (m_listCode.empty())
		return false;

	point.m_instruction = -1;

	for (size_t ip = 0; ip < m_listCode.size(); ++ip) {

		const ibByteUnit& code = m_listCode[ip];

		const bool boundary =
			code.m_numOper == OPER_FUNC || code.m_numOper == OPER_LFUNC ||
			code.m_numOper == OPER_ENDFUNC || code.m_numOper == OPER_ENDLFUNC ||
			code.m_numOper == OPER_END;

		// An instruction with no position of its own (a synthesised jump, a fixup) carries zero and
		// must not drag the answer back to the top of the file.
		if (code.m_numString == 0 && ip > 0)
			continue;

		if (code.m_numString > point.m_position)
			break;

		// ⭐⭐ A BOUNDARY IS WHERE CODE ENDS, NOT WHERE A CARET STANDS. The module's closing marker
		// carries the END of the text as its position and has no destination at all, so answering
		// with it says "nothing here" for every caret at the end of a text that COMPILED — while a
		// text that refused, and so never reached the marker, answered correctly. Measured
		// 2026-09-07: `Catalogs.` (which refuses: a bare expression is not a statement) resolved,
		// and `x = Catalogs.` (which compiles clean) did not. That was the entire difference.
		if (boundary)
			continue;

		point.m_instruction = (long)ip;
	}

	return true;
}
