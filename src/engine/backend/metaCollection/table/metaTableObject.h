#ifndef __META_TABLE_H__
#define __META_TABLE_H__

#include "backend/metaCollection/metaObjectComposite.h"

class BACKEND_API ibValueMetaObjectTableData : public ibValueMetaObjectCompositeData {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectTableData);

public:

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

	using ibValueMetaObjectCompositeData::GetGenericAttributeArrayObject;
	//attribute
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByPredefined(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

#pragma endregion 
#pragma region __array_h__

	//any
	std::vector<ibValueMetaObjectAttributeBase*> GetAnyAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByPredefined(array);
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
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyNumberLine->GetMetaObject() };
		return true;
	}

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	ibPropertyCategory* m_categoryGroup = ibPropertyObject::CreatePropertyCategory(wxT("Group"), _("Group"));
	ibPropertyEnum<ibValueEnumItemMode>* m_propertyUse = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumItemMode>>(m_categoryGroup, wxT("ItemMode"), _("Item mode"), ibItemMode::ibItemMode_Item);
	ibPropertyInnerAttribute<>* m_propertyNumberLine = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryGroup, ibValueMetaObjectCompositeData::CreateNumber(wxT("NumberLine"), _("N"), wxEmptyString, 6, 0));
};

#endif