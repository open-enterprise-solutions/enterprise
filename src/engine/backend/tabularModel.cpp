////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value-model base (shared) + the composer node
////////////////////////////////////////////////////////////////////////////
//
// The SHARED half of the value-model: the ibValueModel base (settings / selection / actions / iteration /
// column collection) + the ibComposerNode out-of-line members. The two paged-fetch realisations live in their
// own translation units — the DB fetch in tabularModelDb.cpp (ibValueModelCursor::RunComposerPage), the in-place RAM
// fetch + composer in tabularModelRam.cpp (ibValueModelStorage::RunComposerPage / ibDataRamComposer).

#include "tabularModel.h"

#include "backend/session/session.h"            // ibSession — the caller a rented run borrows from
#include "backend/job/jobManager.h"             // ibJobManager / ibJobTenancy — the rented run a portion reads on
#include "backend/backend_exception.h"          // ibBackendException — a refusal to rent falls back to inline
#include "backend/tabularModelView.h"           // ibDataViewModel / ibDataViewItem — the base view types
#include "backend/composition/dataComposer.h"  // ibDataComposer — IsGroupedModel reads GroupCount() off the composer
#include "backend/uniqueKey.h"                  // ibUniqueKey — GetItemKey return (base default = no key)

#include <deque>   // GetCompositionAttrByRow — the values a condition is handed stay put while it is read

// (ibValueModelRamTreeBase::PopulateFromTree + its MirrorQueryNodes helper were DELETED with the dead
// RamTreeBase class — the in-memory mirror tree is superseded by hierarchy GROUPING over the composer.)


// ---------------------------------------------------------------------------
// ibComposerNode composer-fetch ctors + SetValue (in-memory source unification). ibComposerNode
// collapsed into the universal row (Max: "collapse …"); these out-of-line members are non-trivial, so they
// live here rather than in the header.

// DETAIL row (a DB-list fetch copy): copy the L5 driver row's values + keep the primary-key row-key as the
// re-fetch selection identity. NO write-back source / backing index any more — a DB grid is READ-ONLY (editing
// is via the object form), and a RAM list edits its LIVE storage rows directly (it never makes these copies).
ibValueModel::ibComposerNode::ibComposerNode(const std::map<ibMetaID, ibValue>& values,
	bool container, std::vector<ibValue> rowKey, std::vector<ibValue> scope)
	: m_valueTable(nullptr), m_groupPath(std::move(scope)), m_rowKey(std::move(rowKey)),
	  m_container(container), m_selfContained(true)
{
	for (const auto& kv : values)
		AppendTableValue(kv.first, kv.second);
}

// STUB row (FindRowValue selection-restore): only the row-key the caller navigates to.
ibValueModel::ibComposerNode::ibComposerNode(std::vector<ibValue> rowKey)
	: m_valueTable(nullptr), m_rowKey(std::move(rowKey)), m_selfContained(true)
{
}

// GROUP-drill row: the dimension-value path root->this + the drillable container flag.
ibValueModel::ibComposerNode::ibComposerNode(const std::map<ibMetaID, ibValue>& values,
	const std::vector<ibValue>& groupPath, bool container, std::vector<ibValue> subPath)
	: m_valueTable(nullptr), m_groupPath(groupPath), m_subPath(std::move(subPath)), m_heading(true),
	  m_container(container), m_selfContained(true)
{
	for (const auto& kv : values)
		AppendTableValue(kv.first, kv.second);
}

// THE node mirrors an edited cell into its own value map and notifies the owning model. A RAM list edits its
// LIVE storage rows THROUGH this (m_valueTable set → the storage IS updated + the model refreshes); a DB-list
// COPY node (m_valueTable null, self-contained) just mirrors into the copy for the inline editor (the grid is
// read-only — a real edit goes through the object form). No write-back source / backing index any more.
bool ibValueModel::ibComposerNode::SetValue(const ibMetaID& id, const ibValue& variant, bool notify)
{
	auto iterator = m_nodeValues.find(id);
	if (iterator == m_nodeValues.end())
		return false;
	ibValue& cValue = m_nodeValues.at(id);

	// 🛑 THE VALUE FIRST, THE NEWS AFTER IT. The notification went out while the cell still held the OLD
	// value, and whatever reacts to it — the view recomputing the row, a handler reading the cell — read
	// that. A cell being edited hides it, since its editor shows the chosen value by itself; a NEIGHBOUR
	// emptied by a link did not: the kind changed, the value cell was cleared to `False`, and the grid kept
	// drawing the old blank (2026-09-23: the row held `False` while the screen showed an empty cell).
	const bool changed = cValue != variant;
	cValue = variant;
	if (notify && m_valueTable != nullptr && changed)
		m_valueTable->RowValueChanged(this, id);
	return true;
}


// Out-of-line (declared in tabularModel.h): driving the settings in needs the COMPOSER's complete
// type, which the header has, and the DESCRIPTION's, which it also has — but the call belongs
// beside the rest of the model's behaviour rather than inline in a header everything includes.
ibValueModel::ibValueModel()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
	m_modelProvider(nullptr)
{
	m_modelProvider = new ibDataViewModelProviderImpl(this);
	m_methodHelperReturnLine.Bind(this, &ibValueModel::DescribeReturnLine);

	// (A MODEL DEALS IN SETTINGS, and it asks nobody for them: the COMPOSER holds the one in force,
	//  and the base forwards. The whole schema — query, main table, variants, structure — is what a
	//  LIST is made of and is never seen from here.)

	// (m_composer is POLYMORPHIC + lazily created, by which time the full subclass exists so
	// CreateComposer picks ibDataDBComposer (DB) / ibDataRamComposer (RAM).
	// Subclasses set their source + default sort/grouping STRAIGHT on it in their ctor —
	// GetModelComposer().FromSource(q).Sort(...) — the persistent settings store the fetch reads.)
}

ibValue::ibMemberTable* ibValueModel::ibValueModelReturnLine::DoGetPMethods() const
{
	const ibValueModel* model = GetOwnerModel();
	return model != nullptr ? &model->m_methodHelperReturnLine : nullptr;
}

void ibValueModel::DescribeReturnLine(ibMemberTable& helper) const
{
	const ibValueModelColumnCollection* columns = GetColumnCollection();
	if (columns == nullptr)
		return;
	for (unsigned int idx = 0; idx < columns->GetColumnCount(); ++idx)
		if (const ibValueModelColumnCollection::ibValueModelColumnInfo* column = columns->GetColumnInfo(idx))
			helper.AppendProp(column->GetColumnName(), column->GetColumnID());
}

ibComposerNode* ibValueModelStorage::NewRow() const
{
	if (!m_newRow) {
		ibNewRowColumns columns;
		DescribeNewRow(columns);
		m_newRow.reset(new ibComposerNode());
		m_newRowAnew.clear();
		for (auto& column : columns) {
			ibValue empty = column.second();
			// Shared only what no row can write into — a primitive, a reference, an enumeration value, asked of the
			// value's kind. Anything else (an array, a structure, a table) is one of its own for each row: a row that
			// wrote into a shared one wrote into every row's.
			const ibClassID clsid = empty.GetClassType();
			if (::IsPrimitive(clsid) || ::IsReference(clsid) || ::IsEnum(clsid))   // the clsid kinds, not ibValue's own
				m_newRow->AppendTableValue(column.first, std::move(empty));
			else
				m_newRowAnew.push_back(std::move(column));
		}
	}
	ibComposerNode* row = new ibComposerNode(*m_newRow);
	for (const auto& column : m_newRowAnew)
		row->AppendTableValue(column.first, column.second());
	return row;
}

ibValueModel::~ibValueModel()
{
	// A READ MUST NOT OUTLIVE WHAT IT IS READING. The rented run walks this model
	// from a worker, and nothing else keeps the model alive: the control's alive
	// token answers for the CONTROL, and dropping a form (or AssociateModel) frees
	// the model outright. So the last read is waited out here — the same promise
	// the base door keeps by joining its thread.
	//
	// Cheap in the ordinary case: by the time a form closes its portion has long
	// since landed, and a run that has finished returns from Wait immediately.
	//
	// ⚠ AND HERE IS TOO LATE FOR WHAT A SUBCLASS OWNS: a base destructor runs after the derived
	// parts are gone, so a model whose read walks its own members waits in its OWN destructor
	// (ibValueDynamicList, ibValueDataComposition). This one is the last line for the rest.
	CancelFetch();

	// The RAM node storage (ibRamValueStorage) is a member of ibValueModelStorage — its dtor DecRefs the nodes; the
	// base just drops the view provider. (A DB model has no node storage.)
	m_modelProvider->DecRef();
}

// ⭐ SAY STOP OUT LOUD. The destructor has always done this, but a form CLOSING is not the same moment
// as the model dying: a report still composing holds references of its own, so the window goes and the
// read carries on against a session nobody is watching. A control that starts a read is the one that
// tells it to stop when its window is going away (Max, 2026-08-19: "it has to understand it must break
// off, forcibly").
//
// Cooperative and blocking, in that order: raise the cancel flag, then wait the run out — a read stops
// at its next row, and returning before it did would let the worker walk a model the caller is about to
// release.
void ibValueModel::CancelFetch()
{
	if (!m_fetchRun)
		return;
	m_fetchRun->Cancel();   // cooperative — heard at the read's next row
	m_fetchRun->Wait();
	m_fetchRun.reset();
}

// (Header-click sort lives on the FRONT — ibValueModelTableBox::OnColumnClick pokes this model's composer
//  directly (ClearSorts + Sort by the column's own bound field name) + RefetchAll. No SortBy on the model.)

// (GetSortModels DELETED — ibSortModel is gone. The sort is L5 (the composer, by field NAME); the few
//  consumers that needed a {col-id, asc} pair — the keyset anchor + the frontend header arrows — now read
//  m_composer.GetSortAt + GetColumnIDByName inline, each at the point of use. Max: "ibSortModel is either part
//  of L5, or removed entirely" — L5 is name-based, so it was removed.)

void ibValueModel::SubmitFetchAsync(std::function<void()> work)
{
	if (!work)
		return;

	// Under the door's own lock — one portion at a time. The lock lives on the view
	// side of this model (its provider bridge IS the ibDataViewModel), which is the
	// one object both halves of a table share.
	auto locked = GetDataViewModel()->GuardFetch(std::move(work));

	// The rented run — see the header for what is borrowed and what is not. The
	// try/catch IS the fallback: StartBackground refuses out loud (no session to
	// rent from, no free connection within the tenant's short wait, the manager
	// stopping), and a refusal here means "read it here instead", never "no data".
	if (ibJobManager* const jobs = ibApplicationInstance::GetJobManager()) {
		try {
			// The handle is KEPT, not dropped: it is what the destructor waits on so
			// a read cannot outlive this model. One at a time by construction — the
			// door's lock already serialises them — so one slot is enough.
			m_fetchRun = jobs->StartBackground(
				[locked](ibSession*) -> ibValue { locked(); return ibValue(); },
				_("reading table data"),
				ibJobTenancy::Tenant);
			return;
		}
		catch (const ibCoreException&) {
			// Nothing to rent — fall through.
		}
	}

	locked();
}

namespace {
// Cursor-paginated iteration over any flat paged model. Drives the
// model's GetFirstFetch / GetNextFetch surface in fixed-size batches
// and wraps each item via GetRowAt — the same factory the GUI uses to
// turn an ibDataViewItem into a script-visible ReturnLine. RAM-paged
// children pick up filter+sort consistency for free (their Get*Fetch
// slices BuildVisibleView). DB-paged lists become script-iterable
// without any per-class override.
class ibValueModelPagedIteratorState : public ibValueIteratorState {
public:
	explicit ibValueModelPagedIteratorState(ibValueModel* model, int batchSize = 64)
		: m_model(model), m_batchSize(batchSize),
		  m_pos(0), m_started(false), m_exhausted(false) {}

	bool MoveNext(ibValue& current) override {
		if (!m_started) {
			m_started = true;
			m_model->GetFirstFetch(ibDataViewItem(), ibDataViewItem(),
				m_batchSize, m_batch);
			m_pos = 0;
			if (m_batch.size() < static_cast<size_t>(m_batchSize))
				m_exhausted = true;
		} else {
			++m_pos;
			if (m_pos >= m_batch.size()) {
				if (m_exhausted) return false;
				ibDataViewItem anchor = m_batch.empty()
					? ibDataViewItem()
					: m_batch[m_batch.size() - 1];
				ibDataViewItemArray nextBatch;
				m_model->GetNextFetch(ibDataViewItem(), anchor,
					m_batchSize, nextBatch);
				if (nextBatch.empty()) {
					m_exhausted = true;
					m_batch.Clear();
					return false;
				}
				if (nextBatch.size() < static_cast<size_t>(m_batchSize))
					m_exhausted = true;
				m_batch = std::move(nextBatch);
				m_pos = 0;
			}
		}
		if (m_pos >= m_batch.size()) return false;

		auto* line = m_model->GetRowAt(m_batch[m_pos]);
		if (line == nullptr) {
			current = ibValue();
		} else {
			current = ibValue(static_cast<ibValue*>(line));
		}
		return true;
	}

	void Reset() override {
		m_batch.Clear();
		m_pos = 0;
		m_started = false;
		m_exhausted = false;
	}

	bool PeekSample(ibValue& current) const override {
		current = m_model->GetEmptyRow();
		// Default GetEmptyRow returns ibValue() (TYPE_EMPTY).
		// Concrete model children override with a typed ReturnLine
		// wrapped as TYPE_REFFER.
		return current.m_typeClass != ibValueTypes::TYPE_EMPTY;
	}

private:
	ibValueModel* m_model;
	int m_batchSize;
	ibDataViewItemArray m_batch;
	size_t m_pos;
	bool m_started;
	bool m_exhausted;
};

// ⭐⭐ A TABLE NOBODY HAS FILTERED, SORTED OR GROUPED IS WALKED IN PLACE — the display order IS the
// storage order, so there is nothing to compute and nothing to look for.
//
// The paged state above exists because a list is a WINDOW onto something arranged: filtered, sorted,
// possibly grouped, possibly a database away. Every batch of it asks the composer for the order of
// the whole table (ComputeOrder), finds the anchor by scanning the rows for it, then scans the order
// for that index — three passes over everything, once per 64 rows. Over a table that arrangement
// does not apply to, all of it computes the sequence 0, 1, 2, …
//
// Measured on this base, 2026-09-08: a 20 000-row answer took ELEVEN SECONDS to count and twelve to
// walk, while building it took under one. That is the quadratic reading a query pays for its own
// answer, and script `foreach` over any plain value table paid it too.
class ibValueModelRowWalk : public ibValueIteratorState {
public:
	explicit ibValueModelRowWalk(ibValueModel* model) : m_model(model) {}

	bool MoveNext(ibValue& current) override {
		if (m_model == nullptr || m_pos >= m_model->GetRowCount())
			return false;
		const ibDataViewItem item = m_model->GetItem(m_pos++);
		auto* const line = m_model->GetRowAt(item);
		current = (line != nullptr) ? ibValue(static_cast<ibValue*>(line)) : ibValue();
		return true;
	}

	void Reset() override { m_pos = 0; }

	bool PeekSample(ibValue& current) const override {
		current = m_model->GetEmptyRow();
		return current.m_typeClass != ibValueTypes::TYPE_EMPTY;
	}

	long Remaining() const override {
		const long rows = m_model != nullptr ? (long)m_model->GetRowCount() : 0;
		return rows > m_pos ? rows - m_pos : 0;
	}

private:
	ibValueModel* m_model;
	long          m_pos = 0;
};

} // namespace

// ⭐ DOES THIS ROW HAVE A CONDITIONAL APPEARANCE AT ALL — the one question asked before anything is built for it:
// the node carries what the walk drew it with, or — for a row the walk never read — the model's setting declares a
// rule. Neither — nothing to count, nothing made (Max, 2026-09-30: "no conditional appearance must cost nothing").
// A row the walk read and found no rule for is the same "neither": it is not asked again.
bool ibValueModel::HasAttrByRow(const ibDataViewItem& row) const
{
	const ibComposerNode* node = GetViewData<ibComposerNode>(row);
	return node != nullptr
		&& (node->HasCompositionAttr()
			|| (!node->IsCompositionAttrKnown() && !GetModelComposer().GetCurrentConditionalAppearanceDesc().IsEmpty()));
}

// ⭐ WHAT A ROW'S CONDITIONAL APPEARANCE DRAWS A CELL WITH, in the composition's own structure — what the node carries
// (moved in from the fetch as it is), or, for a row that carries none while the setting declares rules (a table in
// memory, a heading folded in memory), the rules asked of the row now. The grid's attribute and the cell's text are
// both read off it.
bool ibValueModel::GetCompositionAttrByRow(const ibDataViewItem& row, unsigned int col, ibCompositionAttr& attr) const
{
	const ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return false;
	if (const ibCompositionAttr* kept = node->GetCompositionAttr(static_cast<ibMetaID>(col))) {
		attr = *kept;
		return !attr.IsDefault();
	}
	if (node->IsCompositionAttrKnown())
		return false;   // the walk read it and no rule held — that is the answer, not a question left open

	// A ROW THE WALK NEVER READ — asked now, of the row in memory, by the walk's own reading (compositionCondition.h):
	// the setting's rules made ready for the cell asked (a list asks for what fits on its screen, and can afford it),
	// a field found among the model's columns by its name, a subtree by the composer that feeds this model.
	const ibDataComposer& composer = GetModelComposer();
	const std::vector<ibCompositionRule> rules = ibCompositionRulesOf(composer.GetCurrentConditionalAppearanceDesc());
	std::deque<ibValue> read;   // what the engine is handed stays where it is while the condition is read
	const ibCompositionValueOf valueOf = [this, node, &read](const wxString& path) -> const ibValue* {
		const ibMetaID id = GetColumnIDByName(path);
		ibValue value;
		if (id == wxNOT_FOUND || !node->GetValue(id, value))
			return nullptr;
		read.push_back(value);
		return &read.back();
	};
	attr = ibCompositionAttr();
	ibCompositionAttr own;
	const bool held = ibCompositionSayRules(rules, valueOf, composer.SubtreeOf(), attr,
		[this, col, &own](const wxString& field, const ibCompositionAttr& said) {
			if (GetColumnIDByName(field) == static_cast<ibMetaID>(col))
				own.Say(said);
		});
	attr.Say(own);   // the column's own over the line's, as the walk lays them
	return held && !attr.IsDefault();
}

namespace {
// ⭐ THE CONVERTER — the grid's widget attribute made from the composition's structure, value for value (Max,
// 2026-09-30: "for lists just make a converter — they never hold more than fits on the screen"). The one place a list
// speaks the widget's word, and only as the grid asks, cell by visible cell. (The text is not the widget's: GetValue
// reads it off the composition's structure.)
ibDataViewItemAttr ibWidgetAttrOf(const ibCompositionAttr& drawn)
{
	ibDataViewItemAttr attr;
	if (drawn.m_backgroundColour.IsOk())
		attr.SetBackgroundColour(drawn.m_backgroundColour);
	if (drawn.m_textColour.IsOk())
		attr.SetColour(drawn.m_textColour);
	// …the font PART BY PART, over the row's own — the grid's flags where it has them.
	const ibCompositionFont& font = drawn.m_font;
	if (font.IsBold())
		attr.SetBold(true);
	if (font.IsItalic())
		attr.SetItalic(true);
	if (font.m_strikethrough)
		attr.SetStrikethrough(true);
	if (font.m_underlined)
		attr.SetUnderlined(true);
	if (font.m_pointSize > 0)
		attr.SetPointSize(font.m_pointSize);
	if (!font.m_face.IsEmpty())
		attr.SetFaceName(font.m_face);
	if (drawn.m_horizontalAlignment != wxALIGN_INVALID)
		attr.SetAlignment(drawn.m_horizontalAlignment);
	return attr;
}
} // namespace

// ⭐ A ROW'S CONDITIONAL APPEARANCE, as the grid's attribute — see the header.
bool ibValueModel::GetAttrByRow(const ibDataViewItem& row, unsigned int col, ibDataViewItemAttr& attr) const
{
	if (!HasAttrByRow(row))
		return false;   // no conditional appearance — nothing made to ask
	ibCompositionAttr drawn;
	if (!GetCompositionAttrByRow(row, col, drawn))
		return false;
	attr = ibWidgetAttrOf(drawn);
	return !attr.IsDefault();
}

void ibValueModel::GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const
{
	GetValueByRow(variant, item, col);
	if (!HasAttrByRow(item))
		return;   // no conditional appearance — the value as it is, nothing made to ask
	// …and what its conditional appearance has the cell say instead — its Text, or its value in its Format.
	ibCompositionAttr drawn;
	if (!GetCompositionAttrByRow(item, col, drawn))
		return;
	ibValue value;
	if (const ibComposerNode* node = GetViewData<ibComposerNode>(item))
		node->GetValue(static_cast<ibMetaID>(col), value);
	wxString text;
	if (drawn.TextOf(value, text))
		variant = new ibVariantDataValueModel(ibValue(text));
}

// RAM-backed models have NO source primary key (RunComposerPage stamps an EMPTY row-key for them) → restore by
// index; a DB list / register has a PK → restore by key. Replaces the retired RamFetch flag, derived from source.
bool ibValueModel::HasKeyedRows() const
{
	const ibBackendQueryable* q = GetSourceQueryable();
	return q != nullptr && !q->GetPrimaryKeyColumns().empty();
}

// Grouped when the composer carries at least one grouping dimension (the display is then a tree). The control
// uses this to route a grouped model's row mutations through the re-fetch + selection-restore path.
bool ibValueModel::IsGroupedModel() const
{
	return GetModelComposer().GroupCount() > 0;
}

// Base default: no key. A RAM / in-memory model has no DB identity; the DB cursor + register subclasses override
// to build the row's reference (guid) or dimensions (composite) from the item.
ibUniqueKey ibValueModel::GetItemKey(const ibDataViewItem& /*item*/) const
{
	return ibUniqueKey();
}

// (GetSortArrows DELETED — the header arrow is set on the FRONT: OnColumnClick sets it on the clicked column,
//  and each tablebox column re-reads the composer's active sort on rebuild (OnUpdated), matching by its OWN
//  bound field name. No {col-id, asc} bridge on the model.)

std::shared_ptr<ibValueIteratorState> ibValueModel::CreateIterator()
{
	// Every ibValueModel is paged (fetch is uniform through RunComposerPage), so iteration drives the Get*Fetch
	// cursor — UNLESS the composer currently GROUPS, in which case it is shaped as a TREE (ANY list with grouping
	// is a tree — Max) and the flat iterator would walk only one level.
	const ibDataComposer& composer = GetModelComposer();

	// ⭐ NOTHING IS ARRANGED — so walk the rows where they are. No filter, no sort, no grouping means
	// the paged fetch would compute the order of the whole table per batch and hand back exactly the
	// rows in storage order; this is that answer, at the price it should cost. Any arrangement at
	// all, and the question goes back to the composer, which is the only thing that can answer it.
	if (composer.GroupCount() == 0 && composer.SortCount() == 0
		&& !composer.GetCurrentFilterDesc().IsOk())
		return std::make_shared<ibValueModelRowWalk>(this);

	if (composer.GroupCount() == 0)
		return std::make_shared<ibValueModelPagedIteratorState>(this);
	return ibValue::CreateIterator();
}

#include "backend/picturePredefined.h"   // g_pic*CLSID — the standard command icons

///////////////////////////////////////////////////////////////////////////////////////

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::ibValueModelColumnInfo() :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true)
{
	m_members.Bind(this, &ibValueModelColumnInfo::FillMembers);
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::~ibValueModelColumnInfo()
{
}

enum Prop {
	enColumnName,
	enColumnTypes,
	enColumnCaption,
	enColumnWidth,
	enColumnIndexing
};

void ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Name"));
	helper.AppendProp(wxT("Types"));
	helper.AppendProp(wxT("Caption"));
	helper.AppendProp(wxT("Width"));
	helper.AppendProp(wxT("Indexing"));
}

bool ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enColumnName:
		pvarPropVal = GetColumnName();
		return true;
	case enColumnTypes:
		pvarPropVal = new ibValueTypeDescription(GetColumnType());
		return true;
	case enColumnCaption:
		pvarPropVal = GetColumnCaption();
		return true;
	case enColumnWidth:
		pvarPropVal = GetColumnWidth();
		return true;
	case enColumnIndexing:
		pvarPropVal = IsColumnIndexed();
		return true;
	}

	return false;
}

// The only WRITABLE one so far, and it is written from script rather than only from the inspector:
// asking for an index is a decision about how this table will be used, and that is known where the
// table is filled, not in a form designer.
bool ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (lPropNum == enColumnIndexing) {
		SetColumnIndexed(varPropVal.GetBoolean());
		return true;
	}

	return false;
}

#include "backend/backend_localization.h"   // ibTranslateString — a column here has no format written on it
#include "core/formatString.h"           // ibFormatString — what GetColumnFormat answers with

const ibFormatString& ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo::GetColumnFormat() const
{
	static const ibTranslateString s_none;
	return ibBackendTypeConfigFactory::GetFormatFromColumn(s_none, GetColumnType());
}

const ibFormatString& ibValueModel::ibValueModelColumnCollection::GetColumnFormat(unsigned int col) const
{
	static const ibFormatString s_none;
	const ibValueModelColumnInfo* column = GetColumnByID(col);
	return column != nullptr ? column->GetColumnFormat() : s_none;
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ibValueModel::ibValueModelColumnCollection::GetColumnByID(unsigned int col) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		ibValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (col == columnInfo->GetColumnID())
			return columnInfo;
	}

	return nullptr;
}

ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ibValueModel::ibValueModelColumnCollection::GetColumnByName(const wxString& colName) const
{
	for (unsigned int idx = 0; idx < GetColumnCount(); idx++) {
		ibValueModelColumnInfo* columnInfo = GetColumnInfo(idx);
		wxASSERT(columnInfo);
		if (stringUtils::CompareString(colName, columnInfo->GetColumnName()))
			return columnInfo;
	}

	return nullptr;
}