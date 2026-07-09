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
	// Select(x => x.Field) — a single-column SCALAR projection: the read yields THIS column's
	// value per row instead of the whole row. null = full row (reference / structure).
	// A plain column reads by pointer (m_projectCol); a DOT-WALK leaf (x => x.Ref.Field) is
	// projected onto the builder as SelectPath(...) AS m_projectAlias and read back by that name.
	const ibBackendQueryColumn* m_projectCol = nullptr;
	wxString                    m_projectAlias;
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

	// L4-2 JOIN push-down: when the inner argument is ALSO a queryable and the two key
	// selectors each lower to one column, build the JOIN on the L3 door and run the
	// (outer, inner) result-selector in RAM over the reconstructed rows. The door decides
	// HOW the join runs — co-located in one server-side SELECT (DB ⋈ DB), temp-promoted
	// (a computed inner, e.g. Data.From, materialised into a DB temp table), or RAM-
	// stitched — all transparent here. Each side's row is reconstructed to match the RAM
	// path's shape: a reference object for a single-reference-keyed source (catalog /
	// document), a structure of columns otherwise (register / Data.From) — so no single-PK
	// requirement. Returns true when it handled the op; false on anything outside the slice
	// (the caller then falls to MaterialiseThenRam — the RAM floor / ibValueJoinState,
	// always correct). Unifies the two LINQ join paths from the queryable side without
	// removing the RAM fallback. (docs/query-language-arc.md)
	bool JoinPushDown(ibValue& ret, ibValue** args, long n);

public:
	ibValueQueryable() : ibValue(ibValueTypes::TYPE_VALUE) {}
	ibValueQueryable(const ibBackendQueryable* queryable, const wxString& sourceName);
	// Data.From — an OWNED in-memory source (the wrapper dies with the last chain link).
	ibValueQueryable(std::shared_ptr<const ibBackendQueryable> owned, const wxString& sourceName);

	const ibBackendQueryable* GetQueryable()  const { return m_queryable; }
	const wxString&           GetSourceName() const { return m_sourceName; }

	// RLS — present this queryable's ACCUMULATED chain (From / Join / Where) as an ibBackendQueryable
	// SOURCE (a subquery over the restricted rows). A role's restriction (From(source).Join(ACL).Where(…))
	// is grafted by making the query read FROM this — it carries ALL the source's columns, so the
	// downstream query needs no key. Owns the wrapped builder.
	std::shared_ptr<const ibBackendQueryable> AsSource() const;

	// L4-2 JOIN unification (Layer 2) — the RAM-side entry. When `.Join()` is dispatched
	// on a NON-queryable receiver (a value table) but the inner argument IS a real DB
	// queryable, wrap the receiver as a computed leaf and run server-side through
	// JoinPushDown (the composer temp-promotes the RAM side into a DB temp table). Returns
	// true when handled; false -> the caller stays on the RAM hash-join (ibValueJoinState),
	// always correct. Called from the base LINQ dispatch (procUnitLinq.cpp, case Join), so
	// BOTH receiver kinds (queryable / RAM table) reach the one L3 join executor.
	static bool TryJoinThroughL3(ibValue& receiver, ibValue& ret, ibValue** args, long n);

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

// ibValueQueryDecorator — the universal query DECORATOR (the base type). A policy hands THIS to a
// handler module, which folds Join / Where DIRECTLY into the query being executed — ONE builder, no
// separate template, no copy-merge. RLS (ibRuntimeAccessPolicy running role modules) is the FIRST
// user; multi-company / soft-delete / audit are other policies over the SAME decorator — so RLS is a
// policy, NOT a decorator subtype. Join and Where are the ONLY surface (a decorator NARROWS / shapes,
// nothing else) and write straight into the target ibDataQueryBuilder, so the composer runs ONE final
// SELECT with the real source kept (it PAGES and PUSHES DOWN). The target already had its policy nulled
// by copy-apply-execute, so folding + executing does NOT re-enter the policy. Constructed ONLY by a
// policy — unreachable elsewhere.
class BACKEND_API ibValueQueryDecorator : public ibValue
{
	ibDataQueryBuilder*       m_target = nullptr;   // the query being decorated — Join/Where fold IN HERE
	const ibBackendQueryable* m_source = nullptr;   // the table being decorated (the .From leaf)
	wxString                  m_sourceName;

public:
	ibValueQueryDecorator() : ibValue(ibValueTypes::TYPE_VALUE) {}
	ibValueQueryDecorator(ibDataQueryBuilder* target, const ibBackendQueryable* source, const wxString& sourceName);

	virtual bool     IsEmpty()   const override { return m_source == nullptr; }
	virtual wxString GetString() const override;

	// The ONLY surface: Join(innerQueryable, leftKey, rightKey [, op]) and Where(predicate) — both fold
	// straight into the target query (never a separate builder).
	virtual void DispatchLinqMethod(ibLinqMethod method, ibValue& ret, ibValue** args, long n) override;

	// Type branch in a handler: `Source = "Document.Поступление"` compares the decorated source to its
	// canonical full-name STRING ("<ClassName>.<Name>"), so a module can shape per source.
	virtual bool CompareValueEQ(const ibValue& cParam) const override;
	virtual bool CompareValueNE(const ibValue& cParam) const override { return !CompareValueEQ(cParam); }
};

#endif
