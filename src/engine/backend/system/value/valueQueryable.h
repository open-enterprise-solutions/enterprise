#ifndef __VALUE_QUERYABLE_H__
#define __VALUE_QUERYABLE_H__

// L4-2 — ibValueQueryable (script "Queryable"): a DATA SOURCE as a script value.
//
// Vended by the `Data` global (Data.Catalogs.Номенклатура — see
// moduleManager/moduleManagerDataUnit.cpp); never constructed with New. Wraps the
// metaobject's ibBackendQueryable — the SAME object the text query language reads
// through, so the script surface and `FROM Catalogs.X` are one world by
// construction.
//
// ONLY LINQ speaks to it. The value exposes NO properties and NO own methods —
// the single entry is the LINQ dispatch (DispatchLinqMethod override) plus the
// iteration protocol (CreateIterator — what Foreach uses):
//
//   - a TRANSLATABLE pipeline op FOLDS into the accumulated L3 door builder and
//     returns a NEW Queryable (the chain is lazy):
//       Where(λ)  — the lambda's recorded AST (compiler/lambdaQueryAst.*)
//                   lowers to a predicate tree; captured outer locals resolve
//                   BY NAME from the lambda's captured frames AT FOLD TIME;
//       OrderBy / OrderByDescending(λ) — a pure member-path selector;
//       Take(n)   — the row limit;
//   - a TERMINAL executes through the door: Count / Any (server-side
//     aggregate), First / FirstOrDefault (page of 1), ToArray (stream rows as
//     row-reference objects);
//   - ANYTHING untranslatable (no recorded AST, an unresolvable capture, any
//     other op) MATERIALISES the accumulated query into an Array of references
//     and re-dispatches the op there — the RAM floor, always correct;
//   - Foreach streams the selection row by row (references), no Array built.
//
// LAZY BY CONTRACT: holding the value reads nothing; the debugger / watch shows
// a DESCRIPTION (GetString = "Queryable(Catalogs.X | Where, Take 20)"), never
// data — no implicit execution from looking at the value.
//
// Row identity: sources with a single-reference primary key (catalogs /
// documents / charts / enums) stream their rows AS REFERENCE OBJECTS. Sources
// without one (registers / Data.From collections) stream rows as STRUCTURES
// of their columns — same chain, different row shape.
//
// Data.From(table) wraps an in-memory value table through ibTempTableQueryable;
// the wrapper is OWNED by this value (m_ownedSource) — metaobject sources stay
// non-owning (the metadata owns them).
//
// See docs/query-language-arc.md §23.5 (L4-2) and the naming canon notes.

#include "backend/compiler/value.h"
#include "backend/query/dataQueryBuilder.h"   // ibDataQueryBuilder (accumulated, by value)

#include <memory>

class ibBackendQueryable;

class BACKEND_API ibValueQueryable : public ibValue
{
	const ibBackendQueryable* m_queryable = nullptr;
	wxString                  m_sourceName;          // "Catalogs.Номенклатура" — watch / diagnostics
	ibDataQueryBuilder        m_builder;             // accumulated door verbs (From set at vend)
	long                      m_take = 0;            // Take(n) — page limit at execute (0 = all)
	std::vector<wxString>     m_ops;                 // folded-op labels — the watch string only
	// Owned wrapper for a NON-metaobject source (Data.From over a value table) —
	// shared across chain links (every link reads the same materialised relation).
	std::shared_ptr<const ibBackendQueryable> m_ownedSource;

	// Fresh chain link: same source, the caller mutates the copy's builder/take.
	ibValueQueryable* CloneLink() const;

	// The row-reference column (single-key sources) — null for registers / constants.
	const ibBackendQueryColumn* RowReferenceColumn() const;

	// Execute the accumulated query (honouring m_take) — the implicit execution
	// every terminal / iteration funnels through.
	ibDataQueryResult ExecuteAccumulated() const;

	// The RAM floor: materialise into an Array of references and re-dispatch.
	void MaterialiseThenRam(ibLinqMethod method, ibValue& ret, ibValue** args, long n);

public:
	ibValueQueryable() : ibValue(ibValueTypes::TYPE_VALUE) {}
	ibValueQueryable(const ibBackendQueryable* queryable, const wxString& sourceName);
	// Data.From — an OWNED in-memory source (the wrapper dies with the last chain link).
	ibValueQueryable(std::shared_ptr<const ibBackendQueryable> owned, const wxString& sourceName);

	const ibBackendQueryable* GetQueryable()  const { return m_queryable; }
	const wxString&           GetSourceName() const { return m_sourceName; }

	virtual bool IsEmpty() const override { return m_queryable == nullptr; }

	// Watch-safe: a DESCRIPTION of the source + folded ops, never the data.
	virtual wxString GetString() const override;

	// The single consumer surface — LINQ ops fold / execute (see the header note).
	virtual void DispatchLinqMethod(ibLinqMethod method, ibValue& ret,
	                                ibValue** args, long n) override;

	// Foreach — stream the selection as row references (no materialised Array).
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

	// Identity — two Queryables over the same source compare equal.
	virtual bool CompareValueEQ(const ibValue& cParam) const override {
		const ibValueQueryable* other = dynamic_cast<ibValueQueryable*>(cParam.GetRef());
		return other != nullptr && other->m_queryable == m_queryable;
	}
	virtual bool CompareValueNE(const ibValue& cParam) const override {
		return !CompareValueEQ(cParam);
	}
};

#endif
