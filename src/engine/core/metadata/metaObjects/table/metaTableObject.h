#ifndef _TABLES_H__
#define _TABLES_H__

#include "core/metadata/metaObjects/metaObject.h"
#include "core/metadata/metaObjects/attribute/metaAttributeObject.h"

class IMetaTableData {
public:
	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const = 0;
};

class CMetaTableObject : public IMetaObject,
	public IMetaTableData {
	wxDECLARE_DYNAMIC_CLASS(CMetaTableObject);
private:
	OptionList* GetUseItem(PropertyOption*) {
		OptionList* opt_list = new OptionList;
		opt_list->AddOption(_("for item"), eItemMode_Item);
		opt_list->AddOption(_("for folder"), eItemMode_Folder);
		opt_list->AddOption(_("for folder and item"), eItemMode_Folder_Item);
		return opt_list;
	}
	CMetaDefaultAttributeObject* m_numberLine;
protected:
	PropertyCategory* m_categoryGroup = IPropertyObject::CreatePropertyCategory({ "group", _("group") });
	Property* m_propertyUse = IPropertyObject::CreateProperty(m_categoryGroup, { "use",  _("use") }, &CMetaTableObject::GetUseItem, eItemMode::eItemMode_Item);
public:

	eItemMode GetTableUse() const {
		return (eItemMode)m_propertyUse->GetValueAsInteger();
	}

	CMetaDefaultAttributeObject* GetNumberLine() const {
		return m_numberLine;
	}

	bool IsNumberLine(const meta_identifier_t& id) const {
		return id == m_numberLine->GetMetaID();
	}

	//get table class
	typeDescription_t GetTypeDescription() const;

	virtual bool FilterChild(const CLASS_ID& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return true; 
		return false;
	}

	//ctor 
	CMetaTableObject();
	virtual ~CMetaTableObject();

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("tabularSection");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const {
		return GetObjectAttributes();
	}

	//get attributes, form etc.. 
	virtual std::vector<IMetaAttributeObject*> GetObjectAttributes() const;

	//find attributes, tables etc 
	virtual IMetaAttributeObject* FindAttributeByGuid(const wxString& docPath) const {
		for (auto obj : GetObjectAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//find in current metaObject
	virtual IMetaAttributeObject* FindProp(const meta_identifier_t& id) const;

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

	virtual bool HasAttributes() { return true; }
	virtual wxArrayString GetAttributes();


	/**
	* Property events
	*/
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, Property* property);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif