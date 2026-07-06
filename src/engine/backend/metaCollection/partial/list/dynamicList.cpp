#include "backend/metaCollection/partial/list/dynamicList.h"
#include "backend/appData.h"                         // appData / GetActiveMetaData
#include "backend/query/queryable.h"                 // ibBackendQueryable
// (listFetchDriver.h / session.h(ses_query) / tableView.h(s_constIgnoreParent) includes removed — they were
//  only for the deleted RunPage/ibDynamicListProvider; the fetch lives in the base RunComposerPage now.)
#include "backend/query/queryColumn.h"               // ibBackendQueryColumn::GetColumnId
#include "backend/srcDataObject.h"                      // ibSourceExplorer
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — GetItemKey row guid
#include "backend/serialize/dataBuilder.h"            // ibDataNode (object-level save/load)
#include "backend/metadataConfiguration.h"            // ibMetaDataConfigurationBase (GetSourceMetaData)

namespace {
// (ibDynamicListProvider DELETED — the dynamic list fetches through the base ibValueModel::RunComposerPage now,
//  like every other model: ONE fetch in the parent. Its keyset provider + ibDynamicListNode are gone;
//  RunComposerPage yields ibComposerNode, read through the base GetViewData<ibValueTreeNode>.)

// Column collection straight off the queryable — columns are the queryable's
// columns (GetColumnId / GetName), NOT metaobject attributes.
class ibDynamicListColumns : public ibValueModelCursor::ibValueModelColumnCollection {
public:
	class ColInfo : public ibValueModelColumnInfo {
	public:
		explicit ColInfo(const ibBackendQueryColumn* col) : m_col(col) {}
		virtual unsigned int GetColumnID() const override { return m_col != nullptr ? m_col->GetColumnId() : 0; }
		virtual wxString GetColumnName() const override { return m_col != nullptr ? m_col->GetName() : wxString(); }
		virtual wxString GetColumnCaption() const override { return m_col != nullptr ? m_col->GetName() : wxString(); }
		// Name + id + TYPE straight off the column (an attribute IS-A column → GetTypeDesc is free).
		// Enough to render/edit a cell type-aware; for 90% of sources nothing else is needed.
		virtual const ibTypeDescription GetColumnType() const override { return m_col != nullptr ? m_col->GetTypeDesc() : ibTypeDescription(); }
	private:
		const ibBackendQueryColumn* m_col;
	};

	explicit ibDynamicListColumns(const ibBackendQueryable* queryable) {
		if (queryable != nullptr)
			for (const ibBackendQueryColumn* c : queryable->GetColumns())
				m_cols.push_back(new ColInfo(c));
	}
	virtual ~ibDynamicListColumns() { for (auto* c : m_cols) delete c; }

	virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const override {
		return idx < m_cols.size() ? m_cols[idx] : nullptr;
	}
	virtual unsigned int GetColumnCount() const override { return static_cast<unsigned int>(m_cols.size()); }

private:
	std::vector<ColInfo*> m_cols;
};

// Script return-line over a row node — owner link (column-value Get/SetPropVal
// mapping the node's values is harden).
class ibDynamicListReturnLine : public ibValueModelCursor::ibValueModelReturnLine {
public:
	ibDynamicListReturnLine(ibValueDynamicList* owner, const ibDataViewItem& line)
		: ibValueModelReturnLine(line), m_owner(owner) {}
	virtual ibValueModel* GetOwnerModel() const override { return m_owner; }
private:
	ibValueDynamicList* m_owner;
};
} // namespace

// ===========================================================================
//  ibValueDynamicList
// ===========================================================================

ibValueDynamicList::ibValueDynamicList(const ibBackendQueryable* queryable, ibDynamicListView view)
	: ibValueModelCursor(), ibSourceDataObject(),
	  m_columns(nullptr), m_view(view)
{
	// (No own settings buffer — the BASE model already built m_listSettings; use GetListSettings().)
	if (queryable != nullptr)
		SetSourceQueryable(queryable);   // null → set later via SetSource
}

ibValueDynamicList::~ibValueDynamicList() {}

// --- source (the list starts empty) ----------------------------------------

void ibValueDynamicList::SetSource(const wxString& ns, const wxString& name)
{
	const ibBackendQueryable* before = GetSourceQueryable();
	m_propertySource->SetSource(ns, name);   // resolves the queryable INTO the property variant
	if (GetSourceQueryable() != before)      // only on a REAL change — re-picking the SAME source keeps columns/settings
		RebuildSource();
}

void ibValueDynamicList::SetSourceQueryable(const ibBackendQueryable* queryable)
{
	if (queryable == GetSourceQueryable())   // same source — leave columns/composer/settings untouched
		return;
	m_propertySource->SetVariable(queryable);   // the variable lives in the property, not here
	RebuildSource();
}

void ibValueDynamicList::RebuildSource()
{
	// Rebuild columns + composer from the current source (the property's queryable).
	const ibBackendQueryable* queryable = GetSourceQueryable();
	m_columns = (queryable != nullptr) ? new ibDynamicListColumns(queryable) : nullptr;   // ibValuePtr owns
	if (queryable != nullptr) {
		// A dynamic list is a DB model — rebind the source on its own ibDataDBComposer (m_composer, keeping settings).
		m_composer.FromSource(queryable);
		// FromSource rebinds the source — re-commit the settings buffer onto the composer so a source
		// change does NOT silently drop the Filter/Order/Group the user configured. The settings DATA
		// lives on the base buffer GetListSettings() (untouched here); only the composer source changed.
		RefreshComposerSettings();
	}
}

void ibValueDynamicList::SetCustomQuery(const wxString& queryText)
{
	m_composer.FromText(queryText);   // the dynamic list's own DB composer (FromText is DB-only)
}

void ibValueDynamicList::RefreshComposerSettings()
{
	// Nothing to re-apply: GetListSettings() is now a live FACADE that writes the composer directly, and the
	// settings DIALOG commits its own buffer onto the composer on OK. A settings/source change just refetches.
	NotifyReset();
}

ibValueModel::Features ibValueDynamicList::GetFeatures() const
{
	Features f;
	f.flags |= Features::Filters | Features::Sorting | Features::Grouping;   // tree-ness = grouping (GroupCount)
	if (m_composer.HasCustomText())
		f.flags |= Features::CustomQuery;   // "custom query" installed via SetCustomQuery → FromText
	return f;
}

// --- marshalling (value out/in of the tree node) ----------------------------

void ibValueDynamicList::GetValueByRow(wxVariant& variant, const ibDataViewItem& item, unsigned col) const
{
	ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
	if (node != nullptr)
		node->GetValue(col, variant);
}

bool ibValueDynamicList::SetValueByRow(const wxVariant& variant, const ibDataViewItem& item, unsigned col)
{
	ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
	return node != nullptr && node->SetValue(col, variant);
}

bool ibValueDynamicList::GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const
{
	ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
	return node != nullptr && node->GetValue(id, pvarMetaVal);
}

ibValueModel::ibValueModelReturnLine* ibValueDynamicList::GetRowAt(const ibDataViewItem& line)
{
	if (!line.IsOk())
		return nullptr;
	return new ibDynamicListReturnLine(this, line);
}

ibUniqueKey ibValueDynamicList::GetGuid() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return q->GetQueryTableGuid();

	return wxNullGuid;
}

// The dynamic list creates its own row key (the cursor base makes none): the row's primary-key REFERENCE (guid),
// read off the item's node — the same shape a regular list uses, since a dynamic list row references an object.
ibUniqueKey ibValueDynamicList::GetItemKey(const ibDataViewItem& item) const
{
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr)
		return ibUniqueKey();
	const ibBackendQueryable* q = GetSourceQueryable();
	if (q == nullptr)
		return ibUniqueKey();
	const std::vector<const ibBackendQueryColumn*> keyCols = q->GetPrimaryKeyColumns();
	if (keyCols.empty() || keyCols.front() == nullptr)
		return ibUniqueKey();
	ibValue refVal;
	if (node->GetValue(keyCols.front()->GetColumnId(), refVal)) {
		ibValueReferenceDataObject* refObj = nullptr;
		if (refVal.ConvertToValue(refObj) && refObj != nullptr)
			return ibUniqueKey(refObj->GetGuid());
	}
	return ibUniqueKey();
}

const ibSourceExplorer* ibValueDynamicList::GetSourceExplorer() const
{
	// The explorer's columns come from the QUERYABLE, not a metaobject - the dynamic list is
	// queryable-based. Use the COLUMN-overload (a queryable column IS-A ibBackendSourceColumn): it
	// carries the descriptor on the node (m_col -> GetSourceAttributeObject, so the binding resolves and
	// the header shows the real SYNONYM, not the control name) — the 3-arg plain-value overload set
	// neither m_col nor the synonym (synonym defaulted to the name). null / !IsAllowed are skipped inside.
	// Root flagged a TABLE SECTION — a dynamic list IS a table, so IsTableSource() reports true and the
	// attribute drags as a tablebox (metadata-free, same as the value-table / object lists).
	m_sourceExplorer.Reset(GetObjectTypeName(), GetObjectTypeName(), wxNOT_FOUND, g_valueDynamicListCLSID, /*tableSection*/true);
	const ibBackendQueryable* q = GetSourceQueryable();
	if (q != nullptr)
		for (const ibBackendQueryColumn* col : q->GetColumns())
			m_sourceExplorer.AppendColumn(col);
	return &m_sourceExplorer;
}

wxString ibValueDynamicList::GetSourceCaption() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return q->GetQueryName();

	return GetClassName();
}

const ibMetaData* ibValueDynamicList::GetSourceMetaData() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return q->GetMetaData();

	return ibApplicationData::GetActiveMetaData();
}

// FETCH lives ONLY in the parent now (Max: "fetch lives only in the parent"). The dynamic list NO LONGER overrides
// Get*Fetch / RunPage — it inherits ibValueModel::Get*Fetch → RunComposerPage, exactly like the catalog /
// register / RAM models. RunComposerPage reads through GetSourceQueryable() (the configured source) + the ONE
// base composer + the persistent ListSettings; its grouping branch does the same group-drill RunPage did
// (Filter dim==value per drilled level + TotalBy the next), and its hierarchy branch the self-hierarchy tree.
// The old per-list keyset provider + ibDynamicListNode + the native page.m_hierarchyCol tree are gone — a tree is
// a hierarchy GROUPING over the composer now, not a parent-column page scope.
// ⚠ FromText (SetCustomQuery): RunComposerPage requires GetSourceQueryable() != null, so a custom-text source
//   must vend a queryable to page through here (the computed-source frontier) — FromSource sources work today.

// --- settings surface -------------------------------------------------------

void ibValueDynamicList::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Filter"),   true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Order"),    true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Group"),    true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Settings"), true, false, wxNOT_FOUND);
	helper.AppendProc(wxT("Refresh"), wxT("Refresh()"));
}

bool ibValueDynamicList::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	ibValueListSettings* s = GetListSettings();
	switch (lPropNum) {
	case 0: pvarPropVal = s->GetFilter(); return true;   // Filter
	case 1: pvarPropVal = s->GetOrder();  return true;   // Order
	case 2: pvarPropVal = s->GetGroup();  return true;   // Group
	case 3: pvarPropVal = s;              return true;   // Settings (the whole object)
	}
	return false;
}

bool ibValueDynamicList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum == 0) {   // Refresh
		RefreshComposerSettings();
		RefetchAll();
		return true;
	}
	return false;
}

// --- ibPropertyObject — the dynamic list IS a property object (properties surface onto the attribute) ---

// The form edits the list's OWN properties; when one changes (source / settings) the
// list re-applies it. The "hook" is a virtual on the property-object, not a function ptr.
void ibValueDynamicList::OnPropertyChanged(ibProperty* property, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
	// Source picked: the property already holds the new queryable; rebuild off it.
	if (m_propertySource == property)
		RebuildSource();
	// The source/settings changed; re-commit the settings buffer onto the composer (L5).
	RefreshComposerSettings();
}

// Central save/load entry (ibPropertyObject::ReadProperty/WriteProperty override): the
// Source property + the settings (Filter/Order/Group) held OUTSIDE the property set.
bool ibValueDynamicList::ReadProperty(const ibDataNode& node)
{
	// The property resolves the queryable from the serialized source id; rebuild off it.
	m_propertySource->ReadNodeValue(node.GetProperty(m_propertySource->GetName()));
	RebuildSource();

	GetListSettings()->ReadData(node);   // node → buffer; then commit buffer → composer (the store)
	RefreshComposerSettings();
	return true;
}

bool ibValueDynamicList::WriteProperty(ibDataNode& node) const
{
	// The Source property's write FAILS when no queryable is picked
	// (ibPropertyDynamicSource::WriteNodeValue) — forbid serialising an INCOMPLETE, column-less
	// list that would resolve to nothing on load. Propagate that verdict; the form attribute
	// drops the whole node on false.
	ibDataValue value;
	if (!m_propertySource->WriteNodeValue(value))
		return false;
	node.SetProperty(m_propertySource->GetName(), value);

	// GetListSettings() is the live FACADE over the composer (the store), so this serialises the composer's
	// current Filter/Order/Group directly. (WriteData/ReadData item round-trip is still a TODO stub.)
	GetListSettings()->WriteData(node);
	return true;
}

// Register the type — runtime / designer know "DynamicList".
VALUE_TYPE_REGISTER(ibValueDynamicList, "DynamicList", g_valueDynamicListCLSID);
