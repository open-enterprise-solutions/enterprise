#ifndef __META_TABLE_H__
#define __META_TABLE_H__

#include "backend/metaCollection/metaObjectComposite.h"
#include "backend/query/queryable.h"
#include "backend/query/queryableFactory.h"   // ibQueryableSourceDescriptor (the tabular L4 source descriptor)

class ibValueMetaObjectRecordData;
class ibValueMetaObjectTableData;       // RAM-only tabular section (processors / reports)
class ibValueMetaObjectTableDataRef;    // DB-backed tabular section (reference owners: catalog / document / charts)

// ibTabularQueryable — the L3 queryable for a DB-backed tabular section (uuid-keyed, line-number
// ordered). Only the Ref variant vends it — the RAM base has no physical table — so it navigates
// over ibValueMetaObjectTableDataRef. The adapter is parent-agnostic (the parent uuid is a query
// filter, supplied via WhereKey at read time), so a transient parent simply never queries it.
class BACKEND_API ibTabularQueryable : public ibBackendQueryable {
public:
	explicit ibTabularQueryable(const ibValueMetaObjectTableDataRef* meta) : m_meta(meta) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;   // attribute-by-name AS a column
	virtual wxString GetQueryTableName() const override;
	virtual wxString GetQueryName() const override;   // the section's user-facing name (change ledger)
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;                  // metadata context for column-based value reads
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override;   // { line number } — parent uuid is a plain filter
private:
	const ibValueMetaObjectTableDataRef* m_meta;
};

// ibTabularSourceDescriptor — the DB-backed tabular section's L4 source descriptor. Like the
// standard ibMetaSourceDescriptor it CONTAINS the queryable and replaces the plain m_queryable
// field; but a tabular section is a SUB-object, so its (namespace, name) is PARENT-QUALIFIED —
// ns = the parent record/document's kind, name = "<Parent>.<Section>" — reached as the
// 3-segment source `Document.Expense.Goods`. Methods are out-of-line (the parent type is
// incomplete here).
class BACKEND_API ibTabularSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibTabularSourceDescriptor(ibValueMetaObjectTableDataRef* meta);
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	const ibBackendQueryable* GetQueryable() const { return &m_queryable; }   // the metaobject's GetQueryable() forwards here
private:
	ibValueMetaObjectTableDataRef* m_meta;
	ibTabularQueryable             m_queryable;
};

// ibValueMetaObjectTableData — the SHARED BASE for tabular sections (the generic "a tabular
// section" type used by collectors / data objects / queryable). Carries attributes + line number
// + use mode + the RAM-default behaviour (no queryable, plain value-ctor). It is NOT registered
// with the factory itself — the two thin leaves are: ibValueMetaObjectTableDataRam (RAM, MD_TBL)
// and ibValueMetaObjectTableDataRef (DB-backed, MD_TBLR). Naming is provisional (may be revised).
class BACKEND_API ibValueMetaObjectTableData : public ibValueMetaObjectCompositeData, public ibBackendQueryableHolder {
		public:

public:

	// base default vends NO queryable (no physical table); the DB leaf overrides this.
	virtual const ibBackendQueryable* GetQueryable() const override { return nullptr; }


	ibItemMode GetTableUse() const { return m_propertyUse->GetValueAsEnum(); }

	ibValueMetaObjectAttributePredefined* GetNumberLine() const { return m_propertyNumberLine->GetMetaObject(); }
	bool IsNumberLine(const ibMetaID& id) const { return id == (*m_propertyNumberLine)->GetMetaID(); }

	//get table class
	ibTypeDescription GetTypeDesc() const;

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return clsid;
		return 0;
	}

	//ctor
	ibValueMetaObjectTableData();
	virtual ~ibValueMetaObjectTableData();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//for designer
	virtual bool OnReloadMetaObject();

	//module manager is started or exit
	//after and before for designer
	//ABSTRACT: a tabular section MUST be either RAM (Ram) or DB-backed (Ref). Each leaf overrides
	//this to register its own value-ctor, then chains to this base body (number line + framework).
	//Pure-virtual-with-body: the base supplies the common body but cannot be instantiated itself.
	virtual bool OnBeforeRunMetaObject(int flags) = 0;
	virtual bool OnAfterRunMetaObject(int flags);

	//after and before for designer
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

#pragma region __generic_h__

	//attribute
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByPredefinedAttribute(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

#pragma endregion
#pragma region __array_h__

	//any
	std::vector<ibValueMetaObjectAttributeBase*> GetAnyAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByPredefinedAttribute(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

	//attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//any
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAnyAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID });
	}

	//attribute
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID });
	}

#pragma endregion

	/**
	* Property events
	*/
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, ibProperty* property);

protected:

	//get default attributes
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyNumberLine->GetMetaObject() };
		return true;
	}


	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) override;

private:

	ibPropertyCategory* m_categoryGroup = ibPropertyObject::CreatePropertyCategory(wxT("Group"), _("Group"));
	ibPropertyEnum<ibValueEnumItemMode>* m_propertyUse = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumItemMode>>(m_categoryGroup, wxT("ItemMode"), _("Item mode"), ibItemMode::ibItemMode_Item);
	ibPropertyContainer<>* m_propertyNumberLine = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryGroup, ibValueMetaObjectCompositeData::CreateNumber(wxT("NumberLine"), _("N"), wxEmptyString, 6, 0));
};

// ibValueMetaObjectTableDataRam — RAM-only tabular section (data processors / reports). A thin
// leaf over the shared base: it adds nothing but its factory identity (MD_TBL) — no queryable,
// no physical table, the plain tabular value-ctor (inherited base behaviour).
class BACKEND_API ibValueMetaObjectTableDataRam : public ibValueMetaObjectTableData {
	public:
	ibValueMetaObjectTableDataRam();
	virtual ~ibValueMetaObjectTableDataRam();

	//registers the plain tabular value-ctor in its own run event.
	virtual bool OnBeforeRunMetaObject(int flags) override;
};

// ibValueMetaObjectTableDataRef — DB-backed tabular section. Adds the L4 source descriptor (the
// vended queryable + physical table) and registers itself as an L4 source on run / close. The
// reference owners (catalog / document / charts) hold these; their rows persist to the
// "<Parent><id>_VT<id>" table and are read/written through the L3 door.
class BACKEND_API ibValueMetaObjectTableDataRef : public ibValueMetaObjectTableData {
	public:

	ibValueMetaObjectTableDataRef();
	virtual ~ibValueMetaObjectTableDataRef();

	// the metaobject VENDS its queryable (via the L4 source descriptor it owns) — the common interface.
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }

	//physical DB table — only the DB variant has one.
	virtual wxString GetPhysicalTableName() const {
		ibValueMetaObject* parentMeta = GetParent();
		wxASSERT(parentMeta);
		return wxString::Format(wxT("%s%i_VT%i"),
			parentMeta->GetClassName(),
			parentMeta->GetMetaID(),
			GetMetaID()
		);
	}

	//events — register the reference value-ctor in its own run event; (un)register the L4 source.
	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;

private:

	// the L4 source descriptor — CONTAINS the vended queryable (stable for this tabular section's
	// life) and is registered with the factory on run / close; GetQueryable() forwards to it.
	ibTabularSourceDescriptor m_queryable{ this };
};

#endif
