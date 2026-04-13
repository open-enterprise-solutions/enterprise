#ifndef __CHART_OF_ACCOUNTS_H__
#define __CHART_OF_ACCOUNTS_H__

#include "commonObject.h"
#include "reference/reference.h"
#include "chartOfAccountsEnum.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectChartOfAccounts : public ibValueMetaObjectRecordDataHierarchyMutableRef {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectChartOfAccounts);
private:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
		ID_METATREE_EDIT_PREDEFINED = 19002,
	};

	enum
	{
		eFormObject = 1,
		eFormList,
		eFormSelect,
		eFormFolder,
		eFormFolderSelect
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormObject"), _("Form object"), eFormObject);
		formList.AppendItem(wxT("FormFolder"), _("Form group"), eFormFolder);
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		formList.AppendItem(wxT("FormSelect"), _("Form select"), eFormSelect);
		formList.AppendItem(wxT("FormGroupSelect"), _("Form group select"), eFormFolderSelect);
		return formList;
	}

public:

	// Own attributes accessors
	ibValueMetaObjectAttributePredefined* GetAccountType() const { return m_propertyAttributeAccountType->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetOffBalance() const { return m_propertyAttributeOffBalance->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetQuantitative() const { return m_propertyAttributeQuantitative->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetCurrency() const { return m_propertyAttributeCurrency->GetMetaObject(); }

	//default constructor
	ibValueMetaObjectChartOfAccounts();
	virtual ~ibValueMetaObjectChartOfAccounts();

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
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//form events
	virtual void OnCreateFormObject(ibValueMetaObjectFormBase* metaForm);
	virtual void OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm);

	//get attribute code
	virtual ibValueMetaObjectAttributeBase* GetAttributeForCode() const {
		return m_propertyAttributeCode->GetMetaObject();
	}

	//create associate value
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
	virtual ibBackendValueForm* GetFolderForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
	virtual ibBackendValueForm* GetFolderSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
#pragma endregion

	//descriptions...
	wxString GetDataPresentation(const ibValueDataObject* objValue) const;

	//get module object in compose object
	virtual ibValueMetaObjectModule* GetModuleObject() const { return m_propertyModuleObject->GetMetaObject(); }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return m_propertyModuleManager->GetMetaObject(); }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	//predefined array
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = {
			m_propertyAttributePredefined->GetMetaObject(),
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
			m_propertyAttributeParent->GetMetaObject(),
			m_propertyAttributeIsFolder->GetMetaObject(),
			m_propertyAttributeAccountType->GetMetaObject(),
			m_propertyAttributeOffBalance->GetMetaObject(),
			m_propertyAttributeQuantitative->GetMetaObject(),
			m_propertyAttributeCurrency->GetMetaObject(),
			m_propertyAttributeReference->GetMetaObject(),
			m_propertyAttributeDeletionMark->GetMetaObject(),
		};
		return true;
	}

	//searched array
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = {
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
		};
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue();

	//create empty object
	virtual ibValueRecordDataObjectHierarchyRef* CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid = wxNullGuid);

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(ibValueMetaObjectFormBase* metaObject);

	//load & save metaData from DB
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

private:

	bool FillFormObject(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormObject == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormFolder(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormFolder == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormList(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormList == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormSelect(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormSelect == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormFolderSelect(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormFolderSelect == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleObject = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ObjectModule"), _("Object module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleManager = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectChartOfAccounts::FillFormObject);
	ibPropertyList* m_propertyDefFormFolder = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolder"), _("Default Folder Form"), &ibValueMetaObjectChartOfAccounts::FillFormFolder);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectChartOfAccounts::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectChartOfAccounts::FillFormSelect);
	ibPropertyList* m_propertyDefFormFolderSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolderSelect"), _("Default Folder Select Form"), &ibValueMetaObjectChartOfAccounts::FillFormFolderSelect);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Own predefined attributes for Chart of Accounts

	ibPropertyCategory* m_categoryAccounting = ibPropertyObject::CreatePropertyCategory(wxT("Accounting"), _("Accounting"));

	ibPropertyInnerAttribute<>* m_propertyAttributeAccountType = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateNumber(wxT("AccountType"), _("Account type"), wxEmptyString, 1, 0, ibItemMode::ibItemMode_Folder_Item));

	ibPropertyInnerAttribute<>* m_propertyAttributeOffBalance = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("OffBalance"), _("Off-balance"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	ibPropertyInnerAttribute<>* m_propertyAttributeQuantitative = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("Quantitative"), _("Quantitative"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	ibPropertyInnerAttribute<>* m_propertyAttributeCurrency = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("Currency"), _("Currency accounting"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	friend class ibValueRecordDataObjectChartOfAccounts;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectChartOfAccounts : public ibValueRecordDataObjectHierarchyRef {
	ibValueRecordDataObjectChartOfAccounts(ibValueMetaObjectChartOfAccounts* metaObject, const ibGuid& objGuid = wxNullGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
	ibValueRecordDataObjectChartOfAccounts(const ibValueRecordDataObjectChartOfAccounts& source);
public:

	//save modify
	virtual bool SaveModify() { return WriteObject(); }

	//default methods
	virtual bool FillObject(ibValue& vFillObject) const { return Filling(vFillObject); }
	virtual ibValueRecordDataObjectRef* CopyObject(bool showValue = false) {
		ibValueRecordDataObjectRef* objectRef = CopyObjectValue();
		if (objectRef != nullptr && showValue)
			objectRef->ShowFormValue();
		return objectRef;
	}
	virtual bool WriteObject();
	virtual bool DeleteObject();

	virtual void PrepareNames() const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data
	virtual ibSourceExplorer GetSourceExplorer() const;

#pragma region _form_builder_h_
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* owner = nullptr);
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* owner = nullptr);
#pragma endregion

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectChartOfAccounts;
};

#endif
