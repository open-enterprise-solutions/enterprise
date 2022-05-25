#ifndef _INFORMATION_REGISTER_H__
#define _INFORMATION_REGISTER_H__

#include "baseObject.h"

enum eWriteRegisterMode {
	eIndependent,
	eSubordinateRecorder
};

enum ePeriodicity {
	eNonPeriodic,
	eWithinSecond,
	eWithinDay,
};

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
		optionlist->AddOption(_("formRecord"), eFormRecord);
		optionlist->AddOption(_("formList"), eFormList);

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

private:

	//default form 
	int m_defaultFormRecord;
	int m_defaultFormList;

	//data 
	eWriteRegisterMode m_writeMode;
	ePeriodicity m_periodicity;

private:

	OptionList* GetFormRecord(Property*);
	OptionList* GetFormList(Property*);

	OptionList* GetPeriodicity(Property*)
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("non-periodic"), eNonPeriodic);
		optionlist->AddOption(_("within a second"), eWithinSecond);
		optionlist->AddOption(_("within a day"), eWithinDay);

		return optionlist;
	}

	OptionList* GetWriteMode(Property*)
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("independent"), eIndependent);
		optionlist->AddOption(_("subordinate to recorder"), eSubordinateRecorder);

		return optionlist;
	}

public:

	eWriteRegisterMode GetWriteRegisterMode() const {
		return m_writeMode;
	}

	ePeriodicity GetPeriodicity() const {
		return m_periodicity;
	}

	bool CreateAndUpdateSliceFirstTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);
	bool CreateAndUpdateSliceLastTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	///////////////////////////////////////////////////////////////////

	CMetaObjectInformationRegister();
	virtual ~CMetaObjectInformationRegister();

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
		return m_writeMode == eWriteRegisterMode::eIndependent;
	}

	//has recorder 
	virtual bool HasRecorder() const {
		return m_writeMode == eWriteRegisterMode::eSubordinateRecorder;
	}

	virtual IRecordSetObject* CreateRecordSet();
	virtual IRecordSetObject* CreateRecordSet(const CUniquePairKey& uniqueKey);
	virtual IRecordManagerObject* CreateRecordManager();
	virtual IRecordManagerObject* CreateRecordManager(const CUniquePairKey& uniqueKey);

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const { return m_moduleObject; }
	virtual CMetaCommonModuleObject* GetModuleManager() const { return m_moduleManager; }

	//create associate value 
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t &id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create associate value 
	virtual IRecordSetObject* CreateRecordSetValue() override;
	virtual IRecordManagerObject* CreateRecordManagerValue() override;

	//create form with data 
	virtual CValueForm* CreateObjectValue(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//support form 
	virtual CValueForm* GetRecordForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniquePairKey& formGuid = NULL);
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = Guid());

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(Property* property);

	//read and write property 
	virtual void ReadProperty() override;
	virtual	void SaveProperty() override;

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:

	//support form 
	virtual CValueForm* GetRecordForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid);
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CRecordSetInformationRegister : public IRecordSetObject {
public:

	CRecordSetInformationRegister(CMetaObjectInformationRegister* metaObject) :
		IRecordSetObject(metaObject)
	{
	}

	CRecordSetInformationRegister(CMetaObjectInformationRegister* metaObject, const CUniquePairKey& uniqueKey) :
		IRecordSetObject(metaObject, uniqueKey)
	{
	}

	CRecordSetInformationRegister(const CRecordSetInformationRegister& source) :
		IRecordSetObject(source)
	{
	}

	//default methods
	virtual IRecordSetObject* CopyRegisterValue() {
		return new CRecordSetInformationRegister(*this);
	}

	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true);
	virtual bool DeleteRecordSet();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethods* GetPMethods() const;
	virtual void PrepareNames() const;
	virtual CValue Method(methodArg_t& aParams);

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
	virtual CValue GetAttribute(attributeArg_t& aParams);
};

class CRecordManagerInformationRegister : public IRecordManagerObject {
public:

	CRecordManagerInformationRegister(CMetaObjectInformationRegister* metaObject) :
		IRecordManagerObject(metaObject)
	{
	}

	CRecordManagerInformationRegister(CMetaObjectInformationRegister* metaObject, const CUniquePairKey& uniqueKey) :
		IRecordManagerObject(metaObject, uniqueKey)
	{
	}

	CRecordManagerInformationRegister(const CRecordManagerInformationRegister& source) :
		IRecordManagerObject(source)
	{
	}

	//default methods
	virtual IRecordManagerObject* CopyRegisterValue() {
		return new CRecordManagerInformationRegister(*this);
	}

	virtual CValue CopyRegister();
	virtual bool WriteRegister(bool replace = true);
	virtual bool DeleteRegister();

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	virtual CMethods* GetPMethods() const;
	virtual void PrepareNames() const;
	virtual CValue Method(methodArg_t& aParams);

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
	virtual CValue GetAttribute(attributeArg_t& aParams);

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL);

	//support actions
	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm);
};

#endif 