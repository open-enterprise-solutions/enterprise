////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register - the five readings and their virtual tables
////////////////////////////////////////////////////////////////////////////
//
// ONE PLACE FOR THE READINGS. Balance, Turnovers, DrCrTurnovers, BalanceAndTurnovers and
// RecordsWithAccountDimensions are one register read five ways: each is a clean function ON THE
// METAOBJECT (its own aggregate knowledge) plus a light companion queryable that publishes it to L3.
// The manager holds no reading of its own — it calls these and dresses the rows for a script
// (accountingRegisterManager_impl.cpp), exactly as the accumulation register does.
//
// ⚠ WHERE THESE READ IS DECIDED PER PASS, NOT ONCE. The totals bundle exists (two guarded
// accumulations, one per side — accountingRegisterMetadataSchema.cpp), so a reading takes the
// trigger-maintained TURNOVERS VIEW when the driver can maintain one (HasMaterializedViews) and the
// MOVEMENTS when it cannot. It takes the movements as well whenever the question is one a total
// cannot answer by construction: a filter on the OPPOSITE account of a correspondence line (a totals
// row is keyed by ONE account and the other was never stored beside it), and any fold FINER than the
// stored grain of a day — an hour, a recorder, a line. Answering those from the totals would mean
// ignoring the filter or replying at the wrong granularity, both of which produce a plausible wrong
// number rather than an error. Both surfaces publish the same columns under the same metaIDs, so
// nothing above the reading learns which one answered.
//
// ⚠ AND THERE IS NO PARITY CHECK — deliberately, not pending. Nothing here re-aggregates the
// movements to see whether the stored figures agree: the delta runs in the trigger, in the SAME
// transaction as the movement it accumulates, so the two cannot part by themselves. Materialising the
// figures was done exactly to stop watching them, and a routine that keeps re-asking whether the write
// did what the write is defined to do puts the surveillance back and calls it safety. What CAN part
// them is a write that reached the movements without the trigger — a restore, a bulk load, direct SQL
// — and that is an event with a repair of its own (ibDerivedState::Regenerate), not a reason to stand
// a check beside every read.
//
// ⏳ ALL FIVE READINGS now override GetSourceRelation and fill the neighbour's read spec, so an
// aggregation runs on the server whenever that call's own gates (CanReadOnServer) say the stored
// surface can hold every question in it. The RAM road stays for the rest — a driver with no
// materialised views, a breakdown asked for BY KIND, a fold finer than the stored grain — which is
// why the grain rule is still stated TWICE: here as a predicate over the view's two arms
// (ArmCutAtMoment / ArmCutOverRange) and in the shared ibRegFillArmCut for the spec (arc § 8.3 /
// § 8.3a). Both roads are live, so that is duplication and not residue.
//
// ⛔ AND THE SUBTREE WALK IS NOT HERE ANY MORE. «In hierarchy» is a word of the LANGUAGE now
// (`Account IN HIERARCHY (&Accounts)`), and the walk that resolves it — plus the map saying which
// named account a subordinate's rows are reported UNDER — is ibQueryHierarchyScope
// (query/queryHierarchy.h). It never knew what an account was: it asks the COLUMN, through the
// provider, which is why it could leave — and reads the chart's parent map in ONE query where this
// file used to spend one per node.
//
// ⛔ WHAT IS NOT HERE ANY MORE: raw L1 SQL. The four aggregates used to be built as concatenated
// strings with hand-bound parameters, and the execution half of every one of them sat under `#if 0`
// — SQL composed, never run, an empty table returned and nothing said. That path bypassed the access
// policy, the dialect layer and paging, and met a live hazard on PostgreSQL besides. It is deleted
// rather than revived (arc §6, step 5).

#include "accountingRegister.h"
#include "chartOfAccounts.h"
#include "backend/metaCollection/table/metaTableObject.h"   // ibTabularQueryable — the section as a source (OwnerRefColumn)
#include "reference/reference.h"                                   // ibValueReferenceDataObject — reading what an ACCOUNT declares

#include "backend/query/dataQueryBuilder.h"                        // L3 door — From(source).Select() / SelectAggregate()
#include "backend/databaseLayer/databaseMaterializeBuilder.h"       // L2-2 — RenderMaterializedRead: the READ side of the materialised surface
#include "backend/query/queryRamTable.h"                           // FoldBalancesForward — the running step, shared with the accumulation register
#include "backend/query/queryAst.h"                                // ibQueryDimUnfold — «in» / «in hierarchy» / «hierarchy only», the language's own three words
#include "backend/query/queryHierarchy.h"                          // ibQueryHierarchyScope — the operator that resolves those three words into values
#include "backend/databaseLayer/databaseLayer.h"                   // ibTruncateToPeriod / ibNextPeriodStart — the GRAIN, in RAM terms
#include "backend/system/value/valueArray.h"                        // ibValueArray — a requested breakdown may be a LIST
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegBound / ibRegFold / ibRegFillArmCut
#include "backend/metaCollection/resource/metaResourceObject.h"     // IsBalanceResource — is a balance kept in this figure at all
#include "backend/appData.h"
#include "backend/session/session.h"

#include <algorithm>
#include <unordered_map>   // value-keyed indexes — see ibValueHash / ibValueSeqHash (value.h)
#include <unordered_set>
#include <set>

// ============================================================================
// The requested breakdown — one kind, a LIST of them, or nothing at all
// ============================================================================

// ⭐⭐ THE ORDER IS THE CALLER'S, AND THAT IS THE WHOLE MECHANISM. A reader asks for a breakdown by
// contract and counterparty, in that order, and gets column 1 = contract, column 2 = counterparty —
// whichever slot each one occupies on each account. The order in an account's kinds table is DATA;
// the requested order is the reader's; the engine reconciles them, and it can only do so because each
// stored row says what kind its value was filed under.
//
// ⚠ NOTHING ASKED FOR IS NOT AN EMPTY LIST. It means "as the ACCOUNT declares them" — the slots are
// then reported as they stand, which IS the account's own order, and an account using fewer kinds
// than the register has slots simply leaves the tail columns empty. That is the default a person
// expects from a movements-shaped reading, and it costs no CASE at all.
std::vector<ibValue> ibAcctReadKinds(const ibValue& given)
{
	std::vector<ibValue> kinds;

	ibValueArray* array = nullptr;
	if (given.ConvertToValue(array) && array != nullptr) {
		for (unsigned int idx = 0; idx < array->Count(); idx++) {
			ibValue element;
			if (array->GetAt(ibValue(idx), element) && !element.IsEmpty())
				kinds.push_back(element);
		}
		return kinds;
	}

	if (!given.IsEmpty())
		kinds.push_back(given);
	return kinds;
}

wxString ibValueMetaObjectAccountingRegister::AccountDimensionColumnName(const wxString& sidePrefix, unsigned int no)
{
	return wxString::Format(wxT("AccountDimension%s%u"), sidePrefix, no);
}

// The KIND half of the same pair, spelled through the same door — the slot creator used to format
// this string itself, so "the name of slot N on side S" had two authorities that only happened to
// agree.
wxString ibValueMetaObjectAccountingRegister::AccountDimensionKindColumnName(const wxString& sidePrefix, unsigned int no)
{
	return AccountDimensionColumnName(sidePrefix, no) + wxT("Kind");
}

namespace {

// Does a ROW of this reading name two accounts? Only the correspondence matrix and the movements
// listing do. A balance or a turnover reports ONE account per row — its debit and credit figures side
// by side — even in a correspondence register, because that is what the question asks.

bool PairedRow(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape)
{
	return shape == ibAcctShape::DrCrTurnovers
		|| (shape == ibAcctShape::Records && reg != nullptr && reg->IsCorrespondence());
}

// The side prefix a column carries — and it is decided by the READING, not by the register's mode.
// Where a row is about one account there is no side to tell apart, so `AccountDimension1` says all
// there is to say; where a row is a pair, `AccountDimensionDr1` / `AccountDimensionCr1` do.
wxString SidePrefix(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape, bool creditSide)
{
	if (!PairedRow(reg, shape))
		return wxEmptyString;
	return creditSide ? wxT("Cr") : wxT("Dr");
}

// How many breakdown columns a side reports: what was asked for, or what the register HAS.
unsigned int BreakdownWidth(const ibValueMetaObjectAccountingRegister* reg, const std::vector<ibValue>& kinds)
{
	return kinds.empty() ? reg->GetAccountDimensionCount() : static_cast<unsigned int>(kinds.size());
}

// ⭐⭐ THE SAME ATTRIBUTE, ON WHICHEVER SURFACE THIS READING STANDS.
//
// A reading runs either over the MOVEMENTS (where an attribute IS the column) or over the totals VIEW
// (where the same attribute is published under its own name and metaID — deliberately, so the view is
// interchangeable with the register as a source rather than a parallel vocabulary). Asking the source
// by name is what makes one body of code serve both, and it is why the view was built to keep those
// ids in the first place.
const ibBackendQueryColumn* ColumnOn(const ibBackendQueryable* source, const ibValueMetaObjectAttributeBase* attribute)
{
	if (source == nullptr || attribute == nullptr)
		return nullptr;
	const ibBackendQueryColumn* here = source->ResolveColumnByName(attribute->GetName());
	return here != nullptr ? here : attribute;
}

// ⭐⭐ ONE BREAKDOWN COLUMN — where its value comes from, and how it is read back.
//
// Two roads, and which one is taken is decided by whether the caller named a kind:
//
//   nothing asked for   the slot AS IT STANDS. An ordinary column of the movements table, grouped and
//                       read like any other — no expression, no spread, nothing to reassemble.
//   a kind asked for    a CASE over EVERY slot, selecting the one whose Kind column matches. Written
//                       once per physical field and projected under one prefix, because the value is
//                       a composite (a type tag plus one field per admissible type of the contour)
//                       and a single-field CASE would carry the tag and lose the value.
struct ibAcctBreakdownColumn
{
	wxString                                    m_alias;             // AccountDimension[Dr|Cr]<no>

	// ⭐⭐ THE DECLARATION AND THE COLUMN ARE TWO DIFFERENT THINGS, and this file needs both.
	//
	// The DECLARATION is the slot's attribute. It says what the breakdown IS — its type, and the
	// physical fields the CASE spread is written over — and it is known from metadata alone, so a
	// LAYOUT can be described before any surface has been chosen.
	//
	// The COLUMN is what a row is actually read BY, and it belongs to the surface this reading stands
	// on: the movements publish the attribute itself, the totals view publishes a column of its own
	// under the same name and with its own field spread. Reading a view's row by the movements'
	// attribute asks the selection for fields that are not in it — which is silent, and answers empty.
	const ibValueMetaObjectAttributeBase*       m_attribute     = nullptr;
	const ibValueMetaObjectAttributeBase*       m_kindAttribute = nullptr;
	const ibBackendQueryColumn*                 m_slot          = nullptr;
	const ibBackendQueryColumn*                 m_kindSlot      = nullptr;

	bool                                        m_byKind = false;    // was it selected by kind?

	// ⭐⭐ THE KIND TRAVELS WITH THE VALUE, and it has to.
	//
	// When the caller asked for a kind, the column MEANS that kind and it is known here (m_requestedKind).
	// When nobody asked, the column is a SLOT — and which kind stands in it is decided per account, so
	// after the grouping the value alone says nothing: slot 1 is a counterparty on 62 and an item on 41.
	// So the unrequested case groups by the kind column too and publishes it as `AccountDimension<i>Kind`.
	//
	// Which is exactly what the "turnovers only" flag needs to be readable at all: it is set per (account,
	// KIND), so a reading that has lost the kind cannot tell whether a balance is kept along that
	// breakdown.
	wxString                                    m_kindAlias;         // AccountDimension[Dr|Cr]<no>Kind — unrequested case
	ibValue                                     m_requestedKind;     // the kind the caller asked for, if any
};

// Does this column publish a KIND of its own? Only the unrequested case does — a column selected BY a
// kind already means it. Asked of the DECLARATION, so a layout answers it with no source in hand.
bool BreakdownCarriesKind(const ibAcctBreakdownColumn& column)
{
	return column.m_kindAttribute != nullptr && !column.m_byKind;
}

// ⭐⭐ THE LAYOUT OF ONE SIDE'S BREAKDOWN — the caller's order, from metadata and the call alone.
//
// Described BEFORE a surface is chosen, and that is what makes it ONE rule instead of three: the
// readings that QUERY for their rows hand it a source and go on to group and read by it, while the
// reading that ASSEMBLES its rows out of two other readings (balance-and-turnovers) needs only the
// names and asks this very function for them. Spelled a second time by hand, the two agree until the
// first column is added on one side of the file.
void DescribeBreakdown(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape, bool creditSide,
                       const std::vector<ibValue>& kinds, std::vector<ibAcctBreakdownColumn>& out)
{
	const wxString prefix = SidePrefix(reg, shape, creditSide);
	const unsigned int width = BreakdownWidth(reg, kinds);

	for (unsigned int no = 0; no < width; no++) {
		ibAcctBreakdownColumn column;
		column.m_alias = ibValueMetaObjectAccountingRegister::AccountDimensionColumnName(prefix, no + 1);

		if (kinds.empty()) {
			// The slot AS IT STANDS — and its KIND beside it, because without the kind the column is a
			// position and a position means different things on different accounts.
			column.m_attribute = reg->GetAccountDimensionSlot(creditSide, no);
			if (column.m_attribute == nullptr)
				continue;
			column.m_kindAttribute = reg->GetAccountDimensionKindSlot(creditSide, no);
			if (column.m_kindAttribute != nullptr)
				column.m_kindAlias = column.m_alias + wxT("Kind");
		}
		else {
			column.m_attribute     = reg->GetAccountDimensionSlot(creditSide, 0);
			column.m_byKind        = true;
			column.m_requestedKind = kinds[no];   // the column MEANS this kind — no second column needed
		}
		out.push_back(column);
	}
}

// The CASE spread for one requested kind. `group` decides whether the projection is also a grouping
// key — a total groups by its breakdown, a movements listing merely reports it.
void ProjectDimensionByKind(ibDataQueryBuilder& b, const ibValueMetaObjectAccountingRegister* reg,
                            const ibBackendQueryable* source,
                            bool creditSide, const ibValue& kind, const wxString& alias, bool group)
{
	const ibValueMetaObjectAttributeBase* sample = reg->GetAccountDimensionSlot(creditSide, 0);
	if (sample == nullptr)
		return;

	// Every slot of a side is typed identically — that is the point of the design (they differ by the
	// KIND standing in them, never by declaration) — so one slot's field layout describes them all.
	const wxString sampleBase = sample->GetPhysicalName();
	for (const wxString& field : ibRegFieldsOf(sample)) {
		const wxString suffix = field.length() > sampleBase.length() ? field.Mid(sampleBase.length()) : wxString();

		std::vector<std::pair<ibQueryPredicatePtr, ibQueryColumnExprPtr>> cases;
		for (unsigned int idx = 0; idx < reg->GetAccountDimensionCount(); idx++) {
			// The SOURCE's columns, never the declarations: the spread is written out field by field,
			// and a view names its fields after its own column.
			const ibBackendQueryColumn* slot     = ColumnOn(source, reg->GetAccountDimensionSlot(creditSide, idx));
			const ibBackendQueryColumn* kindSlot = ColumnOn(source, reg->GetAccountDimensionKindSlot(creditSide, idx));
			if (slot == nullptr || kindSlot == nullptr)
				continue;

			// WHEN this slot's kind IS the requested one. The kind is a reference, so the comparison
			// spreads across its own fields on its own — the predicate takes a COLUMN and asks it.
			ibQueryCondition leaf;
			leaf.m_col   = kindSlot;
			leaf.m_op    = ibQueryFilterOp::Equal;
			leaf.m_value = kind;

			cases.push_back({ ibQueryPredicate::Leaf(leaf),
			                  ibQueryColumnExpr::ColField(slot, slot->GetPhysicalName() + suffix) });
		}
		if (cases.empty())
			continue;

		// No ELSE: an account that does not carry this kind reports NOTHING in that column, which is
		// the truth. A zero or an empty string would be a value the row does not have.
		const ibQueryColumnExprPtr expr = ibQueryColumnExpr::Case(std::move(cases), nullptr);
		if (group)
			b.GroupByExpr(expr, alias + suffix);
		else
			b.SelectExpr(expr, alias + suffix);
	}
}

// Every breakdown column of one side, in the caller's order: the layout above, then pointed at the
// surface this reading stands on and grouped by where the reading folds.
void AddBreakdown(ibDataQueryBuilder& b, const ibValueMetaObjectAccountingRegister* reg,
                  const ibBackendQueryable* source,
                  ibAcctShape shape, bool creditSide, const std::vector<ibValue>& kinds, bool group,
                  std::vector<ibAcctBreakdownColumn>& out)
{
	const size_t first = out.size();
	DescribeBreakdown(reg, shape, creditSide, kinds, out);

	for (size_t idx = first; idx < out.size(); idx++) {
		ibAcctBreakdownColumn& column = out[idx];
		column.m_slot = ColumnOn(source, column.m_attribute);

		if (column.m_byKind) {
			ProjectDimensionByKind(b, reg, source, creditSide, column.m_requestedKind, column.m_alias, group);
			continue;
		}

		column.m_kindSlot = ColumnOn(source, column.m_kindAttribute);
		if (group) {
			if (column.m_kindSlot != nullptr)
				b.GroupBy(column.m_kindSlot);
			b.GroupBy(column.m_slot);
		}
	}
}

// Reading one back is the mirror of how it was written: a slot read as itself, a CASE spread
// reassembled from its fields through the codec that reads every composite column.
ibValue ReadBreakdown(ibDataQueryResult& sel, const ibAcctBreakdownColumn& column)
{
	if (column.m_slot == nullptr)
		return ibValue();
	return column.m_byKind ? sel.GetColumnObject(column.m_alias, column.m_slot)
	                       : sel.GetValue(column.m_slot);
}

// The kind this column's value is filed under: the one the caller asked for, or the one the row itself
// carries. Either way a reading knows it — which is what makes the per-kind flags readable.
ibValue ReadBreakdownKind(ibDataQueryResult& sel, const ibAcctBreakdownColumn& column)
{
	if (column.m_byKind)
		return column.m_requestedKind;
	if (column.m_kindSlot == nullptr)
		return ibValue();
	return sel.GetValue(column.m_kindSlot);
}

// THE KEY'S BREAKDOWN HALF, written in ONE order and read back in the same one — the names here and the
// values below. Two loops that must agree, kept adjacent: a stored kind takes a position of its own,
// a requested one does not.
void AppendBreakdownNames(const std::vector<ibAcctBreakdownColumn>& breakdown, std::vector<wxString>& out)
{
	for (const ibAcctBreakdownColumn& column : breakdown) {
		if (BreakdownCarriesKind(column))
			out.push_back(column.m_kindAlias);
		out.push_back(column.m_alias);
	}
}

void AppendBreakdownValues(ibDataQueryResult& sel, const std::vector<ibAcctBreakdownColumn>& breakdown,
                           std::vector<ibValue>& key)
{
	for (const ibAcctBreakdownColumn& column : breakdown) {
		if (BreakdownCarriesKind(column))
			key.push_back(ReadBreakdownKind(sel, column));
		key.push_back(ReadBreakdown(sel, column));
	}
}

// ============================================================================
// The rest of the WHERE — accounts, period, condition
// ============================================================================

// ⭐⭐ AN ACCOUNT NAMES ITS SUBTREE — AND THAT IS NOT AN ACCOUNTING FACT ANY MORE.
//
// "Subordinate to an account" is an ordinary parent link (a chart of accounts derives the hierarchical
// base and states an ITEM hierarchy — an account is subordinate to an ACCOUNT, not to a folder), and
// asking for 60 means asking for 60 with 60.01 and 60.02 under it. That is what a chart of accounts is
// FOR: the parent is the summary account and its children are the detail.
//
// The walk that resolves it, and the map saying which account a subordinate's rows are REPORTED under,
// were written here — and nothing in them was about accounts. They moved to the operator the language
// now spells, `ibQueryHierarchyScope` (query/queryHierarchy.h), where `Account IN HIERARCHY
// (&Accounts)` resolves through the same code, asking the COLUMN through the provider and reading the
// chart's parent map in one query rather than one per node.
// An account argument is a value, a list of them, or nothing; nothing is not a filter — it means every
// account. Each named account brings its subtree, and the whole set becomes one IN: an account is a
// REFERENCE, so the comparison spreads over its own fields, which the predicate already handles.
//
// ⚠ THE FILTER AND THE FOLD WERE ONE HERE, and that was a defect rather than a simplification: the
// argument always expanded the subtree, so "these three accounts exactly" could not be asked at all —
// and the rows still came back under the accounts that carried them, which is the half of «in
// hierarchy» that was missing. One question answered by two words that were never told apart.
void WhereAccount(ibDataQueryBuilder& b, const ibBackendQueryColumn* accountCol, const ibQueryHierarchyScope& scope)
{
	if (accountCol == nullptr || scope.IsEmpty())
		return;

	ibQueryPredicatePtr folded;
	for (const ibValue& account : scope.Accepted()) {
		ibQueryCondition leaf;
		leaf.m_col   = accountCol;
		leaf.m_op    = ibQueryFilterOp::Equal;
		leaf.m_value = account;

		const ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
		folded = folded ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, folded, one) : one;
	}
	if (folded)
		b.Where(folded);
}

// ⭐⭐ THE ACCOUNT CONDITION, READ BACK INTO THE SCOPE IT MEANS.
//
// `AccountCondition` is a slot the source CONSUMES (queryableFactory.h): the predicate written there
// never reaches a WHERE, it arrives here — with the unfold word intact and the accounts still as
// NAMED, because that is the only form a fold can be built from. What comes out is the same
// ibQueryHierarchyScope the account ARGUMENT used to produce: which accounts are admitted, and which
// one each of them is reported under.
//
// ⚠ ONLY ACCOUNTS BELONG IN IT, and this is where that is enforced rather than merely written down.
// A leaf about anything else would be silently dropped — the slot does not reach the WHERE, so there
// is nowhere for it to be applied — and a filter that vanishes reports MORE than was asked for. The
// general `Condition` slot is the place for everything else, and the message says so.
ibQueryHierarchyScope ScopeFromAccountCondition(const ibBackendQueryable* source,
                                                const ibBackendQueryColumn* accountCol,
                                                const ibQueryPredicatePtr& condition)
{
	if (!condition || accountCol == nullptr)
		return ibQueryHierarchyScope();

	std::vector<const ibQueryCondition*> leaves;
	std::function<void(const ibQueryPredicatePtr&)> walk = [&](const ibQueryPredicatePtr& node) {
		if (!node)
			return;
		if (node->m_kind == ibQueryPredicateKind::Leaf) {
			leaves.push_back(&node->m_leaf);
			return;
		}
		// AND composes; anything else (OR, NOT, IS NULL) is a shape this slot does not admit — an
		// account condition is a list of accounts, however it was written.
		if (node->m_kind != ibQueryPredicateKind::And)
			ibBackendCoreException::Error(
				_("the account condition takes comparisons on the account joined by AND - put anything else in Condition"));
		for (const ibQueryPredicatePtr& child : node->m_children)
			walk(child);
	};
	walk(condition);

	std::vector<ibValue>   named;
	ibQueryDimUnfold       unfold = ibQueryDimUnfold::Elements;
	for (const ibQueryCondition* leaf : leaves) {
		if (leaf->m_col != accountCol)
			ibBackendCoreException::Error(
				_("the account condition may only name the account - put anything else in Condition"));
		if (leaf->m_unfold != ibQueryDimUnfold::Elements)
			unfold = leaf->m_unfold;
		if (!leaf->m_values.empty())
			named.insert(named.end(), leaf->m_values.begin(), leaf->m_values.end());
		else if (!leaf->m_value.IsEmpty())
			named.push_back(leaf->m_value);
	}

	return ibQueryHierarchyScope(source, accountCol, named, unfold);
}

// ⭐⭐ HOW A LISTING COMES OUT — the order, and how many.
//
// Only a listing can be asked this: a fold answers with every group it found, and a group has no
// line to put before another. What the author writes is field names, one per element of an array or
// separated by commas in one string, each optionally followed by a direction.
//
// ⚠ THE NAME IS RESOLVED AGAINST THE SOURCE, and a name it does not know is an ERROR rather than a
// sort quietly left out. An ignored ORDER BY is the kind of wrong nobody notices: the rows come back,
// in some order, and the order they came back in looks like an answer.
void OrderRecords(ibDataQueryBuilder& b, const ibBackendQueryable* source, const ibValue& order)
{
	if (source == nullptr || order.IsEmpty())
		return;

	// One value or a list of them — the same shape every argument of this register takes.
	std::vector<wxString> items;
	for (const ibValue& element : ibQueryHierarchyNamedValues(order)) {
		wxString text = element.GetString();
		while (!text.IsEmpty()) {
			const int comma = text.Find(wxT(','));
			wxString one = comma == wxNOT_FOUND ? text : text.Left(comma);
			text = comma == wxNOT_FOUND ? wxString() : text.Mid(comma + 1);
			one.Trim(true).Trim(false);
			if (!one.IsEmpty())
				items.push_back(one);
		}
	}

	for (const wxString& item : items) {
		wxString name = item;
		bool ascending = true;

		// `<field> DESC` — the direction is a word AFTER the name, which is where every language this
		// one resembles puts it.
		const int space = name.Find(wxT(' '));
		if (space != wxNOT_FOUND) {
			const wxString direction = name.Mid(space + 1).Trim(true).Trim(false);
			name = name.Left(space).Trim(true);
			if (stringUtils::CompareString(direction, wxT("DESC")) || stringUtils::CompareString(direction, wxT("Descending")))
				ascending = false;
			else if (!stringUtils::CompareString(direction, wxT("ASC")) && !stringUtils::CompareString(direction, wxT("Ascending")))
				ibBackendCoreException::Error(_("unknown sort direction '%s' - expected ASC or DESC"), direction);
		}

		const ibBackendQueryColumn* column = source->ResolveColumnByName(name);
		if (column == nullptr)
			ibBackendCoreException::Error(_("cannot order by '%s': this reading has no such field"), name);
		b.OrderBy(column, ascending);
	}
}

// ⭐⭐ AND THE OTHER HALF OF A CONDITION: A FILTER OVER THE BREAKDOWN.
//
// A condition may name a DIMENSION (an ordinary column of the register) or an ACCOUNT DIMENSION — and
// the second is a different question entirely. There is no column called "Contractor": the value sits
// in whichever SLOT that account happens to keep contractors in, and that differs per account. So the
// filter asks the slots:
//
//     (Kind1 = <kind> AND Slot1 = <value>) OR (Kind2 = <kind> AND Slot2 = <value>) OR …
//
// Which is a plain predicate — it renders to SQL, and the reading stays on the server. The kind is the
// KEY of the entry (a reference to a characteristic, not a name), so nothing has to be looked up by
// description and two kinds that read alike cannot be confused.
//
// ⚠ Entries keyed by a STRING are dimensions and belong to the other converter; they are skipped here
// rather than guessed at.
// Declared here, defined below with the arm cut that shares them: composing predicates is the same
// operation whichever rule is doing the composing.
ibQueryPredicatePtr AndWith(const ibQueryPredicatePtr& a, const ibQueryPredicatePtr& b);
ibQueryPredicatePtr OrWith(const ibQueryPredicatePtr& a, const ibQueryPredicatePtr& b);

ibQueryPredicatePtr AccountDimensionCondition(const ibValueMetaObjectAccountingRegister* reg,
                                              const ibBackendQueryable* source, bool creditSide,
                                              const ibValue& condition)
{
	ibValueContainer* pairs = nullptr;
	if (reg == nullptr || !condition.ConvertToValue(pairs) || pairs == nullptr)
		return nullptr;

	ibQueryPredicatePtr folded;
	for (const std::pair<ibValue, ibValue>& entry : pairs->Entries()) {
		if (entry.first.GetType() == TYPE_STRING || entry.first.IsEmpty())
			continue;   // a dimension by name — not this converter's business

		ibQueryPredicatePtr perKind;
		for (unsigned int idx = 0; idx < reg->GetAccountDimensionCount(); idx++) {
			const ibValueMetaObjectAttributeBase* kindSlot = reg->GetAccountDimensionKindSlot(creditSide, idx);
			const ibValueMetaObjectAttributeBase* slot     = reg->GetAccountDimensionSlot(creditSide, idx);
			if (kindSlot == nullptr || slot == nullptr)
				continue;

			ibQueryCondition kindLeaf;
			kindLeaf.m_col   = ColumnOn(source, kindSlot);
			kindLeaf.m_op    = ibQueryFilterOp::Equal;
			kindLeaf.m_value = entry.first;

			ibQueryCondition valueLeaf;
			valueLeaf.m_col   = ColumnOn(source, slot);
			valueLeaf.m_op    = ibQueryFilterOp::Equal;
			valueLeaf.m_value = entry.second;

			perKind = OrWith(perKind, AndWith(ibQueryPredicate::Leaf(kindLeaf), ibQueryPredicate::Leaf(valueLeaf)));
		}

		// Several breakdowns named at once narrow together — "the contractor is X AND the contract is Y".
		folded = AndWith(folded, perKind);
	}
	return folded;
}

// The caller's condition over the register's own DIMENSIONS. Re-pointed at this source's columns by
// name, exactly as the accumulation register does — the filter is written once and applies to
// whichever surface a reading happens to stand on.
void WhereCondition(ibDataQueryBuilder& b, const ibBackendQueryable* source, const ibQueryPredicatePtr& filter)
{
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> leaves;
	ibRegFlatLeaves(filter, leaves);
	for (const auto& leaf : leaves) {
		const ibBackendQueryColumn* here = leaf.first != nullptr && source != nullptr
			? source->ResolveColumnByName(leaf.first->GetName()) : nullptr;
		if (here != nullptr)
			b.Where(here, leaf.second);
	}
}

// ⭐⭐ AN INACTIVE MOVEMENT EXISTS AND COUNTS FOR NOTHING — and a reading over the MOVEMENTS is the
// one place that has to say so out loud.
//
// The meaning is declared once, as the totals delta's GUARD (accountingRegisterMetadataSchema.cpp): an
// entry written but not in effect occupies its row and moves no figure. A stored total was accumulated
// under that guard, so a reading of the VIEW inherits it and needs nothing; a reading of the raw
// movements inherits nothing at all. Left unsaid there, the same question answered from the two
// surfaces gives two different numbers — and which one a caller gets depends on whether the driver can
// maintain derived state at all.
void WhereActive(ibDataQueryBuilder& b, const ibValueMetaObjectAccountingRegister* reg,
                 const ibBackendQueryable* source, bool onMovements)
{
	if (!onMovements || reg == nullptr)
		return;
	if (const ibValueMetaObjectAttributeBase* active = reg->GetRegisterActive())
		if (const ibBackendQueryColumn* here = ColumnOn(source, active))
			b.Where(here, ibValue(true));
}

// ⭐ A BOUNDARY IS A DATE, AND MAY NAME THE DOCUMENT AT IT. The date half is applied here; the
// document half is what separates three postings sharing one instant, and it is read from the
// movements — which is exactly where these readings stand, so it costs a comparison and not a
// mechanism.
//
// ⏳ The recorder tie-break is not applied yet: it needs the recorder's field tuple compared as an
// ordering, and this arc's first live run is about the figures. Left NAMED rather than silently
// dropped — a boundary that quietly ignores its document answers about a moment nobody asked for.
// ⭐⭐ THE GRAIN, AND THE TWO ARMS OF ONE VIEW.
//
// A maintained total is complete only down to the grain it is stored at — a DAY. Everything that
// happened inside the current day is in the movements and nowhere else, so the view carries BOTH: the
// stored rows and the movements that came after them. That is what makes "the balance at noon"
// answerable at all.
//
// And it is exactly why a reader must SAY which arm it wants. Silence is not the neutral answer here,
// it is the wrong one: every movement of the current grain would be counted twice — once rolled into
// the day's total, once as itself — and the result looks entirely plausible.
//
// A stored row has no recorder, so the recorder column IS the row's own answer to "which arm am I".
// No flag column had to be invented for it.
ibQueryPredicatePtr StoredArm(const ibBackendQueryColumn* recorderCol)
{
	return recorderCol != nullptr ? ibQueryPredicate::Null(recorderCol, /*negated*/ false) : nullptr;
}

ibQueryPredicatePtr MovementArm(const ibBackendQueryColumn* recorderCol)
{
	return recorderCol != nullptr ? ibQueryPredicate::Null(recorderCol, /*negated*/ true) : nullptr;
}

ibQueryPredicatePtr Compare(const ibBackendQueryColumn* col, ibQueryFilterOp op, const ibValue& value)
{
	if (col == nullptr || value.IsEmpty())
		return nullptr;
	ibQueryCondition leaf;
	leaf.m_col   = col;
	leaf.m_op    = op;
	leaf.m_value = value;
	return ibQueryPredicate::Leaf(leaf);
}

ibQueryPredicatePtr AndWith(const ibQueryPredicatePtr& a, const ibQueryPredicatePtr& b)
{
	if (!a) return b;
	if (!b) return a;
	return ibQueryPredicate::Compose(ibQueryPredicateKind::And, a, b);
}

ibQueryPredicatePtr OrWith(const ibQueryPredicatePtr& a, const ibQueryPredicatePtr& b)
{
	if (!a) return b;
	if (!b) return a;
	return ibQueryPredicate::Compose(ibQueryPredicateKind::Or, a, b);
}

// The cut for a reading that stops AT A MOMENT (a balance).
//
//   no moment at all      the stored arm alone. The trigger keeps the current grain's row up to date,
//                         so this is not stale — it is complete at grain resolution.
//   a moment              the stored rows BELOW that moment's grain, plus the movements from the start
//                         of that grain up to the moment. The grain the moment falls into cannot be
//                         taken as a stored row: it also holds movements after the moment.
ibQueryPredicatePtr ArmCutAtMoment(const ibBackendQueryColumn* recorderCol, const ibBackendQueryColumn* periodCol,
                                   const ibRegBound& bound, ibTotalsPeriod grain)
{
	if (recorderCol == nullptr)
		return nullptr;   // one arm only: nothing to cut

	if (bound.IsEmpty() || bound.m_date.GetType() != TYPE_DATE)
		return StoredArm(recorderCol);

	const ibValue floor = ibValue(ibTruncateToPeriod(bound.m_date.GetDateTime(), grain));
	const ibQueryFilterOp upperOp = bound.m_excluding ? ibQueryFilterOp::Less : ibQueryFilterOp::LessEqual;

	const ibQueryPredicatePtr stored = AndWith(StoredArm(recorderCol),
		Compare(periodCol, ibQueryFilterOp::Less, floor));
	const ibQueryPredicatePtr moves = AndWith(MovementArm(recorderCol),
		AndWith(Compare(periodCol, ibQueryFilterOp::GreaterEqual, floor),
		        Compare(periodCol, upperOp, bound.m_date)));

	return OrWith(stored, moves);
}

// The cut for a reading over an INTERVAL (turnovers). Either end may fall inside a grain, and each
// partial end is answered by the movements while everything between them comes from the stored rows.
ibQueryPredicatePtr ArmCutOverRange(const ibBackendQueryColumn* recorderCol, const ibBackendQueryColumn* periodCol,
                                    const ibRegBound& begin, const ibRegBound& end, ibTotalsPeriod grain)
{
	if (recorderCol == nullptr)
		return nullptr;

	// Where the stored rows may start: the first WHOLE grain at or after the lower bound. The grain the
	// bound falls INTO holds movements before it as well, so it cannot be taken as a row.
	ibValue storedFrom, headFrom;
	if (!begin.IsEmpty() && begin.m_date.GetType() == TYPE_DATE) {
		const wxDateTime moment = begin.m_date.GetDateTime();
		const wxDateTime floor  = ibTruncateToPeriod(moment, grain);
		headFrom   = begin.m_date;
		storedFrom = (floor == moment) ? begin.m_date : ibValue(ibNextPeriodStart(moment, grain));
	}

	// Where they must stop: the start of the grain the upper bound falls into — that grain's movements
	// answer the rest.
	ibValue storedTo, tailFrom;
	if (!end.IsEmpty() && end.m_date.GetType() == TYPE_DATE) {
		const wxDateTime moment = end.m_date.GetDateTime();
		storedTo = ibValue(ibTruncateToPeriod(moment, grain));
		tailFrom = storedTo;
	}

	// ⚠ AN EXCLUDING BOUND EXCLUDES THE BOUND, NOT THE FIRST STORED GRAIN.
	//
	// Where the bound fell INSIDE a grain, `storedFrom` is already the start of the NEXT one — a
	// different moment, and one the caller never named. Comparing it with `>` there drops a whole grain
	// of stored totals (a day, and everything in it) while the head arm covers only up to `storedFrom`,
	// so the day disappears from the answer entirely. The exclusion belongs to the boundary, and it is
	// applied only where `storedFrom` IS the boundary.
	const bool storedStartsAtBound = !storedFrom.IsEmpty() && storedFrom == headFrom;

	ibQueryPredicatePtr stored = StoredArm(recorderCol);
	stored = AndWith(stored, Compare(periodCol,
		storedStartsAtBound && begin.m_excluding ? ibQueryFilterOp::Greater : ibQueryFilterOp::GreaterEqual, storedFrom));
	stored = AndWith(stored, Compare(periodCol, ibQueryFilterOp::Less, storedTo));

	// The head: movements from the lower bound up to the first whole grain.
	ibQueryPredicatePtr head;
	if (!storedFrom.IsEmpty() && !storedStartsAtBound) {
		head = AndWith(MovementArm(recorderCol),
			AndWith(Compare(periodCol, begin.m_excluding ? ibQueryFilterOp::Greater : ibQueryFilterOp::GreaterEqual, headFrom),
			        Compare(periodCol, ibQueryFilterOp::Less, storedFrom)));
	}

	// The tail: movements of the grain the upper bound falls into, up to the bound itself.
	ibQueryPredicatePtr tail;
	if (!tailFrom.IsEmpty()) {
		tail = AndWith(MovementArm(recorderCol),
			AndWith(Compare(periodCol, ibQueryFilterOp::GreaterEqual, tailFrom),
			        Compare(periodCol, end.m_excluding ? ibQueryFilterOp::Less : ibQueryFilterOp::LessEqual, end.m_date)));
	}

	return OrWith(stored, OrWith(head, tail));
}

void WherePeriodAtMost(ibDataQueryBuilder& b, const ibBackendQueryColumn* periodCol, const ibRegBound& bound)
{
	if (periodCol == nullptr || bound.IsEmpty())
		return;
	b.WhereCompare(periodCol, bound.m_excluding ? ibQueryFilterOp::Less : ibQueryFilterOp::LessEqual, bound.m_date);
}

void WherePeriodRange(ibDataQueryBuilder& b, const ibBackendQueryColumn* periodCol,
                      const ibRegBound& begin, const ibRegBound& end)
{
	if (periodCol == nullptr)
		return;
	if (!begin.IsEmpty())
		b.WhereCompare(periodCol, begin.m_excluding ? ibQueryFilterOp::Greater : ibQueryFilterOp::GreaterEqual, begin.m_date);
	if (!end.IsEmpty())
		b.WhereCompare(periodCol, end.m_excluding ? ibQueryFilterOp::Less : ibQueryFilterOp::LessEqual, end.m_date);
}

// ============================================================================
// The figures — one side of one resource
// ============================================================================

// ⭐⭐ THE SIDE IS A CONDITION OVER THE ROW, and which condition depends on the register's mode:
//
//   one-sided        the row IS one side, and RecordType says which. A debit figure sums the resource
//                    on debit rows and nothing on credit ones.
//   correspondence   the row is a whole posting and carries BOTH accounts, so a side is not a property
//                    of the row at all — it is decided by WHICH account the reading grouped by. The
//                    caller therefore sums the resource whole, once per side, in two passes.
//
// ⚠ ALGEBRAIC, ALWAYS. A negative amount is a reversal and must lower its own side rather than be
// normalised into an entry on the other one — an ordinary SUM already does exactly that, which is why
// no reversal handling appears anywhere (arc §4.7).
ibQueryColumnExprPtr SideFigure(const ibValueMetaObjectAccountingRegister* reg,
                                const ibValueMetaObjectAttributeBase* resource, bool credit)
{
	if (resource == nullptr)
		return nullptr;

	if (reg->IsCorrespondence())
		return ibQueryColumnExpr::Col(resource);   // the pass decides the side; the row carries no flag

	const ibValueMetaObjectAttributeBase* recordType = reg->GetRegisterRecordType();
	if (recordType == nullptr)
		return ibQueryColumnExpr::Col(resource);

	ibQueryCondition leaf;
	leaf.m_col   = recordType;
	leaf.m_op    = ibQueryFilterOp::Equal;
	leaf.m_value = ibValue::CreateEnumObject<ibValueEnumAccountingRegisterRecordType>(
		credit ? ibAccountingRecordType::eCredit : ibAccountingRecordType::eDebit);

	std::vector<std::pair<ibQueryPredicatePtr, ibQueryColumnExprPtr>> cases;
	cases.push_back({ ibQueryPredicate::Leaf(leaf), ibQueryColumnExpr::Col(resource) });
	return ibQueryColumnExpr::Case(std::move(cases), ibQueryColumnExpr::Const(ibValue(ibNumber())));
}

// THE IDENTITY OF A ROW IS ITS KEY VALUES — used to merge the two passes of a correspondence reading
// (a debit pass and a credit pass produce rows for the same account and must land on one row).
//
// It used to be those values folded into a string through GetHashKey and joined with \x1f: a text
// conversion per key column per row, and then a std::map comparing the results character by
// character. The values compare as values now (ibValueSeqHash / ibValueSeqEqual, value.h) — a
// reference still keys by its guid, which is the property the fold needs, without rendering it.
using ibAcctKey     = std::vector<ibValue>;
using ibAcctIndex   = std::unordered_map<ibAcctKey, size_t, ibValueSeqHash, ibValueSeqEqual>;

// Per-ACCOUNT caches. The account is one value, so it keys directly (ibValueHash / ibValueEqual)
// — a reference compares by guid there, which is what these caches meant by asking for its
// GetHashKey and then keying a std::map by the resulting text.
using ibAcctTypeCache    = std::unordered_map<ibValue, int, ibValueHash, ibValueEqual>;
using ibAcctKindSet      = std::unordered_set<ibValue, ibValueHash, ibValueEqual>;
// Account -> the dimension kinds it keeps SUMMARY ONLY. Read whole, in one go (see
// SummaryOnlyKindsByAccount): a map of an answer, not a cache of one.
using ibAcctSummaryMap = std::unordered_map<ibValue, ibAcctKindSet, ibValueHash, ibValueEqual>;

// ⭐⭐ ONE PASS OF A READING — which account column the rows are grouped by, which side's slots the
// breakdown is read from, and which figure the sums land in.
//
// A one-sided register makes ONE pass: every row carries RecordType, so both figures are computed in
// the same scan by a conditional sum. A correspondence register makes TWO, because there the side is
// not a property of the row at all — it is decided by WHICH account column the reading grouped by, and
// one row contributes to the debit figure of one account and the credit figure of another.
struct ibAcctPass
{
	const ibValueMetaObjectAttributeBase* m_account      = nullptr;
	bool                                  m_creditSide   = false;   // whose slots the breakdown reads
	bool                                  m_creditFigure = false;   // where this pass's sums land
	bool                                  m_bothFigures  = false;   // one-sided: both, told apart by RecordType
};

std::vector<ibAcctPass> PassesOf(const ibValueMetaObjectAccountingRegister* reg)
{
	std::vector<ibAcctPass> passes;
	if (reg->IsCorrespondence()) {
		passes.push_back({ reg->GetRegisterAccount(),   false, false, false });
		passes.push_back({ reg->GetRegisterAccountCr(), true,  true,  false });
	}
	else {
		passes.push_back({ reg->GetRegisterAccount(),   false, false, true });
	}
	return passes;
}

// ⭐⭐ THE KEY A PASS WRITES ITS ROWS UNDER — the names and the breakdown that produced them.
//
// A correspondence reading makes TWO passes and each breaks down ITS OWN side: the debit pass by the
// debit account's kinds, the credit pass by the credit account's. Those are two lists the CALLER
// hands over, so they need be neither the same length nor the same shape — ask for one kind on debit
// and three on credit and the two passes build keys of two different arities.
//
// Taken from the first pass alone, the credit rows are then poured under debit names: value 1 under
// name 1, and everything after it one place out. So the names belong to the PASS, and a row says
// which pass wrote it.
struct ibAcctKeyLayout
{
	std::vector<wxString>              m_columns;     // the key's column names, in the order it is built
	std::vector<ibAcctBreakdownColumn> m_breakdown;   // the breakdown half of that key
};

// One accumulated output row: the key as VALUES (so it can be written into the RAM table) and the
// figures by their published column name. Rows from the two passes of a correspondence reading meet
// here — same account, same breakdown, one row with both sides filled.
struct ibAcctRow
{
	std::vector<ibValue>         m_key;
	std::map<wxString, ibValue>  m_figures;
	size_t                       m_layout = 0;   // which pass's names this key is spelled in
};

// Rows in insertion order, paired with the key tuple that identifies each — declared HERE rather
// than beside ibAcctKey above, because it names ibAcctRow and that has to exist first.
using ibAcctRowList = std::vector<std::pair<ibAcctKey, ibAcctRow>>;

// The published name of a figure — the resource plus the side's suffix, spelled through ibAcctFigure
// so the shape's column and the value written into it cannot come from two different spellings.
wxString FigureName(const ibValueMetaObjectAttributeBase* resource, const wxString& suffix)
{
	return resource->GetName() + suffix;
}

// ⭐⭐ THE ACCOUNT'S TYPE IS A RULE FOR READING THE OTHER SIDE, not a storage shape.
//
// Both sides are always stored and always computed; what the type decides is what the opposite one
// MEANS. On an active account a credit entry is a REVERSAL — it reduces the debit balance and is not a
// credit balance of its own — so the two fold into one number with a sign. A passive account is the
// mirror. An **active-passive** account folds NOT AT ALL: it can stand on both sides at once (classic
// mutual settlements, where the same account owes some counterparties and is owed by others), and a
// receivable of 100 against a payable of 100 is not "zero" — that answer is wrong in a way no
// formatting can undo.
//
// The type is read from the ACCOUNT, once per account: it is data, and the engine has been storing it
// for years without ever asking (GetAccountType had no callers at all).
// Declared here, defined below beside the flag it reads: the fold needs it, and the fold reads better
// next to the key it is folding than at the bottom of the file.
ibAcctSummaryMap SummaryOnlyKindsByAccount(const ibValueMetaObjectChartOfAccounts* chart);

// Drop the turnovers-only breakdowns out of a BALANCE key and merge whatever rows then coincide.
//
// ⚠ MERGED, NOT JUST BLANKED. Two rows that differed only by a settlement document are ONE balance row
// once that breakdown is gone, and leaving them side by side would report the same balance twice — a
// report that adds up to double.
void FoldOutSummaryOnly(const ibValueMetaObjectChartOfAccounts* chart,
                        const std::vector<ibAcctKeyLayout>& layouts,
                        ibAcctRowList& rows)
{
	if (layouts.empty() || rows.empty())
		return;

	const ibAcctSummaryMap summaryOnlyByAccount = SummaryOnlyKindsByAccount(chart);

	ibAcctRowList merged;
	ibAcctIndex index;

	for (auto& entry : rows) {
		ibAcctRow row = entry.second;
		// Each row is read by the layout of the pass that wrote it — the breakdown of the OTHER pass
		// sits at other positions of the key and would name the wrong slots.
		const std::vector<ibAcctBreakdownColumn>& breakdown =
			layouts[row.m_layout < layouts.size() ? row.m_layout : 0].m_breakdown;
		if (row.m_key.empty() || breakdown.empty()) {
			merged.push_back(entry);
			continue;
		}

		static const ibAcctKindSet s_none;
		const auto foundKinds = summaryOnlyByAccount.find(row.m_key.front());
		const ibAcctKindSet& summaryOnly = foundKinds != summaryOnlyByAccount.end() ? foundKinds->second : s_none;

		// The key is [account] then, per breakdown column, either (kind, value) or just the value —
		// the same order it was built in.
		size_t pos = 1;
		for (const ibAcctBreakdownColumn& column : breakdown) {
			const bool kindStored = BreakdownCarriesKind(column);
			const size_t kindPos  = kindStored ? pos : std::string::npos;
			const size_t valuePos = kindStored ? pos + 1 : pos;
			pos += kindStored ? 2 : 1;

			if (valuePos >= row.m_key.size())
				break;

			const ibValue kind = kindStored ? row.m_key[kindPos] : column.m_requestedKind;
			if (kind.IsEmpty() || summaryOnly.find(kind) == summaryOnly.end())
				continue;

			// No balance is kept along this breakdown: the slot leaves the key entirely — its kind with
			// it, so the row does not claim a breakdown it is not reporting.
			row.m_key[valuePos] = ibValue();
			if (kindStored)
				row.m_key[kindPos] = ibValue();
		}

		const ibAcctKey& identity = row.m_key;
		const auto found = index.find(identity);
		if (found == index.end()) {
			index[identity] = merged.size();
			merged.push_back({ identity, row });
			continue;
		}

		ibAcctRow& into = merged[found->second].second;
		for (const auto& figure : row.m_figures)
			into.m_figures[figure.first] = ibValue(into.m_figures[figure.first].GetNumber() + figure.second.GetNumber());
	}

	rows.swap(merged);
}

int AccountTypeOf(const ibValue& account, ibAcctTypeCache& cache)
{
	if (account.IsEmpty())
		return ibAccountType::eActivePassive;   // nothing to fold by: keep both sides

	const ibValue& key = account;
	const auto found = cache.find(key);
	if (found != cache.end())
		return found->second;

	int accountType = ibAccountType::eActivePassive;

	ibValueReferenceDataObject* reference = nullptr;
	if (account.ConvertToValue(reference) && reference != nullptr) {
		const ibValueMetaObjectChartOfAccounts* chart = nullptr;
		if (reference->GetMetaObject()->ConvertToValue(chart) && chart != nullptr
			&& chart->GetAccountType() != nullptr) {
			ibValue declared;
			if (reference->GetValueByMetaID(chart->GetAccountType()->GetMetaID(), declared))
				accountType = declared.GetInteger();
		}
	}

	cache[key] = accountType;
	return accountType;
}

// ⭐⭐ "TURNOVERS ONLY" — a kind that takes part in turnover and keeps NO BALANCE along it.
//
// Of the four or five breakdowns on an account, some are full: a balance is carried per their values.
// Others are turnover-only cuts — the classic split is a balance per contract but only turnovers per
// settlement document. So:
//
//     THE BALANCE KEY IS NARROWER THAN THE TURNOVER KEY, and which slots drop is decided by DATA —
//     the flag on a row of THIS account's kinds table.
//
// Storage is untouched: a movement carries all its slots regardless. The flag changes only how the
// data is READ, which is why a user may flip it in enterprise mode and nothing breaks — no column
// appears or disappears and no stored row is reinterpreted.
// ⭐⭐ THE CHART ARRIVES AS METADATA, NOT OUT OF THE VALUE.
//
// Which chart this is, and which of its columns carry the kind and the flag, are facts of the
// CONFIGURATION — the caller is a totals reading and holds them already. Digging them out of the
// runtime account value meant asking a reference for its metaobject in order to learn something the
// register had known all along, and it put a runtime object on the path of a question that has none.
// ⭐⭐ ONE READING, NOT ONE PER ACCOUNT — WHICH IS WHY THERE IS NO CACHE.
//
// A cache exists to make a repeated expensive answer cheap. The answer stopped being expensive the
// moment it became a query, and it stopped being repeated the moment the query could bring every
// account's rows home at once: the whole table is a handful of rows per account, and a totals
// reading wants all of them anyway. So this returns the MAP, built once, and the callers look up
// in it — no lazy filling, no per-account round trip, nothing to invalidate.
ibAcctSummaryMap SummaryOnlyKindsByAccount(const ibValueMetaObjectChartOfAccounts* chart)
{
	ibAcctSummaryMap byAccount;

	if (chart == nullptr)
		return byAccount;

	{
		{
			const ibValueMetaObjectAccountDimensionKindsTable* table = chart->GetAccountDimensionKindsTable();
			if (table != nullptr) {
				// ⭐⭐ THE FLAG IS DATA, SO IT IS READ AS DATA.
				//
				// 🛑 This used to open the account's CARD — `reference->GetObject()` — to reach one
				// checkbox on one row of its kinds table. Opening a card is not a read: it creates a
				// runtime object, which needs a module manager, which needs a session. A rented read
				// (a list page, a background composition) deliberately has none, so the assert fired
				// there and a totals reading could not run at all (measured 2026-09-01).
				//
				// ⭐ And the cost was wrong even where it worked: a card materialised PER ACCOUNT,
				// with its modules and its whole attribute set, to answer a question the table
				// answers by itself. The section is an ordinary query source — it is what
				// `ChartOfAccounts.<chart>.AccountDimensionKinds` names — so this asks it.
				const ibValueMetaObjectAttributeBase* kindColumn    = table->GetAccountDimensionKind();
				const ibValueMetaObjectAttributeBase* summaryColumn = table->GetSummaryOnly();
				const ibBackendQueryable* rows = table->GetQueryable();

				if (rows != nullptr && kindColumn != nullptr && summaryColumn != nullptr) {

					const ibTabularQueryable* section = dynamic_cast<const ibTabularQueryable*>(rows);
					const ibBackendQueryColumn* ownerCol = section != nullptr ? section->OwnerRefColumn() : nullptr;
					const ibBackendQueryColumn* kindCol    = ColumnOn(rows, kindColumn);
					const ibBackendQueryColumn* summaryCol = ColumnOn(rows, summaryColumn);

					if (ownerCol != nullptr && kindCol != nullptr && summaryCol != nullptr) {
						ibDataQueryBuilder b;
						b.From(rows);
						// ⚠ NOT FILTERED BY THE CALLER'S RIGHTS, for the same reason the readings above
						// are not: which slots a total keeps is a property of the chart, not of who is
						// looking, and a key that narrows per user is a key that disagrees with itself.
						b.WithAccessPolicy(nullptr);
						b.Select(ownerCol,   wxT("Account"));
						b.Select(kindCol,    kindColumn->GetName());
						b.Select(summaryCol, summaryColumn->GetName());

						// ⚠ NO FILTER, DELIBERATELY. Narrowing to one account is what made this a call
						// per account; the whole table is what a totals reading ends up needing, and
						// asking for it once costs one statement instead of one per row of the report.
						ibDataQueryResult sel = b.Execute(ibReadPageRequest{});
						while (sel.Next()) {
							if (!sel.GetValue(summaryCol).GetBoolean())
								continue;
							const ibValue kind = sel.GetValue(kindCol);
							if (kind.IsEmpty())
								continue;
							const ibValue owner = sel.GetValue(ownerCol);
							if (!owner.IsEmpty())
								byAccount[owner].insert(kind);
						}
					}
				}
			}
		}
	}

	return byAccount;
}

// Fold one pair of figures by the account's type. Applied at READ time, which is why declining to fold
// costs nothing and an unfolded reading of the same data stays available.
void FoldSideByAccountType(int accountType, ibValue& debit, ibValue& credit)
{
	if (accountType == ibAccountType::eActivePassive)
		return;

	const ibNumber net = debit.GetNumber() - credit.GetNumber();
	if (accountType == ibAccountType::eActive) {
		debit  = ibValue(net);            // a credit entry REDUCED the debit balance
		credit = ibValue(ibNumber());
	}
	else {
		debit  = ibValue(ibNumber());
		credit = ibValue(ibNumber() - net);
	}
}

} // namespace

// ============================================================================
// THE SHAPE — metadata plus the call's arguments, and no database
// ============================================================================

const ibBackendQueryable* ibValueMetaObjectAccountingRegister::GetShapeQueryable(
	ibAcctShape shape, const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibRegFold& fold) const
{
	const bool correspondence = IsCorrespondence();

	// THE CACHE KEY IS THE WHOLE QUESTION, not just the shape: this register's virtual tables have a
	// different column set per CALL (the requested kinds decide how many breakdown columns there are),
	// and the granularity decides whether a period column exists at all. A key that ignored either
	// would hand a reader the shape built for somebody else's arguments.
	// A KIND CONTRIBUTES ITS HASH **AND ITS TYPE NUMBER**, not its text.
	//
	// This is a cache key, so a collision is not a slow path — it hands the reader a shape built for
	// somebody else's arguments. A hash alone can collide; the class id pins WHICH KIND OF VALUE
	// produced it, so two different kinds can no longer meet on one bucket by accident. Together they
	// identify the argument as tightly as the old text did, without rendering a reference through
	// wxString::Format to get there.
	wxString key = wxString::Format(wxT("%d|%d|%d|"), static_cast<int>(shape),
		static_cast<int>(fold.m_kind), static_cast<int>(fold.m_unit));
	const auto appendKind = [&key](const wxChar side, const ibValue& kind) {
		key += wxString::Format(wxT("%c%llu:%llu|"), side,
			static_cast<unsigned long long>(kind.GetValueHash()),
			static_cast<unsigned long long>(kind.GetClassType()));
	};
	for (const ibValue& kind : kindsDr) appendKind(wxT('d'), kind);
	for (const ibValue& kind : kindsCr) appendKind(wxT('c'), kind);

	// …and WHAT IT WAS BUILT FROM is remembered beside it, so a register whose attributes changed
	// (a resource added, a dimension re-typed) rebuilds instead of answering from a stale shape. The
	// neighbour learned this one the hard way: a shape asked for before the metadata was read stayed
	// empty for the life of the session.
	wxString builtFrom;
	ibRegSignAttribute(builtFrom, GetRegisterPeriod());
	ibRegSignAttribute(builtFrom, GetRegisterAccount());
	ibRegSignAttribute(builtFrom, GetRegisterAccountCr());
	for (unsigned int idx = 0; idx < GetAccountDimensionCount(); idx++) {
		ibRegSignAttribute(builtFrom, GetRegisterAccountDimension(idx));
		ibRegSignAttribute(builtFrom, GetRegisterAccountDimensionCr(idx));
	}
	for (const auto dimension : GetDimensionArrayObject()) ibRegSignAttribute(builtFrom, dimension);
	for (const auto resource  : GetResourceArrayObject())  ibRegSignAttribute(builtFrom, resource);

	// ⚠ THE RELATION'S NAME IS NOT THE CACHE KEY. Several calls — different kinds, a different
	// granularity — are different SHAPES over the same named relation, which is exactly why the key is
	// the whole call while the name follows the shape alone.
	const wxString shapeName = wxString::Format(wxT("%s_%d"), GetRegisterTableNameDB(), static_cast<int>(shape));
	return m_surfaces.Obtain(key, builtFrom, shapeName, GetMetaData(),
		[&](std::vector<ibTempColumn>& columns, ibMetaID& synthetic)
	{

	// --- the period, when this reading has one ------------------------------------------------
	// A reading that folds the interval WHOLE carries no date: the row covers begin-to-end and was
	// written by no one document. Showing a Period column over it promises a value the rows will not
	// have — which is exactly the silent-empty this whole file is arranged to avoid.
	//
	// ⭐ A ROW AT A MOVEMENT'S OWN GRAIN HAS A DATE TOO. `Recorder` and `Record` fold no interval at
	// all — one row per document, per line — so the row genuinely carries the moment it happened. This
	// is the neighbour's rule said in this register's words (ibRegisterViewColumnFits): Recorder = the
	// period and the document, Record = the period, the document and the line within it.
	const bool atMovementGrain = fold.FromMovements();
	const bool withPeriod = (shape == ibAcctShape::Records) || fold.HasPeriod() || atMovementGrain;
	if (withPeriod && GetRegisterPeriod() != nullptr)
		columns.push_back(ibRegAttributeColumn(GetRegisterPeriod()));

	// --- the movement's own identity — where a row IS a movement, or a document's worth of them ---
	if (shape == ibAcctShape::Records || atMovementGrain) {
		if (GetRegisterRecorder() != nullptr)
			columns.push_back(ibRegAttributeColumn(GetRegisterRecorder()));
		if ((shape == ibAcctShape::Records || fold.HasLineNumber()) && GetRegisterLineNumber() != nullptr)
			columns.push_back(ibRegAttributeColumn(GetRegisterLineNumber()));
		// The side a LINE stands on. A total has none — it reports a debit figure and a credit figure
		// side by side — so this belongs to the movements listing alone.
		if (shape == ibAcctShape::Records && !correspondence && GetRegisterRecordType() != nullptr)
			columns.push_back(ibRegAttributeColumn(GetRegisterRecordType()));

		// ⭐ WHETHER THE LINE IS IN FORCE — published, because this listing does NOT filter by it.
		//
		// A total holds only what counts (the trigger's guard saw to that), so Active has no meaning on
		// one. A listing is the other case: it reports the lines as they were written, inactive ones
		// among them, and a reader who cannot see the flag cannot tell a posting in force from one that
		// was taken out of it. Reporting all the rows and saying nothing is the one combination that
		// misleads; the column costs a field and hands the decision back — show, filter, or grey.
		if (shape == ibAcctShape::Records && GetRegisterActive() != nullptr)
			columns.push_back(ibRegAttributeColumn(GetRegisterActive()));
	}

	// --- the accounts -------------------------------------------------------------------------
	// Under their own metaID, so a composed read reaches them by the attribute exactly as it would on
	// the movements table: a virtual table is interchangeable with the register as a source, never a
	// parallel vocabulary.
	const bool bothSides = PairedRow(this, shape);
	if (GetRegisterAccount() != nullptr)
		columns.push_back(ibRegAttributeColumn(GetRegisterAccount()));
	if (bothSides && GetRegisterAccountCr() != nullptr)
		columns.push_back(ibRegAttributeColumn(GetRegisterAccountCr()));

	// --- the breakdown ------------------------------------------------------------------------
	// Positional names, the caller's order. The TYPE is the slot's — the chart of characteristic
	// types' own composition — so a column of this table admits exactly what a slot admits.
	const auto addBreakdown = [&](bool creditSide, const std::vector<ibValue>& kinds) {
		const wxString prefix = SidePrefix(this, shape, creditSide);
		const unsigned int width = BreakdownWidth(this, kinds);
		for (unsigned int no = 0; no < width; no++) {
			const ibValueMetaObjectAttributeBase* slot = GetAccountDimensionSlot(creditSide, no);
			const ibValueMetaObjectAttributeBase* sample = slot != nullptr ? slot : GetAccountDimensionSlot(creditSide, 0);
			if (sample == nullptr)
				continue;
			const wxString name = AccountDimensionColumnName(prefix, no + 1);

			// ⭐⭐ THE KIND IS PART OF THE KEY, SO IT IS PART OF THE ANSWER.
			//
			// An UNREQUESTED breakdown column is a POSITION, and a position means different things on
			// different accounts: slot 1 is a counterparty on 62 and an item on 41. So every reading
			// that folds groups by the kind column beside the value — one kind per row by construction
			// — and puts it in the key. A shape that then keeps the kind to itself publishes a key it
			// does not report: the value is poured, the kind is dropped on the way (SetByName finds no
			// such column and says nothing), and every reader downstream that needs it — the
			// turnovers-only suppression first of all — asks a table that has forgotten.
			//
			// A REQUESTED column needs none of this: it MEANS the kind the caller named, and a second
			// column saying so would be the same answer twice.
			const ibValueMetaObjectAttributeBase* kindSlot = GetAccountDimensionKindSlot(creditSide, no);
			if (kinds.empty() && kindSlot != nullptr)
				columns.push_back(ibTempColumn(name + wxT("Kind"), name + wxT("Kind"),
				                               kindSlot->GetTypeDesc(), synthetic++));

			columns.push_back(ibTempColumn(name, name, sample->GetTypeDesc(), synthetic++));
		}
	};
	addBreakdown(/*creditSide*/ false, kindsDr);
	if (bothSides)
		addBreakdown(/*creditSide*/ true, kindsCr);

	// --- the register's own dimensions — the standing cut, the same on every line ---------------
	for (const auto dimension : GetDimensionArrayObject())
		if (dimension != nullptr)
			columns.push_back(ibRegAttributeColumn(dimension));

	// --- the figures --------------------------------------------------------------------------
	//
	// ⭐ TAKEN AS (FIGURE, SIDE) RATHER THAN AS THE SPELLED SUFFIX — because the column owes THREE
	// names and two of them are built from that pair: `Resource1BalanceDr` for a query, and "Amount
	// Balance Dr" for whoever reads the column. Handed the finished suffix, this would have had to
	// recover the side by looking at the last two letters, which is classification by spelling —
	// right until a figure ends in "Cr" for a reason of its own.
	const auto addFigure = [&](const ibValueMetaObjectAttributeBase* resource, const wxString& figure, bool credit) {
		const wxString suffix = ibRegSidedFigure(figure, credit);
		columns.push_back(ibTempColumn(resource->GetName() + suffix,
		                               resource->GetName() + wxT("_") + suffix,
		                               resource->GetTypeDesc(), synthetic++,
		                               ibRegFigureColumnCaption(resource->GetSynonym(), ibRegSidedCaption(figure, credit))));
	};

	// A figure with NO side — one row is a pair of accounts, so there is one number and nothing to
	// tell apart. Same pairing of the three names, minus the side.
	const auto addSidelessFigure = [&](const ibValueMetaObjectAttributeBase* resource, const wxString& figure) {
		columns.push_back(ibTempColumn(resource->GetName() + figure,
		                               resource->GetName() + wxT("_") + figure,
		                               resource->GetTypeDesc(), synthetic++,
		                               ibRegFigureColumnCaption(resource->GetSynonym(), ibRegFigureCaption(figure))));
	};

	// ⭐⭐ A BALANCE EXISTS ONLY WHERE A BALANCE IS KEPT. The resource says so: a balance-bearing one
	// (the amount) is carried on both sides and answers "what is on hand"; one that is not (a quantity)
	// has no balance at all — only what moved, as a debit sum and a credit sum. Publishing a
	// `QuantityBalanceDr` column would promise a figure this register does not keep, and the reader
	// would get a plausible number that is the difference of two unrelated flows.
	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const bool keepsBalance = resource->IsBalanceResource();
		switch (shape) {
		case ibAcctShape::Balance:
			if (!keepsBalance)
				break;
			addFigure(resource, ibRegFigure::Balance, /*credit*/ false);
			addFigure(resource, ibRegFigure::Balance, /*credit*/ true);
			break;
		case ibAcctShape::Turnovers:
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ false);
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ true);
			break;
		// One row is a PAIR of accounts, so there is one figure: what moved from that credit to that
		// debit. A "TurnoverDr" here would be the same number under a second name.
		case ibAcctShape::DrCrTurnovers:
			addSidelessFigure(resource, ibRegFigure::Turnover);
			break;
		// The turnover half is reported for EVERY resource; the balance halves only where a balance is
		// kept. A quantitative register therefore shows what moved in and what moved out, and no
		// opening or closing at all — which is what "we do not keep a balance there" means.
		case ibAcctShape::BalanceAndTurnovers:
			if (keepsBalance) {
				addFigure(resource, ibRegFigure::OpeningBalance, /*credit*/ false);
				addFigure(resource, ibRegFigure::OpeningBalance, /*credit*/ true);
			}
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ false);
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ true);
			if (keepsBalance) {
				addFigure(resource, ibRegFigure::ClosingBalance, /*credit*/ false);
				addFigure(resource, ibRegFigure::ClosingBalance, /*credit*/ true);
			}
			break;
		// A movement line reports the resource ITSELF — it is not folded, so it has no side and no
		// suffix. Under its own metaID, like every other attribute of the line.
		case ibAcctShape::Records:
			columns.push_back(ibRegAttributeColumn(resource));
			break;
		}
	}

	});
}

// ============================================================================
// The readings
// ============================================================================

namespace {

// Seed the returned table from the SHAPE — its column ids, names and types. Not invented here: the
// rows must be findable by exactly the columns the source publishes, and a second spelling survives
// only until somebody crosses between the query road and the runtime one.
void SeedFromShape(ibQueryRamTable& table, const ibBackendQueryable* shape)
{
	if (shape == nullptr)
		return;
	for (const ibBackendQueryColumn* col : shape->GetColumns())
		if (col != nullptr)
			table.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());
}

// Pour the accumulated rows into the table, dropping the ones where nothing happened.
//
// ⚠ A KEY WHOSE EVERY FIGURE FOLDED TO NOTHING IS NOT A ROW — the same answer the accumulation
// register gives, and for the same reason: a balance of zero is the absence of stock, not a fact
// about it. The rule is "any figure non-zero", so a debit of 10 against a credit of 10 KEEPS the row
// (something did happen there), while a reversal that undid itself does not.
//
// ⚠ …AND IT IS NOT ALWAYS THIS PASS'S JUDGEMENT TO MAKE. A reading that still has a running step to
// perform (opening balances rolled forward through the periods) cannot tell an empty row from a full
// one until the roll has run: a key carried in with stock and untouched in this period has zeros in
// every movement figure and is a perfectly good row. Such a reading pours everything and prunes
// afterwards, which is what `dropEmpty` is for.
void PourRows(ibQueryRamTable& table,
              const std::vector<ibAcctKeyLayout>& layouts,
              const ibAcctRowList& rows,
              bool dropEmpty = true)
{
	if (layouts.empty())
		return;

	for (const auto& entry : rows) {
		if (dropEmpty) {
			bool anyNonZero = false;
			for (const auto& figure : entry.second.m_figures)
				if (!(figure.second.GetNumber() == ibNumber()))
					anyNonZero = true;
			if (!entry.second.m_figures.empty() && !anyNonZero)
				continue;
		}

		// The names of the PASS that wrote this key, never the first pass's: two passes break down two
		// different sides by two lists the caller chose, and those lists need not be the same length.
		const std::vector<wxString>& keyColumns =
			layouts[entry.second.m_layout < layouts.size() ? entry.second.m_layout : 0].m_columns;

		const long row = table.AppendRow();
		for (size_t i = 0; i < keyColumns.size() && i < entry.second.m_key.size(); i++)
			table.SetByName(row, keyColumns[i], entry.second.m_key[i]);
		for (const auto& figure : entry.second.m_figures)
			table.SetByName(row, figure.first, figure.second);
	}

}

// A RAM column's id by its published name — 0 when the shape does not publish it, which reads back as
// an empty cell and never as somebody else's column.
ibMetaID RamColumnIdByName(const ibQueryRamTable& table, const wxString& name)
{
	for (const ibQueryRamColumn& col : table.Columns())
		if (col.m_name == name)
			return col.m_id;
	return 0;
}

} // namespace

// ⭐⭐ A BALANCE IS EVERY MOVEMENT UP TO A MOMENT, FOLDED BY ACCOUNT.
//
// Both sides are always reported — `<Resource>BalanceDr` and `<Resource>BalanceCr` — and they are NOT
// collapsed into one signed number here. Whether they may be collapsed is the ACCOUNT's business
// (active folds to debit, passive to credit, active-passive folds not at all — the same account can
// owe and be owed at once, and "zero" is the one answer that is wrong in a way no formatting can
// undo). That reading of the type is the next step of the arc; the shape it needs is this one.
ibQueryRamTable ibValueMetaObjectAccountingRegister::ComputeBalance(
	const ibRegBound& bound, const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibValue& condition) const
{
	ibQueryRamTable retTable;
	const ibBackendQueryable* shape = GetShapeQueryable(ibAcctShape::Balance, kindsDr, kindsCr);
	SeedFromShape(retTable, shape);

	const ibBackendQueryable* movements = GetQueryable();
	if (movements == nullptr || shape == nullptr)
		return retTable;

	// ⭐ THE ACCOUNTS, AND HOW THEY WERE ASKED FOR. Named IN HIERARCHY, an account brings everything
	// subordinate to it AND reports it under itself — which is the one thing an accounting register
	// does that no other register does. The filter and the fold are two halves of that single word,
	// so they are built together, once, and read by every pass below.
	// ⭐ THE SUBTREE IS READ THROUGH THE ACCOUNT COLUMN, on the MOVEMENTS — deliberately, whichever
	// surface a given pass then reads. What the walk needs is the column's TARGET (the chart of
	// accounts) and the chart's own parent map, and neither depends on whether this pass stands on the
	// totals view or on the lines: the hierarchy is the chart's, not the surface's.
	const ibQueryHierarchyScope scopeDr = ScopeFromAccountCondition(movements, GetRegisterAccount(),   accountDr);
	const ibQueryHierarchyScope scopeCr = ScopeFromAccountCondition(movements, GetRegisterAccountCr(), accountCr);
	ibAcctRowList rows;   // insertion-ordered; the map is the index into it
	ibAcctIndex index;
	std::vector<ibAcctKeyLayout> layouts;              // one per pass — see ibAcctKeyLayout

	for (const ibAcctPass& pass : PassesOf(this)) {
		if (pass.m_account == nullptr)
			continue;

		const size_t layoutIndex = layouts.size();

		// ⭐⭐ WHICH SURFACE THIS PASS READS — and the answer is not simply "the totals if they exist".
		//
		// The stored totals are keyed by ONE account per row, which is what makes them small. A question
		// about CORRESPONDENCE ("the balance of 51 against 62") asks about the OTHER account on the same
		// movement, and that account is not in this table — it never was, by construction. So a reading
		// that filters by the opposite side falls back to the movements, where both are present.
		//
		// This is a routing decision, not a limitation quietly admitted: answering it from the totals
		// would mean ignoring the filter and returning a plausible, wrong number.
		const ibQueryPredicatePtr oppositeFilter = pass.m_creditSide ? accountDr : accountCr;
		const bool useTotals = HasMaterializedViews() && (!IsCorrespondence() || oppositeFilter == nullptr);
		const ibBackendQueryable* source = useTotals ? GetTurnoverViewQueryable(pass.m_creditSide) : movements;
		if (source == nullptr)
			source = movements;

		const ibBackendQueryColumn* accountCol = ColumnOn(source, pass.m_account);
		// This pass reads ONE side, so it reads that side's account scope.
		const ibQueryHierarchyScope& scope = pass.m_creditSide ? scopeCr : scopeDr;
		const ibBackendQueryColumn* periodCol  = ColumnOn(source, GetRegisterPeriod());

		ibDataQueryBuilder b;
		b.From(source);
		// ⚠ NOT FILTERED BY THE CALLER'S RIGHTS. A total computed from the rows a particular user
		// happens to see is a wrong total, and a wrong total does not look like an error.
		b.WithAccessPolicy(nullptr);

		// ⭐ THE GRAIN CUT. On the totals the view has two arms and the reading must say which rows it
		// takes from each; on the movements there is one arm and the moment is an ordinary comparison.
		const ibQueryPredicatePtr armCut = useTotals
			? ArmCutAtMoment(ColumnOn(source, GetRegisterRecorder()), periodCol, bound, GetTotalsPeriodUnit())
			: nullptr;
		if (armCut)
			b.Where(armCut);
		else
			WherePeriodAtMost(b, periodCol, bound);

		WhereActive(b, this, source, /*onMovements*/ source == movements);

		// The account arguments are FILTERS on their own side, whichever pass is running: "the balance
		// of 51" and "…in correspondence with 62" are two different questions and both are asked here.
		WhereAccount(b, ColumnOn(source, GetRegisterAccount()),   scopeDr);
		WhereAccount(b, ColumnOn(source, GetRegisterAccountCr()), scopeCr);
		WhereCondition(b, source, filter);
		// …and the breakdown half of the same condition, asked of THIS pass's slots.
		if (const ibQueryPredicatePtr slots = AccountDimensionCondition(this, source, pass.m_creditSide, condition))
			b.Where(slots);

		b.GroupBy(accountCol);

		std::vector<ibAcctBreakdownColumn> breakdown;
		// EACH PASS BREAKS DOWN ITS OWN SIDE: the debit pass by the debit account's kinds, the credit pass by
		// the credit account's. They are different accounts, so one list would force a breakdown that only
		// one of them has.
		AddBreakdown(b, this, source, ibAcctShape::Balance, pass.m_creditSide,
			pass.m_creditSide && IsCorrespondence() ? kindsCr : kindsDr, /*group*/ true, breakdown);

		std::vector<const ibBackendQueryColumn*> dimensions;
		for (const auto dimension : GetDimensionArrayObject())
			if (const ibBackendQueryColumn* here = ColumnOn(source, dimension)) {
				b.GroupBy(here);
				dimensions.push_back(here);
			}

		// ⭐ THE FIGURE, ON WHICHEVER SURFACE. A stored total already holds the side apart
		// (`<Res>TurnoverDr` / `TurnoverCr` — that is what the trigger accumulated); the movements hold
		// the raw resource and the side has to be picked out of the row. Same sum, two spellings of what
		// is summed, and nothing above this lambda knows which one it got.
		const auto figureExpr = [&](const ibValueMetaObjectAttributeBase* resource, bool credit) -> ibQueryColumnExprPtr {
			if (useTotals) {
				const ibBackendQueryColumn* stored = source->ResolveColumnByName(
					resource->GetName() + (credit ? ibAcctFigure::TurnoverCr : ibAcctFigure::TurnoverDr));
				return stored != nullptr ? ibQueryColumnExpr::Col(stored) : nullptr;
			}
			return SideFigure(this, resource, credit);
		};

		// Which figures this pass produces: one side in a correspondence register (the side IS the
		// account column it grouped by), both in a one-sided one (told apart by RecordType).
		std::vector<std::pair<wxString, const ibValueMetaObjectAttributeBase*>> figures;
		for (const auto resource : GetResourceArrayObject()) {
			// Only a balance-bearing resource has a balance to report. A quantity is summed as
			// turnover and nowhere else — the register does not keep a quantitative balance, so there
			// is nothing here to fold up to a moment.
			if (resource == nullptr || !resource->IsBalanceResource())
				continue;
			if (pass.m_bothFigures) {
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, figureExpr(resource, false),
				            FigureName(resource, ibAcctFigure::BalanceDr));
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, figureExpr(resource, true),
				            FigureName(resource, ibAcctFigure::BalanceCr));
				figures.push_back({ FigureName(resource, ibAcctFigure::BalanceDr), resource });
				figures.push_back({ FigureName(resource, ibAcctFigure::BalanceCr), resource });
			}
			else {
				const wxString name = FigureName(resource,
					pass.m_creditFigure ? ibAcctFigure::BalanceCr : ibAcctFigure::BalanceDr);
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, figureExpr(resource, pass.m_creditFigure), name);
				figures.push_back({ name, resource });
			}
		}

		// THIS PASS'S NAMES, IN THE ORDER THIS PASS BUILDS ITS KEY.
		std::vector<wxString> keyColumns;
		keyColumns.push_back(GetRegisterAccount()->GetName());
		AppendBreakdownNames(breakdown, keyColumns);
		for (const auto dimension : dimensions)
			keyColumns.push_back(dimension->GetName());

		// The driver's own words reach the caller untouched — this level has nothing truer to say, so
		// there is nothing here to catch.
		ibDataQueryResult sel = b.SelectAggregate();
		while (sel.Next()) {
			std::vector<ibValue> key;
			// ⭐ REPORTED UNDER THE ACCOUNT THAT WAS NAMED. Where the argument named an account IN
			// HIERARCHY, every subordinate account's row is keyed by the named one, so the ten
			// accounts under "work in progress" become its parts rather than ten separate lines.
			// Asked plainly, or not asked at all, this is the row's own account and nothing folds.
			key.push_back(scope.ReportedUnder(sel.GetValue(accountCol)));
			AppendBreakdownValues(sel, breakdown, key);
			for (const auto dimension : dimensions)
				key.push_back(sel.GetValue(dimension));

			const ibAcctKey& identity = key;
			const auto found = index.find(identity);
			if (found == index.end()) {
				index[identity] = rows.size();
				rows.push_back({ identity, ibAcctRow{ key, {}, layoutIndex } });
			}
			ibAcctRow& row = rows[index[identity]].second;
			// ⚠ ADDED, NOT ASSIGNED. Several groups now legitimately land on one row: an account named
			// in hierarchy folds its subordinates into itself, and each of them arrives as its own
			// group from the server. Assignment kept the last one and silently dropped the rest —
			// which reads as "this account has the balance of whichever subordinate came last".
			for (const auto& figure : figures) {
				ibValue& cell = row.m_figures[figure.first];
				const ibValue arriving = sel.GetColumn(figure.first);

				cell = cell.IsEmpty() ? arriving : ibValue(cell.GetNumber() + arriving.GetNumber());
			}
		}

		layouts.push_back(ibAcctKeyLayout{ std::move(keyColumns), std::move(breakdown) });
	}

	// ⭐ A BALANCE IS NOT KEPT ALONG EVERY BREAKDOWN. The kinds an account marks "turnovers only" leave
	// the key here, and the rows that then coincide are merged — otherwise the same balance would be
	// reported once per settlement document that touched it.
	FoldOutSummaryOnly(GetChartOfAccounts(), layouts, rows);

	// ⭐ AND NOW THE ACCOUNT SPEAKS. Up to here both sides were computed and kept apart, which is the
	// only honest way to compute them; the fold is a projection applied at READ time, per account, by
	// the type the account declares about itself. An active-passive one is left alone — that is what it
	// exists for.
	ibAcctTypeCache accountTypes;
	for (auto& entry : rows) {
		const int accountType = AccountTypeOf(
			entry.second.m_key.empty() ? ibValue() : entry.second.m_key.front(), accountTypes);
		for (const auto resource : GetResourceArrayObject()) {
			// ⚠ ONLY WHERE A BALANCE IS KEPT, AND ONLY WHERE ONE WAS COMPUTED. A resource that carries
			// no balance has no pair to fold — and `m_figures[name]` would CREATE the pair rather than
			// find it, so a quantity would acquire a `BalanceDr` of zero that the shape does not even
			// publish. Absent and zero are different statements; this is the indexing that turns one
			// into the other.
			if (resource == nullptr || !resource->IsBalanceResource())
				continue;
			const auto debit  = entry.second.m_figures.find(FigureName(resource, ibAcctFigure::BalanceDr));
			const auto credit = entry.second.m_figures.find(FigureName(resource, ibAcctFigure::BalanceCr));
			if (debit == entry.second.m_figures.end() || credit == entry.second.m_figures.end())
				continue;
			FoldSideByAccountType(accountType, debit->second, credit->second);
		}
	}

	PourRows(retTable, layouts, rows);
	return retTable;
}

// Turnovers — the same fold over an INTERVAL rather than up to a moment, optionally cut into periods.
ibQueryRamTable ibValueMetaObjectAccountingRegister::ComputeTurnover(
	const ibRegBound& begin, const ibRegBound& end,
	const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibRegFold& fold, const ibValue& condition) const
{
	ibQueryRamTable retTable;
	const ibBackendQueryable* shape = GetShapeQueryable(ibAcctShape::Turnovers, kindsDr, kindsCr, fold);
	SeedFromShape(retTable, shape);

	const ibBackendQueryable* movements = GetQueryable();
	if (movements == nullptr || shape == nullptr)
		return retTable;

	const wxString periodName = GetRegisterPeriod() != nullptr ? GetRegisterPeriod()->GetName() : wxString();

	// ⭐⭐ WHAT ONE ROW IS, WHEN THE FOLD STANDS AT A MOVEMENT'S OWN GRAIN.
	//
	// `Recorder` and `Record` are not calendar folds — they ask for a row per DOCUMENT and per LINE.
	// Routed to the movements and then grouped by neither, the reading answered for the whole interval
	// under a word that promised a document per row: one plausible number where tens were asked for.
	// So the movement's identity joins the grouping key, and the shape publishes it (the same rule
	// stated there) — a reader gets the rows the word named, or nothing is called by that word at all.
	const bool atMovementGrain = fold.FromMovements();
	const bool withLineNumber  = fold.HasLineNumber();
	const wxString recorderName = atMovementGrain && GetRegisterRecorder() != nullptr
		? GetRegisterRecorder()->GetName() : wxString();
	const wxString lineName = withLineNumber && GetRegisterLineNumber() != nullptr
		? GetRegisterLineNumber()->GetName() : wxString();
	// A document has a date of its own, so a movement-grained row carries the period too.
	const bool withPeriod = (fold.HasPeriod() || atMovementGrain) && !periodName.IsEmpty();

	ibAcctRowList rows;
	ibAcctIndex index;
	std::vector<ibAcctKeyLayout> layouts;              // one per pass — see ibAcctKeyLayout

	// The accounts as they were ASKED FOR — see ComputeBalance: named in hierarchy, an account brings
	// its subordinates and reports them under itself.
	// ⭐ THE SUBTREE IS READ THROUGH THE ACCOUNT COLUMN, on the MOVEMENTS — deliberately, whichever
	// surface a given pass then reads. What the walk needs is the column's TARGET (the chart of
	// accounts) and the chart's own parent map, and neither depends on whether this pass stands on the
	// totals view or on the lines: the hierarchy is the chart's, not the surface's.
	const ibQueryHierarchyScope scopeDr = ScopeFromAccountCondition(movements, GetRegisterAccount(),   accountDr);
	const ibQueryHierarchyScope scopeCr = ScopeFromAccountCondition(movements, GetRegisterAccountCr(), accountCr);

	for (const ibAcctPass& pass : PassesOf(this)) {
		if (pass.m_account == nullptr)
			continue;

		const size_t layoutIndex = layouts.size();

		// The same routing as the balance: the totals answer unless the question is about the OTHER
		// account of the same movement, which a one-account-per-row table does not carry.
		//
		// ⚠ AND ONE MORE CASE BELONGS TO THE MOVEMENTS: a reading finer than the stored grain. Totals
		// are kept per DAY, so an hourly fold — or one per recorder, per line — cannot be derived from
		// them at all. Sending it to the totals anyway would answer at the wrong granularity, which is
		// a plausible wrong number rather than an error.
		const ibQueryPredicatePtr oppositeFilter = pass.m_creditSide ? accountDr : accountCr;
		const bool finerThanStored = (fold.IsCalendar() && fold.m_unit < GetTotalsPeriodUnit())
			|| fold.FromMovements() || fold.m_kind == ibRegGranularity::Period;
		const bool useTotals = HasMaterializedViews()
			&& (!IsCorrespondence() || oppositeFilter == nullptr)
			&& !finerThanStored;

		const ibBackendQueryable* source = useTotals ? GetTurnoverViewQueryable(pass.m_creditSide) : movements;
		if (source == nullptr)
			source = movements;

		const ibBackendQueryColumn* accountCol = ColumnOn(source, pass.m_account);
		// This pass reads ONE side, so it reads that side's account scope.
		const ibQueryHierarchyScope& scope = pass.m_creditSide ? scopeCr : scopeDr;
		const ibBackendQueryColumn* periodCol  = ColumnOn(source, GetRegisterPeriod());

		ibDataQueryBuilder b;
		b.From(source);
		b.WithAccessPolicy(nullptr);

		// Same cut over an interval: whole grains from the stored rows, the partial ends from the
		// movements. Ask for whole days and the stored rows answer alone; ask noon-to-noon and only the
		// two ends come from the movements.
		const ibQueryPredicatePtr armCut = useTotals
			? ArmCutOverRange(ColumnOn(source, GetRegisterRecorder()), periodCol, begin, end, GetTotalsPeriodUnit())
			: nullptr;
		if (armCut)
			b.Where(armCut);
		else
			WherePeriodRange(b, periodCol, begin, end);

		WhereActive(b, this, source, /*onMovements*/ source == movements);

		WhereAccount(b, ColumnOn(source, GetRegisterAccount()),   scopeDr);
		WhereAccount(b, ColumnOn(source, GetRegisterAccountCr()), scopeCr);
		WhereCondition(b, source, filter);
		// …and the breakdown half of the same condition, asked of THIS pass's slots.
		if (const ibQueryPredicatePtr slots = AccountDimensionCondition(this, source, pass.m_creditSide, condition))
			b.Where(slots);

		b.GroupBy(accountCol);

		std::vector<ibAcctBreakdownColumn> breakdown;
		AddBreakdown(b, this, source, ibAcctShape::Turnovers, pass.m_creditSide,
			pass.m_creditSide && IsCorrespondence() ? kindsCr : kindsDr, /*group*/ true, breakdown);

		std::vector<const ibBackendQueryColumn*> dimensions;
		for (const auto dimension : GetDimensionArrayObject())
			if (const ibBackendQueryColumn* here = ColumnOn(source, dimension)) {
				b.GroupBy(here);
				dimensions.push_back(here);
			}

		// ⭐ THE PERIODICITY IS THE GROUPING KEY OF THE FOLD, not a filter applied after it. A calendar
		// unit TRUNCATES the period; the register's own period groups by the column as it stands; a
		// movement-grained fold groups by it whole (a document happened at one moment); the interval
		// read whole groups by neither and carries no date at all.
		if (periodCol != nullptr) {
			if (fold.IsCalendar())
				b.GroupByExpr(ibQueryColumnExpr::PeriodTrunc(ibQueryColumnExpr::Col(periodCol), fold.m_unit), periodName);
			else if (fold.m_kind == ibRegGranularity::Period || atMovementGrain)
				b.GroupBy(periodCol);
		}

		// …and the movement's own identity, where the fold asked for it.
		const ibBackendQueryColumn* recorderCol = recorderName.IsEmpty() ? nullptr : ColumnOn(source, GetRegisterRecorder());
		const ibBackendQueryColumn* lineCol     = lineName.IsEmpty()     ? nullptr : ColumnOn(source, GetRegisterLineNumber());
		if (recorderCol != nullptr)
			b.GroupBy(recorderCol);
		if (lineCol != nullptr)
			b.GroupBy(lineCol);

		// The same two spellings of "what is summed" as the balance uses — stored side columns on the
		// totals, a side picked out of the row on the movements.
		const auto figureExpr = [&](const ibValueMetaObjectAttributeBase* resource, bool credit) -> ibQueryColumnExprPtr {
			if (useTotals) {
				const ibBackendQueryColumn* stored = source->ResolveColumnByName(
					resource->GetName() + (credit ? ibAcctFigure::TurnoverCr : ibAcctFigure::TurnoverDr));
				return stored != nullptr ? ibQueryColumnExpr::Col(stored) : nullptr;
			}
			return SideFigure(this, resource, credit);
		};

		std::vector<std::pair<wxString, const ibValueMetaObjectAttributeBase*>> figures;
		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr)
				continue;
			if (pass.m_bothFigures) {
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, figureExpr(resource, false),
				            FigureName(resource, ibAcctFigure::TurnoverDr));
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, figureExpr(resource, true),
				            FigureName(resource, ibAcctFigure::TurnoverCr));
				figures.push_back({ FigureName(resource, ibAcctFigure::TurnoverDr), resource });
				figures.push_back({ FigureName(resource, ibAcctFigure::TurnoverCr), resource });
			}
			else {
				const wxString name = FigureName(resource,
					pass.m_creditFigure ? ibAcctFigure::TurnoverCr : ibAcctFigure::TurnoverDr);
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, figureExpr(resource, pass.m_creditFigure), name);
				figures.push_back({ name, resource });
			}
		}

		// THIS PASS'S NAMES, IN THE ORDER THIS PASS BUILDS ITS KEY — and the two passes need not agree
		// on the breakdown, so the names go with the rows rather than with the reading.
		std::vector<wxString> keyColumns;
		keyColumns.push_back(GetRegisterAccount()->GetName());
		AppendBreakdownNames(breakdown, keyColumns);
		for (const auto dimension : dimensions)
			keyColumns.push_back(dimension->GetName());
		if (withPeriod)
			keyColumns.push_back(periodName);
		if (recorderCol != nullptr)
			keyColumns.push_back(recorderName);
		if (lineCol != nullptr)
			keyColumns.push_back(lineName);

		// ⭐ NOTHING DROPS OUT HERE. "Turnovers only" narrows the BALANCE key, never the turnover one —
		// that is the whole point of the flag: a breakdown along which no balance is kept still takes
		// part in turnover, and a reader may unfold by it.
		ibDataQueryResult sel = b.SelectAggregate();
		while (sel.Next()) {
			std::vector<ibValue> key;
			// ⭐ REPORTED UNDER THE ACCOUNT THAT WAS NAMED. Where the argument named an account IN
			// HIERARCHY, every subordinate account's row is keyed by the named one, so the ten
			// accounts under "work in progress" become its parts rather than ten separate lines.
			// Asked plainly, or not asked at all, this is the row's own account and nothing folds.
			key.push_back(scope.ReportedUnder(sel.GetValue(accountCol)));
			AppendBreakdownValues(sel, breakdown, key);
			for (const auto dimension : dimensions)
				key.push_back(sel.GetValue(dimension));
			// A calendar fold reports the TRUNCATED period, which is an aggregate alias; every other
			// reading of it groups by the column itself and reads it as one.
			if (withPeriod)
				key.push_back(fold.IsCalendar() ? sel.GetColumn(periodName) : sel.GetValue(periodCol));
			if (recorderCol != nullptr)
				key.push_back(sel.GetValue(recorderCol));
			if (lineCol != nullptr)
				key.push_back(sel.GetValue(lineCol));

			const ibAcctKey& identity = key;
			if (index.find(identity) == index.end()) {
				index[identity] = rows.size();
				rows.push_back({ identity, ibAcctRow{ key, {}, layoutIndex } });
			}
			ibAcctRow& row = rows[index[identity]].second;
			// ⚠ ADDED, NOT ASSIGNED. Several groups now legitimately land on one row: an account named
			// in hierarchy folds its subordinates into itself, and each of them arrives as its own
			// group from the server. Assignment kept the last one and silently dropped the rest —
			// which reads as "this account has the balance of whichever subordinate came last".
			for (const auto& figure : figures) {
				ibValue& cell = row.m_figures[figure.first];
				const ibValue arriving = sel.GetColumn(figure.first);

				cell = cell.IsEmpty() ? arriving : ibValue(cell.GetNumber() + arriving.GetNumber());
			}
		}

		layouts.push_back(ibAcctKeyLayout{ std::move(keyColumns), std::move(breakdown) });
	}

	PourRows(retTable, layouts, rows);
	return retTable;
}

// ⭐⭐ THE CORRESPONDENCE MATRIX — one row per (debit account, credit account) pair.
//
// EXISTS ONLY WHERE A LINE NAMES BOTH SIDES, and that is a decision rather than a gap. A one-sided
// register discards the pairing at WRITE time: three debits and two credits under one recorder are
// five rows, and nothing in them says which debit answered which credit. The old code obtained the
// pair by self-joining the movements on the recorder — which yields M x N pairs and counts each debit
// amount N times. That is not a cheaper equivalent; it is a wrong number that looks like an answer.
ibQueryRamTable ibValueMetaObjectAccountingRegister::ComputeDrCrTurnover(
	const ibRegBound& begin, const ibRegBound& end,
	const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibValue& condition) const
{
	ibQueryRamTable retTable;
	if (!IsCorrespondence())
		return retTable;   // the pairing was never written down; see above

	const ibBackendQueryable* shape = GetShapeQueryable(ibAcctShape::DrCrTurnovers, kindsDr, kindsCr);
	SeedFromShape(retTable, shape);

	const ibBackendQueryable* movements = GetQueryable();
	if (movements == nullptr || shape == nullptr || GetRegisterAccountCr() == nullptr)
		return retTable;

	// The accounts as they were ASKED FOR — see ComputeBalance: named in hierarchy, an account brings
	// its subordinates and reports them under itself.
	// ⭐ THE SUBTREE IS READ THROUGH THE ACCOUNT COLUMN, on the MOVEMENTS — deliberately, whichever
	// surface a given pass then reads. What the walk needs is the column's TARGET (the chart of
	// accounts) and the chart's own parent map, and neither depends on whether this pass stands on the
	// totals view or on the lines: the hierarchy is the chart's, not the surface's.
	const ibQueryHierarchyScope scopeDr = ScopeFromAccountCondition(movements, GetRegisterAccount(),   accountDr);
	const ibQueryHierarchyScope scopeCr = ScopeFromAccountCondition(movements, GetRegisterAccountCr(), accountCr);
	ibDataQueryBuilder b;
	b.From(movements);
	b.WithAccessPolicy(nullptr);

	WherePeriodRange(b, GetRegisterPeriod(), begin, end);
	WhereActive(b, this, movements, /*onMovements*/ true);
	WhereAccount(b, GetRegisterAccount(),   scopeDr);
	WhereAccount(b, GetRegisterAccountCr(), scopeCr);
	WhereCondition(b, movements, filter);

	// ⭐⭐ A PAIRED ROW HAS TWO SETS OF SLOTS, AND THE CONDITION IS ABOUT THE ROW.
	//
	// A line here names both accounts and files its breakdown twice — once per side — so "the entries
	// on contractor X" is answered by EITHER side carrying it. Asked of the debit slots alone, the
	// credit half of every question was silently dropped; demanded of both, the answer would be almost
	// always empty, because the two sides of a posting rarely keep the same analytics (51 keeps no
	// contractor at all). So the two halves are joined by OR, which is what the question means.
	if (const ibQueryPredicatePtr slots = OrWith(
			AccountDimensionCondition(this, movements, /*creditSide*/ false, condition),
			AccountDimensionCondition(this, movements, /*creditSide*/ true,  condition)))
		b.Where(slots);

	b.GroupBy(GetRegisterAccount());
	b.GroupBy(GetRegisterAccountCr());

	std::vector<ibAcctBreakdownColumn> breakdownDr, breakdownCr;
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ false, kindsDr, /*group*/ true, breakdownDr);
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ true,  kindsCr, /*group*/ true, breakdownCr);

	std::vector<const ibValueMetaObjectAttributeBase*> dimensions;
	for (const auto dimension : GetDimensionArrayObject())
		if (dimension != nullptr) { b.GroupBy(dimension); dimensions.push_back(dimension); }

	// ONE figure per resource: a pair of accounts has no sides of its own — what moved from that credit
	// to that debit is a single number, and calling it TurnoverDr would be the same value twice.
	std::vector<wxString> figures;
	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const wxString name = FigureName(resource, ibAcctFigure::Turnover);
		b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, ibQueryColumnExpr::Col(resource), name);
		figures.push_back(name);
	}

	// ONE PASS, so one layout — a paired row names both accounts at once and breaks both sides down in
	// the same read.
	std::vector<ibAcctKeyLayout> layouts(1);
	std::vector<wxString>& keyColumns = layouts.front().m_columns;
	keyColumns.push_back(GetRegisterAccount()->GetName());
	keyColumns.push_back(GetRegisterAccountCr()->GetName());
	AppendBreakdownNames(breakdownDr, keyColumns);
	AppendBreakdownNames(breakdownCr, keyColumns);
	for (const auto dimension : dimensions) keyColumns.push_back(dimension->GetName());

	ibAcctRowList rows;

	ibDataQueryResult sel = b.SelectAggregate();
	while (sel.Next()) {
		std::vector<ibValue> key;
		key.push_back(sel.GetValue(GetRegisterAccount()));
		key.push_back(sel.GetValue(GetRegisterAccountCr()));
		AppendBreakdownValues(sel, breakdownDr, key);
		AppendBreakdownValues(sel, breakdownCr, key);
		for (const auto dimension : dimensions) key.push_back(sel.GetValue(dimension));

		ibAcctRow row{ key, {}, 0 };
		for (const wxString& figure : figures)
			row.m_figures[figure] = sel.GetColumn(figure);
		rows.push_back({ key, row });
	}

	PourRows(retTable, layouts, rows);
	return retTable;
}

// ⭐⭐ THE SAME READING, ENDED AS A RELATION. Every line above that TOUCHES THE QUERY is repeated here
// and nothing that touches the ROWS is — which is the honest split, because the two endings genuinely
// differ in nothing else.
//
// ⚠ AND IT IS BUILT BY CALLING THE SAME STEPS IN THE SAME ORDER, deliberately, rather than by
// factoring the two into one function with a flag. The reason is what the flag would have to skip:
// the row loop needs the breakdown DESCRIPTIONS (which column reads back which slot), and the
// relation needs none of them — a shared builder would carry that machinery for a caller that throws
// it away, and the day somebody edits one ending, "the same order" is checkable by reading two
// adjacent functions rather than by trusting a boolean.
ibQueryRelPtr ibValueMetaObjectAccountingRegister::BuildDrCrTurnoverRelation(
	const ibRegBound& begin, const ibRegBound& end,
	const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibValue& condition) const
{
	if (!IsCorrespondence())
		return nullptr;   // the pairing was never written down

	const ibBackendQueryable* movements = GetQueryable();
	if (movements == nullptr || GetRegisterAccountCr() == nullptr)
		return nullptr;

	const ibQueryHierarchyScope scopeDr = ScopeFromAccountCondition(movements, GetRegisterAccount(),   accountDr);
	const ibQueryHierarchyScope scopeCr = ScopeFromAccountCondition(movements, GetRegisterAccountCr(), accountCr);

	ibDataQueryBuilder b;
	b.From(movements);
	// ⚠ NOT FILTERED BY THE CALLER'S RIGHTS — a total computed from the rows a particular user happens
	// to see is a wrong total, and a wrong total does not look like an error. It is also what makes
	// this composable at all: the door refuses to hand out a relation for a query carrying a policy.
	b.WithAccessPolicy(nullptr);

	WherePeriodRange(b, GetRegisterPeriod(), begin, end);
	WhereActive(b, this, movements, /*onMovements*/ true);
	WhereAccount(b, GetRegisterAccount(),   scopeDr);
	WhereAccount(b, GetRegisterAccountCr(), scopeCr);
	WhereCondition(b, movements, filter);

	if (const ibQueryPredicatePtr slots = OrWith(
			AccountDimensionCondition(this, movements, /*creditSide*/ false, condition),
			AccountDimensionCondition(this, movements, /*creditSide*/ true,  condition)))
		b.Where(slots);

	b.GroupBy(GetRegisterAccount());
	b.GroupBy(GetRegisterAccountCr());

	std::vector<ibAcctBreakdownColumn> breakdownDr, breakdownCr;
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ false, kindsDr, /*group*/ true, breakdownDr);
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ true,  kindsCr, /*group*/ true, breakdownCr);

	for (const auto dimension : GetDimensionArrayObject())
		if (dimension != nullptr)
			b.GroupBy(dimension);

	for (const auto resource : GetResourceArrayObject())
		if (resource != nullptr)
			b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, ibQueryColumnExpr::Col(resource),
				FigureName(resource, ibAcctFigure::Turnover));

	return b.BuildRelation();
}

// ⭐ OPENING, TURNOVER, CLOSING — three questions of the same data, in one row.
//
// The opening balance is the balance as it stood entering the interval, the turnover is what moved
// inside it, and the closing is the two put together. With a periodicity asked for, each period opens
// where the previous one closed — a running step that walks PERIODS (tens), not movements, and it is
// the SAME fold the accumulation register uses (FoldBalancesForward, deliberately not a private helper
// of one register: an accumulation register signs its movements by record type and this one by side,
// but by the time rows reach the fold that difference is already spent).
ibQueryRamTable ibValueMetaObjectAccountingRegister::ComputeBalanceAndTurnover(
	const ibRegBound& begin, const ibRegBound& end,
	const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibRegFold& fold, const ibValue& condition,
	bool fillEmptyPeriods) const
{
	ibQueryRamTable retTable;
	const ibBackendQueryable* shape = GetShapeQueryable(ibAcctShape::BalanceAndTurnovers, kindsDr, kindsCr, fold);
	SeedFromShape(retTable, shape);
	if (shape == nullptr)
		return retTable;

	// The opening balance is a BALANCE READING at the interval's lower edge — the same function, asked
	// one moment earlier. Written as a call rather than as a fourth copy of the aggregate: two spellings
	// of "the balance entering this interval" is how the opening and the closing come to disagree.
	//
	// ⚠ THE WHOLE QUESTION TRAVELS, the condition included. Its breakdown half is asked of the slots
	// per side, so it has to reach the reading that knows which side it is on — dropped here, the
	// opening answered a wider question than the turnover and the closing was the difference of two
	// different reports.
	ibRegBound openingAt;
	openingAt.m_date      = begin.m_date;
	openingAt.m_excluding = !begin.m_excluding;   // the balance BEFORE the interval starts
	const ibQueryRamTable opening = begin.IsEmpty()
		? ibQueryRamTable()
		: ComputeBalance(openingAt, accountDr, accountCr, kindsDr, kindsCr, filter, condition);

	const ibQueryRamTable turnover = ComputeTurnover(begin, end, accountDr, accountCr,
	                                                 kindsDr, kindsCr, filter, fold, condition);

	// ⭐⭐ TWO IDENTITIES, AND THEY ANSWER TWO DIFFERENT QUESTIONS.
	//
	//   the BALANCE key   the account, its breakdown, the register's dimensions. What a balance is
	//                     carried BY. An opening balance has no period and no document, so this is the
	//                     key it is found by, and the key the running roll carries forward.
	//   the ROW key       that, plus whatever makes one output ROW — the period a fold cuts the
	//                     interval into, and the movement's own identity where the fold stands at that
	//                     grain. Two periods of one account are two rows and must be told apart.
	//
	// ⚠ BOTH ARE COMPLETE BEFORE ANYTHING READS BY THEM. Appended afterwards, a period column is
	// missing from every key already built — every period of a key then lands on one row (the last one
	// written wins) and the column the periodicity was asked for is never filled at all.
	std::vector<wxString> keyColumns;
	if (GetRegisterAccount() != nullptr)
		keyColumns.push_back(GetRegisterAccount()->GetName());

	// The breakdown half, named by THE one rule — the same description the readings that queried for
	// these rows built their keys from, so what they wrote and what is read back cannot be two
	// spellings of one order.
	std::vector<ibAcctBreakdownColumn> layout;
	DescribeBreakdown(this, ibAcctShape::BalanceAndTurnovers, /*creditSide*/ false, kindsDr, layout);
	AppendBreakdownNames(layout, keyColumns);

	for (const auto dimension : GetDimensionArrayObject())
		if (dimension != nullptr)
			keyColumns.push_back(dimension->GetName());

	const wxString periodName = GetRegisterPeriod() != nullptr ? GetRegisterPeriod()->GetName() : wxString();
	const bool withPeriod = (fold.HasPeriod() || fold.FromMovements()) && !periodName.IsEmpty();

	std::vector<wxString> rowColumns = keyColumns;
	if (withPeriod)
		rowColumns.push_back(periodName);
	if (fold.FromMovements() && GetRegisterRecorder() != nullptr)
		rowColumns.push_back(GetRegisterRecorder()->GetName());
	if (fold.HasLineNumber() && GetRegisterLineNumber() != nullptr)
		rowColumns.push_back(GetRegisterLineNumber()->GetName());

	const auto cellByName = [](const ibQueryRamTable& table, long row, const wxString& name) {
		for (const ibQueryRamColumn& col : table.Columns())
			if (col.m_name == name)
				return table.GetCell(row, col.m_id);
		return ibValue();
	};

	// The identity of a row over a NAMED list of columns. A column the table does not carry contributes
	// an EMPTY value rather than nothing at all — which is what lets the opening (which has no period
	// and no document) be found by the balance key while the two lists stay the same length.
	// The key of a row as the TUPLE OF ITS VALUES — what the balance seed and the fold both index by
	// (ibBalanceOpening / ibValueSeqHash). No text conversion: the values are compared as values.
	const auto identityValuesOf = [&](const ibQueryRamTable& table, long row, const std::vector<wxString>& names) {
		std::vector<ibValue> values;
		values.reserve(names.size());
		for (const wxString& name : names)
			values.push_back(cellByName(table, row, name));
		return values;
	};
	// Opening balances by the BALANCE key, so a row that carried stock in but saw no movement still reports.
	std::unordered_map<ibAcctKey, long, ibValueSeqHash, ibValueSeqEqual> openingByKey;
	for (long row = 0; row < opening.RowCount(); row++)
		openingByKey[identityValuesOf(opening, row, keyColumns)] = row;

	ibAcctRowList rows;
	ibAcctIndex index;

	std::vector<ibAcctKeyLayout> layouts(1);
	layouts.front().m_columns   = rowColumns;
	layouts.front().m_breakdown = layout;

	const auto rowFor = [&](const ibAcctKey& identity, const std::vector<ibValue>& key) -> ibAcctRow& {
		const auto found = index.find(identity);
		if (found == index.end()) {
			index[identity] = rows.size();
			rows.push_back({ identity, ibAcctRow{ key, {}, 0 } });
		}
		return rows[index[identity]].second;
	};

	const auto keyValues = [&](const ibQueryRamTable& table, long row) {
		std::vector<ibValue> key;
		for (const wxString& name : rowColumns)
			key.push_back(cellByName(table, row, name));
		return key;
	};

	// ⭐⭐ A TURNOVERS-ONLY BREAKDOWN SHOWS NO BALANCE — only the movement part of the period.
	//
	// It is stated here rather than left to happen: the opening reading has already dropped those
	// breakdowns from its key, so a turnover row that carries one would simply fail to find its opening
	// and report zeros. Same numbers, but by accident — and an accident holds only until somebody makes
	// the keys line up again. So the row is ASKED whether it stands on a turnovers-only breakdown, and
	// if it does, the balance columns stay empty on purpose: along that cut no balance is kept, and a
	// zero would claim one was and came to nothing.
	//
	// ⭐ AND THE KIND IS ASKED OF THE LAYOUT, which is the only place that knows it. A column selected
	// BY a kind MEANS that kind and says so itself; a column left to the account was grouped by its
	// kind column and reports it under that column's own alias. Spelling `<name>Kind` out here a second
	// time is how the question came to be asked of a column the shape did not publish — no error, no
	// row, just a flag that never fired.
	const ibAcctSummaryMap summaryOnlyByAccount = SummaryOnlyKindsByAccount(GetChartOfAccounts());
	const auto standsOnTurnoversOnly = [&](const ibQueryRamTable& table, long row) {
		static const ibAcctKindSet s_none;
		const ibValue account = GetRegisterAccount() != nullptr
			? cellByName(table, row, GetRegisterAccount()->GetName()) : ibValue();
		const auto foundKinds = summaryOnlyByAccount.find(account);
		const ibAcctKindSet& summaryOnly = foundKinds != summaryOnlyByAccount.end() ? foundKinds->second : s_none;
		if (summaryOnly.empty())
			return false;

		for (const ibAcctBreakdownColumn& column : layout) {
			const ibValue kind = column.m_byKind
				? column.m_requestedKind
				: (column.m_kindAlias.IsEmpty() ? ibValue() : cellByName(table, row, column.m_kindAlias));
			if (kind.IsEmpty() || summaryOnly.find(kind) == summaryOnly.end())
				continue;
			// …and it only matters if this row actually carries a value along that breakdown.
			if (!cellByName(table, row, column.m_alias).IsEmpty())
				return true;
		}
		return false;
	};

	// The rows of one balance key, kept together: the running roll below walks them, and it can only
	// walk what is grouped. `balanceless` runs parallel to `rows` — a row standing on a turnovers-only
	// breakdown keeps no balance and must not be rolled one forward either.
	std::unordered_map<ibAcctKey, std::vector<size_t>, ibValueSeqHash, ibValueSeqEqual> byKey;
	std::vector<bool> balanceless;

	for (long row = 0; row < turnover.RowCount(); row++) {
		const ibAcctKey identity   = identityValuesOf(turnover, row, rowColumns);
		const ibAcctKey balanceKey = identityValuesOf(turnover, row, keyColumns);
		const bool turnoversOnlyRow = standsOnTurnoversOnly(turnover, row);

		const size_t before = rows.size();
		ibAcctRow& out = rowFor(identity, keyValues(turnover, row));
		if (rows.size() != before) {
			byKey[balanceKey].push_back(rows.size() - 1);
			balanceless.push_back(turnoversOnlyRow);
		}

		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr)
				continue;
			const ibValue turnDr = cellByName(turnover, row, FigureName(resource, ibAcctFigure::TurnoverDr));
			const ibValue turnCr = cellByName(turnover, row, FigureName(resource, ibAcctFigure::TurnoverCr));

			// A resource that keeps no balance reports what MOVED and nothing else — the same rule the
			// shape publishes by, said once more where the figures are filled in.
			const bool keepsBalance = resource->IsBalanceResource();

			ibValue openDr, openCr;
			const auto found = openingByKey.find(balanceKey);
			if (keepsBalance && !turnoversOnlyRow && found != openingByKey.end()) {
				openDr = cellByName(opening, found->second, FigureName(resource, ibAcctFigure::BalanceDr));
				openCr = cellByName(opening, found->second, FigureName(resource, ibAcctFigure::BalanceCr));
			}

			// The turnover part is always reported; the balance columns are left EMPTY along a
			// turnovers-only breakdown — no balance is kept there, and a zero would say one was kept
			// and came to nothing.
			out.m_figures[FigureName(resource, ibAcctFigure::TurnoverDr)] = turnDr;
			out.m_figures[FigureName(resource, ibAcctFigure::TurnoverCr)] = turnCr;
			if (turnoversOnlyRow || !keepsBalance)
				continue;

			// The interval's opening on every row of the key; the roll below corrects each period to
			// the one before it, and where there is no periodicity there is nothing to correct.
			out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceDr)] = openDr;
			out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceCr)] = openCr;
			out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceDr)] = ibValue(openDr.GetNumber() + turnDr.GetNumber());
			out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceCr)] = ibValue(openCr.GetNumber() + turnCr.GetNumber());
		}
	}

	// A key that carried a balance IN and saw nothing move is still a row — it is exactly the row a
	// report shows as "opening = closing". Judged by the movements alone, every one of those vanishes.
	//
	// ⚠ ASKED OF THE BALANCE KEY. With a periodicity the row identities carry a period the opening
	// never has, so no opening would ever be recognised as already reported and every one of them
	// would come back a second time as a period-less duplicate.
	for (const auto& entry : openingByKey) {
		if (byKey.find(entry.first) != byKey.end())
			continue;

		const size_t before = rows.size();
		ibAcctRow& out = rowFor(entry.first, keyValues(opening, entry.second));
		if (rows.size() != before) {
			byKey[entry.first].push_back(rows.size() - 1);
			balanceless.push_back(false);
		}
		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr || !resource->IsBalanceResource())
				continue;
			const ibValue openDr = cellByName(opening, entry.second, FigureName(resource, ibAcctFigure::BalanceDr));
			const ibValue openCr = cellByName(opening, entry.second, FigureName(resource, ibAcctFigure::BalanceCr));
			out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceDr)] = openDr;
			out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceCr)] = openCr;
			out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceDr)] = openDr;
			out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceCr)] = openCr;
		}
	}

	// ⭐⭐ POURED IN THE ORDER THE ROLL NEEDS — every row of one balance key together, and the periods
	// of a key ascending. The running step is sequential by nature (a period's opening IS the previous
	// period's closing), and the order a GROUP BY answers in is the engine's business, not a promise.
	const size_t periodSlot = withPeriod ? keyColumns.size() : rowColumns.size();
	const auto periodOf = [&](const ibAcctRow& row) {
		// static_cast, not a functional cast: wxLongLong_t is `long long` outside MSVC, and a
		// two-word type name cannot be spelled `T(0)` (docs/portability.md).
		return periodSlot < row.m_key.size() ? row.m_key[periodSlot].GetDate() : static_cast<wxLongLong_t>(0);
	};

	ibAcctRowList ordered;
	std::vector<bool> orderedBalanceless;
	ordered.reserve(rows.size());
	orderedBalanceless.reserve(rows.size());
	for (auto& group : byKey) {
		if (withPeriod)
			std::stable_sort(group.second.begin(), group.second.end(),
				[&](size_t a, size_t b) { return periodOf(rows[a].second) < periodOf(rows[b].second); });
		for (const size_t member : group.second) {
			ordered.push_back(std::move(rows[member]));
			orderedBalanceless.push_back(balanceless[member]);
		}
	}

	// ⚠ EVERY ROW GOES IN, EMPTY ONES INCLUDED. Until the roll has run, a row with nothing in its
	// movement figures may still be the row a report shows as "opening = closing" — judged one pass
	// earlier, every one of those disappears. The pruning is below, after the roll.
	PourRows(retTable, layouts, ordered, /*dropEmpty*/ false);

	// ⭐⭐ AND NOW THE PERIODS ARE ROLLED THROUGH — the SAME step the accumulation register takes, in
	// the one place it lives (FoldBalancesForward). What differs between the two registers is how a
	// movement is signed — by record type there, by side here — and that difference is already spent
	// by the time rows reach the fold: each side arrives as its own figure per period.
	//
	// ⚠ ONE SIDE IS ONE SLOT, and the two are handed over as a receipt with no expense EACH. An
	// accounting row does not net its sides: a debit balance and a credit balance are two answers, and
	// an active-passive account keeps both at once. The fold carries its running total per slot, so
	// two slots roll independently and neither is subtracted from the other.
	std::vector<ibBalanceFoldSlot> slots;
	ibBalanceOpening openingSeed;   // keyed by the key TUPLE — see ibBalanceOpening (queryRamTable.h)
	if (withPeriod) {
		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr || !resource->IsBalanceResource())
				continue;
			ibBalanceFoldSlot debit;
			debit.m_receipt  = RamColumnIdByName(retTable, FigureName(resource, ibAcctFigure::TurnoverDr));
			debit.m_turnover = debit.m_receipt;   // rewritten with receipt - expense, i.e. with itself
			debit.m_opening  = RamColumnIdByName(retTable, FigureName(resource, ibAcctFigure::OpeningBalanceDr));
			debit.m_closing  = RamColumnIdByName(retTable, FigureName(resource, ibAcctFigure::ClosingBalanceDr));

			ibBalanceFoldSlot credit;
			credit.m_receipt  = RamColumnIdByName(retTable, FigureName(resource, ibAcctFigure::TurnoverCr));
			credit.m_turnover = credit.m_receipt;
			credit.m_opening  = RamColumnIdByName(retTable, FigureName(resource, ibAcctFigure::OpeningBalanceCr));
			credit.m_closing  = RamColumnIdByName(retTable, FigureName(resource, ibAcctFigure::ClosingBalanceCr));

			slots.push_back(debit);
			slots.push_back(credit);

			// The seed is keyed by the slot the fold carries its running total under — its TURNOVER
			// column, which is what FoldBalancesForward reads back. Keyed by anything else, the seed is
			// never found and every key appears to start at zero: correct turnovers on top of balances
			// that all begin at nothing.
			for (long row = 0; row < opening.RowCount(); row++) {
				std::map<ibMetaID, ibNumber>& seed = openingSeed[identityValuesOf(opening, row, keyColumns)];
				seed[debit.m_turnover]  = cellByName(opening, row, FigureName(resource, ibAcctFigure::BalanceDr)).GetNumber();
				seed[credit.m_turnover] = cellByName(opening, row, FigureName(resource, ibAcctFigure::BalanceCr)).GetNumber();
			}
		}

		std::vector<ibMetaID> foldKey;
		for (const wxString& name : keyColumns)
			foldKey.push_back(RamColumnIdByName(retTable, name));

		const ibMetaID periodId = RamColumnIdByName(retTable, periodName);

		// ⭐⭐ A PERIOD NOTHING MOVED IN IS STILL A PERIOD — when the caller asked for boundaries.
		//
		// Only rows that HAVE movements come back from the read: a key that stood still through March
		// simply has no March row, and a report then shows February and April side by side as though
		// nothing existed in between. That is wrong in the one way this table exists to prevent, because
		// what a balance says about an empty period is precisely that it did not change.
		//
		// ⚠ INSERTED BEFORE THE ROLL, NOT AFTER. The empty rows carry zero turnover and no balance of
		// their own; the roll is what walks the periods in order and carries the previous closing into
		// each opening — so an empty row added first is filled by the mechanism that already exists,
		// while one added afterwards would have to have its balances computed a second way.
		const wxDateTime from = begin.m_date.GetDateTime();
		const wxDateTime to   = end.m_date.GetDateTime();
		if (fillEmptyPeriods && fold.m_kind == ibRegGranularity::Calendar && periodId != 0 && from.IsValid()) {
			// The periods come from the same two functions the grain cut uses, so "a month" means one
			// thing here and there.
			//
			// Keyed by the key VALUES; "this key in this period" is that tuple with the period
			// appended as one more value. The period used to be rendered to ISO text for the same
			// job — a locale-formatted date is not a key — but an instant compares as an instant,
			// so no rendering is needed at all now.
			std::unordered_map<ibAcctKey, long, ibValueSeqHash, ibValueSeqEqual> firstRowOfKey;
			std::unordered_set<ibAcctKey, ibValueSeqHash, ibValueSeqEqual>       filled;
			for (long row = 0; row < retTable.RowCount(); row++) {
				const ibAcctKey key = identityValuesOf(retTable, row, keyColumns);
				if (firstRowOfKey.find(key) == firstRowOfKey.end())
					firstRowOfKey[key] = row;
				ibAcctKey inPeriod = key;
				inPeriod.push_back(retTable.GetCell(row, periodId));
				filled.insert(std::move(inPeriod));
			}

			for (const auto& keyRow : firstRowOfKey) {
				wxDateTime period = ibTruncateToPeriod(from, fold.m_unit);
				while (period.IsValid() && (!to.IsValid() || !period.IsLaterThan(to))) {
					ibAcctKey probe = keyRow.first;
					probe.push_back(ibValue(period));
					if (filled.find(probe) == filled.end()) {
						// The key as it stands on a row that exists, the period that was missing, and
						// nothing else: zero turnover, and balances the roll will supply.
						const long added = retTable.AppendRow();
						for (const wxString& name : keyColumns) {
							const ibMetaID id = RamColumnIdByName(retTable, name);
							if (id != 0)
								retTable.SetCell(added, id, retTable.GetCell(keyRow.second, id));
						}
						retTable.SetCell(added, periodId, ibValue(period));
					}
					const wxDateTime next = ibNextPeriodStart(period, fold.m_unit);
					if (!next.IsValid() || !next.IsLaterThan(period))
						break;   // a unit that does not advance would loop forever
					period = next;
				}
			}
		}

		FoldBalancesForward(retTable, foldKey, periodId, slots, openingSeed);
	}

	// ⭐ THE BALANCES FOLD, THE TURNOVERS DO NOT. Opening and closing are balances and obey the
	// account's type; debit and credit turnover are two different figures about what MOVED and stay
	// apart on every kind of account — folding them would answer "nothing happened" for a month in
	// which a hundred went in and a hundred went out.
	//
	// ⚠ AFTER THE ROLL, NOT BEFORE. The roll rewrites every opening and closing from its running total,
	// so a fold applied first is simply overwritten; and the closing must be folded once it is
	// assembled, since it is opening plus turnover on each side and netting either half first nets the
	// sides at the wrong moment.
	//
	// ⚠ AND ONLY WHERE A BALANCE IS ACTUALLY KEPT. A turnovers-only row was rolled along with the rest
	// — the roll knows nothing about the flag — so its balance cells are unsaid here rather than left
	// carrying a running total of a breakdown that keeps none.
	ibAcctTypeCache accountTypes;
	const wxString accountName = GetRegisterAccount() != nullptr ? GetRegisterAccount()->GetName() : wxString();
	for (long row = 0; row < retTable.RowCount(); row++) {
		const bool keepsBalanceHere = static_cast<size_t>(row) >= orderedBalanceless.size()
			|| !orderedBalanceless[static_cast<size_t>(row)];
		const int accountType = AccountTypeOf(cellByName(retTable, row, accountName), accountTypes);

		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr || !resource->IsBalanceResource())
				continue;

			const auto foldPair = [&](const wxString& debitSuffix, const wxString& creditSuffix) {
				const ibMetaID debitId  = RamColumnIdByName(retTable, FigureName(resource, debitSuffix));
				const ibMetaID creditId = RamColumnIdByName(retTable, FigureName(resource, creditSuffix));
				if (debitId == 0 || creditId == 0)
					return;
				if (!keepsBalanceHere) {
					retTable.SetCell(row, debitId,  ibValue());
					retTable.SetCell(row, creditId, ibValue());
					return;
				}
				ibValue debit  = retTable.GetCell(row, debitId);
				ibValue credit = retTable.GetCell(row, creditId);
				FoldSideByAccountType(accountType, debit, credit);
				retTable.SetCell(row, debitId,  debit);
				retTable.SetCell(row, creditId, credit);
			};

			foldPair(ibAcctFigure::OpeningBalanceDr, ibAcctFigure::OpeningBalanceCr);
			foldPair(ibAcctFigure::ClosingBalanceDr, ibAcctFigure::ClosingBalanceCr);
		}
	}

	// ⚠ AND NOW A ROW WITH NOTHING TO REPORT IS NOT A ROW — the same rule PourRows applies for the
	// other readings, said here because only now is it answerable: a key carried in with stock and
	// untouched inside the interval has zeros in every movement figure and is a perfectly good row.
	std::vector<ibMetaID> figureIds;
	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		for (const wxString& suffix : { ibAcctFigure::TurnoverDr, ibAcctFigure::TurnoverCr,
		                                ibAcctFigure::OpeningBalanceDr, ibAcctFigure::OpeningBalanceCr,
		                                ibAcctFigure::ClosingBalanceDr, ibAcctFigure::ClosingBalanceCr })
			if (const ibMetaID id = RamColumnIdByName(retTable, FigureName(resource, suffix)))
				figureIds.push_back(id);
	}
	for (long row = retTable.RowCount() - 1; row >= 0 && !figureIds.empty(); --row) {
		bool anyNonZero = false;
		for (const ibMetaID id : figureIds)
			if (!(retTable.GetCell(row, id).GetNumber() == ibNumber()))
				anyNonZero = true;
		if (!anyNonZero)
			retTable.EraseRow(row);
	}

	return retTable;
}

// The movement LINES themselves, with the dimension slots widened into a column per requested kind.
// Not a total and never will be: recorder and line number are precisely what a fold discards, so a
// table that reports them can only be the movements. It therefore needs no trigger, no bundle and no
// parity check — it is a projection of a table that is already queryable.
ibQueryRamTable ibValueMetaObjectAccountingRegister::ComputeRecords(
	const ibRegBound& begin, const ibRegBound& end,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibValue& condition,
	const ibValue& order, long top) const
{
	ibQueryRamTable retTable;
	// A row here IS a movement line, so it carries the period, the document and the line within it.
	ibRegFold recordFold;
	recordFold.m_kind = ibRegGranularity::Record;

	const ibBackendQueryable* shape = GetShapeQueryable(ibAcctShape::Records, kindsDr, kindsCr, recordFold);
	SeedFromShape(retTable, shape);

	const ibBackendQueryable* movements = GetQueryable();
	if (movements == nullptr || shape == nullptr)
		return retTable;

	ibDataQueryBuilder b;
	b.From(movements);
	WherePeriodRange(b, GetRegisterPeriod(), begin, end);
	WhereCondition(b, movements, filter);

	// Both sides of a paired line answer the same question — see the correspondence matrix above. A
	// one-sided register has one set of slots and there is no second half to ask.
	if (const ibQueryPredicatePtr slots = OrWith(
			AccountDimensionCondition(this, movements, /*creditSide*/ false, condition),
			IsCorrespondence() ? AccountDimensionCondition(this, movements, /*creditSide*/ true, condition)
			                   : ibQueryPredicatePtr()))
		b.Where(slots);

	// The breakdown is REPORTED, not grouped — a movement line is already as fine as this gets.
	std::vector<ibAcctBreakdownColumn> breakdownDr, breakdownCr;
	AddBreakdown(b, this, movements, ibAcctShape::Records, /*creditSide*/ false, kindsDr, /*group*/ false, breakdownDr);
	if (IsCorrespondence())
		AddBreakdown(b, this, movements, ibAcctShape::Records, /*creditSide*/ true, kindsCr, /*group*/ false, breakdownCr);

	// Everything else the line carries, straight through — under the same metaIDs the shape published,
	// so a reader reaches them exactly as on the movements table.
	std::vector<const ibValueMetaObjectAttributeBase*> straight;
	const auto carry = [&straight](const ibValueMetaObjectAttributeBase* attribute) {
		if (attribute != nullptr) straight.push_back(attribute);
	};
	carry(GetRegisterPeriod());
	carry(GetRegisterRecorder());
	carry(GetRegisterLineNumber());
	// Reported, never filtered on: this reading answers "what was written", and whether a line is in
	// force is one of the things that were written. The shape publishes the column for the same reason.
	carry(GetRegisterActive());
	carry(GetRegisterAccount());
	if (IsCorrespondence()) carry(GetRegisterAccountCr());
	else                    carry(GetRegisterRecordType());
	for (const auto dimension : GetDimensionArrayObject()) carry(dimension);
	for (const auto resource  : GetResourceArrayObject())  carry(resource);

	// HOW THEY COME OUT, and how many — the two arguments only a listing can be asked. Both are part
	// of the QUESTION rather than of paging: `Top` here means "the first N lines of this order", which
	// is why it rides the query instead of being a page the caller turns.
	OrderRecords(b, movements, order);
	if (top > 0)
		b.Top(top);

	ibDataQueryResult sel = b.Execute(ibReadPageRequest{});
	while (sel.Next()) {
		const long row = retTable.AppendRow();
		for (const ibValueMetaObjectAttributeBase* attribute : straight)
			retTable.SetCell(row, attribute->GetMetaID(), sel.GetValue(attribute));
		const auto pourBreakdown = [&](const std::vector<ibAcctBreakdownColumn>& breakdown) {
			for (const ibAcctBreakdownColumn& column : breakdown) {
				if (BreakdownCarriesKind(column))
					retTable.SetByName(row, column.m_kindAlias, ReadBreakdownKind(sel, column));
				retTable.SetByName(row, column.m_alias, ReadBreakdown(sel, column));
			}
		};
		pourBreakdown(breakdownDr);
		pourBreakdown(breakdownCr);
	}

	return retTable;
}

// ⭐⭐ THE SAME LISTING, ENDED AS A RELATION — and here the door lowers it through the READ path, not
// the GROUP BY one. A movement line is already as fine as this data gets: there is nothing to fold,
// so what leaves is a projection with a WHERE, which is exactly the tree `BuildPageIR` builds.
//
// ⚠ AND WITHOUT A PAGE. Paging belongs to whoever composes with this relation; a LIMIT baked in here
// would cap a join's input to one screenful and read as "the register has forty movements".
ibQueryRelPtr ibValueMetaObjectAccountingRegister::BuildRecordsRelation(
	const ibRegBound& begin, const ibRegBound& end,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibValue& condition,
	const ibValue& order, long top) const
{
	const ibBackendQueryable* movements = GetQueryable();
	if (movements == nullptr)
		return nullptr;

	ibDataQueryBuilder b;
	b.From(movements);
	b.WithAccessPolicy(nullptr);
	WherePeriodRange(b, GetRegisterPeriod(), begin, end);
	WhereCondition(b, movements, filter);

	if (const ibQueryPredicatePtr slots = OrWith(
			AccountDimensionCondition(this, movements, /*creditSide*/ false, condition),
			IsCorrespondence() ? AccountDimensionCondition(this, movements, /*creditSide*/ true, condition)
			                   : ibQueryPredicatePtr()))
		b.Where(slots);

	std::vector<ibAcctBreakdownColumn> breakdownDr, breakdownCr;
	AddBreakdown(b, this, movements, ibAcctShape::Records, /*creditSide*/ false, kindsDr, /*group*/ false, breakdownDr);
	if (IsCorrespondence())
		AddBreakdown(b, this, movements, ibAcctShape::Records, /*creditSide*/ true, kindsCr, /*group*/ false, breakdownCr);

	// ⚠ THE ORDER AND THE COUNT RIDE HERE TOO, and that is the difference between them and PAGING.
	// The page was left off this relation deliberately (a LIMIT of the composer's is the composer's);
	// `Top` is not that — it is part of what was ASKED, "the first N of this order", and a relation
	// that dropped it would answer a different question than the same call answers through rows.
	OrderRecords(b, movements, order);
	if (top > 0)
		b.Top(top);

	return b.BuildRelation();
}

// ============================================================================
// The companions — each publishes a shape and runs one compute
// ============================================================================

ibQueryRamTable ibAcctBalanceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeBalance(m_bound, m_accountDr, m_accountCr, m_kindsDr, m_kindsCr, m_filter, m_condition);
}

ibQueryRamTable ibAcctTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeTurnover(m_begin, m_end, m_accountDr, m_accountCr, m_kindsDr, m_kindsCr, m_filter, m_fold, m_condition);
}

// ============================================================================
//  THE SERVER ROAD — the accumulation register's mechanism, carried over
// ============================================================================
//
// Nothing here is new machinery. The read spec, the arm cut between the stored rows and the
// movements, and the boundary tuple are the neighbour's (registerQueryLowering.h,
// databaseMaterializeBuilder.h); what this file supplies is the accounting register's own names —
// which view, which account column, which figures.
//
// ⚠ AND IT ENGAGES ONLY WHERE THE STORED SURFACE CAN ANSWER THE WHOLE QUESTION. Each gate below is a
// question the totals cannot hold, and each one falls back to the RAM reading that answers it today.
// Falling back is not a defeat: the numbers are the same either way, and an answer built from a
// surface that does not carry the question is a plausible wrong number, which is worse than slow.
bool ibAcctTurnoverQueryable::CanReadOnServer() const
{
	if (m_reg == nullptr || !m_reg->HasMaterializedViews())
		return false;   // the driver maintains nothing — live aggregation is the only road

	// TWO TABLES IN CORRESPONDENCE MODE, one per side: a row about ONE account has its debit figure in
	// the debit table and its credit figure in the credit one, and a read spec reads ONE relation. The
	// union of the two sides is its own step, and it is not this one.
	if (m_reg->IsCorrespondence())
		return false;

	// A BREAKDOWN ASKED FOR BY KIND is a CASE over the slots (§ 7.1) — an expression where the spec
	// takes column names. Asked for nothing, the slots are read as they stand, which is exactly what a
	// stored key holds.
	if (!m_kindsDr.empty() || !m_kindsCr.empty())
		return false;

	// A CONDITION may name an ACCOUNT DIMENSION, and that half is a question about the SLOTS which is
	// built per pass. The dimension half (m_filter) is already a predicate and rides the spec.
	if (!m_condition.IsEmpty())
		return false;

	// FINER THAN THE STORED GRAIN — per recorder, per line — is answered by the movements by
	// construction; a stored row is a day and cannot be cut into hours after the fact.
	if (m_fold.FromMovements())
		return false;

	// A CALENDAR fold coarser than the grain is a projection the view already publishes; `Period`
	// groups the stored column as it stands. Both are the spec's business. Nothing else is.
	return true;
}

ibQueryRelPtr ibAcctTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (!CanReadOnServer())
		return nullptr;

	const ibValueMetaObjectAttributeBase* period  = m_reg->GetRegisterPeriod();
	const ibValueMetaObjectAttributeBase* account = m_reg->GetRegisterAccount();
	if (period == nullptr || account == nullptr)
		return nullptr;

	ibMaterializeReadSpec r;
	r.m_view         = m_reg->GetTurnoverViewName(/*creditSide*/ false);
	r.m_periodColumn = ibRegValueField(period);
	r.m_from         = m_begin.m_date;
	r.m_to           = m_end.m_date;
	r.m_dropZeroRows = true;   // the RAM oracle says the same: an all-zero row is not a turnover

	// THE KEY, in the order the table stores it: the account, its breakdown pairs as they stand, then
	// the register's dimensions. Physical fields, because that is what the surface publishes and what
	// a projection of it names.
	const auto appendFields = [&r](const ibValueMetaObjectAttributeBase* attribute) {
		if (attribute == nullptr)
			return;
		for (const wxString& field : ibRegFieldsOf(attribute))
			r.m_keyColumns.push_back(field);
	};
	appendFields(account);
	for (unsigned int idx = 0; idx < m_reg->GetAccountDimensionCount(); idx++) {
		appendFields(m_reg->GetAccountDimensionKindSlot(/*creditSide*/ false, idx));
		appendFields(m_reg->GetAccountDimensionSlot(/*creditSide*/ false, idx));
	}
	for (const auto dimension : m_reg->GetDimensionArrayObject())
		appendFields(dimension);

	// The filters ride INSIDE the subquery, so the selection happens on the server before the outer
	// query sees a row — which is the whole point of handing the door a relation instead of rows.
	for (const ibQueryExprPtr& condition :
		ibRegFilterExprs(m_filter, m_reg != nullptr ? m_reg->GetMetaData() : nullptr))
		r.m_filters.push_back(condition);

	// ⭐⭐ THE CUT, AND THE HALF OF A BOUNDARY THAT ONLY IT CAN SAY. Whole grains come from the stored
	// rows and each partial end from the movements — and where an end names a DOCUMENT, the recorder's
	// field tuple is compared as an ORDERING, decomposed by the same codec the rows were written
	// through. Three postings sharing one instant are three different answers, and this is what tells
	// them apart.
	ibRegFillArmCut(r, m_reg, m_end, m_begin);

	// ⭐ THE PHYSICAL NAMES ARE ASKED FOR, NOT SPELLED — the neighbour's rule, and it is not fussiness:
	// a read spec naming a column the view does not have returns NULLs rather than an error, so a
	// hand-written `<field>_Dr` beside a view that spells it any other way is a silent column of
	// nothing. The logical side is `ibAcctFigure`; the physical side is the view's business.
	const ibBackendQueryable* view = m_reg->GetTurnoverViewQueryable(/*creditSide*/ false);
	if (view == nullptr)
		return nullptr;

	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const wxString base = resource->GetName();
		r.m_columns.push_back({ base + ibAcctFigure::TurnoverDr, ibRegPhysicalOf(view, base + ibAcctFigure::TurnoverDr),
		                        wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true });
		r.m_columns.push_back({ base + ibAcctFigure::TurnoverCr, ibRegPhysicalOf(view, base + ibAcctFigure::TurnoverCr),
		                        wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange, true });
	}

	return RenderMaterializedRead(r, alias);
}

// ============================================================================
//  THE BALANCE, ON THE SERVER — the turnovers folded UP TO a moment, then folded by the ACCOUNT
// ============================================================================
//
// ⭐⭐ THREE FORMS STAND OVER ONE STORED SURFACE, and this is the second of them. What is materialised
// is the TURNOVERS; a balance is that surface summed with one more condition (`UpToTo`), exactly as
// the neighbour reads "what was carried in" with `BeforeFrom` — no join to a second surface, no
// window, one pass.
//
// What is NOT the surface's business is how the two sides fold: active collapses into debit, passive
// into credit, active-passive does not fold at all, and which of the three applies is a declaration
// on the ACCOUNT. On the server that is a JOIN to the chart of accounts and a CASE over its type
// column — built AROUND the materialised read, because `RenderMaterializedRead` hands back a
// RELATION and says so: the caller drops it into a FROM and everything around it (join, outer where,
// paging, RLS) is ordinary SQL. The read spec stays what it is — ONE surface, its two arms, its
// floor and its boundary tuples — and does not grow a second subject.
namespace {

// ⭐ A QUESTION TO THE DATA, NOT TO THE SCHEMA. `FoldOutSummaryOnly` drops a turnovers-only breakdown
// from the balance key and merges the rows that then coincide — a second join, per slot, and the one
// piece of this reading that is not yet on the server.
//
// But the flag is DATA: if no kind anywhere is marked turnovers-only, that fold is a no-op and the
// server road is open for a register with analytics too. Asking costs one row of one small table,
// and the alternative — gating on "does this register declare analytics at all" — would close the
// road for nearly every accounting register over a flag almost nobody sets.
bool AnyTurnoverOnlyKind(const ibValueMetaObjectChartOfAccounts* chart)
{
	if (chart == nullptr)
		return false;
	const ibValueMetaObjectAccountDimensionKindsTable* kinds = chart->GetAccountDimensionKindsTable();
	if (kinds == nullptr)
		return false;
	const ibValueMetaObjectAttributeBase* flag = kinds->GetSummaryOnly();
	const ibBackendQueryable* rows = kinds->GetQueryable();
	if (flag == nullptr || rows == nullptr)
		return false;

	// ⚠⚠ THIS QUESTION MUST NOT BE ABLE TO BREAK A CALLER, and that is not caution — it is what the
	// question IS. It decides an OPTIMISATION: whether this reading may stand on the stored surface or
	// keeps the RAM road, which answers the same numbers. So every failure means "do not take the
	// short road", never "the reading failed".
	//
	// The failure that matters is a lock. The gate is reached through IsComputedInRam(), which anything
	// may ask at any moment — including while a configuration is being APPLIED, with a DDL transaction
	// open on another channel. A read issued into that answers with a deadlock, and unguarded it would
	// abort the apply: an optimisation hint killing a restructuring is the wrong thing failing.
	try {
		ibDataQueryBuilder b;
		b.From(rows);
		b.Where(flag, ibValue(true));
		ibReadPageRequest page;
		page.m_count = 1;   // the existence of ONE such row is the whole answer
		ibDataQueryResult sel = b.Execute(page);
		return sel.Next();
	}
	catch (const ibBackendException&) {
		return true;   // unknown reads as "there is one" — the conservative half, which closes the road
	}
}

} // namespace

bool ibAcctBalanceQueryable::CanReadOnServer() const
{
	if (m_serverRoad >= 0)
		return m_serverRoad != 0;

	m_serverRoad = 0;   // every early return below leaves the RAM road, which answers the same numbers

	if (m_reg == nullptr || !m_reg->HasMaterializedViews())
		return false;   // the driver maintains nothing — live aggregation is the only road

	// TWO TABLES IN CORRESPONDENCE MODE, one per side, and a read spec reads ONE relation.
	if (m_reg->IsCorrespondence())
		return false;

	// A BREAKDOWN ASKED FOR BY KIND is a CASE over the slots — an expression where the spec takes
	// column names. Asked for nothing, the slots are read as they stand, which is what the key holds.
	if (!m_kindsDr.empty() || !m_kindsCr.empty())
		return false;

	// A CONDITION may name an ACCOUNT DIMENSION, and that half is a question about the SLOTS, built
	// per pass. The dimension half (m_filter) is already a predicate and rides the spec.
	if (!m_condition.IsEmpty())
		return false;

	// The fold by account type is a join to the chart, so the chart has to be there and readable.
	const ibValueMetaObjectChartOfAccounts* chart = m_reg->GetChartOfAccounts();
	if (chart == nullptr || chart->GetAccountType() == nullptr || chart->GetDataReference() == nullptr
	    || chart->GetQueryable() == nullptr)
		return false;

	// …and the OTHER fold, the one that is still RAM-only.
	if (AnyTurnoverOnlyKind(chart))
		return false;

	m_serverRoad = 1;
	return true;
}

ibQueryRelPtr ibAcctBalanceQueryable::GetSourceRelation(const wxString& alias) const
{
	if (!CanReadOnServer())
		return nullptr;

	const ibValueMetaObjectAttributeBase*   period  = m_reg->GetRegisterPeriod();
	const ibValueMetaObjectAttributeBase*   account = m_reg->GetRegisterAccount();
	const ibValueMetaObjectChartOfAccounts* chart   = m_reg->GetChartOfAccounts();
	if (period == nullptr || account == nullptr || chart == nullptr)
		return nullptr;

	const ibBackendQueryable* view = m_reg->GetTurnoverViewQueryable(/*creditSide*/ false);
	const ibBackendQueryable* chartRows = chart->GetQueryable();
	if (view == nullptr || chartRows == nullptr)
		return nullptr;

	ibMaterializeReadSpec r;
	r.m_view         = m_reg->GetTurnoverViewName(/*creditSide*/ false);
	r.m_periodColumn = ibRegValueField(period);
	r.m_to           = m_bound.m_date;   // a balance is open-ended below: everything up to the moment
	r.m_dropZeroRows = true;

	// THE KEY, in the order the table stores it — the account, its breakdown pairs as they stand, then
	// the register's dimensions. Physical fields, because that is what the surface publishes.
	const auto appendFields = [&r](const ibValueMetaObjectAttributeBase* attribute) {
		if (attribute == nullptr)
			return;
		for (const wxString& field : ibRegFieldsOf(attribute))
			r.m_keyColumns.push_back(field);
	};
	appendFields(account);
	for (unsigned int idx = 0; idx < m_reg->GetAccountDimensionCount(); idx++) {
		appendFields(m_reg->GetAccountDimensionKindSlot(/*creditSide*/ false, idx));
		appendFields(m_reg->GetAccountDimensionSlot(/*creditSide*/ false, idx));
	}
	for (const auto dimension : m_reg->GetDimensionArrayObject())
		appendFields(dimension);

	// The filters ride INSIDE the subquery, so the selection happens before the outer query sees a row.
	for (const ibQueryExprPtr& condition :
		ibRegFilterExprs(m_filter, m_reg != nullptr ? m_reg->GetMetaData() : nullptr))
		r.m_filters.push_back(condition);

	// THE CUT. Whole grains come from the stored rows and the partial end from the movements — and
	// where the moment names a DOCUMENT, the recorder's field tuple is compared as an ORDERING. A
	// balance is open-ended below, so there is no lower boundary to pass.
	ibRegFillArmCut(r, m_reg, m_bound, ibRegBound());

	// ⭐ SUMMED `UpToTo` — the one difference from the turnover reading, and the whole of what makes
	// this a balance. The physical names are ASKED FOR, never spelled: a spec naming a column the view
	// does not have returns NULLs rather than an error.
	//
	// A BALANCE EXISTS ONLY WHERE A BALANCE IS KEPT — the resource says so, and one that does not keep
	// one publishes no balance column at all (the shape agrees, § 5e), so there is nothing to project.
	std::vector<const ibValueMetaObjectAttributeBase*> balanceResources;
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr || !resource->IsBalanceResource())
			continue;
		balanceResources.push_back(resource);
		const wxString base = resource->GetName();
		r.m_columns.push_back({ base + ibAcctFigure::BalanceDr, ibRegPhysicalOf(view, base + ibAcctFigure::TurnoverDr),
		                        wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true });
		r.m_columns.push_back({ base + ibAcctFigure::BalanceCr, ibRegPhysicalOf(view, base + ibAcctFigure::TurnoverCr),
		                        wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true });
	}
	if (balanceResources.empty())
		return nullptr;   // nothing to report on this road; the RAM reading says the same, in rows

	const wxString innerAlias = alias + wxT("_t");
	const wxString chartAlias = alias + wxT("_a");

	// ⭐⭐ THE JOIN'S `ON` IS NOT SPELLED, IT IS ASKED FOR — both sides through the SAME function, so
	// the equality holds by construction rather than because two suffixes happened to match. Writing
	// `_RRRef` here by hand would be a comparison that compiles, joins nothing, and reports an empty
	// balance that looks exactly like "no movements".
	//
	// LEFT, not inner: an account row that is missing (a chart edited under a posted register) must
	// not make its figures disappear. The CASE below then falls through to "do not fold", which is the
	// answer that loses nothing.
	ibQueryExprPtr on = ibBinOp(ibQueryBinOp::Eq,
		ibCol(innerAlias, ibRegValueField(account)),
		ibCol(chartAlias, ibRegValueField(chart->GetDataReference())));

	ibQueryRelPtr joined = ibJoin(RenderMaterializedRead(r, innerAlias),
		ibScan(chartRows->GetQueryTableName(), chartAlias), on, ibQueryJoinType::Left);

	// ⭐⭐ THE FOLD, AS A CASE OVER WHAT THE ACCOUNT DECLARES — the same rule the RAM reading applies
	// row by row (FoldSideByAccountType), said once to the server:
	//
	//   active         a credit entry REDUCED the debit balance   ->  Dr - Cr , 0
	//   passive        the mirror                                 ->  0 , Cr - Dr
	//   active-passive both sides stand, and folding them is a LOSS: a receivable of 100 against a
	//                  payable of 100 is not zero, and "zero" is wrong in a way no formatting undoes
	std::vector<ibQueryProjItem> projection;
	for (const wxString& field : r.m_keyColumns)
		projection.push_back({ ibCol(innerAlias, field), field });

	const ibQueryExprPtr accountType = ibCol(chartAlias, ibRegValueField(chart->GetAccountType()));
	const auto isType = [&accountType](ibAccountType declared) {
		return ibBinOp(ibQueryBinOp::Eq, accountType, ibConst(ibValue(static_cast<int>(declared))));
	};

	for (const ibValueMetaObjectAttributeBase* resource : balanceResources) {
		const wxString base = resource->GetName();
		const ibQueryExprPtr debit  = ibCol(innerAlias, base + ibAcctFigure::BalanceDr);
		const ibQueryExprPtr credit = ibCol(innerAlias, base + ibAcctFigure::BalanceCr);
		const ibQueryExprPtr zero   = ibConst(ibValue(0.0));

		projection.push_back({ ibCase({ { isType(ibAccountType::eActive),  ibBinOp(ibQueryBinOp::Sub, debit, credit) },
		                                { isType(ibAccountType::ePassive), zero } }, debit),
		                       base + ibAcctFigure::BalanceDr });
		projection.push_back({ ibCase({ { isType(ibAccountType::ePassive), ibBinOp(ibQueryBinOp::Sub, credit, debit) },
		                                { isType(ibAccountType::eActive),  zero } }, credit),
		                       base + ibAcctFigure::BalanceCr });
	}

	return ibSubquery(ibProject(joined, std::move(projection)), alias);
}

// ⭐ THE GATE IS SHORT HERE, AND THAT IS THE POINT. This reading never stood on the stored surface, so
// none of the questions the other gates ask — is the driver materialising, is the grain fine enough,
// was a breakdown asked for by kind — applies to it. It groups the movements, which are always there
// and always carry every column it names. What CAN say no is the door itself: a source that cannot be
// composed with (a policy on the query, a provider with no relation to give) answers null, and the
// RAM ending takes over with the same numbers.
bool ibAcctDrCrTurnoverQueryable::CanReadOnServer() const
{
	return m_reg != nullptr && m_reg->IsCorrespondence();
}

ibQueryRelPtr ibAcctDrCrTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (!CanReadOnServer())
		return nullptr;
	// The alias is the provider's business — it wraps whatever comes back as `FROM (<this>) AS alias`.
	return m_reg->BuildDrCrTurnoverRelation(m_begin, m_end, m_accountDr, m_accountCr,
		m_kindsDr, m_kindsCr, m_filter, m_condition);
}

ibQueryRamTable ibAcctDrCrTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeDrCrTurnover(m_begin, m_end, m_accountDr, m_accountCr, m_kindsDr, m_kindsCr, m_filter, m_condition);
}

// ============================================================================
//  BALANCE AND TURNOVERS, ON THE SERVER — one surface, read three ways at once
// ============================================================================
//
// ⭐⭐ THE SYMBIOSIS IN ONE PASS: what was carried IN (`BeforeFrom`), what MOVED (`InRange`), what
// REMAINS (`UpToTo`). Three different conditions over the same scan, which is why this needs no join
// between them and no window — and why a key carried in with a balance but untouched inside the
// interval still reports. The neighbour does exactly this
// (`ibBalanceAndTurnoverQueryable::GetSourceRelation`); what this adds is the accounting fold.
//
// ⚠ AND THE FOLD APPLIES TO THE BALANCES ONLY. Opening and closing are folded by the account's
// declared type; the TURNOVERS are not, and must not be — a debit turnover and a credit turnover are
// two things that happened, and netting them would report neither. The RAM reading folds exactly
// these two pairs (`foldPair(Opening…)`, `foldPair(Closing…)`) and leaves the movement figures alone.
bool ibAcctBalanceAndTurnoverQueryable::CanReadOnServer() const
{
	if (m_serverRoad >= 0)
		return m_serverRoad != 0;

	m_serverRoad = 0;

	if (m_reg == nullptr || !m_reg->HasMaterializedViews())
		return false;
	if (m_reg->IsCorrespondence())
		return false;               // two tables, one per side; a read spec reads ONE relation
	if (!m_kindsDr.empty() || !m_kindsCr.empty())
		return false;               // a breakdown by kind is a CASE over the slots
	if (!m_condition.IsEmpty())
		return false;               // its slot half is built per side

	if (m_fold.FromMovements())
		return false;               // recorder / line: a fold is exactly what discards them

	// ⚠⚠ A PERIODICITY USED TO CLOSE THIS ROAD, and the reason was arithmetic rather than effort. The
	// spec kept the period OUT of the key — one row per key for the whole interval — which is what
	// makes `BeforeFrom` and `UpToTo` mean "before the interval" and "through its end". Ask for rows
	// PER MONTH and each row's opening balance has to be measured against ITS OWN month: a running sum
	// over the periods, not a conditional one over the interval. Answered by the same spec it would
	// have repeated the interval's opening balance on every row — plausible, and wrong on every row
	// but the first.
	//
	// Since 2026-08-20 the spec HAS that arithmetic (ibMaterializeGrain + the running forms), so the
	// road is open wherever the engine can rank. Two conditions still close it:
	if (m_fold.HasPeriod()) {
		// ⭐ AN EMPTY PERIOD IS STILL A PERIOD, and no window invents a row that the surface does not
		// have. Carrying a balance across a month nothing moved in needs a calendar to LEFT JOIN
		// against; until there is one, that question belongs to the live path, which builds the
		// periods itself.
		if (m_fillEmptyPeriods)
			return false;

		ibConnectionScope scope;
		if (!ibCanPushWindow(scope.get()))
			return false;   // no windows on this driver — the RAM road answers exactly as before
	}

	const ibValueMetaObjectChartOfAccounts* chart = m_reg->GetChartOfAccounts();
	if (chart == nullptr || chart->GetAccountType() == nullptr || chart->GetDataReference() == nullptr
	    || chart->GetQueryable() == nullptr)
		return false;
	if (AnyTurnoverOnlyKind(chart))
		return false;   // a turnovers-only breakdown reports EMPTY balances — still a RAM-only rule

	m_serverRoad = 1;
	return true;
}

ibQueryRelPtr ibAcctBalanceAndTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (!CanReadOnServer())
		return nullptr;

	const ibValueMetaObjectAttributeBase*   period  = m_reg->GetRegisterPeriod();
	const ibValueMetaObjectAttributeBase*   account = m_reg->GetRegisterAccount();
	const ibValueMetaObjectChartOfAccounts* chart   = m_reg->GetChartOfAccounts();
	if (period == nullptr || account == nullptr || chart == nullptr)
		return nullptr;

	const ibBackendQueryable* view      = m_reg->GetTurnoverViewQueryable(/*creditSide*/ false);
	const ibBackendQueryable* chartRows = chart->GetQueryable();
	if (view == nullptr || chartRows == nullptr)
		return nullptr;

	ibMaterializeReadSpec r;
	r.m_view         = m_reg->GetTurnoverViewName(/*creditSide*/ false);
	r.m_periodColumn = ibRegValueField(period);
	r.m_from         = m_begin.m_date;
	r.m_to           = m_end.m_date;
	r.m_dropZeroRows = true;   // and here the balances count as figures too, so a key carried in reports

	// PER PERIOD — the same shape the accumulation register's reading takes, because it is the same
	// question asked of the same kind of surface. The period joins the key, and the balances become
	// running forms over the period sums; the gate above has already established that this engine
	// can rank and that no empty periods were asked for.
	const bool periodised = m_fold.HasPeriod();
	if (periodised) {
		r.m_grain      = m_fold.IsCalendar() ? ibMaterializeGrain::Calendar : ibMaterializeGrain::StoredPeriod;
		r.m_periodUnit = m_fold.m_unit;
		r.m_fromGrain  = (r.m_from.GetType() == TYPE_DATE && m_fold.IsCalendar())
			? ibValue(ibTruncateToPeriod(r.m_from.GetDateTime(), m_fold.m_unit))
			: r.m_from;
	}

	const auto appendFields = [&r](const ibValueMetaObjectAttributeBase* attribute) {
		if (attribute == nullptr)
			return;
		for (const wxString& field : ibRegFieldsOf(attribute))
			r.m_keyColumns.push_back(field);
	};
	appendFields(account);
	for (unsigned int idx = 0; idx < m_reg->GetAccountDimensionCount(); idx++) {
		appendFields(m_reg->GetAccountDimensionKindSlot(/*creditSide*/ false, idx));
		appendFields(m_reg->GetAccountDimensionSlot(/*creditSide*/ false, idx));
	}
	for (const auto dimension : m_reg->GetDimensionArrayObject())
		appendFields(dimension);

	for (const ibQueryExprPtr& condition :
		ibRegFilterExprs(m_filter, m_reg != nullptr ? m_reg->GetMetaData() : nullptr))
		r.m_filters.push_back(condition);

	// EITHER END MAY REACH BELOW THE GRAIN — "between this document and that one" is the question, and
	// both ends of it name a moment. Whole days come from the stored rows, each partial end from the
	// movements.
	ibRegFillArmCut(r, m_reg, m_end, m_begin);

	std::vector<const ibValueMetaObjectAttributeBase*> balanceResources;
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const wxString base   = resource->GetName();
		const wxString fromDr = ibRegPhysicalOf(view, base + ibAcctFigure::TurnoverDr);
		const wxString fromCr = ibRegPhysicalOf(view, base + ibAcctFigure::TurnoverCr);

		// The turnover half is reported for EVERY resource — what moved in the interval. Periodised,
		// the period itself is the condition: rows are grouped by it, so each figure is a plain sum of
		// that period's rows and `InRange` would be a second, redundant answer to the same question.
		const ibMaterializeWhen movedWhen = periodised ? ibMaterializeWhen::Always : ibMaterializeWhen::InRange;
		r.m_columns.push_back({ base + ibAcctFigure::TurnoverDr, fromDr, wxString(),
		                        ibMaterializeAgg::Value, movedWhen, true });
		r.m_columns.push_back({ base + ibAcctFigure::TurnoverCr, fromCr, wxString(),
		                        ibMaterializeAgg::Value, movedWhen, true });

		// …the balance halves only where a balance is kept. A quantitative register therefore reports
		// what moved in and out and no opening or closing at all, which is what "we keep no balance
		// there" means.
		if (!resource->IsBalanceResource())
			continue;
		balanceResources.push_back(resource);

		// ⭐ THE TWO SIDES ACCUMULATE INDEPENDENTLY, and each is a running form of its own — a debit
		// balance is the debit turnovers carried forward, a credit balance the credit ones. The fold
		// by account type happens ABOVE this read (the CASE over the chart's AccountType), so what is
		// asked for here is the pair, not the difference.
		const ibMaterializeAgg opening = periodised ? ibMaterializeAgg::RunningSumExcludingCurrent : ibMaterializeAgg::Value;
		const ibMaterializeAgg closing = periodised ? ibMaterializeAgg::RunningSum                 : ibMaterializeAgg::Value;
		const ibMaterializeWhen openWhen  = periodised ? ibMaterializeWhen::Always : ibMaterializeWhen::BeforeFrom;
		const ibMaterializeWhen closeWhen = periodised ? ibMaterializeWhen::Always : ibMaterializeWhen::UpToTo;

		r.m_columns.push_back({ base + ibAcctFigure::OpeningBalanceDr, fromDr, wxString(), opening, openWhen,  true });
		r.m_columns.push_back({ base + ibAcctFigure::OpeningBalanceCr, fromCr, wxString(), opening, openWhen,  true });
		r.m_columns.push_back({ base + ibAcctFigure::ClosingBalanceDr, fromDr, wxString(), closing, closeWhen, true });
		r.m_columns.push_back({ base + ibAcctFigure::ClosingBalanceCr, fromCr, wxString(), closing, closeWhen, true });
	}
	if (r.m_columns.empty())
		return nullptr;

	const wxString innerAlias = alias + wxT("_t");
	const wxString chartAlias = alias + wxT("_a");

	// The join, and the reason it is LEFT, are the balance reading's — one paragraph up.
	ibQueryExprPtr on = ibBinOp(ibQueryBinOp::Eq,
		ibCol(innerAlias, ibRegValueField(account)),
		ibCol(chartAlias, ibRegValueField(chart->GetDataReference())));

	ibQueryRelPtr joined = ibJoin(RenderMaterializedRead(r, innerAlias),
		ibScan(chartRows->GetQueryTableName(), chartAlias), on, ibQueryJoinType::Left);

	std::vector<ibQueryProjItem> projection;
	for (const wxString& field : r.m_keyColumns)
		projection.push_back({ ibCol(innerAlias, field), field });

	// ⚠ THE PERIOD IS A COLUMN OF THE ANSWER, not only of the grouping. It is not in m_keyColumns —
	// the read puts it there itself — so a projection built from the keys alone would group by the
	// month and then decline to say WHICH month, which is the one column a periodised reading is
	// asked for.
	if (periodised)
		projection.push_back({ ibCol(innerAlias, r.m_periodColumn), r.m_periodColumn });

	// The turnovers pass through UNFOLDED — see the note above the gate.
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const wxString base = resource->GetName();
		projection.push_back({ ibCol(innerAlias, base + ibAcctFigure::TurnoverDr), base + ibAcctFigure::TurnoverDr });
		projection.push_back({ ibCol(innerAlias, base + ibAcctFigure::TurnoverCr), base + ibAcctFigure::TurnoverCr });
	}

	const ibQueryExprPtr accountType = ibCol(chartAlias, ibRegValueField(chart->GetAccountType()));
	const auto isType = [&accountType](ibAccountType declared) {
		return ibBinOp(ibQueryBinOp::Eq, accountType, ibConst(ibValue(static_cast<int>(declared))));
	};
	const auto foldPair = [&](const wxString& debitName, const wxString& creditName) {
		const ibQueryExprPtr debit  = ibCol(innerAlias, debitName);
		const ibQueryExprPtr credit = ibCol(innerAlias, creditName);
		const ibQueryExprPtr zero   = ibConst(ibValue(0.0));
		projection.push_back({ ibCase({ { isType(ibAccountType::eActive),  ibBinOp(ibQueryBinOp::Sub, debit, credit) },
		                                { isType(ibAccountType::ePassive), zero } }, debit), debitName });
		projection.push_back({ ibCase({ { isType(ibAccountType::ePassive), ibBinOp(ibQueryBinOp::Sub, credit, debit) },
		                                { isType(ibAccountType::eActive),  zero } }, credit), creditName });
	};

	for (const ibValueMetaObjectAttributeBase* resource : balanceResources) {
		const wxString base = resource->GetName();
		foldPair(base + ibAcctFigure::OpeningBalanceDr, base + ibAcctFigure::OpeningBalanceCr);
		foldPair(base + ibAcctFigure::ClosingBalanceDr, base + ibAcctFigure::ClosingBalanceCr);
	}

	return ibSubquery(ibProject(joined, std::move(projection)), alias);
}

ibQueryRamTable ibAcctBalanceAndTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeBalanceAndTurnover(m_begin, m_end, m_accountDr, m_accountCr,
	                                        m_kindsDr, m_kindsCr, m_filter, m_fold, m_condition,
	                                        m_fillEmptyPeriods);
}

ibQueryRelPtr ibAcctRecordsQueryable::GetSourceRelation(const wxString& alias) const
{
	if (!CanReadOnServer())
		return nullptr;
	// The alias is the provider's — it wraps whatever comes back as `FROM (<this>) AS alias`.
	return m_reg->BuildRecordsRelation(m_begin, m_end, m_kindsDr, m_kindsCr, m_filter, m_condition, m_order, m_top);
}

ibQueryRamTable ibAcctRecordsQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeRecords(m_begin, m_end, m_kindsDr, m_kindsCr, m_filter, m_condition, m_order, m_top);
}

// ============================================================================
// The descriptor — one class, five tables
// ============================================================================

ibAcctSourceDescriptor::~ibAcctSourceDescriptor() = default;

wxString ibAcctSourceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_reg->GetClassType());
}

namespace {

// The word a reading is addressed by. `RecordsWithAccountDimensions` is long on purpose: it is the
// movements table WITH the slots widened, and a shorter name would collide with the register itself,
// which is already addressable and means something different.
wxString ShapeWord(ibAcctShape shape)
{
	switch (shape) {
	case ibAcctShape::Balance:             return wxT("Balance");
	case ibAcctShape::Turnovers:           return wxT("Turnovers");
	case ibAcctShape::DrCrTurnovers:       return wxT("DrCrTurnovers");
	case ibAcctShape::BalanceAndTurnovers: return wxT("BalanceAndTurnovers");
	case ibAcctShape::Records:             return wxT("RecordsWithAccountDimensions");
	}
	return wxEmptyString;
}

// A shape's column, shown in the catalogue as the ATTRIBUTE where there is one. A column that IS the
// register's own attribute (an account, a dimension, the period) must be handed over as that
// attribute: as a synthetic triple it loses its picture and the fact that it holds a reference, so the
// same field unfolds one node up and refuses to unfold here.
//
// ⚠ SIX HAND-WRITTEN ACCESSORS AND TWO LOOPS USED TO STAND HERE, and the list was already one short:
// nothing named the ANALYTICS SLOTS, which a `Records` reading publishes under their own metaIDs. The
// register knows its own attributes — every predefined one, the slots included — and it knows them
// through the find it already answers a query's column names with, so nothing is listed and nothing is
// allocated to ask. The neighbour had the same list with the same class of hole.
const ibValueMetaObjectAttributeBase* AttributeById(const ibValueMetaObjectAccountingRegister* reg, const ibMetaID& id)
{
	return reg != nullptr ? reg->FindAnyAttributeObjectByFilter(id) : nullptr;
}

void FillExplorerFromShape(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape,
                           const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
                           const ibRegFold& fold, ibSourceDataObject::ibSourceExplorer& explorer)
{
	const ibBackendQueryable* built = reg != nullptr ? reg->GetShapeQueryable(shape, kindsDr, kindsCr, fold) : nullptr;
	if (built == nullptr)
		return;

	for (const ibBackendQueryColumn* col : built->GetColumns()) {
		if (col == nullptr)
			continue;
		if (const ibValueMetaObjectAttributeBase* attribute = AttributeById(reg, col->GetColumnId()))
			explorer.AppendColumn(attribute, /*enabled*/ true, /*visible*/ true);
		else
			explorer.AppendColumn(col);
	}
}

// (The two ArgAt overloads that stood here were ibRegArg under another name — reading slot N of an
// argument list is the same question for every register, and it now lives once, in
// registerQueryLowering.h with the rest of the CALL helpers.)

} // namespace

wxString ibAcctSourceDescriptor::GetName() const
{
	return m_reg->GetName() + wxT(".") + ShapeWord(m_shape);
}

ibAcctCallArgs ibAcctParseCall(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape,
                               ibValue** paParams, long lSizeArray)
{
	ibAcctCallArgs call;
	if (reg == nullptr)
		return call;

	const ibAcctArgs layout = ibAcctArgs::For(shape, reg->IsCorrespondence());

	call.m_begin     = ibReadRegisterBound(ibRegArg(paParams, lSizeArray, layout.m_begin));
	call.m_end       = ibReadRegisterBound(ibRegArg(paParams, lSizeArray, layout.m_end));
	// ⭐⭐ THE ACCOUNT SLOT TAKES EITHER SHAPE, AND BOTH ARRIVE AS ONE.
	//
	// A QUERY writes a condition there — `Account IN HIERARCHY (&Accounts)` — and it comes through the
	// other entrance, already lowered and still carrying its word. A SCRIPT hands over a value: one
	// account or a list of them, which is what this argument has always been and must go on meaning.
	//
	// So a value is turned into the condition it means, right here, and everything downstream sees one
	// form. The word it means is `Hierarchy`: a bare list of accounts has ALWAYS brought the subtree
	// and reported it under the account named (§ 5e), and reading the same call as `Elements` now would
	// quietly narrow every existing script.
	const auto conditionFromValue = [](const ibValue& given,
	                                   const ibValueMetaObjectAttributeBase* accountAttr) -> ibQueryPredicatePtr {
		if (accountAttr == nullptr)
			return nullptr;
		const std::vector<ibValue> named = ibQueryHierarchyNamedValues(given);
		if (named.empty())
			return nullptr;   // nothing named is not a filter — it means every account
		ibQueryCondition leaf;
		leaf.m_col    = accountAttr;
		leaf.m_op     = ibQueryFilterOp::In;
		leaf.m_values = named;
		leaf.m_unfold = ibQueryDimUnfold::Hierarchy;
		return ibQueryPredicate::Leaf(leaf);
	};
	call.m_accountDr = conditionFromValue(ibRegArg(paParams, lSizeArray, layout.m_accountDr), reg->GetRegisterAccount());
	call.m_accountCr = conditionFromValue(ibRegArg(paParams, lSizeArray, layout.m_accountCr), reg->GetRegisterAccountCr());
	call.m_kindsDr   = ibAcctReadKinds(ibRegArg(paParams, lSizeArray, layout.m_kindsDr));
	call.m_kindsCr   = ibAcctReadKinds(ibRegArg(paParams, lSizeArray, layout.m_kindsCr));
	// The Structure a script passes becomes the condition right here, at the door — everything below
	// sees a predicate, and the same converter serves the query road.
	call.m_filter    = ibRegFilterPredicate(reg, ibRegArg(paParams, lSizeArray, layout.m_condition));
	// …and the same condition kept RAW, because its other half — the entries keyed by a KIND — is a
	// question about the slots, and which slots depends on the side the reading is passing over.
	call.m_condition = ibRegArg(paParams, lSizeArray, layout.m_condition);
	call.m_fold      = ibReadRegisterFold(ibRegArg(paParams, lSizeArray, layout.m_periodicity));

	// ⭐⭐ EVERY SLOT THE LAYOUT DECLARES IS READ HERE, and that is the whole reason this function
	// exists. A slot declared and not read is not a missing feature — it is an argument the author
	// writes, the window offers, and nothing consumes: silently ignored, which is the one failure
	// that looks like success. (The neighbouring register shipped that shape once, with a periodicity
	// that pushed every call's condition into the next slot.)
	//
	// The word is compared rather than parsed into an enum: there are two of them, they are the two
	// the parameter DECLARES as its choices, and anything else means the default — which is what
	// "reports the periods that have movements" is.
	const ibValue fillMethod = ibRegArg(paParams, lSizeArray, layout.m_fillMethod);
	call.m_fillEmptyPeriods = !fillMethod.IsEmpty()
		&& stringUtils::CompareString(fillMethod.GetString(), wxT("MovementsAndPeriodBoundaries"));

	call.m_order = ibRegArg(paParams, lSizeArray, layout.m_order);

	const ibValue top = ibRegArg(paParams, lSizeArray, layout.m_top);
	if (!top.IsEmpty()) {
		// A count that is not a number, or is negative, is not a smaller answer — it is a question
		// nobody asked. Zero and absent mean the same thing: all of them.
		const long asked = static_cast<long>(top.GetInteger());
		call.m_top = asked > 0 ? asked : 0;
	}
	return call;
}

const ibBackendQueryable* ibAcctSourceDescriptor::GetConditionScope() const
{
	// The MOVEMENTS: the account column is theirs, they exist before any companion, and they are the
	// same table whichever surface a pass then reads. Resolving against the companion would mean
	// resolving against the object this call is building.
	return m_reg != nullptr ? m_reg->GetQueryable() : nullptr;
}

const ibBackendQueryable* ibAcctSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray,
                                                                  const std::vector<ibQueryPredicatePtr>& conditions)
{
	// The layout says which slot each condition came from — the same layout the call is read by, so
	// the two cannot drift.
	const ibAcctArgs layout = ibAcctArgs::For(m_shape, m_reg != nullptr && m_reg->IsCorrespondence());
	const auto at = [&conditions](int slot) -> ibQueryPredicatePtr {
		return slot >= 0 && static_cast<size_t>(slot) < conditions.size() ? conditions[slot] : nullptr;
	};

	m_pendingAccountDr = at(layout.m_accountDr);
	m_pendingAccountCr = at(layout.m_accountCr);
	const ibBackendQueryable* q = CreateQueryable(paParams, lSizeArray);
	m_pendingAccountDr.reset();
	m_pendingAccountCr.reset();
	return q;
}

const ibBackendQueryable* ibAcctSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	ibAcctCallArgs call = ibAcctParseCall(m_reg, m_shape, paParams, lSizeArray);

	// ⚠ A CONDITION THAT CAME THROUGH THE OTHER ENTRANCE WINS, and only if there is one. Its slot in
	// paParams carries an empty value (the lowering put one there so the positions still line up), so
	// the parse above found nothing to build from — but a SCRIPT call has no second entrance at all,
	// and its value-built condition must survive. Overwriting unconditionally would erase it.
	if (m_pendingAccountDr) call.m_accountDr = m_pendingAccountDr;
	if (m_pendingAccountCr) call.m_accountCr = m_pendingAccountCr;

	// Built and KEPT by the base — the same call gives the same object back, and a query that reads
	// this table twice keeps both alive.
	switch (m_shape) {
	case ibAcctShape::Balance:
		return MakeCompanion<ibAcctBalanceQueryable>(paParams, lSizeArray, m_reg,
			call.m_begin, call.m_accountDr, call.m_accountCr, call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
	case ibAcctShape::Turnovers:
		return MakeCompanion<ibAcctTurnoverQueryable>(paParams, lSizeArray, m_reg,
			call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
			call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_fold, call.m_condition);
	case ibAcctShape::DrCrTurnovers:
		return MakeCompanion<ibAcctDrCrTurnoverQueryable>(paParams, lSizeArray, m_reg,
			call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
			call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
	case ibAcctShape::BalanceAndTurnovers:
		return MakeCompanion<ibAcctBalanceAndTurnoverQueryable>(paParams, lSizeArray, m_reg,
			call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
			call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_fold, call.m_condition,
			call.m_fillEmptyPeriods);
	case ibAcctShape::Records:
		return MakeCompanion<ibAcctRecordsQueryable>(paParams, lSizeArray, m_reg,
			call.m_begin, call.m_end, call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition,
			call.m_order, call.m_top);
	}
	return nullptr;
}

void ibAcctSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	// No arguments: the catalogue shows the breakdown AS THE REGISTER HAS IT (the slots as they stand),
	// which is also what a call with no kinds returns. What the window offers and what the read gives
	// back are the same answer, asked of the same function.
	ibRegFold fold;
	if (m_shape == ibAcctShape::Records)
		fold.m_kind = ibRegGranularity::Record;
	FillExplorerFromShape(m_reg, m_shape, {}, {}, fold, explorer);
}

void ibAcctSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
                                                const std::vector<ibValue>& args) const
{
	const ibAcctArgs layout = ibAcctArgs::For(m_shape, m_reg->IsCorrespondence());

	ibRegFold fold = ibReadRegisterFold(ibRegArg(args, layout.m_periodicity));
	if (m_shape == ibAcctShape::Records)
		fold.m_kind = ibRegGranularity::Record;

	FillExplorerFromShape(m_reg, m_shape,
		ibAcctReadKinds(ibRegArg(args, layout.m_kindsDr)),
		ibAcctReadKinds(ibRegArg(args, layout.m_kindsCr)), fold, explorer);
}

void ibAcctSourceDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	const ibAcctArgs layout = ibAcctArgs::For(m_shape, m_reg->IsCorrespondence());
	const ibTypeDescription periodType = m_reg->GetRegisterPeriod() != nullptr
		? m_reg->GetRegisterPeriod()->GetTypeDesc() : ibTypeDescription();
	const ibTypeDescription accountType = m_reg->GetRegisterAccount() != nullptr
		? m_reg->GetRegisterAccount()->GetTypeDesc() : ibTypeDescription();

	// ⚠ DECLARED FROM THE SAME LAYOUT THE CALL IS READ BY. Two lists that must agree, derived from one
	// — which is the only arrangement in which they cannot drift apart.
	const auto push = [&out](const wxString& name, const ibTypeDescription& type,
		const wxString& description = wxEmptyString) {
		ibQuerySourceParameter parameter;
		parameter.m_name        = name;
		parameter.m_type        = type;
		parameter.m_description = description;
		out.push_back(parameter);
	};

	// A PREDICATE slot — the author writes a condition here, not a value. `consumed` marks the ones
	// this SOURCE takes for itself instead of letting them be ANDed into the query around it: the
	// account conditions, because a reading folds by them and a filter around it could only select.
	const auto pushCondition = [&out](const wxString& name, bool consumed = false) {
		ibQuerySourceParameter parameter;
		parameter.m_name             = name;
		parameter.m_condition        = true;

		// ⭐ WHICH CONDITION THIS IS, since there are up to four of them and their names differ by
		// one word. An ACCOUNT condition decides which account a row is about — and, when it is
		// written with IN HIERARCHY, which account the rows are reported UNDER, which is a fold and
		// not a filter. The general one narrows the rows the reading produces. A caller that puts
		// one where the other belongs gets a correct query answering a different question.
		parameter.m_description = consumed
			? _("A condition on the ACCOUNT, consumed by the reading itself: `IN HIERARCHY` reports "
			    "the subordinate accounts folded under the one named, which a filter applied around "
			    "the reading could never do. Accounts only - everything else has the general "
			    "Condition slot.")
			: _("A condition on the reading's own columns, applied inside it so it narrows what is "
			    "folded rather than dropping finished rows.");

		parameter.m_consumedBySource = consumed;
		out.push_back(parameter);
	};

	// ⚠ DECLARED IN THE LAYOUT'S OWN ORDER, and the order is the layout's business — a positional call
	// is read by position, so a list that agrees on NAMES and differs on ORDER is a call whose
	// condition arrives where the breakdown was expected. (The neighbouring register shipped exactly
	// that once.) Every branch below therefore mirrors ibAcctArgs::For, in sequence.

	// The interval — or a single MOMENT, which is what a balance stands at. The shared pair is called
	// rather than re-spelled: two spellings of "BeginOfPeriod" is how one becomes "PeriodBegin".
	if (layout.m_end >= 0)
		ibFillRegisterIntervalParameters(periodType, out);
	else
		push(wxT("Period"), periodType,
			_("AS OF WHEN the balance stands - one moment, not an interval. It may name a DOCUMENT "
			  "rather than a date, which is how \"the balance as of this entry\" is asked."));

	// How the interval is CUT — right after the interval, because that is what it is about.
	if (layout.m_periodicity >= 0)
		ibAppendRegisterPeriodicityParameter(out);

	// …and what to do with a period nothing moved in.
	if (layout.m_fillMethod >= 0) {
		ibQuerySourceParameter fill;
		fill.m_name    = wxT("FillMethod");
		fill.m_description = _("What to do with a period nothing moved in: report only the periods "
		                       "that have movements, or a row on every period boundary of the "
		                       "interval - which is what a month-by-month column needs so the "
		                       "quiet months are not simply missing.");
		fill.m_choices = { wxT("Movements"), wxT("MovementsAndPeriodBoundaries") };
		fill.m_default = wxT("Movements");
		out.push_back(fill);
	}

	// ⭐⭐ THE ACCOUNT IS A CONDITION, NOT A VALUE — and that is the correction this layout carries.
	//
	// A list of accounts can only ever say "these"; a condition says what the author means: `Account IN
	// HIERARCHY (&Accounts)` (the summary account and everything under it, reported under the one
	// named), `Account IN (&Exactly)`, a comparison against an account's own attribute. The three
	// unfold words already exist in the language, and this is the slot where they are written.
	//
	// ⚠ ONLY ACCOUNTS BELONG IN IT. It is not a second general condition: what it constrains is which
	// ACCOUNT a row is about, and the reading uses it to decide the rows AND — where the word says so —
	// which account they are reported under. A condition over anything else has the general `Condition`
	// slot, which is a different question and is asked separately.
	//
	// The naming follows the question rather than the storage: a TURNOVER filtered by the other side
	// asks about the CORRESPONDING account (one figure, one side, filtered by its counterpart), while
	// the matrix names two symmetric sides and calls them Dr and Cr.
	const bool symmetricSides = (m_shape == ibAcctShape::DrCrTurnovers);
	if (layout.m_accountDr >= 0)
		pushCondition(layout.m_accountCr >= 0 && symmetricSides ? wxT("AccountConditionDr") : wxT("AccountCondition"), /*consumed*/ true);

	// The breakdown. Typed by the KIND — an element of the chart of characteristic types — because
	// that is what is passed: one kind, or an array of them in the order the columns should come out.
	const ibValueMetaObjectAttributeBase* kindSlot = m_reg->GetRegisterAccountDimensionKind(0);
	const ibTypeDescription kindType = kindSlot != nullptr ? kindSlot->GetTypeDesc() : ibTypeDescription();
	if (layout.m_kindsDr >= 0)
		push(layout.m_kindsCr >= 0 && symmetricSides ? wxT("AccountDimensionsDr") : wxT("AccountDimensions"), kindType,
			_("WHICH BREAKDOWN to report, as a KIND from the chart of characteristic types - or an "
			  "array of kinds, in the order the columns should come out. Not a filter: it says what "
			  "the figures are split BY. Left out, the reading reports the account's own totals with "
			  "no breakdown at all."));

	// The general condition sits where the layout puts it — between the two sides for a turnover,
	// after both for the matrix. See ibAcctArgs::For for why that is not arbitrary.
	if (layout.m_condition >= 0 && !symmetricSides)
		ibAppendRegisterConditionParameter(out);

	if (layout.m_accountCr >= 0)
		pushCondition(symmetricSides ? wxT("AccountConditionCr") : wxT("CorrAccountCondition"), /*consumed*/ true);
	if (layout.m_kindsCr >= 0)
		push(symmetricSides ? wxT("AccountDimensionsCr") : wxT("CorrAccountDimensions"), kindType,
			_("The same, for the other side: the credit side of a matrix, or the CORRESPONDING "
			  "account of a turnover - the one the figure moved against."));

	if (layout.m_condition >= 0 && symmetricSides)
		ibAppendRegisterConditionParameter(out);

	// A LISTING answers with lines, so it is the one reading that can be asked for an order and a
	// count. A fold has no line to order and answers with every group it found.
	// ⚠ ORDER IS NOT A PREDICATE — it is a list of fields with directions, so it is an ordinary
	// expression slot. Declaring it as a condition would put the wrong editor in front of the author
	// and, worse, would let the lowering route it into the WHERE.
	if (layout.m_order >= 0)
		push(wxT("Order"), ibTypeDescription(),
			_("How the LINES are ordered - a list of fields with directions, not a condition. Only a "
			  "listing takes one: a fold has no line to order and answers with every group it "
			  "found."));
	if (layout.m_top >= 0)
		push(wxT("Top"), ibTypeDescription(),
			_("How many lines at most, taken AFTER the order - so it means the first N of that "
			  "order, not an arbitrary N of the whole."));
}

// A READING IS FILTERED BY ITS DIMENSIONS AND ITS BREAKDOWN, never by a resource. A resource is what
// the table folded, and a condition over a fold belongs to the RESULT rather than to an argument of
// the source.
//
// ⭐⭐ EVERY SLOT ASKS ITS OWN QUESTION, so every slot gets its own list — the overload below. This
// one answers for the general `Condition`: the analytics VALUES and the dimensions.
//
// Both halves of that were once wrong in opposite directions. The dimensions were the only thing
// offered, so the general slot was empty of the subconto a filter is normally written with; then the
// account was added here as well, and it does not belong — an account has parameters of its own
// (`AccountCondition`, `…Dr` / `…Cr`, `CorrAccountCondition`), and what is written THERE is not a
// predicate but the hierarchy SCOPE: it decides which accounts are admitted and which one each row
// is reported under. A field that is a scope in one slot and a filter in another is one question
// with two answers, honoured by two different mechanisms.
void ibAcctSourceDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_reg == nullptr)
		return;

	// ⭐⭐ WHAT THE TOTALS TABLE ACTUALLY HOLDS — the accounts, the subconto and the dimensions, and
	// nothing else. That is not a policy about what is useful to filter by; it is the shape of the
	// relation. A totals row is one row per (period, account, its analytical breakdown, dimensions)
	// with the figures summed into it — the recorder, the line number, the active flag and the record
	// type belong to the MOVEMENT and were folded away when the row was made. Offering them here
	// would offer a filter over columns the reading has not got.
	//
	// ⚠ AND THE ACCOUNT IS NOT AMONG THEM. It has slots of its OWN — `AccountCondition` / `…Dr` /
	// `…Cr` / `CorrAccountCondition`, the overload below — and those are not a filter but the
	// hierarchy SCOPE: what is written there decides which accounts are admitted and which one each
	// row is reported under. The same account named twice, once as scope and once as an ordinary
	// predicate, is two answers to one question, and the second one is honoured by a different
	// mechanism than the author is looking at. One place to say it, and it is the account's own slot.
	//
	// THE ANALYTICS VALUES — the subconto itself, per position and per side.
	//
	// ⚠ THE KINDS ARE NOT OFFERED HERE. A kind says what a slot is FILED UNDER, and choosing rows by
	// it is a question of one particular reading, not of an ordinary condition — it has its own place
	// and its own arguments. Offering it beside the values invites a filter that reads as "rows whose
	// third slot happens to hold a Contract", which is a different question from "rows about THIS
	// contract" and is almost never the one being asked.
	for (unsigned int side = 0; side < 2; side++) {
		const bool creditSide = (side != 0);
		if (creditSide && !m_reg->IsCorrespondence())
			break;
		for (unsigned int idx = 0; idx < m_reg->GetAccountDimensionCount(); idx++)
			if (const ibValueMetaObjectAttributeBase* slot = m_reg->GetAccountDimensionSlot(creditSide, idx))
				explorer.AppendColumn(slot, /*enabled*/ true, /*visible*/ true);
	}

	// ⚠ AND THE DIMENSIONS, which are NOT in the attribute list: a dimension is its own metaclass
	// (g_metaDimensionCLSID), so a walk over attributes misses them. Leaving them out is what emptied
	// the ordinary `Condition` slot of everything a filter is normally written with.
	for (const ibValueMetaObjectAttributeBase* dimension : m_reg->GetDimensionArrayObject())
		if (dimension != nullptr)
			explorer.AppendColumn(dimension, /*enabled*/ true, /*visible*/ true);
}

// ⭐⭐ THE ACCOUNT SLOTS ADMIT ACCOUNTS AND NOTHING ELSE — so that is all they are offered.
//
// `AccountCondition` / `…Dr` / `…Cr` / `CorrAccountCondition` are CONSUMED by this source: the
// predicate written there never reaches a WHERE, it is read back into the hierarchy scope that
// decides which accounts are admitted and which one each row is reported under
// (ScopeFromAccountCondition). A leaf about anything else cannot be applied — there is nowhere to
// apply it — so it is dropped, and a filter that vanishes reports MORE than was asked for.
//
// The general `Condition` is the place for everything else, and it answers with the list above.
void ibAcctSourceDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
                                                   const wxString& slot) const
{
	if (m_reg == nullptr)
		return;

	if (!slot.Contains(wxT("AccountCondition")) && !slot.Contains(wxT("CorrAccountCondition"))) {
		FillConditionExplorer(explorer);   // the general condition — dimensions and the subconto values
		return;
	}

	// Which side this slot is about is written in its own name: the matrix names Dr and Cr, a
	// one-figure turnover filtered by its counterpart says Corr, and a one-sided register says
	// neither. Reading the name is not a lettering trick — these are the slot names this same
	// descriptor publishes in DescribeParameters, a few lines up.
	const bool creditSide = slot.Contains(wxT("Cr"));
	explorer.AppendColumn(creditSide ? m_reg->GetRegisterAccountCr() : m_reg->GetRegisterAccount(),
	                      /*enabled*/ true, /*visible*/ true);
}

