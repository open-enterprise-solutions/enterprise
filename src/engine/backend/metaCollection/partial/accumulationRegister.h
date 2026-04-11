#ifndef __ACCUMULATION_REGISTER_H__
#define __ACCUMULATION_REGISTER_H__

#include "commonObject.h"
#include "accumulationRegisterEnum.h"

class ibValueMetaObjectAccumulationRegister : public ibValueMetaObjectRegisterData {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectAccumulationRegister);
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

	//private:
		//ibValueMetaObjectAttributePredefined* m_attributibRecordType = ibValueMetaObjectCompositeData::CreateSpecialType(wxT("recordType"), _("Record type"), wxEmptyString, g_enumRecordTypeCLSID, false, ibValueEnumAccumulationRegisterRecordType::CreateDefEnumValue());

public:

	ibValueMetaObjectAttributePredefined* GetRegisterRecordType() const {
		return m_propertyAttributibRecordType->GetMetaObject();
	}

	bool IsRegisterRecordType(const ibMetaID& id) const {
		return id == (*m_propertyAttributibRecordType)->GetMetaID();
	}

	///////////////////////////////////////////////////////////////////

	ibRegisterType GetRegisterType() const {
		return m_propertyRegisterType->GetValueAsEnum();
	}

	wxString GetRegisterTableNameDB(ibRegisterType rType) const {
		wxString className = GetClassName();
		wxASSERT(m_metaId != 0);

		if (rType == ibRegisterType::eBalances) {
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

	bool CreateAndUpdateBalancesTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);
	bool CreateAndUpdateTurnoverTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	///////////////////////////////////////////////////////////////////

	ibValueMetaObjectAccumulationRegister();
	virtual ~ibValueMetaObjectAccumulationRegister();

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

		if (GetRegisterType() == ibRegisterType::eBalances) {
			array = {
				m_propertyAttributeLineActive->GetMetaObject(),
				m_propertyAttributePeriod->GetMetaObject(),
				m_propertyAttributibRecordType->GetMetaObject(),
				m_propertyAttributeRecorder->GetMetaObject(),
				m_propertyAttributeLineNumber->GetMetaObject()
			};
		}
		else {
			array = { m_propertyAttributeLineActive->GetMetaObject(),
				m_propertyAttributePeriod->GetMetaObject(),
				m_propertyAttributeRecorder->GetMetaObject(),
				m_propertyAttributeLineNumber->GetMetaObject()
			};
		}

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
				prop->AppendItem(
					object->GetName(),
					object->GetMetaID(),
					object->GetIcon(),
					object
				);
			}
		}

		return true;
	}

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyModuleObject = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyModuleManager = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectAccumulationRegister::FillFormList);
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyEnum<ibValueEnumAccumulationRegisterType>* m_propertyRegisterType = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumAccumulationRegisterType>>(m_categoryData, wxT("RegisterType"), _("Register type"), ibRegisterType::eBalances);

	ibPropertyInnerAttribute<>* m_propertyAttributibRecordType = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("RecordType"), _("Record type"), wxEmptyString, g_enumRecordTypeCLSID, false, ibValueEnumAccumulationRegisterRecordType::CreateDefEnumValue()));

	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectAccumulationRegister : public ibValueRecordSetObject {
public:
	ibValueRecordSetObjectAccumulationRegister(ibValueMetaObjectAccumulationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey)
	{
	}

	ibValueRecordSetObjectAccumulationRegister(const ibValueRecordSetObjectAccumulationRegister& source) :
		ibValueRecordSetObject(source)
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
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
};

#endif 