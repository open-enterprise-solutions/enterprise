#ifndef __INFORMATION_REGISTER_H__
#define __INFORMATION_REGISTER_H__

#include "commonObject.h"
#include "informationRegisterEnum.h"

class ibValueMetaObjectInformationRegister : public ibValueMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectInformationRegister);
private:
	enum
	{
		eFormRecord = 1,
		eFormList = 2,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormRecord"), _("Form record"), eFormRecord);
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		return formList;
	}

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

public:
	class ibValueMetaObjectRecordManager : public ibValueMetaObject {
		wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectRecordManager);
	public:
		ibValueMetaObjectRecordManager() : ibValueMetaObject() {}
	};

public:

	ibValueMetaObjectInformationRegister();
	virtual ~ibValueMetaObjectInformationRegister();

	ibWriteRegisterMode GetWriteRegisterMode() const {
		return m_propertyWriteMode->GetValueAsEnum();
	}

	ibPeriodicity GetPeriodicity() const {
		return m_propertyPeriodicity->GetValueAsEnum();
	}

	bool CreateAndUpdateSliceFirstTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);
	bool CreateAndUpdateSliceLastTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

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

	//has record manager 
	virtual bool HasRecordManager() const { return GetWriteRegisterMode() == ibWriteRegisterMode::eIndependent; }

	//has recorder and period 
	virtual bool HasPeriod() const { return GetPeriodicity() != ibPeriodicity::eNonPeriodic; }
	virtual bool HasRecorder() const { return GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder; }

	//get module object in compose object 
	virtual ibValueMetaObjectModule* GetModuleObject() const { return m_propertyModuleObject->GetMetaObject(); }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return m_propertyModuleManager->GetMetaObject(); }

	//create associate value 
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetRecordForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey);
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey);
#pragma endregion

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	//create and update table 
	virtual bool CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	//get default attributes
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const {

		if (GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
			array.emplace_back(m_propertyAttributeLineActive->GetMetaObject());
		}

		if (GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
			GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
			array.emplace_back(m_propertyAttributePeriod->GetMetaObject());
		}

		if (GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
			array.emplace_back(m_propertyAttributeRecorder->GetMetaObject());
			array.emplace_back(m_propertyAttributeLineNumber->GetMetaObject());
		}

		return true;
	}

	//get dimension keys 
	virtual bool FillArrayObjectByDimention(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {

		if (GetWriteRegisterMode() != ibWriteRegisterMode::eSubordinateRecorder) {

			if (GetPeriodicity() != ibPeriodicity::eNonPeriodic) {
				array.emplace_back(m_propertyAttributePeriod->GetMetaObject());
			}

			FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID });
		}
		else {
			array = { m_propertyAttributeRecorder->GetMetaObject() };
		}
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue();

	//create record set
	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey);
	virtual ibValueRecordManagerObject* CreateRecordManagerObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey);

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(ibValueMetaObjectFormBase* metaObject);

	//get command section 
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Combined; }

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

protected:

	//get default form 
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Create)
			return GetRecordForm();
		else if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_List)
			return GetListForm();

		return GetListForm();
	}

private:

	bool FillFormRecord(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormRecord == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object);
			}
		}
		return true;
	}

	bool FillFormList(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormList == object->GetTypeForm()) {
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object);
			}
		}
		return true;
	}

	ibValueMetaObjectRecordManager* m_metaRecordManager;

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleObject = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleManager = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormRecord = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormRecord"), _("Default Record Form"), &ibValueMetaObjectInformationRegister::FillFormRecord);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectInformationRegister::FillFormList);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyEnum<ibValueEnumPeriodicity>* m_propertyPeriodicity = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumPeriodicity>>(m_categoryData, wxT("Periodicity"), _("Periodicity"), ibPeriodicity::eNonPeriodic);
	ibPropertyEnum<ibValueEnumWriteRegisterMode>* m_propertyWriteMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumWriteRegisterMode>>(m_categoryData, wxT("WriteMode"), _("Write mode"), ibWriteRegisterMode::eIndependent);

	friend class ibValueRecordSetObjectInformationRegister;
	friend class ibValueRecordManagerObjectInformationRegister;

	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectInformationRegister : public ibValueRecordSetObject {
	ibValueRecordSetObjectInformationRegister(ibValueMetaObjectInformationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey) {
	}
	ibValueRecordSetObjectInformationRegister(const ibValueRecordSetObjectInformationRegister& source) :
		ibValueRecordSetObject(source) {
	}
public:

	//default methods
	virtual ibValueRecordSetObject* CopyRegisterValue() {
		return new ibValueRecordSetObjectInformationRegister(*this);
	}

	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true);
	virtual bool DeleteRecordSet();

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

protected:
	friend class ibValue;
	friend class ibValueMetaObjectInformationRegister;
};

class ibValueRecordManagerObjectInformationRegister : public ibValueRecordManagerObject {
public:
	ibValueRecordManagerObjectInformationRegister(ibValueMetaObjectInformationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordManagerObject(metaObject, uniqueKey)
	{
	}
	ibValueRecordManagerObjectInformationRegister(const ibValueRecordManagerObjectInformationRegister& source) :
		ibValueRecordManagerObject(source)
	{
	}
	virtual ibValueRecordManagerObject* CopyRegister(bool showValue = false) {
		ibValueRecordManagerObject* objectRef = CopyRegisterValue();
		if (objectRef != nullptr && showValue)
			objectRef->ShowFormValue();
		return objectRef;
	}
	virtual bool WriteRegister(bool replace = true);
	virtual bool DeleteRegister();

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
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
#pragma endregion

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectInformationRegister;
};

#endif 