#ifndef _ENUMERATION_H__
#define _ENUMERATION_H__

#include "baseObject.h"

class CMetaEnumerationObject;

class CMetaObjectEnumeration : public IMetaObjectRecordDataRef {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectEnumeration);

	enum
	{
		ID_METATREE_OPEN_MANAGER = 19000,
	};

	CMetaCommonModuleObject* m_moduleManager;

	enum
	{
		eFormList = 1,
		eFormSelect
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;

		optionlist->AddOption("formList", eFormList);
		optionlist->AddOption("formSelect", eFormSelect);

		return optionlist;
	}

private:

	//default form 
	int m_defaultFormList;
	int m_defaultFormSelect;

	//default attributes 
	CMetaDefaultAttributeObject* m_attributeOrder;

private:

	OptionList* GetFormList(Property*);
	OptionList* GetFormSelect(Property*);

public:

	CMetaObjectEnumeration();
	virtual ~CMetaObjectEnumeration();

	virtual wxString GetClassName() const {
		return wxT("enumeration"); 
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

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject* metaForm);
	virtual void OnRemoveMetaForm(IMetaFormObject* metaForm);

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetDefaultAttributes() const override;

	//searched attributes 
	virtual std::vector<IMetaAttributeObject*> GetSearchedAttributes() const override;

	//create associate value 
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t &id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create empty object
	virtual IRecordDataObjectRef* CreateObjectRefValue() { return NULL; }
	virtual IRecordDataObjectRef* CreateObjectRefValue(const Guid& guid) { return NULL; }

	//create form with data 
	virtual CValueForm* CreateObjectValue(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());
	virtual CValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const { return NULL; }
	virtual CMetaCommonModuleObject* GetModuleManager() const { return m_moduleManager; }

	//descriptions...
	wxString GetDescription(const IObjectValueInfo* objValue) const;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif