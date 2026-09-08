////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2x-team
//	Description : LINQ runtime — split out of procUnit.cpp.
//
// What lives here:
//   - CallLambdaWithArgs (+ arity-specific 1/2-arg shims) — invoke an
//     ibValueFunction from C++ with N ref-bound args (no bytecode tape walk).
//   - InvokeLambdaWithArg — BACKEND_API wrapper, used by host-side
//     aggregations (valueArray.cpp Sum/Min/Max/Average selector overloads).
//   - 16 distinct ibValue*State iterator-state classes (Where / Select /
//     Distinct / OrderBy / GroupBy / Join / Skip / Take / SkipWhile /
//     TakeWhile / Reverse / Concat / WhereIndexed / SelectIndexed +
//     templated ibValueSetOpState used for both Intersect and Except.
//     Union composes Concat + Distinct, no dedicated state).
//   - ibValueQuery — wraps the tail of a pipeline; iterable itself so
//     chains compose (q.Where(p).Select(s)).
//   - ibValueLinqDispatchImpl — central switch keyed on ibLinqMethod.
//   - ibValue::DispatchLinqMethod — virtual entry point, default impl
//     (subclasses override in their own .cpp).
//   - ibValue::GetLinqMethodTable — single source of truth: enum id +
//     script name + IntelliSense help. Consumed by FindLinqMethodByName
//     (compile-side resolver) and the frontend codeEditor autocomplete
//     loader (always offer the LINQ surface after `.`).
//   - ibValue::FindLinqMethodByName — script-name -> enum resolver, used
//     by the compile-side at chain-method emit time (compileCode.cpp).
//
// Reached at runtime through the OPER_CALL_LINQ handler in procUnit.cpp,
// which reads the enum id straight from m_param3.m_numIndex (no
// FindMethod string walk, no const-pool lookup) and calls
// pVariable2->DispatchLinqMethod(...).
////////////////////////////////////////////////////////////////////////////

#include "procUnitLambda.h"   // ibValueFunction full def + AsFunction / AsIterator
#include "backend/query/queryException.h"   // ibBackendQueryLinqException — the pipeline refuses in its own variety

#include "system/value/valueTable.h"  // ibValueModelTable — what a query ANSWERS with: columns and rows
#include "system/value/valueQueryable.h"  // ibValueQueryable::TryJoinThroughL3 — RAM-receiver join push-down (Layer 2)

#include <algorithm>
#include <map>
#include <unordered_map>   // the join index — see KeyHash in ibValueJoinState
#include <set>
#include <vector>

// Diagnostics helpers (LinqLog / LinqLambdaTag / LinqValueTag) writing
// to `linq.log` were stripped 2026-05-12 after the LINQ surface
// stabilised. The retired lambda.log / intelli.log pattern. If runtime
// diagnostics are needed again, prefer wxLogDebug at coarse-grained
// entry points rather than per-row writes.

// Lambda invocation with N bound-by-ref args from host (C++) code.
// Mirrors Phase 1+2 of OPER_CALL_LAMBDA minus the OPER_SET/SETCONST
// tape walk — args come from C++ pointers, not bytecode operands.
// argPtrs[0..argCount-1] are bound to param slots 0..argCount-1 by
// reference; trailing lambda params (argCount..paramCount-1) are
// filled from compile-time defaults and throw if required-without-
// default. Used by both the single-arg LINQ thin wrappers below
// (Where/Select/SkipWhile/...) and the two-arg Join projection +
// Aggregate reducer.
static void CallLambdaWithArgs(ibValueFunction& fn, ibValue** argPtrs,
                                long argCount, ibValue& retVal)
{
	const ibByteCode::ibByteFunction* bfn = fn.GetFunction();
	if (bfn == nullptr) {
		ibBackendQueryLinqException::Error(_("Lambda value is not initialised"));
	}

	const long lambdaParamCount = (long)bfn->m_listParam.size();
	const long lambdaVarCount   = bfn->m_lVarCount;

	if (lambdaParamCount < argCount) {
		ibBackendQueryLinqException::Error(
			_("Lambda must accept at least %ld argument(s)"), argCount);
	}


	// A FRAME THAT CAN BE CAPTURED MUST OWN ITS ARGUMENTS.
	//
	// `SelectMany(x => src.Where(y => y > x))` — the inner lambda reads the outer
	// one's parameter, which is what a query block's second binding compiles to.
	// The capture is established at OPER_LFUNC by walking the frame chain and
	// keeping every ancestor whose `weak_from_this()` still locks; a stack frame
	// never locks, so a lambda materialised under a pipeline captured nothing and
	// `x` arrived empty.
	//
	// Promotion alone was tried twice and crashed the corpus, which is why the
	// note here used to say "not the fix". The missing half was the ARGUMENTS.
	// Below, the fast path binds each parameter to the caller's ibValue by
	// POINTER — and under a pipeline those point into the iterator state
	// (`ibValueJoinState::m_curOuter` and friends), objects that die with the
	// pipeline. A captured frame outlives it, so the pointer dangles: the AV was
	// the frame reading its own parameters after the pipeline had gone, not the
	// capture machinery downstream.
	//
	// So the promoted frame COPIES its arguments into its own slots instead of
	// pointing at the caller's. It costs one ibValue copy per argument on the path
	// that needs it, and nothing at all on the path that does not — a lambda with
	// no inner lambda is not flagged and still binds by pointer.
	const bool bHeapFrame = bfn->m_needsHeapFrame;

	// 🛑 THE FRAME IS BUILT PER CALL, AND REUSING IT IS NOT A ONE-LINE SAVING. Tried and taken out
	// the same hour (2026-09-08): a pipeline calls this once per row, so a frame built per row is
	// most of the cost of a row, and a frame whose slots are all parameters looks like it carries
	// nothing between calls — every slot is rebound below. It carries the CALL'S OWN STATE. A body
	// written `{ return … }` opens a block scope and leaves through the RET, so the matching scope
	// exit never runs and `m_currentScopeDepth` climbs by one every row; the reused frame keeps the
	// previous line, the previous depth, and whatever else a frame accumulates while it runs. The
	// run died on an assertion inside the standard library, three hundred thousand rows in.
	//
	// The saving is real and the way to it is not a reset list that has to stay complete forever —
	// it is for the body to have NO FRAME OF ITS OWN, compiled into the caller's the way a loop body
	// is (docs/linq.md §0.2g). Then there is nothing to reuse and nothing to reset.
	std::shared_ptr<ibRunContext> spHeapCtx;
	// The stack frame leases its slots; the heap-promoted one cannot, because it is
	// the case that OUTLIVES the call — a lambda captured it. procUnitState.h, ibRunStack.
	ibRunContext                  stackCtx(bHeapFrame ? wxNOT_FOUND : (int)lambdaVarCount,
	                                       ibRunLifetime::PerCall);
	if (bHeapFrame)
		spHeapCtx = std::make_shared<ibRunContext>((int)lambdaVarCount);

	ibRunContext& cRunContext = bHeapFrame ? *spHeapCtx : stackCtx;

	cRunContext.m_lStart          = bfn->m_lCodeLine + 1;
	cRunContext.m_lParamCount     = lambdaParamCount;
	cRunContext.m_currentFunction = bfn;

	for (long i = 0; i < argCount; ++i) {
		if (bHeapFrame)
			CopyValue(cRunContext.m_pLocVars[i], *argPtrs[i]);   // the frame owns it
		else
			cRunContext.m_pRefLocVars[i] = argPtrs[i];
	}

	// Fill missing tail params from compile-time defaults.
	const ibByteCode* pLocalByteCode = fn.GetParentBc();
	for (long i = argCount; i < lambdaParamCount; i++) {
		if (i >= (long)bfn->m_listParam.size()) {
			ibBackendQueryLinqException::Error(
				_("Lambda m_listParam shorter than paramCount at %ld"), i);
		}
		const ibParamRunUnit& puDef = bfn->m_listParam[i].m_defaultValue;
		if (puDef.m_numArray == DEF_VAR_SKIP) {
			const wxString& nm = (i < (long)bfn->m_listParam.size())
				? bfn->m_listParam[i].m_strName
				: wxString::Format(wxT("p%ld"), i);
			ibBackendQueryLinqException::Error(
				_("Lambda missing required argument '%s'"), nm);
		}
		CopyValue(cRunContext.m_pLocVars[i], pLocalByteCode->m_listConst[puDef.m_numIndex]);
	}

	fn.Execute(&cRunContext, &retVal, /*bDelta*/false);

}

// Thin arity-specific wrappers — keep the call sites readable
// (`CallLambdaWithArg(predicate, row, result)` reads better than
// `CallLambdaWithArgs(predicate, {&row}, 1, result)`).
static inline void CallLambdaWithArg(ibValueFunction& fn, ibValue& arg, ibValue& retVal) {
	ibValue* ptrs[] = { &arg };
	CallLambdaWithArgs(fn, ptrs, 1, retVal);
}

static inline void CallLambdaWith2Args(ibValueFunction& fn, ibValue& arg0,
                                       ibValue& arg1, ibValue& retVal) {
	ibValue* ptrs[] = { &arg0, &arg1 };
	CallLambdaWithArgs(fn, ptrs, 2, retVal);
}

// ibEventDispatcher facet — a lambda dispatches by running ITS OWN body with the event args (+ the trailing cancel
// ref, the classic event contract). Reuses the SAME frame/param-bind path the LINQ predicates use above; the return
// value is discarded (an event handler's meaningful output is the cancel flag it may set). `runtime` is ignored — the
// lambda resolves the session's lambda runtime itself. Non-const: the invoke may heap-promote the captured frames.
bool ibValueFunction::Dispatch(ibProcUnit* /*runtime*/, ibValue** args, long argc, ibValue& outCancel)
{
	std::vector<ibValue*> params(args, args + argc);
	params.push_back(&outCancel);
	ibValue ret;
	CallLambdaWithArgs(*this, params.data(), (long)params.size(), ret);
	return outCancel.GetBoolean();
}

// Public host-API wrapper — invoke a lambda value (TYPE_FUNCTION,
// directly or wrapped in TYPE_REFFER) with a single argument from
// C++ code. Used by ibValueArray's Sum/Min/Max/Average selector
// overloads and other aggregation helpers that need to fire a script
// lambda from host code without going through OPER_CALL_LAMBDA.
//
// Returns true on successful invoke (retVal populated with lambda's
// return value); false if `callable` isn't a callable lambda value
// (caller falls back to no-selector path).
bool InvokeLambda(ibValue& callable, ibValue** argPtrs, long n, ibValue& retVal) {
	ibValueFunction* fn = AsFunction(&callable);
	if (fn == nullptr)
		return false;
	CallLambdaWithArgs(*fn, argPtrs, n, retVal);
	return true;
}

// Single-argument convenience — the array form with one element.
bool InvokeLambdaWithArg(ibValue& callable, ibValue& arg, ibValue& retVal) {
	ibValue* p = &arg;
	return InvokeLambda(callable, &p, 1, retVal);
}

// Where node — filters upstream by a boolean lambda. Element
// type unchanged, so PeekSample forwards upstream.
class ibValueWhereState : public ibValueIteratorState {
public:
	ibValueWhereState(std::shared_ptr<ibValueIteratorState> upstream,
	                  ibValueFunction predicate)
		: m_upstream(std::move(upstream))
		, m_predicate(predicate)
	{
	}

	bool MoveNext(ibValue& current) override {
		if (!m_upstream) {
			return false;
		}
		while (m_upstream->MoveNext(current)) {
			ibValue result;
			{
				// SQL three-valued NULL for the filter predicate: a comparison with a NULL
				// operand yields UNKNOWN, and IsHasValue (keep-on-TRUE) drops it — so the RAM
				// floor agrees with the SQL push-down / L3 fold. See ts_threeValuedNullCompare.
				ScopedThreeValuedNull tv;
				CallLambdaWithArg(m_predicate, current, result);
			}
			const bool keep = IsHasValue(result);
			if (keep) return true;
		}
		return false;
	}

	void Reset() override {
		if (m_upstream) m_upstream->Reset();
	}

	bool PeekSample(ibValue& current) const override {
		return m_upstream ? m_upstream->PeekSample(current) : false;
	}

private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	ibValueFunction                       m_predicate;
};

// Distinct node — filters duplicates via linear seen-list. Each
// emitted value goes into m_seen; subsequent matches are skipped.
// O(N²) per full iteration — acceptable for Phase 1; Phase 2 may
// swap to a hash-set if profiling demands. Lifetime: shared_ptr
// owns the state, m_seen vector destroys with the state.
class ibValueDistinctState : public ibValueIteratorState {
public:
	explicit ibValueDistinctState(std::shared_ptr<ibValueIteratorState> upstream)
		: m_upstream(std::move(upstream))
	{
	}

	bool MoveNext(ibValue& current) override {
		if (!m_upstream) {
			return false;
		}
		while (m_upstream->MoveNext(current)) {
			bool found = false;
			for (const auto& v : m_seen) {
				if (current == v) { found = true; break; }
			}
			if (!found) {
				m_seen.push_back(current);
				return true;
			}
		}
		return false;
	}

	void Reset() override {
		m_seen.clear();
		if (m_upstream) m_upstream->Reset();
	}

	bool PeekSample(ibValue& current) const override {
		return m_upstream ? m_upstream->PeekSample(current) : false;
	}

private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	std::vector<ibValue>                   m_seen;
};

// Select node — projects each upstream row through a lambda.
// Element type is the lambda body's return type, unknowable
// without body analysis, so PeekSample stays "no hint".
class ibValueSelectState : public ibValueIteratorState {
public:
	ibValueSelectState(std::shared_ptr<ibValueIteratorState> upstream,
	                   ibValueFunction projection)
		: m_upstream(std::move(upstream))
		, m_projection(projection)
	{
	}

	bool MoveNext(ibValue& current) override {
		if (!m_upstream) {
			return false;
		}
		ibValue src;
		if (!m_upstream->MoveNext(src)) {
			return false;
		}
		CallLambdaWithArg(m_projection, src, current);
		return true;
	}

	void Reset() override {
		if (m_upstream) m_upstream->Reset();
	}

	bool PeekSample(ibValue& /*current*/) const override {
		return false;
	}

private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	ibValueFunction                       m_projection;
};

// FLATTEN — the projection yields a SOURCE per element, and its elements are
// handed on in turn. This is what a second `from` in a query block means
// (`from x in a from y in b` iterates b for every x), and it is a pipeline
// operator like any other rather than a nested loop the compiler hand-rolls.
//
// The inner iterator is held between calls, because MoveNext must resume where
// the last one left off; when it runs dry the next outer element opens a new one.
class ibValueSelectManyState : public ibValueIteratorState {
public:
	ibValueSelectManyState(std::shared_ptr<ibValueIteratorState> upstream,
	                       ibValueFunction projection)
		: m_upstream(std::move(upstream))
		, m_projection(projection)
	{
	}

	// The pair-projecting form: `SelectMany(x => src, (x, y) => …)`.
	ibValueSelectManyState(std::shared_ptr<ibValueIteratorState> upstream,
	                       ibValueFunction projection, ibValueFunction result)
		: m_upstream(std::move(upstream))
		, m_projection(projection)
		, m_result(result)
		, m_hasResult(true)
	{
	}

	bool MoveNext(ibValue& current) override {
		if (!m_upstream) {
			return false;
		}
		for (;;) {
			if (m_inner && m_inner->MoveNext(current)) {
				// WITH A RESULT SELECTOR, a flattened element is not the answer —
				// the PAIR is. `from x in a from y in b` reads both x and y after
				// it, so the row that leaves has to carry both, and only this
				// state still holds the outer one.
				//
				// The same shape ibValueJoinState already uses for its projection.
				// Without it the compiler had to nest a lambda inside the
				// collection selector to reach the outer row, and a lambda nested
				// inside a pipeline lambda loses its capture
				// (RuntimeTest.DISABLED_ALambdaInsideAPipelineLambdaSeesTheOuterParameter).
				if (m_hasResult) {
					ibValue elem = current;
					CallLambdaWith2Args(m_result, m_curOuter, elem, current);
				}
				return true;
			}
			ibValue outer;
			if (!m_upstream->MoveNext(outer)) {
				return false;
			}
			m_curOuter = outer;
			ibValue inner;
			CallLambdaWithArg(m_projection, outer, inner);
			// A projection that does not yield a source contributes nothing —
			// refusing is not the floor here, skipping is: one bad row must not
			// end the sequence.
			m_inner = inner.CreateIterator();
		}
	}

	void Reset() override {
		m_inner.reset();
		if (m_upstream) m_upstream->Reset();
	}

	bool PeekSample(ibValue& /*current*/) const override {
		return false;
	}

private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	std::shared_ptr<ibValueIteratorState> m_inner;
	ibValueFunction                       m_projection;

	// Only when a result selector was given: the outer element the inner
	// sequence is currently being drawn from.
	ibValueFunction                       m_result;
	ibValue                               m_curOuter;
	bool                                  m_hasResult = false;
};

// OrderBy node — materialise upstream into a vector, sort by
// key extracted via fn(elem). Semantically "lazy" (returns
// ibValueQuery) but materialises on first MoveNext — sorting
// requires the full sequence in hand. ascending only for now;
// descending = OrderByDescending follow-up (or chain .Reverse()).
class ibValueOrderByState : public ibValueIteratorState {
public:
	ibValueOrderByState(std::shared_ptr<ibValueIteratorState> upstream,
	                    ibValueFunction keySelector,
	                    bool descending)
		: m_upstream(std::move(upstream))
		, m_keySelector(keySelector)
		, m_descending(descending)
	{
	}

	bool MoveNext(ibValue& current) override {
		EnsureSorted();
		if (m_pos >= (long)m_sorted.size()) return false;
		current = m_sorted[m_pos++];
		return true;
	}

	void Reset() override {
		m_pos = 0;
		// Keep m_sorted — single-pass materialisation; subsequent
		// iterations reuse it. If upstream might have changed, drop
		// m_sorted to force rebuild.
	}

	bool PeekSample(ibValue& current) const override {
		return m_upstream ? m_upstream->PeekSample(current) : false;
	}

private:
	void EnsureSorted() {
		if (m_built) return;
		m_built = true;
		if (!m_upstream) return;
		ibValue elem;
		while (m_upstream->MoveNext(elem)) {
			ibValue key;
			CallLambdaWithArg(m_keySelector, elem, key);
			m_keyed.emplace_back(key, elem);
		}
		const bool desc = m_descending;
		std::stable_sort(m_keyed.begin(), m_keyed.end(),
			[desc](const std::pair<ibValue, ibValue>& a, const std::pair<ibValue, ibValue>& b) {
				return desc ? (b.first < a.first) : (a.first < b.first);
			});
		m_sorted.reserve(m_keyed.size());
		for (auto& p : m_keyed) m_sorted.push_back(std::move(p.second));
		m_keyed.clear();
	}

	std::shared_ptr<ibValueIteratorState>     m_upstream;
	ibValueFunction                            m_keySelector;
	bool                                       m_descending = false;
	bool                                       m_built      = false;
	std::vector<std::pair<ibValue, ibValue>>   m_keyed;
	std::vector<ibValue>                       m_sorted;
	long                                       m_pos        = 0;
};

// The key policy for both indexes — group-by's and join's — is ibValueHash /
// ibValueEqual from value.h, shared with everything else that keys by value.
// It used to be declared here; a second copy of "what makes two keys the same"
// is exactly the thing that drifts.
//
// Both indexes were std::map: a tree asks the comparator ~log2(n) times per
// lookup where a bucket asks about once, and comparing an ibValue is the
// expensive part (docs/runtime-perf.md §9). Neither is a sorted structure by
// intent — group-by keeps its emission order in a separate vector (m_groups),
// join has no order at all — so the tree was buying ordering nobody read.

// ⭐⭐ LINQ'S OWN COLLECTION — what a chain COMPILED AS A LOOP works in (compileCode.cpp), as opposed
// to the state classes around it, which is what a chain BUILT AS OBJECTS works in.
//
// The obvious collection is the script's `Array`, and it is the wrong one twice over:
//
//   * IT IS HEAVY. `ibValueArray` is a USER-VISIBLE type — a member table, dynamic members, script
//     semantics, and a `Contains` that walks the whole thing comparing values. `Distinct` written
//     that way costs O(n) PER ROW: fifty million comparisons over ten thousand rows, for a question
//     a set answers in log n.
//   * IT IS BUILT BY NAME. `New Array` compiles to `OPER_NEW` carrying a const STRING, and the
//     runtime resolves the class through `ibValue::CreateObject(className, …)` — a name lookup, at
//     run time, for an object nobody can see, reach or name.
//
// None of that belongs to machinery the person never sees. A pipeline needs a few plain containers
// and no surface at all, and that is this class. Script meets it in exactly one place: where the
// person ASKED for a collection, `ibLinqResult` builds one Array, once, at the end.
//
// ⚠ It is an ibValue only because a frame slot holds ibValues — that is the mechanism for "a thing
// that lives as long as this loop does", and reusing it costs nothing. No member table, no methods,
// no name, no ctor registration: nothing in the language can reach it, call it or write to it.
// ⭐⭐ THE LIGHT CLASS A COMPILED PIPELINE WORKS IN — and the ONE thing it works in.
//
// What a chain needs at run time is small and completely known: keep a row, ask whether a value has
// been seen, put a row under a key, take the rows under a key, order what was kept, and hand the
// answer back. The engine's own collections can all do that — and each of them costs what a
// USER-VISIBLE value costs:
//
//   * `Array` is created through `OPER_NEW` carrying a const STRING, so the class is resolved BY
//     NAME at run time; `Contains` then walks the whole thing, so `Distinct` is O(n) per row.
//   * `Structure` and `Container` key their members by STRING: every `g.Key` is a lookup.
//
// None of that is needed by machinery nobody can see. Here there is no name anywhere: members are
// declared once, so the compiler turns `Key` and `Values` into the ordinals 0 and 1 while it is
// compiling and the access is a switch on a number; a bucket is found by its key in log n; and
// iterating what was kept walks the vector IN PLACE, building nothing per row.
//
// It is an ibValue because a frame slot holds ibValues — that is how a thing lives exactly as long
// as its loop does. It is registered (at the bottom, with its neighbours) because a frame slot is
// ENUMERATED: the debugger lists locals and asks each what it is, and a type the engine has never
// heard of has no answer. What keeps it out of the language is that nothing names it and no ctor is
// published — not that it is unknown.

// ⭐⭐ THE FOUR CLASS IDS, DECLARED HERE AND REGISTERED AT THE BOTTOM. They sit above the classes
// because each class ANSWERS with its own id — see GetClassType on each — and a constant cannot be
// used before it is written down. The registration itself stays at the bottom with its neighbours.
//
// 🛑 WHY THE CLASSES ANSWER AT ALL, INSTEAD OF LETTING THE BASE ANSWER FOR THEM. `ibValue::
// GetClassType()` for an object tag ends in `GetTypeIDByRef(this)`, which takes `typeid(*this)` and
// looks the result up in the ctor REGISTRY (valueFactory.cpp) — RTTI plus a map probe to learn a
// number the class has known since it was compiled. These four are the values a query is MADE of,
// so that question is asked per row and, in a projection, per field.
constexpr ibClassID g_valueLinqRows   = system_to_clsid("VL_LQRW");
constexpr ibClassID g_valueLinqShape  = system_to_clsid("VL_LQSH");
constexpr ibClassID g_valueLinqRecord = system_to_clsid("VL_LQRC");
constexpr ibClassID g_valueLinqGroup  = system_to_clsid("VL_LQGR");

// ⭐ "IS THIS ONE OF OURS" — AN INTEGER COMPARE, NOT A TYPE WALK. Every wrapper below answers its
// own id, so the question needs no `dynamic_cast`: ask the value what it is, and cast only once the
// answer says so. Still CHECKED — a bare `static_cast` would be a claim rather than a question, and
// these values reach here from frame slots the walk does not own.
//
// ⚠ AND IT IS NOT A SPEEDUP — MEASURED, so that nobody spends the day rediscovering it. Five of
// these run per projected row, and A/B against `dynamic_cast` under identical conditions (min of
// three runs each, the bench alone) reads: project 1 field 1085.3 → 1049.8, project 3 fields
// 1657.3 → 1662.2, block select n=16000 709.1 → 691.7. Inside the noise. On this hierarchy — single
// inheritance, no virtual bases — MSVC's dynamic_cast is cheap enough not to show.
//
// It stays because of what it removes ELSEWHERE: the base `GetClassType()` for an object tag ends
// in RTTI plus a ctor-registry probe, and that is asked by the debugger's locals view, by a watch
// and by `TypeOf`, not only here.
template <class T>
inline T* LinqCast(ibValue* held, ibClassID clsid)
{
	return (held != nullptr && held->GetClassType() == clsid) ? static_cast<T*>(held) : nullptr;
}

// The two members of a group — declared ONCE, which is exactly what makes them ordinals.
inline void ibBindLinqGroupMembers(ibValue::ibMemberTable& helper, const ibValue*)
{
	helper.AppendProp(wxT("Key"),    true, false, 0);
	helper.AppendProp(wxT("Values"), true, false, 1);
}

class ibValueLinqRows;

// A GROUP: its key, and the rows under it. Nothing is copied to make one — the rows are the
// collection's own, handed out as a view.
class ibValueLinqGroup : public ibValueStaticMembers<&ibBindLinqGroupMembers> {
public:
	ibValueLinqGroup() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}
	ibValueLinqGroup(const ibValue& key, const ibValue& rows)
		: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_key(key), m_rows(rows) {}

	// Its own id, not the registry's copy of it — see the note beside the constants.
	virtual ibClassID GetClassType() const override { return g_valueLinqGroup; }

	// ⭐ AND ITS OWN ORDINALS, like the record's below. The shared member table still DESCRIBES the
	// surface — a watch and a property grid enumerate it — but turning a name into a number is a
	// question two names can answer outright, without the virtual hop into the general machinery.
	// It is asked once per `g.Key` and once per `g.Values`, i.e. per group of every grouped loop.
	virtual long FindProp(const wxString& name) const override {
		if (stringUtils::CompareString(name, wxT("Key")))    return 0;
		if (stringUtils::CompareString(name, wxT("Values"))) return 1;
		return wxNOT_FOUND;
	}

	virtual bool     IsEmpty()   const override { return false; }
	virtual wxString GetString() const override { return m_key.GetString(); }

	// BY ORDINAL — the names became these numbers at compile time.
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override {
		switch (lPropNum) {
		case 0: pvarPropVal = m_key;  return true;
		case 1: pvarPropVal = m_rows; return true;
		}
		return false;
	}
	virtual bool SetPropVal(const long, const ibValue&) override { return false; }

private:
	ibValue m_key;
	ibValue m_rows;
};

// ⭐⭐ THE SHAPE OF A PROJECTED ROW — the field names of one `select { … }`, in the order they were
// written, which is exactly what makes them ordinals.
//
// A shape is built ONCE per query: the compiler writes the names down as a single constant, the
// first row that needs it splits that constant here, and every row of that query then points at
// this one object. So a name is turned into a number once per QUERY, never per row and never per
// field — and the instruction that fills a row does not carry names at all, only positions.
class ibValueLinqShape : public ibValue {
public:
	ibValueLinqShape() : ibValue(ibValueTypes::TYPE_VALUE) {}
	explicit ibValueLinqShape(const wxString& names) : ibValue(ibValueTypes::TYPE_VALUE) {
		// One constant, split once. `\n` separates the fields — an identifier cannot contain it.
		wxString rest = names;
		while (!rest.IsEmpty()) {
			const int at = rest.Find(wxT('\n'));
			if (at == wxNOT_FOUND) { m_fields.push_back(rest); break; }
			m_fields.push_back(rest.Left(at));
			rest = rest.Mid(at + 1);
		}
	}

	virtual ibClassID GetClassType() const override { return g_valueLinqShape; }

	virtual bool     IsEmpty()   const override { return m_fields.empty(); }
	virtual wxString GetString() const override { return wxT("<row shape>"); }

	long Count() const { return (long)m_fields.size(); }
	const wxString& NameAt(long i) const { return m_fields[(size_t)i]; }

	// The ONLY place a name becomes a number, and it is asked by whoever did NOT get the number
	// baked in at compile time. A projection has a handful of fields, so this is a short walk over
	// a contiguous vector — no map, no member table, nothing built to answer it.
	long Ordinal(const wxString& name) const {
		for (size_t i = 0; i < m_fields.size(); ++i)
			if (stringUtils::CompareString(m_fields[i], name))
				return (long)i;
		return wxNOT_FOUND;
	}

private:
	std::vector<wxString> m_fields;
};

// ⭐⭐ A PROJECTED ROW. What `select { name = expr, … }` produces, and what a `Structure` used to.
//
// The Structure it replaces cost, PER ROW: `OPER_NEW` resolving the class `Structure` through the
// object factory BY NAME, then one `Insert(name, value)` per field — a method resolved by name, its
// arguments loaded through a call frame, and the name stored again inside the object so that every
// later `row.Field` could look it up. For a three-field projection over ten thousand rows that is
// thirty thousand name lookups to express something the compiler knew in full while compiling.
//
// Here the names live in the SHAPE, once per query, and a row is a vector of values in field order.
// Filling one is a store at a known index. Reading one back by name is answered by this class
// itself — `FindProp` walks the shape's names rather than a member table, because a light class
// answers its own questions and does not go through the general machinery to do it.
class ibValueLinqRecord : public ibValue {
public:
	ibValueLinqRecord() : ibValue(ibValueTypes::TYPE_VALUE) {}
	ibValueLinqRecord(ibValueLinqShape* shape, long count)
		: ibValue(ibValueTypes::TYPE_VALUE), m_shape(shape), m_values((size_t)(count < 0 ? 0 : count)) {
		if (m_shape != nullptr) m_shape->IncrRef();
	}
	virtual ~ibValueLinqRecord() { if (m_shape != nullptr) m_shape->DecrRef(); }

	virtual ibClassID GetClassType() const override { return g_valueLinqRecord; }

	// A row EXISTS: it is not empty because a field of it happens to be.
	virtual bool IsEmpty() const override { return m_values.empty(); }

	virtual wxString GetString() const override {
		// What a watch shows. Names included — the person reading it did not write the ordinals.
		wxString text;
		for (size_t i = 0; i < m_values.size(); ++i) {
			if (i != 0) text << wxT(", ");
			if (m_shape != nullptr && (long)i < m_shape->Count()) text << m_shape->NameAt((long)i) << wxT("=");
			text << m_values[i].GetString();
		}
		return text;
	}

	// ⭐ ITS OWN ANSWERS. Four overrides and no member table: the surface IS the shape, so there is
	// nothing to build, nothing to cache and nothing to invalidate.
	virtual long     GetNProps() const override { return (long)m_values.size(); }
	virtual long     FindProp(const wxString& name) const override {
		return m_shape != nullptr ? m_shape->Ordinal(name) : wxNOT_FOUND;
	}
	virtual wxString GetPropName(const long lPropNum) const override {
		// ⚠ `wxString(wxEmptyString)`: outside MSVC the bare constant is a `const wxChar*`, both arms
		// convert to each other and the conditional is AMBIGUOUS — it builds here and fails on macOS
		// and Linux (portability.md §1.10).
		return (m_shape != nullptr && lPropNum >= 0 && lPropNum < m_shape->Count())
			? m_shape->NameAt(lPropNum) : wxString(wxEmptyString);
	}
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override {
		if (lPropNum < 0 || (size_t)lPropNum >= m_values.size()) return false;
		pvarPropVal = m_values[(size_t)lPropNum];
		return true;
	}
	// A projected row is an answer, not a variable: what produced it is gone by the time anyone
	// holds it, so writing a field would change nothing anybody can observe. And it SAYS so —
	// without the override the base answers "writable" from an absent member table, which is how a
	// property grid ends up offering an edit that silently does nothing.
	virtual bool SetPropVal(const long, const ibValue&) override { return false; }
	virtual bool IsPropReadable(const long) const override { return true;  }
	virtual bool IsPropWritable(const long) const override { return false; }

	// The columns this row was made with — the same object for every row of the query, which is what
	// lets the answer be built out of it once instead of asked of every row.
	const ibValueLinqShape* Shape() const { return m_shape; }

	// Filled by the instruction that projects — BY POSITION, which is the position the compiler
	// wrote the field at.
	void SetField(long ordinal, const ibValue& value) {
		if (ordinal >= 0 && (size_t)ordinal < m_values.size())
			m_values[(size_t)ordinal] = value;
	}

private:
	ibValueLinqShape*    m_shape = nullptr;   // shared by every row of the query; refcounted
	std::vector<ibValue> m_values;            // field order = the order they were written in
};

// The collection itself. One object per loop, and everything a pipeline does lives on it.
class ibValueLinqRows : public ibValue {
public:
	ibValueLinqRows() : ibValue(ibValueTypes::TYPE_VALUE) {}
	// A VIEW over somebody else's rows — what a bucket lookup hands out. It keeps the owner alive
	// and copies nothing.
	ibValueLinqRows(ibValueLinqRows* owner, const std::vector<ibValue>* view)
		: ibValue(ibValueTypes::TYPE_VALUE), m_owner(owner), m_view(view) {
		if (m_owner != nullptr) m_owner->IncrRef();
	}
	virtual ~ibValueLinqRows() { if (m_owner != nullptr) m_owner->DecrRef(); }

	virtual ibClassID GetClassType() const override { return g_valueLinqRows; }

	// 🛑 EMPTY MEANS THIS COLLECTION HOLDS NOTHING — rows, BUCKETS or values seen — and it has to,
	// because that is the question a JOIN asks it. The hash a join builds lives entirely in the
	// buckets, so a version of this that answered from the rows alone said "empty" forever: the
	// `!hashSlot` guard read that as "not built yet" and rebuilt the hash on EVERY outer row,
	// appending the whole inner side again each time. Measured on the base — one outer row gave the
	// right answer, the second gave its match twice, the third three times.
	//
	// A VIEW borrows somebody else's rows and has nothing else, so it answers about those.
	virtual bool IsEmpty() const override {
		return m_view != nullptr
			? m_view->empty()
			: (m_rows.empty() && m_buckets.empty() && m_seen.empty());
	}
	virtual wxString GetString() const override { return wxT("<linq>"); }   // watch-safe, and dull

	// ⭐ FIRST TIME? — `Distinct`, entire. ORDERED rather than hashed, and see the note on the
	// buckets below for the measurement that says to keep it that way.
	bool FirstTime(const ibValue& value) { return m_seen.insert(value).second; }

	void Keep(const ibValue& row)    { m_rows.push_back(row); }

	// ⭐⭐ A ROW MAY BE KEYED BY SEVERAL VALUES, and they arrive one at a time. `orderby a, b` is
	// how anybody sorts a report — by warehouse, then by item — and one key per row could not say
	// it: the second clause had nowhere to go, so the language refused it and the only way round
	// was a second pass in script. The keys of a row are kept together and compared in order, which
	// is what "then by" MEANS; the row itself is added once, by the first key.
	//
	// ⚠ THE INDEX IS THE COMPILER'S, not a counter here: it emits one KEEP per key and numbers
	// them, so a key that computes to nothing still occupies its position and the columns stay
	// aligned with the clause that named them.
	void KeepKey(const ibValue& key, long at)
	{
		if (at < 0)
			return;
		if (m_keys.size() < m_rows.size())
			m_keys.resize(m_rows.size());
		if (m_keys.empty())
			return;                                   // a key with no row to belong to
		std::vector<ibValue>& forRow = m_keys.back();
		if ((size_t)at >= forRow.size())
			forRow.resize((size_t)at + 1);
		forRow[(size_t)at] = key;
	}

	long           Count()    const { return (long)Rows().size(); }
	const ibValue& At(long i) const { return Rows()[(size_t)i]; }
	bool           HasKeys()  const { return !m_keys.empty() && m_keys.size() == m_rows.size(); }

	// ⭐ GROUPING AND JOINING. A bucket is found by its key in log n; the order the keys first
	// appeared in is kept apart, because that is the order the answer comes out in and it is not
	// key order.
	//
	// 🛑 A TREE, AND HASHING IT WAS TRIED AND MEASURED SLOWER (2026-09-09). The one key policy for
	// hash containers exists and the CHAIN road uses it — `ibValueJoinState` and
	// `ibValueGroupByState` key `unordered_map` with `ibValueHash` / `ibValueEqual` (value.h) — so
	// this looked like a road left unconverged. Swapping both indexes here to that same pair, join
	// alone, min of three runs each:
	//
	//   n            250      1 000     4 000    16 000
	//   tree      1467.6    1525.2    1715.1    1932.3   ns/row
	//   hash      1622.8    1679.7    1772.2    1986.0   ns/row   (+10.6 / +10.1 / +3.3 / +2.8 %)
	//
	// Slower at every point. `GetValueHash` on each key plus the bucket array and its rehashes cost
	// more than fourteen `CompareValueLS` at n=16000 — those comparisons are cheap since the
	// tag-equality fast path landed (value.cpp, §9). The chain road's containers are not being
	// judged here: they build ONE index and probe it per outer row, which is a different shape.
	//
	// ⚠ AND THE RISE WITH n IS NOT THE INDEX. Both containers rise the same ~30% from n=250 to
	// 16000, so it is not tree depth and not hashing — it is what §8 predicted for scale: more live
	// ibValue, more allocations, worse locality. A `a + b·log₂(n)` fit matched the series and named
	// the wrong cause; the fit was reading warm-up, which is why the same bench run ALONE gives a
	// different curve than run after its neighbours.
	void KeepInBucket(const ibValue& key, const ibValue& row) {
		const auto found = m_buckets.find(key);
		if (found == m_buckets.end()) {
			m_bucketOrder.push_back(key);
			m_buckets.emplace(key, std::vector<ibValue>{ row });
			return;
		}
		found->second.push_back(row);
	}
	const std::vector<ibValue>* Bucket(const ibValue& key) const {
		const auto found = m_buckets.find(key);
		return found == m_buckets.end() ? nullptr : &found->second;
	}
	const std::vector<ibValue>& BucketOrder() const { return m_bucketOrder; }

	// Rows into key order. A STABLE sort over an INDEX: equal keys keep the order they arrived in,
	// and each row moves once instead of being swapped through every comparison.
	// ⭐ REVERSAL IS AN ORDERING WITHOUT KEYS — the rows already carry the order they arrived in, so
	// turning it round is the whole operation. It is here rather than in a state class of its own
	// for the same reason everything else is: one collection, one place the rows live.
	void ReverseRows() { std::reverse(m_rows.begin(), m_rows.end()); }

	void SortByKeys(bool descending) {
		if (!HasKeys())
			return;                                   // nothing to pair them by; leave row order
		std::vector<size_t> order(m_rows.size());
		for (size_t i = 0; i < order.size(); ++i) order[i] = i;
		// Lexicographic over the row's keys — the first that differs decides, exactly as the clauses
		// were written. A row with fewer keys than another (a clause that produced nothing) sorts
		// before it, which keeps the order total and the sort valid.
		// 🛑 ONE THREE-WAY ANSWER PER KEY, NOT TWO BOOLEANS. `a < b` is `CompareValueLS(b) < 0` — a
		// full comparison — so asking `l < r` and then `r < l` runs the whole thing TWICE, and it
		// does so exactly when the keys are EQUAL, which is the only case a second key exists for.
		//
		// This is the same waste the engine already removed one level down: value.cpp's comparison
		// says outright that `a < b ? -1 : (b < a ? 1 : 0)` "runs the comparison twice to learn what
		// one call returns", and was rewritten to ask each type for one three-way answer. The
		// three-way answer is right here; taking it back through `operator<` threw it away again.
		const auto before = [](const std::vector<ibValue>& l, const std::vector<ibValue>& r) {
			const size_t n = std::min(l.size(), r.size());
			for (size_t i = 0; i < n; ++i) {
				const int decided = l[i].CompareValueLS(r[i]);
				if (decided != 0)
					return decided < 0;
			}
			return l.size() < r.size();
		};

		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			return descending ? before(m_keys[b], m_keys[a]) : before(m_keys[a], m_keys[b]);
		});
		std::vector<ibValue> sorted;
		sorted.reserve(m_rows.size());
		for (const size_t i : order) sorted.push_back(m_rows[i]);
		m_rows.swap(sorted);
	}

	// ⭐⭐ ITERATED IN PLACE. `foreach` over what a pipeline kept builds nothing per row: the state
	// walks the vector the collection already holds, and holds the collection while it does.
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override {
		class RowWalk : public ibValueIteratorState {
		public:
			RowWalk(ibValueLinqRows* owner, const std::vector<ibValue>& rows)
				: m_owner(owner), m_rows(rows) { if (m_owner) m_owner->IncrRef(); }
			~RowWalk() override { if (m_owner) m_owner->DecrRef(); }
			bool MoveNext(ibValue& current) override {
				if (m_pos >= m_rows.size()) return false;
				current = m_rows[m_pos++];
				return true;
			}
			void Reset() override { m_pos = 0; }
			// A ROW IS THE SAMPLE OF A ROW. Every row of one query is made the same way, so the
			// first one describes all of them — which is what a reader standing after the dot of a
			// `foreach` variable is asking for. Nothing is consumed: the cursor does not move.
			bool PeekSample(ibValue& current) const override {
				if (m_rows.empty()) return false;
				current = m_rows[0];
				return true;
			}
		private:
			ibValueLinqRows*            m_owner;
			const std::vector<ibValue>& m_rows;
			size_t                      m_pos = 0;
		};
		return std::make_shared<RowWalk>(this, Rows());
	}

	const std::vector<ibValue>& Rows() const { return m_view != nullptr ? *m_view : m_rows; }

private:
	// A VIEW borrows: it has no rows of its own and reads the owner's.
	ibValueLinqRows*             m_owner = nullptr;
	const std::vector<ibValue>*  m_view  = nullptr;

	std::vector<ibValue> m_rows;

	// One entry per kept row, holding that row's ordering keys in clause order — see KeepKey. It
	// was one value per row until 2026-09-08, which is why `orderby a, b` had no way to compile.
	std::vector<std::vector<ibValue>> m_keys;
	// THE ONE KEY POLICY, taken from where it is written (ibValueHash / ibValueEqual, value.h):
	// *"grouping, joining and de-duplicating all need the same pair … every index takes them from
	// here."* These two are indexes by value, so they take them from there.
	std::set<ibValue, std::less<ibValue>>                       m_seen;
	std::map<ibValue, std::vector<ibValue>, std::less<ibValue>> m_buckets;
	std::vector<ibValue>                                        m_bucketOrder;
};
// GroupBy node — bucket upstream by key extracted via fn(elem).
// On first MoveNext drain upstream + build buckets, then emit one
// GROUP per key, in FIRST-APPEARANCE order (m_groups keeps it; the
// index below only finds a key).
class ibValueGroupByState : public ibValueIteratorState {
public:
	ibValueGroupByState(std::shared_ptr<ibValueIteratorState> upstream,
	                    ibValueFunction keySelector)
		: m_upstream(std::move(upstream))
		, m_keySelector(keySelector)
	{
	}

	bool MoveNext(ibValue& current) override {
		EnsureGrouped();
		if (m_pos >= (long)m_groups.size()) return false;

		// ⭐ A GROUP IS A GROUP — the same light one the compiled road hands out, so `g.Key` and
		// `g.Values` mean one thing in this language and are read by ORDINAL on both roads.
		//
		// What stood here built a `Structure` and inserted both members BY NAME, then filled an
		// `Array` a value at a time — three script objects and two name lookups per group, to say
		// something that has exactly two members and always the same two.
		ibValueLinqRows* const values = new ibValueLinqRows();
		for (const ibValue& one : m_groups[m_pos].second)
			values->Keep(one);
		CopyValue(current, ibValue(new ibValueLinqGroup(m_groups[m_pos].first, ibValue(values))));

		++m_pos;
		return true;
	}

	void Reset() override {
		m_pos = 0;
	}

	// ⭐⭐ WHAT THIS YIELDS IS KNOWN WITHOUT RUNNING IT, and that is the whole point of a sample. A
	// grouping answers with one GROUP per key, and a group is `Key` and `Values` whatever the rows
	// turned out to be — so an empty group is a complete answer to "what will I be looking at".
	//
	// It used to say nothing, and the cost was visible one hop later: `foreach (g in rows.GroupBy(f))`
	// left `g.` with no members to offer, on a value whose shape was never in doubt. Nothing is
	// drained to answer this — the upstream is not touched at all.
	bool PeekSample(ibValue& current) const override {
		current = ibValue(new ibValueLinqGroup());
		return true;
	}

private:
	void EnsureGrouped() {
		if (m_built) return;
		m_built = true;
		if (!m_upstream) return;
		// Hybrid: a hash index for key lookup + std::vector for
		// insertion-order preservation. Each key maps to an INDEX into
		// m_groups; this was a linear `!(a<b) && !(b<a)` scan (O(N²)),
		// then a tree (O(N log K)), and is one bucket probe per row now.
		ibValue elem;
		while (m_upstream->MoveNext(elem)) {
			ibValue key;
			CallLambdaWithArg(m_keySelector, elem, key);
			auto it = m_keyIdx.find(key);
			if (it != m_keyIdx.end()) {
				m_groups[it->second].second.push_back(elem);
			} else {
				const size_t idx = m_groups.size();
				m_groups.emplace_back(key, std::vector<ibValue>{ elem });
				m_keyIdx.emplace(key, idx);
			}
		}
	}

	std::shared_ptr<ibValueIteratorState>                            m_upstream;
	ibValueFunction                                                   m_keySelector;
	bool                                                              m_built = false;
	std::vector<std::pair<ibValue, std::vector<ibValue>>>             m_groups;
	std::unordered_map<ibValue, size_t, ibValueHash, ibValueEqual>   m_keyIdx;
	long                                                              m_pos   = 0;
};

// Join node — inner equi-join. Build hash map from `inner` keyed
// by rightKey(fn); for each outer row, look up by leftKey(fn),
// project matched pairs via projection(outer, inner) -> row.
// Iterator emits ZERO results when a left row has no match
// (unmatched-left dropped — inner join semantics). Multi-match
// supported via stateful inner-bucket cursor across MoveNext.
class ibValueJoinState : public ibValueIteratorState {
public:
	ibValueJoinState(std::shared_ptr<ibValueIteratorState> outerUp,
	                 ibValue                                  innerSrc,
	                 ibValueFunction                          leftKey,
	                 ibValueFunction                          rightKey,
	                 ibValueFunction                          projection)
		: m_outerUp(std::move(outerUp))
		, m_innerSrc(innerSrc)
		, m_leftKey(leftKey)
		, m_rightKey(rightKey)
		, m_projection(projection)
	{
	}

	bool MoveNext(ibValue& current) override {
		EnsureIndexed();
		if (!m_outerUp) return false;
		// Hash-bucketed lookup: per outer row resolve leftKey ONCE,
		// then one bucket probe — O(1) expected, see the note on
		// KeyHash below. Stateful inner-bucket cursor across MoveNext calls
		// for multi-match (one outer matches K inner rows -> K
		// result rows). Reset m_curBucket = nullptr at start of new
		// outer iteration; current keeps incrementing m_bucketIdx
		// until bucket exhausted, then advance outer.
		for (;;) {
			if (!m_haveOuter) {
				if (!m_outerUp->MoveNext(m_curOuter)) return false;
				ibValue leftK;
				CallLambdaWithArg(m_leftKey, m_curOuter, leftK);
				auto it = m_hash.find(leftK);
				m_curBucket = (it != m_hash.end()) ? &it->second : nullptr;
				m_bucketIdx = 0;
				m_haveOuter = true;
			}
			if (m_curBucket != nullptr && m_bucketIdx < (long)m_curBucket->size()) {
				// Local copy of inner row — bucket holds const ibValue
				// (map stores by-value); projection lambda binds args
				// by non-const ref. Copy isolates m_inner from any
				// in-lambda mutation as a side-effect (the join row
				// is supposed to be a freshly projected value).
				ibValue innerRow = (*m_curBucket)[m_bucketIdx];
				CallLambdaWith2Args(m_projection, m_curOuter, innerRow, current);
				++m_bucketIdx;
				return true;
			}
			// Bucket exhausted (or no match for this outer) — advance.
			m_haveOuter = false;
		}
	}

	void Reset() override {
		if (m_outerUp) m_outerUp->Reset();
		m_haveOuter = false;
		m_curBucket = nullptr;
		m_bucketIdx = 0;
		// Keep m_hash — index built from a stable iterable.
	}

	bool PeekSample(ibValue& /*current*/) const override { return false; }

private:
	void EnsureIndexed() {
		if (m_built) return;
		m_built = true;
		// Materialise inner once into a hash. Each key maps to a
		// vector of matching inner rows (multi-match support).
		std::shared_ptr<ibValueIteratorState> innerIt = m_innerSrc.CreateIterator();
		if (!innerIt) return;
		ibValue elem;
		while (innerIt->MoveNext(elem)) {
			ibValue rightK;
			CallLambdaWithArg(m_rightKey, elem, rightK);
			m_hash[rightK].push_back(elem);
		}
	}

	std::shared_ptr<ibValueIteratorState> m_outerUp;
	ibValue                                m_innerSrc;
	ibValueFunction                        m_leftKey;
	ibValueFunction                        m_rightKey;
	ibValueFunction                        m_projection;
	bool                                   m_built = false;
	// One bucket probe per outer row — see the key-policy note above, and
	// ibValueHash / ibValueEqual in value.h, for why this is not a tree any more.
	std::unordered_map<ibValue, std::vector<ibValue>, ibValueHash, ibValueEqual> m_hash;
	// Cross-MoveNext state — bucket cursor for multi-match.
	bool                                   m_haveOuter = false;
	ibValue                                m_curOuter;
	const std::vector<ibValue>*            m_curBucket = nullptr;
	long                                   m_bucketIdx = 0;
};

// =================================================================
// Pipeline limit / skip / reverse — small stateful states.
// =================================================================

class ibValueSkipState : public ibValueIteratorState {
public:
	ibValueSkipState(std::shared_ptr<ibValueIteratorState> upstream, long n)
		: m_upstream(std::move(upstream)), m_n(n) {}
	bool MoveNext(ibValue& current) override {
		if (!m_upstream) return false;
		while (m_skipped < m_n) {
			ibValue dump;
			if (!m_upstream->MoveNext(dump)) return false;
			++m_skipped;
		}
		return m_upstream->MoveNext(current);
	}
	void Reset() override { if (m_upstream) m_upstream->Reset(); m_skipped = 0; }
	bool PeekSample(ibValue& c) const override { return m_upstream ? m_upstream->PeekSample(c) : false; }
private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	long m_n = 0;
	long m_skipped = 0;
};

class ibValueTakeState : public ibValueIteratorState {
public:
	ibValueTakeState(std::shared_ptr<ibValueIteratorState> upstream, long n)
		: m_upstream(std::move(upstream)), m_n(n) {}
	bool MoveNext(ibValue& current) override {
		if (!m_upstream || m_taken >= m_n) return false;
		if (!m_upstream->MoveNext(current)) return false;
		++m_taken;
		return true;
	}
	void Reset() override { if (m_upstream) m_upstream->Reset(); m_taken = 0; }
	bool PeekSample(ibValue& c) const override { return m_upstream ? m_upstream->PeekSample(c) : false; }
private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	long m_n = 0;
	long m_taken = 0;
};

class ibValueSkipWhileState : public ibValueIteratorState {
public:
	ibValueSkipWhileState(std::shared_ptr<ibValueIteratorState> upstream, ibValueFunction pred)
		: m_upstream(std::move(upstream)), m_pred(pred) {}
	bool MoveNext(ibValue& current) override {
		if (!m_upstream) return false;
		while (!m_passed) {
			if (!m_upstream->MoveNext(current)) return false;
			ibValue r;
			{ ScopedThreeValuedNull tv; CallLambdaWithArg(m_pred, current, r); }
			if (!IsHasValue(r)) { m_passed = true; return true; }
		}
		return m_upstream->MoveNext(current);
	}
	void Reset() override { if (m_upstream) m_upstream->Reset(); m_passed = false; }
	bool PeekSample(ibValue& c) const override { return m_upstream ? m_upstream->PeekSample(c) : false; }
private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	ibValueFunction m_pred;
	bool m_passed = false;
};

class ibValueTakeWhileState : public ibValueIteratorState {
public:
	ibValueTakeWhileState(std::shared_ptr<ibValueIteratorState> upstream, ibValueFunction pred)
		: m_upstream(std::move(upstream)), m_pred(pred) {}
	bool MoveNext(ibValue& current) override {
		if (!m_upstream || m_done) return false;
		if (!m_upstream->MoveNext(current)) return false;
		ibValue r;
		{ ScopedThreeValuedNull tv; CallLambdaWithArg(m_pred, current, r); }
		if (!IsHasValue(r)) { m_done = true; return false; }
		return true;
	}
	void Reset() override { if (m_upstream) m_upstream->Reset(); m_done = false; }
	bool PeekSample(ibValue& c) const override { return m_upstream ? m_upstream->PeekSample(c) : false; }
private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	ibValueFunction m_pred;
	bool m_done = false;
};

class ibValueReverseState : public ibValueIteratorState {
public:
	explicit ibValueReverseState(std::shared_ptr<ibValueIteratorState> upstream)
		: m_upstream(std::move(upstream)) {}
	bool MoveNext(ibValue& current) override {
		EnsureBuilt();
		if (m_pos <= 0) return false;
		current = m_buf[--m_pos];
		return true;
	}
	void Reset() override { m_pos = (long)m_buf.size(); }
	bool PeekSample(ibValue& c) const override { return m_upstream ? m_upstream->PeekSample(c) : false; }
private:
	void EnsureBuilt() {
		if (m_built) return;
		m_built = true;
		if (!m_upstream) return;
		ibValue v;
		while (m_upstream->MoveNext(v)) m_buf.push_back(v);
		m_pos = (long)m_buf.size();
	}
	std::shared_ptr<ibValueIteratorState> m_upstream;
	bool m_built = false;
	std::vector<ibValue> m_buf;
	long m_pos = 0;
};

// =================================================================
// Set operators — Concat / Union / Intersect / Except.
// =================================================================

class ibValueConcatState : public ibValueIteratorState {
public:
	ibValueConcatState(std::shared_ptr<ibValueIteratorState> a,
	                   std::shared_ptr<ibValueIteratorState> b)
		: m_a(std::move(a)), m_b(std::move(b)) {}
	bool MoveNext(ibValue& current) override {
		if (!m_onB && m_a) {
			if (m_a->MoveNext(current)) return true;
			m_onB = true;
		}
		return m_b ? m_b->MoveNext(current) : false;
	}
	void Reset() override {
		if (m_a) m_a->Reset();
		if (m_b) m_b->Reset();
		m_onB = false;
	}
	bool PeekSample(ibValue& c) const override { return m_a ? m_a->PeekSample(c) : (m_b ? m_b->PeekSample(c) : false); }
private:
	std::shared_ptr<ibValueIteratorState> m_a, m_b;
	bool m_onB = false;
};

// Hybrid set comparator — std::set with ibValue::operator< gives
// strict weak ordering for primitives. Same Cmp pattern as Join/GroupBy.
struct ibValueSetCmp { bool operator()(const ibValue& a, const ibValue& b) const { return a < b; } };

// Intersect / Except share the same structure: materialise `other` into
// a std::set on first MoveNext, then per-upstream-row check membership
// + emit-once dedup. Only the predicate (present vs absent in other)
// differs. Templated on a "should emit" tester so the compiler inlines
// the per-row check into the same shape both subclasses would have had.
template <bool kKeepIfInOther>
class ibValueSetOpState : public ibValueIteratorState {
public:
	ibValueSetOpState(std::shared_ptr<ibValueIteratorState> a, ibValue other)
		: m_a(std::move(a)), m_other(other) {}
	bool MoveNext(ibValue& current) override {
		EnsureBuilt();
		if (!m_a) return false;
		while (m_a->MoveNext(current)) {
			const bool inOther = (m_otherSet.count(current) > 0);
			if (inOther == kKeepIfInOther && m_emitted.insert(current).second)
				return true;
		}
		return false;
	}
	void Reset() override { if (m_a) m_a->Reset(); m_emitted.clear(); }
	bool PeekSample(ibValue& c) const override { return m_a ? m_a->PeekSample(c) : false; }
private:
	void EnsureBuilt() {
		if (m_built) return;
		m_built = true;
		auto it = m_other.CreateIterator();
		if (!it) return;
		ibValue v;
		while (it->MoveNext(v)) m_otherSet.insert(v);
	}
	std::shared_ptr<ibValueIteratorState> m_a;
	ibValue m_other;
	bool m_built = false;
	std::set<ibValue, ibValueSetCmp> m_otherSet;
	std::set<ibValue, ibValueSetCmp> m_emitted;
};

using ibValueIntersectState = ibValueSetOpState<true>;   // keep if in `other`
using ibValueExceptState    = ibValueSetOpState<false>;  // keep if NOT in `other`

// =================================================================
// Indexed Where / Select — lambda takes (element, index).
// =================================================================

class ibValueWhereIndexedState : public ibValueIteratorState {
public:
	ibValueWhereIndexedState(std::shared_ptr<ibValueIteratorState> up, ibValueFunction pred)
		: m_upstream(std::move(up)), m_pred(pred) {}
	bool MoveNext(ibValue& current) override {
		if (!m_upstream) return false;
		while (m_upstream->MoveNext(current)) {
			ibValue idx;
			SetTypeNumber(idx, m_idx);
			++m_idx;
			ibValue r;
			{ ScopedThreeValuedNull tv; CallLambdaWith2Args(m_pred, current, idx, r); }
			if (IsHasValue(r)) return true;
		}
		return false;
	}
	void Reset() override { if (m_upstream) m_upstream->Reset(); m_idx = 0; }
	bool PeekSample(ibValue& c) const override { return m_upstream ? m_upstream->PeekSample(c) : false; }
private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	ibValueFunction m_pred;
	long m_idx = 0;
};

class ibValueSelectIndexedState : public ibValueIteratorState {
public:
	ibValueSelectIndexedState(std::shared_ptr<ibValueIteratorState> up, ibValueFunction proj)
		: m_upstream(std::move(up)), m_proj(proj) {}
	bool MoveNext(ibValue& current) override {
		if (!m_upstream) return false;
		ibValue src;
		if (!m_upstream->MoveNext(src)) return false;
		ibValue idx;
		SetTypeNumber(idx, m_idx);
		++m_idx;
		CallLambdaWith2Args(m_proj, src, idx, current);
		return true;
	}
	void Reset() override { if (m_upstream) m_upstream->Reset(); m_idx = 0; }
	bool PeekSample(ibValue&) const override { return false; }
private:
	std::shared_ptr<ibValueIteratorState> m_upstream;
	ibValueFunction m_proj;
	long m_idx = 0;
};

// Query value — wraps the tail of a LINQ pipeline. Iterable
// itself, so chains compose naturally: q.Where(p).Select(s)
// yields an ibValueQuery whose state is SelectState wrapping
// WhereState wrapping the original source's state.
//
// Single-pass for now: CreateIterator returns m_state directly
// (after Reset). Multi-consumer scenarios (q iterated twice in
// sequence) work because Reset rewinds the chain. Forking
// (q2 = q.Where(...); iterate both q and q2 interleaved) shares
// upstream state and will misbehave — deferred to Phase 2.
// ⭐⭐ AND IT NAMES WHAT MAY BE WRITTEN NEXT. `ibValueQuery` was a bare ibValue, so the tail of every
// chain answered with NOTHING after the dot: `a.Where(…).` offered no `Select`, no `Count`, no
// `ToArray` — the one shape in which LINQ is actually written was the one shape the editor and the
// completion tool could not help with. The table of ops has existed all along and says in its own
// comment that it drives name resolution AND IntelliSense; it simply had no reader on this side.
//
// The surface is the same for every query — a pipeline tail is a pipeline tail — so it is a SHARED
// helper built once (ibValueStaticMembers), not a table per value. It is declared in
// system/value/valueQueryable.h because the DB-backed carrier answers with the SAME list: one
// binder, both ends of the chain.
class ibValueQuery : public ibValueStaticMembers<&ibBindLinqMethods> {
	public:
	ibValueQuery() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	explicit ibValueQuery(std::shared_ptr<ibValueIteratorState> state)
		: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
		, m_state(std::move(state))
	{
	}

	virtual ~ibValueQuery() = default;

	std::shared_ptr<ibValueIteratorState> CreateIterator() override {
		if (m_state) m_state->Reset();
		return m_state;
	}

private:
	std::shared_ptr<ibValueIteratorState> m_state;
};


constexpr ibClassID g_valueQuery = system_to_clsid("VL_QRY");

SYSTEM_TYPE_REGISTER(ibValueQuery, "LinqQuery", g_valueQuery);

namespace {

// ⭐ ONE TABLE-BUILDER, TWO CALLERS — declared here and defined below, beside the query's own
// answer. `ToTable` on a collection and the last instruction of a compiled query ask the same three
// questions of the same rows, and a second builder would be a second set of column names.
struct ibRowColumns {
	std::vector<wxString> m_names;
	bool m_rowIsTheCell = false;   // nothing to take apart: the row goes into the single column
};

std::vector<wxString> ColumnsOf(const ibValueLinqRows& kept);
ibRowColumns          ColumnsOfRowItself(const ibValueLinqRows& kept);
ibValue               TableOfRows(ibValueLinqRows& kept, const std::vector<wxString>& columns,
                                  bool rowIsTheCell = false);

} // namespace

// LINQ dispatcher — reached via the OPER_CALL_LINQ handler in Execute,
// which reads the ibLinqMethod enum id from m_param3.m_numIndex and
// calls ibValue::DispatchLinqMethod (virtual). The default impl walks
// TYPE_REFFER and delegates here.
//
// Doesn't go through ibValue::CallAsFunc — derived class overrides
// would intercept and `return false` on unknown numbers (their
// switches don't cover LINQ indices). The dedicated dispatch keeps
// the LINQ surface independent of every iterable class's method
// resolution.
//
// Param contract: args[0] (when present) is typically a Function value
// (lambda). self must yield a non-null CreateIterator(). Both validated;
// any mismatch throws ibBackendException with a readable message.
static void ibValueLinqDispatchImpl(ibValue* self, ibValue::ibLinqMethod method,
                                     ibValue& ret, ibValue** args, long n)
{
	using M = ibValue::ibLinqMethod;
	const long realNum = static_cast<long>(method);

	if (self == nullptr) {
		ibBackendQueryLinqException::Error(_("Cannot dispatch on null value"));
	}

	// Drive source through its standard iterator protocol. Any
	// ibValue with a non-null CreateIterator becomes a LINQ source
	// for free.
	std::shared_ptr<ibValueIteratorState> upstream = self->CreateIterator();
	if (!upstream) {
		// NOT a LINQ source — but the value may OWN a method of this name (a LINQ-operator name like
		// Select / Where is not reserved: e.g. Query's QueryResult.Select() opens the selection). Resolve
		// the operator's script NAME against the value's own members; if it has it, the `.Select(...)` was
		// meant as that method, not a pipeline op — dispatch there. This is the context the caller asked
		// for: a LINQ-chain Select stays LINQ, a Select on a value that defines one calls the value's.
		wxString methodName;
		for (const auto& info : ibValue::GetLinqMethodTable())
			if (info.id == method) { methodName = info.name; break; }
		const long ownNum = methodName.IsEmpty() ? -1 : self->FindMethod(methodName);
		if (ownNum >= 0) {
			if (self->HasRetVal(ownNum)) self->CallAsFunc(ownNum, ret, args, n);
			else                         self->CallAsProc(ownNum, args, n);
			return;
		}
		ibBackendQueryLinqException::Error(_("Value is not iterable"));
	}

	// Order of cases matches ibValue::ibLinqMethod enum (value.h) +
	// GetLinqMethodTable() (above; drives name resolution + IntelliSense).
	// Lock-step — adding a new op = append enum + table entry + case.
	switch (method) {
		// === Pipeline operators (require lambda arg, return ibValueQuery) ===
		case M::Where:
		case M::Select:
		case M::SelectMany:
		{
			if (n < 1 || args == nullptr || args[0] == nullptr) {
				ibBackendQueryLinqException::Error(_("Method requires a function argument"));
			}
			ibValueFunction* fn = AsFunction(args[0]);
			if (fn == nullptr) {
				ibBackendQueryLinqException::Error(
					_("Argument must be a Function or Procedure value"));
			}

			std::shared_ptr<ibValueIteratorState> pipeline;
			if (method == M::Where) {
				pipeline = std::make_shared<ibValueWhereState>(std::move(upstream), *fn);
			} else if (method == M::SelectMany) {
				// `SelectMany(collectionSelector [, resultSelector])`. The second
				// form projects the PAIR, which is what a second `from` in a query
				// block needs: both rows stay in scope after it.
				ibValueFunction* resultFn = (n > 1 && args[1] != nullptr) ? AsFunction(args[1]) : nullptr;
				pipeline = resultFn != nullptr
					? std::make_shared<ibValueSelectManyState>(std::move(upstream), *fn, *resultFn)
					: std::make_shared<ibValueSelectManyState>(std::move(upstream), *fn);
			} else {
				pipeline = std::make_shared<ibValueSelectState>(std::move(upstream), *fn);
			}
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}

		// === Terminal operators (no args, materialise / aggregate) ===
		case M::Count: // number of elements
		{
			long count = 0;
			ibValue current;
			while (upstream->MoveNext(current)) ++count;
			SetTypeNumber(ret, count);
			break;
		}
		case M::ToArray: // materialise into ibValueArray
		{
			ibValueArray* arr = new ibValueArray();
			ibValue current;
			while (upstream->MoveNext(current)) {
				arr->Add(current);
			}
			CopyValue(ret, ibValue(arr));
			break;
		}
		case M::First: // first element or Empty if exhausted
		{
			ibValue current;
			if (upstream->MoveNext(current)) {
				CopyValue(ret, current);
			} else {
				// ret stays at its caller-default (typically empty / undefined).
			}
			break;
		}
		case M::Any: // true if at least one row
		{
			ibValue current;
			const bool any = upstream->MoveNext(current);
			SetTypeBoolean(ret, any);
			break;
		}
		case M::Distinct: // lazy filter via parallel seen-vector
		{
			// Returns ibValueQuery wrapping ibValueDistinctState — same
			// pattern as Where/Select. Cleaner ownership (shared_ptr
			// throughout) than the earlier materialise-into-Array
			// experiment which had ownership concerns on raw new.
			auto pipeline = std::make_shared<ibValueDistinctState>(std::move(upstream));
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::OrderBy:           // lazy, materialise + stable_sort
		case M::OrderByDescending: // same but reverse comparator
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("OrderBy requires a key selector function"));
			ibValueFunction* keyFn = AsFunction(args[0]);
			if (keyFn == nullptr)
				ibBackendQueryLinqException::Error(_("OrderBy key selector must be a Function value"));
			const bool desc = (method == M::OrderByDescending);
			auto pipeline = std::make_shared<ibValueOrderByState>(std::move(upstream), *keyFn, desc);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::GroupBy: // lazy, materialise + ordered buckets
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("GroupBy requires a key selector function"));
			ibValueFunction* keyFn = AsFunction(args[0]);
			if (keyFn == nullptr)
				ibBackendQueryLinqException::Error(_("GroupBy key selector must be a Function value"));
			auto pipeline = std::make_shared<ibValueGroupByState>(std::move(upstream), *keyFn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Join: // Join(inner, leftKey, rightKey, projection)
		{
			// Layer 2 — RAM receiver ⋈ a DB queryable argument: wrap the receiver as a leaf
			// and run server-side through the L3 door (the composer temp-promotes the RAM
			// side), instead of the RAM hash-join below. Falls through on any other shape.
			if (ibValueQueryable::TryJoinThroughL3(*self, ret, args, n))
				break;
			if (n < 4 || args == nullptr || args[0] == nullptr || args[1] == nullptr
				|| args[2] == nullptr || args[3] == nullptr)
				ibBackendQueryLinqException::Error(
					_("Join requires (inner, leftKey, rightKey, projection)"));
			ibValueFunction* leftKeyFn  = AsFunction(args[1]);
			ibValueFunction* rightKeyFn = AsFunction(args[2]);
			ibValueFunction* projFn     = AsFunction(args[3]);
			if (leftKeyFn == nullptr || rightKeyFn == nullptr || projFn == nullptr)
				ibBackendQueryLinqException::Error(
					_("Join leftKey / rightKey / projection must be Function values"));
			auto pipeline = std::make_shared<ibValueJoinState>(
				std::move(upstream), *args[0], *leftKeyFn, *rightKeyFn, *projFn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Skip: // Skip(n)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("Skip requires a count argument"));
			const long count = (long)args[0]->GetNumber().ToInt64();
			auto pipeline = std::make_shared<ibValueSkipState>(std::move(upstream), count);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Take: // Take(n)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("Take requires a count argument"));
			const long count = (long)args[0]->GetNumber().ToInt64();
			auto pipeline = std::make_shared<ibValueTakeState>(std::move(upstream), count);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::SkipWhile: // SkipWhile(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr) ibBackendQueryLinqException::Error(_("SkipWhile requires a predicate function"));
			auto pipeline = std::make_shared<ibValueSkipWhileState>(std::move(upstream), *fn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::TakeWhile: // TakeWhile(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr) ibBackendQueryLinqException::Error(_("TakeWhile requires a predicate function"));
			auto pipeline = std::make_shared<ibValueTakeWhileState>(std::move(upstream), *fn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Reverse: // Reverse()
		{
			auto pipeline = std::make_shared<ibValueReverseState>(std::move(upstream));
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Concat: // Concat(other)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("Concat requires another iterable"));
			auto bIt = args[0]->CreateIterator();
			if (!bIt) ibBackendQueryLinqException::Error(_("Concat operand is not iterable"));
			auto pipeline = std::make_shared<ibValueConcatState>(std::move(upstream), std::move(bIt));
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Union: // concat + distinct
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("Union requires another iterable"));
			auto bIt = args[0]->CreateIterator();
			if (!bIt) ibBackendQueryLinqException::Error(_("Union operand is not iterable"));
			auto cat = std::make_shared<ibValueConcatState>(std::move(upstream), std::move(bIt));
			auto pipeline = std::make_shared<ibValueDistinctState>(std::move(cat));
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Intersect: // Intersect(other)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("Intersect requires another iterable"));
			auto pipeline = std::make_shared<ibValueIntersectState>(std::move(upstream), *args[0]);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Except: // Except(other)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("Except requires another iterable"));
			auto pipeline = std::make_shared<ibValueExceptState>(std::move(upstream), *args[0]);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Last:          // drain, return final or throw if empty
		case M::LastOrDefault: // drain, return final or empty
		{
			ibValue current;
			bool any = false;
			while (upstream->MoveNext(current)) any = true;
			if (!any && method == M::Last)
				ibBackendQueryLinqException::Error(_("Last() on empty sequence"));
			// Always write ret — empty default for OrDefault path,
			// real value for any() path. CopyValue with TYPE_EMPTY
			// source resets dst (otherwise caller's slot keeps stale value).
			CopyValue(ret, any ? current : ibValue());
			break;
		}
		case M::Single:           // exactly one; throw if 0 or >1
		case M::SingleOrDefault:  // 0 -> empty, exactly 1 -> value, >1 -> throw
		{
			ibValue current, first;
			long count = 0;
			while (upstream->MoveNext(current)) {
				if (count == 0) first = current;
				++count;
				if (count > 1) break;
			}
			if (count == 0 && method == M::Single)
				ibBackendQueryLinqException::Error(_("Single() on empty sequence"));
			if (count > 1)
				ibBackendQueryLinqException::Error(_("Single() - sequence contains more than one element"));
			CopyValue(ret, count == 1 ? first : ibValue());
			break;
		}
		case M::FirstOrDefault:
		{
			ibValue current;
			CopyValue(ret, upstream->MoveNext(current) ? current : ibValue());
			break;
		}
		case M::ElementAt:          // throw on out-of-range
		case M::ElementAtOrDefault: // return empty on OOR
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("ElementAt requires an index"));
			const long idx = (long)args[0]->GetNumber().ToInt64();
			if (idx < 0) {
				if (method == M::ElementAt)
					ibBackendQueryLinqException::Error(_("ElementAt - negative index"));
				CopyValue(ret, ibValue());
				break;
			}
			ibValue current;
			long pos = 0;
			bool found = false;
			while (upstream->MoveNext(current)) {
				if (pos == idx) { found = true; break; }
				++pos;
			}
			if (!found && method == M::ElementAt)
				ibBackendQueryLinqException::Error(_("ElementAt - index out of range"));
			CopyValue(ret, found ? current : ibValue());
			break;
		}
		case M::Contains: // Contains(value)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("Contains requires a value argument"));
			const ibValue& target = *args[0];
			ibValue current;
			bool found = false;
			while (upstream->MoveNext(current)) {
				if (current == target) { found = true; break; }
			}
			SetTypeBoolean(ret, found);
			break;
		}
		case M::SequenceEqual: // SequenceEqual(other)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendQueryLinqException::Error(_("SequenceEqual requires another iterable"));
			auto bIt = args[0]->CreateIterator();
			if (!bIt) ibBackendQueryLinqException::Error(_("SequenceEqual operand is not iterable"));
			ibValue a, b;
			bool equal = true;
			for (;;) {
				const bool ha = upstream->MoveNext(a);
				const bool hb = bIt->MoveNext(b);
				if (!ha && !hb) break;
				if (ha != hb || !(a == b)) { equal = false; break; }
			}
			SetTypeBoolean(ret, equal);
			break;
		}
		case M::Aggregate: // Aggregate(seed, reducer)
		{
			if (n < 2 || args == nullptr || args[0] == nullptr || args[1] == nullptr)
				ibBackendQueryLinqException::Error(_("Aggregate requires (seed, reducer)"));
			ibValueFunction* reducer = AsFunction(args[1]);
			if (reducer == nullptr)
				ibBackendQueryLinqException::Error(_("Aggregate reducer must be a Function value"));
			ibValue acc = *args[0];
			ibValue current;
			while (upstream->MoveNext(current)) {
				ibValue next;
				CallLambdaWith2Args(*reducer, acc, current, next);
				acc = next;
			}
			CopyValue(ret, acc);
			break;
		}
		// AGGREGATES OVER THE STREAM — one pass, nothing materialised. Each takes an
		// OPTIONAL selector, exactly as the concrete collections' own Sum/Min/Max/
		// Average do (ibValueArray::SumWithSelector and friends): `Sum()` totals the
		// elements, `Sum(fn)` totals fn(element). An EMPTY sequence answers Empty
		// rather than zero — the same answer the Array versions give, and the honest
		// one, because "nothing to add up" is not the same statement as "adds up to
		// nothing".
		case M::Sum:
		case M::Min:
		case M::Max:
		case M::Average:
		{
			ibValueFunction* selector = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (n >= 1 && args && args[0] != nullptr && selector == nullptr)
				ibBackendQueryLinqException::Error(_("The aggregate selector must be a Function value"));

			ibValue acc, current, projected;
			long   seen  = 0;

			while (upstream->MoveNext(current)) {
				ibValue* pTerm = &current;
				if (selector != nullptr) {
					CallLambdaWithArgs(*selector, &pTerm, 1, projected);
					pTerm = &projected;
				}
				if (seen == 0) {
					acc = *pTerm;
				}
				else if (method == M::Sum || method == M::Average) {
					acc = acc + *pTerm;
				}
				else if (method == M::Min) {
					if (*pTerm < acc) acc = *pTerm;
				}
				else {   // M::Max
					if (acc < *pTerm) acc = *pTerm;
				}
				seen++;
			}

			if (seen == 0)
				break;                     // ret stays empty — an empty sequence has no total

			if (method == M::Average) {
				const ibNumber count(static_cast<int64_t>(seen));
				CopyValue(ret, ibValue(acc.GetNumber() / count));
			}
			else {
				CopyValue(ret, acc);
			}
			break;
		}
		case M::WhereIndexed: // WhereIndexed(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr)
				ibBackendQueryLinqException::Error(_("WhereIndexed requires a predicate function"));
			auto pipeline = std::make_shared<ibValueWhereIndexedState>(std::move(upstream), *fn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::SelectIndexed: // SelectIndexed(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr)
				ibBackendQueryLinqException::Error(_("SelectIndexed requires a projection function"));
			auto pipeline = std::make_shared<ibValueSelectIndexedState>(std::move(upstream), *fn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}

		case M::ToTable:
		{
			// ⭐⭐ A TABLE OUT OF WHATEVER THE ROWS ARE. This used to REFUSE — *"ToTable is supported
			// on data sources (Data.*) only"* — on the grounds that a RAM iterable has no column
			// schema. It has one: the ROW does. A projected row names its fields, a group is Key and
			// Values, an object row lends its own properties, and a plain value is itself the single
			// column — the same question a query's own answer is built from (ColumnsOf /
			// ColumnsOfRowItself / TableOfRows above), asked here of a collection instead of a loop.
			//
			// A data source never reaches this: `ibValueQueryable` overrides the dispatch and builds
			// the table from the SCHEMA, typed and read server-side, which is the better answer and
			// the reason the compiler leaves `ToTable` to this road rather than folding it into a
			// loop (compileCode.cpp, the terminals the door answers by itself).
			ibValueLinqRows rows;
			ibValue current;
			while (upstream->MoveNext(current))
				rows.Keep(current);

			const std::vector<wxString> named = ColumnsOf(rows);
			if (!named.empty()) {
				CopyValue(ret, TableOfRows(rows, named));
				break;
			}
			const ibRowColumns own = ColumnsOfRowItself(rows);
			CopyValue(ret, TableOfRows(rows, own.m_names, own.m_rowIsTheCell));
			break;
		}

		default:
			ibBackendQueryLinqException::Error(
				_("Unknown method index %ld"), realNum);
	}
}

// Virtual entry point — out-of-line definition for `ibValue::DispatchLinqMethod`.
// Default base impl walks the TYPE_REFFER chain (so an ibValue wrapper
// around a subclass dispatches to the subclass's override via vtable)
// then delegates to the central switch above. Subclasses override
// `DispatchLinqMethod` in their own .cpp; if they call base's impl
// they get the standard CreateIterator + state-class machinery.
void ibValue::DispatchLinqMethod(ibLinqMethod method, ibValue& ret,
                                  ibValue** args, long n)
{
	if (IsReference() && m_pRef != nullptr && m_pRef != this) {
		m_pRef->DispatchLinqMethod(method, ret, args, n);
		return;
	}
	ibValueLinqDispatchImpl(this, method, ret, args, n);
}

// Single source of truth for LINQ method metadata. Order matches
// ibLinqMethod enum (the switch in ibValueLinqDispatchImpl reads enum
// values directly, but consumers that walk the table — autocomplete,
// debugger labels — don't rely on enum-value-as-index). Adding a new
// op = append here, append in enum (value.h), append a case in the
// dispatch switch above.
//
// Helper strings are English short descriptions surfaced in IntelliSense
// tooltips. Concrete enough that the user understands what the op does
// without opening docs/linq.md; one line each. Localisation is a
// future concern (would join the existing wxString runtime-message
// localisation path).
const std::vector<ibValue::ibLinqMethodInfo>& ibValue::GetLinqMethodTable() {
	using M = ibValue::ibLinqMethod;
	static const std::vector<ibLinqMethodInfo> table = {
		{ M::Where,              L"Where",              L"Filter elements by a predicate fn(elem) -> bool" },
		{ M::Select,             L"Select",             L"Project each element through fn(elem) -> newElem" },
		{ M::Count,              L"Count",              L"Return total element count (drains the sequence)" },
		{ M::ToArray,            L"ToArray",            L"Materialise the pipeline into an Array" },
		{ M::First,              L"First",              L"Return the first element; throws if empty" },
		{ M::Any,                L"Any",                L"True iff the sequence has at least one element" },
		{ M::Distinct,           L"Distinct",           L"Filter duplicates (uses ibValue equality)" },
		{ M::OrderBy,            L"OrderBy",            L"Sort ascending by fn(elem) -> key" },
		{ M::OrderByDescending,  L"OrderByDescending",  L"Sort descending by fn(elem) -> key" },
		{ M::GroupBy,            L"GroupBy",            L"Group by fn(elem) -> key; yields one group per key, with Key and Values" },
		{ M::Join,               L"Join",               L"Inner equi-join: Join(inner, leftKey, rightKey, projection)" },
		{ M::Skip,               L"Skip",               L"Skip the first n elements" },
		{ M::Take,               L"Take",               L"Take at most n elements from the front" },
		{ M::SkipWhile,          L"SkipWhile",          L"Skip leading elements while fn(elem) holds" },
		{ M::TakeWhile,          L"TakeWhile",          L"Take leading elements while fn(elem) holds" },
		{ M::Reverse,            L"Reverse",            L"Reverse the sequence (materialises upstream)" },
		{ M::Concat,             L"Concat",             L"Concatenate with another iterable" },
		{ M::Union,              L"Union",              L"Concat + Distinct" },
		{ M::Intersect,          L"Intersect",          L"Elements present in both sequences" },
		{ M::Except,             L"Except",             L"Elements present in this but not in the other" },
		{ M::Last,                L"Last",                L"Return the last element; throws if empty" },
		{ M::LastOrDefault,       L"LastOrDefault",       L"Return the last element or Empty" },
		{ M::Single,              L"Single",              L"Return the only element; throws if 0 or >1" },
		{ M::SingleOrDefault,     L"SingleOrDefault",     L"Return the only element; throws if >1" },
		{ M::FirstOrDefault,      L"FirstOrDefault",      L"Return the first element or Empty" },
		{ M::ElementAt,           L"ElementAt",           L"Return the n-th element; throws on out-of-range" },
		{ M::ElementAtOrDefault,  L"ElementAtOrDefault",  L"Return the n-th element or Empty" },
		{ M::Contains,            L"Contains",            L"True iff the sequence contains the given value" },
		{ M::SequenceEqual,       L"SequenceEqual",       L"Element-wise equality vs another iterable" },
		{ M::Aggregate,           L"Aggregate",           L"Fold via Aggregate(seed, fn(acc, elem) -> acc)" },
		{ M::WhereIndexed,        L"WhereIndexed",        L"Filter with index - fn(elem, index) -> bool" },
		{ M::SelectIndexed,       L"SelectIndexed",       L"Project with index - fn(elem, index) -> newElem" },
		{ M::ToTable,             L"ToTable",             L"Materialise a data source into a value table (Queryable)" },
		{ M::SelectMany,          L"SelectMany",          L"Flatten - fn(elem) -> a source, whose elements are yielded in turn" },
		{ M::Sum,                 L"Sum",                 L"Total of the sequence; Sum(selector?) projects each element first" },
		{ M::Min,                 L"Min",                 L"Smallest element; Min(selector?) compares the projection" },
		{ M::Max,                 L"Max",                 L"Largest element; Max(selector?) compares the projection" },
		{ M::Average,             L"Average",             L"Arithmetic mean; Average(selector?) averages the projection" },
	};
	return table;
}

// LINQ method-name -> enum resolver. Linear scan through the metadata
// table — 32 entries; the compile-side calls this once per chain-method
// emit, runtime never. Case-insensitive match per OES convention.
long ibValue::FindLinqMethodByName(const wxString& name) {
	for (const auto& info : GetLinqMethodTable()) {
		if (stringUtils::CompareString(name, info.name))
			return (long)info.id;
	}
	return -1;
}

////////////////////////////////////////////////////////////////////////////
//	What a COMPILED pipeline does to its own collection — the three verbs behind
//	OPER_LINQ_SEEN / KEEP / RESULT. See ibValueLinqRows above for why that collection is
//	not a script Array, and procUnit.h for the declarations.
////////////////////////////////////////////////////////////////////////////

namespace {

// The collection lives in a frame slot and is made there on first use — which is the whole of its
// lifetime management: it goes when the frame does, exactly like every other local.
ibValueLinqRows& LinqResultIn(ibValue& scratch)
{
	// ⚠ THIS IS THE HOTTEST OF THE FIVE. Every KEEP, every bucket write and every bucket probe comes
	// through here, so it runs at least once per row and, with several ordering keys, once per key.
	ibValueLinqRows* held = LinqCast<ibValueLinqRows>(scratch.GetRef(), g_valueLinqRows);
	if (held == nullptr) {
		held = new ibValueLinqRows();
		scratch = held;
	}
	return *held;
}

// WHAT COLUMNS THESE ROWS HAVE — asked of the FIRST row, because every row of one query is made the
// same way. Empty when the rows are plain values, which is the one case that has no columns to name.
//
// The question itself lives one level down (`ibLinqNamedColumns`, below) because the EDITOR asks it
// too, of a sample row rather than a real one — see the note on the exported form.
std::vector<wxString> ColumnsOf(const ibValueLinqRows& kept)
{
	if (kept.Count() == 0)
		return {};

	std::vector<wxString> names;
	ibLinqNamedColumns(kept.At(0), names);
	return names;
}

// ⭐⭐ …AND WHEN NOTHING NAMED THEM, THE ROW STILL CAN — with the SECOND HALF OF THE ANSWER beside
// it, because that half decides how a cell is then read.
//
// A catalog element, a value-table row, anything with a surface lists its own properties and its
// cells are read off them by ordinal. A plain number has no surface: it is ITSELF the one column,
// and reading property 0 of it answers with nothing. Those are two different reads, and which one
// applies is decided HERE, once, where the columns are decided — asking the row again at the read
// would be deciding it twice, and two decisions about one fact drift. (`ibRowColumns` is declared
// above the dispatcher, which is this builder's other caller.)
//
// 🛑 NOTHING IS FILTERED OUT OF THE NAMES, and that is not laziness. The cells are read BY ORDINAL —
// `GetPropVal(i)` against `m_names[i]` — so dropping a name without dropping the same ordinal from
// the read would slide every column after it onto the wrong value.
ibRowColumns ColumnsOfRowItself(const ibValueLinqRows& kept)
{
	ibRowColumns answer;

	if (kept.Count() == 0)
		return answer;

	ibValue* const first = kept.At(0).GetRef();
	if (first == nullptr)
		return answer;

	const long count = first->GetNProps();
	for (long i = 0; i < count; ++i)
		answer.m_names.push_back(first->GetPropName(i));

	if (answer.m_names.empty()) {
		// `Value` is what this house calls the one column of a table it had to invent a name for.
		answer.m_names.push_back(wxT("Value"));
		answer.m_rowIsTheCell = true;
	}
	return answer;
}

// ⭐⭐ THE ANSWER OF A PROJECTION IS A TABLE — the columns the select named, filled with the rows it
// produced.
//
// A `select { Country = …, Total = … }` NAMES COLUMNS. The thing in this house that HAS columns is
// the value table, so that is what the query answers with: a person can show it in a grid, hand it
// to a report, keep working with it — and feed it straight back into another query, because a table
// is iterable and therefore a LINQ source. An Array of anonymous rows was none of that.
//
// Division of labour: the light record is the row WHILE the query works — it is what `distinct`
// compares, what `orderby` sorts and what a bucket holds — and the table is the ANSWER, built once,
// here, at the same single place every other shape of query becomes a value the language holds.
//
// No name is used to build it: the columns come from the shape and every cell is written by the
// column's ID.
ibValue TableOfRows(ibValueLinqRows& kept, const std::vector<wxString>& columns,
	bool rowIsTheCell)
{
	ibValueModelTable* const table = new ibValueModelTable();
	auto* const cols = table->GetColumnCollection();
	if (cols == nullptr)
		return ibValue(table);

	std::vector<unsigned int> columnIds;
	columnIds.reserve(columns.size());
	for (const wxString& name : columns) {
		// ⚠ AN UNDECLARED COLUMN IS A STRING COLUMN — the value table says so itself (valueTable.cpp,
		// enAddColumn), and everything then compares as text: `5` sorts after `100`. A projected
		// column holds whatever its expression produced, so it must DECLARE that: an empty type
		// description admits anything, because AdjustValue hands the value back untouched when the
		// description says nothing (valueType.cpp).
		auto* const col = cols->AddColumn(name, ibTypeDescription(), name);
		columnIds.push_back(col != nullptr ? col->GetColumnID() : 0);
	}

	// 🛑 THE ROWS GO IN WITHOUT TELLING ANYBODY, and that is the difference between an answer and a
	// standstill. `AppendRow` is the door a PERSON adds a row through: it fills the new row from the
	// filter in force, asks the composer about groups, and NOTIFIES the model — and the notify makes
	// the view's order stale, which is recomputed over every row there is. Once per row that is
	// O(n²): measured on this base, a 50 000-row answer took SEVENTY SECONDS to hand back.
	//
	// Nothing here is a person adding a row. The table is being BUILT, nobody is watching it yet,
	// there is no filter and no grouping to obey, and every cell is written explicitly — so the row
	// is made and put in, and the notify is not sent. Same door (the storage's own Append), one
	// argument different.
	for (const ibValue& row : kept.Rows()) {
		ibValue* const source = row.GetRef();
		if (source == nullptr)
			continue;

		ibComposerNode* const node = new ibComposerNode();

		// WHICH READ THIS IS WAS DECIDED WHERE THE COLUMNS WERE — see ibRowColumns. A row with a
		// surface hands over its properties by ordinal; a row that IS the value goes in whole.
		if (rowIsTheCell) {
			node->AppendTableValue(columnIds[0], row);
		}
		else {
			for (size_t i = 0; i < columns.size(); ++i) {
				ibValue cell;
				source->GetPropVal((long)i, cell);             // by ordinal on both sides
				node->AppendTableValue(columnIds[i], cell);    // absent reads land as an empty cell
			}
		}
		table->Append(node, /*notify*/ false);
	}
	return ibValue(table);
}

} // namespace

bool ibLinqSeen(ibValue& scratch, const ibValue& value)
{
	return LinqResultIn(scratch).FirstTime(value);
}

// ⭐⭐ ONE INSTRUCTION PER ORDERING KEY, and only the FIRST carries the row. `orderby a, b` emits a
// KEEP that adds the row with key 0, then one KEEP per further key which adds nothing but the key —
// so several keys reach a row without a second opcode and without widening the one there is
// (compileCodeLINQ.cpp emits them; the fourth operand carries the position).
void ibLinqKeep(ibValue& scratch, const ibValue* row, const ibValue* key, long keyAt)
{
	ibValueLinqRows& kept = LinqResultIn(scratch);
	if (row != nullptr)
		kept.Keep(*row);
	if (key != nullptr)
		kept.KeepKey(*key, keyAt);
}

void ibLinqResult(ibValue& out, ibValue& scratch, int ordering, bool wantFirst)
{
	ibValueLinqRows& kept = LinqResultIn(scratch);
	// 1 descending by key · 2 ascending · 3 simply reversed (no keys involved)
	if (ordering == 3)      kept.ReverseRows();
	else if (ordering != 0) kept.SortByKeys(ordering == 1);

	if (wantFirst) {
		// An empty source answers with an empty value, which is what `First` over nothing IS — not
		// an error, and not a reason for the caller to have emitted a guard.
		if (kept.Count() > 0)
			CopyValue(out, kept.At(0));
		return;
	}

	// ⭐ THE ONE PLACE LINQ'S OWN COLLECTION BECOMES SOMETHING THE LANGUAGE HOLDS. Built once, at the
	// end, with the size known — not grown a row at a time through a script method call.
	//
	// ⭐⭐ AND WHAT IT BECOMES IS A TABLE — always, when the rows have columns, which after the
	// compiler's projection they always do: a `select` names columns whichever way it is written
	// (compileCode.cpp), a `group` is Key and Values, and one exit for every query is the point.
	//
	// The Array below is for rows that are plain values with nothing to call a column — a chain
	// asked to end in `ToArray`, which says in its own name what it wants back.
	const std::vector<wxString> columns = ColumnsOf(kept);
	if (!columns.empty()) {
		out = TableOfRows(kept, columns);
		return;
	}

	ibValueArray* const array = new ibValueArray();
	for (const ibValue& row : kept.Rows())
		array->Add(row);
	out = array;
}

void ibLinqBucket(ibValue& scratch, const ibValue& key, const ibValue& row)
{
	LinqResultIn(scratch).KeepInBucket(key, row);
}

// ⭐⭐ A VIEW, NOT A COPY. This is a join's per-row lookup: building an Array here would construct
// one object and copy every matched row FOR EVERY OUTER ROW, which is exactly the cost the whole
// arrangement exists to avoid. The value handed back points at the bucket and keeps the collection
// alive while it is read.
void ibLinqBucketGet(ibValue& out, ibValue& scratch, const ibValue& key)
{
	ibValueLinqRows& kept = LinqResultIn(scratch);
	const std::vector<ibValue>* const bucket = kept.Bucket(key);
	if (bucket == nullptr) { out = ibValue(); return; }   // no match is an ordinary answer
	out = new ibValueLinqRows(&kept, bucket);
}

// The groups come back as a LIGHT COLLECTION, like everything else here — a `group … into g` loop
// iterates it, and a terminal `group` hands it to the one instruction that ends every query, which
// is where it becomes an Array. There is exactly one place LINQ's own collection turns into a value
// the language holds, and this is not it.
//
// The groups themselves stay light either way: a group's `Values` is a VIEW of its bucket, and it
// answers Count / Where / anything else through CreateIterator — nothing is copied to make it
// usable.
void ibLinqGroups(ibValue& out, ibValue& scratch)
{
	ibValueLinqRows& kept = LinqResultIn(scratch);
	ibValueLinqRows* const groups = new ibValueLinqRows();
	// In FIRST-APPEARANCE order — the order the rows arrived in, not key order, which would be a
	// different answer and one nobody asked for. Each group's rows are a VIEW of the bucket.
	for (const ibValue& key : kept.BucketOrder()) {
		const std::vector<ibValue>* const bucket = kept.Bucket(key);
		if (bucket == nullptr) continue;
		groups->Keep(ibValue(new ibValueLinqGroup(key, ibValue(new ibValueLinqRows(&kept, bucket)))));
	}
	out = groups;
}

// ⭐⭐ THE PROJECTION. `select { a = …, b = … }` makes ONE of these per row, and it is the last place
// the compiled road went through the object factory by name.
//
// The shape is made in `shapeSlot` by the first row and read by every row after it — a frame slot is
// how a thing lives exactly as long as its loop does, and the rows that outlive the loop hold the
// shape themselves. So the names are turned into positions once per QUERY; per row there is one
// allocation of exactly the right size, and then stores at known indexes.
void ibLinqRow(ibValue& out, ibValue& shapeSlot, const wxString& names, long count)
{
	ibValueLinqShape* shape = LinqCast<ibValueLinqShape>(shapeSlot.GetRef(), g_valueLinqShape);
	if (shape == nullptr) {
		shape = new ibValueLinqShape(names);
		shapeSlot = shape;
	}
	out = new ibValueLinqRecord(shape, count);
}

void ibLinqField(ibValue& row, const ibValue& value, long ordinal)
{
	ibValueLinqRecord* const record = LinqCast<ibValueLinqRecord>(row.GetRef(), g_valueLinqRecord);

	// 🛑 THIS CAST CANNOT FAIL, AND THAT IS WHY THE FAILURE HAS TO SPEAK. `OPER_LINQ_ROW` and every
	// `OPER_LINQ_FIELD` after it are emitted together, into the SAME cell, by one lambda
	// (compileCodeLINQ.cpp) — so a row that is not a record means the tape is not the tape the
	// compiler wrote. Silently skipping the store would lose a COLUMN of the answer and leave no
	// trace of it: the query would come back with a field quietly empty, which is the worst shape a
	// defect can take. A raise says which ordinal, and stops.
	if (record == nullptr)
		ibBackendQueryLinqException::Error(
			_("The projected row is missing where field %d should be stored"), (int)ordinal);

	record->SetField(ordinal, value);
}

void ibLinqGroupedSample(ibValue& out)
{
	ibValueLinqRows* const rows = new ibValueLinqRows();
	rows->Keep(ibValue(new ibValueLinqGroup()));
	out = rows;
}

bool ibLinqNamedColumns(const ibValue& row, std::vector<wxString>& outNames)
{
	outNames.clear();

	ibValue* const held = row.GetRef();

	// A projected row carries the shape the `select` named.
	if (const ibValueLinqRecord* const record = LinqCast<ibValueLinqRecord>(held, g_valueLinqRecord)) {
		const ibValueLinqShape* const shape = record->Shape();
		for (long i = 0; shape != nullptr && i < shape->Count(); ++i)
			outNames.push_back(shape->NameAt(i));
		return true;
	}

	// A group is two columns and names them itself — the same two ordinals its members answer by.
	if (LinqCast<ibValueLinqGroup>(held, g_valueLinqGroup) != nullptr) {
		outNames.push_back(wxT("Key"));
		outNames.push_back(wxT("Values"));
		return true;
	}

	return false;
}

// ⭐ REGISTERED, AND IT HAS TO BE — being unreachable from script is not the same as being unknown
// to the engine. This value lives in a FRAME SLOT, and frame slots are enumerated: the debugger
// lists locals, a watch renders them, `TypeOf` can be asked. Every one of those asks the value what
// type it is, and a type with no registration has no answer — `GetNameObjectFromID` raises on an id
// nobody registered. So it is registered like its neighbours; what keeps it out of the language is
// that nothing NAMES it and no ctor is published, not that the engine has never heard of it.
// 🛑 …AND THE GROUP WAS THE ONE LEFT OUT, which cost an assert the day something finally asked it.
// Three of the four were registered when this note was written; a group was not, because at that
// point it never left the RUNTIME — it lived inside a grouped answer and nothing ever asked it what
// it was. The moment a READER was handed one (`ibLinqGroupedSample`, so the caret can say what a
// grouping will look like without running it) `ClassNameOf` asked, `GetTypeIDByRef` found no ctor,
// and the designer stopped on an assert.
//
// ⭐ THE RULE, and it is wider than this file: a value that never leaves the runtime can skip
// registration and nobody notices — the bill arrives when somebody READS it. So a type is either
// registered with its neighbours or it is genuinely unreachable, and "unreachable" is a claim that
// stops being true the first time a door hands one out.
// The ids themselves are declared with the classes, which now ANSWER with them (GetClassType) —
// so the number is written once and both the registry and the value read the same constant.
SYSTEM_TYPE_REGISTER(ibValueLinqRows,   "LinqRows",  g_valueLinqRows);
SYSTEM_TYPE_REGISTER(ibValueLinqShape,  "LinqShape", g_valueLinqShape);
SYSTEM_TYPE_REGISTER(ibValueLinqRecord, "LinqRow",   g_valueLinqRecord);
SYSTEM_TYPE_REGISTER(ibValueLinqGroup,  "LinqGroup", g_valueLinqGroup);
