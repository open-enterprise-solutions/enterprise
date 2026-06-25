#ifndef __DYNAMIC_LIST_H__
#define __DYNAMIC_LIST_H__

#include "backend/tableInfo.h"                       // ibValueModelTreeBase
#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject
#include "backend/composition/dataComposer.h"        // L5 — ibDataComposer
#include "backend/composition/listFilter.h"          // ibValueListSettings
#include "backend/propertyManager/propertyObject.h"  // ibPropertyObject — the dynamic list IS a property object
#include "backend/propertyManager/property/propertyDynamicSource.h"  // ibPropertyDynamicSource — the "Source" property
#include "backend/propertyManager/property/propertyDynamicList.h"  // ibPropertyDynamicList — the "Settings" → "Open" action

class ibBackendQueryable;

const ibClassID g_valueDynamicListCLSID = string_to_clsid("VL_DLST");

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
//   * Base = ibValueModelTreeBase (a tree understands a flat list — one root).
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
class BACKEND_API ibValueDynamicList : public ibValueModelTreeBase, public ibSourceDataObject, public ibPropertyObject {
public:

	// Row node — identity is the primary-key column VALUES (keyset). No guid/ref.
	struct ibDynamicListNode : public ibValueTreeNode {
		ibDynamicListNode(ibValueModelTreeBase* tree, const std::vector<ibValue>& key, bool container = false)
			: ibValueTreeNode(tree), m_key(key), m_container(container) {}
		const std::vector<ibValue>& GetKey() const { return m_key; }
		virtual bool IsContainer() const override { return m_container; }
		virtual bool IsEqualTo(const ibDataViewObject& other) const override {
			const auto* o = dynamic_cast<const ibDynamicListNode*>(&other);
			return o != nullptr && m_key == o->m_key;
		}
	private:
		std::vector<ibValue> m_key;
		bool                 m_container;
	};

	// The source may be passed here too — null means "set it later" (SetSource).
	// View: a normal list or a choice (selection) list.
	ibValueDynamicList(const ibBackendQueryable* queryable = nullptr,
		ibDynamicListView view = ibDynamicListView_Normal);
	virtual ~ibValueDynamicList();

	ibDynamicListView GetView() const { return m_view; }
	bool IsChoice() const { return m_view == ibDynamicListView_Choice; }

	// --- source (the list starts empty) -------------------------------------
	void SetSource(const wxString& ns, const wxString& name);
	void SetSourceQueryable(const ibBackendQueryable* queryable);
	void SetCustomQuery(const wxString& queryText);
	// Facade — the source variable lives in the Source property, not on the list.
	const ibBackendQueryable* GetSourceQueryable() const { return m_propertySource->GetVariable(); }

	// L5 — the list's query layer (reads the queryable, pages the data).
	ibDataComposer& GetComposer() const { return m_composer; }
	ibValueListSettings* GetListSettings() const { return m_settings; }

	// --- ibPropertyObject — the dynamic list IS a property object; its properties (source /
	// settings) surface onto the form attribute. OnPropertyChanged catches an edit and stores it
	// here — the real "hook", instead of a backend function-pointer. ----------
	virtual wxString GetClassName() const override;
	virtual wxString GetObjectTypeName() const override { return wxT("DynamicList"); }
	virtual bool IsEditable() const override { return true; }
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;
	
	// Central save/load entry override: the Source property PLUS the settings
	// (Filter/Order/Group) held on m_settings, outside the property set. The form
	// attribute serializes the dynamic list through this — it is the attached object.
	virtual bool ReadProperty(const ibDataNode& node) override;
	virtual bool WriteProperty(ibDataNode& node) const override;

	// Re-apply the settings onto the composer (call on settings change, NOT per
	// fetch). Replaces — does not clear-then-rebuild every read.
	void RefreshComposerSettings();

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const { return false; }

	//events:
	virtual void AddValue(unsigned int before = 0) override;
	virtual void CopyValue() override;
	virtual void EditValue() override;
	virtual void DeleteValue() override;

	// --- ibValueModelTreeBase ----------------------------------------------
	virtual void GetValueByRow(wxVariant& variant, const ibDataViewItem& item, unsigned col) const override;
	virtual bool SetValueByRow(const wxVariant& variant, const ibDataViewItem& item, unsigned col) override;
	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_columns; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override;
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const override;
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) override { return false; }
	virtual Features GetFeatures() const override;

	// Thin fetch — all the work is in the L5 provider (RunPage).
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent, const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const override;

	// --- ibSourceDataObject -------------------------------------------------
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override;
	virtual ibClassID GetSourceClassType() const override { return g_valueDynamicListCLSID; }
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }
	virtual bool IsEmpty() const override { return false; }
	virtual ibUniqueKey GetGuid() const override;
	virtual ibSourceExplorer GetSourceExplorer() const override;
	virtual wxString GetSourceCaption() const override;
	// The walk-tree's columns come from the QUERYABLE — a CLOSED set (a column not in it is a
	// broken binding, not a config-wide re-find).
	virtual const ibBackendSourceColumn* GetSourceColumn(const ibMetaID& id) const override;
	virtual bool HasOwnColumns() const override { return true; }

	virtual const ibMetaData* GetSourceMetaData() const override;

	// settings surface (Filter/Order/Group/Settings + Refresh/EditSettings)
	void FillMembers(ibMemberTable& helper) const;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

private:
	
	// THIN — build the provider, run the composer onto it, adopt its rows.
	unsigned int RunPage(const ibDataViewItem& parent, const ibDataViewItem& anchor,
		int count, ibFetchDirection dir, ibDataViewItemArray& out) const;

	// Rebuild the columns + composer from the current source (the Source property's
	// queryable). Called when the source is (re)set / picked / loaded.
	void RebuildSource();

	mutable ibDataComposer            m_composer;    // L5 — the visible query layer
	ibValuePtr<ibValueListSettings>   m_settings;    // Filter / Order / Group (applied onto the composer)
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
