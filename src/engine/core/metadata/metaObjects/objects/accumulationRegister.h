#ifndef _ACCUMULATION_REGISTER_H__
#define _ACCUMULATION_REGISTER_H__

#include "baseObject.h"
#include "compiler/enumObject.h"

enum eRegisterType {
	eBalances,
	eTurnovers
};

enum eRecordType {
	eExpense,
	eReceipt
};

const CLASS_ID g_enumRecordTypeCLSID = TEXT2CLSID("EN_RETP");

class CValueEnumAccumulationRegisterRecordType : public IEnumeration<eRecordType> {
	wxDECLARE_DYNAMIC_CLASS(CValueEnumAccumulationRegisterRecordType);
public:

	static CValue CreateDefEnumValue();

	CValueEnumAccumulationRegisterRecordType() : IEnumeration() {
		InitializeEnumeration();
	}

	CValueEnumAccumulationRegisterRecordType(eRecordType recordType) : IEnumeration(recordType) {
		InitializeEnumeration(recordType);
	}

	virtual wxString GetTypeString() const override {
		return wxT("accumulationRecordType");
	}

protected:

	void CreateEnumeration()
	{
		AddEnumeration(eExpense, wxT("expense"));
		AddEnumeration(eReceipt, wxT("receipt"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eRecordType value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

////////////////////////////////////////////////////////////////////////////

class CMetaObjectAccumulationRegister : public IMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectAccumulationRegister);

	enum
	{
		eFormList = 1,
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;
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

private:

	CMetaDefaultAttributeObject* m_attributeRecordType;

	//default form 
	int m_defaultFormList;

	//data 
	eRegisterType m_registerType;

private:

	OptionList* GetFormList(Property*);

	OptionList* GetRegisterType(Property*)
	{
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("balances"), eBalances);
		optionlist->AddOption(_("turnovers"), eTurnovers);

		return optionlist;
	}

public:

	CMetaDefaultAttributeObject* GetRegisterRecordType() const {
		return m_attributeRecordType;
	}

	bool IsRegisterRecordType(const meta_identifier_t& id) const {
		return id == m_attributeRecordType->GetMetaID();
	}

	///////////////////////////////////////////////////////////////////

	eRegisterType GetRegisterType() const {
		return m_registerType;
	}

	wxString GetRegisterTableNameDB(eRegisterType rType) const
	{
		wxString className = GetClassName();
		wxASSERT(m_metaId != 0);

		if (rType == eRegisterType::eBalances) {
			return wxString::Format("%s%i_T",
				className, GetMetaID());
		}

		return wxString::Format("%s%i_Tn",
			className, GetMetaID());
	}

	wxString GetRegisterTableNameDB() const
	{
		return GetRegisterTableNameDB(m_registerType);
	}

	///////////////////////////////////////////////////////////////////

	bool CreateAndUpdateBalancesTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);
	bool CreateAndUpdateTurnoverTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	///////////////////////////////////////////////////////////////////

	CMetaObjectAccumulationRegister();
	virtual ~CMetaObjectAccumulationRegister();

	virtual wxString GetClassName() const {
		return wxT("accumulationRegister");
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
		return false;
	}

	//has recorder 
	virtual bool HasRecorder() const {
		return true;
	}

	virtual IRecordSetObject* CreateRecordSet();
	virtual IRecordSetObject* CreateRecordSet(const CUniquePairKey& uniqueKey);

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const { return m_moduleObject; }
	virtual CMetaCommonModuleObject* GetModuleManager() const { return m_moduleManager; }

	//create associate value 
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t& id);

	//create object data with metaForm
	virtual ISourceDataObject* CreateObjectData(IMetaFormObject* metaObject);

	//create associate value 
	virtual IRecordSetObject* CreateRecordSetValue() override;

	//create form with data 
	virtual CValueForm* CreateObjectValue(IMetaFormObject* metaForm) override {
		return metaForm->GenerateFormAndRun(
			NULL, CreateObjectData(metaForm)
		);
	}

	//support form 
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
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#define thisObject wxT("thisObject")

class CRecordSetAccumulationRegister : public IRecordSetObject {
public:

	CRecordSetAccumulationRegister(CMetaObjectAccumulationRegister* metaObject) :
		IRecordSetObject(metaObject)
	{
	}

	CRecordSetAccumulationRegister(CMetaObjectAccumulationRegister* metaObject, const CUniquePairKey& uniqueKey) :
		IRecordSetObject(metaObject, uniqueKey)
	{
	}

	CRecordSetAccumulationRegister(const CRecordSetAccumulationRegister& source) :
		IRecordSetObject(source)
	{
	}

	//default methods
	virtual IRecordSetObject* CopyRegisterValue() {
		return new CRecordSetAccumulationRegister(*this);
	}

	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true);
	virtual bool DeleteRecordSet();

	//////////////////////////////////////////////////////////////////////////////

	virtual bool SaveVirtualTable();
	virtual bool DeleteVirtualTable();

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

#endif 