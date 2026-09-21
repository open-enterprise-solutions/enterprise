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
#include "reference/reference.h"                                   // ibValueReferenceDataObject — reading what an ACCOUNT declares

#include "backend/query/dataQueryBuilder.h"                        // L3 door — From(source).Select() / SelectAggregate()
#include "backend/databaseLayer/databaseMaterializeBuilder.h"       // L2-2 — RenderMaterializedRead: the READ side of the materialised surface
#include "backend/query/queryRamTable.h"                           // FoldBalancesForward — the running step, shared with the accumulation register
#include "backend/query/queryAST.h"                                // ibQueryDimUnfold — «in» / «in hierarchy» / «hierarchy only», the language's own three words
#include "backend/query/queryHierarchy.h"                          // ibQueryHierarchyScope — the operator that resolves those three words into values
#include "backend/query/queryException.h"                          // ibBackendQueryNameException — a breakdown field the register does not have
#include "backend/databaseLayer/databaseLayer.h"                   // ibTruncateToPeriod / ibNextPeriodStart — the GRAIN, in RAM terms
#include "backend/system/value/valueArray.h"                        // ibValueArray — a requested breakdown may be a LIST
#include "backend/system/value/valueType.h"                         // ibValueTypeDescription::AdjustValue — a column's typed empty
#include "backend/metaData.h"                                       // ibMetaData whole — AdjustValue(…, metaData) must see it is no ibValue
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegBound / ibRegFold / ibRegFillArmCut
#include "backend/metaCollection/resource/metaResourceObject.h"     // IsBalanceResource — one value for the entry, or one per side
#include "backend/metaCollection/accountingKind/metaAccountingKindObject.h"   // the chart's flag a figure is kept under
#include "backend/appData.h"
#include "backend/session/session.h"

#include <algorithm>
#include <functional>      // ConditionOnPass — the test rebuilt over whichever column a pass reads
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

// …and the name the ACCOUNT is published under, by the same rule. A row about one account calls it
// `Account` — the reference's Balance table has an Account, not a debit Account — even in a correspondence register,
// whose movements call the same attribute `AccountDr`; a paired row keeps `AccountDr` beside `AccountCr`.
wxString PublishedAccountName(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape)
{
	const ibValueMetaObjectAttributeBase* account = reg != nullptr ? reg->GetRegisterAccount() : nullptr;
	if (account == nullptr)
		return wxString();
	return PairedRow(reg, shape) ? account->GetName()
	                             : ibValueMetaObjectAccountingRegister::AccountColumnName(wxEmptyString);
}

// How many breakdown columns a side reports: what was asked for, or what the register HAS — and never
// more than it has. A kind asked for past the last slot has no field to be read from; the call door
// refuses it by name (ibAcctParseCall), and the shape a call-less catalogue builds stops at the slots.
unsigned int BreakdownWidth(const ibValueMetaObjectAccountingRegister* reg, const std::vector<ibValue>& kinds)
{
	const unsigned int slots = reg->GetAccountDimensionCount();
	return kinds.empty() ? slots : std::min(slots, static_cast<unsigned int>(kinds.size()));
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
	// …and the attribute's own face when this source does not name it — an attribute HOLDS a query
	// column rather than being one (docs/private/ownership-authority.md).
	return here != nullptr ? here : attribute->GetQueryColumn();
}

// ⭐ THE SAME, ASKED WITH A COLUMN IN HAND. A field kept per side is not reached through its attribute
// — the attribute has two columns and this caller already holds the one it means.
const ibBackendQueryColumn* ColumnOn(const ibBackendQueryable* source, const ibBackendQueryColumn* column)
{
	if (source == nullptr || column == nullptr)
		return nullptr;
	const ibBackendQueryColumn* here = source->ResolveColumnByName(column->GetName());
	return here != nullptr ? here : column;
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
// `corr` — the breakdown of the CORRESPONDENT on a turnover row, published as `CorrAccountDimension<n>`.
void DescribeBreakdown(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape, bool creditSide,
                       const std::vector<ibValue>& kinds, std::vector<ibAcctBreakdownColumn>& out, bool corr = false)
{
	const wxString prefix = SidePrefix(reg, shape, creditSide);
	const unsigned int width = BreakdownWidth(reg, kinds);

	for (unsigned int no = 0; no < width; no++) {
		ibAcctBreakdownColumn column;
		column.m_alias = corr ? ibValueMetaObjectAccountingRegister::CorrAccountDimensionColumnName(no + 1)
		                      : ibValueMetaObjectAccountingRegister::AccountDimensionColumnName(prefix, no + 1);

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
                  std::vector<ibAcctBreakdownColumn>& out, bool corr = false)
{
	const size_t first = out.size();
	DescribeBreakdown(reg, shape, creditSide, kinds, out, corr);

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
	return column.m_byKind ? sel.GetColumn(column.m_alias, column.m_slot)
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
// `published` is the column the reading publishes for this slot's account when that column is its own
// (a synthetic id — `Account` collapsed from two sides, `CorrAccount`); a condition written through the
// query names it, one built from a script's value names the attribute.
ibQueryHierarchyScope ScopeFromAccountCondition(const ibBackendQueryable* source,
                                                const ibBackendQueryColumn* accountCol,
                                                const ibQueryPredicatePtr& condition,
                                                const ibBackendQueryColumn* published = nullptr)
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
		// By the column's ID, not its object: the condition is written against the columns the reading
		// PUBLISHES (`Account` in a balance, `AccountDr` in the matrix — ibAcctConditionScope), and the
		// condition scope is another copy of the same shape. A column that IS the attribute carries the
		// attribute's id; one the reading composes (`CorrAccount`) carries its own.
		const auto names = [&](const ibBackendQueryColumn* column) {
			return column != nullptr && leaf->m_col->GetColumnId() == column->GetColumnId();
		};
		if (leaf->m_col == nullptr || !(names(accountCol) || names(published)))
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

// ⭐⭐ THE CONDITION OF A READING, ASKED OF THE SURFACE ONE PASS STANDS ON.
//
// A condition written into a virtual table's parameters is a SELECTION, made before anything is folded: the
// table arrives already narrowed (Max, 2026-09-17: "a virtual table does not pick out of a big array, it
// hands you the filtered table at once"). So it cannot stay the query's — it goes down into every pass, as
// the WHOLE tree the author wrote: NOT, OR, a walk through a reference (`NOT Account.OffBalance`), IN, IS
// NULL, REFS.
//
// What it is written against is the reading's own columns (the condition scope — ibAcctConditionScope), and
// those are not the columns a pass reads. `Account` is the debit account on one pass and the credit account
// on the other; `CorrAccount` is the opposite one; a breakdown column is a slot of the pass's side, or —
// asked BY KIND — whichever slot that kind stands in, which differs per account; a dimension kept per side
// is the side's half. So each column is found again for the pass, by its IDENTITY (the id the shape gave
// it), and the tree is rebuilt over what was found. A walk keeps its path and becomes a correlated EXISTS
// (m_asExists): a filter through a reference must never multiply a row it keeps.

// Where a column the condition names is read on one pass: one column, or — a breakdown asked for BY KIND —
// every slot of the side with its kind column beside it, because the kind stands in a different slot on
// each account.
struct ibAcctConditionColumn
{
	const ibBackendQueryColumn* m_column = nullptr;
	std::vector<std::pair<const ibBackendQueryColumn*, const ibBackendQueryColumn*>> m_byKind;   // (kind slot, slot)
	ibValue                     m_kind;
	// Read on the OPPOSITE side of a row about one account — the correspondent, which a stored side does
	// not carry: a pass that has to read it stands on the movements.
	bool                        m_correspondent = false;

	bool IsFound() const { return m_column != nullptr || !m_byKind.empty(); }
};

ibAcctConditionColumn ConditionColumnOn(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* source,
                                        ibAcctShape shape, bool creditSide,
                                        const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
                                        const ibBackendQueryColumn* named)
{
	ibAcctConditionColumn out;
	if (reg == nullptr || source == nullptr || named == nullptr)
		return out;

	const ibMetaID id = named->GetColumnId();
	const bool paired = PairedRow(reg, shape);
	const bool bothSidesOnOneRow = shape == ibAcctShape::Records || shape == ibAcctShape::DrCrTurnovers;

	const auto is = [id](const ibValueMetaObjectAttributeBase* attribute) {
		return attribute != nullptr && id == attribute->GetMetaID();
	};
	// …or a column the shape composed over it (ibRegDerivedColumnId) — `Account` of both sides, `CorrAccount`,
	// a breakdown column, `CurrencyCorr`.
	const auto composedOver = [id](const ibValueMetaObjectAttributeBase* attribute) {
		return attribute != nullptr && id == ibRegDerivedColumnId(attribute->GetMetaID());
	};
	const auto found = [&](const ibBackendQueryColumn* column, bool correspondent = false) {
		out.m_column        = column;
		out.m_correspondent = correspondent;
		return out;
	};
	const auto on = [&](const ibValueMetaObjectAttributeBase* attribute, bool correspondent = false) {
		return found(attribute != nullptr ? ColumnOn(source, attribute) : nullptr, correspondent);
	};

	// --- the accounts ---------------------------------------------------------------------------------
	const ibValueMetaObjectAttributeBase* account   = reg->GetRegisterAccount();
	const ibValueMetaObjectAttributeBase* accountCr = reg->GetRegisterAccountCr();
	const ibValueMetaObjectAttributeBase* own       = creditSide && accountCr != nullptr ? accountCr : account;
	const ibValueMetaObjectAttributeBase* opposite  = accountCr == nullptr ? nullptr : (creditSide ? account : accountCr);
	if (is(account))
		return on(paired ? account : own);
	if (composedOver(account))
		return on(own);
	if (is(accountCr))
		return on(accountCr);
	if (composedOver(accountCr))
		return on(opposite, /*correspondent*/ true);

	// --- the breakdown ----------------------------------------------------------------------------------
	for (unsigned int no = 0; no < reg->GetAccountDimensionCount(); no++) {
		for (const bool side : { false, true }) {
			const ibValueMetaObjectAttributeBase* slot = reg->GetAccountDimensionSlot(side, no);
			const ibValueMetaObjectAttributeBase* kind = reg->GetAccountDimensionKindSlot(side, no);
			const bool valueColumn = is(slot) || composedOver(slot);
			const bool kindColumn  = is(kind) || composedOver(kind);
			if (!valueColumn && !kindColumn)
				continue;

			// A paired row numbers each side's slots under that side. A row about one account numbers the
			// ACCOUNT's under the debit slots — read on this pass's own side — and its correspondent's under
			// the credit ones, read on the other.
			const bool readSide      = paired ? side : (side ? !creditSide : creditSide);
			const bool correspondent = !paired && side;
			const std::vector<ibValue>& kinds = side ? kindsCr : kindsDr;

			if (kindColumn)
				return on(reg->GetAccountDimensionKindSlot(readSide, no), correspondent);
			if (kinds.empty())
				return on(reg->GetAccountDimensionSlot(readSide, no), correspondent);
			if (no >= kinds.size())
				return out;

			out.m_kind          = kinds[no];
			out.m_correspondent = correspondent;
			for (unsigned int idx = 0; idx < reg->GetAccountDimensionCount(); idx++) {
				const ibValueMetaObjectAttributeBase* anySlot = reg->GetAccountDimensionSlot(readSide, idx);
				const ibValueMetaObjectAttributeBase* anyKind = reg->GetAccountDimensionKindSlot(readSide, idx);
				if (anySlot != nullptr && anyKind != nullptr)
					out.m_byKind.push_back({ ColumnOn(source, anyKind), ColumnOn(source, anySlot) });
			}
			return out;
		}
	}

	// --- the dimensions ---------------------------------------------------------------------------------
	for (const auto dimension : reg->GetDimensionArrayObject()) {
		if (dimension == nullptr)
			continue;
		const ibValueMetaObjectAttributeBase* debit  = reg->GetFieldSide(/*creditSide*/ false, dimension);
		const ibValueMetaObjectAttributeBase* credit = reg->GetFieldSide(/*creditSide*/ true, dimension);
		if (is(dimension))
			return bothSidesOnOneRow ? on(dimension)
			                         : found(ColumnOn(source, reg->GetRegisterDimension(creditSide, dimension)));
		if (is(debit))
			return on(debit);
		if (is(credit))
			return on(credit);
		if (composedOver(credit))
			return found(ColumnOn(source, reg->GetRegisterDimension(!creditSide, dimension)), /*correspondent*/ true);
	}

	// --- a movement line's own fields, where a row IS a line ----------------------------------------------
	// Anywhere else they are what the reading folds away (the period is its boundaries, a figure its sum), and
	// a selection by them would change the answer's meaning rather than narrow it.
	if (shape == ibAcctShape::Records)
		if (const ibValueMetaObjectAttributeBase* attribute = reg->FindAnyAttributeObjectByFilter(id))
			return on(attribute);

	return out;
}

// Does the condition name the CORRESPONDENT of a row about one account? A stored side keeps one account, so
// such a pass has to stand on the movements — the routing the correspondent account argument already takes.
bool ConditionNamesCorrespondent(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape,
                                 const ibQueryPredicatePtr& condition)
{
	if (!condition || reg == nullptr)
		return false;
	const auto names = [&](const ibBackendQueryColumn* column) {
		return column != nullptr
			&& ConditionColumnOn(reg, reg->GetQueryable(), shape, false, {}, {}, column).m_correspondent;
	};
	switch (condition->m_kind) {
	case ibQueryPredicateKind::Leaf:
		return names(condition->m_leaf.m_path.empty() ? condition->m_leaf.m_col : condition->m_leaf.m_path.front());
	case ibQueryPredicateKind::IsNull:
	case ibQueryPredicateKind::RefType:
		return names(condition->m_path.empty() ? condition->m_col : condition->m_path.front());
	default:
		for (const ibQueryPredicatePtr& child : condition->m_children)
			if (ConditionNamesCorrespondent(reg, shape, child))
				return true;
		return false;
	}
}

ibQueryPredicatePtr ConditionOnPass(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* source,
                                    ibAcctShape shape, bool creditSide,
                                    const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
                                    const ibQueryPredicatePtr& condition);

// A computed side of a comparison (`Quantity * Price > 100` on a listing), its columns found again for the pass.
ibQueryColumnExprPtr ConditionExprOnPass(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* source,
                                         ibAcctShape shape, bool creditSide,
                                         const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
                                         const ibQueryColumnExprPtr& expr)
{
	if (!expr)
		return expr;
	auto here = std::make_shared<ibQueryColumnExpr>(*expr);
	if (here->m_kind == ibQueryColumnExprKind::Column && here->m_col != nullptr) {
		const ibAcctConditionColumn found = ConditionColumnOn(reg, source, shape, creditSide, kindsDr, kindsCr, here->m_col);
		// ⚠ ONE COLUMN, AND ITS FIRST FIELD. A breakdown by kind is a different slot per account — a CASE, not a
		// column to compute over — and a named field is spelled after the column it was written against.
		if (found.m_column == nullptr || !here->m_field.IsEmpty())
			ibRegRefuseConditionColumn(here->m_col);
		here->m_col = found.m_column;
	}
	const auto again = [&](const ibQueryColumnExprPtr& e) {
		return ConditionExprOnPass(reg, source, shape, creditSide, kindsDr, kindsCr, e);
	};
	here->m_lhs  = again(here->m_lhs);
	here->m_rhs  = again(here->m_rhs);
	here->m_else = again(here->m_else);
	for (ibQueryColumnExprPtr& arg : here->m_args)
		arg = again(arg);
	for (auto& branch : here->m_cases) {
		branch.first  = ConditionOnPass(reg, source, shape, creditSide, kindsDr, kindsCr, branch.first);
		branch.second = again(branch.second);
	}
	return here;
}

ibQueryPredicatePtr ConditionOnPass(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* source,
                                    ibAcctShape shape, bool creditSide,
                                    const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
                                    const ibQueryPredicatePtr& condition)
{
	if (!condition || reg == nullptr || source == nullptr)
		return nullptr;

	const auto again = [&](const ibQueryPredicatePtr& child) {
		return ConditionOnPass(reg, source, shape, creditSide, kindsDr, kindsCr, child);
	};

	// One test of one column, rebuilt over the column the pass reads — or, by kind, over each slot with its
	// kind beside it. `nullWhereAbsent`: the test is IS NULL, which an account with no such kind passes too —
	// the reading reports its column empty.
	const auto over = [&](const ibBackendQueryColumn* named, bool nullWhereAbsent,
	                      const std::function<ibQueryPredicatePtr(const ibBackendQueryColumn*)>& rebuild) {
		const ibAcctConditionColumn found = ConditionColumnOn(reg, source, shape, creditSide, kindsDr, kindsCr, named);
		if (!found.IsFound())
			ibRegRefuseConditionColumn(named);
		if (found.m_column != nullptr)
			return rebuild(found.m_column);

		ibQueryPredicatePtr holds, carries;
		for (const auto& slot : found.m_byKind) {
			ibQueryCondition kindIs;
			kindIs.m_col   = slot.first;
			kindIs.m_value = found.m_kind;
			const ibQueryPredicatePtr standsHere = ibQueryPredicate::Leaf(kindIs);
			carries = OrWith(carries, standsHere);
			holds   = OrWith(holds, AndWith(standsHere, rebuild(slot.second)));
		}
		return nullWhereAbsent ? OrWith(holds, ibQueryPredicate::Not(carries)) : holds;
	};

	switch (condition->m_kind) {
	case ibQueryPredicateKind::And:
	case ibQueryPredicateKind::Or: {
		ibQueryPredicatePtr folded;
		for (const ibQueryPredicatePtr& child : condition->m_children) {
			const ibQueryPredicatePtr one = again(child);
			folded = condition->m_kind == ibQueryPredicateKind::And ? AndWith(folded, one) : OrWith(folded, one);
		}
		return folded;
	}
	case ibQueryPredicateKind::Not:
		return condition->m_children.empty() ? nullptr : ibQueryPredicate::Not(again(condition->m_children.front()));

	case ibQueryPredicateKind::Leaf: {
		ibQueryCondition leaf = condition->m_leaf;
		if (leaf.m_semiJoin)
			ibBackendCoreException::Error(_("a reading's condition cannot hold a semi-join - write it as IN (SELECT ...)"));

		if (leaf.m_expr) {
			leaf.m_expr      = ConditionExprOnPass(reg, source, shape, creditSide, kindsDr, kindsCr, leaf.m_expr);
			leaf.m_valueExpr = ConditionExprOnPass(reg, source, shape, creditSide, kindsDr, kindsCr, leaf.m_valueExpr);
			return ibQueryPredicate::Leaf(leaf);
		}

		const bool walks = !leaf.m_path.empty();
		const ibBackendQueryColumn* head = walks ? leaf.m_path.front() : leaf.m_col;

		// ⭐ `IN HIERARCHY` HERE IS A SELECTION — the fold by it is the account argument's, which took the plain
		// account names before this (SplitAccountCondition). So the word is resolved into the subtree it stands
		// for, read through the column on the movements (or the row a walk ends at), and the leaf is an IN.
		if (leaf.m_unfold != ibQueryDimUnfold::Elements) {
			const ibBackendQueryable* owner = reg->GetQueryable();
			const ibAcctConditionColumn onLines = ConditionColumnOn(reg, owner, shape, creditSide, kindsDr, kindsCr, head);
			const ibBackendQueryColumn* column = onLines.m_column != nullptr ? onLines.m_column
				: (!onLines.m_byKind.empty() ? onLines.m_byKind.front().second : nullptr);
			for (size_t hop = 1; column != nullptr && walks && hop < leaf.m_path.size(); ++hop) {
				owner  = owner->GetProvider().ResolveReferenceTarget(owner, column);
				column = owner != nullptr ? owner->ResolveColumnByName(leaf.m_path[hop]->GetName()) : nullptr;
			}
			if (column == nullptr)
				ibRegRefuseConditionColumn(head);
			leaf.m_values = ibQueryHierarchyScope(owner, column, leaf.m_values, leaf.m_unfold).Accepted();
			leaf.m_unfold = ibQueryDimUnfold::Elements;
			leaf.m_op     = ibQueryFilterOp::In;
		}

		return over(head, /*nullWhereAbsent*/ false, [&](const ibBackendQueryColumn* column) {
			ibQueryCondition here = leaf;
			if (walks) {
				here.m_path.front() = column;
				here.m_asExists     = true;
			}
			else {
				here.m_col = column;
			}
			return ibQueryPredicate::Leaf(here);
		});
	}

	case ibQueryPredicateKind::IsNull:
	case ibQueryPredicateKind::RefType: {
		if (condition->m_expr)
			ibBackendCoreException::Error(_("a reading's condition tests a field, not a computed value"));
		const bool walks = condition->m_path.size() > 1;
		const bool isNull = condition->m_kind == ibQueryPredicateKind::IsNull && !condition->m_negated;
		return over(walks ? condition->m_path.front() : condition->m_col, isNull,
			[&](const ibBackendQueryColumn* column) {
				auto here = std::make_shared<ibQueryPredicate>(*condition);
				if (walks)
					here->m_path.front() = column;
				else
					here->m_col = column;
				return ibQueryPredicatePtr(here);
			});
	}
	}
	return nullptr;
}

// The condition, applied to one pass of a reading built through the door. `creditSide` is the pass's side;
// a reading whose row carries both sides (the matrix, a correspondence listing) passes the debit one and
// the columns name their sides themselves.
void WhereCondition(ibDataQueryBuilder& b, const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* source,
                    ibAcctShape shape, bool creditSide,
                    const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
                    const ibQueryPredicatePtr& filter)
{
	if (const ibQueryPredicatePtr here = ConditionOnPass(reg, source, shape, creditSide, kindsDr, kindsCr, filter))
		b.Where(here);
}

// ⭐ THE ACCOUNT ARGUMENT, TAKEN APART. What the reading FOLDS by is a list of accounts — `Account IN
// HIERARCHY (&A)`, `Account = &B` — and that list is only ever what the top-level AND says about the account
// itself. Everything else written into the slot (`NOT Account.OffBalance`, `Account.Code LIKE "6%"`, an OR of
// two accounts) is a SELECTION, and it joins the condition. `namesAccount` says which leaf is a plain naming
// of this slot's account.
void SplitAccountCondition(const ibQueryPredicatePtr& condition,
                           const std::function<bool(const ibQueryCondition&)>& namesAccount,
                           ibQueryPredicatePtr& accounts, ibQueryPredicatePtr& selection)
{
	if (!condition)
		return;
	if (condition->m_kind == ibQueryPredicateKind::And) {
		for (const ibQueryPredicatePtr& child : condition->m_children)
			SplitAccountCondition(child, namesAccount, accounts, selection);
		return;
	}
	if (condition->m_kind == ibQueryPredicateKind::Leaf && namesAccount(condition->m_leaf))
		accounts = AndWith(accounts, condition);
	else
		selection = AndWith(selection, condition);
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

// …and the totals' OTHER guard, for a pass that stands on one side: that side names an account
// (ibRegSideNamed). A stored total never took an unnamed side in; a pass over the raw movements has to leave
// it out itself — the unnamed side of an off-balance entry, or it reports under an empty account.
void WhereSideNamed(ibDataQueryBuilder& b, const ibBackendQueryable* source, const ibValueMetaObjectAttributeBase* sideAccount,
                    bool onMovements)
{
	if (!onMovements || sideAccount == nullptr)
		return;
	if (const ibBackendQueryColumn* here = ColumnOn(source, sideAccount))
		if (const ibQueryPredicatePtr named = ibRegSideNamed(here, ibValueTypeDescription::AdjustValue(here->GetTypeDesc(), sideAccount->GetMetaData())))
			b.Where(named);
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
// the day's total, once as itself — and the result looks entirely plausible. The arms themselves are
// ibRegStoredArm / ibRegMovementArm (registerQueryLowering.h), shared with the accumulation register.

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

// ⭐⭐ ONE END OF AN INTERVAL, AS A MOMENT — the instant, and the document at it when the bound names one.
//
// A bound read from a point in time carries both halves (ibReadRegisterBound), and the server road compares
// them together: the instant first, the document only among the postings of that very instant
// (BoundedByMoment, databaseMaterializeBuilder.cpp). This road compared the instant alone, so "up to
// receipt No. 2" took every posting of that second — No. 3 and No. 4 with it — a plausible wrong total and
// no refusal (found 2026-09-16). The document is compared as an ORDERED value of its column, which the door
// spreads over its fields in the same order the server's tuple walks (DecomposeOrdered), so the two roads
// place a posting of that instant on the same side.
//
//   atMost      up to the bound (its upper end)      period < d   OR (period = d AND recorder <= doc)
//   !atMost     from the bound on (its lower end)    period > d   OR (period = d AND recorder >= doc)
//   excluding   the named document itself is out     the last comparison opens: <, >
ibQueryPredicatePtr MomentCondition(const ibBackendQueryColumn* periodCol, const ibBackendQueryColumn* recorderCol,
                                    const ibRegBound& bound, bool atMost)
{
	if (periodCol == nullptr || bound.IsEmpty())
		return nullptr;
	const ibQueryFilterOp closed = atMost ? ibQueryFilterOp::LessEqual : ibQueryFilterOp::GreaterEqual;
	const ibQueryFilterOp open   = atMost ? ibQueryFilterOp::Less      : ibQueryFilterOp::Greater;
	if (!bound.HasRecorder() || recorderCol == nullptr)
		return Compare(periodCol, bound.m_excluding ? open : closed, bound.m_date);
	return OrWith(Compare(periodCol, open, bound.m_date),
		AndWith(Compare(periodCol, ibQueryFilterOp::Equal, bound.m_date),
		        Compare(recorderCol, bound.m_excluding ? open : closed, bound.m_recorder)));
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
		return ibRegStoredArm(recorderCol);

	const ibValue floor = ibValue(ibTruncateToPeriod(bound.m_date.GetDateTime(), grain));

	const ibQueryPredicatePtr stored = AndWith(ibRegStoredArm(recorderCol),
		Compare(periodCol, ibQueryFilterOp::Less, floor));
	const ibQueryPredicatePtr moves = AndWith(ibRegMovementArm(recorderCol),
		AndWith(Compare(periodCol, ibQueryFilterOp::GreaterEqual, floor),
		        MomentCondition(periodCol, recorderCol, bound, /*atMost*/ true)));

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
		// A bound that names a DOCUMENT reaches inside its grain even on the grain's edge: the postings of
		// that instant before the document are outside, so the grain cannot be a stored row (ibRegFillArmCut).
		storedFrom = (floor == moment && !begin.HasRecorder()) ? begin.m_date : ibValue(ibNextPeriodStart(moment, grain));
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

	ibQueryPredicatePtr stored = ibRegStoredArm(recorderCol);
	stored = AndWith(stored, Compare(periodCol,
		storedStartsAtBound && begin.m_excluding ? ibQueryFilterOp::Greater : ibQueryFilterOp::GreaterEqual, storedFrom));
	stored = AndWith(stored, Compare(periodCol, ibQueryFilterOp::Less, storedTo));

	// The head: movements from the lower bound up to the first whole grain.
	ibQueryPredicatePtr head;
	if (!storedFrom.IsEmpty() && !storedStartsAtBound) {
		head = AndWith(ibRegMovementArm(recorderCol),
			AndWith(MomentCondition(periodCol, recorderCol, begin, /*atMost*/ false),
			        AndWith(Compare(periodCol, ibQueryFilterOp::Less, storedFrom),
			                MomentCondition(periodCol, recorderCol, end, /*atMost*/ true))));
	}

	// The tail: movements of the grain the upper bound falls into, up to the bound itself.
	ibQueryPredicatePtr tail;
	if (!tailFrom.IsEmpty()) {
		tail = AndWith(ibRegMovementArm(recorderCol),
			AndWith(Compare(periodCol, ibQueryFilterOp::GreaterEqual, tailFrom),
			        AndWith(MomentCondition(periodCol, recorderCol, end, /*atMost*/ true),
			                MomentCondition(periodCol, recorderCol, begin, /*atMost*/ false))));
	}
	// ⚠ EACH MOVEMENT ARM IS BOUNDED BY BOTH ENDS. When the interval starts and ends inside ONE grain the head and
	// the tail cover the same grain, and a tail bounded by the end alone took that grain's postings from its very
	// start — `Turnovers(10:00, 23:59)` read the whole day, a document before the start included (measured
	// 2026-09-17, three postings in one second). The rows are the union of the arms, so the bound belongs to each.

	return OrWith(stored, OrWith(head, tail));
}

// The movements' own recorder column, or null for a register kept without one (a bound then is its instant).
const ibBackendQueryColumn* RecorderColumnOf(const ibValueMetaObjectAccountingRegister* reg)
{
	return reg != nullptr && reg->HasRecorder() && reg->GetRegisterRecorder() != nullptr
		? reg->GetRegisterRecorder()->GetQueryColumn() : nullptr;
}

// The same ends over the MOVEMENTS, as moments (MomentCondition): `recorderCol` is the movements' recorder,
// which a bound naming a document is compared by.
void WherePeriodAtMost(ibDataQueryBuilder& b, const ibBackendQueryColumn* periodCol,
                       const ibBackendQueryColumn* recorderCol, const ibRegBound& bound)
{
	if (const ibQueryPredicatePtr upTo = MomentCondition(periodCol, recorderCol, bound, /*atMost*/ true))
		b.Where(upTo);
}

void WherePeriodRange(ibDataQueryBuilder& b, const ibBackendQueryColumn* periodCol,
                      const ibBackendQueryColumn* recorderCol, const ibRegBound& begin, const ibRegBound& end)
{
	if (const ibQueryPredicatePtr from = MomentCondition(periodCol, recorderCol, begin, /*atMost*/ false))
		b.Where(from);
	if (const ibQueryPredicatePtr upTo = MomentCondition(periodCol, recorderCol, end, /*atMost*/ true))
		b.Where(upTo);
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
// ⭐ AND WHICH FIELD IS READ IS THE SIDE'S TOO. A balanced figure is one column of the movement and
// both sides sum it; a SPLIT one is two — what the debit side took and what the credit side gave — so
// a side's figure is read from that side's half. The condition below is unchanged: it still says WHICH
// ROWS count, and the half says WHAT is summed on them.
ibQueryColumnExprPtr SideFigure(const ibValueMetaObjectAccountingRegister* reg,
                                const ibValueMetaObjectResource* resource, bool credit)
{
	if (resource == nullptr)
		return nullptr;

	const ibBackendQueryColumn* field = reg->GetRegisterResource(credit, resource);
	if (field == nullptr)
		return nullptr;

	if (reg->IsCorrespondence())
		return ibQueryColumnExpr::Col(field);   // the pass decides the side; the row carries no flag

	const ibValueMetaObjectAttributeBase* recordType = reg->GetRegisterRecordType();
	if (recordType == nullptr)
		return ibQueryColumnExpr::Col(field);

	ibQueryCondition leaf;
	leaf.m_col   = recordType->GetQueryColumn();
	leaf.m_op    = ibQueryFilterOp::Equal;
	leaf.m_value = ibValue::CreateEnumObject<ibValueEnumAccountingRegisterRecordType>(
		credit ? ibAccountingRecordType::eCredit : ibAccountingRecordType::eDebit);

	std::vector<std::pair<ibQueryPredicatePtr, ibQueryColumnExprPtr>> cases;
	cases.push_back({ ibQueryPredicate::Leaf(leaf), ibQueryColumnExpr::Col(field) });
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
// Account -> the dimension kinds of its kinds table — all of them, or the ones it keeps SUMMARY ONLY.
// Read whole, in one go (see KindsByAccount): a map of an answer, not a cache of one.
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

// ⭐⭐ …AND THE STORED NAME OF THE SAME FIGURE — `<the resource's own field><figure>`, the twin of the
// view's ibAcctTurnoverField and of the accumulation register's ibAccumFigureField, and for their reason
// plus one of its own.
//
// 🛑 A NAME IS NOT AN IDENTIFIER THE DRIVER CAN HAND BACK. A server reading is read back by the labels
// of its result, and Firebird's descriptor carries 31 characters of a label: `CurrencyAmountClosingGross
// BalanceDr` and `…Cr` both came back as `CURRENCYAMOUNTCLOSINGGROSSBALANC`, neither was found by its
// name, and the continental ledger's gross currency balance read as zero while the same pair folded to
// the right 200 (measured 2026-09-16). The resource's field is short by construction and is the name the
// configuration's author never chooses — so a figure column is kept, and every server projection
// answers, under this one; FigureName stays what a query writes and what the RAM rows are poured by.
wxString FigureField(const ibValueMetaObjectAttributeBase* resource, const wxString& suffix)
{
	return ibRegValueField(resource) + suffix;
}

// One field a relation answers under: where it is STORED (or what the statement wrote it as), and the
// field of the published column it answers under. The two are one string for the account and the
// dimensions — the view keeps them under the movements' own fields and the shape publishes them the
// same way — and two for the breakdown.
struct ibAcctServerKey
{
	wxString                    m_stored;
	wxString                    m_published;
	const ibBackendQueryColumn* m_column = nullptr;   // the stored column the field is one of
};

// A column is published under a name of its own — a slot as `AccountDimension1`, and on the CREDIT side
// of a correspondence register the credit account as `AccountDr`, the credit half of a dimension under
// the dimension — so its fields are paired with the published column's by ROLE: the type tag with the
// type tag, the reference's table with its table, never by spelling.
void PairByRole(std::vector<ibAcctServerKey>& keys, const ibBackendQueryColumn* stored,
                const ibBackendQueryColumn* published)
{
	if (stored == nullptr)
		return;
	if (published == nullptr) {
		for (const wxString& field : ColumnFieldNames(stored))
			keys.push_back({ field, field, stored });
		return;
	}
	const std::vector<ibColumnSlot> to = DescribeColumnLayout(published);
	for (const ibColumnSlot& from : DescribeColumnLayout(stored))
		for (const ibColumnSlot& slot : to)
			if (slot.m_role == from.m_role) {
				keys.push_back({ from.m_name, slot.m_name, stored });
				break;
			}
}

// The fields a key groups by, as the codec reads them — defined with the server readings below, where
// its note says why an empty value needs it.
std::unordered_map<wxString, ibQueryExprPtr> KeyFieldsAsRead(const ibBackendQueryColumn* column,
                                                              const ibMetaData* metaData, const wxString& alias);

// ⭐⭐ THE ACCOUNT'S TYPE IS A RULE FOR READING THE OTHER SIDE, not a storage shape.
//
// Both sides are always stored and always computed; what the type decides is what the opposite one
// MEANS. On an active account a credit entry is a REVERSAL — it reduces the debit balance and is not a
// credit balance of its own — so the two fold into one number with a sign. A passive account is the
// mirror. An **active-passive** account folds NOT ACROSS ITS ANALYTICS: it can stand on both sides at
// once (classic mutual settlements, where the same account owes some counterparties and is owed by
// others), and a receivable of 100 against a payable of 100 is not "zero" — that answer is wrong in a
// way no formatting can undo. WITHIN one set of its analytics — one counterparty — it folds like any
// other account (see AtFullAnalytics).
//
// The type is read from the ACCOUNT, once per account: it is data, and the engine has been storing it
// for years without ever asking (GetAccountType had no callers at all).
// Declared here, defined below beside the flag it reads: the fold needs it, and the fold reads better
// next to the key it is folding than at the bottom of the file. `onlySummary` narrows the answer to
// the kinds kept for turnovers only; without it, every kind the account's table lists.
ibAcctSummaryMap KindsByAccount(const ibValueMetaObjectChartOfAccounts* chart, bool onlySummary);
ibAcctSummaryMap KindsByAccount(const ibValueMetaObjectChartOfAccounts* chart, const ibValueMetaObjectAttributeBase* flag);

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

	const ibAcctSummaryMap summaryOnlyByAccount = KindsByAccount(chart, /*onlySummary*/ true);

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
ibAcctSummaryMap KindsByAccount(const ibValueMetaObjectChartOfAccounts* chart, bool onlySummary)
{
	const ibValueMetaObjectAccountDimensionKindsTable* table = chart != nullptr ? chart->GetAccountDimensionKindsTable() : nullptr;
	if (table == nullptr || (onlySummary && table->GetSummaryOnly() == nullptr))
		return ibAcctSummaryMap();
	return KindsByAccount(chart, onlySummary ? table->GetSummaryOnly() : nullptr);
}

// …and the same reading NARROWED BY ANY FLAG COLUMN of the kinds table — "turnovers only", or a tick of a
// breakdown's accounting kind ("the quantity is kept by this subconto"). No flag: every kind of every account.
// A flag left empty on a row reads as unticked, which is what an older row without the column means.
ibAcctSummaryMap KindsByAccount(const ibValueMetaObjectChartOfAccounts* chart, const ibValueMetaObjectAttributeBase* flag)
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
				const ibValueMetaObjectAttributeBase* kindColumn = table->GetAccountDimensionKind();
				const ibBackendQueryable* rows = table->GetQueryable();

				if (rows != nullptr && kindColumn != nullptr) {

					// The owning account — asked by the chart's own reference, which is what the section names its owner after.
					const ibBackendQueryColumn* ownerCol = chart->GetDataReference() != nullptr
						? rows->ResolveColumnByName(chart->GetDataReference()->GetName()) : nullptr;
					const ibBackendQueryColumn* kindCol  = ColumnOn(rows, kindColumn);
					const ibBackendQueryColumn* flagCol  = flag != nullptr ? ColumnOn(rows, flag) : nullptr;

					if (ownerCol != nullptr && kindCol != nullptr && (flag == nullptr || flagCol != nullptr)) {
						ibDataQueryBuilder b;
						b.From(rows);
						// ⚠ NOT FILTERED BY THE CALLER'S RIGHTS, for the same reason the readings above
						// are not: which slots a total keeps is a property of the chart, not of who is
						// looking, and a key that narrows per user is a key that disagrees with itself.
						b.WithAccessPolicy(nullptr);
						b.Select(ownerCol, ownerCol->GetName());
						b.Select(kindCol,  kindCol->GetName());
						if (flagCol != nullptr)
							b.Select(flagCol, flagCol->GetName());

						// ⚠ NO FILTER, DELIBERATELY. Narrowing to one account is what made this a call
						// per account; the whole table is what a totals reading ends up needing, and
						// asking for it once costs one statement instead of one per row of the report.
						ibDataQueryResult sel = b.Execute(ibReadPageRequest{});
						while (sel.Next()) {
							if (flagCol != nullptr && !sel.GetValue(flagCol).GetBoolean())
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

// ⭐⭐ DOES THIS ROW STAND ON ONE SET OF THE ACCOUNT'S ANALYTICS? — the grain at which an active-passive
// balance may be folded (accounting-register-arc.md §4.7: "folding is legitimate only WITHIN one identical
// set of dimension values, never across an account"). It does when the reading reports every slot as it
// stands (no kinds asked — BreakdownWidth), when the account keeps no analytics at all, or when the kinds
// asked cover every kind the account keeps a BALANCE along (its turnovers-only kinds left the key in
// FoldOutSummaryOnly). A breakdown by fewer kinds than that is a row across several sets — a receivable
// and a payable of one counterparty's two contracts — and stays on both sides.
bool AtFullAnalytics(const ibValue& account, const std::vector<ibValue>& askedKinds,
                     const ibAcctSummaryMap& kindsByAccount, const ibAcctSummaryMap& summaryOnlyByAccount)
{
	if (askedKinds.empty())
		return true;
	const auto kinds = kindsByAccount.find(account);
	if (kinds == kindsByAccount.end())
		return true;
	const auto summaryOnly = summaryOnlyByAccount.find(account);
	for (const ibValue& kind : kinds->second) {
		if (summaryOnly != summaryOnlyByAccount.end() && summaryOnly->second.count(kind) != 0)
			continue;
		if (std::find(askedKinds.begin(), askedKinds.end(), kind) == askedKinds.end())
			return false;
	}
	return true;
}

// Fold one pair of figures by the account's type. Applied at READ time, which is why declining to fold
// costs nothing and an unfolded reading of the same data stays available.
//
// An active-passive pair folds only `withinOneAnalyticsSet` (AtFullAnalytics), and then onto the side its
// net stands on: one counterparty that was shipped 69 820 and paid 55 000 OWES 14 820 — reporting both
// figures as its balance (as this did until 2026-09-15) reads as a receivable and a payable at once.
void FoldSideByAccountType(int accountType, ibValue& debit, ibValue& credit, bool withinOneAnalyticsSet)
{
	if (accountType == ibAccountType::eActivePassive && !withinOneAnalyticsSet)
		return;

	const ibNumber net = debit.GetNumber() - credit.GetNumber();
	const bool onDebit = accountType == ibAccountType::eActive
		|| (accountType == ibAccountType::eActivePassive && !(net < ibNumber()));
	if (onDebit) {
		debit  = ibValue(net);            // a credit entry REDUCED the debit balance
		credit = ibValue(ibNumber());
	}
	else {
		debit  = ibValue(ibNumber());
		credit = ibValue(ibNumber() - net);
	}
}

// ⭐ THE SAME FOLD, SAID TO THE SERVER — FoldSideByAccountType as a CASE over the account's declared type,
// for the two readings that fold on the server (balance, balance-and-turnovers). An active-passive pair folds
// onto the side its net stands on where its row stands on one set of the account's analytics — `apFolds`, the
// server's AtFullAnalytics (FullAnalyticsOnServer); null when every row does, as when the slots are read as they
// stand. An account row that is missing (the LEFT join to the chart) matches no type and falls through to "do
// not fold", the answer that loses nothing.
std::pair<ibQueryExprPtr, ibQueryExprPtr> FoldedPairOnServer(const ibQueryExprPtr& accountType,
                                                             const ibQueryExprPtr& debit, const ibQueryExprPtr& credit,
                                                             const ibQueryExprPtr& apFolds = nullptr)
{
	const auto isType = [&accountType](ibAccountType declared) {
		return ibBinOp(ibQueryBinOp::Eq, accountType, ibConst(ibValue(static_cast<int>(declared))));
	};
	const ibQueryExprPtr zero        = ibConst(ibValue(0.0));
	const ibQueryExprPtr apFolding   = apFolds ? ibBinOp(ibQueryBinOp::And, isType(ibAccountType::eActivePassive), apFolds)
	                                           : isType(ibAccountType::eActivePassive);
	const ibQueryExprPtr debitStands = ibBinOp(ibQueryBinOp::And, apFolding, ibBinOp(ibQueryBinOp::Ge, debit, credit));

	const ibQueryExprPtr dr = ibCase({ { isType(ibAccountType::eActive),       ibBinOp(ibQueryBinOp::Sub, debit, credit) },
	                                   { isType(ibAccountType::ePassive),      zero },
	                                   { debitStands,                          ibBinOp(ibQueryBinOp::Sub, debit, credit) },
	                                   { apFolding,                            zero } }, debit);
	const ibQueryExprPtr cr = ibCase({ { isType(ibAccountType::ePassive),      ibBinOp(ibQueryBinOp::Sub, credit, debit) },
	                                   { isType(ibAccountType::eActive),       zero },
	                                   { debitStands,                          zero },
	                                   { apFolding,                            ibBinOp(ibQueryBinOp::Sub, credit, debit) } }, credit);
	return { dr, cr };
}

// ⭐ IS THIS FIGURE KEPT ON THE ACCOUNT JOINED AS `chartAlias` — said to the server. Null when the
// resource names no kind of accounting: such a figure belongs to every account. The flag is the chart's
// own boolean the resource names, compared through the codec like any other value; a chart that never
// declared that kind keeps it nowhere. The twin of IsAccountingKindKept, which asks the account object.
ibQueryExprPtr KindKeptOnServer(const ibValueMetaObjectAccountingRegister* reg, const ibValueMetaObjectResource* resource,
                            const wxString& chartAlias)
{
	const ibMetaDescription& kind = resource->GetAccountingKind();
	if (!kind.IsOk())
		return nullptr;
	if (const ibValueMetaObjectChartOfAccounts* chart = reg->GetChartOfAccounts())
		for (const ibValueMetaObjectAccountingKind* flag : chart->GetAccountingKindArrayObject())
			if (flag != nullptr && flag->GetMetaID() == kind.GetByIdx(0))
				return ibRegCompositeIR(flag->GetQueryColumn(), reg->GetMetaData(), ibValue(true), ibQueryBinOp::Eq, chartAlias);
	// Typed, because two bare parameters compared have no type to prepare (Firebird: -804).
	return ibBinOp(ibQueryBinOp::Eq, ibCast(ibConst(ibValue(1)), ibTypeInteger()), ibCast(ibConst(ibValue(0)), ibTypeInteger()));
}

// A figure as the account keeps it: itself where kept, NULL where not — a CASE with no ELSE.
ibQueryExprPtr FigureWhereKept(const ibQueryExprPtr& kept, const ibQueryExprPtr& figure)
{
	return kept ? ibCase({ { kept, figure } }, nullptr) : figure;
}

// Does any resource of the register depend on the account for being kept? When none does, a reading
// needs no join to the chart for it.
bool AnyFigureKeptByKind(const ibValueMetaObjectAccountingRegister* reg)
{
	for (const auto resource : reg->GetResourceArrayObject())
		if (resource != nullptr && resource->GetAccountingKind().IsOk())
			return true;
	return false;
}

// The BREAKDOWN's accounting kind a resource is kept by ("the quantity is kept by each subconto that says
// so") — the tick column of the account's kinds table it names; null when it names none.
const ibValueMetaObjectAttributeBase* BreakdownKindOf(const ibValueMetaObjectAccountingRegister* reg,
                                                     const ibValueMetaObjectResource* resource)
{
	const ibMetaDescription& kind = resource != nullptr ? resource->GetAccountDimensionAccountingKind() : ibMetaDescription();
	const ibValueMetaObjectChartOfAccounts* chart = reg->GetChartOfAccounts();
	if (!kind.IsOk() || chart == nullptr)
		return nullptr;
	for (const ibValueMetaObjectAccountDimensionAccountingKind* flag : chart->GetAccountDimensionAccountingKindArrayObject())
		if (flag != nullptr && flag->GetMetaID() == kind.GetByIdx(0))
			return flag;
	return nullptr;
}

// Does any resource name a BREAKDOWN's accounting kind? The metadata half of the rule below — the joins it needs
// are built only then.
bool AnyFigureKeptByBreakdown(const ibValueMetaObjectAccountingRegister* reg)
{
	for (const auto resource : reg->GetResourceArrayObject())
		if (BreakdownKindOf(reg, resource) != nullptr)
			return true;
	return false;
}

// ⭐ THE ACCOUNT'S KINDS TABLE AS THE SERVER READINGS ASK IT — the rows, the owner's key field, the kind and the
// turnovers-only flag. The owner is ONE raw key field, the account a spread reference: paired by role they share
// nothing, so a row's account identity (its ReferenceId field, ibRegFieldOfRole) is compared with the owner key itself. `m_rows` is null
// where the chart has no such table to ask.
struct ibAcctKindsTable
{
	const ibValueMetaObjectAccountDimensionKindsTable* m_table = nullptr;
	const ibBackendQueryable*   m_rows       = nullptr;
	wxString                    m_ownerField;
	const ibBackendQueryColumn* m_kindCol    = nullptr;
	const ibBackendQueryColumn* m_summaryCol = nullptr;
};

ibAcctKindsTable KindsTableOf(const ibValueMetaObjectAccountingRegister* reg)
{
	ibAcctKindsTable out;
	const ibValueMetaObjectChartOfAccounts* chart = reg->GetChartOfAccounts();
	const ibValueMetaObjectAccountDimensionKindsTable* table = chart != nullptr ? chart->GetAccountDimensionKindsTable() : nullptr;
	const ibBackendQueryable* rows = table != nullptr ? table->GetQueryable() : nullptr;
	if (rows == nullptr || chart->GetDataReference() == nullptr)
		return out;
	const ibBackendQueryColumn* ownerCol = rows->ResolveColumnByName(chart->GetDataReference()->GetName());
	const std::vector<wxString> ownerFields = ownerCol != nullptr ? ColumnFieldNames(ownerCol) : std::vector<wxString>();
	const ibBackendQueryColumn* kindCol = ColumnOn(rows, table->GetAccountDimensionKind());
	if (ownerFields.size() != 1 || kindCol == nullptr)
		return out;
	out.m_table      = table;
	out.m_rows       = rows;
	out.m_ownerField = ownerFields.front();
	out.m_kindCol    = kindCol;
	out.m_summaryCol = table->GetSummaryOnly() != nullptr ? ColumnOn(rows, table->GetSummaryOnly()) : nullptr;
	return out;
}

// A condition as a typed 0/1 — what a flag that may be NULL is tested through, and what a UNION arm carries. Typed,
// because a CASE of two bare parameters has no type to prepare (Firebird: -804).
ibQueryExprPtr ibAcctOneIf(const ibQueryExprPtr& condition)
{
	return ibCase({ { condition, ibCast(ibConst(ibValue(1)), ibTypeInteger()) } }, ibCast(ibConst(ibValue(0)), ibTypeInteger()));
}

ibQueryExprPtr ibAcctIsOne(const ibQueryExprPtr& flag)
{
	return ibBinOp(ibQueryBinOp::Eq, flag, ibCast(ibConst(ibValue(1)), ibTypeInteger()));
}

// EXISTS a row of the kinds table of the account `rowAlias.accountId` that meets `where` (asked under `k`).
ibQueryExprPtr KindsRowExists(const ibAcctKindsTable& kinds, const wxString& rowAlias, const wxString& accountId,
	const ibQueryExprPtr& where, const wxString& k, bool negated = false)
{
	ibQueryExprPtr on = ibBinOp(ibQueryBinOp::Eq, ibCol(k, kinds.m_ownerField), ibCol(rowAlias, accountId));
	if (where)
		on = ibBinOp(ibQueryBinOp::And, on, where);
	return ibExists(ibProject(ibFilter(ibScan(kinds.m_rows->GetQueryTableName(), k), on),
		{ { ibCol(k, kinds.m_ownerField), kinds.m_ownerField } }), negated);
}

// ⭐ DOES AN ACTIVE-PASSIVE ROW STAND ON ONE SET OF ITS ACCOUNT'S ANALYTICS? — AtFullAnalytics, said to the server:
// no row of the account's kinds table that keeps a balance and is not among the kinds asked. Null when nothing was
// asked: the slots as they stand are the full set.
ibQueryExprPtr FullAnalyticsOnServer(const ibValueMetaObjectAccountingRegister* reg, const std::vector<ibValue>& kinds,
	const wxString& rowAlias, const ibBackendQueryColumn* accountCol, const wxString& k)
{
	const ibAcctKindsTable table = KindsTableOf(reg);
	const wxString accountId = accountCol != nullptr ? ibRegFieldOfRole(accountCol, ibColumnRole::ReferenceId) : wxString();
	if (kinds.empty() || table.m_rows == nullptr || accountId.IsEmpty())
		return nullptr;
	ibQueryExprPtr anyAsked;
	for (const ibValue& kind : kinds)
		if (const ibQueryExprPtr one = ibRegCompositeIR(table.m_kindCol, reg->GetMetaData(), kind, ibQueryBinOp::Eq, k))
			anyAsked = anyAsked ? ibBinOp(ibQueryBinOp::Or, anyAsked, one) : one;
	// Each test as a 0/1: a flag never written compares as NULL, and a NOT over it would drop the row.
	ibQueryExprPtr uncovered = anyAsked ? ibBinOp(ibQueryBinOp::Eq, ibAcctOneIf(anyAsked), ibCast(ibConst(ibValue(0)), ibTypeInteger())) : nullptr;
	if (table.m_summaryCol != nullptr) {
		const ibQueryExprPtr keepsBalance = ibBinOp(ibQueryBinOp::Eq,
			ibAcctOneIf(ibRegCompositeIR(table.m_summaryCol, reg->GetMetaData(), ibValue(true), ibQueryBinOp::Eq, k)),
			ibCast(ibConst(ibValue(0)), ibTypeInteger()));
		uncovered = uncovered ? ibBinOp(ibQueryBinOp::And, uncovered, keepsBalance) : keepsBalance;
	}
	return KindsRowExists(table, rowAlias, accountId, uncovered, k, /*negated*/ true);
}

// The kind each breakdown slot of a server row stands in: its kind column when the slots were read as they stand,
// or the kind the call asked for (a column MEANS it then).
std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> SlotKindsOf(const ibValueMetaObjectAccountingRegister* reg,
	const ibBackendQueryable* published, ibAcctShape shape, bool creditSide, const std::vector<ibValue>& kinds)
{
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> out;
	std::vector<ibAcctBreakdownColumn> layout;
	DescribeBreakdown(reg, shape, creditSide, kinds, layout);
	for (const ibAcctBreakdownColumn& column : layout) {
		const ibBackendQueryColumn* kindColumn = !column.m_kindAlias.IsEmpty() && published != nullptr
			? published->ResolveColumnByName(column.m_kindAlias) : nullptr;
		if (kindColumn != nullptr || !column.m_requestedKind.IsEmpty())
			out.push_back({ kindColumn, column.m_requestedKind });
	}
	return out;
}

// ⭐⭐ KEPT BY A SUBCONTO, SAID TO THE SERVER — the rule ReportFiguresAsKept applies in RAM, so a reading keeps its
// road whatever the ticks are. Each slot of a row is joined to its ACCOUNT's kinds-table row for the kind standing
// in it: none found — the slot is empty, or no subconto of that account — and it cuts nothing; found — its tick
// says whether a figure is kept by it (KeptBySubcontoOnServer). One LEFT join per slot, against a table of a few
// rows per account: the cost does not grow with the movements. `kindAliases` names the joins.
ibQueryRelPtr JoinSubcontoKinds(const ibValueMetaObjectAccountingRegister* reg, ibQueryRelPtr rel,
	const ibBackendQueryColumn* accountCol, const wxString& rowAlias,
	const std::vector<std::pair<const ibBackendQueryColumn*, ibValue>>& slotKinds,
	const wxString& prefix, std::vector<wxString>& kindAliases)
{
	// ⚠ THE OWNER IS ONE RAW KEY FIELD (KindsTableOf): paired by role with the account the join said nothing at all.
	const ibAcctKindsTable table = KindsTableOf(reg);
	const wxString accountId = accountCol != nullptr ? ibRegFieldOfRole(accountCol, ibColumnRole::ReferenceId) : wxString();
	if (table.m_rows == nullptr || accountId.IsEmpty())
		return rel;

	for (size_t i = 0; i < slotKinds.size(); ++i) {
		const wxString k = prefix + wxString::Format(wxT("_k%u"), static_cast<unsigned>(i));
		const ibQueryExprPtr sameAccount = ibBinOp(ibQueryBinOp::Eq, ibCol(rowAlias, accountId), ibCol(k, table.m_ownerField));
		const ibQueryExprPtr sameKind = slotKinds[i].first != nullptr
			? ibRegSameValueIR(slotKinds[i].first, rowAlias, table.m_kindCol, k)
			: ibRegCompositeIR(table.m_kindCol, reg->GetMetaData(), slotKinds[i].second, ibQueryBinOp::Eq, k);
		if (!sameAccount || !sameKind)
			continue;
		rel = ibJoin(rel, ibScan(table.m_rows->GetQueryTableName(), k), ibBinOp(ibQueryBinOp::And, sameAccount, sameKind),
			ibQueryJoinType::Left);
		kindAliases.push_back(k);
	}
	return rel;
}

// Kept by every subconto its row stands on: each joined kinds row either absent or ticked for the resource's
// breakdown kind. Null where the resource names no such kind — nothing to judge.
ibQueryExprPtr KeptBySubcontoOnServer(const ibValueMetaObjectAccountingRegister* reg, const ibValueMetaObjectResource* resource,
	const std::vector<wxString>& kindAliases)
{
	const ibValueMetaObjectAttributeBase* flag = BreakdownKindOf(reg, resource);
	const ibAcctKindsTable table = KindsTableOf(reg);
	if (flag == nullptr || kindAliases.empty() || table.m_rows == nullptr)
		return nullptr;
	const ibBackendQueryColumn* flagCol = ColumnOn(table.m_rows, flag);
	const std::vector<wxString> kindFields = ColumnFieldNames(table.m_kindCol);
	if (flagCol == nullptr || kindFields.empty())
		return nullptr;
	// "No such row" is asked of the kind's last field — the identity a joined row always has.
	const wxString presentField = kindFields.back();

	ibQueryExprPtr kept;
	for (const wxString& k : kindAliases) {
		const ibQueryExprPtr one = ibBinOp(ibQueryBinOp::Or, ibIsNull(ibCol(k, presentField)),
			ibRegCompositeIR(flagCol, reg->GetMetaData(), ibValue(true), ibQueryBinOp::Eq, k));
		kept = kept ? ibBinOp(ibQueryBinOp::And, kept, one) : one;
	}
	return kept;
}

// Both judgements of a figure — the account's kind and the subconto's — as one condition; either may be absent.
ibQueryExprPtr BothKept(const ibQueryExprPtr& byAccount, const ibQueryExprPtr& bySubconto)
{
	if (!byAccount) return bySubconto;
	if (!bySubconto) return byAccount;
	return ibBinOp(ibQueryBinOp::And, byAccount, bySubconto);
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
		[&](std::vector<ibTempColumn>& columns)
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
	//
	// ⭐⭐ …UNLESS THE COLUMN IS NOT THE ATTRIBUTE. A row about one account of a correspondence register
	// reports `Account` filled from the debit account on one pass and from the credit account on the other:
	// neither attribute, so it takes an id of its own — the attribute's type and fields, the reading's
	// identity. Composed over the attribute's REAL metaID rather than a running ordinal: the same column
	// then has the same id in every shape and in the condition scope (ScopeFromAccountCondition), whatever
	// the call's arguments put before it, and two such columns cannot meet on one number.
	if (GetRegisterAccount() != nullptr)
		columns.push_back(bothSides || !correspondence
			? ibRegAttributeColumn(GetRegisterAccount())
			: ibRegAttributeColumn(GetRegisterAccount(), PublishedAccountName(this, shape), AccountColumnSynonym(wxEmptyString),
			                       ibRegDerivedColumnId(GetRegisterAccount()->GetMetaID())));
	if (bothSides && GetRegisterAccountCr() != nullptr)
		columns.push_back(ibRegAttributeColumn(GetRegisterAccountCr()));

	// ⭐⭐ THE CORRESPONDENT OF A TURNOVER — the account the row's account moved against, published beside
	// it as the reference publishes it. Synthetic like `Account` above: it is the credit account on the
	// debit pass and the debit account on the credit one. Always in the shape; the ROWS are cut by it only
	// when a query reads it (ibQueryReadColumns) — a turnover of 62 is one row until somebody asks what it
	// moved against.
	const bool withCorrespondent = shape == ibAcctShape::Turnovers && correspondence;
	if (withCorrespondent && GetRegisterAccountCr() != nullptr)
		columns.push_back(ibRegAttributeColumn(GetRegisterAccountCr(), CorrAccountColumnName(), _("Corresponding account"),
		                                       ibRegDerivedColumnId(GetRegisterAccountCr()->GetMetaID())));

	// --- the breakdown ------------------------------------------------------------------------
	// Positional names, the caller's order. The TYPE is the slot's — the chart of characteristic
	// types' own composition — so a column of this table admits exactly what a slot admits.
	// `corr` names the correspondent's breakdown: the credit slots, under `CorrAccountDimension<n>`.
	const auto addBreakdown = [&](bool creditSide, const std::vector<ibValue>& kinds, bool corr) {
		const wxString prefix = SidePrefix(this, shape, creditSide);
		const unsigned int width = BreakdownWidth(this, kinds);
		for (unsigned int no = 0; no < width; no++) {
			// The width never passes the slots (BreakdownWidth), so every column stands on a slot of its own.
			const ibValueMetaObjectAttributeBase* slot = GetAccountDimensionSlot(creditSide, no);
			if (slot == nullptr)
				continue;
			const wxString name = corr ? CorrAccountDimensionColumnName(no + 1) : AccountDimensionColumnName(prefix, no + 1);

			// ⭐ NUMBERED OVER THE SLOT'S REAL metaID, like the account above — a slot has an id of its own,
			// so the column's id is the same in every shape whatever stands before it.

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
				                               kindSlot->GetTypeDesc(), ibRegDerivedColumnId(kindSlot->GetMetaID())));

			// What the slot HOLDS (GetTypeValueDesc), not what it declares: the declaration is the chart's
			// characteristic, one class no value carries, and a column of this table is a plain column with
			// no chart to expand it through — typed by the declaration, it could not be opened in a field
			// picker, offered no value to filter by, and adjusted every counterparty poured into it to
			// nothing (2026-09-15).
			columns.push_back(ibTempColumn(name, name, slot->GetTypeValueDesc(), ibRegDerivedColumnId(slot->GetMetaID())));
		}
	};
	addBreakdown(/*creditSide*/ false, kindsDr, /*corr*/ false);
	if (bothSides)
		addBreakdown(/*creditSide*/ true, kindsCr, /*corr*/ false);
	else if (withCorrespondent)
		addBreakdown(/*creditSide*/ true, kindsCr, /*corr*/ true);

	// --- the register's own dimensions — the standing cut, the same on every line ---------------
	// ⭐ ONE COLUMN OR TWO, AND THE SHAPE DECIDES WHICH. A reading about ONE ACCOUNT has already chosen
	// a side, so a split dimension appears once, under its own name, and each pass fills it from its own
	// half — the reference's Balance table publishes one Currency, not a pair. A reading whose row is about BOTH
	// sides (a movement line, a pair of accounts) publishes both, because the currency given is not the
	// currency taken.
	const bool bothSidesOnOneRow = (shape == ibAcctShape::Records || shape == ibAcctShape::DrCrTurnovers);
	// The field itself, or — on a row about both sides — its two side attributes when it is kept per side.
	const auto addField = [&](const ibValueMetaObjectAttributeBase* field) {
		const ibValueMetaObjectAttributeBase* debit  = bothSidesOnOneRow ? GetFieldSide(/*creditSide*/ false, field) : nullptr;
		const ibValueMetaObjectAttributeBase* credit = bothSidesOnOneRow ? GetFieldSide(/*creditSide*/ true, field) : nullptr;
		if (debit == nullptr || credit == nullptr) {
			columns.push_back(ibRegAttributeColumn(field));
			return;
		}
		columns.push_back(ibRegAttributeColumn(debit));
		columns.push_back(ibRegAttributeColumn(credit));
	};
	for (const auto dimension : GetDimensionArrayObject())
		if (dimension != nullptr)
			addField(dimension);
	// …and the correspondent's half of a dimension kept per side (`CurrencyCorr`): typed as the side, filled
	// from whichever side the correspondent stands on — so an id of its own, like `CorrAccount`.
	if (withCorrespondent)
		for (const auto dimension : GetDimensionArrayObject())
			if (const ibValueMetaObjectAttributeBase* credit = GetFieldSide(/*creditSide*/ true, dimension))
				columns.push_back(ibRegAttributeColumn(credit, CorrFieldColumnName(dimension->GetName()),
					dimension->GetSynonym() + wxT(" ") + _("corr."), ibRegDerivedColumnId(credit->GetMetaID())));

	// --- the figures --------------------------------------------------------------------------
	//
	// ⭐ TAKEN AS (FIGURE, SIDE) RATHER THAN AS THE SPELLED SUFFIX — because the column owes THREE
	// names and two of them are built from that pair: `Resource1BalanceDr` for a query, and "Amount
	// Balance Dr" for whoever reads the column. Handed the finished suffix, this would have had to
	// recover the side by looking at the last two letters, which is classification by spelling —
	// right until a figure ends in "Cr" for a reason of its own.
	// …and a FOURTH thing the column owes: what it is OF. A figure is of its RESOURCE, and saying so is
	// what lets a field list draw it as one — a balance, a turnover and a gross balance are three
	// readings of one declared figure, and a reader picks them out of the cuts at a glance.
	//
	// 🛑 AND IT IS ONE FIELD — the neighbour's lesson (accumulationRegisterMetadataSchema.cpp: "a column
	// that still calls itself composite is asked for `Quantity_Turnover_N`"), learnt again here. The
	// server roads (GetSourceRelation of the balance, the turnovers, balance-and-turnovers, the Dr/Cr
	// turnovers) project each figure as ONE field; a composite `Amount_TurnoverDr` was spread into
	// `Amount_TurnoverDr_TYPE` / `…_N`, which no projection has. The RAM road never noticed — it pours by
	// name — so only the one-sided register, the one that stood on the server, failed: `-206
	// AMOUNT_TURNOVERDR_TYPE` on the Anglo-Saxon trial balance (2026-09-16). The field is kept under
	// FigureField, which says why not under the name.
	// Each numbered over its resource (ibRegDerivedColumnId, from 1); `figureNo` counts the figures of the resource
	// in hand and starts again with the next one.
	unsigned int figureNo = 0;
	const auto addFigure = [&](const ibValueMetaObjectAttributeBase* resource, const wxString& figure, bool credit) {
		const wxString suffix = ibRegSidedFigure(figure, credit);
		columns.push_back(ibTempColumn(FigureName(resource, suffix), FigureField(resource, suffix),
		                               resource->GetTypeDesc(), ibRegDerivedColumnId(resource->GetMetaID(), ++figureNo),
		                               ibRegColumnCaptionOf(resource->GetSynonym(), ibRegSidedCaption(figure, credit)),
		                               ibBackendQueryColumn::Kind::Computed, resource->GetColumnIcon()));
	};

	// A figure with NO side — one row is a pair of accounts, so there is one number and nothing to
	// tell apart. Same pairing of the names, minus the side.
	const auto addSidelessFigure = [&](const ibValueMetaObjectAttributeBase* resource, const wxString& figure) {
		columns.push_back(ibTempColumn(FigureName(resource, figure), FigureField(resource, figure),
		                               resource->GetTypeDesc(), ibRegDerivedColumnId(resource->GetMetaID(), ++figureNo),
		                               ibRegColumnCaptionOf(resource->GetSynonym(), ibRegFigureCaption(figure)),
		                               ibBackendQueryColumn::Kind::Computed, resource->GetColumnIcon()));
	};

	// ⭐⭐ BALANCED OR NOT, A FIGURE IS REPORTED — what the flag decides is how many numbers it is.
	//
	// It used to decide whether the figure appeared at all: a quantity, being non-balance, was left out
	// of the balance columns and, in places, out of the reading entirely. That was a misreading of the
	// word. "Balance" says the two sides of an entry carry ONE value which they must agree on (the
	// amount); cleared, they carry two — the quantity that left the credit account and the quantity
	// that reached the debit one — and each side then has its own turnover AND its own balance, which
	// is exactly what `…Dr` / `…Cr` are. Excluded, "what is on hand in pieces" could not be asked at
	// all (Max, 2026-09-16: "I must see the debit quantity and the credit quantity, and I don't see it
	// here").
	//
	// The one shape that still differs is the account PAIR: balanced, what moved from that credit to
	// that debit is a single number; split, it is two, because what left is not what arrived.
	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const bool keepsBalance = resource->IsBalanceResource();
		figureNo = 0;
		switch (shape) {
		// ⭐⭐ THREE READINGS OF ONE FIGURE, and the reference publishes all three: the balance as ONE
		// signed number (`<Res>Balance` — what is left, on whichever side it stands), the FOLDED pair
		// (`…Dr` / `…Cr`, the figure a bookkeeper signs), and the GROSS pair (the same before the fold
		// by account type — what each side actually holds). All of them are synthetic columns of this
		// surface: one set of numbers, three ways of reporting it, nothing stored twice.
		case ibAcctShape::Balance:
			addSidelessFigure(resource, ibRegFigure::Balance);
			addFigure(resource, ibRegFigure::Balance, /*credit*/ false);
			addFigure(resource, ibRegFigure::Balance, /*credit*/ true);
			addFigure(resource, ibRegFigure::GrossBalance, /*credit*/ false);
			addFigure(resource, ibRegFigure::GrossBalance, /*credit*/ true);
			break;
		case ibAcctShape::Turnovers:
			addSidelessFigure(resource, ibRegFigure::Turnover);
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ false);
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ true);
			// A figure the two sides do not agree on is two numbers on one line — and the correspondent's
			// is published beside the account's. A balanced one would be the same number again.
			if (withCorrespondent && !keepsBalance) {
				addSidelessFigure(resource, ibRegFigure::CorrTurnover);
				addFigure(resource, ibRegFigure::CorrTurnover, /*credit*/ false);
				addFigure(resource, ibRegFigure::CorrTurnover, /*credit*/ true);
			}
			break;
		// One row is a PAIR of accounts. Balanced, that pair moved ONE number and a "TurnoverDr" here
		// would be the same number under a second name; split, it moved two — what left the credit
		// account and what reached the debit one are different things and both are reported.
		case ibAcctShape::DrCrTurnovers:
			if (keepsBalance) {
				addSidelessFigure(resource, ibRegFigure::Turnover);
			}
			else {
				addFigure(resource, ibRegFigure::Turnover, /*credit*/ false);
				addFigure(resource, ibRegFigure::Turnover, /*credit*/ true);
			}
			break;
		// The same three readings of each of the three moments, in the order a person reads them:
		// what was there, what moved, what is left.
		case ibAcctShape::BalanceAndTurnovers:
			addSidelessFigure(resource, ibRegFigure::OpeningBalance);
			addFigure(resource, ibRegFigure::OpeningBalance, /*credit*/ false);
			addFigure(resource, ibRegFigure::OpeningBalance, /*credit*/ true);
			addFigure(resource, ibRegFigure::OpeningGrossBalance, /*credit*/ false);
			addFigure(resource, ibRegFigure::OpeningGrossBalance, /*credit*/ true);
			addSidelessFigure(resource, ibRegFigure::Turnover);
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ false);
			addFigure(resource, ibRegFigure::Turnover, /*credit*/ true);
			addSidelessFigure(resource, ibRegFigure::ClosingBalance);
			addFigure(resource, ibRegFigure::ClosingBalance, /*credit*/ false);
			addFigure(resource, ibRegFigure::ClosingBalance, /*credit*/ true);
			addFigure(resource, ibRegFigure::ClosingGrossBalance, /*credit*/ false);
			addFigure(resource, ibRegFigure::ClosingGrossBalance, /*credit*/ true);
			break;
		// A movement line reports the resource ITSELF — it is not folded, so no figure suffix. Under its own
		// attribute, like every other field of the line: the resource, or its two sides when it is kept per
		// side (Max, 2026-09-16: "movements must show debit and credit everywhere, unless it is balanced").
		case ibAcctShape::Records:
			addField(resource);
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
	// Typed by what the column HOLDS (GetTypeValueDesc): a cell of this table receives a stored value, and
	// a column typed by a characteristic's declaration adjusts every counterparty put into it to nothing.
	for (const ibBackendQueryColumn* col : shape->GetColumns())
		if (col != nullptr)
			table.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeValueDesc());
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
	//
	// ⭐⭐ AND THE ACCOUNT CONDITION IS THE ACCOUNT — the one whose balance is asked — so EVERY PASS reads
	// it on its OWN account column: the debit pass on the debit account, the credit pass on the credit
	// account, both reported under it. The correspondent (a reading that names one) is read on the OPPOSITE
	// column. It was tied to the COLUMN instead of the pass — the account condition always on the debit
	// column — so the credit pass of "the balance of 36" read the credit side of the entries DEBITING 36:
	// its correspondents (70, broken down by item), under their own accounts, and with no breakdown of 36's
	// credit at all. Nothing could see it while a breakdown by kind failed on Firebird (-104); the day it
	// ran, the balance of 36 by counterparty came back as one empty row and six rows of revenue
	// (2026-09-15).
	const ibQueryHierarchyScope scopeAccount = ScopeFromAccountCondition(movements, GetRegisterAccount()->GetQueryColumn(),   accountDr,
		shape->ResolveColumnByName(PublishedAccountName(this, ibAcctShape::Balance)));
	const ibQueryHierarchyScope scopeCorr    = ScopeFromAccountCondition(movements, GetRegisterAccountCr()->GetQueryColumn(), accountCr);
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
		// that names a correspondent falls back to the movements, where both are present — in EITHER pass,
		// because the correspondent is the opposite account of both.
		//
		// This is a routing decision, not a limitation quietly admitted: answering it from the totals
		// would mean ignoring the filter and returning a plausible, wrong number.
		const ibQueryPredicatePtr oppositeFilter = accountCr;
		const bool useTotals = HasMaterializedViews() && (!IsCorrespondence() || oppositeFilter == nullptr);
		const ibBackendQueryable* source = useTotals ? GetTurnoverViewQueryable(pass.m_creditSide) : movements;
		if (source == nullptr)
			source = movements;

		const ibBackendQueryColumn* accountCol = ColumnOn(source, pass.m_account);
		// This pass is about THE account, on its own side — so it reports under the account's scope.
		const ibQueryHierarchyScope& scope = scopeAccount;
		// The OTHER account of this pass's movements — the correspondent's column. None in a one-sided register.
		const ibValueMetaObjectAttributeBase* opposite = !IsCorrespondence() ? nullptr
			: (pass.m_creditSide ? GetRegisterAccount() : GetRegisterAccountCr());
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
			WherePeriodAtMost(b, periodCol, ColumnOn(source, GetRegisterRecorder()), bound);

		WhereActive(b, this, source, /*onMovements*/ source == movements);
		WhereSideNamed(b, source, pass.m_account, /*onMovements*/ source == movements);

		// "The balance of 51" on this pass's own account column; "…in correspondence with 62" on the other.
		WhereAccount(b, accountCol, scopeAccount);
		if (opposite != nullptr)
			WhereAccount(b, ColumnOn(source, opposite), scopeCorr);
		WhereCondition(b, this, source, ibAcctShape::Balance, pass.m_creditSide, kindsDr, kindsCr, filter);
		// …and the breakdown half of the same condition, asked of THIS pass's slots.
		if (const ibQueryPredicatePtr slots = AccountDimensionCondition(this, source, pass.m_creditSide, condition))
			b.Where(slots);

		b.GroupBy(accountCol);

		std::vector<ibAcctBreakdownColumn> breakdown;
		// EACH PASS BREAKS DOWN ITS OWN SIDE, by the kinds asked of THE account: its debit slots in the debit
		// pass, its credit slots in the credit pass. It is one account on two sides, so it is one list.
		AddBreakdown(b, this, source, ibAcctShape::Balance, pass.m_creditSide, kindsDr, /*group*/ true, breakdown);

		// …AND ITS OWN SIDE'S DIMENSIONS with it: a non-balance dimension holds a value per side, so the
		// credit pass reads the credit half — on the movements because that is where the credit value
		// is, and on the stored surface because that is the column its rows are keyed by.
		//
		// ⭐ READ BY THE SIDE'S COLUMN, REPORTED UNDER THE DIMENSION — the same split the ACCOUNT makes
		// two lines below, where the credit pass reads `AccountCr` and the key still says `Account`.
		// The two passes fill ONE result, so a pass naming its own halves would publish a second column
		// for what the reading calls one field.
		std::vector<const ibBackendQueryColumn*> dimensions;
		std::vector<wxString> dimensionNames;
		for (const auto dimension : GetDimensionArrayObject())
			if (const ibBackendQueryColumn* here = ColumnOn(source, GetRegisterDimension(pass.m_creditSide, dimension))) {
				b.GroupBy(here);
				dimensions.push_back(here);
				dimensionNames.push_back(dimension->GetName());
			}

		// ⭐ THE FIGURE, ON WHICHEVER SURFACE. A stored total already holds the side apart
		// (`<Res>TurnoverDr` / `TurnoverCr` — that is what the trigger accumulated); the movements hold
		// the raw resource and the side has to be picked out of the row. Same sum, two spellings of what
		// is summed, and nothing above this lambda knows which one it got.
		const auto figureExpr = [&](const ibValueMetaObjectResource* resource, bool credit) -> ibQueryColumnExprPtr {
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
			// EVERY figure folds up to a moment, a split one by its own halves — "how much is on hand
			// in pieces" is the same question as "how much in money", asked of a figure that happens to
			// be kept per side. SideFigure reads the side's half; nothing else here changes.
			if (resource == nullptr)
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
		keyColumns.push_back(PublishedAccountName(this, ibAcctShape::Balance));
		AppendBreakdownNames(breakdown, keyColumns);
		for (const wxString& dimensionName : dimensionNames)
			keyColumns.push_back(dimensionName);

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
	// the type the account declares about itself. An active-passive one folds only on a row that stands on
	// one set of its analytics (AtFullAnalytics) — across its analytics it keeps both sides, which is what
	// it exists for.
	ibAcctTypeCache accountTypes;
	const ibAcctSummaryMap kindsByAccount       = KindsByAccount(GetChartOfAccounts(), /*onlySummary*/ false);
	const ibAcctSummaryMap summaryOnlyByAccount = KindsByAccount(GetChartOfAccounts(), /*onlySummary*/ true);
	for (auto& entry : rows) {
		const ibValue account = entry.second.m_key.empty() ? ibValue() : entry.second.m_key.front();
		const int accountType = AccountTypeOf(account, accountTypes);
		const bool oneSet = AtFullAnalytics(account, kindsDr, kindsByAccount, summaryOnlyByAccount);
		for (const auto resource : GetResourceArrayObject()) {
			// ⚠ ONLY WHERE ONE WAS COMPUTED — `m_figures[name]` would CREATE the pair rather than find
			// it, and a figure this pass did not produce would acquire a `BalanceDr` of zero. Absent
			// and zero are different statements; this is the indexing that turns one into the other.
			if (resource == nullptr)
				continue;
			const auto debit  = entry.second.m_figures.find(FigureName(resource, ibAcctFigure::BalanceDr));
			const auto credit = entry.second.m_figures.find(FigureName(resource, ibAcctFigure::BalanceCr));

			// ⭐ THE GROSS PAIR IS TAKEN BEFORE THE FOLD, which is the whole of what makes it gross —
			// the same two numbers, kept as they stood when each side was summed. Taken even where only
			// ONE side was produced (an account that was only ever debited): the missing half is a zero
			// of that side and not a reason to leave the reading blank.
			const ibValue grossDr = debit  != entry.second.m_figures.end() ? debit->second  : ibValue(ibNumber());
			const ibValue grossCr = credit != entry.second.m_figures.end() ? credit->second : ibValue(ibNumber());
			entry.second.m_figures[FigureName(resource, ibAcctFigure::GrossBalanceDr)] = grossDr;
			entry.second.m_figures[FigureName(resource, ibAcctFigure::GrossBalanceCr)] = grossCr;

			// …and the sideless one, read from the FOLDED pair — what is left, signed, on whichever side
			// it stands. Folded first, because that is the pair every other reading of this row shows.
			if (debit != entry.second.m_figures.end() && credit != entry.second.m_figures.end())
				FoldSideByAccountType(accountType, debit->second, credit->second, oneSet);

			const ibNumber foldedDr = debit  != entry.second.m_figures.end() ? debit->second.GetNumber()  : ibNumber();
			const ibNumber foldedCr = credit != entry.second.m_figures.end() ? credit->second.GetNumber() : ibNumber();
			entry.second.m_figures[FigureName(resource, ibRegFigure::Balance)] = ibValue(foldedDr - foldedCr);
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
	const ibQueryPredicatePtr& filter, const ibRegFold& fold, const ibValue& condition,
	bool byCorrespondent) const
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
	//
	// ⭐⭐ THE ACCOUNT CONDITION IS THE ACCOUNT, read by every pass on its OWN column; the correspondent on the
	// OPPOSITE one — see ComputeBalance, where the same mistake (the condition tied to the debit column)
	// turned the credit turnover of an account into its correspondents' (2026-09-15).
	const ibQueryHierarchyScope scopeAccount = ScopeFromAccountCondition(movements, GetRegisterAccount()->GetQueryColumn(),   accountDr,
		shape->ResolveColumnByName(PublishedAccountName(this, ibAcctShape::Turnovers)));
	const ibQueryHierarchyScope scopeCorr    = ScopeFromAccountCondition(movements, GetRegisterAccountCr()->GetQueryColumn(), accountCr,
		shape->ResolveColumnByName(CorrAccountColumnName()));

	// ⭐⭐ THE CORRESPONDENT IS STITCHED INTO THE ROW, NOT READ FROM A SECOND TABLE.
	//
	// Each pass groups by its own account AND by the account on the other side of the same postings — the
	// debit pass by the credit account, the credit pass by the debit one — with that side's breakdown and
	// that side's half of every field kept per side. Both passes write the key in one order (account,
	// breakdown, correspondent, its breakdown, dimensions, their other halves, period), so "62 against 51"
	// from the debit pass and "62 against 51" from the credit pass are ONE row with a debit turnover and a
	// credit turnover — the same stitching that makes an account one row without the correspondent.
	//
	// The correspondent's analytics are its slots AS THEY STAND — a turnover takes no list of kinds for them
	// (ibAcctArgs::For), so `kindsCr` arrives empty here. Only in correspondence: a one-sided line never names
	// the other account.
	byCorrespondent = byCorrespondent && IsCorrespondence() && GetRegisterAccountCr() != nullptr;

	for (const ibAcctPass& pass : PassesOf(this)) {
		if (pass.m_account == nullptr)
			continue;

		const size_t layoutIndex = layouts.size();

		// The same routing as the balance: the totals answer unless the question names a correspondent —
		// the OTHER account of the same movement, which a one-account-per-row table does not carry.
		//
		// ⚠ AND ONE MORE CASE BELONGS TO THE MOVEMENTS: a reading finer than the stored grain. Totals
		// are kept per DAY, so an hourly fold — or one per recorder, per line — cannot be derived from
		// them at all. Sending it to the totals anyway would answer at the wrong granularity, which is
		// a plausible wrong number rather than an error.
		const ibQueryPredicatePtr oppositeFilter = accountCr;
		const bool finerThanStored = (fold.IsCalendar() && fold.m_unit < GetTotalsPeriodUnit())
			|| fold.FromMovements() || fold.m_kind == ibRegGranularity::Period;
		// …and a row cut by the correspondent is the movements' too: a stored row has one account.
		const bool useTotals = HasMaterializedViews()
			&& (!IsCorrespondence() || oppositeFilter == nullptr)
			&& !ConditionNamesCorrespondent(this, ibAcctShape::Turnovers, filter)
			&& !finerThanStored
			&& !byCorrespondent;

		const ibBackendQueryable* source = useTotals ? GetTurnoverViewQueryable(pass.m_creditSide) : movements;
		if (source == nullptr)
			source = movements;

		const ibBackendQueryColumn* accountCol = ColumnOn(source, pass.m_account);
		// This pass is about THE account, on its own side — so it reports under the account's scope.
		const ibQueryHierarchyScope& scope = scopeAccount;
		// The OTHER account of this pass's movements — the correspondent's column. None in a one-sided register.
		const ibValueMetaObjectAttributeBase* opposite = !IsCorrespondence() ? nullptr
			: (pass.m_creditSide ? GetRegisterAccount() : GetRegisterAccountCr());
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
			WherePeriodRange(b, periodCol, ColumnOn(source, GetRegisterRecorder()), begin, end);

		WhereActive(b, this, source, /*onMovements*/ source == movements);
		WhereSideNamed(b, source, pass.m_account, /*onMovements*/ source == movements);

		WhereAccount(b, accountCol, scopeAccount);
		if (opposite != nullptr)
			WhereAccount(b, ColumnOn(source, opposite), scopeCorr);
		WhereCondition(b, this, source, ibAcctShape::Turnovers, pass.m_creditSide, kindsDr, kindsCr, filter);
		// …and the breakdown half of the same condition, asked of THIS pass's slots.
		if (const ibQueryPredicatePtr slots = AccountDimensionCondition(this, source, pass.m_creditSide, condition))
			b.Where(slots);

		b.GroupBy(accountCol);

		std::vector<ibAcctBreakdownColumn> breakdown;
		// Each pass breaks down its own side by the kinds asked of THE account (see ComputeBalance).
		AddBreakdown(b, this, source, ibAcctShape::Turnovers, pass.m_creditSide, kindsDr, /*group*/ true, breakdown);

		// …the correspondent on the other side of the same postings, with ITS breakdown by the kinds asked
		// of the correspondent (see the note above the loop).
		const ibBackendQueryColumn* corrAccountCol = byCorrespondent ? ColumnOn(source, opposite) : nullptr;
		std::vector<ibAcctBreakdownColumn> corrBreakdown;
		if (corrAccountCol != nullptr) {
			b.GroupBy(corrAccountCol);
			AddBreakdown(b, this, source, ibAcctShape::Turnovers, !pass.m_creditSide, kindsCr, /*group*/ true,
				corrBreakdown, /*corr*/ true);
		}

		// …and its own side's dimensions with it, read by the side's column and reported under the
		// dimension — see ComputeBalance. A dimension kept per side then gives its OTHER half to the
		// correspondent (`CurrencyCorr`).
		std::vector<const ibBackendQueryColumn*> dimensions;
		std::vector<wxString> dimensionNames;
		for (const auto dimension : GetDimensionArrayObject())
			if (const ibBackendQueryColumn* here = ColumnOn(source, GetRegisterDimension(pass.m_creditSide, dimension))) {
				b.GroupBy(here);
				dimensions.push_back(here);
				dimensionNames.push_back(dimension->GetName());
			}
		if (corrAccountCol != nullptr)
			for (const auto dimension : GetDimensionArrayObject())
				if (GetFieldSide(/*creditSide*/ true, dimension) != nullptr)
					if (const ibBackendQueryColumn* other = ColumnOn(source, GetRegisterDimension(!pass.m_creditSide, dimension))) {
						b.GroupBy(other);
						dimensions.push_back(other);
						dimensionNames.push_back(CorrFieldColumnName(dimension->GetName()));
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
		const auto figureExpr = [&](const ibValueMetaObjectResource* resource, bool credit) -> ibQueryColumnExprPtr {
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

				// ⭐ …AND WHAT THE CORRESPONDENT MOVED ON THE SAME POSTINGS, where the two sides do not agree on
				// the figure. Said from the correspondent's side, as its own turnover would be: the debit pass
				// (the correspondent on credit) sums the credit half into `CorrTurnoverCr`, the credit pass the
				// debit half into `CorrTurnoverDr`. Read on the movements only — a stored side keeps its own half.
				if (!useTotals && !resource->IsBalanceResource() && GetFieldSide(/*creditSide*/ true, resource) != nullptr) {
					const bool corrCredit = !pass.m_creditFigure;
					const wxString corrName = FigureName(resource,
						corrCredit ? ibAcctFigure::CorrTurnoverCr : ibAcctFigure::CorrTurnoverDr);
					b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, SideFigure(this, resource, corrCredit), corrName);
					figures.push_back({ corrName, resource });
				}
			}
		}

		// THIS PASS'S NAMES, IN THE ORDER THIS PASS BUILDS ITS KEY — and the two passes need not agree
		// on the breakdown, so the names go with the rows rather than with the reading.
		std::vector<wxString> keyColumns;
		keyColumns.push_back(PublishedAccountName(this, ibAcctShape::Turnovers));
		AppendBreakdownNames(breakdown, keyColumns);
		if (corrAccountCol != nullptr) {
			keyColumns.push_back(CorrAccountColumnName());
			AppendBreakdownNames(corrBreakdown, keyColumns);
		}
		for (const wxString& dimensionName : dimensionNames)
			keyColumns.push_back(dimensionName);
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
			// The correspondent likewise, under the account named in its own condition.
			if (corrAccountCol != nullptr) {
				key.push_back(scopeCorr.ReportedUnder(sel.GetValue(corrAccountCol)));
				AppendBreakdownValues(sel, corrBreakdown, key);
			}
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

	// The sideless readings, from the stitched pair: what moved on balance, the account's and the
	// correspondent's. The server road projects the same difference.
	for (auto& entry : rows) {
		std::map<wxString, ibValue>& figures = entry.second.m_figures;
		const auto net = [&figures](const wxString& debit, const wxString& credit, const wxString& into) {
			const auto dr = figures.find(debit);
			const auto cr = figures.find(credit);
			if (dr == figures.end() && cr == figures.end())
				return;
			const ibNumber drNumber = dr != figures.end() ? dr->second.GetNumber() : ibNumber();
			const ibNumber crNumber = cr != figures.end() ? cr->second.GetNumber() : ibNumber();
			figures[into] = ibValue(drNumber - crNumber);
		};
		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr)
				continue;
			net(FigureName(resource, ibAcctFigure::TurnoverDr), FigureName(resource, ibAcctFigure::TurnoverCr),
			    FigureName(resource, ibRegFigure::Turnover));
			net(FigureName(resource, ibAcctFigure::CorrTurnoverDr), FigureName(resource, ibAcctFigure::CorrTurnoverCr),
			    FigureName(resource, ibRegFigure::CorrTurnover));
		}
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
	const ibQueryHierarchyScope scopeDr = ScopeFromAccountCondition(movements, GetRegisterAccount()->GetQueryColumn(),   accountDr);
	const ibQueryHierarchyScope scopeCr = ScopeFromAccountCondition(movements, GetRegisterAccountCr()->GetQueryColumn(), accountCr);
	ibDataQueryBuilder b;
	b.From(movements);
	b.WithAccessPolicy(nullptr);

	WherePeriodRange(b, GetRegisterPeriod()->GetQueryColumn(), RecorderColumnOf(this), begin, end);
	WhereActive(b, this, movements, /*onMovements*/ true);
	WhereAccount(b, GetRegisterAccount()->GetQueryColumn(),   scopeDr);
	WhereAccount(b, GetRegisterAccountCr()->GetQueryColumn(), scopeCr);
	WhereCondition(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ false, kindsDr, kindsCr, filter);

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

	b.GroupBy(GetRegisterAccount()->GetQueryColumn());
	b.GroupBy(GetRegisterAccountCr()->GetQueryColumn());

	std::vector<ibAcctBreakdownColumn> breakdownDr, breakdownCr;
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ false, kindsDr, /*group*/ true, breakdownDr);
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ true,  kindsCr, /*group*/ true, breakdownCr);

	// A PAIRED ROW CARRIES BOTH HALVES OF A SPLIT FIELD, and both are reported: the currency given and
	// the currency taken are what make the pair what it is. A balanced one is a single value of the
	// line and appears once. (The reference publishes a debit Currency and a credit Currency here, against a
	// single Organization.)
	std::vector<const ibBackendQueryColumn*> dimensions;
	for (const auto dimension : GetDimensionArrayObject()) {
		if (dimension == nullptr)
			continue;
		for (const bool credit : { false, true }) {
			const ibBackendQueryColumn* column = GetRegisterDimension(credit, dimension);
			if (column == nullptr || std::find(dimensions.begin(), dimensions.end(), column) != dimensions.end())
				continue;   // a balanced dimension is one column on both sides — grouped once
			b.GroupBy(column);
			dimensions.push_back(column);
		}
	}

	// ONE figure per BALANCED resource: a pair of accounts has no sides of its own — what moved from
	// that credit to that debit is a single number, and calling it TurnoverDr would be the same value
	// twice. A SPLIT figure is two numbers even here, because what left is not what arrived.
	std::vector<wxString> figures;
	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		if (resource->IsBalanceResource()) {
			const wxString name = FigureName(resource, ibAcctFigure::Turnover);
			b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, ibQueryColumnExpr::Col(resource->GetQueryColumn()), name);
			figures.push_back(name);
			continue;
		}
		const wxString debitName  = FigureName(resource, ibAcctFigure::TurnoverDr);
		const wxString creditName = FigureName(resource, ibAcctFigure::TurnoverCr);
		b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, SideFigure(this, resource, /*credit*/ false), debitName);
		b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, SideFigure(this, resource, /*credit*/ true),  creditName);
		figures.push_back(debitName);
		figures.push_back(creditName);
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
		key.push_back(sel.GetValue(GetRegisterAccount()->GetQueryColumn()));
		key.push_back(sel.GetValue(GetRegisterAccountCr()->GetQueryColumn()));
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

	const ibQueryHierarchyScope scopeDr = ScopeFromAccountCondition(movements, GetRegisterAccount()->GetQueryColumn(),   accountDr);
	const ibQueryHierarchyScope scopeCr = ScopeFromAccountCondition(movements, GetRegisterAccountCr()->GetQueryColumn(), accountCr);

	ibDataQueryBuilder b;
	b.From(movements);
	// ⚠ NOT FILTERED BY THE CALLER'S RIGHTS — a total computed from the rows a particular user happens
	// to see is a wrong total, and a wrong total does not look like an error. It is also what makes
	// this composable at all: the door refuses to hand out a relation for a query carrying a policy.
	b.WithAccessPolicy(nullptr);

	WherePeriodRange(b, GetRegisterPeriod()->GetQueryColumn(), RecorderColumnOf(this), begin, end);
	WhereActive(b, this, movements, /*onMovements*/ true);
	WhereAccount(b, GetRegisterAccount()->GetQueryColumn(),   scopeDr);
	WhereAccount(b, GetRegisterAccountCr()->GetQueryColumn(), scopeCr);
	WhereCondition(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ false, kindsDr, kindsCr, filter);

	if (const ibQueryPredicatePtr slots = OrWith(
			AccountDimensionCondition(this, movements, /*creditSide*/ false, condition),
			AccountDimensionCondition(this, movements, /*creditSide*/ true,  condition)))
		b.Where(slots);

	b.GroupBy(GetRegisterAccount()->GetQueryColumn());
	b.GroupBy(GetRegisterAccountCr()->GetQueryColumn());

	std::vector<ibAcctBreakdownColumn> breakdownDr, breakdownCr;
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ false, kindsDr, /*group*/ true, breakdownDr);
	AddBreakdown(b, this, movements, ibAcctShape::DrCrTurnovers, /*creditSide*/ true,  kindsCr, /*group*/ true, breakdownCr);

	// ⭐ A ROW IS A PAIR, SO A SPLIT FIELD IS BOTH OF ITS COLUMNS — the currency given and the currency
	// taken, the quantity that left and the quantity that arrived. The shape publishes exactly that
	// (`bothSidesOnOneRow`), and a relation that grouped by the debit side alone and summed the debit
	// quantity as the pair's one turnover answered a different question under the right names.
	for (const auto dimension : GetDimensionArrayObject()) {
		if (dimension == nullptr)
			continue;
		const ibBackendQueryColumn* debit  = GetRegisterDimension(/*creditSide*/ false, dimension);
		const ibBackendQueryColumn* credit = GetRegisterDimension(/*creditSide*/ true, dimension);
		if (debit != nullptr)
			b.GroupBy(debit);
		if (credit != nullptr && credit != debit)
			b.GroupBy(credit);   // a balanced dimension is one column on both sides — grouped once
	}

	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		if (resource->IsBalanceResource()) {
			b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, ibQueryColumnExpr::Col(resource->GetQueryColumn()),
				FigureField(resource, ibAcctFigure::Turnover));
			continue;
		}
		b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum,
			ibQueryColumnExpr::Col(GetRegisterResource(/*creditSide*/ false, resource)),
			FigureField(resource, ibAcctFigure::TurnoverDr));
		b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum,
			ibQueryColumnExpr::Col(GetRegisterResource(/*creditSide*/ true, resource)),
			FigureField(resource, ibAcctFigure::TurnoverCr));
	}

	const ibQueryRelPtr folded = b.BuildRelation();
	const ibBackendQueryable* published = GetShapeQueryable(ibAcctShape::DrCrTurnovers, kindsDr, kindsCr, ibRegFold());
	if (folded == nullptr || published == nullptr)
		return folded;

	// ⭐⭐ THE RELATION ANSWERS UNDER THE NAMES THE SHAPE PUBLISHES — and a GROUP BY writes three kinds of
	// name that are not those. A key column comes out under its own fields, which is right for the
	// accounts and the dimensions and wrong for a SLOT, published as `AccountDimensionDr1`; a slot asked
	// for BY KIND and every sum come out under the statement's own spelling (ibSqlAliasOf — `out_…`).
	// Read by the published names, the sums came back as zero and a walk through the account refused
	// the statement (measured 2026-09-16 — this road had not been read through since the names moved).
	const wxString inner = wxT("drcr");
	std::unordered_map<wxString, wxString> labelOf;   // published field -> what the statement wrote
	std::vector<ibAcctServerKey> slots;
	for (const std::vector<ibAcctBreakdownColumn>* side : { &breakdownDr, &breakdownCr })
		for (const ibAcctBreakdownColumn& column : *side) {
			if (column.m_byKind) {
				if (const ibBackendQueryColumn* out = published->ResolveColumnByName(column.m_alias))
					for (const ibColumnSlot& slot : DescribeColumnLayout(out))
						labelOf[slot.m_name] = ibSqlAliasOf(slot.m_name);
				continue;
			}
			if (column.m_kindSlot != nullptr)
				PairByRole(slots, column.m_kindSlot, published->ResolveColumnByName(column.m_kindAlias));
			if (column.m_slot != nullptr)
				PairByRole(slots, column.m_slot, published->ResolveColumnByName(column.m_alias));
		}
	for (const ibAcctServerKey& key : slots)
		labelOf[key.m_published] = key.m_stored;
	for (const auto resource : GetResourceArrayObject())
		if (resource != nullptr)
			for (const wxString& figure : { wxString(ibAcctFigure::Turnover), wxString(ibAcctFigure::TurnoverDr), wxString(ibAcctFigure::TurnoverCr) })
				labelOf[FigureField(resource, figure)] = ibSqlAliasOf(FigureField(resource, figure));

	std::vector<ibQueryProjItem> projection;
	for (const ibBackendQueryColumn* column : published->GetColumns())
		for (const ibColumnSlot& slot : DescribeColumnLayout(column)) {
			const auto found = labelOf.find(slot.m_name);
			projection.push_back({ ibCol(inner, found != labelOf.end() ? found->second : slot.m_name), slot.m_name });
		}
	const ibQueryRelPtr renamed = ibProject(ibSubquery(folded, inner), std::move(projection));

	// …AND ONE PAIR IS ONE ROW, whichever way its empty values were stored — the note at KeyFieldsAsRead.
	// The GROUP BY above compared fields, so a pair of accounts whose currency is untagged on some lines
	// and a typed empty on others came back twice (measured 2026-09-16 against the lines themselves).
	// Each figure with the resource it is of and the side that judges it: the debit turnover by the debit
	// account, the credit one by the credit account, a pair's single turnover by either (ReportFiguresAsKept).
	struct ibPairFigure { const ibValueMetaObjectResource* m_resource; int m_side; };   // 0 either, 1 Dr, 2 Cr
	std::map<wxString, ibPairFigure> figureOf;
	std::set<wxString> figureFields;
	for (const auto resource : GetResourceArrayObject())
		if (resource != nullptr) {
			figureOf[FigureField(resource, ibAcctFigure::Turnover)]   = { resource, 0 };
			figureOf[FigureField(resource, ibAcctFigure::TurnoverDr)] = { resource, 1 };
			figureOf[FigureField(resource, ibAcctFigure::TurnoverCr)] = { resource, 2 };
			for (const wxString& figure : { wxString(ibAcctFigure::Turnover), wxString(ibAcctFigure::TurnoverDr), wxString(ibAcctFigure::TurnoverCr) })
				figureFields.insert(FigureField(resource, figure));
		}

	const wxString named = wxT("drcr_n");
	std::vector<ibQueryProjItem> outputs;
	std::vector<ibQueryExprPtr>  groupKeys;
	for (const ibBackendQueryColumn* column : published->GetColumns()) {
		const std::vector<ibColumnSlot> slots = DescribeColumnLayout(column);
		const bool figure = !slots.empty() && figureFields.count(slots.front().m_name) != 0;
		std::unordered_map<wxString, ibQueryExprPtr> asRead;
		if (!figure)
			asRead = KeyFieldsAsRead(column, GetMetaData(), named);
		for (const ibColumnSlot& slot : slots) {
			if (figure) {
				// A sum of nothing is zero — see ServerSideRead: a figure added after the first postings is
				// NULL on every line before it.
				outputs.push_back({ ibCast(ibFunc(wxT("COALESCE"), { ibFunc(wxT("SUM"), { ibCol(named, slot.m_name) }), ibRegTypedZero() }),
				                           ibTypeNumber(18, 6)), slot.m_name });
				continue;
			}
			const auto found = asRead.find(slot.m_name);
			const ibQueryExprPtr key = found != asRead.end() ? found->second : ibCol(named, slot.m_name);
			outputs.push_back({ key, slot.m_name });
			groupKeys.push_back(key);
		}
	}
	const ibQueryRelPtr summed = ibAggregate(ibSubquery(renamed, named), std::move(outputs), std::move(groupKeys));

	// …AND A FIGURE IS EMPTY WHERE ITS ACCOUNT KEEPS NO SUCH ACCOUNTING. A pair has two accounts, so the
	// chart is joined twice.
	const ibValueMetaObjectChartOfAccounts* chart = GetChartOfAccounts();
	const bool byAccount  = AnyFigureKeptByKind(this);
	const bool bySubconto = AnyFigureKeptByBreakdown(this);
	if ((!byAccount && !bySubconto) || chart == nullptr || chart->GetDataReference() == nullptr || chart->GetQueryable() == nullptr)
		return summed;

	const wxString pairs = wxT("drcr_k"), debitChart = wxT("drcr_ad"), creditChart = wxT("drcr_ac");
	const wxString chartTable = chart->GetQueryable()->GetQueryTableName();
	const ibBackendQueryColumn* chartRef = chart->GetDataReference()->GetQueryColumn();
	ibQueryRelPtr withCharts = ibSubquery(summed, pairs);
	if (byAccount)
		withCharts = ibJoin(
			ibJoin(withCharts, ibScan(chartTable, debitChart),
				ibRegSameValueIR(GetRegisterAccount()->GetQueryColumn(), pairs, chartRef, debitChart), ibQueryJoinType::Left),
			ibScan(chartTable, creditChart),
			ibRegSameValueIR(GetRegisterAccountCr()->GetQueryColumn(), pairs, chartRef, creditChart), ibQueryJoinType::Left);
	// …and each side's breakdown slots to ITS account's kinds rows: the debit slots belong to the debit account.
	std::vector<wxString> subcontosDr, subcontosCr;
	if (bySubconto) {
		withCharts = JoinSubcontoKinds(this, withCharts, GetRegisterAccount()->GetQueryColumn(), pairs,
			SlotKindsOf(this, published, ibAcctShape::DrCrTurnovers, /*creditSide*/ false, kindsDr), wxT("drcr_sd"), subcontosDr);
		withCharts = JoinSubcontoKinds(this, withCharts, GetRegisterAccountCr()->GetQueryColumn(), pairs,
			SlotKindsOf(this, published, ibAcctShape::DrCrTurnovers, /*creditSide*/ true, kindsCr), wxT("drcr_sc"), subcontosCr);
	}

	std::vector<ibQueryProjItem> judged;
	for (const ibBackendQueryColumn* column : published->GetColumns())
		for (const ibColumnSlot& slot : DescribeColumnLayout(column)) {
			const ibQueryExprPtr value = ibCol(pairs, slot.m_name);
			const auto figure = figureOf.find(slot.m_name);
			if (figure == figureOf.end()) {
				judged.push_back({ value, slot.m_name });
				continue;
			}
			const ibQueryExprPtr byDebit  = BothKept(byAccount ? KindKeptOnServer(this, figure->second.m_resource, debitChart) : nullptr,
				KeptBySubcontoOnServer(this, figure->second.m_resource, subcontosDr));
			const ibQueryExprPtr byCredit = BothKept(byAccount ? KindKeptOnServer(this, figure->second.m_resource, creditChart) : nullptr,
				KeptBySubcontoOnServer(this, figure->second.m_resource, subcontosCr));
			const ibQueryExprPtr kept = figure->second.m_side == 1 ? byDebit
				: figure->second.m_side == 2 ? byCredit
				: (byDebit && byCredit ? ibBinOp(ibQueryBinOp::Or, byDebit, byCredit) : nullptr);
			judged.push_back({ FigureWhereKept(kept, value), slot.m_name });
		}
	return ibProject(withCharts, std::move(judged));
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
	const ibQueryPredicatePtr& filter, const ibRegFold& fold, const ibValue& condition) const
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
		keyColumns.push_back(PublishedAccountName(this, ibAcctShape::BalanceAndTurnovers));

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
	const ibAcctSummaryMap summaryOnlyByAccount = KindsByAccount(GetChartOfAccounts(), /*onlySummary*/ true);
	const auto standsOnTurnoversOnly = [&](const ibQueryRamTable& table, long row) {
		static const ibAcctKindSet s_none;
		const ibValue account = GetRegisterAccount() != nullptr
			? cellByName(table, row, PublishedAccountName(this, ibAcctShape::BalanceAndTurnovers)) : ibValue();
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

			// EVERY figure has a balance — a split one keeps it per side, which is what these columns
			// are (§ the shape). What still has none is a TURNOVERS-ONLY breakdown: nothing is carried
			// along it by declaration, and a zero there would say a balance was kept and came to
			// nothing.
			// ⚠ THE OPENING IS CARRIED IN GROSS. The balance reading has already folded its pair by the account's
			// type; taken folded, a receivable and a payable of one row entered here as their difference, the
			// closing added the turnovers to that, and the gross pair reported below was a net one (the RAM road's
			// `OpeningGrossBalanceDr` of 36 was 1140 where 31140 stood — measured 2026-09-17). The fold is
			// applied once, at the end, to the pair as it stands.
			ibValue openDr, openCr;
			const auto found = openingByKey.find(balanceKey);
			if (!turnoversOnlyRow && found != openingByKey.end()) {
				openDr = cellByName(opening, found->second, FigureName(resource, ibAcctFigure::GrossBalanceDr));
				openCr = cellByName(opening, found->second, FigureName(resource, ibAcctFigure::GrossBalanceCr));
			}

			// The turnover part is always reported; the balance columns are left EMPTY along a
			// turnovers-only breakdown — no balance is kept there, and a zero would say one was kept
			// and came to nothing.
			out.m_figures[FigureName(resource, ibAcctFigure::TurnoverDr)] = turnDr;
			out.m_figures[FigureName(resource, ibAcctFigure::TurnoverCr)] = turnCr;
			if (turnoversOnlyRow)
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
	//
	// ⭐ AND WITH A CALENDAR PERIODICITY IT STANDS IN THE FIRST PERIOD OF THE INTERVAL — the period the
	// balance is carried into. Built from the opening, which has no period, it used to come out with an
	// empty one (measured 2026-09-16: fixed assets on 10 read by month from August).
	const bool everyPeriod = withPeriod && fold.m_kind == ibRegGranularity::Calendar && !fold.FromMovements()
		&& begin.m_date.GetType() == TYPE_DATE;
	const wxDateTime firstPeriod = everyPeriod ? ibTruncateToPeriod(begin.m_date.GetDateTime(), fold.m_unit) : wxDateTime();
	for (const auto& entry : openingByKey) {
		if (byKey.find(entry.first) != byKey.end())
			continue;

		ibAcctKey identity = entry.first;
		std::vector<ibValue> key = keyValues(opening, entry.second);
		if (everyPeriod) {
			identity.push_back(ibValue(firstPeriod));
			if (keyColumns.size() < key.size())
				key[keyColumns.size()] = ibValue(firstPeriod);
		}

		const size_t before = rows.size();
		ibAcctRow& out = rowFor(identity, key);
		if (rows.size() != before) {
			byKey[entry.first].push_back(rows.size() - 1);
			balanceless.push_back(false);
		}
		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr)
				continue;
			const ibValue openDr = cellByName(opening, entry.second, FigureName(resource, ibAcctFigure::GrossBalanceDr));   // gross — see above
			const ibValue openCr = cellByName(opening, entry.second, FigureName(resource, ibAcctFigure::GrossBalanceCr));
			out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceDr)] = openDr;
			out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceCr)] = openCr;
			out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceDr)] = openDr;
			out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceCr)] = openCr;
		}
	}

	// ⭐⭐ EVERY PERIOD OF THE INTERVAL, FOR EVERY KEY — and the pruning at the end decides which of them is a
	// row (Max, 2026-09-16: "if a month has no turnover but has balances, you must show them with zero
	// turnovers — in effect nothing changed; if there are no balances and no turnovers, there is nothing to output").
	//
	// A key that stood still through March has no March row in what was read, and a report then shows
	// February and April side by side as though nothing existed in between — wrong in the one way this
	// table exists to prevent, because what a balance says about an empty period is precisely that it did
	// not change. So the missing periods are added with zero turnover and no balance of their own; the
	// roll below carries the previous closing into each, and a period whose balance and turnover are all
	// zero is dropped by the pruning after it — a closed account reports nothing.
	//
	// ⚠ ADDED BEFORE THE ORDERING, NOT AFTER IT. The roll walks the rows of a key in the order they are
	// poured; rows appended to the table after the pour were walked last whatever their period, and a
	// March row filled after April carried April's closing. That is how the boundaries option used to do
	// it, and it is the reason this is the one place the periods are filled now.
	// Only between two ends: with no upper one there is no last period to stop at, and the loop below would
	// walk the calendar for ever — the server road does not invent periods there either.
	// ⭐⭐ THE BALANCE OF A TURNOVERS-ONLY BREAKDOWN STILL MOVES. Its turnovers are reported by the subconto and its
	// balance without it — and the balance row CLOSES ON THOSE TURNOVERS, or the reading says the account ended
	// where it began: 36 kept by counterparty "turnovers only" read 29 580 at the end of September while its balance
	// was 44 880 (measured 2026-09-17). So each such row lends its turnovers to the balance row of its key with the
	// subconto left out — built here if nothing else made it — for the closing (and the roll through the periods)
	// to count; they are taken back after the roll, and the balance row reports turnovers of its own only (Max: the
	// report would count them twice otherwise).
	const auto summaryFolded = [&](const ibQueryRamTable& table, long row, const std::vector<wxString>& names) {
		std::vector<ibValue> values = identityValuesOf(table, row, names);
		const ibValue account = GetRegisterAccount() != nullptr
			? cellByName(table, row, PublishedAccountName(this, ibAcctShape::BalanceAndTurnovers)) : ibValue();
		const auto foundKinds = summaryOnlyByAccount.find(account);
		if (foundKinds == summaryOnlyByAccount.end())
			return values;
		for (const ibAcctBreakdownColumn& column : layout) {
			const ibValue kind = column.m_byKind
				? column.m_requestedKind
				: (column.m_kindAlias.IsEmpty() ? ibValue() : cellByName(table, row, column.m_kindAlias));
			if (kind.IsEmpty() || foundKinds->second.find(kind) == foundKinds->second.end())
				continue;
			for (size_t i = 0; i < names.size(); ++i)
				if (names[i] == column.m_alias || (!column.m_kindAlias.IsEmpty() && names[i] == column.m_kindAlias))
					values[i] = ibValue();
		}
		return values;
	};
	std::unordered_map<size_t, std::map<wxString, ibNumber>> borrowedOf;   // row -> figure -> lent turnover
	for (long row = 0; row < turnover.RowCount(); row++) {
		if (!standsOnTurnoversOnly(turnover, row))
			continue;
		const ibAcctKey identity   = summaryFolded(turnover, row, rowColumns);
		const ibAcctKey balanceKey = summaryFolded(turnover, row, keyColumns);

		const size_t before = rows.size();
		rowFor(identity, identity);
		const size_t at = index[identity];
		ibAcctRow& out = rows[at].second;
		if (rows.size() != before) {
			byKey[balanceKey].push_back(at);
			balanceless.push_back(false);
			// Nothing moved on this key by itself and, read whole, nothing was carried in either: the opening is
			// its key's (in periods the roll seeds it), its own turnovers nothing.
			const auto found = openingByKey.find(balanceKey);
			for (const auto resource : GetResourceArrayObject()) {
				if (resource == nullptr)
					continue;
				const ibValue openDr = !withPeriod && found != openingByKey.end()
					? cellByName(opening, found->second, FigureName(resource, ibAcctFigure::GrossBalanceDr)) : ibValue(ibNumber());
				const ibValue openCr = !withPeriod && found != openingByKey.end()
					? cellByName(opening, found->second, FigureName(resource, ibAcctFigure::GrossBalanceCr)) : ibValue(ibNumber());
				out.m_figures[FigureName(resource, ibAcctFigure::TurnoverDr)] = ibValue(ibNumber());
				out.m_figures[FigureName(resource, ibAcctFigure::TurnoverCr)] = ibValue(ibNumber());
				out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceDr)] = openDr;
				out.m_figures[FigureName(resource, ibAcctFigure::OpeningBalanceCr)] = openCr;
				out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceDr)] = openDr;
				out.m_figures[FigureName(resource, ibAcctFigure::ClosingBalanceCr)] = openCr;
			}
		}
		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr)
				continue;
			for (const wxString& side : { wxString(ibAcctFigure::TurnoverDr), wxString(ibAcctFigure::TurnoverCr) }) {
				const wxString name = FigureName(resource, side);
				const ibNumber lent = cellByName(turnover, row, name).GetNumber();
				borrowedOf[at][name] = borrowedOf[at][name] + lent;
				ibValue& cell = out.m_figures[name];
				cell = ibValue(cell.GetNumber() + lent);
				if (!withPeriod) {
					const wxString closing = FigureName(resource, side == ibAcctFigure::TurnoverDr
						? ibAcctFigure::ClosingBalanceDr : ibAcctFigure::ClosingBalanceCr);
					ibValue& closeCell = out.m_figures[closing];
					closeCell = ibValue(closeCell.GetNumber() + lent);
				}
			}
		}
	}

	if (everyPeriod && firstPeriod.IsValid() && end.m_date.GetType() == TYPE_DATE) {
		const std::vector<wxDateTime> calendar = ibRegCalendarOf(ibValue(firstPeriod), end.m_date, fold.m_unit, /*maxPeriods*/ 0);
		const size_t slot = keyColumns.size();
		for (auto& group : byKey) {
			if (group.second.empty())
				continue;
			std::unordered_set<ibValue, ibValueHash, ibValueEqual> present;
			for (const size_t member : group.second)
				if (slot < rows[member].second.m_key.size())
					present.insert(rows[member].second.m_key[slot]);

			const size_t templateRow = group.second.front();
			const bool templateBalanceless = balanceless[templateRow];
			for (const wxDateTime& period : calendar) {
				const ibValue value(period);
				if (present.find(value) == present.end()) {
					ibAcctRow added = rows[templateRow].second;   // the key as it stands on a row that exists
					added.m_figures.clear();
					if (slot < added.m_key.size())
						added.m_key[slot] = value;
					ibAcctKey identity = group.first;
					identity.push_back(value);
					group.second.push_back(rows.size());
					rows.push_back({ identity, std::move(added) });
					balanceless.push_back(templateBalanceless);
				}
			}
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
	std::vector<const std::map<wxString, ibNumber>*> orderedBorrowed;   // the turnovers a balance row was lent (above)
	ordered.reserve(rows.size());
	orderedBalanceless.reserve(rows.size());
	for (auto& group : byKey) {
		if (withPeriod)
			std::stable_sort(group.second.begin(), group.second.end(),
				[&](size_t a, size_t b) { return periodOf(rows[a].second) < periodOf(rows[b].second); });
		for (const size_t member : group.second) {
			ordered.push_back(std::move(rows[member]));
			orderedBalanceless.push_back(balanceless[member]);
			const auto lent = borrowedOf.find(member);
			orderedBorrowed.push_back(lent != borrowedOf.end() ? &lent->second : nullptr);
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
			if (resource == nullptr)
				continue;
			ibBalanceFoldSlot debit;
			debit.m_receipt  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::TurnoverDr));
			debit.m_turnover = debit.m_receipt;   // rewritten with receipt - expense, i.e. with itself
			debit.m_opening  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::OpeningBalanceDr));
			debit.m_closing  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::ClosingBalanceDr));

			ibBalanceFoldSlot credit;
			credit.m_receipt  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::TurnoverCr));
			credit.m_turnover = credit.m_receipt;
			credit.m_opening  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::OpeningBalanceCr));
			credit.m_closing  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::ClosingBalanceCr));

			slots.push_back(debit);
			slots.push_back(credit);

			// The seed is keyed by the slot the fold carries its running total under — its TURNOVER
			// column, which is what FoldBalancesForward reads back. Keyed by anything else, the seed is
			// never found and every key appears to start at zero: correct turnovers on top of balances
			// that all begin at nothing.
			for (long row = 0; row < opening.RowCount(); row++) {
				std::map<ibMetaID, ibNumber>& seed = openingSeed[identityValuesOf(opening, row, keyColumns)];
				seed[debit.m_turnover]  = cellByName(opening, row, FigureName(resource, ibAcctFigure::GrossBalanceDr)).GetNumber();   // gross, folded once at the end
				seed[credit.m_turnover] = cellByName(opening, row, FigureName(resource, ibAcctFigure::GrossBalanceCr)).GetNumber();
			}
		}

		std::vector<ibMetaID> foldKey;
		for (const wxString& name : keyColumns)
			foldKey.push_back(retTable.ColumnIdByName(name));

		const ibMetaID periodId = retTable.ColumnIdByName(periodName);

		// The periods nothing moved in are already among the rows — filled before the ordering (see
		// `everyPeriod` above), whatever fill method the call named: which of them is a row is decided by
		// the pruning at the end, and the answer is the same for both methods (ibAcctParseCall).

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
	// The lent turnovers go back: the closing (and every period's roll) has counted them, and the balance row of a
	// turnovers-only breakdown reports turnovers of its own only.
	for (size_t row = 0; row < orderedBorrowed.size(); ++row) {
		if (orderedBorrowed[row] == nullptr)
			continue;
		for (const auto& lent : *orderedBorrowed[row])
			if (const ibMetaID id = retTable.ColumnIdByName(lent.first))
				retTable.SetCell(static_cast<long>(row), id, ibValue(retTable.GetCell(static_cast<long>(row), id).GetNumber() - lent.second));
	}

	ibAcctTypeCache accountTypes;
	const wxString accountName = PublishedAccountName(this, ibAcctShape::BalanceAndTurnovers);
	// An active-passive balance folds only on a row that stands on one set of its analytics (AtFullAnalytics).
	const ibAcctSummaryMap kindsByAccount = KindsByAccount(GetChartOfAccounts(), /*onlySummary*/ false);
	for (long row = 0; row < retTable.RowCount(); row++) {
		const bool keepsBalanceHere = static_cast<size_t>(row) >= orderedBalanceless.size()
			|| !orderedBalanceless[static_cast<size_t>(row)];
		const ibValue account = cellByName(retTable, row, accountName);
		const int accountType = AccountTypeOf(account, accountTypes);
		const bool oneSet = AtFullAnalytics(account, kindsDr, kindsByAccount, summaryOnlyByAccount);

		for (const auto resource : GetResourceArrayObject()) {
			if (resource == nullptr)
				continue;

			// ⭐ ONE PASS FILLS ALL THREE READINGS of a moment: the GROSS pair as the numbers stand,
			// the folded pair, and the sideless net taken from the folded one. A row that keeps no
			// balance here (a turnovers-only breakdown) empties them all rather than showing zeros.
			const auto foldPair = [&](const wxString& debitSuffix, const wxString& creditSuffix,
				const wxString& grossDebitSuffix, const wxString& grossCreditSuffix, const wxString& netFigure) {
				const ibMetaID debitId  = retTable.ColumnIdByName(FigureName(resource, debitSuffix));
				const ibMetaID creditId = retTable.ColumnIdByName(FigureName(resource, creditSuffix));
				const ibMetaID grossDebitId  = retTable.ColumnIdByName(FigureName(resource, grossDebitSuffix));
				const ibMetaID grossCreditId = retTable.ColumnIdByName(FigureName(resource, grossCreditSuffix));
				const ibMetaID netId = retTable.ColumnIdByName(FigureName(resource, netFigure));
				if (debitId == 0 || creditId == 0)
					return;
				if (!keepsBalanceHere) {
					retTable.SetCell(row, debitId,  ibValue());
					retTable.SetCell(row, creditId, ibValue());
					if (grossDebitId  != 0) retTable.SetCell(row, grossDebitId,  ibValue());
					if (grossCreditId != 0) retTable.SetCell(row, grossCreditId, ibValue());
					if (netId != 0) retTable.SetCell(row, netId, ibValue());
					return;
				}
				ibValue debit  = retTable.GetCell(row, debitId);
				ibValue credit = retTable.GetCell(row, creditId);
				if (grossDebitId  != 0) retTable.SetCell(row, grossDebitId,  debit);
				if (grossCreditId != 0) retTable.SetCell(row, grossCreditId, credit);
				FoldSideByAccountType(accountType, debit, credit, oneSet);
				retTable.SetCell(row, debitId,  debit);
				retTable.SetCell(row, creditId, credit);
				if (netId != 0)
					retTable.SetCell(row, netId, ibValue(debit.GetNumber() - credit.GetNumber()));
			};

			foldPair(ibAcctFigure::OpeningBalanceDr, ibAcctFigure::OpeningBalanceCr,
				ibAcctFigure::OpeningGrossBalanceDr, ibAcctFigure::OpeningGrossBalanceCr, ibRegFigure::OpeningBalance);
			foldPair(ibAcctFigure::ClosingBalanceDr, ibAcctFigure::ClosingBalanceCr,
				ibAcctFigure::ClosingGrossBalanceDr, ibAcctFigure::ClosingGrossBalanceCr, ibRegFigure::ClosingBalance);

			// The turnover has no fold and no gross form — it is what moved, per side — but it does
			// have a sideless reading: what moved on balance.
			const ibMetaID turnDrId  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::TurnoverDr));
			const ibMetaID turnCrId  = retTable.ColumnIdByName(FigureName(resource, ibAcctFigure::TurnoverCr));
			const ibMetaID turnNetId = retTable.ColumnIdByName(FigureName(resource, ibRegFigure::Turnover));
			if (turnDrId != 0 && turnCrId != 0 && turnNetId != 0)
				retTable.SetCell(row, turnNetId, ibValue(
					retTable.GetCell(row, turnDrId).GetNumber() - retTable.GetCell(row, turnCrId).GetNumber()));
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
			if (const ibMetaID id = retTable.ColumnIdByName(FigureName(resource, suffix)))
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

// ⭐⭐ ZERO AND EMPTY ARE TWO ANSWERS (Max, 2026-09-16). A figure the row's account KEEPS is a number,
// and a zero is a legitimate one — a month with a balance and no movement, a turnover on one side and
// nothing on the other. A figure the account keeps NO accounting for — a quantity on a supplier account, a
// currency amount on a goods one — has no place on that row, and says so by being EMPTY. The server road
// says the same with a CASE over the chart's flag (KindKeptOnServer); this is the RAM road's pass, asked of the
// account through the same question a write empties a figure by.
//
// Judged per ROW by the row's account — and on a PAIR of accounts by the side the figure is of: the debit
// turnover by the debit account, the credit one by the credit account, a pair's single turnover by either.
void ibValueMetaObjectAccountingRegister::ReportFiguresAsKept(ibQueryRamTable& table, ibAcctShape shape,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr) const
{
	if (shape == ibAcctShape::Records || GetRegisterAccount() == nullptr)
		return;   // a movement line is the resource itself, emptied at write already

	const bool paired = PairedRow(this, shape);
	const ibMetaID accountId   = table.ColumnIdByName(PublishedAccountName(this, shape));
	const ibMetaID accountCrId = paired && GetRegisterAccountCr() != nullptr
		? table.ColumnIdByName(GetRegisterAccountCr()->GetName()) : 0;
	if (accountId == 0)
		return;
	// The correspondent of a turnover row — its figures are judged by IT, not by the row's account.
	const ibMetaID corrAccountId = shape == ibAcctShape::Turnovers ? table.ColumnIdByName(CorrAccountColumnName()) : 0;

	// `m_byBreakdown` — the tick of the kinds table this figure is kept BY (a breakdown's accounting kind), or null.
	struct ibFigureCell { ibMetaID m_id; ibMetaID m_kind; ibAcctJudgedBy m_by; bool m_balance; size_t m_resource;
	                      const ibValueMetaObjectAttributeBase* m_byBreakdown; };
	std::vector<ibFigureCell> cells;
	size_t resourceNo = 0;
	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const ibMetaDescription& kind = resource->GetAccountingKind();
		const ibMetaID kindId = kind.IsOk() ? kind.GetByIdx(0) : 0;
		const ibValueMetaObjectAttributeBase* byBreakdown = BreakdownKindOf(this, resource);
		const size_t thisResource = resourceNo++;
		const auto add = [&](const wxString& suffix, ibAcctJudgedBy by, bool balance) {
			if (const ibMetaID id = table.ColumnIdByName(FigureName(resource, suffix)))
				cells.push_back({ id, kindId, by, balance, thisResource, byBreakdown });
		};
		add(ibAcctFigure::TurnoverDr, ibAcctJudgedBy::Account, false);
		add(ibAcctFigure::TurnoverCr, paired ? ibAcctJudgedBy::CreditAccount : ibAcctJudgedBy::Account, false);
		add(ibRegFigure::Turnover,    paired ? ibAcctJudgedBy::EitherAccount : ibAcctJudgedBy::Account, false);
		add(ibAcctFigure::CorrTurnoverDr, ibAcctJudgedBy::CorrAccount, false);
		add(ibAcctFigure::CorrTurnoverCr, ibAcctJudgedBy::CorrAccount, false);
		add(ibRegFigure::CorrTurnover,    ibAcctJudgedBy::CorrAccount, false);
		for (const wxString& suffix : { wxString(ibAcctFigure::BalanceDr), wxString(ibAcctFigure::BalanceCr), wxString(ibRegFigure::Balance),
		                                wxString(ibAcctFigure::GrossBalanceDr), wxString(ibAcctFigure::GrossBalanceCr),
		                                wxString(ibAcctFigure::OpeningBalanceDr), wxString(ibAcctFigure::OpeningBalanceCr), wxString(ibRegFigure::OpeningBalance),
		                                wxString(ibAcctFigure::OpeningGrossBalanceDr), wxString(ibAcctFigure::OpeningGrossBalanceCr),
		                                wxString(ibAcctFigure::ClosingBalanceDr), wxString(ibAcctFigure::ClosingBalanceCr), wxString(ibRegFigure::ClosingBalance),
		                                wxString(ibAcctFigure::ClosingGrossBalanceDr), wxString(ibAcctFigure::ClosingGrossBalanceCr) })
			add(suffix, ibAcctJudgedBy::Account, true);
	}
	if (cells.empty())
		return;

	std::map<ibMetaID, std::unordered_map<ibValue, bool, ibValueHash, ibValueEqual>> memory;
	const auto keeps = [&](const ibValue& account, ibMetaID kind) {
		return kind == 0 || IsAccountingKindKept(account, kind, memory[kind]);
	};

	// ⭐⭐ KEPT BY A SUBCONTO — the second half of a kind of accounting. The account says it keeps quantity; each
	// row of its kinds table says whether the quantity is kept BY that subconto. A row of this reading broken
	// down by a subconto of the account that is not ticked has no quantity to report — it is EMPTY, however many
	// pieces its movements carry — while the same figure by the ticked subconto alone, or by the account whole,
	// is there. A kind the account does not keep at all breaks nothing down on its row and is not asked about.
	// Which kinds a row is broken down by: the call's list, or the kind each slot stands in on that row.
	struct ibBreakdownCell { ibMetaID m_kindId; ibValue m_requested; };
	const auto breakdownOf = [&](bool creditSide, const std::vector<ibValue>& kinds) {
		std::vector<ibAcctBreakdownColumn> layout;
		DescribeBreakdown(this, shape, creditSide, kinds, layout);
		std::vector<ibBreakdownCell> out;
		for (const ibAcctBreakdownColumn& column : layout)
			out.push_back({ column.m_kindAlias.IsEmpty() ? 0 : table.ColumnIdByName(column.m_kindAlias), column.m_requestedKind });
		return out;
	};
	const bool anyByBreakdown = std::any_of(cells.begin(), cells.end(), [](const ibFigureCell& c) { return c.m_byBreakdown != nullptr; });
	const std::vector<ibBreakdownCell> breakdownDr = anyByBreakdown ? breakdownOf(false, kindsDr) : std::vector<ibBreakdownCell>();
	const std::vector<ibBreakdownCell> breakdownCr = anyByBreakdown && paired ? breakdownOf(true, kindsCr) : std::vector<ibBreakdownCell>();
	const ibAcctSummaryMap kindsOfAccount = anyByBreakdown ? KindsByAccount(GetChartOfAccounts(), static_cast<const ibValueMetaObjectAttributeBase*>(nullptr))
	                                                       : ibAcctSummaryMap();
	std::map<const ibValueMetaObjectAttributeBase*, ibAcctSummaryMap> tickedBy;
	for (const ibFigureCell& cell : cells)
		if (cell.m_byBreakdown != nullptr && tickedBy.find(cell.m_byBreakdown) == tickedBy.end())
			tickedBy[cell.m_byBreakdown] = KindsByAccount(GetChartOfAccounts(), cell.m_byBreakdown);

	const auto keptByBreakdown = [&](long row, const ibValue& account, const std::vector<ibBreakdownCell>& breakdown,
	                                 const ibValueMetaObjectAttributeBase* flag) {
		if (flag == nullptr)
			return true;
		const auto all = kindsOfAccount.find(account);
		if (all == kindsOfAccount.end())
			return true;   // the account keeps no analytics — nothing on its row is a subconto of it
		const ibAcctSummaryMap& ticked = tickedBy[flag];
		const auto mine = ticked.find(account);
		for (const ibBreakdownCell& slot : breakdown) {
			const ibValue kind = !slot.m_requested.IsEmpty() ? slot.m_requested
			                   : slot.m_kindId != 0 ? table.GetCell(row, slot.m_kindId) : ibValue();
			if (kind.IsEmpty() || all->second.find(kind) == all->second.end())
				continue;
			if (mine == ticked.end() || mine->second.find(kind) == mine->second.end())
				return false;
		}
		return true;
	};

	for (long row = 0; row < table.RowCount(); ++row) {
		const ibValue account   = table.GetCell(row, accountId);
		const ibValue accountCr = accountCrId != 0 ? table.GetCell(row, accountCrId) : ibValue();
		const ibValue corrAccount = corrAccountId != 0 ? table.GetCell(row, corrAccountId) : ibValue();

		// ⚠ A TURNOVERS-ONLY BREAKDOWN LEAVES ITS BALANCES EMPTY ON PURPOSE (ComputeBalanceAndTurnover): no
		// balance is kept along it, and a zero would say one was. Such a row is recognised by exactly that —
		// every balance cell empty — and its balances are left as they are.
		bool balancesSaid = false;
		for (const ibFigureCell& cell : cells)
			if (cell.m_balance && !table.GetCell(row, cell.m_id).IsEmpty())
				balancesSaid = true;
		const bool balancelessRow = shape == ibAcctShape::BalanceAndTurnovers && !balancesSaid;

		// ⭐ A FIGURE THAT IS THERE IS KEPT. A row reported UNDER an account (`IN HIERARCHY` — 632 under 63)
		// carries its subordinates' figures, and the named account may keep no such accounting itself; a
		// non-zero currency amount then says by itself that it has a place on this row.
		// (Not on a PAIR: there each side is judged by its own account, and the quantity that reached the
		// goods account says nothing about a place for one on the supplier's side.)
		std::vector<bool> moved(resourceNo, false);
		for (const ibFigureCell& cell : cells) {
			if (paired)
				break;
			if (cell.m_by == ibAcctJudgedBy::CorrAccount)
				continue;   // what the correspondent moved says nothing about a place on the account's side
			const ibValue value = table.GetCell(row, cell.m_id);
			if (value.GetType() == TYPE_NUMBER && !value.GetNumber().IsZero())
				moved[cell.m_resource] = true;
		}

		for (const ibFigureCell& cell : cells) {
			bool kept = moved[cell.m_resource];
			if (!kept) switch (cell.m_by) {
			case ibAcctJudgedBy::Account:       kept = keeps(account, cell.m_kind); break;
			case ibAcctJudgedBy::CreditAccount: kept = keeps(accountCr, cell.m_kind); break;
			case ibAcctJudgedBy::EitherAccount: kept = keeps(account, cell.m_kind) || keeps(accountCr, cell.m_kind); break;
			// Not cut by the correspondent, the row has none — the figure is every correspondent's, and kept.
			case ibAcctJudgedBy::CorrAccount:   kept = corrAccount.IsEmpty() || keeps(corrAccount, cell.m_kind); break;
			}
			// …and by the subconto the row is broken down by — asked even of a figure that moved: pieces summed
			// across a subconto the quantity is not kept by are not a quantity of that subconto.
			if (kept && cell.m_byBreakdown != nullptr) switch (cell.m_by) {
			case ibAcctJudgedBy::Account:       kept = keptByBreakdown(row, account, breakdownDr, cell.m_byBreakdown); break;
			case ibAcctJudgedBy::CreditAccount: kept = keptByBreakdown(row, accountCr, breakdownCr, cell.m_byBreakdown); break;
			case ibAcctJudgedBy::EitherAccount: kept = keptByBreakdown(row, account, breakdownDr, cell.m_byBreakdown)
			                                        || keptByBreakdown(row, accountCr, breakdownCr, cell.m_byBreakdown); break;
			case ibAcctJudgedBy::CorrAccount:   break;   // the correspondent's own breakdown is not this row's
			}
			if (!kept)
				table.SetCell(row, cell.m_id, ibValue());
			else if (!(cell.m_balance && balancelessRow) && table.GetCell(row, cell.m_id).GetType() != TYPE_NUMBER)
				table.SetCell(row, cell.m_id, ibValue(ibNumber()));
		}
	}
}

// The movement LINES themselves, with the dimension slots widened into a column per requested kind.
// Not a total and never will be: recorder and line number are precisely what a fold discards, so a
// table that reports them can only be the movements. It therefore needs no trigger, no bundle and no
// parity check — it is a projection of a table that is already queryable.
ibQueryRamTable ibValueMetaObjectAccountingRegister::ComputeRecords(
	const ibRegBound& begin, const ibRegBound& end,
	const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	const ibQueryPredicatePtr& filter, const ibValue& condition) const
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
	WherePeriodRange(b, GetRegisterPeriod()->GetQueryColumn(), RecorderColumnOf(this), begin, end);
	WhereCondition(b, this, movements, ibAcctShape::Records, /*creditSide*/ false, kindsDr, kindsCr, filter);

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
	// A field kept per side is carried as its two side attributes, each under its own id — which is also
	// how the shape published them.
	std::vector<const ibBackendQueryColumn*> straight;
	const auto carry = [this, &straight](const ibValueMetaObjectAttributeBase* attribute) {
		const ibValueMetaObjectAttributeBase* debit  = GetFieldOnSide(/*creditSide*/ false, attribute);
		const ibValueMetaObjectAttributeBase* credit = GetFieldOnSide(/*creditSide*/ true, attribute);
		if (debit != nullptr)
			straight.push_back(debit->GetQueryColumn());
		if (credit != nullptr && credit != debit)
			straight.push_back(credit->GetQueryColumn());
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
	// Each field of the line — and one kept per side carries two values.
	for (const auto dimension : GetDimensionArrayObject())
		carry(dimension);
	for (const auto resource : GetResourceArrayObject())
		carry(resource);

	ibDataQueryResult sel = b.Execute(ibReadPageRequest{});
	while (sel.Next()) {
		const long row = retTable.AppendRow();
		for (const ibBackendQueryColumn* column : straight)
			retTable.SetCell(row, column->GetColumnId(), sel.GetValue(column));
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
	const ibQueryPredicatePtr& filter, const ibValue& condition) const
{
	const ibBackendQueryable* movements = GetQueryable();
	if (movements == nullptr)
		return nullptr;

	ibDataQueryBuilder b;
	b.From(movements);
	b.WithAccessPolicy(nullptr);
	WherePeriodRange(b, GetRegisterPeriod()->GetQueryColumn(), RecorderColumnOf(this), begin, end);
	WhereCondition(b, this, movements, ibAcctShape::Records, /*creditSide*/ false, kindsDr, kindsCr, filter);

	if (const ibQueryPredicatePtr slots = OrWith(
			AccountDimensionCondition(this, movements, /*creditSide*/ false, condition),
			IsCorrespondence() ? AccountDimensionCondition(this, movements, /*creditSide*/ true, condition)
			                   : ibQueryPredicatePtr()))
		b.Where(slots);

	const ibQueryRelPtr lines = b.BuildRelation();
	ibRegFold recordFold;
	recordFold.m_kind = ibRegGranularity::Record;
	const ibBackendQueryable* published = GetShapeQueryable(ibAcctShape::Records, kindsDr, kindsCr, recordFold);
	if (lines == nullptr || published == nullptr)
		return nullptr;

	// ⭐⭐ THE BREAKDOWN UNDER THE NAMES THE SHAPE PUBLISHES — what ComputeRecords pours by name. The read
	// relation carries the movements' own fields and drops an expression projection, so the slots answered
	// under their physical names and `AccountDimensionDr1` read EMPTY on every line of this road (2026-09-17).
	// So the lines are wrapped: every field as it stands, and beside them each breakdown column spelled out —
	// a slot and its kind under the published names, or, by kind, a CASE over the slots per field.
	const wxString a = wxT("records");
	std::vector<ibQueryProjItem> projection{ { ibCol(a, wxT("*")), wxString() } };
	for (const bool creditSide : IsCorrespondence() ? std::vector<bool>{ false, true } : std::vector<bool>{ false }) {
		std::vector<ibAcctBreakdownColumn> layout;
		DescribeBreakdown(this, ibAcctShape::Records, creditSide, creditSide ? kindsCr : kindsDr, layout);
		for (const ibAcctBreakdownColumn& column : layout) {
			const ibBackendQueryColumn* out = published->ResolveColumnByName(column.m_alias);
			if (out == nullptr)
				continue;
			const std::vector<ibColumnSlot> outSlots = DescribeColumnLayout(out);
			if (!column.m_byKind) {
				// the slot and its kind as they stand, field by field under the published spelling
				const auto spell = [&](const ibValueMetaObjectAttributeBase* attribute, const ibBackendQueryColumn* to) {
					if (attribute == nullptr || to == nullptr)
						return;
					const std::vector<ibColumnSlot> from = DescribeColumnLayout(attribute->GetQueryColumn());
					const std::vector<ibColumnSlot> into = DescribeColumnLayout(to);
					for (size_t f = 0; f < from.size() && f < into.size(); ++f)
						projection.push_back({ ibCol(a, from[f].m_name), into[f].m_name });
				};
				spell(column.m_kindAttribute, column.m_kindAlias.IsEmpty() ? nullptr : published->ResolveColumnByName(column.m_kindAlias));
				spell(column.m_attribute, out);
				continue;
			}
			std::vector<std::vector<std::pair<ibQueryExprPtr, ibQueryExprPtr>>> cases(outSlots.size());
			for (unsigned int idx = 0; idx < GetAccountDimensionCount(); ++idx) {
				const ibValueMetaObjectAttributeBase* kindSlot = GetAccountDimensionKindSlot(creditSide, idx);
				const ibValueMetaObjectAttributeBase* slot     = GetAccountDimensionSlot(creditSide, idx);
				if (kindSlot == nullptr || slot == nullptr)
					continue;
				const ibQueryExprPtr when = ibRegCompositeIR(kindSlot->GetQueryColumn(), GetMetaData(), column.m_requestedKind, ibQueryBinOp::Eq, a);
				const std::vector<ibColumnSlot> from = DescribeColumnLayout(slot->GetQueryColumn());
				for (size_t f = 0; when && f < from.size() && f < outSlots.size(); ++f)
					cases[f].push_back({ when, ibCol(a, from[f].m_name) });
			}
			for (size_t f = 0; f < outSlots.size(); ++f)
				if (!cases[f].empty())
					projection.push_back({ ibCase(std::move(cases[f]), nullptr), outSlots[f].m_name });
		}
	}

	return ibProject(ibSubquery(lines, a), std::move(projection));
}

// ============================================================================
// The companions — each publishes a shape and runs one compute
// ============================================================================

ibQueryRamTable ibAcctBalanceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	ibQueryRamTable table = m_reg->ComputeBalance(m_bound, m_accountDr, m_accountCr, m_kindsDr, m_kindsCr, m_filter, m_condition);
	m_reg->ReportFiguresAsKept(table, m_shape, m_kindsDr, m_kindsCr);
	return table;
}

ibQueryRamTable ibAcctTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	ibQueryRamTable table = m_reg->ComputeTurnover(m_begin, m_end, m_accountDr, m_accountCr, m_kindsDr, m_kindsCr, m_filter, m_fold, m_condition,
		m_byCorrespondent);
	m_reg->ReportFiguresAsKept(table, m_shape, m_kindsDr, m_kindsCr);
	return table;
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
namespace {

// ⭐⭐ THE KEY A SERVER READ GROUPS BY IS THE KEY THE SHAPE PUBLISHES — THE BREAKDOWN INCLUDED.
//
// 🛑 These readings stood on the ACCOUNT grain, on the reasoning that a call naming no kinds asks for no
// analytics. But the shape publishes `AccountDimension<i>` and `…Kind` whether or not kinds were named
// — the slots AS THEY STAND, which is what the RAM road groups by in that case — and a reading cannot
// know which of its columns the query will take. So the key without the slots answered right only for a
// query that happened not to name them: a trial balance by analytics over a one-sided register failed
// with `-206 ACCOUNTDIMENSION1_TYPE` (2026-09-16), and a balance folded per ACCOUNT where the RAM road
// folds per set of analytics. Every figure is kept at the breakdown grain too (see the declaration), so
// that grain answers both questions, and the two roads now answer the same one.
//
// ⭐⭐ ONE VALUE, ONE KEY — AND AN EMPTY VALUE IS STORED TWO WAYS.
//
// A cell a writer filled holds the column's TYPED EMPTY (the type tag, the reference's table, a zero
// id). A cell nobody wrote holds no tag at all — `_TYPE` NULL or 0 — and that is a legitimate state, not
// damage: a column ADDED to a table that already had rows starts that way, and a type change clears the
// tag of the values it no longer admits (structureBatch.cpp). The codec reads both as the same typed
// empty (columnLayout.cpp, TagFitsColumn), so every reading through it sees one value.
//
// A GROUP BY does not read through the codec — it compares fields — and saw two. Measured 2026-09-16 on
// the continental ledger, whose currency was added after its first postings: "28 / Coffee" came back as
// two rows, one with the untagged currency of July and one with the typed empty of September. Summed they
// were the RAM answer to the kopeck; folded apart, each half folded its own balance.
//
// So the key a server read groups by is the key AS READ: an untagged cell answers with the fields of the
// column's typed empty, written by the same codec a write goes through (the capture ibRegCompositeIR
// uses), never spelled here.
std::unordered_map<wxString, ibQueryExprPtr> KeyFieldsAsRead(const ibBackendQueryColumn* column,
                                                              const ibMetaData* metaData, const wxString& alias)
{
	std::unordered_map<wxString, ibQueryExprPtr> out;
	const std::vector<ibColumnSlot> slots = DescribeColumnLayout(column);

	wxString tag;
	for (const ibColumnSlot& slot : slots)
		if (slot.m_role == ibColumnRole::Discriminator) {
			tag = slot.m_name;
			break;
		}
	if (tag.IsEmpty()) {
		for (const ibColumnSlot& slot : slots)
			out[slot.m_name] = ibCol(alias, slot.m_name);
		return out;   // a single raw field has no second way to be empty
	}

	std::vector<wxString> fields;
	for (const ibColumnSlot& slot : slots)
		fields.push_back(slot.m_name);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibColumnCodec::WriteValue(column, metaData, ibValueTypeDescription::AdjustValue(column->GetTypeDesc(), metaData), &capture, pos);
	const std::vector<ibQueryExprPtr>& empty = capture.CapturedValues();

	const ibQueryExprPtr untagged = ibBinOp(ibQueryBinOp::Or, ibIsNull(ibCol(alias, tag)),
		ibBinOp(ibQueryBinOp::Eq, ibCol(alias, tag), ibConst(ibValue(0))));
	for (size_t i = 0; i < slots.size(); ++i) {
		const ibQueryExprPtr asEmpty = (i < empty.size() && empty[i]) ? empty[i] : ibConst(ibValue());
		out[slots[i].m_name] = ibCase({ { untagged, asEmpty } }, ibCol(alias, slots[i].m_name));
	}
	return out;
}

// The key of ONE SIDE's stored surface, answering under the names the shape publishes. The side's own
// account, its own slots and its own half of every dimension — the same split the RAM road's credit pass
// makes when it reads `AccountCr` and reports under `Account`.
std::vector<ibAcctServerKey> ServerKeys(const ibValueMetaObjectAccountingRegister* reg,
                                        const ibBackendQueryable* published, ibAcctShape shape, bool creditSide)
{
	std::vector<ibAcctServerKey> keys;

	const ibValueMetaObjectAttributeBase* account     = reg->GetRegisterAccount();
	const ibValueMetaObjectAttributeBase* sideAccount = creditSide ? reg->GetRegisterAccountCr() : account;
	if (account != nullptr && sideAccount != nullptr)
		PairByRole(keys, sideAccount->GetQueryColumn(), published->ResolveColumnByName(PublishedAccountName(reg, shape)));

	std::vector<ibAcctBreakdownColumn> layout;
	DescribeBreakdown(reg, shape, creditSide, /*kinds*/ {}, layout);
	for (const ibAcctBreakdownColumn& column : layout) {
		if (BreakdownCarriesKind(column))
			PairByRole(keys, column.m_kindAttribute->GetQueryColumn(), published->ResolveColumnByName(column.m_kindAlias));
		PairByRole(keys, column.m_attribute->GetQueryColumn(), published->ResolveColumnByName(column.m_alias));
	}

	for (const auto dimension : reg->GetDimensionArrayObject())
		if (dimension != nullptr)
			PairByRole(keys, reg->GetRegisterDimension(creditSide, dimension),
				published->ResolveColumnByName(dimension->GetName()));

	return keys;
}

// ⭐ WHAT A SIDE'S READ IS FILTERED BY — the caller's condition over the dimensions, each leaf asked of
// the side's own half (a non-balance dimension is two columns, and the credit surface has only the
// credit one), and the account condition as the accounts it names, on the side's own account column.
//
// The dimension is recognised by the COLUMN'S ID, which a published column keeps as its dimension's own
// metaID — not by the name, which two things may share.
//
// ⭐⭐ THE WHOLE CONDITION, NOT ITS FLAT EQUALITIES. This took the leaves an AND of `=` holds and dropped the
// rest without a word — a NOT, an OR, a walk through the account were simply not asked, and the read came
// back wider than the question. The condition is now found again on the side's surface (ConditionOnPass —
// the same finding the RAM passes make) and lowered whole by the door that writes every other WHERE.
std::vector<ibQueryExprPtr> ServerFilters(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape,
                                          const ibQueryPredicatePtr& filter, const std::vector<ibValue>& kinds,
                                          const ibQueryHierarchyScope& accounts, bool creditSide)
{
	std::vector<ibQueryExprPtr> out;
	const ibMetaData* metaData = reg->GetMetaData();

	const ibBackendQueryable* view = reg->GetTurnoverViewQueryable(creditSide);
	if (const ibQueryPredicatePtr here = ConditionOnPass(reg, view, shape, creditSide, kinds, {}, filter))
		if (const ibQueryExprPtr lowered = ibDbTableProvider::BuildPredicateIR(view, here))
			out.push_back(lowered);

	const ibValueMetaObjectAttributeBase* account = creditSide ? reg->GetRegisterAccountCr() : reg->GetRegisterAccount();
	if (!accounts.IsEmpty() && account != nullptr) {
		ibQueryExprPtr any;
		for (const ibValue& value : accounts.Accepted())
			if (const ibQueryExprPtr one = ibRegCompositeIR(account->GetQueryColumn(), metaData, value, ibQueryBinOp::Eq))
				any = any ? ibBinOp(ibQueryBinOp::Or, any, one) : one;
		if (any)
			out.push_back(any);
	}
	return out;
}

// Does the account condition REPORT rows under another account? `IN HIERARCHY` does — the subordinates
// add up into the one named — and a stored row cannot be re-keyed under a value the server would have to
// be handed per account, so such a call keeps the RAM road. A plain `IN` or `=` names the rows it wants
// and nothing more, which is an ordinary filter. Asked of the predicate, so the gate reads nothing.
bool AccountConditionFoldsHierarchy(const ibQueryPredicatePtr& condition)
{
	if (!condition)
		return false;
	if (condition->m_kind == ibQueryPredicateKind::Leaf)
		return condition->m_leaf.m_unfold != ibQueryDimUnfold::Elements;
	for (const ibQueryPredicatePtr& child : condition->m_children)
		if (AccountConditionFoldsHierarchy(child))
			return true;
	return false;
}


// ⭐⭐ TWO SIDES, ONE ANSWER. A correspondence register keeps a surface per side — the debit one keyed
// by the debit account, the credit one by the credit account — and "the balance of 63" is both at once:
// what 63 took in as a debit account and what it gave out as a credit one. Each side is read on its own,
// answering under the same names; the two are laid one under the other and summed by the key, so an
// account that moved on both sides is one row. What is folded by the account's type is that sum —
// exactly the pair the RAM road folds after its two passes.
//
// ONE SIDE IS SUMMED TOO. Its read grouped by the stored fields, and an empty value stored two ways is two
// of its rows under one key as read (KeyFieldsAsRead) — this is where they become one.
ibQueryRelPtr SumOfSides(const std::vector<ibQueryRelPtr>& sides, const std::vector<wxString>& keyNames,
                         const std::vector<wxString>& figureNames, const wxString& alias)
{
	ibQueryRelPtr all = sides.front();
	for (size_t i = 1; i < sides.size(); ++i)
		all = ibUnionAll(all, sides[i]);

	const wxString unionAlias = alias + wxT("_u");
	std::vector<ibQueryProjItem> projection;
	std::vector<ibQueryExprPtr>  groupKeys;
	for (const wxString& name : keyNames) {
		projection.push_back({ ibCol(unionAlias, name), name });
		groupKeys.push_back(ibCol(unionAlias, name));
	}
	for (const wxString& name : figureNames)
		projection.push_back({ ibCast(ibFunc(wxT("SUM"), { ibCol(unionAlias, name) }), ibTypeNumber(18, 6)), name });

	return ibAggregate(ibSubquery(all, unionAlias), std::move(projection), std::move(groupKeys));
}

// The sides a server read stands on: both surfaces of a correspondence register, the one of a one-sided.
std::vector<bool> SidesOf(const ibValueMetaObjectAccountingRegister* reg)
{
	return reg->IsCorrespondence() ? std::vector<bool>{ false, true } : std::vector<bool>{ false };
}

// One figure a server read asks for: the name it answers under, which side it is, how it is summed and
// over which rows, and the stored turnover it is summed from.
struct ibAcctServerFigure
{
	wxString          m_name;
	bool              m_credit;
	ibMaterializeAgg  m_agg;
	ibMaterializeWhen m_when;
	wxString          m_from;    // the stored turnover's logical name, asked of the side's view
};

// ⭐ ONE SIDE'S READ, ANSWERING UNDER THE PUBLISHED NAMES. The spec arrives with what does not depend on
// the side — the period, the interval, the grain, the arm cut — and this adds the side's surface, key,
// filters and figures. A figure the side does not store (the credit turnover on the debit surface of a
// correspondence register) is a typed zero, so both sides answer with the same columns. `keyNames` comes
// back as the names the key answers under, the period among them when the read is cut into periods.
ibQueryRelPtr ServerSideRead(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* published,
                             ibAcctShape shape, bool creditSide, ibMaterializeReadSpec spec,
                             const ibQueryPredicatePtr& filter, const std::vector<ibValue>& kinds,
                             const ibQueryHierarchyScope& accounts,
                             const std::vector<ibAcctServerFigure>& figures, const wxString& alias,
                             std::vector<wxString>& keyNames)
{
	const ibBackendQueryable* view = reg->GetTurnoverViewQueryable(creditSide);
	if (view == nullptr)
		return nullptr;

	spec.m_view = reg->GetTurnoverViewName(creditSide);

	const std::vector<ibAcctServerKey> keys = ServerKeys(reg, published, shape, creditSide);
	for (const ibAcctServerKey& key : keys)
		spec.m_keyColumns.push_back(key.m_stored);

	// The filters ride INSIDE the subquery, so the selection happens on the server before the outer
	// query sees a row — which is the whole point of handing the door a relation instead of rows.
	spec.m_filters = ServerFilters(reg, shape, filter, kinds, accounts, creditSide);

	// ⭐ THE PHYSICAL NAMES ARE ASKED FOR, NOT SPELLED — a read spec naming a column the view does not
	// have returns NULLs rather than an error. The logical side is `ibAcctFigure`; the physical side is
	// the view's business.
	const auto storedHere = [reg, creditSide](const ibAcctServerFigure& figure) {
		return !reg->IsCorrespondence() || figure.m_credit == creditSide;
	};
	for (const ibAcctServerFigure& figure : figures)
		if (storedHere(figure))
			spec.m_columns.push_back({ figure.m_name, ibRegPhysicalOf(view, figure.m_from), wxString(),
			                           figure.m_agg, figure.m_when, true });

	std::vector<ibQueryProjItem> projection;
	keyNames.clear();
	std::unordered_map<const ibBackendQueryColumn*, std::unordered_map<wxString, ibQueryExprPtr>> asRead;
	for (const ibAcctServerKey& key : keys) {
		auto& fields = asRead[key.m_column];
		if (fields.empty())
			fields = KeyFieldsAsRead(key.m_column, reg->GetMetaData(), alias);
		const auto found = fields.find(key.m_stored);
		projection.push_back({ found != fields.end() ? found->second : ibCol(alias, key.m_stored), key.m_published });
		keyNames.push_back(key.m_published);
	}
	// ⚠ THE PERIOD IS A COLUMN OF THE ANSWER when the read is cut into periods — the read puts it into
	// the grouping itself, and a projection built from the key alone would group by the month and
	// decline to say which month.
	if (spec.m_grain != ibMaterializeGrain::Whole) {
		projection.push_back({ ibCol(alias, spec.m_periodColumn), spec.m_periodColumn });
		keyNames.push_back(spec.m_periodColumn);
	}
	// ⚠ A SUM OF NOTHING IS ZERO. A figure added to a register that already had postings is NULL in every
	// stored row written before it, and a sum over only those is NULL — which reads as "no such accounting
	// here" where the account does keep it and nothing moved (measured 2026-09-16: the quantity of goods
	// account 28 inside a July day). Coalesced where the side answers, so the zero is said once.
	for (const ibAcctServerFigure& figure : figures)
		projection.push_back({ storedHere(figure) ? ibFunc(wxT("COALESCE"), { ibCol(alias, figure.m_name), ibRegTypedZero() }) : ibRegTypedZero(),
		                       figure.m_name });

	return ibProject(RenderMaterializedRead(spec, alias), std::move(projection));
}

// Every side of the register read, and the sides summed into one answer — a relation under `alias`'s
// sub-aliases, with the key under the names in `keyNames` and the figures under their own. Null when a
// side has no surface to read.
ibQueryRelPtr ServerRead(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* published,
                         ibAcctShape shape, const ibMaterializeReadSpec& spec,
                         const ibQueryPredicatePtr& filter, const std::vector<ibValue>& kinds,
                         const ibQueryHierarchyScope& accounts,
                         const std::vector<ibAcctServerFigure>& figures, const wxString& alias,
                         std::vector<wxString>& keyNames)
{
	std::vector<ibQueryRelPtr> sides;
	for (bool creditSide : SidesOf(reg)) {
		ibQueryRelPtr side = ServerSideRead(reg, published, shape, creditSide, spec, filter, kinds, accounts, figures,
			alias + (creditSide ? wxT("_cr") : wxT("_dr")), keyNames);
		if (side == nullptr)
			return nullptr;
		sides.push_back(side);
	}

	std::vector<wxString> figureNames;
	for (const ibAcctServerFigure& figure : figures)
		figureNames.push_back(figure.m_name);
	return SumOfSides(sides, keyNames, figureNames, alias);
}

// ⭐⭐ THE CALL'S BREAKDOWN, RE-KEYED OVER A READ OF THE SLOTS AS THEY STAND — a breakdown BY KIND and a
// TURNOVERS-ONLY subconto, answered by the server. The stored surface is keyed by the slots (their kinds beside
// them), and so is its read; over it each row is re-keyed the way the RAM road keys it, and summed again:
//
//   by kind        a column per asked kind: the value of the slot that kind stands in, a CASE over the slots —
//                  ProjectDimensionByKind, applied to what the surface already grouped
//   turnovers-only a BALANCE is not kept along such a subconto: the slot leaves the key, value and kind
//                  (FoldOutSummaryOnly). Which slot that is, is DATA — the flag on the row of the account's kinds
//                  table for the kind standing in it — so it is asked per row, once per slot, and carried up as a
//                  0/1 column the levels above test
//
// Three levels: the flags, the keys (a GROUP BY over a CASE that holds a subquery is refused by Firebird), the sums.
enum class ibAcctTake
{
	AsRead,                 // the figure as the read holds it
	Zero,                   // a typed zero — the half a UNION arm does not report
	UnlessTurnoversOnly,    // zero on a row standing on a turnovers-only subconto: a balance row's own turnovers
};

struct ibAcctTakeFigure
{
	wxString   m_name;      // what the re-keyed read answers under
	wxString   m_from;      // the read's figure it is taken from
	ibAcctTake m_take;
};

struct ibAcctRegroup
{
	const ibBackendQueryable* m_asStand   = nullptr;   // the shape the read was keyed by (no kinds asked)
	const ibBackendQueryable* m_published = nullptr;   // the call's shape — the names the key answers under
	ibAcctShape               m_shape     = ibAcctShape::Balance;
	std::vector<ibValue>      m_kinds;                 // asked; empty = the slots as they stand
	bool                      m_foldTurnoversOnly = false;   // a balance key: turnovers-only slots leave it
	bool                      m_onlyTurnoversOnly = false;   // keep only the rows standing on such a slot
};

// The name the turnovers-only flag of slot `i` is carried under between the levels.
wxString TurnoversOnlyFlagName(size_t i)
{
	return wxString::Format(wxT("TurnoversOnly%u"), static_cast<unsigned>(i + 1));
}

// Every figure the takes read, once — what the lower levels carry up.
std::vector<wxString> TakenFrom(const std::vector<ibAcctTakeFigure>& figures)
{
	std::vector<wxString> out;
	for (const ibAcctTakeFigure& figure : figures)
		if (std::find(out.begin(), out.end(), figure.m_from) == out.end())
			out.push_back(figure.m_from);
	return out;
}

// The column a balance-and-turnovers row says it stands on a turnovers-only subconto by (BalanceAndTurnoverArms).
const wxString& StandsOnTurnoversOnlyName()
{
	static const wxString s_name(wxT("StandsOnTurnoversOnly"));
	return s_name;
}

// `keyNames` in: the read's key; out: the key the re-keyed read answers under.
ibQueryRelPtr RegroupServerRead(const ibValueMetaObjectAccountingRegister* reg, const ibQueryRelPtr& read,
	const ibAcctRegroup& how, const std::vector<ibAcctTakeFigure>& figures, std::vector<wxString>& keyNames,
	const wxString& alias)
{
	const ibMetaData* metaData = reg->GetMetaData();
	const wxString r = alias + wxT("_r"), s = alias + wxT("_s"), g = alias + wxT("_g");
	const std::vector<wxString> carried = TakenFrom(figures);

	std::vector<ibAcctBreakdownColumn> stand, asked;
	DescribeBreakdown(reg, how.m_shape, /*creditSide*/ false, {}, stand);
	if (!how.m_kinds.empty())
		DescribeBreakdown(reg, how.m_shape, /*creditSide*/ false, how.m_kinds, asked);

	std::set<wxString> slotFields;   // re-keyed below, never passed through
	std::vector<const ibBackendQueryColumn*> kindOf(stand.size(), nullptr), valueOf(stand.size(), nullptr);
	for (size_t i = 0; i < stand.size(); ++i) {
		kindOf[i]  = stand[i].m_kindAlias.IsEmpty() ? nullptr : how.m_asStand->ResolveColumnByName(stand[i].m_kindAlias);
		valueOf[i] = how.m_asStand->ResolveColumnByName(stand[i].m_alias);
		for (const ibBackendQueryColumn* column : { kindOf[i], valueOf[i] })
			if (column != nullptr)
				for (const wxString& field : ColumnFieldNames(column))
					slotFields.insert(field);
	}

	// --- the flags: is slot i a turnovers-only subconto of the row's account ---------------------------------------
	bool asksTurnoversOnly = how.m_foldTurnoversOnly || how.m_onlyTurnoversOnly;
	for (const ibAcctTakeFigure& figure : figures)
		asksTurnoversOnly = asksTurnoversOnly || figure.m_take == ibAcctTake::UnlessTurnoversOnly;
	const ibAcctKindsTable kinds = KindsTableOf(reg);
	const ibBackendQueryColumn* accountCol = how.m_asStand->ResolveColumnByName(PublishedAccountName(reg, how.m_shape));
	const wxString accountId = accountCol != nullptr ? ibRegFieldOfRole(accountCol, ibColumnRole::ReferenceId) : wxString();
	std::vector<bool> flagged(stand.size(), false);
	ibQueryRelPtr source = ibSubquery(read, r);
	wxString below = r;
	if (asksTurnoversOnly && kinds.m_rows != nullptr && kinds.m_summaryCol != nullptr && !accountId.IsEmpty()) {
		std::vector<ibQueryProjItem> level;
		for (const wxString& name : keyNames)
			level.push_back({ ibCol(r, name), name });
		for (const wxString& name : carried)
			level.push_back({ ibCol(r, name), name });
		for (size_t i = 0; i < stand.size(); ++i) {
			if (kindOf[i] == nullptr)
				continue;
			const wxString k = alias + wxString::Format(wxT("_to%u"), static_cast<unsigned>(i));
			const ibQueryExprPtr summary = ibBinOp(ibQueryBinOp::And, ibRegSameValueIR(kindOf[i], r, kinds.m_kindCol, k),
				ibRegCompositeIR(kinds.m_summaryCol, metaData, ibValue(true), ibQueryBinOp::Eq, k));
			level.push_back({ ibAcctOneIf(KindsRowExists(kinds, r, accountId, summary, k)), TurnoversOnlyFlagName(i) });
			flagged[i] = true;
		}
		source = ibSubquery(ibProject(source, std::move(level)), s);
		below = s;
	}
	const auto turnoversOnly = [&](size_t i) { return ibAcctIsOne(ibCol(below, TurnoversOnlyFlagName(i))); };

	// A row stands on a turnovers-only subconto the call REPORTS: any such slot as they stand, only an asked kind's.
	ibQueryExprPtr stands;
	for (size_t i = 0; i < stand.size(); ++i) {
		if (!flagged[i])
			continue;
		ibQueryExprPtr reported = turnoversOnly(i);
		if (!how.m_kinds.empty()) {
			ibQueryExprPtr anyAsked;
			for (const ibValue& kind : how.m_kinds)
				if (const ibQueryExprPtr one = ibRegCompositeIR(kindOf[i], metaData, kind, ibQueryBinOp::Eq, below))
					anyAsked = anyAsked ? ibBinOp(ibQueryBinOp::Or, anyAsked, one) : one;
			reported = anyAsked ? ibBinOp(ibQueryBinOp::And, reported, anyAsked) : nullptr;
		}
		if (reported)
			stands = stands ? ibBinOp(ibQueryBinOp::Or, stands, reported) : reported;
	}
	if (how.m_onlyTurnoversOnly) {
		if (!stands)
			return nullptr;   // no row can stand on one — the caller has nothing to add
		source = ibFilter(source, stands);
	}

	// --- the keys ---------------------------------------------------------------------------------------------------
	std::vector<ibQueryProjItem> level;
	std::vector<wxString> outKeys;
	for (const wxString& name : keyNames)
		if (slotFields.find(name) == slotFields.end()) {
			level.push_back({ ibCol(below, name), name });
			outKeys.push_back(name);
		}
	const bool fold = how.m_foldTurnoversOnly;
	if (how.m_kinds.empty()) {
		for (size_t i = 0; i < stand.size(); ++i)
			for (const ibBackendQueryColumn* column : { kindOf[i], valueOf[i] }) {
				if (column == nullptr)
					continue;
				for (const wxString& field : ColumnFieldNames(column)) {
					const ibQueryExprPtr value = ibCol(below, field);
					level.push_back({ fold && flagged[i] ? ibCase({ { turnoversOnly(i), ibConst(ibValue()) } }, value) : value, field });
					outKeys.push_back(field);
				}
			}
	}
	else {
		for (const ibAcctBreakdownColumn& column : asked) {
			const ibBackendQueryColumn* out = how.m_published->ResolveColumnByName(column.m_alias);
			if (out == nullptr)
				continue;
			const std::vector<ibColumnSlot> outSlots = DescribeColumnLayout(out);
			std::vector<std::vector<std::pair<ibQueryExprPtr, ibQueryExprPtr>>> cases(outSlots.size());
			for (size_t i = 0; i < stand.size(); ++i) {
				if (kindOf[i] == nullptr || valueOf[i] == nullptr)
					continue;
				ibQueryExprPtr when = ibRegCompositeIR(kindOf[i], metaData, column.m_requestedKind, ibQueryBinOp::Eq, below);
				if (!when)
					continue;
				if (fold && flagged[i])
					when = ibBinOp(ibQueryBinOp::And, when, ibBinOp(ibQueryBinOp::Eq, ibCol(below, TurnoversOnlyFlagName(i)),
						ibCast(ibConst(ibValue(0)), ibTypeInteger())));
				// Every slot of a side is declared alike, so its fields stand position for position with the column's.
				const std::vector<ibColumnSlot> slotSlots = DescribeColumnLayout(valueOf[i]);
				for (size_t f = 0; f < outSlots.size() && f < slotSlots.size(); ++f)
					cases[f].push_back({ when, ibCol(below, slotSlots[f].m_name) });
			}
			for (size_t f = 0; f < outSlots.size(); ++f) {
				if (cases[f].empty())
					continue;
				level.push_back({ ibCase(std::move(cases[f]), nullptr), outSlots[f].m_name });
				outKeys.push_back(outSlots[f].m_name);
			}
		}
	}
	for (const ibAcctTakeFigure& figure : figures) {
		const ibQueryExprPtr value = ibCol(below, figure.m_from);
		switch (figure.m_take) {
		case ibAcctTake::AsRead:
			level.push_back({ value, figure.m_name });
			break;
		case ibAcctTake::Zero:
			level.push_back({ ibRegTypedZero(), figure.m_name });
			break;
		case ibAcctTake::UnlessTurnoversOnly:
			level.push_back({ stands ? ibCase({ { stands, ibRegTypedZero() } }, value) : value, figure.m_name });
			break;
		}
	}

	// --- the sums ---------------------------------------------------------------------------------------------------
	std::vector<ibQueryProjItem> projection;
	std::vector<ibQueryExprPtr> groupKeys;
	for (const wxString& name : outKeys) {
		projection.push_back({ ibCol(g, name), name });
		groupKeys.push_back(ibCol(g, name));
	}
	for (const ibAcctTakeFigure& figure : figures)
		projection.push_back({ ibCast(ibFunc(wxT("SUM"), { ibCol(g, figure.m_name) }), ibTypeNumber(18, 6)), figure.m_name });

	keyNames = outKeys;
	return ibAggregate(ibSubquery(ibProject(source, std::move(level)), g), std::move(projection), std::move(groupKeys));
}

// ⭐ A BALANCE-AND-TURNOVERS READ WITH TURNOVERS-ONLY SUBCONTOS — two arms, one answer (the RAM road's lending):
//
//   balances   keyed WITHOUT the turnovers-only subconto: the opening and the closing of every row of that key —
//              the turnovers along the subconto included, so the balance closes on them — and turnovers of the
//              key's own rows only
//   turnovers  the rows standing ON such a subconto, keyed by it: their turnovers, and no balance (a marker the
//              projection above reads, so the balances there are empty rather than zero)
//
// `marker` names the 0/1 column that says which arm a row came from.
ibQueryRelPtr BalanceAndTurnoverArms(const ibValueMetaObjectAccountingRegister* reg, const ibQueryRelPtr& read,
	ibAcctRegroup how, const std::vector<ibAcctTakeFigure>& balanceArm, const std::vector<ibAcctTakeFigure>& turnoverArm,
	std::vector<wxString>& keyNames, const wxString& alias)
{
	const wxString& marker = StandsOnTurnoversOnlyName();
	std::vector<wxString> balanceKeys = keyNames, turnoverKeys = keyNames;
	how.m_foldTurnoversOnly = true;
	const ibQueryRelPtr balances = RegroupServerRead(reg, read, how, balanceArm, balanceKeys, alias + wxT("_b"));
	how.m_foldTurnoversOnly = false;
	how.m_onlyTurnoversOnly = true;
	const ibQueryRelPtr turnovers = RegroupServerRead(reg, read, how, turnoverArm, turnoverKeys, alias + wxT("_o"));
	if (balances == nullptr)
		return nullptr;
	keyNames = balanceKeys;

	// Both arms under one column order — the balance arm's — each with its marker.
	const auto arm = [&](const ibQueryRelPtr& rel, const std::vector<ibAcctTakeFigure>& figures, int standsOn, const wxString& a) {
		std::vector<ibQueryProjItem> projection;
		for (const wxString& name : balanceKeys)
			projection.push_back({ ibCol(a, name), name });
		for (const ibAcctTakeFigure& figure : figures)
			projection.push_back({ ibCol(a, figure.m_name), figure.m_name });
		projection.push_back({ ibCast(ibConst(ibValue(standsOn)), ibTypeInteger()), marker });
		return ibProject(ibSubquery(rel, a), std::move(projection));
	};
	const ibQueryRelPtr balanceRows = arm(balances, balanceArm, 0, alias + wxT("_ba"));
	return turnovers != nullptr ? ibUnionAll(balanceRows, arm(turnovers, turnoverArm, 1, alias + wxT("_oa"))) : balanceRows;
}

// The accounts a server read is narrowed to. The gate has already sent a condition that reports rows
// under another account back to the RAM road, so what arrives here names its accounts outright and the
// scope reads nothing.
// `published` is the reading, which names its own `Account` (ScopeFromAccountCondition).
ibQueryHierarchyScope ServerAccounts(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* published,
                                     ibAcctShape shape, const ibQueryPredicatePtr& condition)
{
	const ibValueMetaObjectAttributeBase* account = reg->GetRegisterAccount();
	return account != nullptr
		? ScopeFromAccountCondition(reg->GetQueryable(), account->GetQueryColumn(), condition,
			published != nullptr ? published->ResolveColumnByName(PublishedAccountName(reg, shape)) : nullptr)
		: ibQueryHierarchyScope();
}

// The gate every stored-surface reading shares on the account arguments. A CORRESPONDING account ("the
// turnovers of 51 against 62") is a question about the OTHER account of a movement, which no stored row
// holds — it is answered by the movements. An account condition that reports rows under another account
// is the RAM road's too (AccountConditionFoldsHierarchy).
bool AccountArgumentsReadOnServer(const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr)
{
	return accountCr == nullptr && !AccountConditionFoldsHierarchy(accountDr);
}

// ⭐⭐ BALANCE AND TURNOVERS PER PERIOD, ON THE SERVER — every period of the interval for every key.
//
// The owner's rule (Max, 2026-09-16): a period where a balance stands or something moved is a row, a period
// with a balance and no movement reports zero turnovers, a period with neither is no row. What the stored
// surface has is only the periods something moved in, and no window invents the others — so the read is
// assembled around a CALENDAR:
//
//   turnovers   the sides' turnovers per key per period (the ordinary server read, grouped by the period);
//   opening     each key's balance entering the interval (the same read, before its start), NOT pruned —
//               grouped over every row up to the end, it is also the set of every key;
//   grid        the opening read times every period of the calendar;
//   running     the grid joined to the turnovers, and each period's closing the opening plus the turnovers so far —
//               a window along the periods of one key, peers included;
//
// then folded by the account's type exactly as the unperiodised reading folds, and pruned of the rows where
// every figure is zero. One side or two: the turnovers and the opening are already summed across the sides,
// so a correspondence register runs one running sum per side of the KEY, not per surface — which is what
// kept its periodised reading off the server before.
//
// BY KIND and WITH TURNOVERS-ONLY SUBCONTOS the reads are re-keyed (RegroupServerRead): the opening and the grid by
// the balance key, the running closing on every turnover of that key — the ones along a turnovers-only subconto
// lent to it — and the rows standing on such a subconto laid under the grid with their turnovers and no balance.
ibQueryRelPtr PeriodisedOnServer(const ibValueMetaObjectAccountingRegister* reg, const ibBackendQueryable* published,
                                 ibAcctShape shape, const ibRegBound& begin, const ibRegBound& end, const ibRegFold& fold,
                                 const ibQueryPredicatePtr& filter, const ibQueryPredicatePtr& accountDr,
                                 const std::vector<ibValue>& kinds, bool turnoversOnly, const wxString& alias)
{
	const ibValueMetaObjectAttributeBase*   period  = reg->GetRegisterPeriod();
	const ibValueMetaObjectAttributeBase*   account = reg->GetRegisterAccount();
	const ibValueMetaObjectChartOfAccounts* chart   = reg->GetChartOfAccounts();
	if (period == nullptr || account == nullptr || chart == nullptr || chart->GetQueryable() == nullptr)
		return nullptr;
	const std::vector<wxDateTime> periods = ibRegCalendarOf(begin.m_date, end.m_date, fold.m_unit);
	if (periods.empty())
		return nullptr;

	const wxString periodField = ibRegValueField(period);
	const ibQueryHierarchyScope accounts = ServerAccounts(reg, published, shape, accountDr);

	// --- the turnovers per key per period --------------------------------------------------------------
	ibMaterializeReadSpec t;
	t.m_periodColumn = periodField;
	t.m_from         = begin.m_date;
	t.m_to           = end.m_date;
	t.m_dropZeroRows = true;
	t.m_grain        = ibMaterializeGrain::Calendar;
	t.m_periodUnit   = fold.m_unit;
	t.m_fromGrain    = ibValue(periods.front());
	ibRegFillArmCut(t, reg, end, begin);

	// --- the balance entering the interval, per key ---------------------------------------------------------
	ibMaterializeReadSpec o;
	o.m_periodColumn = periodField;
	o.m_from         = begin.m_date;
	o.m_to           = end.m_date;
	// ⭐ NOT PRUNED: this read is also the KEY SET. Grouped over every row up to the end of the interval, it
	// has a row for each key that holds a balance entering it or moves inside it — a zero opening included —
	// so the grid is this read times the calendar, and the turnovers are joined to it once.
	o.m_dropZeroRows = false;
	ibRegFillArmCut(o, reg, end, begin);

	std::vector<ibAcctServerFigure> turnovers, openings;
	for (const auto resource : reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const wxString fromDr = FigureName(resource, ibAcctFigure::TurnoverDr);
		const wxString fromCr = FigureName(resource, ibAcctFigure::TurnoverCr);
		turnovers.push_back({ FigureField(resource, ibAcctFigure::TurnoverDr), false, ibMaterializeAgg::Value, ibMaterializeWhen::InRange, fromDr });
		turnovers.push_back({ FigureField(resource, ibAcctFigure::TurnoverCr), true,  ibMaterializeAgg::Value, ibMaterializeWhen::InRange, fromCr });
		openings.push_back({ FigureField(resource, ibAcctFigure::OpeningBalanceDr), false, ibMaterializeAgg::Value, ibMaterializeWhen::BeforeFrom, fromDr });
		openings.push_back({ FigureField(resource, ibAcctFigure::OpeningBalanceCr), true,  ibMaterializeAgg::Value, ibMaterializeWhen::BeforeFrom, fromCr });
	}
	if (turnovers.empty())
		return nullptr;

	const bool rekeys = !kinds.empty() || turnoversOnly;
	const ibBackendQueryable* readShape = rekeys ? reg->GetShapeQueryable(shape, {}, {}, fold) : published;
	if (readShape == nullptr)
		return nullptr;
	std::vector<wxString> turnKeys, keyNames;
	ibQueryRelPtr turnRead = ServerRead(reg, readShape, shape, t, filter, kinds, accounts, turnovers, alias + wxT("_ptr"), turnKeys);
	ibQueryRelPtr openRead = ServerRead(reg, readShape, shape, o, filter, kinds, accounts, openings, alias + wxT("_por"), keyNames);
	if (turnRead == nullptr || openRead == nullptr)
		return nullptr;

	// --- the grid and its running balances, one per side of each resource (registerQueryLowering.h) ----------
	// With turnovers-only subcontos a balance runs on every turnover of its key and reports its own rows' only: the
	// running sum reads the turnovers `…WithLent`, the period reports the plain ones.
	std::vector<wxString> passThrough;
	std::vector<ibRegRunningFigure> running;
	std::vector<ibAcctTakeFigure> turnTakes, openTakes, standingTakes;
	for (const auto resource : reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		for (const bool credit : { false, true }) {
			const wxString turnName = FigureField(resource, credit ? ibAcctFigure::TurnoverCr : ibAcctFigure::TurnoverDr);
			const wxString openName = FigureField(resource, credit ? ibAcctFigure::OpeningBalanceCr : ibAcctFigure::OpeningBalanceDr);
			const wxString runName  = turnoversOnly ? turnName + wxT("WithLent") : turnName;
			passThrough.push_back(turnName);
			running.push_back({ runName, openName, openName,
				FigureField(resource, credit ? ibAcctFigure::ClosingBalanceCr : ibAcctFigure::ClosingBalanceDr) });
			turnTakes.push_back({ turnName, turnName, turnoversOnly ? ibAcctTake::UnlessTurnoversOnly : ibAcctTake::AsRead });
			if (turnoversOnly)
				turnTakes.push_back({ runName, turnName, ibAcctTake::AsRead });
			openTakes.push_back({ openName, openName, ibAcctTake::AsRead });
			standingTakes.push_back({ turnName, turnName, ibAcctTake::AsRead });
		}
	}

	ibQueryRelPtr standing;
	if (rekeys) {
		ibAcctRegroup how;
		how.m_asStand           = readShape;
		how.m_published         = published;
		how.m_shape             = shape;
		how.m_kinds             = kinds;
		how.m_foldTurnoversOnly = turnoversOnly;
		const ibQueryRelPtr asStand = turnRead;
		std::vector<wxString> standingKeys = turnKeys;
		openRead = RegroupServerRead(reg, openRead, how, openTakes, keyNames, alias + wxT("_pgo"));
		turnRead = RegroupServerRead(reg, turnRead, how, turnTakes, turnKeys, alias + wxT("_pgt"));
		if (turnoversOnly) {
			how.m_foldTurnoversOnly = false;
			how.m_onlyTurnoversOnly = true;
			standing = RegroupServerRead(reg, asStand, how, standingTakes, standingKeys, alias + wxT("_pgs"));
		}
	}

	ibQueryRelPtr grid = ibRegRunningGrid(openRead, turnRead, periods, periodField, keyNames, passThrough, running, alias);
	if (grid == nullptr)
		return nullptr;

	// …and the rows standing on a turnovers-only subconto under it, in its column order, marked (BalanceAndTurnoverArms).
	const bool marked = standing != nullptr;
	if (marked) {
		const auto arm = [&](const ibQueryRelPtr& rel, bool standsOn, const wxString& a) {
			std::vector<ibQueryProjItem> projection;
			for (const wxString& name : keyNames)
				projection.push_back({ ibCol(a, name), name });
			projection.push_back({ ibCol(a, periodField), periodField });
			for (const wxString& name : passThrough)
				projection.push_back({ ibCol(a, name), name });
			for (const ibRegRunningFigure& figure : running) {
				projection.push_back({ standsOn ? ibRegTypedZero() : ibCol(a, figure.m_openingOut), figure.m_openingOut });
				projection.push_back({ standsOn ? ibRegTypedZero() : ibCol(a, figure.m_closingOut), figure.m_closingOut });
			}
			projection.push_back({ ibCast(ibConst(ibValue(standsOn ? 1 : 0)), ibTypeInteger()), StandsOnTurnoversOnlyName() });
			return ibProject(ibSubquery(rel, a), std::move(projection));
		};
		grid = ibUnionAll(arm(grid, false, alias + wxT("_pga")), arm(standing, true, alias + wxT("_psa")));
	}
	const wxString aR = alias + wxT("_pr"), aA = alias + wxT("_pa");
	const ibQueryRelPtr rows = ibSubquery(grid, aR);

	// --- folded by the account's type, as the unperiodised reading folds --------------------------------------
	ibQueryRelPtr joined = ibJoin(rows, ibScan(chart->GetQueryable()->GetQueryTableName(), aA),
		ibRegSameValueIR(account->GetQueryColumn(), aR, chart->GetDataReference()->GetQueryColumn(), aA), ibQueryJoinType::Left);
	std::vector<wxString> subcontos;   // …and the breakdown slots to their kinds rows (JoinSubcontoKinds)
	if (AnyFigureKeptByBreakdown(reg))
		joined = JoinSubcontoKinds(reg, joined, account->GetQueryColumn(), aR,
			SlotKindsOf(reg, published, shape, /*creditSide*/ false, kinds), alias + wxT("_s"), subcontos);
	const ibQueryExprPtr accountType = ibCol(aA, ibRegValueField(chart->GetAccountType()));
	const ibQueryExprPtr apFolds = FullAnalyticsOnServer(reg, kinds, aR, account->GetQueryColumn(), alias + wxT("_fa"));
	const ibQueryExprPtr keepsBalance = marked
		? ibBinOp(ibQueryBinOp::Eq, ibCol(aR, StandsOnTurnoversOnlyName()), ibCast(ibConst(ibValue(0)), ibTypeInteger())) : nullptr;

	std::vector<ibQueryProjItem> projection;
	for (const wxString& name : keyNames)
		projection.push_back({ ibCol(aR, name), name });
	projection.push_back({ ibCol(aR, periodField), periodField });
	ibQueryExprPtr anyFigure;
	const auto nonZero = [&anyFigure](const ibQueryExprPtr& figure) {
		const ibQueryExprPtr one = ibBinOp(ibQueryBinOp::Ne, figure, ibRegTypedZero());
		anyFigure = anyFigure ? ibBinOp(ibQueryBinOp::Or, anyFigure, one) : one;
	};
	for (const auto resource : reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const ibQueryExprPtr kept = BothKept(KindKeptOnServer(reg, resource, aA), KeptBySubcontoOnServer(reg, resource, subcontos));
		const ibQueryExprPtr turnDr = ibCol(aR, FigureField(resource, ibAcctFigure::TurnoverDr));
		const ibQueryExprPtr turnCr = ibCol(aR, FigureField(resource, ibAcctFigure::TurnoverCr));
		projection.push_back({ FigureWhereKept(kept, turnDr), FigureField(resource, ibAcctFigure::TurnoverDr) });
		projection.push_back({ FigureWhereKept(kept, turnCr), FigureField(resource, ibAcctFigure::TurnoverCr) });
		projection.push_back({ FigureWhereKept(kept, ibBinOp(ibQueryBinOp::Sub, turnDr, turnCr)), FigureField(resource, ibRegFigure::Turnover) });
		nonZero(turnDr);
		nonZero(turnCr);
		const ibQueryExprPtr balanceKept = BothKept(kept, keepsBalance);   // …and none on a turnovers-only row
		const auto foldPair = [&](const wxString& debitName, const wxString& creditName,
			const wxString& grossDebitName, const wxString& grossCreditName, const wxString& netName) {
			const ibQueryExprPtr grossDr = ibCol(aR, debitName);
			const ibQueryExprPtr grossCr = ibCol(aR, creditName);
			const auto folded = FoldedPairOnServer(accountType, grossDr, grossCr, apFolds);
			projection.push_back({ FigureWhereKept(balanceKept, grossDr), grossDebitName });
			projection.push_back({ FigureWhereKept(balanceKept, grossCr), grossCreditName });
			projection.push_back({ FigureWhereKept(balanceKept, folded.first),  debitName });
			projection.push_back({ FigureWhereKept(balanceKept, folded.second), creditName });
			projection.push_back({ FigureWhereKept(balanceKept, ibBinOp(ibQueryBinOp::Sub, folded.first, folded.second)), netName });
			nonZero(folded.first);
			nonZero(folded.second);
		};
		foldPair(FigureField(resource, ibAcctFigure::OpeningBalanceDr), FigureField(resource, ibAcctFigure::OpeningBalanceCr),
			FigureField(resource, ibAcctFigure::OpeningGrossBalanceDr), FigureField(resource, ibAcctFigure::OpeningGrossBalanceCr),
			FigureField(resource, ibRegFigure::OpeningBalance));
		foldPair(FigureField(resource, ibAcctFigure::ClosingBalanceDr), FigureField(resource, ibAcctFigure::ClosingBalanceCr),
			FigureField(resource, ibAcctFigure::ClosingGrossBalanceDr), FigureField(resource, ibAcctFigure::ClosingGrossBalanceCr),
			FigureField(resource, ibRegFigure::ClosingBalance));
	}

	// --- and a period where every figure is zero is no row ---------------------------------------------
	const ibQueryRelPtr folded = anyFigure ? ibFilter(joined, anyFigure) : joined;
	return ibProject(folded, std::move(projection));
}

} // namespace

bool ibAcctTurnoverQueryable::CanReadOnServer() const
{
	if (m_reg == nullptr || !m_reg->HasMaterializedViews())
		return false;   // the driver maintains nothing — live aggregation is the only road

	// ⭐⭐ BOTH MODES. A correspondence register keeps two surfaces, one per side, and was sent back here
	// on the ground that a read spec reads one relation. It still does — each side is read by its own
	// spec and the two are summed around them (SumOfSides), which is the relation tree's work and not
	// the spec's (2026-09-16: the continental ledger had never once stood on its totals).
	//
	// 🛑 AND THE ACCOUNT ARGUMENTS WERE NEVER APPLIED ON THIS ROAD. They are consumed by the reading and
	// do not reach an outer WHERE, and the server read did not look at them: `Account IN HIERARCHY (&63)`
	// over a one-sided register answered with 28 and 31 as well (measured 2026-09-16).
	if (!AccountArgumentsReadOnServer(m_accountDr, m_accountCr))
		return false;

	// A ROW CUT BY THE CORRESPONDENT names two accounts, and a stored side keeps one (ComputeTurnover) — and so
	// does a row SELECTED by it: the condition asks the other account of the movement.
	if (m_byCorrespondent || ConditionNamesCorrespondent(m_reg, m_shape, m_filter))
		return false;

	// A BREAKDOWN ASKED FOR BY KIND is read as the slots stand and re-keyed over the read (RegroupServerRead). The
	// credit kinds belong to a paired row only, which this reading does not publish.
	if (!m_kindsCr.empty())
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
	r.m_periodColumn = ibRegValueField(period);
	r.m_from         = m_begin.m_date;
	r.m_to           = m_end.m_date;
	r.m_dropZeroRows = true;   // the RAM oracle says the same: an all-zero row is not a turnover

	// ⭐ PER PERIOD, when periods were asked for — the grain the balance-and-turnovers reading uses, minus
	// its running forms: a turnover of a month is that month's sum and nothing carried. Without it the
	// read folded the interval whole while the shape published a `Period` the answer did not have.
	const bool periodised = m_fold.HasPeriod();
	if (periodised) {
		r.m_grain      = m_fold.IsCalendar() ? ibMaterializeGrain::Calendar : ibMaterializeGrain::StoredPeriod;
		r.m_periodUnit = m_fold.m_unit;
		r.m_fromGrain  = (r.m_from.GetType() == TYPE_DATE && m_fold.IsCalendar())
			? ibValue(ibTruncateToPeriod(r.m_from.GetDateTime(), m_fold.m_unit))
			: r.m_from;
	}

	// ⭐⭐ THE CUT, AND THE HALF OF A BOUNDARY THAT ONLY IT CAN SAY. Whole grains come from the stored
	// rows and each partial end from the movements — and where an end names a DOCUMENT, the recorder's
	// field tuple is compared as an ORDERING, decomposed by the same codec the rows were written
	// through. Three postings sharing one instant are three different answers, and this is what tells
	// them apart.
	ibRegFillArmCut(r, m_reg, m_end, m_begin);

	const ibMaterializeWhen moved = ibMaterializeWhen::InRange;
	std::vector<ibAcctServerFigure> figures;
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		figures.push_back({ FigureField(resource, ibAcctFigure::TurnoverDr), false, ibMaterializeAgg::Value, moved,
		                    FigureName(resource, ibAcctFigure::TurnoverDr) });
		figures.push_back({ FigureField(resource, ibAcctFigure::TurnoverCr), true,  ibMaterializeAgg::Value, moved,
		                    FigureName(resource, ibAcctFigure::TurnoverCr) });
	}

	const wxString innerAlias = alias + wxT("_t");
	std::vector<wxString> keyNames;
	// By kind: read as the slots stand, and re-keyed by the kinds asked (RegroupServerRead). A turnover keeps its
	// turnovers-only subcontos — only a balance leaves them out.
	const ibBackendQueryable* readShape = m_kindsDr.empty() ? this : m_reg->GetShapeQueryable(m_shape, {}, {}, Fold());
	if (readShape == nullptr)
		return nullptr;
	ibQueryRelPtr read = ServerRead(m_reg, readShape, m_shape, r, m_filter, m_kindsDr,
		ServerAccounts(m_reg, this, m_shape, m_accountDr), figures, innerAlias, keyNames);
	if (read != nullptr && !m_kindsDr.empty()) {
		ibAcctRegroup how;
		how.m_asStand   = readShape;
		how.m_published = this;
		how.m_shape     = m_shape;
		how.m_kinds     = m_kindsDr;
		std::vector<ibAcctTakeFigure> takes;
		for (const ibAcctServerFigure& figure : figures)
			takes.push_back({ figure.m_name, figure.m_name, ibAcctTake::AsRead });
		read = RegroupServerRead(m_reg, read, how, takes, keyNames, innerAlias + wxT("_k"));
	}
	if (read == nullptr)
		return nullptr;

	// …and the turnover with no side, which the shape publishes beside the pair: what moved on balance.
	// Left out, a query naming it failed with an unknown column on this road only.
	// …and EMPTY where the row's account keeps no such accounting — which needs the account's row of the
	// chart, joined exactly as the balance reading joins it, and only when some figure depends on it.
	ibQueryRelPtr source = ibSubquery(read, innerAlias);
	const wxString chartAlias = alias + wxT("_a");
	const ibValueMetaObjectChartOfAccounts* chart = m_reg->GetChartOfAccounts();
	const bool judged = AnyFigureKeptByKind(m_reg) && chart != nullptr && chart->GetDataReference() != nullptr
		&& chart->GetQueryable() != nullptr;
	if (judged)
		source = ibJoin(source, ibScan(chart->GetQueryable()->GetQueryTableName(), chartAlias),
			ibRegSameValueIR(account->GetQueryColumn(), innerAlias, chart->GetDataReference()->GetQueryColumn(), chartAlias),
			ibQueryJoinType::Left);
	// …and the breakdown slots to their kinds rows, for a figure kept by subconto (JoinSubcontoKinds).
	std::vector<wxString> subcontos;
	if (AnyFigureKeptByBreakdown(m_reg))
		source = JoinSubcontoKinds(m_reg, source, account->GetQueryColumn(), innerAlias,
			SlotKindsOf(m_reg, this, m_shape, /*creditSide*/ false, m_kindsDr), alias, subcontos);

	std::vector<ibQueryProjItem> projection;
	for (const wxString& name : keyNames)
		projection.push_back({ ibCol(innerAlias, name), name });
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const ibQueryExprPtr kept = BothKept(judged ? KindKeptOnServer(m_reg, resource, chartAlias) : nullptr,
			KeptBySubcontoOnServer(m_reg, resource, subcontos));
		const ibQueryExprPtr turnDr = ibCol(innerAlias, FigureField(resource, ibAcctFigure::TurnoverDr));
		const ibQueryExprPtr turnCr = ibCol(innerAlias, FigureField(resource, ibAcctFigure::TurnoverCr));
		projection.push_back({ FigureWhereKept(kept, turnDr), FigureField(resource, ibAcctFigure::TurnoverDr) });
		projection.push_back({ FigureWhereKept(kept, turnCr), FigureField(resource, ibAcctFigure::TurnoverCr) });
		projection.push_back({ FigureWhereKept(kept, ibBinOp(ibQueryBinOp::Sub, turnDr, turnCr)), FigureField(resource, ibRegFigure::Turnover) });
	}
	return ibSubquery(ibProject(source, std::move(projection)), alias);
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
// from the balance key and merges the rows that then coincide; on the server that is the read re-keyed
// with a test per slot against the kinds table (RegroupServerRead).
//
// But the flag is DATA: if no kind anywhere is marked turnovers-only, that fold is a no-op and the
// read stays as it is — no re-keying, no test per row. Asking costs one row of one small table, and the
// alternative — re-keying wherever a register declares analytics at all — would pay for it on nearly
// every accounting register over a flag almost nobody sets.
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
	// question IS. It decides an OPTIMISATION: whether the read may skip the per-row test of the flags,
	// which answers the same numbers when no flag is set. So every failure means "test them", never "the
	// reading failed".
	//
	// The failure that matters is a lock. A relation is built whenever a query is, including while a
	// configuration is being APPLIED, with a DDL transaction open on another channel. A read issued into
	// that answers with a deadlock, and unguarded it would abort the apply: an optimisation hint killing a
	// restructuring is the wrong thing failing.
	try {
		ibDataQueryBuilder b;
		b.From(rows);
		b.Where(flag->GetQueryColumn(), ibValue(true));
		ibReadPageRequest page;
		page.m_count = 1;   // the existence of ONE such row is the whole answer
		ibDataQueryResult sel = b.Execute(page);
		return sel.Next();
	}
	catch (const ibBackendException&) {
		return true;   // unknown reads as "there is one" — the half that tests every row and loses nothing
	}
}

} // namespace

// ⚠ NOTHING DATA-DEPENDENT IS REMEMBERED HERE. A companion is not call-scoped: the descriptor keeps it by its
// arguments (MakeCompanionFor), so a road remembered on it outlived the flags it was read from — a tick cleared
// on an account and the same report run again stood where it stood before (2026-09-17). The flags are asked of
// the kinds table by the relation itself, per row. Every early return below leaves the RAM road, which answers
// the same numbers.
bool ibAcctBalanceQueryable::CanReadOnServer() const
{

	if (m_reg == nullptr || !m_reg->HasMaterializedViews())
		return false;   // the driver maintains nothing — live aggregation is the only road

	// Both modes — a correspondence register's two surfaces are summed around their reads (SumOfSides).
	// What still belongs to the RAM road on the account arguments is the turnover reading's gate.
	if (!AccountArgumentsReadOnServer(m_accountDr, m_accountCr))
		return false;

	// A BREAKDOWN ASKED FOR BY KIND, and a TURNOVERS-ONLY subconto leaving the key, are the read re-keyed
	// (RegroupServerRead). The credit kinds belong to a paired row only, which this reading does not publish.
	if (!m_kindsCr.empty())
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

	const ibBackendQueryable* chartRows = chart->GetQueryable();
	if (chartRows == nullptr)
		return nullptr;

	ibMaterializeReadSpec r;
	r.m_periodColumn = ibRegValueField(period);
	r.m_to           = m_bound.m_date;   // a balance is open-ended below: everything up to the moment
	r.m_dropZeroRows = true;

	// THE CUT. Whole grains come from the stored rows and the partial end from the movements — and
	// where the moment names a DOCUMENT, the recorder's field tuple is compared as an ORDERING. A
	// balance is open-ended below, so there is no lower boundary to pass.
	ibRegFillArmCut(r, m_reg, m_bound, ibRegBound());

	// ⭐ SUMMED `UpToTo` — the one difference from the turnover reading, and the whole of what makes
	// this a balance. EVERY figure has a balance, and a split one keeps it per side — the stored surface
	// already holds the two sides apart (`…TurnoverDr` / `…TurnoverCr` are what the trigger accumulated).
	//
	// The key is the breakdown grain's (ServerKeys), which is also what makes the fold below per SET OF
	// ANALYTICS, as its note says and as the RAM road folds.
	std::vector<ibAcctServerFigure> figures;
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		figures.push_back({ FigureField(resource, ibAcctFigure::BalanceDr), false, ibMaterializeAgg::Value,
		                    ibMaterializeWhen::UpToTo, FigureName(resource, ibAcctFigure::TurnoverDr) });
		figures.push_back({ FigureField(resource, ibAcctFigure::BalanceCr), true,  ibMaterializeAgg::Value,
		                    ibMaterializeWhen::UpToTo, FigureName(resource, ibAcctFigure::TurnoverCr) });
	}
	if (figures.empty())
		return nullptr;   // nothing to report on this road; the RAM reading says the same, in rows

	const wxString innerAlias = alias + wxT("_t");
	const wxString chartAlias = alias + wxT("_a");

	std::vector<wxString> keyNames;
	// By kind, or with a turnovers-only subconto leaving the balance key: read as the slots stand and re-keyed
	// (RegroupServerRead).
	const bool turnoversOnly = AnyTurnoverOnlyKind(chart);
	const bool rekeys = !m_kindsDr.empty() || turnoversOnly;
	const ibBackendQueryable* readShape = rekeys ? m_reg->GetShapeQueryable(m_shape, {}, {}, Fold()) : this;
	if (readShape == nullptr)
		return nullptr;
	ibQueryRelPtr read = ServerRead(m_reg, readShape, m_shape, r, m_filter, m_kindsDr,
		ServerAccounts(m_reg, this, m_shape, m_accountDr), figures, innerAlias, keyNames);
	if (read != nullptr && rekeys) {
		ibAcctRegroup how;
		how.m_asStand           = readShape;
		how.m_published         = this;
		how.m_shape             = m_shape;
		how.m_kinds             = m_kindsDr;
		how.m_foldTurnoversOnly = turnoversOnly;
		std::vector<ibAcctTakeFigure> takes;
		for (const ibAcctServerFigure& figure : figures)
			takes.push_back({ figure.m_name, figure.m_name, ibAcctTake::AsRead });
		read = RegroupServerRead(m_reg, read, how, takes, keyNames, innerAlias + wxT("_k"));
	}
	if (read == nullptr)
		return nullptr;

	// ⭐⭐ THE JOIN'S `ON` IS NOT SPELLED, IT IS ASKED FOR — both sides through the SAME function, so
	// the equality holds by construction rather than because two suffixes happened to match. Writing
	// `_RRRef` here by hand would be a comparison that compiles, joins nothing, and reports an empty
	// balance that looks exactly like "no movements".
	//
	// LEFT, not inner: an account row that is missing (a chart edited under a posted register) must
	// not make its figures disappear. The CASE below then falls through to "do not fold", which is the
	// answer that loses nothing.
	//
	// 🛑 THE WHOLE REFERENCE, NOT ITS FIRST VALUE FIELD. `ibRegValueField` of a reference is `_RTRef` —
	// the TABLE code, the same on every account of the chart — so `_RTRef = _RTRef` joined each row to
	// every account there is: the one-sided trial balance came back multiplied by the size of the chart
	// (13 accounts, 45 500 read as 591 500), and the fold picked a type per copy (2026-09-16).
	ibQueryExprPtr on = ibRegSameValueIR(account->GetQueryColumn(), innerAlias,
		chart->GetDataReference()->GetQueryColumn(), chartAlias);

	ibQueryRelPtr joined = ibJoin(ibSubquery(read, innerAlias),
		ibScan(chartRows->GetQueryTableName(), chartAlias), on, ibQueryJoinType::Left);

	// …and each breakdown slot to its account's kinds row, for a figure kept by subconto (JoinSubcontoKinds).
	std::vector<wxString> subcontos;
	if (AnyFigureKeptByBreakdown(m_reg))
		joined = JoinSubcontoKinds(m_reg, joined, account->GetQueryColumn(), innerAlias,
			SlotKindsOf(m_reg, this, m_shape, /*creditSide*/ false, m_kindsDr), alias, subcontos);

	// ⭐⭐ THE FOLD, AS A CASE OVER WHAT THE ACCOUNT DECLARES — the same rule the RAM reading applies
	// row by row (FoldSideByAccountType), said once to the server (FoldedPairOnServer):
	//
	//   active         a credit entry REDUCED the debit balance   ->  Dr - Cr , 0
	//   passive        the mirror                                 ->  0 , Cr - Dr
	//   active-passive onto the side its net stands on where the row stands on one set of the account's
	//                  analytics (the slots as they stand, or kinds asked covering every kind it keeps a
	//                  balance along — FullAnalyticsOnServer), so one counterparty that was shipped 100 and
	//                  paid 60 owes 40; a receivable and a payable of two DIFFERENT counterparties are two
	//                  rows and are never netted
	std::vector<ibQueryProjItem> projection;
	for (const wxString& name : keyNames)
		projection.push_back({ ibCol(innerAlias, name), name });

	const ibQueryExprPtr accountType = ibCol(chartAlias, ibRegValueField(chart->GetAccountType()));
	const ibQueryExprPtr apFolds = FullAnalyticsOnServer(m_reg, m_kindsDr, innerAlias, account->GetQueryColumn(), alias + wxT("_fa"));

	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const ibQueryExprPtr grossDr = ibCol(innerAlias, FigureField(resource, ibAcctFigure::BalanceDr));
		const ibQueryExprPtr grossCr = ibCol(innerAlias, FigureField(resource, ibAcctFigure::BalanceCr));
		// …and each of them EMPTY where the account keeps no such accounting, or not by a subconto of the row
		// (ReportFiguresAsKept).
		const ibQueryExprPtr kept = BothKept(KindKeptOnServer(m_reg, resource, chartAlias),
			KeptBySubcontoOnServer(m_reg, resource, subcontos));

		// ⭐ THE GROSS PAIR IS WHAT THE INNER READ ALREADY HOLDS — the sums of each side, before this
		// projection folds them. Published beside the folded pair, so a reader can check a balance
		// against what the two sides actually carry without asking the movements again.
		projection.push_back({ FigureWhereKept(kept, grossDr), FigureField(resource, ibAcctFigure::GrossBalanceDr) });
		projection.push_back({ FigureWhereKept(kept, grossCr), FigureField(resource, ibAcctFigure::GrossBalanceCr) });

		const auto folded = FoldedPairOnServer(accountType, grossDr, grossCr, apFolds);
		projection.push_back({ FigureWhereKept(kept, folded.first),  FigureField(resource, ibAcctFigure::BalanceDr) });
		projection.push_back({ FigureWhereKept(kept, folded.second), FigureField(resource, ibAcctFigure::BalanceCr) });

		// …and the sideless net, from the folded pair: one number, signed, on whichever side it stands.
		projection.push_back({ FigureWhereKept(kept, ibBinOp(ibQueryBinOp::Sub, folded.first, folded.second)), FigureField(resource, ibRegFigure::Balance) });
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
	// Wrapped under the alias here, as every other reading wraps itself — see ibAcctRecordsQueryable.
	const ibQueryRelPtr rel = m_reg->BuildDrCrTurnoverRelation(m_begin, m_end, m_accountDr, m_accountCr,
		m_kindsDr, m_kindsCr, m_filter, m_condition);
	return rel != nullptr ? ibSubquery(rel, alias) : nullptr;
}

ibQueryRamTable ibAcctDrCrTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	ibQueryRamTable table = m_reg->ComputeDrCrTurnover(m_begin, m_end, m_accountDr, m_accountCr, m_kindsDr, m_kindsCr, m_filter, m_condition);
	m_reg->ReportFiguresAsKept(table, m_shape, m_kindsDr, m_kindsCr);
	return table;
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
	// Asked afresh every time — see the balance reading's gate.

	if (m_reg == nullptr || !m_reg->HasMaterializedViews())
		return false;
	if (!AccountArgumentsReadOnServer(m_accountDr, m_accountCr))
		return false;               // see the turnover reading's gate
	if (!m_kindsCr.empty())
		return false;               // the credit kinds belong to a paired row, which this reading does not publish
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
		// have: a month with a balance and no movement is a row with zero turnovers (Max, 2026-09-16).
		// The server read therefore stands on a CALENDAR of the interval's periods, joined to every key
		// (PeriodisedOnServer) — which needs calendar periods, an interval with both ends, a driver that
		// can rank, and a calendar short enough to be spelled into a statement. Anything else is the live
		// path's, which builds the periods itself.
		if (!m_fold.IsCalendar() || m_begin.m_date.GetType() != TYPE_DATE || m_end.m_date.GetType() != TYPE_DATE)
			return false;
		if (ibRegCalendarOf(m_begin.m_date, m_end.m_date, m_fold.m_unit).empty())
			return false;
		ibConnectionScope scope;
		if (!ibCanPushWindow(scope.get()))
			return false;   // no windows on this driver — the RAM road answers exactly as before
	}

	const ibValueMetaObjectChartOfAccounts* chart = m_reg->GetChartOfAccounts();
	if (chart == nullptr || chart->GetAccountType() == nullptr || chart->GetDataReference() == nullptr
	    || chart->GetQueryable() == nullptr)
		return false;
	return true;
}

ibQueryRelPtr ibAcctBalanceAndTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (!CanReadOnServer())
		return nullptr;

	// Per period: every period of the interval for every key — a calendar, see PeriodisedOnServer.
	if (m_fold.HasPeriod()) {
		const ibQueryRelPtr periodised = PeriodisedOnServer(m_reg, this, m_shape, m_begin, m_end, m_fold,
			m_filter, m_accountDr, m_kindsDr, AnyTurnoverOnlyKind(m_reg->GetChartOfAccounts()), alias + wxT("_t"));
		return periodised != nullptr ? ibSubquery(periodised, alias) : nullptr;
	}

	const ibValueMetaObjectAttributeBase*   period  = m_reg->GetRegisterPeriod();
	const ibValueMetaObjectAttributeBase*   account = m_reg->GetRegisterAccount();
	const ibValueMetaObjectChartOfAccounts* chart   = m_reg->GetChartOfAccounts();
	if (period == nullptr || account == nullptr || chart == nullptr)
		return nullptr;

	const ibBackendQueryable* chartRows = chart->GetQueryable();
	if (chartRows == nullptr)
		return nullptr;

	ibMaterializeReadSpec r;
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

	// EITHER END MAY REACH BELOW THE GRAIN — "between this document and that one" is the question, and
	// both ends of it name a moment. Whole days come from the stored rows, each partial end from the
	// movements.
	ibRegFillArmCut(r, m_reg, m_end, m_begin);

	std::vector<ibAcctServerFigure> figures;
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		const wxString fromDr = FigureName(resource, ibAcctFigure::TurnoverDr);
		const wxString fromCr = FigureName(resource, ibAcctFigure::TurnoverCr);

		// The turnover half is reported for EVERY resource — what moved in the interval, periodised or
		// not. Grouped by the period it looks redundant, and it is not: the read carries the history the
		// balances are made of, and a period's turnover summed over all of it would count the part of
		// its first period that lies before the interval (see RenderMaterializedRead, `readsHistory`).
		const ibMaterializeWhen movedWhen = ibMaterializeWhen::InRange;
		figures.push_back({ FigureField(resource, ibAcctFigure::TurnoverDr), false, ibMaterializeAgg::Value, movedWhen, fromDr });
		figures.push_back({ FigureField(resource, ibAcctFigure::TurnoverCr), true,  ibMaterializeAgg::Value, movedWhen, fromCr });

		// …and the balance halves for every figure: a split one keeps its balance per side, which is
		// what these two columns are.
		// ⭐ THE TWO SIDES ACCUMULATE INDEPENDENTLY, and each is a running form of its own — a debit
		// balance is the debit turnovers carried forward, a credit balance the credit ones. The fold
		// by account type happens ABOVE this read (the CASE over the chart's AccountType), so what is
		// asked for here is the pair, not the difference.
		const ibMaterializeAgg opening = periodised ? ibMaterializeAgg::RunningSumExcludingCurrent : ibMaterializeAgg::Value;
		const ibMaterializeAgg closing = periodised ? ibMaterializeAgg::RunningSum                 : ibMaterializeAgg::Value;
		const ibMaterializeWhen openWhen  = periodised ? ibMaterializeWhen::Always : ibMaterializeWhen::BeforeFrom;
		const ibMaterializeWhen closeWhen = periodised ? ibMaterializeWhen::Always : ibMaterializeWhen::UpToTo;

		figures.push_back({ FigureField(resource, ibAcctFigure::OpeningBalanceDr), false, opening, openWhen,  fromDr });
		figures.push_back({ FigureField(resource, ibAcctFigure::OpeningBalanceCr), true,  opening, openWhen,  fromCr });
		figures.push_back({ FigureField(resource, ibAcctFigure::ClosingBalanceDr), false, closing, closeWhen, fromDr });
		figures.push_back({ FigureField(resource, ibAcctFigure::ClosingBalanceCr), true,  closing, closeWhen, fromCr });
	}
	if (figures.empty())
		return nullptr;

	const wxString innerAlias = alias + wxT("_t");
	const wxString chartAlias = alias + wxT("_a");

	// Both sides of a correspondence register, summed by the key — see the balance reading. The period
	// is among the key's names when the read is cut into periods (the read groups by it, and the answer
	// has to say WHICH period).
	std::vector<wxString> keyNames;
	// By kind, or with turnovers-only subcontos, read as the slots stand and re-keyed (RegroupServerRead) — the
	// latter as two arms, the balances by their key and the turnovers along such a subconto by it
	// (BalanceAndTurnoverArms).
	const bool turnoversOnly = AnyTurnoverOnlyKind(chart);
	const bool rekeys = !m_kindsDr.empty() || turnoversOnly;
	const ibBackendQueryable* readShape = rekeys ? m_reg->GetShapeQueryable(m_shape, {}, {}, Fold()) : this;
	if (readShape == nullptr)
		return nullptr;
	ibQueryRelPtr read = ServerRead(m_reg, readShape, m_shape, r, m_filter, m_kindsDr,
		ServerAccounts(m_reg, this, m_shape, m_accountDr), figures, innerAlias, keyNames);
	if (read != nullptr && rekeys) {
		ibAcctRegroup how;
		how.m_asStand   = readShape;
		how.m_published = this;
		how.m_shape     = m_shape;
		how.m_kinds     = m_kindsDr;
		std::vector<ibAcctTakeFigure> balanceArm, turnoverArm;
		for (const ibAcctServerFigure& figure : figures) {
			const bool moved = figure.m_when == ibMaterializeWhen::InRange;
			balanceArm.push_back({ figure.m_name, figure.m_name,
				moved && turnoversOnly ? ibAcctTake::UnlessTurnoversOnly : ibAcctTake::AsRead });
			turnoverArm.push_back({ figure.m_name, figure.m_name, moved ? ibAcctTake::AsRead : ibAcctTake::Zero });
		}
		read = turnoversOnly
			? BalanceAndTurnoverArms(m_reg, read, how, balanceArm, turnoverArm, keyNames, innerAlias + wxT("_k"))
			: RegroupServerRead(m_reg, read, how, balanceArm, keyNames, innerAlias + wxT("_k"));
	}
	if (read == nullptr)
		return nullptr;
	const ibQueryExprPtr keepsBalance = turnoversOnly
		? ibBinOp(ibQueryBinOp::Eq, ibCol(innerAlias, StandsOnTurnoversOnlyName()), ibCast(ibConst(ibValue(0)), ibTypeInteger())) : nullptr;

	// The join, and the reason it is LEFT and compares the WHOLE reference, are the balance reading's.
	ibQueryExprPtr on = ibRegSameValueIR(account->GetQueryColumn(), innerAlias,
		chart->GetDataReference()->GetQueryColumn(), chartAlias);

	ibQueryRelPtr joined = ibJoin(ibSubquery(read, innerAlias),
		ibScan(chartRows->GetQueryTableName(), chartAlias), on, ibQueryJoinType::Left);
	std::vector<wxString> subcontos;   // …and the breakdown slots to their kinds rows — the balance reading's
	if (AnyFigureKeptByBreakdown(m_reg))
		joined = JoinSubcontoKinds(m_reg, joined, account->GetQueryColumn(), innerAlias,
			SlotKindsOf(m_reg, this, m_shape, /*creditSide*/ false, m_kindsDr), alias, subcontos);

	std::vector<ibQueryProjItem> projection;
	for (const wxString& name : keyNames)
		projection.push_back({ ibCol(innerAlias, name), name });

	// The turnovers pass through UNFOLDED — see the note above the gate — and carry their sideless
	// reading with them: what moved on balance.
	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		// …each of them EMPTY where the account keeps no such accounting, or not by a subconto of the row.
		const ibQueryExprPtr kept = BothKept(KindKeptOnServer(m_reg, resource, chartAlias),
			KeptBySubcontoOnServer(m_reg, resource, subcontos));
		const ibQueryExprPtr turnDr = ibCol(innerAlias, FigureField(resource, ibAcctFigure::TurnoverDr));
		const ibQueryExprPtr turnCr = ibCol(innerAlias, FigureField(resource, ibAcctFigure::TurnoverCr));
		projection.push_back({ FigureWhereKept(kept, turnDr), FigureField(resource, ibAcctFigure::TurnoverDr) });
		projection.push_back({ FigureWhereKept(kept, turnCr), FigureField(resource, ibAcctFigure::TurnoverCr) });
		projection.push_back({ FigureWhereKept(kept, ibBinOp(ibQueryBinOp::Sub, turnDr, turnCr)), FigureField(resource, ibRegFigure::Turnover) });
	}

	const ibQueryExprPtr accountType = ibCol(chartAlias, ibRegValueField(chart->GetAccountType()));
	const ibQueryExprPtr apFolds = FullAnalyticsOnServer(m_reg, m_kindsDr, innerAlias, account->GetQueryColumn(), alias + wxT("_fa"));
	// ⭐ THREE READINGS OF ONE MOMENT, projected together: the GROSS pair as the inner read holds it,
	// the folded pair, and the sideless net taken from the folded one — the same three the RAM road
	// fills, so a reading answers the same whichever road it took.
	const auto foldPair = [&](const ibQueryExprPtr& kept, const wxString& debitName, const wxString& creditName,
		const wxString& grossDebitName, const wxString& grossCreditName, const wxString& netName) {
		const ibQueryExprPtr grossDr = ibCol(innerAlias, debitName);
		const ibQueryExprPtr grossCr = ibCol(innerAlias, creditName);
		projection.push_back({ FigureWhereKept(kept, grossDr), grossDebitName });
		projection.push_back({ FigureWhereKept(kept, grossCr), grossCreditName });
		const auto folded = FoldedPairOnServer(accountType, grossDr, grossCr, apFolds);
		projection.push_back({ FigureWhereKept(kept, folded.first),  debitName });
		projection.push_back({ FigureWhereKept(kept, folded.second), creditName });
		projection.push_back({ FigureWhereKept(kept, ibBinOp(ibQueryBinOp::Sub, folded.first, folded.second)), netName });
	};

	for (const auto resource : m_reg->GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		// …and none on a row standing on a turnovers-only subconto: empty, not zero.
		const ibQueryExprPtr kept = BothKept(BothKept(KindKeptOnServer(m_reg, resource, chartAlias),
			KeptBySubcontoOnServer(m_reg, resource, subcontos)), keepsBalance);
		foldPair(kept, FigureField(resource, ibAcctFigure::OpeningBalanceDr), FigureField(resource, ibAcctFigure::OpeningBalanceCr),
			FigureField(resource, ibAcctFigure::OpeningGrossBalanceDr), FigureField(resource, ibAcctFigure::OpeningGrossBalanceCr),
			FigureField(resource, ibRegFigure::OpeningBalance));
		foldPair(kept, FigureField(resource, ibAcctFigure::ClosingBalanceDr), FigureField(resource, ibAcctFigure::ClosingBalanceCr),
			FigureField(resource, ibAcctFigure::ClosingGrossBalanceDr), FigureField(resource, ibAcctFigure::ClosingGrossBalanceCr),
			FigureField(resource, ibRegFigure::ClosingBalance));
	}

	return ibSubquery(ibProject(joined, std::move(projection)), alias);
}

ibQueryRamTable ibAcctBalanceAndTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	ibQueryRamTable table = m_reg->ComputeBalanceAndTurnover(m_begin, m_end, m_accountDr, m_accountCr,
	                                                         m_kindsDr, m_kindsCr, m_filter, m_fold, m_condition);
	m_reg->ReportFiguresAsKept(table, m_shape, m_kindsDr, m_kindsCr);
	return table;
}

ibQueryRelPtr ibAcctRecordsQueryable::GetSourceRelation(const wxString& alias) const
{
	if (!CanReadOnServer())
		return nullptr;
	// ⚠ WRAPPED UNDER THE ALIAS HERE, as every other reading wraps itself. The comment that stood here said
	// the provider does it; the road a dot-walk takes does not — it joins the target tables onto what comes
	// back and qualifies by the alias, so `R.Account.Code` rendered `FROM AccountingRegister1408 LEFT JOIN …
	// ON (AccountingRegister1408_T_4.fld1414_RRRef = …)` and Firebird refused the alias (-206, 2026-09-16).
	const ibQueryRelPtr rel = m_reg->BuildRecordsRelation(m_begin, m_end, m_kindsDr, m_kindsCr, m_filter, m_condition);
	return rel != nullptr ? ibSubquery(rel, alias) : nullptr;
}

ibQueryRamTable ibAcctRecordsQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeRecords(m_begin, m_end, m_kindsDr, m_kindsCr, m_filter, m_condition);
}

// ============================================================================
// The descriptor — one class, five tables
// ============================================================================

// ⭐⭐ THE CONDITIONS OF A CALL ARE WRITTEN IN THE NAMES THE TABLE IS SELECTED BY (Max, 2026-09-16:
// "the conditions must be written in the names the table is selected by"). A balance is selected as `Account`,
// `AccountDimension1`, `Currency`; the matrix as `AccountDr`, `AccountCr`, `CurrencyDr`, `CurrencyCr` — and
// its conditions say the same. They were resolved against the MOVEMENTS, so a balance took `AccountDr` in
// its account condition while publishing `Account`: one account, two names in one call.
//
// The scope is the table's own SHAPE and nothing else: a column the table is not selected by is not
// invented for its conditions (Max: "you can't make up a debit column out of thin air when your selection
// is by account"). The readings compare an account condition by the account's id (ScopeFromAccountCondition), so
// the shape's `Account` and the movements' own column are the same account to them — and a turnover's
// `CorrAccount`, published under the credit account's id, is the credit account to them.
class ibAcctConditionScope : public ibBackendQueryable
{
public:
	ibAcctConditionScope(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape)
		: m_reg(reg), m_shape(shape) {}

	const ibBackendQueryable* Shape() const {
		ibRegFold fold;
		if (m_shape == ibAcctShape::Records)
			fold.m_kind = ibRegGranularity::Record;
		return m_reg->GetShapeQueryable(m_shape, {}, {}, fold);
	}

	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		const ibBackendQueryable* shape = Shape();
		return shape != nullptr ? shape->ResolveColumnByName(name) : nullptr;
	}
	std::vector<const ibBackendQueryColumn*> GetColumns() const override {
		const ibBackendQueryable* shape = Shape();
		return shape != nullptr ? shape->GetColumns() : std::vector<const ibBackendQueryColumn*>();
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		return col != nullptr && ResolveColumnByName(col->GetName()) == col;
	}
	ibBackendQueryProvider& GetProvider() const override {
		const ibBackendQueryable* shape = Shape();
		return shape != nullptr ? shape->GetProvider() : ibBackendQueryable::GetProvider();
	}
	wxString GetQueryTableName() const override {
		const ibBackendQueryable* shape = Shape();
		return shape != nullptr ? shape->GetQueryTableName() : wxString();
	}
	const ibMetaData* GetMetaData() const override { return m_reg->GetMetaData(); }

private:
	const ibValueMetaObjectAccountingRegister* m_reg;
	ibAcctShape                                m_shape;
};

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
// (Its one caller is gone: a surface's column now carries its own caption and picture, so nothing has
//  to look the attribute up to borrow them — see FillExplorerFromShape just below.)

void FillExplorerFromShape(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape,
                           const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
                           const ibRegFold& fold, ibSourceDataObject::ibSourceExplorer& explorer)
{
	const ibBackendQueryable* built = reg != nullptr ? reg->GetShapeQueryable(shape, kindsDr, kindsCr, fold) : nullptr;
	if (built == nullptr)
		return;

	// ⭐⭐ WHAT THE SURFACE PUBLISHES IS WHAT IS SHOWN. This used to swap in the ATTRIBUTE whose id the
	// column carries — a way of borrowing its caption and its picture back when a surface column had
	// neither. It has both now (ibRegAttributeColumn hands them over at publication), and the swap had
	// become a lie: a surface column is not always an attribute of the movements under the same name,
	// and the swap published `Currency` where the surface has `CurrencyDr`, and drew the pair with two
	// different pictures (Max saw it in the query constructor, 2026-09-16).
	for (const ibBackendQueryColumn* col : built->GetColumns())
		if (col != nullptr)
			explorer.AppendColumn(col);
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
		leaf.m_col    = accountAttr->GetQueryColumn();
		leaf.m_op     = ibQueryFilterOp::In;
		leaf.m_values = named;
		leaf.m_unfold = ibQueryDimUnfold::Hierarchy;
		return ibQueryPredicate::Leaf(leaf);
	};
	call.m_accountDr = conditionFromValue(ibRegArg(paParams, lSizeArray, layout.m_accountDr), reg->GetRegisterAccount());
	call.m_accountCr = conditionFromValue(ibRegArg(paParams, lSizeArray, layout.m_accountCr), reg->GetRegisterAccountCr());
	call.m_kindsDr   = ibAcctReadKinds(ibRegArg(paParams, lSizeArray, layout.m_kindsDr));
	call.m_kindsCr   = ibAcctReadKinds(ibRegArg(paParams, lSizeArray, layout.m_kindsCr));

	// ⭐ A KIND PAST THE LAST SLOT ASKS FOR A FIELD THAT IS NOT THERE. The register keeps as many breakdown
	// values per line as it has slots; a fourth kind of a three-slot register would be read from a fourth
	// field nobody declared. Said the way any query hears about a field that does not exist — naming the
	// field and the table — rather than answered with a column fewer (BreakdownWidth).
	const unsigned int slots = reg->GetAccountDimensionCount();
	const auto refuseBeyondSlots = [&](const std::vector<ibValue>& kinds, bool creditSide) {
		if (kinds.size() <= slots)
			return;
		const wxString field = creditSide && !PairedRow(reg, shape)
			? ibValueMetaObjectAccountingRegister::CorrAccountDimensionColumnName(slots + 1)
			: ibValueMetaObjectAccountingRegister::AccountDimensionColumnName(SidePrefix(reg, shape, creditSide), slots + 1);
		ibBackendQueryNameException::Error(_("unknown attribute '%s' on source '%s'"),
			field, reg->GetName() + wxT(".") + ShapeWord(shape));
	};
	refuseBeyondSlots(call.m_kindsDr, /*creditSide*/ false);
	refuseBeyondSlots(call.m_kindsCr, /*creditSide*/ true);
	// The Structure a script passes becomes the condition right here, at the door — everything below
	// sees a predicate, and the same converter serves the query road.
	call.m_filter    = ibRegFilterPredicate(reg, ibRegArg(paParams, lSizeArray, layout.m_condition));
	// …and the same condition kept RAW, because its other half — the entries keyed by a KIND — is a
	// question about the slots, and which slots depends on the side the reading is passing over.
	call.m_condition = ibRegArg(paParams, lSizeArray, layout.m_condition);
	call.m_fold      = ibReadRegisterFold(ibRegArg(paParams, lSizeArray, layout.m_periodicity));

	// ⭐⭐ EVERY SLOT THE LAYOUT DECLARES IS READ HERE — with one that is declared and answers nothing, and
	// says so. The FILL METHOD of balance-and-turnovers is the reference's argument and keeps its place in
	// the call, but the owner's rule made its two words one answer (2026-09-16): every period of the
	// interval where a balance stands or something moved is a row, whichever is named. So it is not read,
	// and nothing downstream carries a flag that changes nothing.
	return call;
}

const ibBackendQueryable* ibAcctSourceDescriptor::GetConditionScope() const
{
	// The table's SHAPE (see ibAcctConditionScope) — metadata only, so it exists before the call's
	// companion does, and it is what the companion itself publishes.
	if (m_reg == nullptr)
		return nullptr;
	if (m_conditionScope == nullptr)
		m_conditionScope = std::make_shared<ibAcctConditionScope>(m_reg, m_shape);
	return m_conditionScope.get();
}

const ibBackendQueryable* ibAcctSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray,
                                                                  const std::vector<ibQueryPredicatePtr>& conditions,
                                                                  const ibQueryReadColumns& read)
{
	// The layout says which slot each condition came from — the same layout the call is read by, so
	// the two cannot drift.
	const ibAcctArgs layout = ibAcctArgs::For(m_shape, m_reg != nullptr && m_reg->IsCorrespondence());
	const auto at = [&conditions](int slot) -> ibQueryPredicatePtr {
		return slot >= 0 && static_cast<size_t>(slot) < conditions.size() ? conditions[slot] : nullptr;
	};

	m_pendingAccountDr = at(layout.m_accountDr);
	m_pendingAccountCr = at(layout.m_accountCr);
	m_pendingCondition = at(layout.m_condition);
	m_pendingRead      = read;
	const ibBackendQueryable* q = CreateQueryable(paParams, lSizeArray);
	m_pendingAccountDr.reset();
	m_pendingAccountCr.reset();
	m_pendingCondition.reset();
	m_pendingRead      = ibQueryReadColumns();
	return q;
}

namespace {

// ⭐ DOES THE QUERY READ THE CORRESPONDENT OF A TURNOVER — any of the columns the shape publishes for it
// (GetShapeQueryable): the account, its breakdown, the other half of a field kept per side, the figures
// it moved. Named through the same spellings the shape is built with. One of them read, and the rows are
// cut by the correspondent; none, and a turnover of 62 stays one row.
bool ReadsCorrespondent(const ibValueMetaObjectAccountingRegister* reg, const std::vector<ibValue>& kindsCr,
                        const ibQueryReadColumns& read)
{
	using Reg = ibValueMetaObjectAccountingRegister;
	if (read.m_all)
		return true;
	if (read.Reads(Reg::CorrAccountColumnName()))
		return true;
	for (unsigned int no = 1; no <= BreakdownWidth(reg, kindsCr); no++)
		if (read.Reads(Reg::CorrAccountDimensionColumnName(no)) || read.Reads(Reg::CorrAccountDimensionColumnName(no) + wxT("Kind")))
			return true;
	for (const auto dimension : reg->GetDimensionArrayObject())
		if (dimension != nullptr && read.Reads(Reg::CorrFieldColumnName(dimension->GetName())))
			return true;
	for (const auto resource : reg->GetResourceArrayObject())
		if (resource != nullptr)
			for (const wxString& suffix : { wxString(ibRegFigure::CorrTurnover), ibAcctFigure::CorrTurnoverDr, ibAcctFigure::CorrTurnoverCr })
				if (read.Reads(FigureName(resource, suffix)))
					return true;
	return false;
}

} // namespace

const ibBackendQueryable* ibAcctSourceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	ibAcctCallArgs call = ibAcctParseCall(m_reg, m_shape, paParams, lSizeArray);

	// ⚠ A CONDITION THAT CAME THROUGH THE OTHER ENTRANCE WINS, and only if there is one. Its slot in
	// paParams carries an empty value (the lowering put one there so the positions still line up), so
	// the parse above found nothing to build from — but a SCRIPT call has no second entrance at all,
	// and its value-built condition must survive. Overwriting unconditionally would erase it.
	// ⭐⭐ AN ACCOUNT ARGUMENT IS TWO THINGS, and only one of them is a fold. The plain naming of the account —
	// `Account IN HIERARCHY (&A)`, `Account = &B` — says which accounts the rows are about and which one each is
	// reported under; the rest of what was written there (`NOT Account.OffBalance`, an OR, a walk into the
	// chart) SELECTS among them, and joins the condition, which every pass applies before it folds.
	const ibAcctConditionScope* scope = static_cast<const ibAcctConditionScope*>(GetConditionScope());
	const auto takeAccounts = [&](const ibQueryPredicatePtr& written, const ibValueMetaObjectAttributeBase* account,
	                              const wxString& publishedName, ibQueryPredicatePtr& into) {
		if (!written || account == nullptr)
			return;
		const ibBackendQueryColumn* published = scope != nullptr ? scope->ResolveColumnByName(publishedName) : nullptr;
		const auto namesAccount = [&](const ibQueryCondition& leaf) {
			if (leaf.m_col == nullptr || !leaf.m_path.empty() || leaf.m_expr || leaf.m_semiJoin)
				return false;
			if (leaf.m_op != ibQueryFilterOp::Equal && leaf.m_op != ibQueryFilterOp::In)
				return false;
			const ibMetaID id = leaf.m_col->GetColumnId();
			return id == account->GetMetaID() || (published != nullptr && id == published->GetColumnId());
		};
		ibQueryPredicatePtr accounts, selection;
		SplitAccountCondition(written, namesAccount, accounts, selection);
		into          = accounts;
		call.m_filter = AndWith(call.m_filter, selection);
	};
	const bool symmetricSides = m_shape == ibAcctShape::DrCrTurnovers;
	if (m_pendingAccountDr)
		takeAccounts(m_pendingAccountDr, m_reg->GetRegisterAccount(),
			PublishedAccountName(m_reg, m_shape), call.m_accountDr);
	if (m_pendingAccountCr && m_reg->GetRegisterAccountCr() != nullptr)
		takeAccounts(m_pendingAccountCr, m_reg->GetRegisterAccountCr(),
			symmetricSides ? m_reg->GetRegisterAccountCr()->GetName() : ibValueMetaObjectAccountingRegister::CorrAccountColumnName(),
			call.m_accountCr);

	// …and the CONDITION itself, written against the reading's columns, applied inside it.
	call.m_filter = AndWith(call.m_filter, m_pendingCondition);

	// Built and KEPT by the base — the same call gives the same object back, and a query that reads
	// this table twice keeps both alive. ⚠ THE CONSUMED CONDITIONS ARE PART OF THE CALL: their slots in
	// paParams are empty, so a key of paParams alone handed a query with one account the companion
	// built for another (MakeCompanionFor, queryableFactory.h).
	const std::vector<ibQueryPredicatePtr> consumed{ m_pendingAccountDr, m_pendingAccountCr, m_pendingCondition };
	switch (m_shape) {
	case ibAcctShape::Balance:
		return MakeCompanionFor<ibAcctBalanceQueryable>(consumed, paParams, lSizeArray, m_reg,
			call.m_begin, call.m_accountDr, call.m_accountCr, call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
	case ibAcctShape::Turnovers: {
		// ⚠ WHETHER THE ROWS ARE CUT BY THE CORRESPONDENT IS PART OF THE CALL — two queries over the same
		// arguments, one reading CorrAccount and one not, are two different readings. Said to the companion
		// store as one more argument, after the ones the call wrote.
		const bool byCorrespondent = m_reg->IsCorrespondence() && ReadsCorrespondent(m_reg, call.m_kindsCr, m_pendingRead);
		std::vector<ibValue> args;
		for (long i = 0; i < lSizeArray; ++i)
			args.push_back(paParams != nullptr && paParams[i] != nullptr ? *paParams[i] : ibValue());
		args.push_back(ibValue(byCorrespondent));
		std::vector<ibValue*> argPtrs;
		for (ibValue& arg : args)
			argPtrs.push_back(&arg);
		return MakeCompanionFor<ibAcctTurnoverQueryable>(consumed, argPtrs.data(), static_cast<long>(argPtrs.size()), m_reg,
			call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
			call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_fold, call.m_condition, byCorrespondent);
	}
	case ibAcctShape::DrCrTurnovers:
		return MakeCompanionFor<ibAcctDrCrTurnoverQueryable>(consumed, paParams, lSizeArray, m_reg,
			call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
			call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
	case ibAcctShape::BalanceAndTurnovers:
		return MakeCompanionFor<ibAcctBalanceAndTurnoverQueryable>(consumed, paParams, lSizeArray, m_reg,
			call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
			call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_fold, call.m_condition);
	case ibAcctShape::Records:
		return MakeCompanionFor<ibAcctRecordsQueryable>(consumed, paParams, lSizeArray, m_reg,
			call.m_begin, call.m_end, call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
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
			    "the reading could never do. Any condition on the account is taken - NOT, OR, "
			    "`NOT Account.OffBalance`, `Account.Code LIKE \"6%\"` - and selects the rows before "
			    "they are folded.")
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
		push(wxT("AccountDimensionsCr"), kindType,
			_("The same, for the credit side of the matrix."));

	if (layout.m_condition >= 0 && symmetricSides)
		ibAppendRegisterConditionParameter(out);
}

// A READING IS FILTERED BY ITS DIMENSIONS AND ITS BREAKDOWN, never by a resource. A resource is what
// the table folded, and a condition over a fold belongs to the RESULT rather than to an argument of
// the source.
//
// ⭐⭐ EVERY SLOT ASKS ITS OWN QUESTION, so every slot gets its own list — the overload below. This
// one answers for the general `Condition`: the analytics VALUES and the dimensions.
//
// Both halves of that were once wrong in opposite directions. The dimensions were the only thing
// offered, so the general slot was empty of the analytics a filter is normally written with; then the
// account was added here as well, and it does not belong — an account has parameters of its own
// (`AccountCondition`, `…Dr` / `…Cr`, `CorrAccountCondition`), and what is written THERE is not a
// predicate but the hierarchy SCOPE: it decides which accounts are admitted and which one each row
// is reported under. A field that is a scope in one slot and a filter in another is one question
// with two answers, honoured by two different mechanisms.
void ibAcctSourceDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_reg == nullptr)
		return;

	// ⭐⭐ WHAT THE TOTALS TABLE ACTUALLY HOLDS — the accounts, their analytics and the dimensions, and
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
	// THE ANALYTICS VALUES — the breakdown itself, per position and per side.
	//
	// ⚠ THE KINDS ARE NOT OFFERED HERE. A kind says what a slot is FILED UNDER, and choosing rows by
	// it is a question of one particular reading, not of an ordinary condition — it has its own place
	// and its own arguments. Offering it beside the values invites a filter that reads as "rows whose
	// third slot happens to hold a Contract", which is a different question from "rows about THIS
	// contract" and is almost never the one being asked.
	//
	// ⭐ IN THE NAMES THE TABLE IS SELECTED BY — its own columns, asked of its shape by the names the shape
	// gives them: `AccountDimension1` and `Currency` in a balance, `AccountDimensionDr1` / `…Cr1` and
	// `CurrencyDr` / `CurrencyCr` in the matrix. The movements' spelling was offered here, and a balance
	// offered a filter over `AccountDimensionDr1` its rows do not have.
	const ibAcctConditionScope* scope = static_cast<const ibAcctConditionScope*>(GetConditionScope());
	if (scope == nullptr)
		return;
	const bool paired = PairedRow(m_reg, m_shape);
	const auto offer = [&explorer, scope](const wxString& name) {
		if (const ibBackendQueryColumn* column = scope->ResolveColumnByName(name))
			explorer.AppendColumn(column, /*enabled*/ true, /*visible*/ true);
	};
	for (const bool creditSide : { false, true }) {
		if (creditSide && !paired)
			break;
		for (unsigned int idx = 0; idx < m_reg->GetAccountDimensionCount(); idx++)
			offer(ibValueMetaObjectAccountingRegister::AccountDimensionColumnName(SidePrefix(m_reg, m_shape, creditSide), idx + 1));
	}

	// ⚠ AND THE DIMENSIONS, which are NOT in the attribute list: a dimension is its own metaclass
	// (g_metaDimensionCLSID), so a walk over attributes misses them. Leaving them out is what emptied
	// the ordinary `Condition` slot of everything a filter is normally written with. A row about both
	// sides is selected by both sides of a dimension kept per side, a row about one account by the field.
	for (const ibValueMetaObjectAttributeBase* dimension : m_reg->GetDimensionArrayObject()) {
		if (dimension == nullptr)
			continue;
		const ibValueMetaObjectAttributeBase* debit  = paired ? m_reg->GetFieldSide(/*creditSide*/ false, dimension) : nullptr;
		const ibValueMetaObjectAttributeBase* credit = paired ? m_reg->GetFieldSide(/*creditSide*/ true, dimension) : nullptr;
		if (debit != nullptr && credit != nullptr) {
			offer(debit->GetName());
			offer(credit->GetName());
		}
		else
			offer(dimension->GetName());
	}

	// ⭐ …AND THE CORRESPONDENT'S HALF of a turnover row: its analytics and its side of a dimension kept per
	// side (`CorrAccountDimension1`, `CurrencyCorr`) — columns of the table like any other, so a condition may
	// name them; one that does reads the correspondent, and the rows are cut by it (ibQueryReadColumns). Its
	// ACCOUNT stays in its own slot (`CorrAccountCondition`), for the reason the account does above.
	if (m_shape == ibAcctShape::Turnovers && m_reg->IsCorrespondence()) {
		for (unsigned int idx = 0; idx < m_reg->GetAccountDimensionCount(); idx++)
			offer(ibValueMetaObjectAccountingRegister::CorrAccountDimensionColumnName(idx + 1));
		for (const ibValueMetaObjectAttributeBase* dimension : m_reg->GetDimensionArrayObject())
			if (dimension != nullptr && m_reg->GetFieldSide(/*creditSide*/ true, dimension) != nullptr)
				offer(ibValueMetaObjectAccountingRegister::CorrFieldColumnName(dimension->GetName()));
	}
}

// ⭐⭐ THE ACCOUNT SLOTS ARE ABOUT THE ACCOUNT — so the account is all they are offered.
//
// `AccountCondition` / `…Dr` / `…Cr` / `CorrAccountCondition` are CONSUMED by this source: the plain
// naming of the account written there becomes the hierarchy scope that decides which one each row is
// reported under (ScopeFromAccountCondition), and whatever else is said about the account — through it
// (`Account.OffBalance`), under NOT, in an OR — selects the rows before the fold (SplitAccountCondition).
//
// The general `Condition` is the place for everything else, and it answers with the list above.
void ibAcctSourceDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
                                                   const wxString& slot) const
{
	if (m_reg == nullptr)
		return;

	if (!slot.Contains(wxT("AccountCondition")) && !slot.Contains(wxT("CorrAccountCondition"))) {
		FillConditionExplorer(explorer);   // the general condition — dimensions and the analytics values
		return;
	}

	// Which side this slot is about is written in its own name: the matrix names Dr and Cr, a
	// one-figure turnover filtered by its counterpart says Corr, and a one-sided register says
	// neither. Reading the name is not a lettering trick — these are the slot names this same
	// descriptor publishes in DescribeParameters, a few lines up.
	//
	// ⭐ AND THE ACCOUNT IS OFFERED BY THE NAME THE TABLE IS SELECTED BY: `Account` in a balance or a
	// turnover, `AccountDr` / `AccountCr` in the matrix. A table that is not selected by a credit account
	// is not offered one; the corresponding-account slot of a turnover offers `CorrAccount`.
	const bool corrSlot   = slot.Contains(wxT("CorrAccountCondition"));
	const bool creditSide = !corrSlot && slot.Contains(wxT("Cr"));
	const ibAcctConditionScope* scope = static_cast<const ibAcctConditionScope*>(GetConditionScope());
	if (scope == nullptr)
		return;
	const ibValueMetaObjectAttributeBase* const accountCr = m_reg->GetRegisterAccountCr();
	const wxString name = corrSlot ? (m_shape == ibAcctShape::Turnovers ? ibValueMetaObjectAccountingRegister::CorrAccountColumnName() : wxString())
		: !creditSide ? PublishedAccountName(m_reg, m_shape)
		: (PairedRow(m_reg, m_shape) && accountCr != nullptr ? accountCr->GetName() : wxString());
	if (const ibBackendQueryColumn* column = name.IsEmpty() ? nullptr : scope->ResolveColumnByName(name))
		explorer.AppendColumn(column, /*enabled*/ true, /*visible*/ true);
}

