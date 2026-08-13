#ifndef __VALUE_DYNAMIC_LIST_H__
#define __VALUE_DYNAMIC_LIST_H__

#include "backend/model.h"                       // ibValueModelCursor
#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject
#include "backend/composition/dataComposer.h"        // L5 — ibDataDBComposer
#include "backend/composition/listFilter.h"          // ibValueListSettings
#include "backend/propertyManager/propertyObject.h"  // ibPropertyObject — the dynamic list IS a property object
#include "backend/propertyManager/property/propertyDynamicSource.h"  // ibPropertyDynamicSource — the "Source" property
#include "backend/propertyManager/property/propertyDynamicList.h"  // ibPropertyDynamicList — the "Settings" → "Open" action
#include "backend/propertyManager/property/propertyBoolean.h"  // ibPropertyBoolean — the "arbitrary query" flag
#include "backend/propertyManager/property/propertyString.h"   // ibPropertyString — the custom query text
#include "backend/query/queryLowering.h"              // ibQueryLowering::OutputColumn — what the arbitrary query produces

class ibBackendQueryable;
class ibBackendQueryColumn;
class ibQueryableSourceDescriptor;

constexpr ibClassID g_valueDynamicListCLSID = value_to_clsid("VL_DLST");

// View kind of the dynamic list — a regular list, or a selection (choice) list where activating a row picks the
// value into the owner. It is the list's DEFAULT behaviour and is SERIALISED (implicitly — a hidden intrinsic
// field, not a user-facing property). On drop onto a form GetSourceExplorer stamps the explorer's choice flag from
// it, which the form auto-build copies onto the TableBox — where it ALSO serialises. So both carry it: the list's
// value is the default a fresh form inherits; the TableBox copy is the per-form override the user may clear.
enum ibDynamicListView {
	ibDynamicListView_Normal = 0,
	ibDynamicListView_Choice,
};

// ---------------------------------------------------------------------------
// ibValueDynamicList — THE universal dynamic list. Works through a QUERYABLE
// via the L5 composer; knows NOTHING about metadata.
//
//   * Created EMPTY — SetSource(ns,name) adds the queryable (a register / a
//     reference / anything in the queryable factory). SetCustomQuery for an
//     arbitrary query.
//   * Base = ibValueModelCursor (a tree understands a flat list — one root).
//   * L5 (m_composer) is the visible query layer; it pulls data off the queryable.
//   * Fetch is THIN: it runs the composer onto a special driver/provider that
//     emits rows straight into the table (OnRow → node). All work lives there.
//   * Row identity = the KEYSET (primary-key column VALUES) — no guid, no ref.
//   * Settings (Filter/Order/Group) are APPLIED ON CHANGE onto the composer (not
//     cleared+reapplied per fetch).
// ---------------------------------------------------------------------------
// The dynamic list is AT ONCE a runtime model (a tree), a form data source, AND a property
// object (like ibValueSizerItem): its properties surface onto the form attribute. The attribute
// merely casts its runtime value to ibPropertyObject and shows that object's properties — it
// knows nothing about "a dynamic list" (anything property-bearing can sit there).
class BACKEND_API ibValueDynamicList : public ibValueModelCursor, public ibSourceDataObject, public ibPropertyObject {
public:

	// (ibDynamicListNode REMOVED — the dynamic list's rows are ibComposerNode now, produced by the
	//  base RunComposerPage. The keyset / group-path identity it carried is the composer node's backing-index /
	//  group-path; the read path uses the base GetViewData<ibValueTreeNode>.)

	// The source may be passed here too — null means "set it later" (SetSource).
	// `view` seeds the DEFAULT view (Normal / Choice) — see ibDynamicListView; it serialises and re-loads.
	ibValueDynamicList(const ibBackendQueryable* queryable = nullptr, ibDynamicListView view = ibDynamicListView_Normal);
	virtual ~ibValueDynamicList();

	// --- source & query (L5) ------------------------------------------------
	// The list starts EMPTY; SetSource/SetSourceQueryable install the queryable,
	// SetCustomQuery an arbitrary query. The source variable itself lives in the
	// Source property (read via the GetSourceQueryable facade), not on the list.
	void SetSource(const wxString& ns, const wxString& name);
	void SetSourceQueryable(const ibBackendQueryable* queryable);
	void SetCustomQuery(const wxString& queryText);
	const ibBackendQueryable* GetSourceQueryable() const { return m_propertySource->GetQueryable(); }

	// --- the arbitrary query, ON TOP of the main table -------------------------
	//
	// ⚠ THE MAIN TABLE IS ALWAYS THERE. It is not an alternative to the query, it is what the list
	// IS: the source of the COMMANDS (open a row, create, mark for deletion), of the icon and the
	// caption, and of the value a choice hands back. A list with no main table would be a grid of
	// numbers nobody could do anything with.
	//
	// The arbitrary query lives OVER it — an implicit join, in Max's words. What it changes is what
	// the list READS and therefore which columns it offers; what it cannot change is whose rows they
	// are. Both are serialised, always, and neither replaces the other.
	//
	// Ticking the flag GENERATES a starting query over the main table (SeedArbitraryQuery), because
	// an empty text is not a query and a person switching this on means "let me change what is read",
	// not "let me start from a blank page". Unticking clears it — the main table alone is the read.
	bool     IsArbitraryQuery() const { return m_propertyUseCustomQuery->GetValueAsBoolean(); }
	void     SetArbitraryQuery(bool use);
	wxString GetArbitraryQueryText() const { return m_propertyCustomQuery->GetValueAsString(); }
	void     SetArbitraryQueryText(const wxString& text) { m_propertyCustomQuery->SetValue(text); }

	// THE QUERY THE MAIN TABLE WOULD WRITE FOR ITSELF — `SELECT <fields> FROM <the main table>`. The
	// starting point when the flag goes on, and empty when there is no main table to write it over.
	wxString SeedArbitraryQuery() const;

	// WHY THE ARBITRARY QUERY PRODUCES NOTHING — the ENGINE's own words, empty when it resolved.
	// (What it DOES produce is asked for as the model's columns, the way a host asks any list what
	// it has; there was a second accessor handing out the raw schema, and nobody used it.)
	const wxString& GetArbitraryQueryError() const { return m_queryError; }
	// Re-apply the source (metaobject OR arbitrary query) onto the composer — the settings dialog's "Query" tab
	// calls this after editing the flag / text (SetValue does not fire OnPropertyChanged).
	void     ApplySource() { RebuildSource(); }
	// The source's DESCRIPTOR (holder) — parallel to GetSourceQueryable; the list reaches the source's
	// command interface (open a row / commands) through it. The list itself stays metadata-blind.
	const ibQueryableSourceDescriptor* GetSourceDescriptor() const { return m_propertySource->GetDescriptor(); }
	// (GetComposer() removed — it just forwarded to the inherited GetModelComposer(); use that directly.)
	// (GetListSettings() removed — the settings buffer lives on the BASE model now (m_listSettings); the
	//  duplicate m_settings is gone. Max: "doesn't this live on the base model now?".)
	// Commit Filter/Order/Group from the buffer ONTO the composer — call on a settings change, NOT per fetch.
	void RefreshComposerSettings();

	// Add a FILTER to the list — a predicate `path op value` fed to the composer underneath (the SINGLE
	// settings store; it persists and feeds the fetch). The backend injects a FIXED predicate here when it
	// GENERATES a select form (folder-select: IsFolder "=" true); the general / user filter path lands here too.
	void AddFilter(const wxString& path, const wxString& op, const ibValue& value);

	// Add a SORT — a sort line (path, ascending) onto the composer. The metaobject sets the DEFAULT sort here at
	// LIST CREATION (an ordinary serialised sort a user can remove), NOT a runtime method re-applied every fetch.
	void AddSort(const wxString& path, bool ascending = true);

	// The FOLDER-flag column of a hierarchical list — set at creation (ibCreateHierarchyList) so folder rows render
	// as drillable containers even when EMPTY (the DB level-fetch reports hasChildren=false for every row). The
	// metaobject picked the column (GetDataIsFolder); the fetch reads the row's value for it. Null on a flat list.
	void SetFolderColumn(const ibBackendQueryColumn* col) { m_folderColumn = col; }
	const ibBackendQueryColumn* GetFolderDisplayColumn() const override { return m_folderColumn; }

	// --- ibValueModel / ibValueModelCursor --------------------------------
	virtual void GetValueByRow(wxVariant& variant, const ibDataViewItem& item, unsigned col) const override;
	virtual bool SetValueByRow(const wxVariant& variant, const ibDataViewItem& item, unsigned col) override;
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const override;
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) override { return false; }
	

	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_columns; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override;
	virtual Features GetFeatures() const override;
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const override { return false; }

	// DynamicRead OFF → the base ibValueModelCursor serves the WHOLE list from its RAM snapshot instead of the live
	// keyset cursor (EnsureSnapshot / RunStoragePage). Read straight off the designer property (default = live).
	bool IsDynamicRead() const override { return m_propertyDynamicRead->GetValueAsBoolean(); }
	// Add/Copy/Edit/Delete: NOT overridden — the base no-ops. List mutation goes through the
	// choice/keyset path (separate design). AutoCreateColumn also keeps the base default (false).

	// Double-click a read-only row → open its value. The list is a BRIDGE: it holds the current row's key
	// (GetItemKey) but knows nothing about "how to open" — it delegates to the source's command interface
	// (vended by the descriptor, parallel to the queryable). A source without one (custom query) no-ops.
	virtual void ActivateItem(const ibDataViewItem& row, class ibBackendValueForm* srcForm) override;

	// Command STORE — the list carries NO commands of its own; it forwards to the SOURCE's command interface
	// (the metaobject's set, reached through the descriptor). The TableBox merges these into its bar and routes
	// a click back here → we forward it with the row's key. No command interface (custom query) → empty / no-op.
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	virtual void CallAsCommand(const ibActionID& lNumAction, const ibDataViewCommandContext& ctx, class ibBackendValueForm* srcForm) override;

	// Picker Choose — the list is a BRIDGE: it hands the row's value map (its default columns) to the source's
	// command interface, which reads the right SELECT value per family (reference / record key). Metadata-blind.
	virtual ibValue GetItemSelectValue(const ibDataViewItem& item) const override;

	// (No Get*Fetch override — the dynamic list inherits ibValueModel::Get*Fetch → RunComposerPage. Fetch
	//  lives ONLY in the parent; the source is GetSourceQueryable(), the composer + ListSettings do the rest.)

	// --- ibSourceDataObject (the list IS a form data source) ----------------
	// The source metaobject comes THROUGH the queryable (metadata-backed sources hold it; a custom-query source has
	// none → null). The front reads the source's icon / presentation off it, exactly as every other source object.
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override {
		const ibBackendQueryable* q = GetSourceQueryable();
		return q != nullptr ? q->GetSourceMetaObject() : nullptr;
	}
	virtual ibClassID GetSourceClassType() const override { return g_valueDynamicListCLSID; }
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }
	virtual bool IsEmpty() const override { return false; }
	virtual ibUniqueKey GetGuid() const override;

	// Key on the dynamic LIST comes from the METAOBJECT through the source descriptor (the node has no metaobject
	// id): the key = the family-correct identity (record → reference guid, register → composite key). GetGuid()
	// above is the LIST's own query-table identity — a different thing.
	ibUniqueKey GetItemKey(const ibDataViewItem& item) const override;

	// Selection-restore lookup (after a child-form save changes/creates a row): the identity VALUE →
	// a stub carrying the ROW-KEY, matched against the fetched batch by m_rowKey. The row-key is built by the
	// SOURCE DESCRIPTOR (GetRowKeyByValue) — a record keys by its reference, a register decomposes its COMPOSITE
	// key — so a register row restores (the base's flat {value} stub never matched a register's multi-column key).
	virtual ibDataViewItem FindRowValue(const ibValue& varValue, const wxString& colName = wxEmptyString) const override;

	virtual const ibSourceExplorer* GetSourceExplorer() const override;

	// The list is BOTH an ibSourceDataObject (the scalar walk holds that pointer) and, through ibValueModel,
	// an ibTabularDataObject. Two unrelated bases declare this signature, so they are two slots; the body is
	// the table's, and this bridges the source one to it. See valueTable.h for the full note.
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override {
		return ibTabularDataObject::GetValueBySourceHop(hop, out);
	}

	virtual wxString GetSourceCaption() const override;
	// READ — the metadata OF THE CHOSEN SOURCE, taken straight from the VALUE (the picked queryable's metaobject).
	// TERMINAL (no owner-walk), so a form's metadata may fall back to this list without recursing.
	virtual const ibMetaData* GetSourceMetaData() const override;

	// SELECTION — which config to PICK / resolve a source FROM: the owner form's config, via the attach owner
	// (m_owner->GetMetaData()). This is what GetSourceMetaData used to do; it lets the Source picker + the property
	// factory resolve a source BY NAME through THIS config's OWN factory (a copied / other-config list resolves its
	// columns). Split from GetSourceMetaData so READ (from the value) stays terminal — no attach-owner cycle.
	virtual const ibMetaData* GetMetaData() const override;

	// --- ibPropertyObject + serialization -----------------------------------
	// The list IS a property object: its Source / Settings properties surface onto the form
	// attribute (like ibValueSizerItem) — the attribute just casts the runtime value to
	// ibPropertyObject, knowing nothing about "a dynamic list". OnPropertyChanged is the real
	// hook (a virtual, not a backend function-pointer). Read/WriteProperty persist the Source
	// property PLUS the settings held on the base buffer GetListSettings() (outside the property set).
	// Name comes from the FACTORY: ibValue::GetClassName() resolves it by the object's own clsid
	// (GetClassType → the registered name); no hardcoded literal, it stays only in VALUE_TYPE_REGISTER.
	// ibPropertyObject declares GetClassName PURE on a base UNRELATED to ibValue — so it MUST be
	// overridden here (this both closes that pure slot and disambiguates the two same-name bases;
	// dropping it gives C2385 at the GetClassName() call in GetSourceCaption). Same as ibValueMetaObject.
	virtual wxString GetClassName() const override { return ibValue::GetClassName(); }
	virtual wxString GetObjectTypeName() const override { return GetClassName(); }
	virtual bool IsEditable() const override { return true; }
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;
	virtual bool ReadProperty(const ibDataNode& node) override;
	virtual bool WriteProperty(ibDataNode& node) const override;

	// --- script member surface (Filter/Order/Group/Settings + Refresh) ------
	void FillMembers(ibMemberTable& helper) const;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

private:
	
	// (RunPage REMOVED — the dynamic list fetches through the base ibValueModel::RunComposerPage; its grouping
	//  drill + self-hierarchy live there now, over GetSourceQueryable() + the one base composer.)

	// Rebuild the columns + composer from the current source (the Source property's
	// queryable). Called when the source is (re)set / picked / loaded.
	void RebuildSource();

	// DROP THE FILTER / SORT / GROUPING LINES whose field the list no longer has — run after every
	// rebuild, so removing the arbitrary query takes its columns' settings with it. By RESOLUTION,
	// never by chasing the change: the same rule that handles a table removed from the query, an
	// attribute renamed, a metaobject deleted.
	void PruneUnresolvedSettings();

	// The dynamic list uses the ONE base composer (GetModelComposer()) AND the ONE base settings buffer
	// (GetListSettings() → m_listSettings) — no subclass holds its own. (The duplicate m_settings was removed.)
	ibValuePtr<ibValueModelColumnCollection> m_columns;   // queryable-derived columns

	// WHAT THE ARBITRARY QUERY PRODUCES, and what went wrong if it produces nothing. Rebuilt on every
	// RebuildSource — which is what makes the pickers follow the text: change the query, and the
	// filters / sorts / groupings are offering the new fields the moment the change is applied.
	//
	// The schema OWNS its synthetic columns (OutputColumn::m_ownedCol), so it outlives the door that
	// resolved it — which it has to: nothing here reads rows, the columns are just being named.
	std::vector<ibQueryLowering::OutputColumn> m_querySchema;
	wxString                                   m_queryError;   // the ENGINE's words, verbatim; empty = it resolved

	// Folder-flag column for the hierarchical display (folder rows = drillable containers). Non-owning — points at
	// the metaobject's queryable column, handed in by ibCreateHierarchyList. Null on a flat / non-folder list.
	const ibBackendQueryColumn* m_folderColumn = nullptr;

	// The list's SOURCE config, STORED (the metadata VARIABLE GetSourceMetaData returns) — captured from the picked
	// queryable's metaobject in RebuildSource (fires on ctor / load / source-change / copy). Held so the READ never
	// re-resolves the queryable through m_owner->GetMetaData (which walks the form → recurses). Null until a source
	// is picked (GetSourceMetaData then yields the ACTIVE config). Non-owning — points at an open config's ibMetaData.
	const ibMetaData* m_sourceMetaData = nullptr;

	// Default view (Normal / Choice) — serialised as a hidden field by Read/WriteProperty; GetSourceExplorer stamps
	// the explorer's choice flag from it (see ibDynamicListView). Seeded by the ctor, overridden by the loaded value.
	ibDynamicListView m_view = ibDynamicListView_Normal;

	// --- property surface — the list's properties SURFACE onto the form attribute (like
	// ibValueSizerItem). CreateProperty right in the declaration (this / protected base).
	// "Source" = the registered queryables; "Settings" = an "Open" action button. ---
	ibPropertyCategory*        m_categoryList   = ibPropertyObject::CreatePropertyCategory(wxT("DynamicList"), _("Dynamic list"));
	// Source = a dynamic-source property holding the chosen queryable variable; the list
	// reads it via GetSourceQueryable() facade (the variable does NOT live on the list).
	ibPropertyDynamicSource*   m_propertySource = ibPropertyObject::CreateProperty<ibPropertyDynamicSource>(m_categoryList, wxT("Source"), _("Source"));
	// "Settings..." — an action property; m_owner = THIS dynamic list, and the frontend
	// ibPGDynamicListProperty casts owner → dynamic list and opens the settings form.
	ibPropertyDynamicList* m_propertySettings = ibPropertyObject::CreateProperty<ibPropertyDynamicList>(m_categoryList, wxT("Settings"), _("Settings"));
	// Arbitrary-query mode: the flag switches the source from a picked metaobject to a QUERY TEXT (both serialised
	// by ReadProperty/WriteProperty). The text is edited on the settings dialog's first "Query" tab.
	ibPropertyBoolean* m_propertyUseCustomQuery = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryList, wxT("UseCustomQuery"), _("Arbitrary query"), false);
	ibPropertyString*  m_propertyCustomQuery    = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryList, wxT("CustomQuery"), _("Query text"), wxEmptyString);
	// DynamicRead — the safety toggle ("Dynamic data read"). TRUE (default): a live keyset cursor paged from the DB
	// batch by batch. FALSE: the whole result set is materialised into a RAM snapshot ONCE and paged in memory (the base
	// ibValueModelCursor::EnsureSnapshot / RunStoragePage) — the fallback for when cursor paging misbehaves, or a
	// small / stable list where liveness does not matter. Read by IsDynamicRead() above; serialised by Read/WriteProperty.
	ibPropertyBoolean* m_propertyDynamicRead = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryList, wxT("DynamicRead"), _("Dynamic data read"), true);
};

// Create a folder-select SOURCE: a dynamic list in CHOICE mode with a FIXED `IsFolder = true` predicate — the
// backend injects it when it GENERATES a folder-select form (the select itself is front-driven). Shared by every
// hierarchical metatype (catalog / charts). A null / folder-less queryable → a plain choice list (no predicate).
// Create a list with a DEFAULT sort set at creation — the metaobject passes its presentation column (a document its
// Number, an enum its Order, a register its Period). The sort becomes an ordinary serialised, user-editable setting
// (removable), NOT a runtime re-applied default. null col → no sort. (Flat sources.) `view` seeds the default view —
// a SELECT form passes ibDynamicListView_Choice so the generated form inherits choice mode (serialised, removable).
BACKEND_API ibValueDynamicList* ibCreateList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* defaultSort, ibDynamicListView view = ibDynamicListView_Normal);

// Create a HIERARCHY list — folder-FIRST sort (folderCol DESC: folders on top) THEN presentation sort. The
// metaobject passes its IsFolder + presentation (Description) columns. Folders are a SORT setting now, not a
// structural GetFolderColumn; the tree itself comes from the queryable's hierarchy (parent) column. `view` seeds the
// default view — a SELECT form passes ibDynamicListView_Choice (choice mode, serialised and user-removable).
BACKEND_API ibValueDynamicList* ibCreateHierarchyList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* folderCol, const ibBackendQueryColumn* presentationCol, ibDynamicListView view = ibDynamicListView_Normal);

// Create a FOLDER-SELECT list — presentation sort + a fixed `IsFolder = true` FILTER (only folders). The metaobject
// passes its IsFolder + presentation columns (folder-select is a filter setting, no GetFolderColumn). `view` seeds the
// default view — the folder-select call site passes ibDynamicListView_Choice (choice mode, serialised, user-removable).
BACKEND_API ibValueDynamicList* ibCreateFolderList(const ibBackendQueryable* queryable, const ibBackendQueryColumn* folderCol, const ibBackendQueryColumn* presentationCol, ibDynamicListView view = ibDynamicListView_Normal);

#endif // __VALUE_DYNAMIC_LIST_H__
