#ifndef _ACCUMULATION_REGISTER_H__
#define _ACCUMULATION_REGISTER_H__

#include "object.h"

#include "accumulationRegisterEnum.h"

class CMetaObjectAccumulationRegister : public IMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectAccumulationRegister);
private:
	enum
	{
		eFormList = 1,
	};

	virtual OptionList* GetFormType() override
	{
		OptionList* optionlist = new OptionList;
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

private:

	CMetaObjectAttributeDefault* m_attributeRecordType;

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms" });
	Property* m_propertyDefFormList = IPropertyObject::CreateProperty(m_categoryForm, { "default_list",  "default list" }, &CMetaObjectAccumulationRegister::GetFormList, wxNOT_FOUND);

	PropertyCategory* m_categoryData = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertyRegisterType = IPropertyObject::CreateProperty(m_categoryData, { "register_type", "register type" }, &CMetaObjectAccumulationRegister::GetRegisterType, eRegisterType::eBalances);

private:

	OptionList* GetRegisterType(PropertyOption*) {
		OptionList* optionlist = new OptionList;
		optionlist->AddOption(_("balances"), eBalances);
		optionlist->AddOption(_("turnovers"), eTurnovers);
		return optionlist;
	}

	OptionList* GetFormList(PropertyOption*);

public:

	CMetaObjectAttributeDefault* GetRegisterRecordType() const {
		return m_attributeRecordType;
	}

	bool IsRegisterRecordType(const meta_identifier_t& id) const {
		return id == m_attributeRecordType->GetMetaID();
	}

	///////////////////////////////////////////////////////////////////

	eRegisterType GetRegisterType() const {
		return (eRegisterType)m_propertyRegisterType->GetValueAsInteger();
	}

	wxString GetRegisterTableNameDB(eRegisterType rType) const {
		wxString className = GetClassName();
		wxASSERT(m_metaId != 0);

		if (rType == eRegisterType::eBalances) {
			return wxString::Format("%s%i_T",
				className, GetMetaID());
		}

		return wxString::Format("%s%i_Tn",
			className, GetMetaID());
	}

	wxString GetRegisterTableNameDB() const {
		return GetRegisterTableNameDB(GetRegisterType());
	}

	///////////////////////////////////////////////////////////////////

	bool CreateAndUpdateBalancesTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags);
	bool CreateAndUpdateTurnoverTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags);

	///////////////////////////////////////////////////////////////////

	CMetaObjectAccumulationRegister();
	virtual ~CMetaObjectAccumulationRegister();

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
		return false;
	}

	//has recorder and period 
	virtual bool HasPeriod() const {
		return true;
	}

	virtual bool HasRecorder() const {
		return true;
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

	//load & save metaData from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:
	friend class IMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class CRecordSetObjectAccumulationRegister : public IRecordSetObject {
public:
	CRecordSetObjectAccumulationRegister(CMetaObjectAccumulationRegister* metaObject, const CUniquePairKey& uniqueKey = wxNullUniquePairKey) :
		IRecordSetObject(metaObject, uniqueKey)
	{
	}

	CRecordSetObjectAccumulationRegister(const CRecordSetObjectAccumulationRegister& source) :
		IRecordSetObject(source)
	{
	}

	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true);
	virtual bool DeleteRecordSet();

	//////////////////////////////////////////////////////////////////////////////

	virtual bool SaveVirtualTable();
	virtual bool DeleteVirtualTable();

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
};

#endif 