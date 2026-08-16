#ifndef __CHART_OF_CHARACTERISTIC_TYPES_H__
#define __CHART_OF_CHARACTERISTIC_TYPES_H__

#include "commonObject.h"
#include "reference/reference.h"
#include "backend/system/value/valueType.h"   // g_valueTypeDescriptionCLSID — the characteristic's own Type

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectChartOfCharacteristicTypes : 
	public ibValueMetaObjectRecordDataHierarchyMutableRef, 
	public ibBackendTypeConfigFactory {
	public:
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

	// THE CONTOUR — every value a characteristic of this chart may ever take.
	//
	// Public because it is what the OUTSIDE asks for: an accounting register types its dimension
	// VALUE slots by it, and an editor offers it as the permitted set. `GetTypeDesc` says the same
	// thing but stays protected — it is the type-factory override, an internal contract, and a
	// caller reaching for it would be borrowing a mechanism instead of asking a question.
	ibTypeDescription& GetTypesOfCharacteristics() const { return m_propertyTypesOfCharacteristics->GetValueAsTypeDesc(); }

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
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetFolderForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetFolderSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion

	//descriptions...
	wxString GetDataPresentation(const ibValueDataObject* objValue) const;

	//get module object in compose object
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

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
	virtual const ibMetaData* GetMetaData() const { return m_metaData; }
	virtual ibMetaData* GetMetaData() { return m_metaData; }

	// Additive contract — chains to HierarchyMutableRef. ChartOfChar
	// adds only its Type attribute on top of the inherited set.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataHierarchyMutableRef::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeType->GetMetaObject());
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
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create empty object
	virtual ibValueRecordDataObjectHierarchyRef* CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid = wxNullGuid) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ObjectModule"), _("Object module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryType = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyType* m_propertyTypesOfCharacteristics = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryType, wxT("TypesOfCharacteristics"), _("Types of Characteristics"), ibValueTypes::TYPE_STRING);

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormObject);
	ibPropertyList* m_propertyDefFormFolder = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolder"), _("Default Folder Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormFolder);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormSelect);
	ibPropertyList* m_propertyDefFormFolderSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolderSelect"), _("Default Folder Select Form"), &ibValueMetaObjectChartOfCharacteristicTypes::FillFormFolderSelect);

	//default array
	// THE CHARACTERISTIC'S OWN TYPE — a filter over what this chart declares, held as a type
	// description rather than a value. FILL-CHECKED: a characteristic must name a concrete type,
	// and empty is an ERROR rather than "everything the contour allows". Without that, a
	// characteristic with no type would let a value of any kind into a slot the kind was supposed
	// to narrow — which is the whole purpose of this second tier.
	ibPropertyContainer<>* m_propertyAttributeType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("Type"), _("Type"), wxEmptyString, g_valueTypeDescriptionCLSID, /*fillCheck*/ true, ibValue(), ibItemMode::ibItemMode_Item));

	friend class ibValueRecordDataObjectChartOfCharacteristicTypes;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectChartOfCharacteristicTypes : public ibValueRecordDataObjectHierarchyRef {
	public:
	ibValueRecordDataObjectChartOfCharacteristicTypes(const ibValueMetaObjectChartOfCharacteristicTypes* metaObject, const ibGuid& objGuid = wxNullGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
	ibValueRecordDataObjectChartOfCharacteristicTypes(const ibValueRecordDataObjectChartOfCharacteristicTypes& source);
public:

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	// SaveModify / FillObject / CopyObject / WriteObject / DeleteObject
	// inherited from ibValueRecordDataObjectHierarchyRef and
	// ibValueRecordDataObjectRef.

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	// Own methods (data members come from the base FillDataMembers); bound in the ctor.
	void FillMethods(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	// ShowFormValue / GetFormValue inherited from HierarchyRef.
protected:
	virtual ibFormID GetCurrentObjectFormID() const override {
		return m_objMode == ibObjectMode::OBJECT_ITEM
			? ibValueMetaObjectChartOfCharacteristicTypes::eFormObject
			: ibValueMetaObjectChartOfCharacteristicTypes::eFormFolder;
	}
public:

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectChartOfCharacteristicTypes;
};

#endif
