#ifndef __COMPOSITION_CONDITION_H__
#define __COMPOSITION_CONDITION_H__

// ---------------------------------------------------------------------------
// THE CONDITION, READ AGAINST A ROW IN HAND — the composer's evaluator, in a file of its own and called the same way
// by the RAM composer and by the database one (Max, 2026-09-30). Every reader of rows in memory asks it and none
// keeps a reading of its own: the walk hiding a node's headings (ibDataDBComposer::LevelShows), a table filtering its
// rows (ibDataRamComposer::ComputeOrder), a rule of the conditional appearance marking a line
// (ibDataComposer::ConditionalAppearance — on the walk, on a printed list — and ibValueModel::GetAttrByRow on a
// list's grid). A read from the database hands its own filter to the server; what is read here stays in memory.
//
// The caller says what it knows of the row — a field's value (ibCompositionValueOf) and what «in hierarchy» admits
// (ibCompositionSubtreeOf, which the composer answers: ibDataComposer::SubtreeOf) — and the engine does the rest.
// ---------------------------------------------------------------------------

#include "backend/backend_core.h"
#include "backend/compiler/value.h"           // ibValue — what a row holds
#include "backend/compositionDescription.h"   // the filter and the rules read here
#include "backend/composition/drivers/compositionDriver.h"   // ibCompositionAttr — what a rule draws a cell with

#include <wx/string.h>
#include <functional>   // what the caller knows of the row
#include <vector>

class ibQueryHierarchyScope;   // queryHierarchy.h — what «in hierarchy» admits, handed by pointer

// COMPARE ONE VALUE THE WAY A FILTER LINE SPELLS IT — `=`, `<>` / `!=`, the four ordered ones, and
// LIKE (whose `%` / `_` are the wildcards the query language uses). An operator nobody recognises
// answers TRUE: a filter that cannot be read must not silently hide rows.
//
// One implementation, because there is one question. The RAM composer asks it per row, the walk
// asks it per group, and a second copy of the spelling would answer one of them differently.
BACKEND_API bool ibCompositionCompare(const ibValue& cell, const wxString& op, const ibValue& value);
// …AND ASKED WITH THE KIND A STORED CONDITION ACTUALLY HOLDS — see the definition.
BACKEND_API bool ibCompositionCompare(const ibValue& cell, ibComparisonKind kind, const ibValue& value);

// ⭐⭐ DOES A CONDITION HOLD FOR THE ROW IN HAND. A condition names fields; `valueOf` answers a field's value in this
// row — either side of a comparison may be one — null where the row does not carry it. `subtreeOf` answers what
// «in hierarchy» of a named value admits for a field (ibQueryHierarchyScope) — the caller's to answer, since it
// knows the field's catalog and reads its tree once — null where it cannot.
//
// `whenUnknown` — the answer where the condition cannot be read here: a field the row does not carry (absent, or
// NULL — the fold's word for a column its level does not hold), a subtree nobody answered. A filter that HIDES
// shows then (true); a rule that MARKS does not mark (false). Inside, the reading is three-valued — AND, OR and
// NOT combine "unknown" honestly — and only the whole condition's unknown becomes the caller's answer.
using ibCompositionValueOf = std::function<const ibValue*(const wxString& path)>;
using ibCompositionSubtreeOf = std::function<const ibQueryHierarchyScope*(const wxString& path, const ibValue& named)>;
BACKEND_API bool ibCompositionFilterHolds(const ibFilterDescription& filter, const ibCompositionValueOf& valueOf,
	const ibCompositionSubtreeOf& subtreeOf, bool whenUnknown);

// ⭐⭐ A RULE MADE READY — its appearance turned ONCE into what a cell is drawn with (ibCompositionAttr: its colours,
// its font, its alignment, its Text in the language in force, its Format parsed): the ONE conversion from what a
// setting stores to what every driver takes. A report reads a million lines and a rule does not change between
// them, so nothing of it is unpacked, translated or parsed per line — only its condition is left to each one.
// The description it points at is the setting's own and outlives the run (the storeys are not written during one).
struct ibCompositionRule {
	const ibConditionalAppearanceRuleDescription* m_rule = nullptr;   // its condition and its fields
	ibCompositionAttr                             m_attr;             // what it draws a cell with
};
// …a storey's rules made ready — the ticked ones that say anything a cell shows, in their order.
BACKEND_API std::vector<ibCompositionRule> ibCompositionRulesOf(const ibConditionalAppearanceDescription& rules);

// ⭐ …AND WHAT THEY SAY OF ONE ROW — each rule whose condition holds on it (a condition it cannot read marks nothing)
// said over the LINE where it names no field, else handed to `sayField` once per field it names: the caller knows
// its own columns — the walk its schema, a list its model. True when any rule held.
using ibCompositionSayField = std::function<void(const wxString& field, const ibCompositionAttr& attr)>;
BACKEND_API bool ibCompositionSayRules(const std::vector<ibCompositionRule>& rules,
	const ibCompositionValueOf& valueOf, const ibCompositionSubtreeOf& subtreeOf,
	ibCompositionAttr& line, const ibCompositionSayField& sayField);

#endif // __COMPOSITION_CONDITION_H__
