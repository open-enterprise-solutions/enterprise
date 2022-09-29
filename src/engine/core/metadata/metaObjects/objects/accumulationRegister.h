#ifndef _ACCUMULATION_REGISTER_H__
#define _ACCUMULATION_REGISTER_H__

#include "object.h"

#include "accumulationRegisterEnum.h"

class CMetaObjectAccumulationRegister : public IMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectAccumulationRegister);

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

	CMetaModuleObject* m_moduleObject;
	CMetaCommonModuleObject* m_moduleManager;

private:

	CMetaDefaultAttributeObject* m_attributeRecordType;

protected:

	PropertyCategory* m_categoryForm = IPropertyObject::CreatePropertyCategory({ "defaultForms", "default forms"});
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

	CMetaDefaultAttributeObject* GetRegisterRecordType() const {
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
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullUniqueKey);

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

	CRecordSetAccumulationRegister(CMetaObjectAccumulationRegister* metaObject, const CUniquePairKey& uniqueKey = wxNullUniquePairKey) :
		IRecordSetObject(metaObject, uniqueKey)
	{
	}

	CRecordSetAccumulationRegister(const CRecordSetAccumulationRegister& source) :
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