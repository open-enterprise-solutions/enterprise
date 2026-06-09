#ifndef __META_TABLE_H__
#define __META_TABLE_H__

#include "backend/metaCollection/metaObjectComposite.h"
#include "backend/query/queryable.h"

// ibTabularQueryable — the L3 queryable for a tabular section (uuid-keyed, line-number
// ordered). The tabular metaobject VENDS this adapter via the common GetQueryable()
// interface — same as catalog / document / register / constant. The adapter is
// parent-agnostic (the parent uuid is a query filter, supplied via WhereKey at read
// time), so a transient (data-processor / report) parent simply never queries it.
class ibValueMetaObjectTableData;
class BACKEND_API ibTabularQueryable : public ibBackendQueryable {
public:
	explicit ibTabularQueryable(const ibValueMetaObjectTableData* meta) : m_meta(meta) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;   // attribute-by-name AS a column
	virtual wxString GetQueryTableName() const override;
	virtual ibMetaID GetQueryMetaID() const override;
	virtual const ibMetaData* GetMetaData() const override;                  // metadata context for column-based value reads
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override;   // { line number } — parent uuid is a plain filter
private:
	const ibValueMetaObjectTableData* m_meta;
};

class BACKEND_API ibValueMetaObjectTableData : public ibValueMetaObjectCompositeData, public ibBackendQueryableHolder {
	public:

public:

	// the metaobject VENDS its queryable (a stable member) — the common interface.
	virtual const ibBackendQueryable* GetQueryable() const override { return &m_queryable; }


	ibItemMode GetTableUse() const { return m_propertyUse->GetValueAsEnum(); }

	ibValueMetaObjectAttributePredefined* GetNumberLine() const { return m_propertyNumberLine->GetMetaObject(); }
	bool IsNumberLine(const ibMetaID& id) const { return id == (*m_propertyNumberLine)->GetMetaID(); }

	//get table class
	ibTypeDescription GetTypeDesc() const;

	virtual bool FilterChild(const ibClassID& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return true;
		return false;
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
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	//after and before for designer 
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

	//special functions for DB 
	virtual wxString GetTableNameDB() const {
		ibValueMetaObject* parentMeta = GetParent();
		wxASSERT(parentMeta);
		return wxString::Format(wxT("%s%i_VT%i"),
			parentMeta->GetClassName(),
			parentMeta->GetMetaID(),
			GetMetaID()
		);
	}

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

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	ibPropertyCategory* m_categoryGroup = ibPropertyObject::CreatePropertyCategory(wxT("Group"), _("Group"));
	ibPropertyEnum<ibValueEnumItemMode>* m_propertyUse = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumItemMode>>(m_categoryGroup, wxT("ItemMode"), _("Item mode"), ibItemMode::ibItemMode_Item);
	ibPropertyContainer<>* m_propertyNumberLine = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryGroup, ibValueMetaObjectCompositeData::CreateNumber(wxT("NumberLine"), _("N"), wxEmptyString, 6, 0));

	// the vended queryable — stable for this tabular section's life (see GetQueryable()).
	ibTabularQueryable m_queryable{ this };
};

#endif