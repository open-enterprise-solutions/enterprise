#ifndef _INFORMATION_REGISTER_H__
#define _INFORMATION_REGISTER_H__

#include "object.h"

#include "informationRegisterEnum.h"

class CMetaObjectInformationRegister : public IMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectInformationRegister);
private:
	enum
	{
		eFormRecord = 1,
		eFormList,
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(wxT("formRecord"), _("Form record"), eFormRecord);
		optionlist->AddOption(wxT("formList"), _("Form list"), eFormList);
		return optionlist;
	}

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

	CMetaObjectModule* m_moduleObject;
	CMetaObjectCommonModule* m_moduleManager;

public:
	class CMetaObjectRecordManager : public IMetaObject {
		wxDECLARE_DYNAMIC_CLASS(CMetaObjectRecordManager);
	public:
		CMetaObjectRecordManager() : IMetaObject() {}
	};
private:
	CMetaObjectRecordManager* m_metaRecordManager;

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms" });
	Property* m_propertyDefFormRecord = IPropertyObject::CreateProperty(m_categoryForm, { "default_record", "default record" }, &CMetaObjectInformationRegister::GetFormRecord, wxNOT_FOUND);
	Property* m_propertyDefFormList = IPropertyObject::CreateProperty(m_categoryForm, { "default_list", "default list" }, &CMetaObjectInformationRegister::GetFormList, wxNOT_FOUND);

	PropertyCategory* m_categoryData = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertyPeriodicity = IPropertyObject::CreateProperty(m_categoryData, { "periodicity", "periodicity" }, &CMetaObjectInformationRegister::GetPeriodicity, ePeriodicity::eNonPeriodic);
	Property* m_propertyWriteMode = IPropertyObject::CreateProperty(m_categoryData, { "write_mode", "write mode" }, &CMetaObjectInformationRegister::GetWriteMode, eWriteRegisterMode::eIndependent);

private:

	OptionList* GetPeriodicity(PropertyOption*) {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("non-periodic"), eNonPeriodic);
		optionlist->AddOption(_("within a second"), eWithinSecond);
		optionlist->AddOption(_("within a day"), eWithinDay);

		return optionlist;
	}

	OptionList* GetWriteMode(PropertyOption*) {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("independent"), eIndependent);
		optionlist->AddOption(_("subordinate to recorder"), eSubordinateRecorder);

		return optionlist;
	}

	OptionList* GetFormRecord(PropertyOption*);
	OptionList* GetFormList(PropertyOption*);

public:

	CMetaObjectInformationRegister();
	virtual ~CMetaObjectInformationRegister();

	eWriteRegisterMode GetWriteRegisterMode() const {
		return (eWriteRegisterMode)m_propertyWriteMode->GetValueAsInteger();
	}

	ePeriodicity GetPeriodicity() const {
		return (ePeriodicity)m_propertyPeriodicity->GetValueAsInteger();
	}

	bool CreateAndUpdateSliceFirstTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags);
	bool CreateAndUpdateSliceLastTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags);

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
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//form events 
	virtual void OnCreateFormObject(IMetaObjectForm* metaForm);
	virtual void OnRemoveMetaForm(IMetaObjectForm* metaForm);

	//get default attributes
	virtual std::vector<IMetaObjectAttribute*> GetDefaultAttributes() const;

	//get dimension keys 
	virtual std::vector<IMetaObjectAttribute*> GetGenericDimensions() const;

	//has record manager 
	virtual bool HasRecordManager() const {
		return GetWriteRegisterMode() == eWriteRegisterMode::eIndependent;
	}

	//has recorder and period 
	virtual bool HasPeriod() const {
		return GetPeriodicity() != ePeriodicity::eNonPeriodic;
	}

	virtual bool HasRecorder() const {
		return GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder;
	}

	//get module object in compose object 
	virtual CMetaObjectModule* GetModuleObject() const {
		return m_moduleObject;
	}

	virtual CMetaObjectCommonModule* GetModuleManager() const {
		return m_moduleManager;
	}

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
	virtual IBackendValueForm* GetRecordForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniquePairKey& formGuid = wxNullUniquePairKey);
	virtual IBackendValueForm* GetListForm(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr, const CUniqueKey& formGuid = wxNullUniqueKey);

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual IRecordSetObject* CreateRecordSetObjectRegValue(const CUniquePairKey& uniqueKey = wxNullUniquePairKey);
	virtual IRecordManagerObject* CreateRecordManagerObjectRegValue(const CUniquePairKey& uniqueKey = wxNullUniquePairKey);

	//load & save metaData from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:

	//support form 
	virtual IBackendValueForm* GetRecordForm(const meta_identifier_t& id, IBackendControlFrame* ownerControl, const CUniqueKey& formGuid);

protected:
	friend class IMetaData;
	friend class CRecordSetObjectInformationRegister;
	friend class CRecordManagerObjectInformationRegister;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class CRecordSetObjectInformationRegister : public IRecordSetObject {
	CRecordSetObjectInformationRegister(CMetaObjectInformationRegister* metaObject, const CUniquePairKey& uniqueKey = wxNullUniquePairKey) :
		IRecordSetObject(metaObject, uniqueKey) {
	}
	CRecordSetObjectInformationRegister(const CRecordSetObjectInformationRegister& source) :
		IRecordSetObject(source) {
	}
public:

	//default methods
	virtual IRecordSetObject* CopyRegisterValue() {
		return new CRecordSetObjectInformationRegister(*this);
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
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

protected:
	friend class IMetaData;
	friend class CMetaObjectInformationRegister;
};

class CRecordManagerObjectInformationRegister : public IRecordManagerObject {
public:
	CRecordManagerObjectInformationRegister(CMetaObjectInformationRegister* metaObject, const CUniquePairKey& uniqueKey = wxNullUniquePairKey) :
		IRecordManagerObject(metaObject, uniqueKey)
	{
	}
	CRecordManagerObjectInformationRegister(const CRecordManagerObjectInformationRegister& source) :
		IRecordManagerObject(source)
	{
	}
	virtual IRecordManagerObject* CopyRegister(bool showValue = false) {
		IRecordManagerObject* objectRef = CopyRegisterValue();
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
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr);
	virtual IBackendValueForm* GetFormValue(const wxString& formName = wxEmptyString, IBackendControlFrame* ownerControl = nullptr);

	//support actionData
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

protected:
	friend class IMetaData;
	friend class CMetaObjectInformationRegister;
};

#endif 