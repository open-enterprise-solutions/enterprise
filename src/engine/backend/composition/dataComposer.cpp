////////////////////////////////////////////////////////////////////////////
//	L5-1 — the data composer: schema -> rendered L4-1 text -> driver (dataComposer.h)
////////////////////////////////////////////////////////////////////////////

#include "dataComposerInternal.h"

#include "backend/query/queryParser.h"        // ibQueryParser — text -> AST
#include "backend/query/queryable.h"          // ibBackendQueryable / ibBackendQueryColumn
#include "backend/query/queryableFactory.h"   // the source factory — the column dictionary
#include "backend/query/dataQueryBuilder.h"   // ibDataQueryResult / ibSelectKind
#include "backend/query/querySelector.h"      // ibSelector — the TOTALS pre-order walk
#include "backend/appData.h"                  // ibApplicationData::GetQueryableFactory
#include "backend/metaData.h"                 // ibMetaData::GetSourceFactory — resolve by-name sources per-config
#include "backend/backend_exception.h"        // ibBackendCoreException
#include "backend/query/queryReadState.h"  // ibQueryReadState — one build, one state of the data
#include "backend/query/queryRender.h"        // ibQueryColumnFromPath — a dotted path becomes a column, once
#include "backend/query/queryKeywords.h"      // ibQueryKeywordText — a grouping line is written in the query's own words
#include "backend/query/queryLexer.h"         // ibQueryLexer::IsIdentifier — what a NAME is, asked of the tier that defines it

//////////////////////////////////////////////////////////////////////
// sources
//////////////////////////////////////////////////////////////////////

// The marker namespace any FromSource(queryable) source renders under ("Temp.t0"). Not a real
// metaobject kind — the factory never resolves it; the auxiliary registry (ibTempSourceScope) catches
// the name first and returns the registered queryable directly. Kept opaque on purpose: the value is
// irrelevant to resolution (ResolveSource keys on the name alone), it only has to parse as <ns>.<name>.
static const wxChar* const s_tempSourceNamespace = wxT("Temp");

ibDataDBComposer& ibDataDBComposer::FromSource(const wxString& ns, const wxString& name)
{
	// SET the single FROM source (symmetric with FromText, which clears too). Append-WITHOUT-clear made the
	// persistent model composer ACCUMULATE one duplicate source per fetch — RunComposerPage rebinds the source
	// every page over the SAME composer, and ClearSettings does not touch m_sources — which renders as a growing
	// self-JOIN (m_sources: [0]=FROM, [1..]=JOIN). Multi-source JOINs are built on the query BUILDER (.Join),
	// not by repeated FromSource, so resetting here is safe.
	m_sourceText.Clear();
	m_sources.clear();
	m_directSources.clear();   // a transient registry belongs to ONE source set — reset it in lock-step
	m_prepared.Forget();
	m_sources.push_back({ ns, name });
	return *this;
}

ibDataDBComposer& ibDataDBComposer::FromSource(const ibBackendQueryable* queryable)
{
	if (queryable == nullptr)
		ibBackendCoreException::Error(_("Composer: a null queryable was given as the source"));

	// FROM resets the source set — the verbatim text, the factory sources, AND the transient registry.
	m_sourceText.Clear();
	m_sources.clear();
	m_directSources.clear();
	m_prepared.Forget();

	// Source the LIVE queryable DIRECTLY through the auxiliary per-query registry: register it under an
	// auto-numbered per-query name (t0, t1, …) and render "FROM Temp.t0"; ResolveSource hands it straight
	// back, unchanged. The NUMBER is how the query tells transient sources apart — a future JOIN of a second
	// temp table just takes the next slot (t1, t2) over the same registry.
	//
	// NO metadata round-trip. The caller was JUST handed a complete L3 queryable — it carries its own
	// columns + provider + reconstruction context — so the composer has no business searching the metadata
	// to recover a metaobject identity for it, then converting that to a "kind.name" the lowering only
	// re-resolves to the very same queryable. Worse, the old recovery used ibValue::GetNameObjectFromID, a
	// STATIC-registry lookup that returns nothing for a class registered in the METADATA (every dynamic
	// catalog / document) — so it was outright broken for those. A named metaobject source, when one is
	// genuinely wanted (readable text), is requested via FromSource(ns, name).
	const wxString name = wxString::Format(wxT("t%u"), static_cast<unsigned int>(m_directSources.size()));
	m_directSources[name] = queryable;
	m_sources.push_back({ s_tempSourceNamespace, name });
	return *this;
}

ibDataDBComposer& ibDataDBComposer::FromText(const wxString& text)
{
	m_sourceText = text;
	m_sources.clear();
	// ⭐ AND THIS IS WHERE A RUN STARTS. Every compose calls it, so it is the honest moment to let go
	// of what the LAST run prepared: a temp table holds ROWS, and rows read a minute ago are not an
	// answer to a report being asked again now. (Handing the same text back does not save them — the
	// question is not "is it the same query" but "is it the same reading of the data".)
	m_prepared.Forget();
	return *this;
}

bool ibCompositionCompare(const ibValue& cell, const wxString& op, const ibValue& value)
{
	if (op == wxT("="))                            return cell == value;
	if (op == wxT("<>") || op == wxT("!="))        return cell != value;
	if (op == wxT(">"))                            return cell >  value;
	if (op == wxT(">="))                           return cell >= value;
	if (op == wxT("<"))                            return cell <  value;
	if (op == wxT("<="))                           return cell <= value;
	if (op.CmpNoCase(wxT("LIKE")) == 0) {
		wxString pattern = value.GetString();
		pattern.Replace(wxT("%"), wxT("*"));
		pattern.Replace(wxT("_"), wxT("?"));
		return cell.GetString().Lower().Matches(pattern.Lower());
	}
	return true;   // unknown operator → do not hide anything over a line nobody can read
}

// ⭐ THE SAME COMPARISON, ASKED WITH THE KIND IT IS. A stored condition holds an ibComparisonKind,
// not a spelling, and the string pair that used to translate between them is gone (there was an
// inverse map that read "IN" back as Equal — see list-settings.md § 5a). So the row-side comparison
// answers the kind directly; membership is the one that cannot be asked of a single value here, and
// it hides nothing rather than pretending.
bool ibCompositionCompare(const ibValue& cell, ibComparisonKind kind, const ibValue& value)
{
	switch (kind) {
	case ibComparisonKind_Equal:        return cell == value;
	case ibComparisonKind_NotEqual:     return cell != value;
	case ibComparisonKind_Greater:      return cell >  value;
	case ibComparisonKind_GreaterEqual: return cell >= value;
	case ibComparisonKind_Less:         return cell <  value;
	case ibComparisonKind_LessEqual:    return cell <= value;
	case ibComparisonKind_Contains:     return ibCompositionCompare(cell, wxT("LIKE"),
	                                        ibValue(wxT("%") + value.GetString() + wxT("%")));
	default:                            return true;   // In / InHierarchy — a set is the server's question
	}
}

//////////////////////////////////////////////////////////////////////
// settings
//////////////////////////////////////////////////////////////////////

ibDataComposer& ibDataComposer::Select(const wxString& nameOrPath)
{
	if (!nameOrPath.IsEmpty())
		m_commonSelected.push_back(ibSelectedFieldDescription::Field(nameOrPath));
	return *this;
}

wxString ibDataComposer::AddParam(const ibValue& value)
{
	// The value travels as an auto-named &parameter — never inlined into the
	// text. That is what keeps a string value from being read as syntax and a
	// date from being read in somebody's locale.
	const wxString param = wxString::Format(wxT("__f%d"), m_autoParam++);
	m_params[param] = value;
	return param;
}

// ⭐⭐ A SETTING WITH NO FIELD IS NOT A SETTING — IT IS THE ABSENCE OF ONE, and it stops here.
//
// These three verbs write a PATH into the rendered text, and an empty one renders a clause with a hole
// in it: `ORDER BY ` (nothing after it), `WHERE  = &p0`, `TOTALS BY `. The parser then refuses the whole
// query — correctly, it is not a query — and the list that asked shows the same blank a source with no
// rows shows. That is how one unset field turned an ENUM's choice list into an empty window: its default
// sort named a presentation column the enum has no physical field for, so the path arrived empty, and
// `SELECT Ref FROM Temp.t0 ORDER BY ` died at the space after BY.
//
// Dropping is the right answer rather than raising: "sort by nothing" HAS a meaning — the source's own
// order — while a refusal would only trade an empty list for an error on lists that are otherwise fine.
// The façade already guarded one of its two doors this way (ibValueSortList::Add writes the composer
// only `if (!field.IsEmpty())`, while its buffer branch stored the empty item that later came here), and
// a guard living in one of two doors is the shape of this defect, not its fix.
// THE COMPARISON A SPELLING MEANS. The imperative door takes the operator as TEXT — it is what a
// script writes and what the old flat store rendered straight into the query — while a filter line
// holds the comparison as a KIND. One place says which is which, so a spelling nobody mapped falls
// back to equality rather than reaching the renderer as a word it cannot spell.
static ibComparisonKind ibComparisonFromOpText(const wxString& op)
{
	const wxString t = op.Strip(wxString::both).Upper();
	if (t == wxT("<>") || t == wxT("!=")) return ibComparisonKind_NotEqual;
	if (t == wxT(">"))                    return ibComparisonKind_Greater;
	if (t == wxT("<"))                    return ibComparisonKind_Less;
	if (t == wxT(">="))                   return ibComparisonKind_GreaterEqual;
	if (t == wxT("<="))                   return ibComparisonKind_LessEqual;
	if (t == wxT("LIKE"))                 return ibComparisonKind_Contains;
	if (t == wxT("IN"))                   return ibComparisonKind_In;
	if (t == wxT("IN HIERARCHY"))         return ibComparisonKind_InHierarchy;
	return ibComparisonKind_Equal;
}

// ⭐⭐ SETTING A FILTER IS SETTING THE READER'S SETTING (Max, 2026-08-24). Not a store of its own:
// what a script asks for, what a metaobject declares at creation and what a person types in the
// settings window are the same fact, and they now live in the same place — so the window shows a
// declared line, and a declared line cannot be silently outranked by a saved one.
ibDataComposer& ibDataComposer::Filter(const wxString& path, const wxString& op, const ibValue& value)
{
	if (path.IsEmpty())
		return *this;
	UserSettings().m_filter.Append(path, ibComparisonFromOpText(op), value);
	return *this;
}

// …AND SCOPING A READ IS NOT. See ScopeTo in the header: the engine's own condition for ONE fetch,
// ANDed with the setting in force and popped when the fetch is done.
ibDataComposer& ibDataComposer::ScopeTo(const wxString& path, const wxString& op, const ibValue& value)
{
	if (path.IsEmpty())
		return *this;
	m_scopeConditions.push_back({ path, op, AddParam(value) });
	return *this;
}

// ===========================================================================
//  A FILTER DESCRIPTION → THE CONDITION IT MEANS
// ===========================================================================
namespace {
// A SIDE is a field or a value. A field becomes a column (its path travels as SEGMENTS — that is
// what the lowering dot-walks to build its joins; one glued string would have to be split again);
// a value becomes a named parameter, which is why this needs the composer at all.
ibQueryAstExprPtr ibBuildFilterSide(ibDataComposer& composer, const ibFilterOperandDescription& side)
{
	if (side.IsField())
		return ibQueryColumnFromPath(side.m_path);

	ibQueryAstExprPtr e = ibQueryAstExpr::Make(ibQueryAstExprKind::Param);
	e->m_paramName = composer.AddParam(side.m_value);
	return e;
}

ibQueryAstExprPtr ibBuildFilterNodes(ibDataComposer& composer,
	const std::vector<ibFilterNodeDescription>& nodes, ibFilterGroupKind kind);

ibQueryAstExprPtr ibBuildFilterCondition(ibDataComposer& composer, const ibFilterNodeDescription& item)
{
	// ⭐⭐ EVERYTHING THAT WAS PASSED IS SUBSTITUTED, AND THE ONLY SWITCH IS THE LINE'S OWN (Max,
	// 2026-08-29: *"whatever value we pass must be substituted — the one exception is the `use` flag
	// standing at false. Empty or not empty makes no difference: we can filter BY an empty value"*, and
	// then: *"Undefined can be in a filter too — you substitute everything"*). `m_use` is read by the
	// caller (ibBuildFilterNodes); nothing here judges what somebody chose.
	//
	// 🛑 THIS USED TO THROW LINES AWAY BY THE VALUE, and by the wrong predicate at that: it asked
	// `IsEmpty()`, which for a BOOLEAN is `false`, for a NUMBER is zero and for a STRING is the empty one
	// (value.cpp). So `Flag = False`, `Quantity = 0`, `Description = ""` and `Counterparty = <empty>` were
	// all discarded as unwritten — everywhere in the product, silently, and indistinguishable from a
	// filter that simply matched nothing. Two questions had one answer: "did somebody write this line"
	// and "is the value they wrote falsy". Only the first one is anybody's business here, and the line
	// answers it itself.

	// CONTAINS IS A LIKE, not a comparison — its own node kind, so the lowering can do what a LIKE
	// needs instead of being handed an operator it has no meaning for.
	const bool isLike = (item.m_comparison == ibComparisonKind_Contains);

	// ⭐⭐ «IN HIERARCHY» IS AN *IN* CARRYING A WORD — one node kind, two comparisons.
	//
	// The AST already holds the unfold word on the In node (queryAst.h: `m_unfold`), and L4 resolves
	// the subtree into the values it stands for before anything below sees it — so both comparisons
	// reuse the whole mechanism by choosing that node, and «in hierarchy» differs from «in» by the
	// word alone. An operator of its own would have been a second way to ask what the language asks.
	const bool isIn = (item.m_comparison == ibComparisonKind_In
	                || item.m_comparison == ibComparisonKind_InHierarchy);

	ibQueryAstExprPtr e = ibQueryAstExpr::Make(isLike ? ibQueryAstExprKind::Like
	                                            : isIn ? ibQueryAstExprKind::In
	                                                   : ibQueryAstExprKind::Compare);
	if (item.m_comparison == ibComparisonKind_InHierarchy)
		e->m_unfold = ibQueryDimUnfold::Hierarchy;

	if (!isLike && !isIn) {
		switch (item.m_comparison) {
		case ibComparisonKind_NotEqual:     e->m_cmp = ibQueryCompareOp::Ne; break;
		case ibComparisonKind_Greater:      e->m_cmp = ibQueryCompareOp::Gt; break;
		case ibComparisonKind_Less:         e->m_cmp = ibQueryCompareOp::Lt; break;
		case ibComparisonKind_GreaterEqual: e->m_cmp = ibQueryCompareOp::Ge; break;
		case ibComparisonKind_LessEqual:    e->m_cmp = ibQueryCompareOp::Le; break;
		default:                            e->m_cmp = ibQueryCompareOp::Eq; break;
		}
	}

	e->m_lhs = ibBuildFilterSide(composer, item.m_left);

	// An IN takes a LIST, not a right-hand operand: one entry here, because a filter line holds one
	// value. Where that value is itself a list (an array chosen in the cell) the lowering flattens it
	// — the node is already the set-valued one.
	if (isIn)
		e->m_list.push_back(ibBuildFilterSide(composer, item.m_right));
	else
		e->m_rhs = ibBuildFilterSide(composer, item.m_right);
	return e;
}

ibQueryAstExprPtr ibBuildFilterNodes(ibDataComposer& composer,
	const std::vector<ibFilterNodeDescription>& nodes, ibFilterGroupKind kind)
{
	// LEFT-FOLDED into binary Logical nodes — the AST has no n-ary AND, and the fold is what a
	// reader expects: `a AND b AND c` groups as `(a AND b) AND c`.
	ibQueryAstExprPtr acc;
	for (const ibFilterNodeDescription& item : nodes) {
		if (!item.m_use)
			continue;   // switched off — as if it were not written
		ibQueryAstExprPtr child = item.m_kind == ibFilterNodeKind_Group
			? ibBuildFilterNodes(composer, item.m_children, item.m_groupKind)
			: ibBuildFilterCondition(composer, item);
		if (!child)
			continue;
		if (!acc) {
			acc = child;
			continue;
		}
		ibQueryAstExprPtr joined = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		joined->m_isOr = (kind == ibFilterGroupKind_Or);
		joined->m_lhs = acc;
		joined->m_rhs = child;
		acc = joined;
	}

	if (!acc || kind != ibFilterGroupKind_Not)
		return acc;

	// NOT negates the WHOLE group, not its first child.
	ibQueryAstExprPtr negated = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
	negated->m_lhs = acc;
	return negated;
}
} // namespace

ibQueryAstExprPtr ibDataComposer::BuildFilterAst(const ibFilterDescription& filter)
{
	return ibBuildFilterNodes(*this, filter.m_nodes, filter.m_rootKind);
}

// ⭐⭐ AN ASSIGNMENT, AND THAT IS THE WHOLE OF IT (Max, 2026-08-23, more than once: "we apply a
// setting by plain assignment — you set the section's value, and everything that was in it before is
// dropped by the fact that you wrote `=`").
//
// Nothing is cleared because there is nothing to clear: the filter, the sort and the grouping are
// not stored twice. They ARE this setting, and the render reads them from here. A door that
// translated a setting into some other shape is what made "clear first" necessary in the first
// place — and the moment between the clearing and the re-filling is exactly where half of one
// setting stood beside half of another.
ibDataComposer& ibDataComposer::SetUserSettingsDesc(const ibSettingsDescription& settings)
{
	m_userSettings = settings;
	m_readerHasSetting   = true;   // …and from now on this setting answers for every part, empty ones included
	return *this;
}

// …AND DROPPING IT IS THE RESET. `m_variants[0]`'s setting composes again, and nothing had to be
// remembered to make that happen.
ibDataComposer& ibDataComposer::ClearUserSettings()
{
	// …EMPTIED, which IS the reset: with nothing in it, every part comes from the zeroth again.
	// Nothing is remembered to undo, and there is no second mechanism.
	m_userSettings.Clear();
	m_readerHasSetting = false;   // …and there is no reader's setting again, which is what "reset" means
	return *this;
}

// ⭐⭐ THE VARIANTS COME IN AS A COPY, AND THEY ONLY EVER GO OUT AS CONST (Max, 2026-08-24: *"the
// variants are not changed through the settings — you hand them out as a constant, they are only
// copied; the user setting is the one you take and set"*). That is why there is no mutable door to
// an element of this array anywhere on this class: a settings window COPIES what composes, edits the
// copy, and hands it back as the READER's setting. The author's array changes in the designer, on
// the description, and reaches a composer only through here.
//
// A record can say anything; the invariant that `[0]` exists is ours.
ibDataComposer& ibDataComposer::LoadVariants(const std::vector<ibVariantDescription>& variants)
{
	m_variants = variants;
	if (m_variants.empty())
		m_variants.emplace_back();
	return *this;
}

// ⭐⭐ STATING THE ORDER IS SETTING THE READER'S SORT. A column heading clicked, `Sort()` from a
// script, a value table asked to sort — all three are somebody saying "order it this way, now", and
// that is the reader's section by definition. It used to be a flat store the render preferred the
// setting over, so all three did nothing at all on a list that had one (see ClearSorts).
ibDataComposer& ibDataComposer::Sort(const wxString& path, bool ascending)
{
	if (path.IsEmpty())
		return *this;   // see the note above Filter
	UserSettings().m_sort.Append(path, ascending);
	return *this;
}

ibDataComposer& ibDataComposer::Resource(const wxString& func, const wxString& path)
{
	// A RESOURCE WITH NOTHING TO AGGREGATE is the absence of one — the same rule the sort and the
	// filter follow above. It would render `SUM()` (or a bare comma with the expression form) and
	// the parser would refuse the whole query, which the caller then sees as an empty result
	// rather than as the empty setting it actually was.
	if (path.IsEmpty())
		return *this;
	m_resources.push_back({ func, path });
	return *this;
}

ibDataComposer& ibDataComposer::TotalBy(const wxString& path, ibQueryDimUnfold kind)
{
	if (path.IsEmpty())
		return *this;   // see the note above Filter
	// A LEVEL, not a field: "group by this, then by that". Welding several fields into ONE heading
	// is the report's own act and it happens in the grouping form, over the level's field list.
	AppendLevel(path, kind);
	return *this;
}

ibDataComposer& ibDataComposer::Parameter(const wxString& name, const ibValue& value)
{
	m_params[name] = value;
	return *this;
}

ibDataComposer& ibDataComposer::ClearSettings()
{
	m_commonSelected.clear();
	m_scopeConditions.clear();
	// THE FILTER AND THE ORDER ARE THE READER'S — see Filter() and Sort(). Dropping their setting
	// altogether is what "back to the defaults" means: `m_variants[0]` composes again.
	ClearUserSettings();
	m_resources.clear();
	TrimLevels(0);
	return *this;
}

//////////////////////////////////////////////////////////////////////
// render
//////////////////////////////////////////////////////////////////////

// The nested source's alias when the settings are written over an author's query. ASCII, a legal
// identifier, and unlikely to collide with anything a person names a table.
static const wxChar* kAuthorQuerySource = wxT("AuthorQuery");

const wxChar* ibDataDBComposer::AuthorQuerySourceName() { return kAuthorQuerySource; }

// ⭐⭐ WHAT THE SELECT LIST IS WHEN NOBODY HAS CHOSEN A FIELD — and the answer is not one answer,
// because it is not one question (see ibDataComposer::ReadsEveryField).
//
//   * a LIST reads everything: its columns ARE its source's. Over one source that is a star; over a
//     LINK's join tree it is the qualified list worked out with the split, because a star there
//     publishes two selections' columns under one set of BARE names and two `Qty` answer to one.
//   * a REPORT reads what it folds BY, and nothing else. "Nothing selected" means nothing is shown
//     beside the groupings, and the fields nobody named do not travel at all — which is where the
//     query gets smaller (Max, 2026-08-28).
//
// ⚠ A REPORT WITH NO GROUPINGS EITHER falls back to the star, and does not have to be guarded: a
// composition that names no field, folds by nothing and states no resource has no settings at all,
// so the verbatim road above is taken and this is never reached.
wxString ibDataDBComposer::WhenNothingChosen(const Output& output) const
{
	if (!ReadsEveryField()) {
		wxString grouped;
		for (const wxString& name : ibComposerGroupingFieldsOf(output)) {
			if (!grouped.IsEmpty())
				grouped += wxT(", ");
			grouped += name;
		}
		if (!grouped.IsEmpty())
			return grouped;
	}
	return m_resolved.m_allFields.IsEmpty() ? wxString(wxT("*")) : m_resolved.m_allFields;
}

// ===========================================================================
//  WHAT IS SHOWN — the selected-fields table, resolved
// ===========================================================================
//
// ⭐ THE BODIES LIVE HERE, NOT IN THE HEADER (Max, 2026-08-28). dataComposer.h is included by the
// whole backend and by the frontend, so a static helper only this file uses cost every one of them a
// full rebuild each time a line of it changed — four in one evening. What the header states is what
// other TIERS ask for; the machinery of an answer belongs beside the answer.
// (NOT anonymous any more: these are the module.s own vocabulary and the aspect files below call
//  them — see dataComposerInternal.h, where they are stated.)

// ⭐⭐ DOES THIS COLUMN ANSWER TO THIS PATH — and the two are NOT spelled the same.
//
// A path is what a person picked: `Ref.Date`, `Sales.Qty`. The OUTPUT NAME the engine gives it drops
// a leading source qualifier and CONCATENATES the walk (`ibQueryProposedName`): `RefDate`, `Qty`.
// Comparing the two as strings therefore misses every dot-walk — and a row whose every cell was
// blanked is a row the printer does not draw at all, so a detail level printed nothing while its
// fields were plainly in the list (found live, 2026-08-28).
//
// ⚠ BOTH READINGS ARE ACCEPTED because the composer cannot tell here which segment named a source:
// `Sales.Qty` over a linked package drops `Sales`, while `Ref.Date` over one source keeps `Ref`.
// Asking the query tier would mean holding its select; accepting both spellings of one name costs
// nothing and cannot say yes to a different field — the segments are the same words in the same
// order either way.
bool ibComposerColumnAnswersTo(const ibQueryLowering::OutputColumn& oc, const wxString& path)
{
	if (path.IsEmpty())
		return false;
	if (oc.m_name.IsSameAs(path, false) || oc.m_alias.IsSameAs(path, false))
		return true;

	wxString joined, tail;
	bool afterFirst = false;
	for (size_t at = 0; at < path.length(); ) {
		const size_t dot  = path.find(wxT('.'), at);
		const size_t stop = dot == wxString::npos ? path.length() : dot;
		const wxString segment = path.Mid(at, stop - at);
		joined += segment;
		if (afterFirst)
			tail += segment;
		afterFirst = true;
		at = stop + 1;
	}
	return oc.m_name.IsSameAs(joined, false) || oc.m_alias.IsSameAs(joined, false)
	    || (!tail.IsEmpty() && (oc.m_name.IsSameAs(tail, false) || oc.m_alias.IsSameAs(tail, false)));
}

// ⭐⭐ ONE TABLE, RESOLVED AGAINST WHAT IS IN FORCE ABOVE IT — the whole of the inheritance rule.
//
//   * an EMPTY table inherits — a node nobody has touched shows what the storey above shows, and
//     that is the state every node starts in;
//   * an `Auto` row is WHERE the inherited set lands, so a node may put its own fields before it,
//     after it, or on both sides;
//   * a table with NO `Auto` row states this node's composition whole — and its children then
//     inherit THAT, because it is the storey above them (Max, 2026-08-28: "the children further down
//     will now take from THIS element"). Refusing to inherit does not break the chain; it makes this
//     node its new beginning.
void ibComposerResolveSelected(std::vector<wxString>& into,
                     const std::vector<ibSelectedFieldDescription>& rows,
                     const std::vector<wxString>& inherited)
{
	if (rows.empty()) {
		ibDataComposer::AppendFields(into, inherited);
		return;
	}
	for (const ibSelectedFieldDescription& row : rows) {
		if (row.IsAuto())
			ibDataComposer::AppendFields(into, inherited);
		else if (!row.m_path.IsEmpty())
			ibDataComposer::AppendFields(into, { row.m_path });
	}
}

// ⭐ WHAT A REPORT READS WHEN NOBODY HAS CHOSEN A FIELD — the fields it groups BY, and nothing else.
// Not a star: the whole point of an empty selection is that nothing extra is shown, and a report
// still has to read what it folds by.
//
// (The RESOURCES are not here and do not need to be: an aggregate is resolved against the SOURCES,
//  not against this list, so `TOTALS SUM(Amount)` folds a column the projection never names. What
//  must be here is what the fold groups by, because a heading prints its key.)
static void AppendGroupingFields(std::vector<wxString>& into, const ibDataComposer::GroupNode& level)
{
	for (const ibGroupLineDescription& line : level.m_settings.m_group.m_lines)
		if (!line.m_path.IsEmpty())
			ibDataComposer::AppendFields(into, { line.m_path });
	for (const ibDataComposer::GroupNode& child : level.m_children)
		AppendGroupingFields(into, child);
}

std::vector<wxString> ibComposerGroupingFieldsOf(const ibDataComposer::Output& output)
{
	std::vector<wxString> fields;
	for (const std::vector<ibDataComposer::GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
		for (const ibDataComposer::GroupNode& level : *axis)
			AppendGroupingFields(fields, level);
	return fields;
}

// WHAT IS IN FORCE UNDER A NODE, given what was in force above it. The chain is CARRIED by whoever
// walks the tree — it is not worked out by climbing back up, for the same reason a branch is a fact
// about the node rather than a state of the walker: the walk already holds it.
std::vector<wxString> ibComposerSelectedUnder(const std::vector<wxString>& above,
                                    const ibDataComposer::GroupNode& level)
{
	std::vector<wxString> here;
	ibComposerResolveSelected(here, level.m_selected, above);
	return here;
}

// ONE NODE AND EVERYTHING UNDER IT — what it shows, what it hides on, what it orders by, and the
// same three of every node beneath. `above` is the set in force where this node stands.
//
// 🛑 THE CHILDREN WERE NOT WALKED AT ALL. Only the top level of each axis was read, so a field
// selected on a nested grouping — or its filter's column, or its sort key — never reached the query.
// The setting saved, travelled through variants and meant nothing.
static void CollectProjection(std::vector<wxString>& into, const std::vector<wxString>& above,
                       const ibDataComposer::GroupNode& level)
{
	const std::vector<wxString> here = ibComposerSelectedUnder(above, level);
	ibDataComposer::AppendFields(into, here);
	for (const ibFilterNodeDescription& node : level.m_settings.m_filter.m_nodes)
		ibDataComposer::AppendFilterFields(into, node);
	for (const ibSortLineDescription& line : level.m_settings.m_sort.m_lines)
		if (!line.m_path.IsEmpty())
			ibDataComposer::AppendFields(into, { line.m_path });
	for (const ibDataComposer::GroupNode& child : level.m_children)
		CollectProjection(into, here, child);
}



// ⭐ WHICH WAY THE RECORDS READ. A record declared on the COLUMN axis is a column of its own —
// "exactly as a detail record is in the rows, so in the columns" (Max, 2026-08-26) — and the fold
// has to be told, because the level itself is written last either way. Rows when nobody declared
// one: the records are read regardless, and down the page is where a report puts them.
// ⭐⭐ THE LADDER, DERIVED FROM THE SETTING — see the declaration for why it lives here and not in the
// widget that used to build it.
void ibDataComposer::BuildPrintLevels(bool tree, const ibBackendQueryable* source)
{
	if (!tree) {
		// A FLAT VIEW IS A FLAT READ, and it wins over a stored grouping — the same rule the model's
		// own paging follows (a flat List view passes the ignore-parent sentinel and the grouping is
		// off). Said once, here, so the two roads cannot answer it differently.
		ibSettingsDescription flat = GetCurrentSettingsDesc();
		flat.m_group.Clear();
		SetUserSettingsDesc(flat);
		TrimLevels(0);
		return;
	}

	// AN AUTHOR'S LADDER STANDS AS IT IS. A report declares its levels; this is for a composition
	// whose structure lives in a SETTING, which is what a list is.
	if (!LevelChain().empty())
		return;

	// WHAT THE SOURCE CALLS ITS OWN ROW, and whether it has a tree at all. Both are facts about the
	// source, so both are asked of it — a widget cannot know them and should never have been asked.
	wxString identity;
	bool     hasTree = false;
	if (source != nullptr) {
		const std::vector<const ibBackendQueryColumn*> key = source->GetPrimaryKeyColumns();
		if (key.size() == 1 && key.front() != nullptr) {
			identity = key.front()->GetName();
			hasTree  = source->GetHierarchyColumn() != nullptr;
		}
	}

	// ⭐⭐ A TREE RUNG IS A RUNG — ANY NUMBER OF THEM, IN ANY ORDER. A rung unfolds ITS OWN reference, so
	// a counterparty tree with a goods tree inside each of them is two recursions that never meet: each
	// stands on a different field (Max, 2026-08-29: *"we can output several hierarchies, and the order does
	// not matter"*).
	//
	// 🛑 TWO RULES WERE PROPOSED HERE AND BOTH ARE WRONG, which is worth keeping so neither comes back:
	//   * "the tree is always LAST" — a working report of the same shape refutes it: counterparty → goods
	//     (tree) → period, the tree in the MIDDLE with an ordinary grouping hanging off its concrete elements.
	//   * "there is only ONE tree" — I argued it from "a row nests one way", which is true only of two rungs
	//     over the SAME field. Over two different references there is no conflict at all.
	// What is genuinely impossible is the same field twice — a level repeated is a fold repeated — and that
	// is checked below, where the source's own tree is offered.
	bool identityNamed = false;
	for (const ibGroupLineDescription& line : GetCurrentGroupDesc().m_lines) {
		if (line.m_path.IsEmpty())
			continue;
		AppendLevel(line.m_path, line.m_kind);
		identityNamed = identityNamed || (!identity.IsEmpty() && line.m_path == identity);
	}

	// ⭐ THE TREE IS A LEVEL OVER THE ROW'S OWN REFERENCE, and it stands BESIDE a grouping rather
	// than instead of one: the groupings a person set fold first, the source's own tree inside each.
	// …only where there IS a tree, and only if that field is not already the deepest level — a level
	// repeated is a fold repeated.
	if (hasTree && !identityNamed && !identity.IsEmpty())
		AppendLevel(identity, ibQueryDimUnfold::Hierarchy);

	if (LevelChain().empty())
		return;   // nothing folded — every row is a record already, and a ladder of one records level is not one

	// ⭐⭐ …AND A LEVEL OF RECORDS AT THE BOTTOM. Headings are what a fold is FOR, so an output always
	// writes them; ROWS are written only by an output whose ladder NAMES them.
	//
	// 🛑 A RULE OF MY OWN STOOD HERE AND IS REMOVED. It skipped this level whenever the deepest rung
	// grouped by the row's identity, reasoning that such a node IS the row and a record under it prints
	// the same line twice. The reasoning is right and the PLACE is wrong: the fold already states it,
	// per level, where it can SEE the level's key (`keyIsTheRow`, queryProvider.cpp). Being a second
	// answer it was also a worse one — it fired on spellings the fold does not, and emptied every
	// heading of its own fields. Two builds, two wrong shapes, no gain (Max, 2026-08-29: *"roll them
	// back, they carry no value"*).
	GroupNode records;
	records.m_kind = ibCompositionLevelKind::Details;
	LevelChain().push_back(std::move(records));
}

ibTotalsAxis ibDataComposer::DetailAxisOf(const Output& output)
{
	for (const GroupNode& level : output.m_columnGroups)
		if (level.IsDetailRecords())     // …asked the one way — see IsDetailLevel
			return ibTotalsAxis::Columns;
	return ibTotalsAxis::Rows;
}

const ibDataComposer::GroupNode* ibDataComposer::DetailLevelOf(const Output& output)
{
	for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
		for (const GroupNode& level : *axis)
			if (level.IsDetailRecords())
				return &level;
	return nullptr;
}

std::vector<wxString> ibDataComposer::SelectedFor(const Output& output) const
{
	// 🛑 THE BASE GOES THROUGH THE SAME SIEVE. Taking it as it stands let a duplicate that was
	// already inside it reach the SELECT list, and a derived table refuses two columns of one name:
	// "column FLD1022_TYPE was specified multiple times for derived table Q_SUB0" (Firebird -104,
	// measured 2026-08-24). A field named twice is named once.
	//
	// ⭐⭐ FOUR STOREYS, AND EACH ONE RESOLVES AGAINST THE ONE ABOVE IT:
	//
	//   composition (the AUTHOR) → the setting in force (the READER) → output → node
	//
	// The reader's storey is what makes "I want to see these columns" a setting rather than a change
	// to the report — the same division the filter and the sort already have.
	//
	// THE COMPOSITION SPEAKS FIRST, and it has nothing above it: an `Auto` row there stands for
	// nothing and simply contributes nothing.
	// ⚠ …AND THE AUTHOR'S OWN TABLE IS THE SAFETY NET, NOT A FLOOR UNDER THE READER. While nobody has
	// set anything it is what the report shows; once a reader HAS a setting, theirs answers — and an
	// empty table of theirs means empty. Seeding the pile with the author's rows made "I removed the
	// columns" unsayable: an empty reader storey inherits what it is piled on (Max, 2026-08-29).
	std::vector<wxString> base;
	if (!ReaderHasSetting())
		ibComposerResolveSelected(base, m_commonSelected, {});

	// ⚠ ASKED OF THE PART, not of the whole setting: GetCurrentSettingsDesc assembles a COPY of every
	// section, and this is called once per output and again for the projection.
	std::vector<wxString> byReader;
	ibComposerResolveSelected(byReader, GetCurrentSelectedDesc(), base);

	std::vector<wxString> selected;
	ibComposerResolveSelected(selected, output.m_selected, byReader);
	// WHAT A NODE NAMES IS **NOT** HERE — deliberately. This is what the report SHOWS down to the
	// output, and a node's own fields are what IT shows.
	return selected;
}

std::vector<wxString> ibDataComposer::SelectedFor(const Output& output, const GroupNode& level) const
{
	return ibComposerSelectedUnder(SelectedFor(output), level);
}

// ⭐ WHAT IS SHOWN — the same walk as the projection below, minus what is only READ. A filter and a
// sort name fields the query must fetch; a report does not print them for that.
static void CollectShown(std::vector<wxString>& into, const std::vector<wxString>& above,
                         const ibDataComposer::GroupNode& level)
{
	const std::vector<wxString> here = ibComposerSelectedUnder(above, level);
	ibDataComposer::AppendFields(into, here);
	for (const ibDataComposer::GroupNode& child : level.m_children)
		CollectShown(into, here, child);
}

std::vector<wxString> ibDataComposer::ShownFor(const Output& output) const
{
	const std::vector<wxString> atOutput = SelectedFor(output);
	std::vector<wxString> shown = atOutput;
	for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
		for (const GroupNode& level : *axis)
			CollectShown(shown, atOutput, level);
	return shown;
}

std::vector<wxString> ibDataComposer::ProjectionFor(const Output& output) const
{
	const std::vector<wxString> atOutput = SelectedFor(output);
	std::vector<wxString> selected = atOutput;
	for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
		for (const GroupNode& level : *axis)
			CollectProjection(selected, atOutput, level);
	return selected;
}

wxString ibDataDBComposer::RenderText() const
{
	// THE FIRST OUTPUT is what "the composer's query" has always meant — a list has exactly one.
	return RenderTextFor(Root());
}

// RENDER ONE OUTPUT: its own levels, its own filter and sort, its own selected fields. The
// RESOURCES are the composition's and every output rolls the same ones, which is why they are read
// off `m_resources` here rather than off the output.
wxString ibDataDBComposer::RenderTextFor(const std::vector<const Output*>& outputs) const
{
	// The settings that are not a ladder belong to all of them alike — see AppendSettingsClauses.
	const Output& output = *outputs.front();

	// ⭐ EVERY BRANCH'S FIELDS, ONCE. One read has one projection, so what it publishes is the UNION
	// of what the branches asked for; each of them prints its own out of that (SchemaFor). Deduped by
	// NAME, because two outputs naming the same field are naming one column — projecting it twice
	// would publish two columns answering to one name, and a reader would get whichever came first.
	std::vector<wxString> projected;
	for (const Output* out : outputs) {
		for (const wxString& name : ProjectionFor(*out)) {
			bool already = false;
			for (const wxString& have : projected)
				if (have.IsSameAs(name, false)) { already = true; break; }
			if (!already)
				projected.push_back(name);
		}
	}
	// Anything asked of this read — by the composition above it or by the output itself. Miss one
	// and the author's verbatim text is handed back unchanged, with the setting silently dropped.
	const bool hasSettings = !ProjectionFor(output).empty()
	                       || output.m_settings.m_filter.IsOk() || output.m_settings.m_sort.IsOk()
	                       || !m_scopeConditions.empty()
	                       || !m_resources.empty() || !ChainFrom(output).empty()
	                       || GetCurrentSettingsDesc().IsOk();   // …and whatever composes

	// ⚠ THE VERBATIM ROAD IS FOR ONE QUERY ONLY. Handing a PACKAGE back untouched reads as "nothing
	// was asked of it", and something was: the `LINK` section is not a statement, so whoever parses
	// the text afterwards takes its last SELECT and the relations are silently gone — a smaller
	// answer, quietly. A package is therefore always stood on (SplitSourceText), settings or not.
	if (!m_sourceText.IsEmpty()) {
		SplitSourceText();
		if (!hasSettings && m_resolved.m_preamble.IsEmpty())
			return m_sourceText;   // the author's text, verbatim — nothing is being asked of it
	}

	// ⭐ SETTINGS OVER AN AUTHOR'S QUERY — the seam this used to refuse.
	//
	// The author's query is not edited. It becomes a NESTED SOURCE, and the filter, the sort and the
	// grouping are written OVER it:
	//
	//     SELECT * FROM (<the author's query>) AS AuthorQuery WHERE … ORDER BY … TOTALS …
	//
	// which is the only reading that is right in every case: a WHERE injected INTO the author's text
	// would run before their own aggregates, their DISTINCT and their TOP, and would quietly answer a
	// different question than the one the user typed into the filter.
	//
	// Nothing new was needed for it. A subquery source round-trips (queryRender / queryParser), the
	// lowering realises it (WrapSelectAsQueryable), and the optimizer's FROM-subquery flattening folds
	// the plain case straight back into ONE server-side SELECT — so a simple author query with a
	// filter costs exactly what it did before there was a filter. (queryRewrite.h rule 2.)
	//
	// The paths the settings name are the query's OUTPUT names, which is what a host offers a person
	// to pick from (a dynamic list vends them as its column collection) — the same names, both sides.
	if (!m_sourceText.IsEmpty()) {
		wxString authorProj;
		for (const wxString& name : ProjectionFor(output)) {
			if (!authorProj.IsEmpty())
				authorProj += wxT(", ");
			authorProj += name;
		}
		// ⭐ …AND A PACKAGE IS NOT WRAPPED, IT IS STOOD ON. `(SELECT …; SELECT …)` is not a query, so
		// the statements that PREPARE stay ahead of the composer's own select and what it reads FROM
		// is what the package produces — the linked selections, or its last statement. See
		// SplitSourceText: this is the only line that has to know there is a difference.
		SplitSourceText();

		// No explicit selection: everything the nested query yields. `*` and not a reflected list,
		// because the nested query's columns are ITS business — asking what they are would mean
		// resolving the text here, and the lowering is about to do that anyway.
		wxString text;
		if (!m_resolved.m_preamble.IsEmpty())
			text = m_resolved.m_preamble + wxT("\n;\n");
		text += wxT("SELECT ") + (authorProj.IsEmpty() ? WhenNothingChosen(output) : authorProj)
			+ wxT("\nFROM ") + m_resolved.m_from;
		AppendSettingsClauses(text, outputs);
		return text;
	}

	if (m_sources.empty())
		ibBackendCoreException::Error(_("Composer: no source is set"));
	// (RESOURCES WITH NO GROUPING ARE NO LONGER REFUSED. They are a grand total, and the clause
	//  writer says so with `BY OVERALL` — see AppendSettingsClauses. Refusing them was a rule about
	//  what the RENDERER could write, stated as if it were a rule about what a report may ask for.)
	//
	// NB: TotalBy WITHOUT an aggregate is valid — "TOTALS BY <dim>" is a pure grouping
	// / hierarchy with no rolled aggregate (a list grouped by a field).

	// --- the projection -------------------------------------------------------
	// (`projected` was worked out at the top: the UNION of what every branch asks for, deduped by
	//  name. One read has one projection, whether it serves one output or several.)
	wxString proj;
	if (!projected.empty()) {
		for (const wxString& name : projected) {
			if (!proj.IsEmpty())
				proj += wxT(", ");
			proj += name;
		}
	}
	else {
		// ⭐ A REPORT READS WHAT IT FOLDS BY, not the whole table. The reflection below is the LIST's
		// answer — its columns are its source's — and asking the factory for every column of a table
		// nobody chose from is exactly the waste the selected-fields table exists to remove.
		//
		// It fills the same `proj` the branch above fills, so the FROM and the joins below are built
		// once, by the code that already knows how.
		if (!ReadsEveryField()) {
			for (const wxString& name : ibComposerGroupingFieldsOf(output)) {
				if (!proj.IsEmpty())
					proj += wxT(", ");
				proj += name;
			}
		}

		// No explicit selection: ALL the (single) source's columns — READ-ONLY
		// reflection through the factory; execution still flows through the text.
		if (proj.IsEmpty() && m_sources.size() > 1)
			ibBackendCoreException::Error(_("Composer: joined sources need an explicit Select list"));

		const Source& s0 = m_sources.front();

		// A TRANSIENT (RAM / temp) source registered via FromSource(queryable) — reflect its
		// columns straight off the live queryable (the factory carries no descriptor for it).
		// Otherwise the factory resolves the metaobject source by name (READ-ONLY dictionary).
		// …and the source is only asked when the groupings above did not answer: a report that folds
		// by something has already said what it reads.
		const ibBackendQueryable* src = nullptr;
		if (proj.IsEmpty()) {
		const auto dit = m_directSources.find(s0.m_name);
		if (dit != m_directSources.end())
			src = dit->second;
		else {
			// Resolve through THIS query's OWN config factory (per-config sources); no config → the global base.
			ibQueryableFactory* factory = m_metaData != nullptr ? m_metaData->GetSourceFactory() : nullptr;
			if (factory == nullptr)
				factory = ibApplicationData::GetQueryableFactory();
			if (factory == nullptr)
				ibBackendCoreException::Error(_("Composer: the query engine is not available (no application data)"));
			src = factory->Resolve(s0.m_namespace, s0.m_name);
		}
		if (src == nullptr)
			ibBackendCoreException::Error(_("Composer: unknown source '%s.%s'"), s0.m_namespace, s0.m_name);

		for (const ibBackendQueryColumn* col : src->GetColumns()) {
			if (col == nullptr || col->GetName().IsEmpty())
				continue;
			if (!proj.IsEmpty())
				proj += wxT(", ");
			proj += col->GetName();
		}
		if (proj.IsEmpty())
			ibBackendCoreException::Error(_("Composer: source '%s.%s' exposes no columns"), s0.m_namespace, s0.m_name);
		}
	}

	// --- the clauses ----------------------------------------------------------
	wxString text = wxT("SELECT ") + proj
		+ wxT(" FROM ") + m_sources[0].m_namespace + wxT(".") + m_sources[0].m_name;

	// Additional sources: the language's auto-join-by-reference (ON omitted).
	for (size_t i = 1; i < m_sources.size(); ++i)
		text += wxT(" JOIN ") + m_sources[i].m_namespace + wxT(".") + m_sources[i].m_name;

	AppendSettingsClauses(text, outputs);
	return text;
}

// WHERE / ORDER BY / TOTALS — the settings, written the same way whichever source they stand over.
// A composed source and an author's query differ in their FROM and in nothing else: the filter a
// person set is the same filter, and two spellings of it would be two chances to drift.
// ⭐ ONE GROUPING LINE, WRITTEN AS THE QUERY SAYS IT — `<path> [HIERARCHY] [PERIODS(unit, from, to)]`.
//
// The FORM is `ibRenderTotalField`'s (queryRender.cpp) and this is the same sentence written from
// the stored side: a description holds the bounds as TEXT (a description goes to a file, an
// expression tree does not), so there is nothing to render them from — they are already what the
// author typed, usually `&Parameter`.
//
// ⚠ A BOUND LEFT OUT MEANS "FROM THE DATA" and keeps its position: `PERIODS(Month, , &To)`. Filling
// one in would be this writer answering a question the person left open.
static void ibAppendGroupLine(wxString& text, const ibGroupLineDescription& line)
{
	text += line.m_path;
	if (line.m_kind == ibQueryDimUnfold::Hierarchy)
		text += wxT(" ") + ibQueryKeywordText(ibQueryKeyword::Hierarchy);
	else if (line.m_kind == ibQueryDimUnfold::HierarchyOnly)
		text += wxT(" ") + ibQueryKeywordText(ibQueryKeyword::HierarchyOnly);

	if (!line.m_periods.IsOk())
		return;
	text += wxT(" ") + ibQueryKeywordText(ibQueryKeyword::Periods) + wxT("(") + line.m_periods.m_unit;
	if (!line.m_periods.m_from.IsEmpty())
		text += wxT(", ") + line.m_periods.m_from;
	if (!line.m_periods.m_to.IsEmpty())
		text += (line.m_periods.m_from.IsEmpty() ? wxT(", , ") : wxT(", ")) + line.m_periods.m_to;
	text += wxT(")");
}

// ⭐⭐ THE OUTPUTS THIS TEXT IS WRITTEN FOR — one of them in the ordinary case, several when they
// share the read as BRANCHES. ONE list and not "an output plus the others": the first output is not
// a different kind of thing from the rest, and saying it twice would leave two places to disagree
// about which of them the WHERE and the ORDER BY came from (Max, 2026-08-27, on the first shape of
// this: why does it take the array of outputs AND an output of its own?).
//
// Everything that is NOT a ladder — the scope filter, the order, the resources — is shared by
// construction: outputs only ever share a read when those already agree (BranchableOutputs), so
// they are read off the first and no branch can quietly carry its own.
// ⭐ ONE CONDITION INTO A QUERY — declared where the two roads a setting travels are described.
// A null condition is the ordinary case (nothing was filtered) and must read as "leave it alone",
// so every caller can hand over whatever BuildFilterAst gave it without asking first.
void ibDataDBComposer::AndWhere(ibQuerySelect& ast, const ibQueryAstExprPtr& condition)
{
	if (condition == nullptr)
		return;
	if (ast.m_where == nullptr) {
		ast.m_where = condition;
		return;
	}
	ibQueryAstExprPtr both = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
	both->m_isOr = false;
	both->m_lhs  = ast.m_where;
	both->m_rhs  = condition;
	ast.m_where  = both;
}

void ibDataDBComposer::AppendSettingsClauses(wxString& text, const std::vector<const Output*>& outputs) const
{
	const Output& output = *outputs.front();
	// ⭐ WHERE A FILTER SITS DECIDES WHAT IT DOES (Max):
	//
	//   * ON THE OUTPUT — it REMOVES ROWS. This is the report's subject ("only this warehouse"), and
	//     everything, totals included, is computed over what is left. That is the WHERE below.
	//   * ON A LEVEL — it HIDES HEADINGS, and the rows keep counting. Deleting five rows three
	//     levels down would move the grand total, and a total that shifts because somebody tidied a
	//     sub-heading is a number nobody can defend. Applied on the WALK instead (see RunOutput).
	//
	// So a level's filter is deliberately NOT written here.
	// THE COMPOSITION'S OWN FILTER FIRST, then the output's. Both exclude; the first excludes for
	// every output there is, so no output can see more than it admits.
	bool wrote = false;
	auto appendFilters = [&text, &wrote](const std::vector<FilterItem>& filters) {
		for (const FilterItem& f : filters) {
			text += (wrote ? wxT(" AND ") : wxT(" WHERE "));
			text += f.m_path + wxT(" ") + f.m_op + wxT(" &") + f.m_param;
			wrote = true;
		}
	};
	appendFilters(m_scopeConditions);
	// (The output's OWN filter is a description now, and it is ANDed into the parsed AST rather than
	//  written into the text — see RunOutput. A tree condition is never rendered and re-parsed.)

	// THE ORDER: the output's own when it stated one, otherwise the one in force. A level's sort
	// orders that level's headings and is applied on the walk — it is not this clause.
	//
	// ⭐ AND WHICHEVER IT IS, IT REPLACES THE OTHER WHOLE. A setting is one thing; taking its sort
	// and somebody else's filter would be running on a setting nobody wrote. In force = the reader's
	// if they set one, else the author's.
	//
	// 🛑 THERE WAS A THIRD ANSWERER HERE — the flat `m_commonSorts`, reached only when neither
	// section said anything. Everything the imperative door wrote went there, so a click on a column
	// heading was ignored the moment the list had a sort setting (2026-08-24). One store now: the
	// door writes the reader's section, so the two cannot disagree.
	const ibSortDescription& order = output.m_settings.m_sort.IsOk()
		? output.m_settings.m_sort : GetCurrentSortDesc();
	size_t written = 0;
	std::vector<wxString> sorted;   // what this clause already names — a key written twice is refused
	auto writeKey = [&](const wxString& path, bool ascending) {
		if (path.IsEmpty())
			return;   // a line with no field is the absence of one — see Sort()
		for (const wxString& already : sorted)
			if (already.IsSameAs(path, false))
				return;
		sorted.push_back(path);
		text += (written++ == 0 ? wxT(" ORDER BY ") : wxT(", "));
		text += path;
		if (!ascending)
			text += wxT(" DESC");
	};
	for (const ibSortLineDescription& line : order.m_lines)
		writeKey(line.m_path, line.m_ascending);

	// ⭐⭐ AND A LEVEL'S OWN SORT COMES HERE TOO — when its key is a FIELD.
	//
	// 🛑 It was answered on the WALK, beside the level's filter, and for a filter that is right: the
	// fold has run, and hiding a heading changes nothing above it. A SORT is not like that. A heading
	// carries the level's key and the figures rolled into it — and nothing else — so ordering the
	// headings by an ordinary field (a document's date, its moment, `Ref.Anything`) had nothing to
	// read and left the order exactly as it was (Max, 2026-08-25, over documents in identifier
	// order: *"an ordinary date does not sort them either"*).
	//
	// The fold keeps a group where its FIRST ROW is, so the honest way to order headings by a field
	// of the rows is to order the ROWS by it. That is this clause. The levels' keys follow it
	// (queryLowering) as the tie-break, so the grouping still holds together.
	//
	// ⚠ IN LEVEL ORDER, outer first: the keys are read in the order they are written, so an outer
	// level's sort decides the sequence of its headings and an inner one only arranges what sits
	// under each of them — which is what a ladder of settings means.
	//
	// A key that names a RESOURCE stays out: an aggregate does not exist on the detail read, and it
	// is the one thing a heading DOES carry — so it is ordered on the walk instead (LevelOrder).
	//
	// BOTH AXES, in the order they fold: a column heading is ordered by a field exactly as a row
	// heading is, and it is the same read that has to come back sorted.
	for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups }) {
		for (const GroupNode& level : *axis) {
			for (const ibSortLineDescription& line : level.m_settings.m_sort.m_lines) {
				bool isResource = false;
				for (const ibResourceDescription& resource : m_resources)
					if (resource.m_path.IsSameAs(line.m_path, false)) { isResource = true; break; }
				if (!isResource)
					writeKey(line.m_path, line.m_ascending);
			}
		}
	}

	// TOTALS [agg(path), …] BY dim [HIERARCHY], … — the aggregate list may be empty
	// (pure grouping / hierarchy), so emit the block whenever there is either an
	// aggregate OR a BY dimension.
	if (!m_resources.empty() || HasGroupingFields(output) || GetCurrentGroupDesc().IsOk()) {
		// ⭐⭐ A RESOURCE NOBODY SELECTED IS NOT COMPUTED (Max, 2026-08-28: "resources are exactly the
		// same — a resource that does not take part you throw out; it starts being used the moment
		// you switch that field on in the settings"). A declared resource is an OFFER, and what a report shows is what
		// somebody chose; folding a figure no cell prints costs the server an aggregate for nothing.
		//
		// ⚠ ASKED OF THE SELECTION IN FORCE FOR THESE OUTPUTS, by the name the resource answers to —
		// its alias where it has one, its path where it does not, which is what a person picks in
		// the fields table either way.
		//
		// 🛑 AND AN EMPTY TABLE EXCLUDES EVERYTHING — there is no "nobody said anything, so show it
		// all" (Max, 2026-08-28: "there is nothing in the selected fields — that is the same as the
		// report printing nothing"). The AUTHOR states the composition and the order at the ROOT of their
		// variant; that is the work of laying a report down, and a report where it was never done
		// shows nothing rather than guessing.
		//
		// The one composition this does not apply to is a LIST, which answers the other question
		// (ReadsEveryField): its columns are its source's, so everything it declares is shown.
		std::vector<wxString> shown;
		for (const Output* out : outputs)
			for (const wxString& name : ProjectionFor(*out))
				if (std::find(shown.begin(), shown.end(), name) == shown.end())
					shown.push_back(name);
		const auto resourceIsShown = [&](const ibResourceDescription& resource) {
			if (ReadsEveryField())
				return true;
			const wxString& answersTo = resource.m_alias.IsEmpty() ? resource.m_path : resource.m_alias;
			for (const wxString& name : shown)
				if (name.IsSameAs(answersTo, false) || name.IsSameAs(resource.m_path, false))
					return true;
			return false;
		};

		text += wxT(" TOTALS");
		bool wroteResource = false;
		for (size_t i = 0; i < m_resources.size(); ++i) {
			if (!resourceIsShown(m_resources[i]))
				continue;
			text += (!wroteResource ? wxT(" ") : wxT(", "));
			wroteResource = true;
			// NO FUNCTION MEANS THE TEXT IS THE EXPRESSION. `Resource("SUM", "Amount")` renders
			// `SUM(Amount)`; `Resource("", "SUM(Amount) / COUNT(DISTINCT Doc)")` renders itself.
			// One store, because the first is what the second would have been written as.
			// (And *this* is the tier that says TOTALS — it is the query's keyword. Everything above
			//  says resource; the two words used to meet in the middle of one function.)
			text += m_resources[i].m_func.IsEmpty()
				? m_resources[i].m_path
				: m_resources[i].m_func + wxT("(") + m_resources[i].m_path + wxT(")");
			// ⭐ AND THE NAME IT WAS GIVEN, through the language rather than beside it. The query text
			// is the one seam between the composition and the engine (the composer is a tier ABOVE
			// this text), so a resource's name travels the way a level's does — as `AS name`. Nothing
			// downstream has to be told about resources at all.
			// ⭐ …AND OVER WHAT, written before the name exactly as the language reads it. A resource
			// with an area is the composition's answer to "evaluate this in the context of a
			// grouping": it names a level of this very report, and the engine folds the figure there
			// instead of at every heading.
			if (!m_resources[i].m_scope.IsEmpty())
				text += wxT(" OVER ") + m_resources[i].m_scope;
			if (!m_resources[i].m_alias.IsEmpty())
				text += wxT(" AS ") + m_resources[i].m_alias;
		}
		// ONE LEVEL PER OUTPUT IN THE CHAIN, and a level's own fields inside it. Several fields are
		// written in BRACKETS — `BY (Partner, Contract), Store` — which is what says they are one
		// heading; a single field keeps the bare spelling it always had.
		// ⚠ A LEVEL WITH NO FIELDS IS NOT A DIMENSION — it is the DETAIL records, and it has nothing
		// to write here. Writing it anyway produced `BY ` with nothing after it and the parser
		// refused the whole query, which the caller then saw as an empty report.
		// ⭐ THE USER'S GROUPING REPLACES THE LADDER, whole — the same rule the filter and the sort
		// follow, for the same reason: a setting is one thing. Their lines are one field each (a flat
		// list cannot say "one heading of two fields"), which is exactly what a user's grouping is.
		bool wroteBy = false;
		// A level written straight after `SPLIT` needs no separator — the word IS the separator, and
		// a comma there would read back as one more level of the ladder above.
		bool afterSplit = false;
		if (GetCurrentGroupDesc().IsOk()) {
			for (const ibGroupLineDescription& line : GetCurrentGroupDesc().m_lines) {
				if (line.m_path.IsEmpty())
					continue;   // a line with no field is the absence of one
				text += (wroteBy ? wxT(", ") : wxT(" BY "));
				wroteBy = true;
				ibAppendGroupLine(text, line);
			}
		}
		const auto writeAxis = [&](const std::vector<GroupNode>& axis) {
			for (size_t i = 0; i < axis.size(); ++i) {
				const std::vector<TotalByItem>& fields = axis[i].m_settings.m_group.m_lines;
				if (fields.empty())
					continue;
				if (afterSplit)  afterSplit = false;                    // the word already separated it
				else             text += (wroteBy ? wxT(", ") : wxT(" BY "));
				wroteBy = true;
				const bool bracketed = fields.size() > 1;
				if (bracketed)
					text += wxT("(");
				for (size_t f = 0; f < fields.size(); ++f) {
					if (f > 0)
						text += wxT(", ");
					ibAppendGroupLine(text, fields[f]);
				}
				if (bracketed)
					text += wxT(")");
			}
		};
		// ⭐⭐ A CROSS-TABLE IS ONE FOLD, NOT TWO. Both axes' keys go into the SAME `BY`, rows first
		// and columns under them, and the server returns one row per intersection — which is exactly
		// what a cell IS. Nothing here knows the word "cross": the shape appears because the keys
		// were written in an order, and the printer reads that order back off `m_rowGroups.size()`.
		//
		// ROWS FIRST because the fold nests: a cell stands where a row key has already been chosen,
		// so the row keys are the outer ones. Writing columns first would give a table transposed —
		// the same numbers, in the wrong place, with nothing to say which was meant.
		if (!GetCurrentGroupDesc().IsOk()) {
			if (outputs.size() == 1) {
				writeAxis(output.m_rowGroups);
				writeAxis(output.m_columnGroups);
			}
			else {
				// ⭐⭐ EVERY OUTPUT IS A BRANCH OF ONE READ. A report's tables sit on the same source
				// and differ only in how they fold it — which is exactly what `SPLIT` says. Nothing
				// stands above them, so the branches fork at the grand total: the outputs are
				// siblings, not one nested inside another.
				//
				// ⚠ THE BRANCH IS NAMED AFTER THE OUTPUT, and there is no second name anywhere. The
				// walk asks for a branch by that name (RunOutputPass), so an output and its branch
				// cannot drift apart — there is only the one name to drift.
				for (size_t at = 0; at < outputs.size(); ++at) {
					const Output* branch = outputs[at];
					// `SPLIT <name> BY <levels>` — the node is named where it is opened, and the name
					// is the OUTPUT's (or the one derived from its position when it never got one).
					// Asked through BranchNameFor, which the WALK asks too, so the two cannot spell it
					// differently and an output can never lose its own branch.
					// ⚠ THE CLAUSE'S OWN `BY` COMES FIRST, even when the ladder opens with a node.
					// The grammar is `TOTALS <resources> BY <ladder>`, and a ladder may begin with a
					// SPLIT — but the `BY` that introduces the whole clause is still required. Writing
					// the first node without it produced `TOTALS COUNT(Number) SPLIT Output1 BY …`
					// and the parser stopped exactly there ("expected BY in TOTALS", live 2026-08-27).
					text += (wroteBy ? wxT(" ") : wxT(" BY "));
					text += ibQueryKeywordText(ibQueryKeyword::Split) + wxT(" ")
					      + BranchNameFor(*branch, at) + wxT(" ")
					      + ibQueryKeywordText(ibQueryKeyword::By) + wxT(" ");
					wroteBy    = true;
					afterSplit = true;
					writeAxis(branch->m_rowGroups);
					writeAxis(branch->m_columnGroups);
				}
			}
		}

		// ⭐⭐ RESOURCES WITH NO GROUPING ARE A GRAND TOTAL — `TOTALS COUNT(x) BY OVERALL`, which is
		// exactly "one row over everything" and is what the author asked for by declaring resources
		// and no level (Max, 2026-08-22: added the resources and composing refused).
		//
		// Written here rather than refused, because there is nothing wrong with the request. What
		// WAS wrong is what came out: `TOTALS COUNT(x)` with no BY at all — a query the engine's own
		// parser throws back ("expected BY in TOTALS"), on a composition nobody typed by hand. The
		// guard that catches this on the other road never runs for a composition over an AUTHOR'S
		// TEXT, which is what every report's composer is.
		if (!wroteBy)
			text += wxT(" BY OVERALL");
	}
}

//////////////////////////////////////////////////////////////////////
// execute / the driver walk
//////////////////////////////////////////////////////////////////////

namespace {

// Page-cache signature helpers — a value the signature can't render losslessly
// (a reference / an object) disables caching for that run; correctness over speed.
bool ValueSignable(const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: case TYPE_NUMBER: case TYPE_DATE:
	case TYPE_STRING:  case TYPE_NULL:   case TYPE_EMPTY:
		return true;
	default:
		return false;
	}
}

wxString ValueSig(const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: return v.GetBoolean() ? wxT("B1") : wxT("B0");
	case TYPE_NUMBER:  return wxT("N") + v.GetNumber().ToString();
	case TYPE_DATE:    return wxT("D") + v.GetDateTime().GetValue().ToString();
	case TYPE_STRING:  return wxT("S") + v.GetString();
	default:           return wxT("_");
	}
}

} // namespace

// ⭐⭐ THE AUTHOR'S TEXT, SPLIT — see the header. This is the whole of what a composition over a
// PACKAGE needed: everything else was already built (the language names results and relates them,
// the lowering declares them to the server as `WITH`, the field list already reads the package).
// What was missing was one honest answer to "and where do the SETTINGS go", and it is: over what
// the package PRODUCES, with everything that prepares it left standing in front.
void ibDataDBComposer::SplitSourceText() const
{
	if (m_resolved.m_ofText == m_sourceText && !m_resolved.m_from.IsEmpty())
		return;

	// ⭐ THROWN AWAY WHOLE, not field by field. This used to clear five members by name, and the day
	// a sixth was added it had to be remembered — a rule that lives in somebody's head is a rule
	// that eventually is not followed.
	m_resolved = SourceResolution();
	m_resolved.m_ofText = m_sourceText;

	// THE TABLES BELONGED TO THE OLD TEXT. A split that has been worked out again is a different
	// query, and rows prepared for the previous one are not an answer to this one.
	m_prepared.Forget();
	// THE ONE-QUERY ROAD IS THE DEFAULT AND ALSO THE FALLBACK: half-typed text does not parse, and a
	// RENDER is not where a person is told about a syntax error. EnsureAst parses next and says it
	// there — at the position, in the parser's own words.
	m_resolved.m_from = wxT("(") + m_sourceText + wxT(") AS ") + kAuthorQuerySource;

	ibQueryPackage package;
	try { package = ibQueryParser().ParsePackage(m_sourceText); }
	catch (const ibBackendException&) { return; }

	if (package.m_statements.size() <= 1 && package.m_links.empty())
		return;   // one query and nothing related — a nested source, exactly as before

	// LINKS AND NO STATEMENTS AT ALL — a text somebody is halfway through. There is no result to
	// stand on, so nothing is split; the read parses the text and complains in the parser's words.
	if (package.m_statements.empty())
		return;

	// ⭐⭐ A TEMP TABLE IS PREPARATION, NOT A RIVAL TO A NAME (Max, 2026-08-27: *"INTO is not in the
	// link and should not be — INTO is for the ONTO selections, they can use it there"*). The two
	// words do different work and the package uses both at once: `INTO` makes rows the statements
	// after it READ, and the selections that read them are the ones a `LINK` relates. So nothing is
	// refused here — the preparing statements are run before the read (EnsureTempTables), and by the
	// time a named selection is declared to the server its table is standing.
	//
	// The statements stay in the text either way: what a person sees is the package they wrote plus
	// the one statement the composer adds.
	m_resolved.m_package = package;

	// EVERY STATEMENT THAT IS NOT PREPARATION HAS TO BE NAMED (Max, 2026-08-26: one query — a name
	// is optional; two and more — every one of them needs a name). Not a rule of style: a statement
	// nothing can address is never read, and a package quietly carrying one produces an answer with
	// a whole selection missing from it. An `INTO` statement is exempt BY DEFINITION — the name it
	// carries is its table's, and that is what the ones after it address it by.
	const size_t last = package.m_statements.size() - 1;
	for (size_t i = 0; i < package.m_statements.size(); ++i) {
		const ibQuerySelectPtr select = package.m_statements[i].m_select;
		if (select == nullptr || !select->m_ontoName.IsEmpty() || !select->m_intoTemp.IsEmpty())
			continue;
		if (i == last && package.m_links.empty())
			continue;   // …the one exception: with no links the LAST statement IS the result
		ibBackendCoreException::Error(
			_("Composer: a query of several selections has one that is neither named nor a temporary "
			  "table. Name it with ONTO — without a name nothing can read it, and it is left out of "
			  "the answer."));
	}

	// A PACKAGE'S STATEMENTS ALL STAY IN FRONT when the links say how they meet: each of them is a
	// SOURCE of the composer's own select, resolved through the named-result scope (as `WITH` where
	// the engine reads a named query). Nothing is materialised and nothing is executed twice.
	if (!package.m_links.empty()) {
		std::vector<wxString> declared;
		for (const ibQueryAstStatement& statement : package.m_statements)
			if (statement.m_select != nullptr && !statement.m_select->m_ontoName.IsEmpty())
				declared.push_back(statement.m_select->m_ontoName);

		// ⭐ PLACED BY THE ENGINE'S OWN PLACER, not by a second reading of the links here. `LINK`
		// declares relations as a set; which of them stands where in the FROM is one question with
		// one answer, and ibQueryLowering::PlacePackageLinks is it.
		const ibQueryLowering::FromTree tree =
			ibQueryLowering::PlacePackageLinks(package.m_links, declared);

		// NOTHING USABLE WAS WRITTEN YET — every link is a row somebody opened and left. That is an
		// ordinary state of the window, so it falls through to the road below and the package reads
		// its last statement; it must NOT fall back to wrapping the whole text, which would put a
		// `LINK` section inside brackets and fail as a syntax error nobody caused.
		if (!tree.m_head.IsEmpty()) {
			m_resolved.m_from = tree.m_head;
			for (const ibQueryLowering::JoinStep& step : tree.m_steps) {
				wxString kind;
				switch (step.m_kind) {
				case ibQueryJoinKindAst::Left:  kind = ibQueryKeywordText(ibQueryKeyword::Left)  + wxT(" "); break;
				case ibQueryJoinKindAst::Right: kind = ibQueryKeywordText(ibQueryKeyword::Right) + wxT(" "); break;
				case ibQueryJoinKindAst::Full:  kind = ibQueryKeywordText(ibQueryKeyword::Full)  + wxT(" "); break;
				default: break;
				}
				m_resolved.m_from += wxT("\n\t") + kind + ibQueryKeywordText(ibQueryKeyword::Join)
				              + wxT(" ") + step.m_name
				              + wxT(" ") + ibQueryKeywordText(ibQueryKeyword::On)
				              + wxT(" ") + (step.m_on ? ibRenderQueryExpr(*step.m_on) : wxString());
			}
			// ⭐⭐ …AND WHAT THIS READS WHEN NOBODY HAS SELECTED ANYTHING — every placed selection's
			// fields, QUALIFIED. Not `*`: over a join a star publishes both sides under bare names,
			// and two selections that both project `Attribute2` then answer to one name.
			//
			// ⚠ ALL OR NOTHING, the same rule ExecutePackage follows: a selection that projects a
			// star of its own does not name its fields here, so there is nothing to qualify and the
			// honest fallback is the star — with the duplicate-name hazard then visible in the
			// author's own text.
			std::vector<wxString> placed{ tree.m_head };
			for (const ibQueryLowering::JoinStep& step : tree.m_steps)
				placed.push_back(step.m_name);

			wxString qualified;
			bool everyNameKnown = true;
			for (const wxString& name : placed) {
				const ibQuerySelect* select = nullptr;
				for (const ibQueryAstStatement& statement : package.m_statements)
					if (statement.m_select && statement.m_select->m_ontoName.IsSameAs(name, false)) {
						select = statement.m_select.get();
						break;
					}
				if (select == nullptr || select->m_selectAll || select->m_projections.empty()) {
					everyNameKnown = false;
					break;
				}
				for (const ibQueryProjection& projection : select->m_projections) {
					if (projection.m_star || !projection.m_expr)
						continue;
					const wxString field = ibQueryOutputName(projection);
					if (field.IsEmpty())
						continue;
					if (!qualified.IsEmpty())
						qualified += wxT(", ");
					qualified += name + wxT(".") + field;
				}
			}
			if (everyNameKnown)
				m_resolved.m_allFields = qualified;

			// EVERY STATEMENT STAYS IN FRONT: each is a SOURCE of the select above, resolved through
			// the named-result scope. Rendered from the package WITHOUT its links — they have just
			// become the FROM, and writing them twice would relate the selections a second time.
			// ⚠ AND WITHOUT THE LINK'S OWN STATEMENT EITHER. A link section takes a place in the
			// sequence (it runs there and answers with how many rows it removed), so it is a
			// statement as well as a relation — and here the relation has just become the FROM
			// above. Copying the statement while dropping the links it points at would render an
			// EMPTY statement: a bare `;` between two selections, which reads back as "expected
			// SELECT" (2026-09-04).
			ibQueryPackage related;
			for (const ibQueryAstStatement& statement : package.m_statements)
				if (!statement.IsLink())
					related.m_statements.push_back(statement);

			m_resolved.m_preamble = ibRenderQueryPackage(related);
			return;
		}
	}

	// THE LAST STATEMENT PRODUCES THE RESULT — not a new rule but the one already in force, since it
	// is the statement the field list offers a person to pick from (ibQueryFieldsOfText). It is read
	// as a nested source, exactly as a lone query is.
	const ibQuerySelectPtr result = package.m_statements[last].m_select;
	if (result == nullptr)
		return;

	ibQueryPackage prepared;
	prepared.m_statements.assign(package.m_statements.begin(), package.m_statements.begin() + last);
	m_resolved.m_preamble = ibRenderQueryPackage(prepared);

	// ⚠ WITHOUT ITS OWN `ONTO`. The name is what a LATER statement reads the result by, and there is
	// none: this select is about to stand inside the composer's FROM under the composer's own alias.
	ibQuerySelect readAsSource = *result;
	readAsSource.m_ontoName.Clear();
	m_resolved.m_from = wxT("(") + ibRenderQuery(readAsSource) + wxT(") AS ") + kAuthorQuerySource;
}

// ⭐⭐ WHAT PREPARES, RUN ONCE — see the header.
void ibDataDBComposer::EnsureTempTables() const
{
	if (m_prepared.m_ready)
		return;

	// The registry every read of this composer resolves through. Its OWN transient sources are
	// always in it (a RAM table handed in by the host); the prepared tables join them.
	m_prepared.m_sources = m_directSources;
	m_prepared.m_ready = true;

	bool prepares = false;
	for (const ibQueryAstStatement& statement : m_resolved.m_package.m_statements)
		if (statement.m_select != nullptr && !statement.m_select->m_intoTemp.IsEmpty()) {
			prepares = true;
			break;
		}
	if (!prepares)
		return;   // nothing to make — which is every composition that does not write INTO

	// The scope is opened HERE and holds a POINTER to the map, so each table is visible to the
	// statement after it — that is the batch contract, and PreparePackage fills the map as it goes.
	ibSourceMetaDataScope mdScope(m_metaData);
	ibTempSourceScope     tempScope(m_prepared.m_sources);
	ibQueryLowering::PreparePackage(m_resolved.m_package, m_params, m_prepared.m_store, m_prepared.m_sources);
}

// TEXT -> the statement that carries the settings, and the named results standing behind it.
ibQuerySelectPtr ibDataDBComposer::ParseComposed(const wxString& text, ibQueryPackage& package,
	std::map<wxString, const ibQuerySelect*>& named) const
{
	package = ibQueryParser().ParsePackage(text);
	named.clear();
	if (package.m_statements.empty())
		return nullptr;

	// EVERY STATEMENT BUT THE LAST IS A NAMED RESULT — this text is what THIS composer wrote
	// (SplitSourceText), so there is nothing else it can be: the preamble is exactly the selections
	// the final statement reads, and the final statement is the one carrying the settings.
	for (size_t i = 0; i + 1 < package.m_statements.size(); ++i) {
		const ibQuerySelectPtr select = package.m_statements[i].m_select;
		if (select != nullptr && !select->m_ontoName.IsEmpty())
			named.emplace(select->m_ontoName.Lower(), select.get());
	}
	return package.m_statements.back().m_select;
}

void ibDataDBComposer::EnsureAst()
{
	// The cache key is the rendered text PLUS the tree condition's version: that
	// condition never becomes text, so the text alone would say "nothing changed"
	// after the user rewrote the whole filter.
	const wxString text = RenderText();
	if (m_rendered.m_ast != nullptr && text == m_rendered.m_text && GetCurrentFilterDesc() == m_rendered.m_filter)
		return;   // same query, same condition — the cached parse stands

	// ⭐⭐ AND WHAT THE COMPOSER WROTE IS JOURNALLED HERE, WHERE IT IS BORN. The engine's own `query
	// run:` line is written far downstream, AFTER the lowering — so anything that refuses on the way
	// (an unknown attribute, a source that will not resolve) leaves a refusal in the journal with no
	// text beside it, and a person is asked to show a query nobody ever printed (Max, 2026-08-24:
	// *"you simply do not write the queries — an ordinary query reaches the log, a composer's does
	// not"*).
	//
	// A ROAD OF ITS OWN, because it answers a different question from the engine's: this is what the
	// SETTINGS produced, before anything rewrote or folded it. Reading the two together is what
	// tells a rendering fault from a lowering one.
	//
	// ⚠ WRITTEN AFTER THE CACHE GATE, so it costs one line per CHANGE and not one per fetch: a list
	// paging through a hundred screens renders the same text and says nothing.
	ibJournalInfo(wxT("composer.text"), wxT("rendered:\n%s"), text);

	m_rendered.m_ast = ParseComposed(text, m_rendered.m_package, m_rendered.m_named);
	if (m_rendered.m_ast == nullptr)
		ibBackendCoreException::Error(_("Composer: the rendered query failed to parse"));

	// ⭐ THE USER'S FILTER IS BUILT HERE, out of the section they set — and it REPLACES the
	// composition's own condition rather than joining it. A setting is one thing: half of theirs
	// ANDed with half of the composition's is a filter nobody wrote.
	//
	// Built at the render, not when the setting was assigned: assigning is assigning (the old value
	// is thrown away by the fact of `=`), and what a query needs is made when a query is made.
	const ibQueryAstExprPtr condition = BuildFilterAst(GetCurrentFilterDesc());

	// THE TREE'S CONDITION GOES IN AS AN AST, ANDed with whatever the query text
	// already asked for. No rendering, no re-parsing — the expression the filter
	// built is the expression the engine lowers.
	AndWhere(*m_rendered.m_ast, condition);

	m_rendered.m_text = text;
	m_rendered.m_filter = GetCurrentFilterDesc();
}

bool ibDataDBComposer::BuildPageSignature(const ibReadPageRequest& page, wxString& signature) const
{
	// Cache the paged hot path only; the tree's parent filter is excluded (its
	// reference blob is not signable). An ANCHORED page is NOT cached: the anchor
	// keyset EMBEDS its values (a reference key renders as its _RRRef blob, not an
	// ibParam), so the SQL is per-anchor — a shared cached render would replay a
	// stale anchor. The unanchored first page still caches.
	if (page.m_count <= 0 || page.m_hierarchyFilter || page.m_hasAnchor)
		return false;
	for (const auto& p : m_params)
		if (!ValueSignable(p.second))
			return false;

	signature << wxT("c") << page.m_count
	          << wxT("d") << static_cast<int>(page.m_direction)
	          << wxT("a") << (page.m_hasAnchor ? 1 : 0)
	          << wxT("r") << (page.m_reverseSort ? 1 : 0)
	          << wxT("|T") << m_rendered.m_text << wxT("|P");
	for (const auto& p : m_params)
		signature << wxT(";") << p.first << wxT("=") << ValueSig(p.second);
	return true;
}
