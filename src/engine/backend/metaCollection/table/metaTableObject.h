#ifndef _TABLES_H__
#define _TABLES_H__

#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

class BACKEND_API IMetaTableData {
public:
	//override base objects 
	virtual std::vector<IMetaObjectAttribute*> GetGenericAttributes() const = 0;
};

class BACKEND_API CMetaObjectTable : public IMetaObject,
	public IMetaTableData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectTable);
private:
	OptionList* GetUseItem(PropertyOption*) {
		OptionList* opt_list = new OptionList;
		opt_list->AddOption(_("for item"), eItemMode_Item);
		opt_list->AddOption(_("for folder"), eItemMode_Folder);
		opt_list->AddOption(_("for folder and item"), eItemMode_Folder_Item);
		return opt_list;
	}
	CMetaObjectAttributeDefault* m_numberLine;
protected:
	PropertyCategory* m_categoryGroup = IPropertyObject::CreatePropertyCategory({ "group", _("group") });
	Property* m_propertyUse = IPropertyObject::CreateProperty(m_categoryGroup, { "use",  _("use") }, &CMetaObjectTable::GetUseItem, eItemMode::eItemMode_Item);
public:

	eItemMode GetTableUse() const {
		return (eItemMode)m_propertyUse->GetValueAsInteger();
	}

	CMetaObjectAttributeDefault* GetNumberLine() const {
		return m_numberLine;
	}

	bool IsNumberLine(const meta_identifier_t& id) const {
		return id == m_numberLine->GetMetaID();
	}

	//get table class
	typeDescription_t GetTypeDescription() const;

	virtual bool FilterChild(const class_identifier_t& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return true; 
		return false;
	}

	//ctor 
	CMetaObjectTable();
	virtual ~CMetaObjectTable();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnLoadMetaObject(IMetaData* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	//after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	//after and before for designer 
	virtual bool OnAfterCloseMetaObject();

	//override base objects 
	virtual std::vector<IMetaObjectAttribute*> GetGenericAttributes() const {
		return GetObjectAttributes();
	}

	//get attributes, form etc.. 
	virtual std::vector<IMetaObjectAttribute*> GetObjectAttributes() const;

	//find attributes, tables etc 
	virtual IMetaObjectAttribute* FindAttributeByGuid(const wxString& strDocPath) const {
		for (auto& obj : GetObjectAttributes()) {
			if (strDocPath == obj->GetDocPath())
				return obj;
		}
		return nullptr;
	}

	//find in current metaObject
	virtual IMetaObjectAttribute* FindProp(const meta_identifier_t& id) const;

	//special functions for DB 
	virtual wxString GetTableNameDB() const {
		IMetaObject* parentMeta = GetParent();
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
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, Property* property);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif