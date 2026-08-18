#include "backend/system/value/valueDynamicList.h"
#include "backend/appData.h"                         // appData / GetActiveMetaData
#include "backend/query/queryable.h"                 // ibBackendQueryable
#include "backend/query/queryableFactory.h"          // ibQueryableSourceDescriptor (source holder + its command surface)
// (listFetchDriver.h / session.h(ses_query) / modelView.h(s_constIgnoreParent) includes removed — they were
//  only for the deleted RunPage/ibDynamicListProvider; the fetch lives in the base RunComposerPage now.)
#include "backend/query/queryColumn.h"               // ibBackendQueryColumn::GetColumnId
#include "backend/query/queryParser.h"               // the arbitrary query is READ by the engine's own parser
#include "backend/query/queryRender.h"               // ibRenderQuery / ibQueryColumnFromPath — the seed query, written out
#include "backend/backend_exception.h"               // the engine's verdict on a query that will not resolve
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
		// The wrapped column already tells the declaration from what a value may be — pass it on.
		virtual const ibTypeDescription GetColumnTypeValue() const override { return m_col != nullptr ? m_col->GetTypeValueDesc() : ibTypeDescription(); }
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

// The same collection over an ARBITRARY QUERY's output schema. A query's column is an
// ibBackendQueryColumn exactly as a queryable's is (the lowering hands one back for every output,
// synthesising it where the column is computed), so this is the same class with a different way in
// — not a second idea of what a column is.
class ibQuerySchemaColumns : public ibValueModelCursor::ibValueModelColumnCollection {
public:
	class ColInfo : public ibValueModelColumnInfo {
	public:
		ColInfo(wxString name, const ibBackendQueryColumn* col) : m_name(std::move(name)), m_col(col) {}
		virtual unsigned int GetColumnID() const override { return m_col != nullptr ? m_col->GetColumnId() : 0; }
		// THE QUERY'S NAME FOR IT, not the underlying column's. `Owner.Code AS Supplier` is called
		// `Supplier` here and nowhere else — that is the whole point of writing the query.
		virtual wxString GetColumnName() const override { return m_name; }
		virtual wxString GetColumnCaption() const override { return m_name; }
		virtual const ibTypeDescription GetColumnType() const override { return m_col != nullptr ? m_col->GetTypeDesc() : ibTypeDescription(); }
		virtual const ibTypeDescription GetColumnTypeValue() const override { return m_col != nullptr ? m_col->GetTypeValueDesc() : ibTypeDescription(); }
	private:
		wxString                    m_name;
		const ibBackendQueryColumn* m_col;
	};

	explicit ibQuerySchemaColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) {
		for (const ibQueryLowering::OutputColumn& oc : schema)
			m_cols.push_back(new ColInfo(oc.m_name, oc.m_col));
	}
	virtual ~ibQuerySchemaColumns() { for (auto* c : m_cols) delete c; }

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

	// THE ARBITRARY QUERY, OVER THE MAIN TABLE. The query decides what is READ and therefore which
	// columns exist; the main table — resolved above and left untouched — decides whose rows they are,
	// which is what the commands, the icon and the choice value hang off. Neither replaces the other.
	//
	// The columns come from the ENGINE describing the text (resolve, do not run), so the moment the
	// query changes the list is offering the new fields: to its columns, and through GetSourceExplorer
	// to every picker in the settings dialog. That was the gap — the composer took the text and
	// nothing downstream knew what came out of it.
	m_querySchema.clear();
	m_queryError.clear();
	if (IsArbitraryQuery() && !GetArbitraryQueryText().IsEmpty()) {
		m_composer.FromText(GetArbitraryQueryText());

		// The names resolve against the SOURCE's config, the same one the composer was just handed.
		const ibSourceMetaDataScope scope(GetSourceMetaData());
		try {
			ibQueryParser parser;
			const ibQuerySelectPtr ast = parser.Parse(GetArbitraryQueryText());
			if (ast)
				ibQueryLowering::DescribeOutput(*ast, {}, m_querySchema);
		}
		catch (const ibBackendException& error) {
			// AT ONCE, AND IN THE ENGINE'S WORDS. A query that cannot be described is a query that
			// cannot be run, and finding that out when the list is first opened — in front of a user
			// rather than its author — is the thing worth avoiding.
			m_queryError = error.GetErrorDescription();
			m_querySchema.clear();
		}
		m_columns = new ibQuerySchemaColumns(m_querySchema);   // ibValuePtr owns
		PruneUnresolvedSettings();   // the query decides which fields exist; the settings follow it
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
	PruneUnresolvedSettings();   // ...and a source change decides it too
}

void ibValueDynamicList::SetCustomQuery(const wxString& queryText)
{
	m_composer.FromText(queryText);   // the dynamic list's own DB composer (FromText is DB-only)
}

// THE QUERY THE MAIN TABLE WOULD WRITE FOR ITSELF. Not a template with a hole in it — a real query
// over the real source, rendered by the same renderer the constructor round-trips through, so what
// appears when the box is ticked is something that already runs and can be opened in the constructor
// on the spot.
//
// Every column, spelled out rather than `SELECT *`: the point of switching this on is to CHANGE what
// is read, and changing a list of fields you can see is an edit, while changing a star is a rewrite.
wxString ibValueDynamicList::SeedArbitraryQuery() const
{
	const ibBackendQueryable* queryable = GetSourceQueryable();
	if (queryable == nullptr)
		return wxEmptyString;

	// THE NAME THE LANGUAGE KNOWS IT BY — `Catalog.Products`, the descriptor's own namespace and name,
	// which is exactly what the lowering resolves a FROM against. Not GetQueryName(): that is the
	// physical table, and a query written against a physical table would be a query the config cannot
	// be restructured under.
	const ibQueryableSourceDescriptor* descriptor = GetSourceDescriptor();
	if (descriptor == nullptr || descriptor->GetName().IsEmpty())
		return wxEmptyString;

	ibQuerySelect select;
	if (!descriptor->GetNamespace().IsEmpty())
		select.m_from.m_name.push_back(descriptor->GetNamespace());
	select.m_from.m_name.push_back(descriptor->GetName());

	// ⚠ NAMED, AND EVERY FIELD QUALIFIED BY THAT NAME — `Catalog.Products AS Products`, then
	// `Products.Code`. Not decoration: a bare `Code` is unambiguous only until a SECOND table joins,
	// and at that moment EVERY field written before it becomes ambiguous at once. The engine then
	// refuses them, the refresh drops what no longer resolves, and the whole field list disappears in
	// one gesture that had nothing to do with those fields. (Seen live: adding one field from the
	// catalogue collapsed a seeded query to `SELECT *`.)
	//
	// This is the same rule the query constructor holds to when IT adds a table, and the seed has to
	// hold to it too — a starting query that breaks on the first edit is worse than no starting query.
	select.m_from.m_alias = descriptor->GetName();
	const wxString prefix = select.m_from.m_alias + wxT(".");

	for (const ibBackendQueryColumn* column : queryable->GetColumns()) {
		if (column == nullptr || column->GetName().IsEmpty() || !column->IsAllowed())
			continue;
		ibQueryProjection projection;
		projection.m_expr = ibQueryColumnFromPath(prefix + column->GetName());
		select.m_projections.push_back(std::move(projection));
	}
	if (select.m_projections.empty())
		select.m_selectAll = true;   // a source that vends no columns: read it whole rather than read nothing

	return ibRenderQuery(select);
}

// TICKING IT SEEDS, UNTICKING IT CLEARS. The flag is not a mode with its own empty state: switching
// it on means "let me change what is read", so there is something to change; switching it off means
// the main table alone is the read, so the text goes rather than lying in wait for the next tick.
void ibValueDynamicList::SetArbitraryQuery(bool use)
{
	if (use == IsArbitraryQuery())
		return;

	m_propertyUseCustomQuery->SetValue(use);
	if (use) {
		if (GetArbitraryQueryText().IsEmpty())
			m_propertyCustomQuery->SetValue(SeedArbitraryQuery());
	}
	else {
		m_propertyCustomQuery->SetValue(wxEmptyString);
	}
	RebuildSource();   // SetValue does not fire OnPropertyChanged - the columns and the pickers follow HERE
}

// DROP THE SETTINGS WHOSE FIELD THE LIST NO LONGER HAS.
//
// Taking the arbitrary query away takes its columns with it, and a filter, a sort or a grouping over
// one of those is left pointing at nothing. Same for a table dropped out of the query, an attribute
// renamed in the configuration, a metaobject deleted.
//
// ⚠ NOTHING CHASES THE CHANGE. The list re-asks "does this still resolve?" after every rebuild, and
// what does not, goes. A cleanup hung off the untick would cover the untick and nothing else.
//
// The answer comes from the SOURCE EXPLORER, which is the same list the pickers are built from — so
// what a person can still choose and what the list still keeps are one answer, not two.
void ibValueDynamicList::PruneUnresolvedSettings()
{
	const ibSourceExplorer* explorer = GetSourceExplorer();
	if (explorer == nullptr || explorer->GetHelperCount() == 0)
		return;   // nothing to check against is not a verdict — leave the settings alone

	m_composer.PruneUnresolvedSettings([explorer](const wxString& path) {
		// THE FIRST SEGMENT is what has to exist here: the rest is a reference walk, and a walk
		// resolves through the metadata of whatever the first segment turned out to be.
		const wxString head = path.BeforeFirst(wxT('.'));
		if (head.IsEmpty())
			return true;
		return explorer->FindByName(head) != nullptr;
	});
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

// ⭐⭐ A SORT IS ONLY A SORT IF THE SOURCE HAS THAT COLUMN.
//
// The metaobject hands a column in; the composer writes its NAME into query text. A column the queryable
// does not carry therefore leaves the source and returns as a word in `ORDER BY` that resolves to nothing
// — and the whole list query dies, which a list shows as an empty window, indistinguishable from a source
// with no rows.
//
// It is not hypothetical: an ENUMERATION passed its `Order` attribute here, from a time when that was a
// physical column. Its table is the uuid key plus seed rows now, so the source vends `Ref` and nothing
// else, and every enum choice list rendered `SELECT Ref FROM Temp.t0 ORDER BY Order` — refused by the
// parser, since `Order` is also a keyword. One stale argument, and no enum could be picked at all.
//
// Asked of the queryable rather than fixed at the four call sites: the factory is the one place that holds
// BOTH facts (the source and the column), so a metaobject cannot get this wrong again by passing a column
// that used to exist.
static bool ibSourceHasColumn(const ibBackendQueryable* queryable, const ibBackendQueryColumn* column)
{
	if (queryable == nullptr || column == nullptr)
		return false;
	for (const ibBackendQueryColumn* col : queryable->GetColumns())
		if (col == column)
			return true;
	return false;
}

// Create a list with a DEFAULT sort set at creation — the metaobject passes its presentation column. The sort lands
// on the composer (the store) so the serializer saves it and the user can remove it; NOT re-applied at runtime.
ibValueDynamicList* ibCreateList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* defaultSort, ibDynamicListView view)
{
	ibValueDynamicList* list = new ibValueDynamicList(queryable, view);
	if (ibSourceHasColumn(queryable, defaultSort))
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
		if (ibSourceHasColumn(queryable, folderCol))
			list->AddSort(folderCol->GetName(), /*ascending*/false);   // folders first (IsFolder true sorts before false)
	}
	if (ibSourceHasColumn(queryable, presentationCol))
		list->AddSort(presentationCol->GetName());
	return list;
}

// Folder-select list — presentation sort + a fixed `IsFolder = true` filter (only folders). The metaobject passes
// the columns (folder-select is a filter setting; no structural GetFolderColumn). `view` seeds the default view,
// same as the sibling factories — the folder-select call site passes ibDynamicListView_Choice.
ibValueDynamicList* ibCreateFolderList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* folderCol, const ibBackendQueryColumn* presentationCol, ibDynamicListView view)
{
	ibValueDynamicList* list = new ibValueDynamicList(queryable, view);
	if (ibSourceHasColumn(queryable, presentationCol))
		list->AddSort(presentationCol->GetName());
	if (ibSourceHasColumn(queryable, folderCol))
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
	if (holder == nullptr)
		return ibUniqueKey();
	// A RESTORE STUB IS A ROW WE KNOW THE KEY OF, not a row without one. FindRowValue answers with a key-only
	// node (m_rowKey set, no cells) and that node becomes the current row after a save — the bootstrap's Select
	// is programmatic and fires no SELECTION_CHANGED, so nothing replaces it until the user clicks. Decoding
	// identity from the CELLS alone therefore returned nothing and every by-key command (Copy / Edit / Delete /
	// MarkAsDelete) silently did nothing on a freshly created element. Resolve the row by its key first — the
	// SAME point lookup the keyset anchor and the breadcrumb walk already run on a stub — and decode identity
	// from what comes back. One question ("what row is this?"), one existing answer, asked in a third place.
	if (node->IsKeyOnlyAnchor()) {
		ibRowMetaValues resolved;
		for (const auto& cell : ResolveAnchorByKey(node->GetRowKey()))
			resolved.insert_or_assign(cell.first, cell.second);
		return holder->GetItemKey(resolved);
	}
	return holder->GetItemKey(node->GetTableValues());
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

	// …AND WHAT THE ARBITRARY QUERY ADDS ON TOP. The main table's fields are there because the list is
	// that table; the query's output columns are there because that is what the list actually shows —
	// and a field a person can SEE in the list but cannot filter, sort or group by is the defect Max
	// reported ("I add fields to the query and I do not see them in the filters").
	//
	// Added, not substituted: a query that renames `Owner.Code` to `Supplier` still leaves every field
	// of the main table reachable, because the row is still a row of that table.
	//
	// (A name the main table already offers is not added twice — the explorer would show one field
	// under two entries that mean the same column.)
	for (const ibQueryLowering::OutputColumn& column : m_querySchema) {
		if (column.m_name.IsEmpty())
			continue;
		const bool already = m_sourceExplorer.FindByName(column.m_name) != nullptr;
		if (already)
			continue;

		// The column's own DESCRIPTOR when the query did not rename it — that is what carries the
		// synonym and the binding, so a plain projection reads in the pickers exactly as the field
		// does. A RENAMED one (`Owner.Code AS Supplier`) is appended by name and type: the new name is
		// the query's, and it belongs to no attribute to borrow a synonym from.
		if (column.m_col != nullptr && column.m_col->GetName().IsSameAs(column.m_name, false))
			m_sourceExplorer.AppendColumn(column.m_col);
		else
			m_sourceExplorer.AppendColumn(column.m_name,
				column.m_col != nullptr ? column.m_col->GetColumnId() : wxNOT_FOUND,
				column.m_col != nullptr ? column.m_col->GetTypeDesc() : ibTypeDescription());
	}
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
// (The old ⚠ here — "RunComposerPage requires GetSourceQueryable() != null, so a custom-text source must vend a
//  queryable to page through" — is answered by the MAIN TABLE being mandatory. The list always has its queryable;
//  the arbitrary query changes what is READ, never whose rows they are. The composer wraps the author's text as a
//  nested source when there are settings over it (ibDataDBComposer::RenderText), so the paged read is the ordinary
//  one. What the query MUST carry for a row to be openable is the main table's key columns — which is why the
//  seed (SeedArbitraryQuery) selects every column rather than a chosen few.)

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
	// BOTH, ALWAYS. The main table is what the list IS (its commands, its icon, the value a choice
	// hands back), and the arbitrary query is what it READS — so neither is written in place of the
	// other. An older blob that stored only one loads with the other simply absent, which is the same
	// state as "not set".
	m_propertyUseCustomQuery->SetNodeValue(node.GetProperty(m_propertyUseCustomQuery->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));   // resolves the queryable from the id
	m_propertyCustomQuery->SetNodeValue(node.GetProperty(m_propertyCustomQuery->GetName()));
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
	node.SetProperty(m_propertyUseCustomQuery->GetName(), m_propertyUseCustomQuery->GetNodeValue());

	// ⚠ THE MAIN TABLE IS REQUIRED, arbitrary query or not. The Source property's write FAILS when no
	// queryable is picked (ibPropertyDynamicSource::WriteNodeValue) — forbid serialising an INCOMPLETE
	// list, which without a main table would load as a grid nobody can act on: no commands, no icon,
	// no value to hand back from a choice. Propagate that verdict; the form attribute drops the whole
	// node on false.
	//
	// (It used to be an EITHER/OR, and that was the mistake: a query was allowed to stand IN PLACE OF
	// the source, which took the list's identity away along with its data.)
	// ASKED OF THE PROPERTY, not inferred from what it wrote. This used to test the verdict of
	// WriteNodeValue — which answers `true` whether or not a source was picked (it just writes
	// nothing when there is none), so the check never fired and the node went out sourceless.
	if (m_propertySource->IsEmptyProperty())
		return false;
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	// …and the query over it, when there is one.
	node.SetProperty(m_propertyCustomQuery->GetName(), m_propertyCustomQuery->GetNodeValue());

	// GetListSettings() is the live FACADE over the composer (the store), so this serialises the
	// composer's current Filter, Order AND Group — all three, through the facade's own Count/Get/Add.
	// (It wrote the filter alone until 2026-08-07: sort and grouping live in the composer in facade
	// mode, the buffer fields it read were empty, and a sort set on a live list never reached disk.)
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
