#ifndef _INFORMATION_REGISTER_H__
#define _INFORMATION_REGISTER_H__

#include "object.h"

#include "informationRegisterEnum.h"

class CMetaObjectInformationRegister : public IMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectInformationRegister);

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

	CMetaModuleObject* m_moduleObject;
	CMetaCommonModuleObject* m_moduleManager;

	class CMetaObjectRecordManager : public IMetaObject {
	public:
		CMetaObjectRecordManager() : IMetaObject() {}

		virtual wxString GetClassName() const {
			return wxT("recordManager");
		}
	};

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

	bool CreateAndUpdateSliceFirstTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);
	bool CreateAndUpdateSliceLastTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	///////////////////////////////////////////////////////////////////

	virtual wxString GetClassName() const {
		return wxT("informationRegister");
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
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject* metaForm);
	virtual void OnRemoveMetaForm(IMetaFormObject* metaForm);

	//get default attributes
	virtual std::vector<IMetaAttributeObject*> GetDefaultAttributes() const;

	//get dimension keys 
	virtual std::vector<IMetaAttributeObject*> GetGenericDimensions() const;

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
	virtual CMetaModuleObject* GetModuleObject() const {
		return m_moduleObject;
	}

	virtual CMetaCommonModuleObject* GetModuleManager() const {
		return m_moduleManager;
	}

	//create associate value 
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t& id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create form with data 
	virtual CValueForm* CreateObjectForm(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual CValueForm* GetRecordForm(const wxString& formName = wxEmptyString, IControlFrame* ownerControl = NULL, const CUniquePairKey& formGuid = wxNullUniquePairKey);
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IControlFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullUniqueKey);

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(Property* property);

protected:

	virtual IRecordSetObject* CreateRecordSetObjectRegValue(const CUniquePairKey& uniqueKey = wxNullUniquePairKey);
	virtual IRecordManagerObject* CreateRecordManagerObjectRegValue(const CUniquePairKey& uniqueKey = wxNullUniquePairKey);

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:

	//support form 
	virtual CValueForm* GetRecordForm(const meta_identifier_t& id, IControlFrame* ownerControl, const CUniqueKey& formGuid);

protected:
	friend class CRecordSetInformationRegister;
	friend class CRecordManagerInformationRegister;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class CRecordSetInformationRegister : public IRecordSetObject {
protected:

	CRecordSetInformationRegister(CMetaObjectInformationRegister* metaObject, const CUniquePairKey& uniqueKey = wxNullUniquePairKey) :
		IRecordSetObject(metaObject, uniqueKey) {
	}

	CRecordSetInformationRegister(const CRecordSetInformationRegister& source) :
		IRecordSetObject(source) {
	}

public:

	//default methods
	virtual IRecordSetObject* CopyRegisterValue() {
		return new CRecordSetInformationRegister(*this);
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
	friend class CMetaObjectInformationRegister;
};

class CRecordManagerInformationRegister : public IRecordManagerObject {
public:

	CRecordManagerInformationRegister(CMetaObjectInformationRegister* metaObject, const CUniquePairKey& uniqueKey = wxNullUniquePairKey) :
		IRecordManagerObject(metaObject, uniqueKey)
	{
	}

	CRecordManagerInformationRegister(const CRecordManagerInformationRegister& source) :
		IRecordManagerObject(source)
	{
	}

	virtual IRecordManagerObject* CopyRegister(bool showValue = false) {
		IRecordManagerObject* objectRef = CopyRegisterValue();
		if (objectRef != NULL && showValue)
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
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IControlFrame* owner = NULL);
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IControlFrame* owner = NULL);

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, CValueForm* srcForm);

protected:
	friend class CMetaObjectInformationRegister;
};

#endif 