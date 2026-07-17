#include "backend/system/value/valueDynamicList.h"
#include "backend/appData.h"                         // appData / GetActiveMetaData
#include "backend/query/queryable.h"                 // ibBackendQueryable
#include "backend/query/queryableFactory.h"          // ibQueryableSourceDescriptor (source holder + its command surface)
// (listFetchDriver.h / session.h(ses_query) / modelView.h(s_constIgnoreParent) includes removed — they were
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
		// ALL columns — the runtime model collection must resolve any column a tablebox references (including a
		// system one the user re-shows). The DEFAULT-vs-hidden split is on the SOURCE EXPLORER (GetSourceExplorer),
		// which the tablebox column auto-creation reads to set each column's visibility.
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
	m_propertySource->SetQueryable(queryable);   // the queryable lives in the property, not here
	RebuildSource();
}

void ibValueDynamicList::RebuildSource()
{
	// STORE the source config into the metadata VARIABLE, taken from the VALUE — the picked queryable's metaobject
	// knows its config (the edited config in the designer, the copy's config on a copy). The list HOLDS it so
	// GetSourceMetaData is terminal (it never re-resolves the queryable through m_owner->GetMetaData, which walks the
	// form → recurses). A metaobject-less source (arbitrary query / register virtual table / not yet picked) leaves
	// the variable as-is; GetSourceMetaData then falls back to the ACTIVE config.
	const ibBackendQueryable* queryable = GetSourceQueryable();
	if (queryable != nullptr) {
		if (const ibValueMetaObjectGenericData* mo = queryable->GetSourceMetaObject())
			m_sourceMetaData = mo->GetMetaData();
	}

	// Thread the stored source config into the composer so an arbitrary-query source (FromText) resolves its by-name
	// FROM against THIS config, not the global factory. (A metaobject source binds by queryable below — no lookup.)
	m_composer.SetMetaData(GetSourceMetaData());

	// Arbitrary-query mode: the source is a QUERY TEXT — bind the composer straight to it. Columns come from the
	// query RESULT, not a metaobject (the computed-source frontier — that + the runtime fetch is the "rest" to
	// finish; design-time config + serialisation stand now).
	if (IsArbitraryQuery()) {
		m_composer.FromText(GetArbitraryQueryText());
		m_columns = nullptr;   // TODO: derive columns from the query-result queryable
		NotifyReset();
		return;
	}
	// Rebuild columns + composer from the current source (the property's queryable, resolved above).
	m_columns = (queryable != nullptr) ? new ibDynamicListColumns(queryable) : nullptr;   // ibValuePtr owns
	if (queryable != nullptr) {
		// A dynamic list is a DB model — rebind the source on its own ibDataDBComposer (m_composer, keeping settings).
		m_composer.FromSource(queryable);
		// (No default sort re-applied here — the metaobject sets it at CREATION as an ordinary serialised sort, so a
		//  user can remove it and it stays removed. Was: GetPresentationSortColumn re-applied every fetch. — Max.)
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

// Add a filter — forward the predicate to the composer under the list. The composer is the SINGLE settings
// store (it persists the line and the fetch reads it WITHOUT clearing), so a backend-injected select predicate and
// a user filter coexist. Same shape the presentation Sort uses in RebuildSource.
void ibValueDynamicList::AddFilter(const wxString& path, const wxString& op, const ibValue& value)
{
	m_composer.Filter(path, op, value);
	NotifyReset();
}

// Add a sort — forward the sort line to the composer (the settings store). The metaobject sets the default sort
// at list CREATION through here, so it becomes an ordinary serialised sort the user can later remove.
void ibValueDynamicList::AddSort(const wxString& path, bool ascending)
{
	m_composer.Sort(path, ascending);
	NotifyReset();
}

// Create a list with a DEFAULT sort set at creation — the metaobject passes its presentation column. The sort lands
// on the composer (the store) so the serializer saves it and the user can remove it; NOT re-applied at runtime.
ibValueDynamicList* ibCreateList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* defaultSort, ibDynamicListView view)
{
	ibValueDynamicList* list = new ibValueDynamicList(queryable, view);
	if (defaultSort != nullptr)
		list->AddSort(defaultSort->GetName());
	return list;
}

// Hierarchy list — folder-first sort (folders on top) then presentation. Both are ordinary creation-time sorts on
// the composer; the metaobject passes the columns. The TREE comes from the queryable's hierarchy (parent) column.
ibValueDynamicList* ibCreateHierarchyList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* folderCol, const ibBackendQueryColumn* presentationCol, ibDynamicListView view)
{
	ibValueDynamicList* list = new ibValueDynamicList(queryable, view);
	if (folderCol != nullptr) {
		list->SetFolderColumn(folderCol);                          // folder rows render as drillable containers (even empty)
		list->AddSort(folderCol->GetName(), /*ascending*/false);   // folders first (IsFolder true sorts before false)
	}
	if (presentationCol != nullptr)
		list->AddSort(presentationCol->GetName());
	return list;
}

// Folder-select list — presentation sort + a fixed `IsFolder = true` filter (only folders). The metaobject passes
// the columns (folder-select is a filter setting; no structural GetFolderColumn). `view` seeds the default view,
// same as the sibling factories — the folder-select call site passes ibDynamicListView_Choice.
ibValueDynamicList* ibCreateFolderList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* folderCol, const ibBackendQueryColumn* presentationCol, ibDynamicListView view)
{
	ibValueDynamicList* list = new ibValueDynamicList(queryable, view);
	if (presentationCol != nullptr)
		list->AddSort(presentationCol->GetName());
	if (folderCol != nullptr)
		list->AddFilter(folderCol->GetName(), wxT("="), ibValue(true));   // only folders
	return list;
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

// Scalar hop gate — DESIGN-TIME dot-walk (WalkColumns) only. Like the value-table, the dynamic list holds
// 0..N query rows, so there is NO single scalar cell to read: the walk steps by TYPE, not value. Hand back the
// pinned reference branch's empty typed twin (CoerceHopType — the reference builds its OWN twin). The column's
// CURRENT type is the stale-pin filter, read STRAIGHT off the queryable (its stable column set) — NOT via
// GetSourceExplorer(), which Reset()s the very explorer WalkColumns is mid-walk on. WITHOUT this override a
// dotted reference column (List.Ref.Field) reads back "<not selected>" though the picker shows it, because the
// base ibSourceDataObject::GetValueBySourceHop returns false — the missing twin of ibValueModelTable's.
bool ibValueDynamicList::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	ibTypeDescription colType;
	if (const ibBackendQueryable* q = GetSourceQueryable()) {
		for (const ibBackendQueryColumn* col : q->GetColumns())
			if (col != nullptr && col->GetColumnId() == hop.m_id) { colType = col->GetTypeDesc(); break; }
	}
	return ibValueReferenceDataObject::CoerceHopType(hop, out, colType, GetSourceMetaData());
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
// The row's identity key comes from the METAOBJECT — the node carries no metaobject id ("the node has no id"), and a
// register has SEVERAL key columns, so only the metaobject can shape the family-correct key. The list snaps the
// source descriptor and forwards the row's value map; the metaobject decodes it (record → reference guid, register
// → composite record key). Same currency (rowValues) as GetItemSelectValue. Neutral surface (custom query) → empty.
ibUniqueKey ibValueDynamicList::GetItemKey(const ibDataViewItem& item) const
{
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr)
		return ibUniqueKey();
	const ibQueryableSourceDescriptor* holder = GetSourceDescriptor();
	return holder != nullptr ? holder->GetItemKey(node->GetTableValues()) : ibUniqueKey();
}

// Selection-restore after a child-form save (createdValue / changedValue → the current row is re-found): the list
// is metadata-blind, so the ROW-KEY is built by the source descriptor (GetRowKeyByValue) — a record keys by its
// reference, a REGISTER decomposes its COMPOSITE key across the PK columns (the SAME the fetch stamps into
// m_rowKey). The stub carries only that key; the freshly-fetched batch matches it by m_rowKey (IsEqualTo). This is
// the per-list "find by key" the deleted models did — a register row would otherwise never restore, because the
// base's flat {value} stub carries one element and a register's key is multi-column. Custom-query source → base.
ibDataViewItem ibValueDynamicList::FindRowValue(const ibValue& varValue, const wxString& colName) const
{
	(void)colName;
	if (varValue.IsEmpty())
		return ibDataViewItem();
	const ibQueryableSourceDescriptor* holder = GetSourceDescriptor();
	std::vector<ibValue> rowKey = holder != nullptr
		? holder->GetRowKeyByValue(varValue)
		: std::vector<ibValue>{ varValue };
	if (rowKey.empty())
		return ibDataViewItem();
	auto* stub = new ibValueModel::ibComposerNode(std::move(rowKey));
	ibDataViewItem item(stub);   // IncRef → 2
	stub->DecRef();              // refcount = 1, owned by item
	return item;
}

// Bridge: open the activated row's value. The list holds the row key but not the "how" — it snaps the source
// descriptor (holder) off the property (LIVE, never cached) and delegates to its command surface. A source with a
// neutral surface (e.g. a custom-query source, or an enum) simply no-ops.
void ibValueDynamicList::ActivateItem(const ibDataViewItem& row, ibBackendValueForm* srcForm)
{
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->ShowValueByKey(GetItemKey(row), srcForm);
}

// Forward the source's command SET to the TableBox (which merges it into the bar). Metadata-blind: whatever the
// descriptor lists — the metaobject's set, forwarded — is what shows. Neutral surface → nothing.
void ibValueDynamicList::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->GetCommandCollection(formType, commands);
}

// Run a bar/menu click: convert BOTH front-owned rows to their keys and hand them to the source descriptor,
// which forwards to the metaobject (opens a form / refreshes srcForm). key = the selected row (delete / edit
// target, and the parent source for create when present); anchor = where the user stands in the tree, the
// create fallback when nothing is selected so a new element lands in the browsed folder.
void ibValueDynamicList::CallAsCommand(const ibActionID& lNumAction, const ibDataViewCommandContext& ctx, ibBackendValueForm* srcForm)
{
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->CallAsCommand(lNumAction, GetItemKey(ctx.m_anchor), GetItemKey(ctx.m_selection), srcForm);
}

// Picker Choose — hand the row's value map (its default columns) to the source descriptor, which reads the
// family-correct SELECT value (a catalog its reference, a register its record key). The list stays blind.
ibValue ibValueDynamicList::GetItemSelectValue(const ibDataViewItem& item) const
{
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr)
		return ibValue();
	const ibQueryableSourceDescriptor* holder = GetSourceDescriptor();
	return holder != nullptr ? holder->GetSelectValue(node->GetTableValues()) : ibValue();
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
	// The list is a MIRROR: it resets the explorer and hands it to the source descriptor (the single bridge), which
	// forwards the fill to the metaobject — the same one that lists/runs commands and resolves select.
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->FillSourceExplorer(m_sourceExplorer);
	// The list's DEFAULT view rides onto the explorer: a Choice list stamps the choice flag the form auto-build copies
	// onto the mainTableBox (where it serialises per-form). A Normal list leaves it off. This is the ONE propagation.
	m_sourceExplorer.SetChoiceMode(m_view == ibDynamicListView_Choice);
	return &m_sourceExplorer;
}

wxString ibValueDynamicList::GetSourceCaption() const
{
	// Caption = the metaobject's "Type: Synonym" (Catalog: Products) when the source is metadata-backed; a custom-
	// query source has no metaobject, so fall back to the queryable's own name. GetSourceMetaObject comes THROUGH
	// the queryable (the same path the front reads the icon), so caption + icon stay consistent.
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return GetSourceMetaObject() ?
			stringUtils::GenerateSynonym(GetSourceMetaObject()->GetClassName()) + wxT(": ") + GetSourceMetaObject()->GetSynonym() : q->GetQueryName();

	return GetClassName();
}

const ibMetaData* ibValueDynamicList::GetMetaData() const
{
	// SELECTION — which config to PICK / resolve a source FROM: the owner FORM's config, via the standard attach
	// chain (holder -> attribute -> form). This is what GetSourceMetaData used to do. The Source picker and the
	// property factory resolve a source BY NAME through it. Non-cyclic: the form falls back to its SOURCE (this
	// list) for metadata, and the list's GetSourceMetaData reads from the VALUE (terminal), so the walk ends.
	if (const ibPropertyObject *o = GetAttachOwner())
		return o->GetMetaData();

	return ibApplicationData::GetActiveMetaData();
}

const ibMetaData* ibValueDynamicList::GetSourceMetaData() const
{
	// READ — the STORED source config, captured from the picked queryable's metaobject when the source was set
	// (RebuildSource: ctor / load / source-change / copy). Returned straight from the field — NO owner-walk, NO
	// queryable re-resolve (GetSourceQueryable -> GetQueryable re-resolves through m_owner->GetMetaData,
	// which walks the form → would recurse here). TERMINAL, so a form's metadata may fall back to this list without
	// looping. Nothing stored yet (no source picked) -> the ACTIVE config. Selection is GetMetaData.
	return m_sourceMetaData != nullptr ? m_sourceMetaData : ibApplicationData::GetActiveMetaData();
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
	// Source picked, or the arbitrary-query flag / text changed — rebuild off the current source.
	if (m_propertySource == property || m_propertyUseCustomQuery == property || m_propertyCustomQuery == property)
		RebuildSource();
	// The source/settings changed; re-commit the settings buffer onto the composer (L5).
	RefreshComposerSettings();
	// Toggling DynamicRead switches the fetch path (live keyset cursor ↔ whole-list RAM snapshot). Re-drive the fetch
	// so it takes effect: RefetchAll bumps the view generation → the snapshot rebuilds on the next fetch, or the cursor
	// resumes. (Nothing to rebuild in the source/composer — only the fetch route changes.)
	if (m_propertyDynamicRead == property)
		RefetchAll();
}

// Central save/load entry (ibPropertyObject::ReadProperty/WriteProperty override): the
// Source property + the settings (Filter/Order/Group) held OUTSIDE the property set.
bool ibValueDynamicList::ReadProperty(const ibDataNode& node)
{
	// The flag chooses the source: an arbitrary query TEXT, or a picked metaobject (both serialised).
	m_propertyUseCustomQuery->ReadNodeValue(node.GetProperty(m_propertyUseCustomQuery->GetName()));
	if (IsArbitraryQuery())
		m_propertyCustomQuery->ReadNodeValue(node.GetProperty(m_propertyCustomQuery->GetName()));
	else
		m_propertySource->ReadNodeValue(node.GetProperty(m_propertySource->GetName()));   // resolves the queryable from the id
	RebuildSource();

	GetListSettings()->ReadData(node);   // node → buffer; then commit buffer → composer (the store)
	RefreshComposerSettings();

	// Default view — a hidden intrinsic field (absent on an old blob → Normal, forward-compatible).
	m_view = (ibDynamicListView)node.GetValue<s32>(wxT("View"));

	// DynamicRead — stored INVERTED (DynamicReadOff: 1 = static/RAM-snapshot, absent/0 = the default dynamic read), so
	// a list blob written before this field loads as dynamic. (SetValue does not fire OnPropertyChanged.)
	m_propertyDynamicRead->SetValue(node.GetValue<s32>(wxT("DynamicReadOff")) == 0);
	return true;
}

bool ibValueDynamicList::WriteProperty(ibDataNode& node) const
{
	// The flag first — it decides which source half is serialised.
	node.SetProperty(m_propertyUseCustomQuery->GetName(), m_propertyUseCustomQuery->GetNodeValue());
	if (IsArbitraryQuery()) {
		// Arbitrary query: serialise the query TEXT — there is no metaobject source to require.
		node.SetProperty(m_propertyCustomQuery->GetName(), m_propertyCustomQuery->GetNodeValue());
	}
	else {
		// The Source property's write FAILS when no queryable is picked
		// (ibPropertyDynamicSource::WriteNodeValue) — forbid serialising an INCOMPLETE, column-less
		// list that would resolve to nothing on load. Propagate that verdict; the form attribute
		// drops the whole node on false.
		ibDataValue value;
		if (!m_propertySource->WriteNodeValue(value))
			return false;
		node.SetProperty(m_propertySource->GetName(), value);
	}

	// GetListSettings() is the live FACADE over the composer (the store), so this serialises the composer's
	// current Filter/Order/Group directly. (WriteData/ReadData item round-trip is still a TODO stub.)
	GetListSettings()->WriteData(node);

	// Default view — written implicitly as a hidden intrinsic field (see ibDynamicListView).
	node.SetValue<s32>(wxT("View"), (s32)m_view);

	// DynamicRead — record only the DEVIATION from the default (1 = static/RAM snapshot; 0/absent = dynamic). See ReadProperty.
	node.SetValue<s32>(wxT("DynamicReadOff"), IsDynamicRead() ? 0 : 1);
	return true;
}

// Register the type — runtime / designer know "DynamicList".
VALUE_TYPE_REGISTER(ibValueDynamicList, "DynamicList", g_valueDynamicListCLSID);
// The row / return-line type. When a choice has NO source-command handler (holder == null →
// GetItemSelectValue empty), the base ibValueModelReturnLine::GetSelectValue falls back to the row's
// OWN value (GetValue → this), so its C++ type MUST be in the ctor registry or GetTypeIDByRef asserts.
// Mirrors the value-table's VL_TVROW registration — missed in the dynamic-list migration.
SYSTEM_TYPE_REGISTER(ibDynamicListReturnLine, "DynamicListRow", system_to_clsid("VL_DLROW"));
