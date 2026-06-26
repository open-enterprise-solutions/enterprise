#include "backend/metaCollection/partial/list/dynamicList.h"
#include "backend/composition/listFetchDriver.h"
#include "backend/appData.h"                         // appData / GetActiveMetaData
#include "backend/session/session.h"                 // ses_query
#include "backend/tableView.h"                       // s_constIgnoreParent
#include "backend/query/queryable.h"                 // ibBackendQueryable
#include "backend/query/queryColumn.h"               // ibBackendQueryColumn::GetColumnId
#include "backend/srcDataObject.h"                      // ibSourceExplorer
#include "backend/serialize/dataBuilder.h"            // ibDataNode (object-level save/load)
#include "backend/metadataConfiguration.h"            // ibMetaDataConfigurationBase (GetSourceMetaData)

// ===========================================================================
//  L5 fetch provider — composer.Run(this) → OnRow builds rows straight into the
//  table (the model). All fetch work lives HERE; Get*Fetch stays thin.
// ===========================================================================
namespace {
class ibDynamicListProvider : public ibCompositionDriver {
public:
	ibDynamicListProvider(ibValueModelTreeBase* tree, const ibReadPageRequest& page,
		const std::vector<const ibBackendQueryColumn*>& keyCols)
		: m_tree(tree), m_page(page), m_keyCols(keyCols) {}

	bool GetPageRequest(ibReadPageRequest& request) const override { request = m_page; return true; }

	// Schema arrives BEFORE the rows — map the key columns to their positions.
	void OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) override {
		m_schema = schema;
		m_keyIdx.clear();
		for (const ibBackendQueryColumn* kc : m_keyCols)
			for (size_t i = 0; i < m_schema.size(); ++i)
				if (m_schema[i].m_col == kc) { m_keyIdx.push_back(i); break; }
	}

	// Build the node straight into the table — keyset identity (no guid) + all
	// columns, mapped by the schema's column ids.
	void OnRow(int /*level*/, bool hasChildren, const std::vector<ibValue>& values) override {
		std::vector<ibValue> key;
		for (size_t i : m_keyIdx)
			if (i < values.size()) key.push_back(values[i]);
		auto* node = new ibValueDynamicList::ibDynamicListNode(m_tree, key, hasChildren);
		for (size_t i = 0; i < m_schema.size() && i < values.size(); ++i)
			if (m_schema[i].m_col != nullptr)
				node->AppendTableValue(m_schema[i].m_col->GetColumnId(), values[i]);
		m_rows.push_back(node);
	}

	std::vector<ibValueDynamicList::ibDynamicListNode*> TakeRows() { return std::move(m_rows); }

private:
	ibValueModelTreeBase*                                m_tree;
	ibReadPageRequest                                    m_page;
	std::vector<const ibBackendQueryColumn*>             m_keyCols;
	std::vector<ibQueryLowering::OutputColumn>           m_schema;
	std::vector<size_t>                                  m_keyIdx;
	std::vector<ibValueDynamicList::ibDynamicListNode*>  m_rows;
};

// Column collection straight off the queryable — columns are the queryable's
// columns (GetColumnId / GetName), NOT metaobject attributes.
class ibDynamicListColumns : public ibValueModelTreeBase::ibValueModelColumnCollection {
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
class ibDynamicListReturnLine : public ibValueModelTreeBase::ibValueModelReturnLine {
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
	: ibValueModelTreeBase(), ibSourceDataObject(),
	  m_columns(nullptr), m_view(view)
{
	m_settings = new ibValueListSettings();
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
		m_composer.FromSource(queryable);   // L5 reads off the queryable
		// FromSource resets the composer — re-apply the (surviving) settings so a source
		// change does NOT silently drop the Filter/Order/Group the user configured. The
		// settings DATA lives on m_settings (untouched here); only the composer was rebuilt.
		RefreshComposerSettings();
	}
}

void ibValueDynamicList::SetCustomQuery(const wxString& queryText)
{
	m_composer.FromText(queryText);
}

void ibValueDynamicList::RefreshComposerSettings()
{
	// Replace the settings ON the composer (call on settings change, NOT per fetch).
	m_composer.ClearSettings();
	ibApplyDynamicSettings(m_composer, GetListSettings());
}

ibValueModel::Features ibValueDynamicList::GetFeatures() const
{
	Features f;
	f.flags |= Features::DbFetch | Features::Tree | Features::Filters | Features::Sorting;
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

const ibSourceExplorer* ibValueDynamicList::GetSourceExplorer() const
{
	// The explorer's columns come from the QUERYABLE, not a metaobject - the dynamic list is
	// queryable-based. Use the COLUMN-overload (a queryable column IS-A ibBackendSourceColumn): it
	// carries the descriptor on the node (m_col -> GetSourceAttributeObject, so the binding resolves and
	// the header shows the real SYNONYM, not the control name) — the 3-arg plain-value overload set
	// neither m_col nor the synonym (synonym defaulted to the name). null / !IsAllowed are skipped inside.
	m_sourceExplorer.Reset(GetObjectTypeName(), GetObjectTypeName(), wxNOT_FOUND, g_valueDynamicListCLSID);
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

const ibValueMetaObjectGenericData* ibValueDynamicList::GetSourceMetaObject() const
{
	return nullptr;
}

const ibMetaData* ibValueDynamicList::GetSourceMetaData() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return q->GetMetaData();

	return ibApplicationData::GetActiveMetaData();
}

// --- events (no-op stubs — add/copy/edit/delete on a dynamic list go through the choice /
//     keyset path, designed separately; see project memory) ---
void ibValueDynamicList::AddValue(unsigned int before)
{
}

void ibValueDynamicList::CopyValue()
{
}

void ibValueDynamicList::EditValue()
{
}

void ibValueDynamicList::DeleteValue()
{
}

// --- THIN fetch — all the work is in the L5 provider ------------------------

unsigned int ibValueDynamicList::RunPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibFetchDirection dir, ibDataViewItemArray& out) const
{
	const ibBackendQueryable* q = GetSourceQueryable();
	if (q == nullptr)
		return 0;
	auto db = ses_query;
	if (db == nullptr || !db->IsOpen())
		return 0;

	ibReadPageRequest page;
	page.m_direction   = dir;
	page.m_count       = (count > 0 ? count : 21) + 1;   // +1 probe row → hasMore
	page.m_reverseSort = (dir == ibFetchDirection::Backward);
	if (anchor.IsOk()) {
		page.m_hasAnchor = true;
		ibValueTreeNode* a = GetViewData<ibValueTreeNode>(anchor);
		for (const auto& s : m_sortOrder.m_sorts) {
			if (!s.m_sortEnable) continue;
			ibValue v;
			if (a != nullptr) a->GetValue(s.m_sortModel, v);
			page.m_anchorSortValues.push_back(v);
		}
	}

	// Tree scope lives IN the page (the composer reads it via GetPageRequest):
	// List view passes s_constIgnoreParent → flat; Tree view passes a real parent.
	// A source with no parent column (document / register) is always flat.
	const ibBackendQueryColumn* parentCol = q->GetParentColumn();
	page.m_parentFilter = true;
	page.m_parentCol    = parentCol;
	page.m_flatScan     = (parent == s_constIgnoreParent) || (parentCol == nullptr);
	page.m_isTopLevel   = !parent.IsOk() || page.m_flatScan;
	// (page.m_parentGuid — harden: derive the parent scope guid from the parent
	//  node's keyset; flat path doesn't need it.)

	ibDynamicListProvider provider(const_cast<ibValueDynamicList*>(this), page,
		q->GetPrimaryKeyColumns());
	m_composer.Run(provider);   // L5 fills the provider — the only loop lives there

	std::vector<ibDynamicListNode*> rows = provider.TakeRows();
	if (static_cast<int>(rows.size()) > count && count > 0) {
		delete rows.back();
		rows.pop_back();
	}
	if (dir == ibFetchDirection::Backward)
		std::reverse(rows.begin(), rows.end());

	const unsigned int fetched = static_cast<unsigned int>(rows.size());
	AdoptRowsToItems(rows, out);
	return fetched;
}

unsigned int ibValueDynamicList::GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	return RunPage(parent, anchor, count, ibFetchDirection::Reset, out);
}

unsigned int ibValueDynamicList::GetNextFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (!anchor.IsOk())
		return RunPage(parent, ibDataViewItem(), count, ibFetchDirection::Reset, out);
	return RunPage(parent, anchor, count, ibFetchDirection::Forward, out);
}

unsigned int ibValueDynamicList::GetPrevFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor,
	int count, ibDataViewItemArray& out) const
{
	if (!anchor.IsOk())
		return 0;
	return RunPage(parent, anchor, count, ibFetchDirection::Backward, out);
}

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
	if (ibValueListSettings* s = GetListSettings()) {
		switch (lPropNum) {
		case 0: pvarPropVal = s->GetFilter(); return true;
		case 1: pvarPropVal = s->GetOrder();  return true;
		case 2: pvarPropVal = s->GetGroup();  return true;
		case 3: pvarPropVal = s;              return true;
		}
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

wxString ibValueDynamicList::GetClassName() const
{
	return wxT("DynamicList");
}

// The form edits the list's OWN properties; when one changes (source / settings) the
// list re-applies it. The "hook" is a virtual on the property-object, not a function ptr.
void ibValueDynamicList::OnPropertyChanged(ibProperty* property, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
	// Source picked: the property already holds the new queryable; rebuild off it.
	if (m_propertySource == property)
		RebuildSource();
	// The settings form edited m_settings in place; re-apply onto the composer (L5).
	RefreshComposerSettings();
}

// Central save/load entry (ibPropertyObject::ReadProperty/WriteProperty override): the
// Source property + the settings (Filter/Order/Group) held OUTSIDE the property set.
bool ibValueDynamicList::ReadProperty(const ibDataNode& node)
{
	if (m_propertySource != nullptr) {
		// The property resolves the queryable from the serialized source id; rebuild off it.
		m_propertySource->ReadNodeValue(node.GetProperty(m_propertySource->GetName()));
		RebuildSource();
	}
	
	if (m_settings != nullptr)
		m_settings->ReadData(node);   // ibValueListSettings own method
	
	RefreshComposerSettings();	
	return true;
}

bool ibValueDynamicList::WriteProperty(ibDataNode& node) const
{
	// The Source property's write FAILS when no queryable is picked
	// (ibPropertyDynamicSource::WriteNodeValue) — forbid serialising an INCOMPLETE, column-less
	// list that would resolve to nothing on load. Propagate that verdict; the form attribute
	// drops the whole node on false.
	if (m_propertySource != nullptr) {
		ibDataValue value;
		if (!m_propertySource->WriteNodeValue(value))
			return false;
		node.SetProperty(m_propertySource->GetName(), value);
	}

	if (m_settings != nullptr)
		m_settings->WriteData(node);   // ibValueListSettings own method

	return true;
}

// Register the type — runtime / designer know "DynamicList".
VALUE_TYPE_REGISTER(ibValueDynamicList, "DynamicList", g_valueDynamicListCLSID);
