#include "backend/system/value/valueDynamicList.h"
#include "backend/appData.h"                         // appData / GetActiveMetaData
#include "backend/query/queryable.h"                 // ibBackendQueryable
#include "backend/query/queryableFactory.h"          // ibQueryableSourceDescriptor (source holder + its command surface)
// (listFetchDriver.h / session.h(ses_query) / modelView.h(s_constIgnoreParent) includes removed — they were
//  only for the deleted RunPage/ibDynamicListProvider; the fetch lives in the base RunComposerPage now.)
#include "backend/query/queryColumn.h"               // ibBackendQueryColumn::GetColumnId
#include "core/query/queryParser.h"               // the arbitrary query is READ by the engine's own parser
#include "core/query/queryRender.h"               // ibRenderQuery / ibQueryColumnFromPath — the seed query, written out
#include "backend/backend_exception.h"               // the engine's verdict on a query that will not resolve
#include "backend/srcDataObject.h"                      // ibSourceExplorer
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — GetItemKey row guid
#include "core/serialize/dataBuilder.h"            // ibDataNode (object-level save/load)
#include "backend/backend_composition.h"             // ibBackendCompositionDescriptionMemory — read against its configuration
#include "backend/metadataConfiguration.h"            // ibMetaDataConfigurationBase (GetSourceMetaData)
#include "core/picturePredefined.h"                // g_picRowFolderCLSID — what a grouping's heading wears

#include <algorithm>                                  // std::find — a name's id already taken in the query's columns

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
		virtual ibString GetColumnName() const override { return m_col != nullptr ? m_col->GetName() : ibString(); }
		// …and how it reads: the column's synonym, as a metaobject's column reads (commonObject.h) — the name is the designer's.
		virtual ibString GetColumnCaption() const override { return m_col != nullptr ? m_col->GetSynonym() : ibString(); }
		// Name + id + TYPE straight off the column (an attribute IS-A column → GetTypeDesc is free).
		// Enough to render/edit a cell type-aware; for 90% of sources nothing else is needed.
		virtual const ibTypeDescription GetColumnType() const override { return m_col != nullptr ? m_col->GetTypeDesc() : ibTypeDescription(); }
		// The wrapped column already tells the declaration from what a value may be — pass it on.
		virtual const ibTypeDescription GetColumnTypeValue() const override { return m_col != nullptr ? m_col->GetTypeValueDesc() : ibTypeDescription(); }
		virtual bool IsColumnAvailable() const override { return m_col == nullptr || m_col->IsAvailable(); }
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
		ColInfo(ibString name, const ibBackendQueryColumn* col, ibMetaID id, bool walkAvailable = true)
			: m_name(std::move(name)), m_col(col), m_id(id), m_walkAvailable(walkAvailable) {}
		virtual unsigned int GetColumnID() const override { return m_id; }
		// THE QUERY'S NAME FOR IT, not the underlying column's. `Owner.Code AS Supplier` is called
		// `Supplier` here and nowhere else — that is the whole point of writing the query.
		virtual ibString GetColumnName() const override { return m_name; }
		virtual ibString GetColumnCaption() const override { return m_name; }
		virtual const ibTypeDescription GetColumnType() const override { return m_col != nullptr ? m_col->GetTypeDesc() : ibTypeDescription(); }
		virtual const ibTypeDescription GetColumnTypeValue() const override { return m_col != nullptr ? m_col->GetTypeValueDesc() : ibTypeDescription(); }
		// The column's own answer, asked live — AND what the query knew of the road to it: a walk read
		// through its real LEAF hands over that leaf, which answers for itself alone
		// (OutputColumn::m_available).
		virtual bool IsColumnAvailable() const override { return m_walkAvailable && (m_col == nullptr || m_col->IsAvailable()); }
	private:
		ibString                    m_name;
		const ibBackendQueryColumn* m_col;
		ibMetaID                    m_id;
		bool                        m_walkAvailable;
	};

	// ⭐⭐ AN OUTPUT IS KNOWN BY ITS NAME, AND ITS ID FOLLOWS THE NAME. A form's column is bound to the list by
	// id (ibSourceHop) and that id goes to the file, while the id the query gives a renamed or computed output
	// is the lowering's — a kind and an ordinal (ibBackendQueryColumn::SyntheticId), which nothing is meant to
	// persist. So it moved whenever the query did: `Stock.QuantityBalance AS Stock` bound a column to the
	// balance's figure, the join became a grouped nested query, the output kept its name, got another number,
	// and the column went blank (2026-10-28). A declared attribute under its own name keeps the attribute's id
	// — the row's key, its folder flag and the commands read it by that id; every other output is numbered
	// from its name.
	explicit ibQuerySchemaColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) {
		std::vector<ibMetaID> taken;
		for (const ibQueryLowering::OutputColumn& oc : schema) {
			ibMetaID id = wxNOT_FOUND;
			if (oc.m_col != nullptr && !ibBackendQueryColumn::IsSyntheticId(oc.m_col->GetColumnId())
			    && oc.m_col->GetName().IsSameAs(oc.m_name, false))
				id = oc.m_col->GetColumnId();
			else {
				// (Body 0 would be -1, which every reader takes for "none" — so the bodies start at 1. Two names
				//  of one hash — about one list in a hundred thousand — take the next body, not one cell.)
				ibMetaID body = static_cast<ibMetaID>(ib_clsid_hash(oc.m_name.Lower()) % (s_nameBodies - 1)) + 1;
				id = ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Output, body);
				while (std::find(taken.begin(), taken.end(), id) != taken.end()) {
					body = body % static_cast<ibMetaID>(s_nameBodies - 1) + 1;
					id = ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Output, body);
				}
			}
			taken.push_back(id);
			m_cols.push_back(new ColInfo(oc.m_name, oc.m_col, id, oc.m_available));
		}
	}
	virtual ~ibQuerySchemaColumns() { for (auto* c : m_cols) delete c; }

	virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const override {
		return idx < m_cols.size() ? m_cols[idx] : nullptr;
	}
	virtual unsigned int GetColumnCount() const override { return static_cast<unsigned int>(m_cols.size()); }

private:
	// The bodies a name is numbered within — as many as still take a kind (CanComposeSyntheticId).
	static constexpr ibClassID s_nameBodies = ibClassID(1) << 25;
	static_assert(ibBackendQueryColumn::CanComposeSyntheticId(static_cast<ibMetaID>(s_nameBodies - 1)),
		"a name's body must still take a kind");

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
	// ⚠ THE SOURCE'S OWN CONFIGURATION FIRST, the ACTIVE one only if there is no source to ask. A
	// queryable lives in the configuration it was created in — the edited one in the designer, the
	// copy's own on a copy — and resolving against the active configuration there would answer with
	// somebody else's tables.
	//
	// ⚠⚠ A LIST BELONGING TO A CONFIGURATION MUST NEVER FALL BACK TO THE ACTIVE ONE — it would see THE
	// WRONG TABLES. Two configurations are open in the designer at once, and the active one is not
	// necessarily this object's; a name resolved there answers with somebody else's metadata.
	//
	// The line below is INSURANCE, not the road: it catches the list CREATED IN CODE
	// (`New DynamicList()` in a script, one a form builds), which has nowhere to take a configuration
	// from and where exactly one is running anyway.
	if (queryable != nullptr) {
		if (const ibValueMetaObjectGenericData* mo = queryable->GetSourceMetaObject())
			m_sourceMetaData = mo->GetMetaData();
	}
	if (m_sourceMetaData == nullptr)
		m_sourceMetaData = activeMetaData;

	// ⭐⭐ A LIST'S COLUMNS ARE ITS SOURCE'S, and that is said ONCE, here (Max, 2026-08-28). It chose
	// them by choosing the source, so "nothing selected" can only mean "everything the source has" —
	// the opposite of what it means for a REPORT, where the columns are what a person deliberately
	// put in the selected fields. See ibDataComposer::ReadsEveryField: it is not a preference, it is
	// which of the two questions this composition asks, and only the host knows.
	m_composer.SetReadsEveryField(true);

	if (queryable != nullptr)
		SetSourceQueryable(queryable);   // null → set later via SetSource

	// Refresh was declared and never bound, so a script could not call it — and
	// nothing else on the list either. GetMetadata is how a list form learns
	// which object it is listing (a custom query has none, and answers empty).
	m_members.Bind(this, &ibValueDynamicList::FillMembers);
}

// 🛑 THE READ IS WAITED OUT BEFORE THE LIST GOES — here, in the class whose parts it walks, as the
// composition does. The run reads this list's composer (ibValueModelCursor::m_composer) and asks this
// list's own overrides; the base destructor waited too, but by then both were gone: a form closed on a
// list still grouping a million rows, the read heard the cancel, and its catch put the grouping back
// into a destroyed composer (2026-10-03/05, "front() called on empty vector" in PutGroups).
ibValueDynamicList::~ibValueDynamicList() { CancelFetch(); }

// --- source (the list starts empty) ----------------------------------------

void ibValueDynamicList::SetSource(const ibString& ns, const ibString& name)
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

	// ⭐⭐ THE MAIN TABLE LANDS IN THE DESCRIPTION HERE — every road that changes the source passes this
	// point: the grid's edit (OnChildChanged), the drop-down and the "Select source" dialog (SetSource),
	// a queryable handed in (SetSourceQueryable). It was written in OnPropertyChanged alone, so a source
	// picked in the dialog left `m_mainTable` unset: the settings window offered no fields, and the
	// description went to the file without its anchor (Max, 2026-10-13).
	GetCompositionDesc().m_mainTable = m_propertySource->GetTableId();

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
	m_sourceExplorer.Clear();   // the source or its query changed — described anew on the next read
	if (IsArbitraryQuery()) {
		m_composer.FromText(GetArbitraryQueryText());

		// The names resolve against the SOURCE's config, the same one the composer was just handed.
		const ibSourceMetaDataScope scope(GetSourceMetaData());
		try {
			ibQueryParser parser;
			const ibQuerySelectPtr ast = parser.Parse(GetArbitraryQueryText());
			if (ast)
				ibQueryLowering::DescribeOutput(*ast, {}, m_querySchema);
		}
		catch (const ibCoreException&) {
			// A QUERY THAT CANNOT BE DESCRIBED HAS NO COLUMNS — that is the whole of what this road
			// has to decide. The engine's WORDS are told to whoever can act on them: the settings
			// window asks the text the same question and shows the answer under the editor.
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
		// FromSource rebinds the source — drive the settings onto the composer again so a source change
		// does NOT silently drop the Filter/Order/Group the user configured. The settings DATA lives in
		// the description (untouched here); only the composer source changed.
		RefreshComposerSettings();
	}
	PruneUnresolvedSettings();   // ...and a source change decides it too
}

void ibValueDynamicList::SetCustomQuery(const ibString& queryText)
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
ibString ibValueDynamicList::SeedArbitraryQuery() const
{
	const ibBackendQueryable* queryable = GetSourceQueryable();
	if (queryable == nullptr)
		return ibEmptyString;

	// THE NAME THE LANGUAGE KNOWS IT BY — `Catalog.Products`, the descriptor's own namespace and name,
	// which is exactly what the lowering resolves a FROM against. Not GetQueryName(): that is the
	// physical table, and a query written against a physical table would be a query the config cannot
	// be restructured under.
	const ibQueryableSourceDescriptor* descriptor = GetSourceDescriptor();
	if (descriptor == nullptr || descriptor->GetName().IsEmpty())
		return ibEmptyString;

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
	const ibString prefix = select.m_from.m_alias + ibT(".");

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

// (⛔ `SetArbitraryQuery(bool)` STOOD HERE — "ticking it seeds, unticking it clears". It had no
//  callers, and the flag it drove is gone: HAVING a query is what says the list runs one. The seed
//  it wrote lives on as `SeedArbitraryQuery` for whoever offers a "start me a query" button.)

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

	m_composer.PruneUnresolvedSettings([explorer](const ibString& path) {
		// THE FIRST SEGMENT is what has to exist here: the rest is a reference walk, and a walk
		// resolves through the metadata of whatever the first segment turned out to be.
		const ibString head = path.BeforeFirst(ibT('.'));
		if (head.IsEmpty())
			return true;
		return explorer->FindByName(head) != nullptr;
	});
}

// ⭐ WHAT THE LIST DECLARES, PUT BACK. Rebinding the source rebuilds the composer from scratch, so
// the settings the list itself holds have to be stated again — otherwise changing a source silently
// dropped the filter and sort that came with the list.
void ibValueDynamicList::RefreshComposerSettings()
{
	// ⭐ THE VARIANTS — restated after a rebind, because rebinding builds the composer from scratch.
	// What a reader saved is a setting of its own and survives this untouched; writing the author's
	// into that slot instead would wipe their filter every time the source was re-bound
	// (Max, 2026-08-24).
	GetModelComposer().LoadVariants(GetCompositionDesc().m_variants);
	NotifyReset();
}

// ⭐ ADD A FILTER — a line in the READER's setting, like every other filter there is. It used to go
// into a flat store of the composer's own, invisible to the settings window and outranked by
// anything saved there; a filter a script states and a filter a person types are the same fact and
// now live in the same place (Max, 2026-08-24: "if you set a filter somewhere, it is saved into the
// user setting").
void ibValueDynamicList::AddFilter(const ibString& path, const ibString& op, const ibValue& value)
{
	m_composer.Filter(path, op, value);
	NotifyReset();
}

// Add a sort — a line in the READER's setting, the same place a filter goes and the same place the
// settings window writes. The metaobject sets the list's default order at CREATION through here, so
// it arrives as an ordinary sort line the person can see and remove rather than a hidden default.
//
// 🛑 IT WENT TO A STORE THE RENDER PREFERRED THE SETTING OVER, so once a list had a sort setting the
// order stated here was dropped — and with it the column-heading click, which goes through the same
// door (2026-08-24).
void ibValueDynamicList::AddSort(const ibString& path, bool ascending)
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
	if (queryable == nullptr || column == nullptr || !column->IsAllowed())
		return false;   // switched off — vended by the source, answered by no find (see the composer's SELECT)
	for (const ibBackendQueryColumn* col : queryable->GetColumns())
		if (col == column)
			return true;
	return false;
}

// ⭐⭐ A LIST IS BORN WITH ITS REQUEST — the one place every factory below makes a list, and the only
// moment a composer reads what narrows it. Each `field = value` pair of the request becomes a filter line,
// put on HERE rather than inside the list, because a dynamic list has no business knowing what a request
// is: it knows filters, the factory knows why there is one. Exactly the arrangement a folder-select list
// already has — its `IsFolder = true` is a creation-time filter line set by its factory (Max, 2026-09-23:
// "before creating the list you simply assign it this filter"). Empty request, no lines.
static ibValueDynamicList* ibNewList(const ibCreateRequest& request, const ibBackendQueryable* queryable, ibDynamicListView view)
{
	ibValueDynamicList* list = new ibValueDynamicList(queryable, view);
	for (const std::pair<const ibString, ibValue>& parameter : request.m_condition.m_parameters)
		list->AddFilter(parameter.first, ibT("="), parameter.second);
	return list;
}

// Create a list with a DEFAULT sort set at creation — the metaobject passes its presentation column. The sort lands
// on the composer (the store) so the serializer saves it and the user can remove it; NOT re-applied at runtime.
ibValueDynamicList* ibCreateList(const ibCreateRequest& request, const ibBackendQueryable* queryable, const ibBackendQueryColumn* defaultSort, ibDynamicListView view)
{
	ibValueDynamicList* list = ibNewList(request, queryable, view);
	if (ibSourceHasColumn(queryable, defaultSort))
		list->AddSort(defaultSort->GetName());
	return list;
}

// Hierarchy list — folder-first sort (folders on top) then presentation. Both are ordinary creation-time sorts on
// the composer; the metaobject passes the columns. The TREE comes from the queryable's hierarchy (parent) column.
ibValueDynamicList* ibCreateHierarchyList(const ibCreateRequest& request, const ibBackendQueryable* queryable, const ibBackendQueryColumn* folderCol, const ibBackendQueryColumn* presentationCol, ibDynamicListView view)
{
	ibValueDynamicList* list = ibNewList(request, queryable, view);
	// ⚠ A FOLDER COLUMN THE SOURCE DOES NOT HAVE IS NO FOLDER COLUMN. An arrangement without folders (a chart of
	// accounts nests items in items) switches IsFolder off, and the find by name does not answer a switched-off
	// field. One question for the display column and the sort.
	if (ibSourceHasColumn(queryable, folderCol)) {
		list->SetFolderColumn(folderCol);                          // folder rows render as drillable containers (even empty)
		list->AddSort(folderCol->GetName(), /*ascending*/false);   // folders first (IsFolder true sorts before false)
	}
	if (ibSourceHasColumn(queryable, presentationCol))
		list->AddSort(presentationCol->GetName());
	return list;
}

// Folder-select list — presentation sort + a fixed `IsFolder = true` filter (only folders). The metaobject passes
// the columns (folder-select is a filter setting; no structural GetFolderColumn). `view` seeds the default view,
// same as the sibling factories — the folder-select call site passes ibDynamicListView_Choice.
ibValueDynamicList* ibCreateFolderList(const ibCreateRequest& request, const ibBackendQueryable* queryable, const ibBackendQueryColumn* folderCol, const ibBackendQueryColumn* presentationCol, ibDynamicListView view)
{
	ibValueDynamicList* list = ibNewList(request, queryable, view);
	if (ibSourceHasColumn(queryable, presentationCol))
		list->AddSort(presentationCol->GetName());
	if (ibSourceHasColumn(queryable, folderCol))
		list->AddFilter(folderCol->GetName(), ibT("="), ibValue(true));   // only folders
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

void ibValueDynamicList::GetValueByRow(ibVariant& variant, const ibDataViewItem& item, unsigned col) const
{
	ibValueTreeNode* node = GetViewData<ibValueTreeNode>(item);
	if (node != nullptr)
		node->GetValue(col, variant);
}

bool ibValueDynamicList::SetValueByRow(const ibVariant& variant, const ibDataViewItem& item, unsigned col)
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

const ibUniqueKey& ibValueDynamicList::GetGuid() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return q->GetQueryTableGuid();   // the key its source's metaobject keeps

	static const ibUniqueKey none;   // no source picked yet
	return none;
}

// The dynamic list creates its own row key (the cursor base makes none): the row's primary-key REFERENCE (guid),
// read off the item's node — the same shape a regular list uses, since a dynamic list row references an object.
// The row's identity key comes from the METAOBJECT — the node carries no metaobject id ("the node has no id"), and a
// register has SEVERAL key columns, so only the metaobject can shape the family-correct key. The list snaps the
// source descriptor and forwards the row's value map; the metaobject decodes it (record → reference guid, register
// → composite record key). Same currency (rowValues) as GetItemSelectValue. Neutral surface (custom query) → empty.
// ⭐⭐ THE ROW'S VALUES — its own cells, or, for a restore stub, the ones its key resolves to.
//
// A RESTORE STUB IS A ROW WE KNOW THE KEY OF, not a row without one. FindRowValue answers with a key-only
// node (m_rowKey set, no cells) and that node becomes the current row after a save — the bootstrap's Select
// is programmatic and fires no SELECTION_CHANGED, so nothing replaces it until the user clicks. Decoding
// identity from the CELLS alone therefore returned nothing and every by-key command (Copy / Edit / Delete /
// MarkAsDelete) silently did nothing on a freshly created element. Resolve the row by its key first — the
// SAME point lookup the keyset anchor and the breadcrumb walk already run on a stub — and decode from what
// comes back. One question ("what row is this?"), one existing answer.
//
// 🛑 AND THE PICKER ASKS IT TOO. A choice list opened on a field that already holds a value stands on THAT
// value — which is a stub. Choose on it, and the select value was read off cells the stub does not have: the
// list handed back the row itself, the field refused it and went empty (Max, 2026-09-23: "choose a value,
// press choose again, and the second time it clears"; the journal: `list.select … found no reference cell`).
// The answer lived in GetItemKey only; now both ask it here.
ibRowMetaValues ibValueDynamicList::GetRowValues(ibValueModel::ibComposerNode* node) const
{
	if (!node->IsKeyOnlyAnchor())
		return node->GetTableValues();
	ibRowMetaValues resolved;
	for (const auto& cell : ResolveAnchorByKey(node->GetRowKey()))
		resolved.insert_or_assign(cell.first, cell.second);
	return resolved;
}

ibUniqueKey ibValueDynamicList::GetItemKey(const ibDataViewItem& item) const
{
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr)
		return ibUniqueKey();
	const ibQueryableSourceDescriptor* holder = GetSourceDescriptor();
	if (holder == nullptr)
		return ibUniqueKey();
	return holder->GetItemKey(GetRowValues(node));
}

// ⚠ NO LOOKUP FOR A KEY-ONLY ROW. GetItemKey resolves a restore stub by its key because a command needs the row;
// the picture is asked on every paint, and a point read per paint is the price of a picture - so a stub shows
// none until the fetch that replaces it.
//
// ⭐ A HEADING OF A GROUPING WEARS THE FOLDER'S PICTURE — the one a catalog's folder row wears: a heading holds the
// rows under it the way a folder does. It is no record of the source, so the source cannot answer for it, and with
// no picture at all a grouped list read as a column of bare lines (Max, 2026-09-29: "there should be a picture here
// too" — "take the folder's picture").
ibPictureID ibValueDynamicList::GetRowPicture(const ibDataViewItem& item) const
{
	ibValueModel::ibComposerNode* node = GetViewData<ibValueModel::ibComposerNode>(item);
	if (node == nullptr || node->IsKeyOnlyAnchor())
		return 0;
	if (node->IsGroup())
		return g_picRowFolderCLSID;
	const ibQueryableSourceDescriptor* holder = GetSourceDescriptor();
	return holder != nullptr ? holder->GetRowPicture(node->GetTableValues()) : 0;
}

// Selection-restore after a child-form save (createdValue / changedValue → the current row is re-found): the list
// is metadata-blind, so the ROW-KEY is built by the source descriptor (GetRowKeyByValue) — a record keys by its
// reference, a REGISTER decomposes its COMPOSITE key across the PK columns (the SAME the fetch stamps into
// m_rowKey). The stub carries only that key; the freshly-fetched batch matches it by m_rowKey (IsEqualTo). This is
// the per-list "find by key" the deleted models did — a register row would otherwise never restore, because the
// base's flat {value} stub carries one element and a register's key is multi-column. Custom-query source → base.
ibDataViewItem ibValueDynamicList::FindRowValue(const ibValue& varValue, const ibString& colName) const
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
	return holder != nullptr ? holder->GetSelectValue(GetRowValues(node)) : ibValue();
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
	if (!m_sourceExplorer.Reset(GetSourceMetaData()->GetFactoryCountChanges(),
		GetObjectTypeName(), GetObjectTypeName(), wxNOT_FOUND, g_valueDynamicListCLSID, /*tableSection*/true))
		return &m_sourceExplorer;   // built at this version of the metadata, of this source and query — read as it is
	// The list is a MIRROR: it resets the explorer and hands it to the source descriptor (the single bridge), which
	// forwards the fill to the metaobject — the same one that lists/runs commands and resolves select.
	if (!IsArbitraryQuery())
		if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
			holder->FillSourceExplorer(m_sourceExplorer);

	// ⭐ OVER AN ARBITRARY QUERY, WHAT THE QUERY READS — its output columns, since that is what the list
	// shows, and a field a person can SEE in the list but cannot filter, sort or group by is the defect Max
	// reported ("I add fields to the query and I do not see them in the filters").
	//
	// 🛑 …AND ONLY THAT. The main table's fields stood here too, "because the row is still a row of that
	// table" — true while the list read the table, false since it reads `SELECT * FROM (<query>) AS
	// AuthorQuery` (2026-08-27): a field the query does not select is not in what is read. Offered here, it
	// was kept by the settings' prune as well, and the catalog's own folders-first sort put `ORDER BY
	// IsFolder` on a query without it (2026-10-28). The main table's fields stay reachable through the
	// query's reference to its row (`Ref.Code`).
	//
	// (A name the query gives twice is offered once.)
	for (const ibQueryLowering::OutputColumn& column : m_querySchema) {
		if (column.m_name.IsEmpty())
			continue;
		const bool already = m_sourceExplorer.FindByName(column.m_name) != nullptr;
		if (already)
			continue;

		// The column's own DESCRIPTOR when the list knows the output by the column's id — a declared
		// attribute the query did not rename: that is what carries the synonym and the binding, so a plain
		// projection reads in the pickers exactly as the field does. Any other (`Owner.Code AS Supplier`, a
		// figure) is appended by name, type and the id the list numbered it with: the name is the query's,
		// and it belongs to no attribute to borrow a synonym from.
		const ibMetaID id = GetColumnIDByName(column.m_name);
		if (column.m_col != nullptr && column.m_col->GetColumnId() == id)
			m_sourceExplorer.AppendColumn(column.m_col);
		else
			m_sourceExplorer.AppendColumn(column.m_name, id, column.GetTypeDesc());
	}
	// The list's DEFAULT view rides onto the explorer: a Choice list stamps the choice flag the form auto-build copies
	// onto the mainTableBox (where it serialises per-form). A Normal list leaves it off. This is the ONE propagation.
	m_sourceExplorer.SetChoiceMode(m_view == ibDynamicListView_Choice);
	return &m_sourceExplorer;
}

ibString ibValueDynamicList::GetSourceCaption() const
{
	// Caption = the metaobject's "Type: Synonym" (Catalog: Products) when the source is metadata-backed; a custom-
	// query source has no metaobject, so fall back to the queryable's own name. GetSourceMetaObject comes THROUGH
	// the queryable (the same path the front reads the icon), so caption + icon stay consistent.
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return GetSourceMetaObject() ?
			stringUtils::GenerateSynonym(GetSourceMetaObject()->GetClassName()) + ibT(": ") + GetSourceMetaObject()->GetSynonym() : q->GetQueryName();

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

	return ibApplicationInstance::GetActiveMetaData();
}

const ibMetaData* ibValueDynamicList::GetSourceMetaData() const
{
	// READ — the STORED source config, captured from the picked queryable's metaobject when the source was set
	// (RebuildSource: ctor / load / source-change / copy). Returned straight from the field — NO owner-walk, NO
	// queryable re-resolve (GetSourceQueryable -> GetQueryable re-resolves through m_owner->GetMetaData,
	// which walks the form → would recurse here). TERMINAL, so a form's metadata may fall back to this list without
	// looping. Nothing stored yet (no source picked) -> the ACTIVE config. Selection is GetMetaData.
	return m_sourceMetaData != nullptr ? m_sourceMetaData : ibApplicationInstance::GetActiveMetaData();
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

// ⚠ NO Filter / Order / Group / Settings HERE FOR NOW. They vended the runtime wrapper that went
// under the knife on 2026-08-23 (composition/listFilter.h) — a half-built second model of what the
// list already is. What the list IS lives in its description (GetCompositionDesc); the script
// surface over it is rebuilt over the settled structure, in
// system/value/composition/valueComposerSettings.h. Offering a property that hands out nothing would
// be worse than not offering it: a script would set a filter and be told nothing went wrong.
enum Func {
	enRefresh,
	enGetMetadata,
};

void ibValueDynamicList::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProc(ibT("Refresh"), ibT("Refresh()"));
	helper.AppendFunc(ibT("GetMetadata"), ibT("GetMetadata()"));
}

bool ibValueDynamicList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case Func::enRefresh:
		RefreshComposerSettings();
		RefetchAll();
		return true;
	}
	return false;
}

bool ibValueDynamicList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case Func::enGetMetadata:
		// A custom query lists no object: the empty value.
		pvarRetValue = GetSourceMetaObject();
		return true;
	}
	return false;
}

// --- ibPropertyObject — the dynamic list IS a property object (properties surface onto the attribute) ---

// The form edits the list's OWN properties; when one changes (source / settings) the
// list re-applies it. The "hook" is a virtual on the property-object, not a function ptr.
void ibValueDynamicList::OnPropertyChanged(ibProperty* property, const ibVariant& /*oldValue*/, const ibVariant& /*newValue*/)
{
	// (The source and the settings are rebuilt in OnChildChanged — the change of a child is what
	//  starts the update, and it travels on up to the holder from there.)
	//
	// 🛑 THE ANCHOR WAS WRITTEN AT SERIALISE TIME, and that could not even compile: `WriteProperty`
	// is const, so `GetCompositionDesc()` hands back a const reference there (C3892). Which was the
	// design saying so out loud — a save must WRITE what is held, not decide it. It is written on the
	// change, in RebuildSource, which every change of source passes through.

	// ⭐ A MAIN TABLE SAID WITH THE SETTINGS IS PUT ON THE WIDGET THAT SHOWS IT, as a read puts it
	// (ReadProperty): the description is its home, the Source property its view. A whole composition
	// handed in with its main table left the Source empty, and RebuildSource wrote the empty one back.
	const ibCompositionDescription& description = GetCompositionDesc();
	if (m_propertySettings == property && description.HasMainTable()
		&& description.m_mainTable != m_propertySource->GetTableId())
		m_propertySource->SetTableId(description.m_mainTable);

	// The source/settings changed; re-commit the settings buffer onto the composer (L5).
	RefreshComposerSettings();
	// Toggling DynamicRead switches the fetch path (live keyset cursor ↔ whole-list RAM snapshot). Re-drive the fetch
	// so it takes effect: RefetchAll bumps the view generation → the snapshot rebuilds on the next fetch, or the cursor
	// resumes. (Nothing to rebuild in the source/composer — only the fetch route changes.)
	if (m_propertyDynamicRead == property)
		RefetchAll();
}

// A child changed — the source picked, or the settings (the query among them) coming back from their
// window. The columns are the source's plus what the query adds, so they are made again here; the news
// goes on up to the holder, which shows them (the form's attribute tree).
void ibValueDynamicList::OnChildChanged()
{
	RebuildSource();
	ibPropertyObject::OnChildChanged();
}

// Central save/load entry (ibPropertyObject::ReadProperty/WriteProperty override): the
// Source property + the settings (Filter/Order/Group) held OUTSIDE the property set.
bool ibValueDynamicList::ReadProperty(const ibDataNode& node)
{
	// ⭐⭐ A LIST LIVES ON THE COMMON COMPOSER, DEGENERATE (Max, 2026-08-23): its main table, its
	// query and the same filter / sort / grouping, with the structure, resources and variants a
	// report also has left empty. So it reads through the SAME description a report does — the parts
	// it does not use come back empty rather than being a second format.
	// …against its own configuration, like a report's (valueDataComposition.cpp): a saved filter
	// compares against references, and only the metadata can build those.
	//
	// ⭐ A LIST THAT HAS ITS SOURCE ALREADY KEEPS IT — the main list of an object form, made from the
	// object the form was opened for (ibValueForm::ReadAttributes): the file brings its children, the
	// query, the filter and the order, not whose rows they are.
	const ibMetaID given = m_propertySource->GetTableId();
	ibBackendCompositionDescriptionMemory::ReadNode(node, GetCompositionDesc(), GetSourceMetaData());

	// ⭐ …AND THE ANCHOR IS PUT BACK ON THE WIDGET THAT SHOWS IT. The main table lives in the
	// description (see WriteProperty); the Source property is its view, and it is filled here — as an
	// ID, without resolving. Nothing is registered yet at load time, and resolving now is exactly
	// what used to throw the value away (ibPropertyDynamicSource::ReadNodeValue).
	if (given != wxNOT_FOUND)
		GetCompositionDesc().m_mainTable = given;
	else
		m_propertySource->SetTableId(GetCompositionDesc().m_mainTable);

	// Default view — a hidden intrinsic field (absent on an old blob → Normal, forward-compatible).
	m_view = (ibDynamicListView)node.GetValue<std::int32_t>(ibT("View"));
	m_sourceExplorer.Clear();   // read anew — its source and its view with it

	// DynamicRead — stored INVERTED (DynamicReadOff: 1 = static/RAM-snapshot, absent/0 = the default dynamic read), so
	// a list blob written before this field loads as dynamic. (SetValue does not fire OnPropertyChanged.)
	m_propertyDynamicRead->SetValue(node.GetValue<std::int32_t>(ibT("DynamicReadOff")) == 0);

	// ⭐ …AND WHAT WAS READ IS BUILT, as an edit of the source or the settings builds it (OnChildChanged).
	// A list read from a file kept its query in the description and nothing else: the composer had no
	// source, the query's columns were described nowhere, and its fields reached no picker until
	// somebody opened the settings window. The id above is not resolved by this — only asked.
	RebuildSource();
	return true;
}

bool ibValueDynamicList::WriteProperty(ibDataNode& node) const
{
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

	// ⭐⭐ THE MAIN TABLE IS ALREADY IN THE DESCRIPTION — put there when the source was PICKED (see
	// RebuildSource), because that is when it is decided. A save writes what is held; it does
	// not decide anything, which is also why this method can be const.
	//
	// 🛑 IT REACHED THE FILE THROUGH NEITHER SIDE before: this override writes the description and
	// not the property set, and `m_mainTable` was set by no line in the tree — so the anchor a
	// person picked was serialised nowhere at all and the list came back sourceless. Same cure as
	// the arbitrary query: one home, and the widget reads it.

	// …AND THE REST GOES OUT AS THE COMMON DESCRIPTION — main table, query, and the same filter /
	// sort / grouping a report writes. A list fills three of its fields and leaves the others empty,
	// which is what "degenerate composer" means on disk as well as in the head.
	if (!ibCompositionDescriptionMemory::WriteNode(node, GetCompositionDesc()))
		return false;

	// (The settings went out a SECOND time here until the description arrived — the composer's filter,
	//  order and grouping are part of it, and writing them again beside it is how one record grows two
	//  answers to the same question.)

	// Default view — written implicitly as a hidden intrinsic field (see ibDynamicListView).
	node.SetValue<std::int32_t>(ibT("View"), (std::int32_t)m_view);

	// DynamicRead — record only the DEVIATION from the default (1 = static/RAM snapshot; 0/absent = dynamic). See ReadProperty.
	node.SetValue<std::int32_t>(ibT("DynamicReadOff"), IsDynamicRead() ? 0 : 1);
	return true;
}

// Register the type — runtime / designer know "DynamicList".
VALUE_TYPE_REGISTER(ibValueDynamicList, "DynamicList", g_valueDynamicListCLSID);
// The row / return-line type. When a choice has NO source-command handler (holder == null →
// GetItemSelectValue empty), the base ibValueModelReturnLine::GetSelectValue falls back to the row's
// OWN value (GetValue → this), so its C++ type MUST be in the ctor registry or GetTypeIDByRef asserts.
// Mirrors the value-table's VL_TVROW registration — missed in the dynamic-list migration.
SYSTEM_TYPE_REGISTER(ibDynamicListReturnLine, "DynamicListRow", system_to_clsid("VL_DLROW"));
