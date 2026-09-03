#ifndef __VALUE_QUERY_H__
#define __VALUE_QUERY_H__

// L4-1 — runtime script value objects for the text query language. THREE types, mirroring the L3
// two-step read (door.Execute() -> result.Select(kind) -> cursor):
//
//   ibValueQueryExec    (script "Query")        — New Query("SELECT …"); q.SetParameter(n, v); q.Execute()
//                                                   -> a QueryResult
//   ibValueQueryResult  (script "QueryResult")  — res.Select()  -> a QuerySelect (the walkable selection).
//   ibValueQuerySelect  (script "QuerySelect")  — the SELECTION cursor:
//                                                   while (s.Next()) { v = s.SomeColumn; lvl = s.Level(); if s.HasChildren()
//                                                   { d = s.Select(); while (d.Next()) … } }   — Select() on a
//                                                   node descends into its child sub-selection (recursive). Output
//                                                   columns are read DIRECTLY as attributes (s.ColumnName) — no Field().
//
// One selection type for BOTH a flat list AND hierarchical TOTALS — no difference at the script
// surface. Internally a QuerySelect is backed by EITHER the forward cursor (a plain SELECT — dot-walk
// projections read by alias) OR an ibSelector (a grouped / TOTALS selection — a folded, re-windable
// tree); res.Select() picks the backing from the query (TotalBy present → grouped, else flat).
//
// > NOTE: the C++ class is `ibValueQueryExec` — `ibValueQuery` is already the LINQ chain wrapper
// > (compiler/procUnitLinq.cpp). Script-visible names: `Query` / `QueryResult` / `QuerySelect`.
//
// See docs/query-language-arc.md §14 / §22 / §23.

#include "backend/compiler/value.h"
#include "backend/compiler/enumUnit.h"          // ibValueEnumeration — runtime enum reflecting ibSelectKind
#include "backend/query/queryAst.h"
#include "backend/query/queryLowering.h"
#include "backend/query/queryTempStore.h"        // ibQueryTempTableStore — what TempTablesManager holds
#include "backend/query/dataQueryBuilder.h"     // ibDataQueryResult / ibSelectKind
#include "backend/query/queryReadState.h"    // ibQueryReadState — the state a live answer is read in
#include "backend/query/querySelector.h"        // ibSelector — the grouped / hierarchical traversal

#include <map>
#include <memory>
#include <vector>

// Runtime enumeration mirroring ibSelectKind — the result-traversal METHOD passed to Select(method):
//   Direct            — a flat list (rows as-is)
//   ByGroups          — folded by the grouping levels
//   ByGroupsHierarchy — folded by levels + the reference-field hierarchy (folders)
// Script: New Query(...).Execute().Select(QueryResultIteration.ByGroupsHierarchy). Registered like the
// other system enums (ENUM_TYPE_REGISTER) in valueQuery.cpp.
class BACKEND_API ibValueEnumQuerySelectKind : public ibValueEnumeration<ibSelectKind>
{
public:
	ibValueEnumQuerySelectKind() : ibValueEnumeration() {}

	virtual void CreateEnumeration() override {
		AddEnumeration(ibSelectKind::ibSelectKind_Direct,            wxT("Direct"),            _("Direct"));
		AddEnumeration(ibSelectKind::ibSelectKind_ByGroups,          wxT("ByGroups"),          _("ByGroups"));
		AddEnumeration(ibSelectKind::ibSelectKind_ByGroupsHierarchy, wxT("ByGroupsHierarchy"), _("ByGroupsHierarchy"));
	}
};

// Type-invariant method surface (Close) — bound through the static base.
void ibValueTempTablesManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ---------------------------------------------------------------------------
// script "TempTablesManager" — the thing that keeps temp tables alive between queries.
// ---------------------------------------------------------------------------
//
// A temp table is made by one query (`SELECT … INTO Sales`) and read by another. What decides
// whether the second query can see it is not the query language — it is WHO IS HOLDING the table,
// and this value is that holder made visible:
//
//     manager = New TempTablesManager();
//     first  = New Query("SELECT Ref INTO Sales FROM Document.Orders");
//     first.TempTablesManager = manager;
//     first.Execute();                        // yields the ROW COUNT; the table stays in `manager`
//
//     second = New Query("SELECT Ref FROM Sales");
//     second.TempTablesManager = manager;     // the SAME tables, a different query
//     second.Execute();
//
//     manager.Close();                        // and they are gone
//
// It is a variable with an owner's job, which is why it is wrapped rather than left implicit: the
// tables die when the manager says so (or when it does), never at some scope boundary the author
// cannot see. One verb — `Close()` — because there is only one decision to make about them.
class BACKEND_API ibValueTempTablesManager : public ibValueStaticMembers<&ibValueTempTablesManager_BindNames>
{
	enum { enClose = 0 };

	// Non-copyable ownership behind a shared handle: the value may be assigned to several queries
	// (that IS the point of it), and every one of them must see the SAME tables.
	std::shared_ptr<ibQueryTempTableStore> m_store = std::make_shared<ibQueryTempTableStore>();

public:
	ibValueTempTablesManager() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	// The store the queries attached to this manager read and write. Never null.
	ibQueryTempTableStore* GetStore() const { return m_store.get(); }

	bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;   // Close
};

// Type-invariant method surface (SetParameter / Execute) — bound through the static base.
void ibValueQueryExec_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueQueryExec : public ibValueStaticMembers<&ibValueQueryExec_BindNames>
{
	enum { enSetParameter = 0, enExecute = 1, enExecuteBatch = 2 };   // unified method index space (proc + func)

public:
	// PUBLIC because the bind function that names the property is a free one (the static member
	// table is shared, so it cannot be a member) — and naming the index in both places beats
	// writing a bare 0 in one of them.
	enum { enPropTempTablesManager = 0 };

private:

	wxString                    m_text;
	// Parsed once in Init (AOT-cacheable). The text is ALWAYS a package — an ordinary query is a
	// package of one — so there is one execution path and no branch about which kind of text this is.
	ibQueryPackage              m_package;
	std::map<wxString, ibValue> m_params;

	// The manager this query's temp tables belong to, or an empty value.
	//
	// EMPTY IS A MEANING, not a missing setting: without a manager the tables this query makes die
	// WITH IT — they exist for the length of one execution and nothing outside can name them. With
	// one, they are kept by the manager until it is closed (or dies), and any other query attached
	// to the same manager reads them. The query never owns them in that case; it only writes into
	// something that outlives it.
	ibValue                     m_tempTables;

	// Run the whole package and convert its results to script values, by position:
	//   a plain select      -> a QueryResult
	//   a select INTO temp  -> the ROW COUNT (the table went into the temp; there is none to hand back)
	//   a drop              -> Undefined
	// Returns false when nothing could run (empty text, or the designer's degraded path).
	// Run the package. `names` — when asked for, the ONTO name of each statement, in step with the
	// results (empty where a statement gave none).
	bool RunPackage(std::vector<ibValue>& out, std::vector<wxString>* names = nullptr);

public:
	ibValueQueryExec() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	bool Init(ibValue** paParams, const long lSizeArray) override;                                   // New Query("text")
	bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;       // SetParameter
	bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
	                ibValue** paParams, const long lSizeArray) override;                              // Execute -> QueryResult

	// TempTablesManager — read and written like any property, because that is what it is: the
	// query is TOLD where its temp tables live, it does not go looking for them.
	bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
};

// What Execute() returns: the raw result + its output schema. res.Select(kind?) consumes it into a
// QuerySelect (the walkable selection) — flat cursor for a plain query, a folded ibSelector for TOTALS.
class BACKEND_API ibValueQueryResult : public ibValueDynamicMembers
{
	enum { enSelect = 0 };

	std::unique_ptr<ibDataQueryResult>         m_result;   // move-only L3 result (cursor); consumed by Select()
	std::vector<ibQueryLowering::OutputColumn> m_schema;
	bool                                       m_hasTotals = false;   // the query had a TOTALS clause

	// ⭐ THE STATE THIS ANSWER WAS READ IN, held for as long as the answer is. A script runs a query
	// and draws its rows whenever it likes; the cursor above is live until then, and every statement
	// behind it must see the same data or the answer contradicts itself. Handed on to the selection
	// in Select(), so releasing THIS does not end the read. Null when a transaction was already open.
	std::shared_ptr<ibQueryReadState>       m_snapshot;

	// ⭐⭐ …AND THE TEMP TABLES THESE ROWS LIVE IN, for exactly the same reason and by the same means.
	// A package's own store dies when its last reader lets go — and a script's `Execute()` hands the
	// result out and reads it later, so the reader IS this object. Held, never touched: the share is
	// the whole of its job. (Null when a TempTablesManager owns the store instead.)
	//
	// 🛑 Without it, `SELECT … INTO T …; SELECT … FROM T` read a column at 0xdddddddd and took the
	// process down on the first field (dump 2026-09-04) — see ibQueryLowering::PackageResult::m_temps.
	std::shared_ptr<ibQueryTempTableStore>  m_temps;

	void FillMembers(ibMemberTable& helper) const;

public:
	ibValueQueryResult();                                                                            // empty
	ibValueQueryResult(ibDataQueryResult&& result, std::vector<ibQueryLowering::OutputColumn> schema, bool hasTotals,
	                   std::shared_ptr<ibQueryReadState> snapshot = nullptr,
	                   std::shared_ptr<ibQueryTempTableStore> temps = nullptr);
	~ibValueQueryResult() override;

	bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
	                ibValue** paParams, const long lSizeArray) override;                              // Select
};

// The SELECTION cursor — flat list OR grouped/TOTALS tree, one uniform surface.
class BACKEND_API ibValueQuerySelect : public ibValueDynamicMembers
{
	enum { enNext = 0, enReset = 1, enHasChildren = 2, enSelect = 3, enTotal = 4, enLevel = 5 };

	// Exactly ONE backing. m_flat = a plain SELECT forward cursor (alias-readable — dot-walk projections
	// included). m_tree = a grouped / TOTALS selection (a folded tree: Next/Reset re-windable, Level() /
	// HasChildren() / Select() descend). m_schema names the output columns for Field + properties.
	std::unique_ptr<ibDataQueryResult>         m_flat;
	std::unique_ptr<ibSelector>                m_tree;
	std::vector<ibQueryLowering::OutputColumn> m_schema;

	// The read's state, inherited from the QueryResult this selection came out of — see the note
	// there. A flat selection needs it most: its cursor is still live and draws rows on demand.
	std::shared_ptr<ibQueryReadState>       m_snapshot;

	// …and the temp tables its rows live in, inherited the same way and for the same reason: a
	// selection outlives the result it came from (`s = q.ExecuteBatch().Get(1).Select()` keeps only
	// this object alive), so the share has to reach the LAST reader, not the first.
	std::shared_ptr<ibQueryTempTableStore>  m_temps;

	void    FillMembers(ibMemberTable& helper) const;
	ibValue ReadColumn(const ibQueryLowering::OutputColumn& oc) const;   // current row / node cell

public:
	ibValueQuerySelect();                                                                            // empty
	ibValueQuerySelect(std::unique_ptr<ibDataQueryResult> flat,
	                   std::vector<ibQueryLowering::OutputColumn> schema,
	                   std::shared_ptr<ibQueryReadState> snapshot = nullptr,
	                   std::shared_ptr<ibQueryTempTableStore> temps = nullptr);                    // flat list
	ibValueQuerySelect(ibSelector&& tree,
	                   std::vector<ibQueryLowering::OutputColumn> schema,
	                   std::shared_ptr<ibQueryReadState> snapshot = nullptr,
	                   std::shared_ptr<ibQueryTempTableStore> temps = nullptr);                    // grouped / TOTALS
	~ibValueQuerySelect() override;

	bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
	                ibValue** paParams, const long lSizeArray) override;                              // Next/Reset/HasChildren/Select/Total/Level
	bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;                              // s.ColumnName (direct attribute)
	bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;                         // read-only
};

#endif
