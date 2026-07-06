#ifndef __DYNAMIC_LIST_H__
#define __DYNAMIC_LIST_H__

#include "backend/tableInfo.h"                       // ibValueModelCursor
#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject
#include "backend/composition/dataComposer.h"        // L5 — ibDataDBComposer
#include "backend/composition/listFilter.h"          // ibValueListSettings
#include "backend/propertyManager/propertyObject.h"  // ibPropertyObject — the dynamic list IS a property object
#include "backend/propertyManager/property/propertyDynamicSource.h"  // ibPropertyDynamicSource — the "Source" property
#include "backend/propertyManager/property/propertyDynamicList.h"  // ibPropertyDynamicList — the "Settings" → "Open" action

class ibBackendQueryable;

constexpr ibClassID g_valueDynamicListCLSID = value_to_clsid("VL_DLST");

// View kind of the dynamic list — a regular list, or a selection (choice) list
// where activating a row picks the value into the owner.
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
	// View: a normal list or a choice (selection) list.
	ibValueDynamicList(const ibBackendQueryable* queryable = nullptr,
		ibDynamicListView view = ibDynamicListView_Normal);
	virtual ~ibValueDynamicList();

	// --- kind ---------------------------------------------------------------
	ibDynamicListView GetView() const { return m_view; }
	bool IsChoice() const { return m_view == ibDynamicListView_Choice; }

	// --- source & query (L5) ------------------------------------------------
	// The list starts EMPTY; SetSource/SetSourceQueryable install the queryable,
	// SetCustomQuery an arbitrary query. The source variable itself lives in the
	// Source property (read via the GetSourceQueryable facade), not on the list.
	void SetSource(const wxString& ns, const wxString& name);
	void SetSourceQueryable(const ibBackendQueryable* queryable);
	void SetCustomQuery(const wxString& queryText);
	const ibBackendQueryable* GetSourceQueryable() const { return m_propertySource->GetVariable(); }
	// (GetComposer() removed — it just forwarded to the inherited GetModelComposer(); use that directly.)
	// (GetListSettings() removed — the settings buffer lives on the BASE model now (m_listSettings); the
	//  duplicate m_settings is gone. Max: "doesn't this live on the base model now?".)
	// Commit Filter/Order/Group from the buffer ONTO the composer — call on a settings change, NOT per fetch.
	void RefreshComposerSettings();

	// --- ibValueModel / ibValueModelCursor --------------------------------
	virtual void GetValueByRow(wxVariant& variant, const ibDataViewItem& item, unsigned col) const override;
	virtual bool SetValueByRow(const wxVariant& variant, const ibDataViewItem& item, unsigned col) override;
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const override;
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) override { return false; }
	
	
	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_columns; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override;
	virtual Features GetFeatures() const override;
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const override { return false; }
	// Add/Copy/Edit/Delete: NOT overridden — the base no-ops. List mutation goes through the
	// choice/keyset path (separate design). AutoCreateColumn also keeps the base default (false).

	// (No Get*Fetch override — the dynamic list inherits ibValueModel::Get*Fetch → RunComposerPage. Fetch
	//  lives ONLY in the parent; the source is GetSourceQueryable(), the composer + ListSettings do the rest.)

	// --- ibSourceDataObject (the list IS a form data source) ----------------
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return nullptr; }
	virtual ibClassID GetSourceClassType() const override { return g_valueDynamicListCLSID; }
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }
	virtual bool IsEmpty() const override { return false; }
	virtual ibUniqueKey GetGuid() const override;

	// Key CREATION on the dynamic LIST (the cursor base makes none): a row's key = its primary-key REFERENCE
	// (guid), the handle the command layer opens the underlying object with. GetGuid() above is the LIST's own
	// query-table identity — a different thing.
	ibUniqueKey GetItemKey(const ibDataViewItem& item) const override;

	virtual const ibSourceExplorer* GetSourceExplorer() const override;
	virtual wxString GetSourceCaption() const override;
	virtual const ibMetaData* GetSourceMetaData() const override;

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

	// The dynamic list uses the ONE base composer (GetModelComposer()) AND the ONE base settings buffer
	// (GetListSettings() → m_listSettings) — no subclass holds its own. (The duplicate m_settings was removed.)
	ibValuePtr<ibValueModelColumnCollection> m_columns;   // queryable-derived columns
	ibDynamicListView                 m_view;        // normal / choice

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
};

#endif // __DYNAMIC_LIST_H__
