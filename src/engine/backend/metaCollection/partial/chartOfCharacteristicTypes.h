#ifndef __CHART_OF_CHARACTERISTIC_TYPES_H__
#define __CHART_OF_CHARACTERISTIC_TYPES_H__

#include "commonObject.h"
#include "reference/reference.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectChartOfCharacteristicTypes : 
	public ibValueMetaObjectRecordDataHierarchyMutableRef, 
	public ibBackendTypeConfigFactory {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectChartOfCharacteristicTypes);
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

	ibValueMetaObjectAttributePredefined* GetDataType() const { return m_propertyAttributeType->GetMetaObject(); }
	virtual bool IsDataType(const ibMetaID& id) const { return id == (*m_propertyAttributeType)->GetMetaID(); }

	//default constructor
	ibValueMetaObjectChartOfCharacteristicTypes();
	virtual ~ibValueMetaObjectChartOfCharacteristicTypes();

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

	//get type desc
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertyTypesOfCharacteristics->GetValueAsTypeDesc(); }

	//get metadata
	virtual ibMetaData* GetMetaData() const { return m_metaData; }

	//predefined array
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = {
			m_propertyAttributeType->GetMetaObject(),
			m_propertyAttributePredefined->GetMetaObject(),
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
			m_propertyAttributeParent->GetMetaObject(),
			m_propertyAttributeIsFolder->GetMetaObject(),
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

	ibPropertyCategory* m_categoryType = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyType* m_propertyTypesOfCharacteristics = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryType, wxT("TypesOfCharacteristics"), _("Types of Characteristics"), ibValueTypes::TYPE_STRING);

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormObject);
	ibPropertyList* m_propertyDefFormFolder = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolder"), _("Default Folder Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormFolder);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormSelect);
	ibPropertyList* m_propertyDefFormFolderSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolderSelect"), _("Default Folder Select Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormFolderSelect);

	//default array 
	ibPropertyContainer<>* m_propertyAttributeType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("Type"), _("Type"), wxEmptyString, string_to_clsid("VL_TYPED"), ibItemMode::ibItemMode_Item));

	friend class ibValueRecordDataObjectChartOfCharacteristicTypes;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectChartOfCharacteristicTypes : public ibValueRecordDataObjectHierarchyRef {
	ibValueRecordDataObjectChartOfCharacteristicTypes(ibValueMetaObjectChartOfCharacteristicTypes* metaObject, const ibGuid& objGuid = wxNullGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
	ibValueRecordDataObjectChartOfCharacteristicTypes(const ibValueRecordDataObjectChartOfCharacteristicTypes& source);
public:

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

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

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual void PrepareNames() const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data
	virtual ibSourceExplorer GetSourceExplorer() const;

#pragma region _form_builder_h_
	//support show
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* owner = nullptr);
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* owner = nullptr);
#pragma endregion

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectChartOfCharacteristicTypes;
};

#endif
