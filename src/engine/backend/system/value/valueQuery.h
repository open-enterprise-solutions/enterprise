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
#include "backend/query/dataQueryBuilder.h"     // ibDataQueryResult / ibSelectKind
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

// Type-invariant method surface (SetParameter / Execute) — bound through the static base.
void ibValueQueryExec_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueQueryExec : public ibValueStaticMembers<&ibValueQueryExec_BindNames>
{
	enum { enSetParameter = 0, enExecute = 1 };   // unified method index space (proc + func)

	wxString                    m_text;
	ibQuerySelectPtr            m_ast;            // parsed once in Init (AOT-cacheable)
	std::map<wxString, ibValue> m_params;

public:
	ibValueQueryExec() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	bool Init(ibValue** paParams, const long lSizeArray) override;                                   // New Query("text")
	bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;       // SetParameter
	bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
	                ibValue** paParams, const long lSizeArray) override;                              // Execute -> QueryResult
};

// What Execute() returns: the raw result + its output schema. res.Select(kind?) consumes it into a
// QuerySelect (the walkable selection) — flat cursor for a plain query, a folded ibSelector for TOTALS.
class BACKEND_API ibValueQueryResult : public ibValueDynamicMembers
{
	enum { enSelect = 0 };

	std::unique_ptr<ibDataQueryResult>         m_result;   // move-only L3 result (cursor); consumed by Select()
	std::vector<ibQueryLowering::OutputColumn> m_schema;
	bool                                       m_hasTotals = false;   // the query had a TOTALS clause

	void FillMembers(ibMemberTable& helper) const;

public:
	ibValueQueryResult();                                                                            // empty
	ibValueQueryResult(ibDataQueryResult&& result, std::vector<ibQueryLowering::OutputColumn> schema, bool hasTotals);
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

	void    FillMembers(ibMemberTable& helper) const;
	ibValue ReadColumn(const ibQueryLowering::OutputColumn& oc) const;   // current row / node cell

public:
	ibValueQuerySelect();                                                                            // empty
	ibValueQuerySelect(std::unique_ptr<ibDataQueryResult> flat,
	                   std::vector<ibQueryLowering::OutputColumn> schema);                            // flat list
	ibValueQuerySelect(ibSelector&& tree,
	                   std::vector<ibQueryLowering::OutputColumn> schema);                            // grouped / TOTALS
	~ibValueQuerySelect() override;

	bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
	                ibValue** paParams, const long lSizeArray) override;                              // Next/Reset/HasChildren/Select/Total/Level
	bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;                              // s.ColumnName (direct attribute)
	bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;                         // read-only
};

#endif
