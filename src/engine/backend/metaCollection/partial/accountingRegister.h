#ifndef __ACCOUNTING_REGISTER_H__
#define __ACCOUNTING_REGISTER_H__

#include "commonObject.h"
#include "accountingRegisterEnum.h"

class ibValueMetaObjectAccountingRegister : public ibValueMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectAccountingRegister);
private:
	enum
	{
		eFormList = 2,
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		return formList;
	}

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
	};

public:

	// Predefined attribute accessors
	ibValueMetaObjectAttributePredefined* GetRegisterRecordType() const {
		return m_propertyAttributeRecordType->GetMetaObject();
	}

	ibValueMetaObjectAttributePredefined* GetRegisterAccount() const {
		return m_propertyAttributeAccount->GetMetaObject();
	}

	ibValueMetaObjectAttributePredefined* GetRegisterSubconto1() const {
		return m_propertyAttributeSubconto1->GetMetaObject();
	}

	ibValueMetaObjectAttributePredefined* GetRegisterSubconto2() const {
		return m_propertyAttributeSubconto2->GetMetaObject();
	}

	ibValueMetaObjectAttributePredefined* GetRegisterSubconto3() const {
		return m_propertyAttributeSubconto3->GetMetaObject();
	}

	bool IsRegisterRecordType(const ibMetaID& id) const {
		return id == (*m_propertyAttributeRecordType)->GetMetaID();
	}

	bool IsRegisterAccount(const ibMetaID& id) const {
		return id == (*m_propertyAttributeAccount)->GetMetaID();
	}

	///////////////////////////////////////////////////////////////////

	wxString GetRegisterTableNameDB() const {
		wxString className = GetClassName();
		wxASSERT(m_metaId != 0);
		return wxString::Format("%s%i_T", className, GetMetaID());
	}

	///////////////////////////////////////////////////////////////////

	bool CreateAndUpdateRegisterTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	///////////////////////////////////////////////////////////////////

	ibValueMetaObjectAccountingRegister();
	virtual ~ibValueMetaObjectAccountingRegister();

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
	virtual bool HasRecordManager() const { return false; }

	//has recorder and period
	virtual bool HasPeriod() const { return true; }
	virtual bool HasRecorder() const { return true; }

	//get module object in compose object
	virtual ibValueMetaObjectModule* GetModuleObject() const { return m_propertyModuleObject->GetMetaObject(); }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return m_propertyModuleManager->GetMetaObject(); }

	//create associate value
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form
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
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = {
			m_propertyAttributeLineActive->GetMetaObject(),
			m_propertyAttributePeriod->GetMetaObject(),
			m_propertyAttributeRecordType->GetMetaObject(),
			m_propertyAttributeAccount->GetMetaObject(),
			m_propertyAttributeSubconto1->GetMetaObject(),
			m_propertyAttributeSubconto2->GetMetaObject(),
			m_propertyAttributeSubconto3->GetMetaObject(),
			m_propertyAttributeRecorder->GetMetaObject(),
			m_propertyAttributeLineNumber->GetMetaObject()
		};
		return true;
	}

	//get dimension keys
	virtual bool FillArrayObjectByDimention(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyAttributeRecorder->GetMetaObject() };
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue();

	//create record set
	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey);

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(ibValueMetaObjectFormBase* metaObject);

	//load & save metaData from DB
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	bool FillFormList(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormList == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleObject = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleManager = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectAccountingRegister::FillFormList);

	// Predefined attributes: RecordType (Debit/Credit)
	ibPropertyInnerAttribute<>* m_propertyAttributeRecordType = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateSpecialType(wxT("RecordType"), _("Record type"), wxEmptyString, g_enumAccountingRecordTypeCLSID, false, ibValueEnumAccountingRegisterRecordType::CreateDefEnumValue()));

	// Predefined attribute: Account (reference to Chart of Accounts - polymorphic)
	ibPropertyInnerAttribute<>* m_propertyAttributeAccount = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Account"), _("Account"), wxEmptyString, false, ibItemMode::ibItemMode_Item));

	// Predefined attributes: Subconto 1-3 (polymorphic references, type determined by ПВХ linked to account)
	ibPropertyInnerAttribute<>* m_propertyAttributeSubconto1 = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Subconto1"), _("Subconto 1"), wxEmptyString, false, ibItemMode::ibItemMode_Item));

	ibPropertyInnerAttribute<>* m_propertyAttributeSubconto2 = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Subconto2"), _("Subconto 2"), wxEmptyString, false, ibItemMode::ibItemMode_Item));

	ibPropertyInnerAttribute<>* m_propertyAttributeSubconto3 = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Subconto3"), _("Subconto 3"), wxEmptyString, false, ibItemMode::ibItemMode_Item));

	friend class ibValueRecordSetObjectAccountingRegister;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectAccountingRegister : public ibValueRecordSetObject {
public:
	ibValueRecordSetObjectAccountingRegister(ibValueMetaObjectAccountingRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey) {}

	ibValueRecordSetObjectAccountingRegister(const ibValueRecordSetObjectAccountingRegister& source) :
		ibValueRecordSetObject(source) {}

	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true);
	virtual bool DeleteRecordSet();

	virtual bool SaveVirtualTable();
	virtual bool DeleteVirtualTable();

	virtual void PrepareNames() const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
};

#endif
