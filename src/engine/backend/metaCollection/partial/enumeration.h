#ifndef _ENUMERATION_H__
#define _ENUMERATION_H__

#include "object.h"

class CMetaObjectEnumeration : public IMetaObjectRecordDataEnumRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectEnumeration);

	enum
	{
		ID_METATREE_OPEN_MANAGER = 19000,
	};

	CMetaObjectCommonModule* m_moduleManager;

	enum
	{
		eFormList = 1,
		eFormSelect
	};

	virtual OptionList* GetFormType() override {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(wxT("formList"), _("Form list"), eFormList);
		optionlist->AddOption(wxT("formSelect"), _("Form select"), eFormSelect);
		return optionlist;
	}

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms" });
	Property* m_propertyDefFormList = IPropertyObject::CreateProperty(m_categoryForm, { "default_list", "default list" }, &CMetaObjectEnumeration::GetFormList, wxNOT_FOUND);
	Property* m_propertyDefFormSelect = IPropertyObject::CreateProperty(m_categoryForm, { "default_select" , "default select" }, &CMetaObjectEnumeration::GetFormSelect, wxNOT_FOUND);

private:

	OptionList* GetFormList(PropertyOption*);
	OptionList* GetFormSelect(PropertyOption*);

public:

	virtual bool FilterChild(const class_identifier_t& clsid) const {
		if (clsid == g_metaEnumCLSID)
			return true;
		return IMetaObjectGenericData::FilterChild(clsid);
	}

	CMetaObjectEnumeration();
	virtual ~CMetaObjectEnumeration();

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
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//form events 
	virtual void OnCreateFormObject(IMetaObjectForm* metaForm);
	virtual void OnRemoveMetaForm(IMetaObjectForm* metaForm);

	//override base objects 
	virtual std::vector<IMetaObjectAttribute*> GetDefaultAttributes() const override;

	//searched attributes 
	virtual std::vector<IMetaObjectAttribute*> GetSearchedAttributes() const override;

	//create associate value 
	virtual CMetaObjectForm* GetDefaultFormByID(const form_identifier_t& id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaObjectForm* metaObject);

	//create form with data 
	virtual IBackendValueForm* CreateObjectForm(IMetaObjectForm* metaForm) override {
		return metaForm->GenerateFormAndRun(
			nullptr, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual IBackendValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid) { return nullptr; }
	virtual IBackendValueForm* GetListForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);
	virtual IBackendValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullGuid);

	//get module object in compose object 
	virtual CMetaObjectModule* GetModuleObject() const {
		return nullptr;
	}

	virtual CMetaObjectCommonModule* GetModuleManager() const {
		return m_moduleManager;
	}

	//descriptions...
	wxString GetDataPresentation(const IObjectValueInfo* objValue) const;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

protected:

	//load & save metaData from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif