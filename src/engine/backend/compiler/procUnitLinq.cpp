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

#include "procUnit.h"
#include "procUnitValues.h"   // ibValueFunction full def + AsFunction / AsIterator
#include "procUnitState.h"
#include "session/session.h"

#include "system/value/valueArray.h"  // ToArray() materialiser
#include "system/value/valueMap.h"    // ibValueStructure / ibValueContainer (GroupBy)
#include "system/value/valueQueryable.h"  // ibValueQueryable::TryJoinThroughL3 — RAM-receiver join push-down (Layer 2)

#include <algorithm>
#include <map>
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
		ibBackendCoreException::Error(_("LINQ: lambda value is not initialised"));
	}

	const long lambdaParamCount = (long)bfn->m_listParam.size();
	const long lambdaVarCount   = bfn->m_lVarCount;

	if (lambdaParamCount < argCount) {
		ibBackendCoreException::Error(
			_("LINQ: lambda must accept at least %ld argument(s)"), argCount);
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

	std::shared_ptr<ibRunContext> spHeapCtx;
	ibRunContext                  stackCtx(bHeapFrame ? wxNOT_FOUND : (int)lambdaVarCount);
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
			ibBackendCoreException::Error(
				_("LINQ: lambda m_listParam shorter than paramCount at %ld"), i);
		}
		const ibParamRunUnit& puDef = bfn->m_listParam[i].m_defaultValue;
		if (puDef.m_numArray == DEF_VAR_SKIP) {
			const wxString& nm = (i < (long)bfn->m_listParam.size())
				? bfn->m_listParam[i].m_strName
				: wxString::Format(wxT("p%ld"), i);
			ibBackendCoreException::Error(
				_("LINQ: lambda missing required argument '%s'"), nm);
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
		long rowIdx = 0;
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
			++rowIdx;
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
		long rowIdx = 0;
		while (m_upstream->MoveNext(current)) {
			bool found = false;
			for (const auto& v : m_seen) {
				if (current == v) { found = true; break; }
			}
			++rowIdx;
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

// GroupBy node — bucket upstream by key extracted via fn(elem).
// On first MoveNext drain upstream + build ordered buckets, then
// emit one `Structure{Key, Values:Array}` per group. Key equality
// uses ibValue::operator< on the key (stable / total order; same
// comparator as OrderBy / Sort).
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

		ibValueStructure* row = new ibValueStructure();
		row->Insert(ibValue(wxT("Key")),    m_groups[m_pos].first);
		ibValueArray* values = new ibValueArray();
		for (auto& v : m_groups[m_pos].second) values->Add(v);
		ibValue valuesVal(values);
		row->Insert(ibValue(wxT("Values")), valuesVal);
		CopyValue(current, ibValue(row));

		++m_pos;
		return true;
	}

	void Reset() override {
		m_pos = 0;
	}

	bool PeekSample(ibValue& /*current*/) const override { return false; }

private:
	struct KeyCmp {
		bool operator()(const ibValue& a, const ibValue& b) const { return a < b; }
	};

	void EnsureGrouped() {
		if (m_built) return;
		m_built = true;
		if (!m_upstream) return;
		// Hybrid: std::map for O(log N) key lookup + std::vector for
		// insertion-order preservation. Each key maps to an INDEX into
		// m_groups; previously this was a linear `!(a<b) && !(b<a)`
		// scan giving O(N²) on lookup. New strategy is O(N log K).
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
	std::map<ibValue, size_t, KeyCmp>                                 m_keyIdx;
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
		// look up bucket (O(log N) via std::map; could be replaced
		// with std::unordered_map keyed on ibValue::GetString if N
		// huge). Stateful inner-bucket cursor across MoveNext calls
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
	struct KeyCmp {
		bool operator()(const ibValue& a, const ibValue& b) const { return a < b; }
	};

	void EnsureIndexed() {
		if (m_built) return;
		m_built = true;
		// Materialise inner once into a hash. Each key maps to a
		// vector of matching inner rows (multi-match support).
		std::shared_ptr<ibValueIteratorState> innerIt = m_innerSrc.CreateIterator();
		if (!innerIt) return;
		ibValue elem;
		long count = 0;
		while (innerIt->MoveNext(elem)) {
			ibValue rightK;
			CallLambdaWithArg(m_rightKey, elem, rightK);
			m_hash[rightK].push_back(elem);
			++count;
		}
	}

	std::shared_ptr<ibValueIteratorState> m_outerUp;
	ibValue                                m_innerSrc;
	ibValueFunction                        m_leftKey;
	ibValueFunction                        m_rightKey;
	ibValueFunction                        m_projection;
	bool                                   m_built = false;
	std::map<ibValue, std::vector<ibValue>, KeyCmp> m_hash;
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
class ibValueQuery : public ibValue {
	public:
	ibValueQuery() : ibValue(ibValueTypes::TYPE_VALUE) {}

	explicit ibValueQuery(std::shared_ptr<ibValueIteratorState> state)
		: ibValue(ibValueTypes::TYPE_VALUE)
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
		ibBackendCoreException::Error(_("LINQ: cannot dispatch on null value"));
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
		ibBackendCoreException::Error(_("LINQ: value is not iterable"));
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
				ibBackendCoreException::Error(_("LINQ: method requires a function argument"));
			}
			ibValueFunction* fn = AsFunction(args[0]);
			if (fn == nullptr) {
				ibBackendCoreException::Error(
					_("LINQ: argument must be a Function or Procedure value"));
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
			long count = 0;
			while (upstream->MoveNext(current)) {
				arr->Add(current);
				++count;
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
				ibBackendCoreException::Error(_("LINQ: OrderBy requires a key selector function"));
			ibValueFunction* keyFn = AsFunction(args[0]);
			if (keyFn == nullptr)
				ibBackendCoreException::Error(_("LINQ: OrderBy key selector must be a Function value"));
			const bool desc = (method == M::OrderByDescending);
			auto pipeline = std::make_shared<ibValueOrderByState>(std::move(upstream), *keyFn, desc);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::GroupBy: // lazy, materialise + ordered buckets
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: GroupBy requires a key selector function"));
			ibValueFunction* keyFn = AsFunction(args[0]);
			if (keyFn == nullptr)
				ibBackendCoreException::Error(_("LINQ: GroupBy key selector must be a Function value"));
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
				ibBackendCoreException::Error(
					_("LINQ: Join requires (inner, leftKey, rightKey, projection)"));
			ibValueFunction* leftKeyFn  = AsFunction(args[1]);
			ibValueFunction* rightKeyFn = AsFunction(args[2]);
			ibValueFunction* projFn     = AsFunction(args[3]);
			if (leftKeyFn == nullptr || rightKeyFn == nullptr || projFn == nullptr)
				ibBackendCoreException::Error(
					_("LINQ: Join leftKey / rightKey / projection must be Function values"));
			auto pipeline = std::make_shared<ibValueJoinState>(
				std::move(upstream), *args[0], *leftKeyFn, *rightKeyFn, *projFn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Skip: // Skip(n)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: Skip requires a count argument"));
			const long count = (long)args[0]->GetNumber().ToInt64();
			auto pipeline = std::make_shared<ibValueSkipState>(std::move(upstream), count);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Take: // Take(n)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: Take requires a count argument"));
			const long count = (long)args[0]->GetNumber().ToInt64();
			auto pipeline = std::make_shared<ibValueTakeState>(std::move(upstream), count);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::SkipWhile: // SkipWhile(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr) ibBackendCoreException::Error(_("LINQ: SkipWhile requires a predicate function"));
			auto pipeline = std::make_shared<ibValueSkipWhileState>(std::move(upstream), *fn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::TakeWhile: // TakeWhile(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr) ibBackendCoreException::Error(_("LINQ: TakeWhile requires a predicate function"));
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
				ibBackendCoreException::Error(_("LINQ: Concat requires another iterable"));
			auto bIt = args[0]->CreateIterator();
			if (!bIt) ibBackendCoreException::Error(_("LINQ: Concat operand is not iterable"));
			auto pipeline = std::make_shared<ibValueConcatState>(std::move(upstream), std::move(bIt));
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Union: // concat + distinct
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: Union requires another iterable"));
			auto bIt = args[0]->CreateIterator();
			if (!bIt) ibBackendCoreException::Error(_("LINQ: Union operand is not iterable"));
			auto cat = std::make_shared<ibValueConcatState>(std::move(upstream), std::move(bIt));
			auto pipeline = std::make_shared<ibValueDistinctState>(std::move(cat));
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Intersect: // Intersect(other)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: Intersect requires another iterable"));
			auto pipeline = std::make_shared<ibValueIntersectState>(std::move(upstream), *args[0]);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::Except: // Except(other)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: Except requires another iterable"));
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
				ibBackendCoreException::Error(_("LINQ: Last() on empty sequence"));
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
				ibBackendCoreException::Error(_("LINQ: Single() on empty sequence"));
			if (count > 1)
				ibBackendCoreException::Error(_("LINQ: Single() - sequence contains more than one element"));
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
				ibBackendCoreException::Error(_("LINQ: ElementAt requires an index"));
			const long idx = (long)args[0]->GetNumber().ToInt64();
			if (idx < 0) {
				if (method == M::ElementAt)
					ibBackendCoreException::Error(_("LINQ: ElementAt - negative index"));
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
				ibBackendCoreException::Error(_("LINQ: ElementAt - index out of range"));
			CopyValue(ret, found ? current : ibValue());
			break;
		}
		case M::Contains: // Contains(value)
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: Contains requires a value argument"));
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
				ibBackendCoreException::Error(_("LINQ: SequenceEqual requires another iterable"));
			auto bIt = args[0]->CreateIterator();
			if (!bIt) ibBackendCoreException::Error(_("LINQ: SequenceEqual operand is not iterable"));
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
				ibBackendCoreException::Error(_("LINQ: Aggregate requires (seed, reducer)"));
			ibValueFunction* reducer = AsFunction(args[1]);
			if (reducer == nullptr)
				ibBackendCoreException::Error(_("LINQ: Aggregate reducer must be a Function value"));
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
		case M::WhereIndexed: // WhereIndexed(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr)
				ibBackendCoreException::Error(_("LINQ: WhereIndexed requires a predicate function"));
			auto pipeline = std::make_shared<ibValueWhereIndexedState>(std::move(upstream), *fn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}
		case M::SelectIndexed: // SelectIndexed(fn)
		{
			ibValueFunction* fn = (n >= 1 && args && args[0]) ? AsFunction(args[0]) : nullptr;
			if (fn == nullptr)
				ibBackendCoreException::Error(_("LINQ: SelectIndexed requires a projection function"));
			auto pipeline = std::make_shared<ibValueSelectIndexedState>(std::move(upstream), *fn);
			CopyValue(ret, ibValue(new ibValueQuery(std::move(pipeline))));
			break;
		}

		case M::ToTable:
			// Meaningful only on a data source (Data.* / Queryable, which overrides the
			// dispatch) — a plain RAM iterable has no column schema to materialise.
			ibBackendCoreException::Error(
				_("LINQ: ToTable is supported on data sources (Data.*) only"));
			break;

		default:
			ibBackendCoreException::Error(
				_("LINQ: unknown method index %ld"), realNum);
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
		{ M::GroupBy,            L"GroupBy",            L"Group by fn(elem) -> key; emits Structure{Key, Values}" },
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
