#ifndef __INFORMATION_REGISTER_H__
#define __INFORMATION_REGISTER_H__

#include "commonObject.h"
#include "informationRegisterEnum.h"
#include "backend/query/queryable.h"   // ibComputedRegisterQueryable<TReg> — shared base for the slice / balance / turnover virtual tables

class ibValueMetaObjectInformationRegister : public ibValueMetaObjectRegisterData {
	public:
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
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	//create associate value 
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetRecordForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey) const;
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey) const;
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

	// Additive contract — IR's predefined attribute set depends on
	// WriteRegisterMode + Periodicity (some IR variants are just (key,
	// value) maps with no period/recorder). Base RegisterData is empty,
	// so the chain call is a no-op; kept for consistency with the
	// additive convention.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRegisterData::FillArrayObjectByPredefinedAttribute(array);

		if (GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder)
			array.emplace_back(m_propertyAttributeLineActive->GetMetaObject());

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
	virtual bool FillArrayObjectByDimension(
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
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create record set
	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const;
	virtual ibValueRecordManagerObject* CreateRecordManagerObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormRecord = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormRecord"), _("Default Record Form"), &ibValueMetaObjectInformationRegister::FillFormRecord);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectInformationRegister::FillFormList);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyEnum<ibValueEnumPeriodicity>* m_propertyPeriodicity = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumPeriodicity>>(m_categoryData, wxT("Periodicity"), _("Periodicity"), ibPeriodicity::eNonPeriodic);
	ibPropertyEnum<ibValueEnumWriteRegisterMode>* m_propertyWriteMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumWriteRegisterMode>>(m_categoryData, wxT("WriteMode"), _("Write mode"), ibWriteRegisterMode::eIndependent);

	// THE one place the period-slice is computed — the register's OWN SQL knowledge
	// (table / dimensions / the MAX/MIN self-join). The slice companion queryable
	// (ibSliceQueryable, a friend) calls it through m_reg; the period bound
	// parameterises it: last = MAX / "<=", first = MIN / ">=". Returns the slice table.
	ibQueryRamTable ComputeSlice(const ibValue& cPeriod, const ibValue& cFilter,
	                             const wxString& aggregateFn, const wxString& compareOp) const;

	friend class ibSliceQueryable;
	friend class ibValueRecordSetObjectInformationRegister;
	friend class ibValueRecordManagerObjectInformationRegister;

	friend class ibMetaData;
};

//********************************************************************************************
//*    Slice companion queryables — call-scoped relations handed to L3 via From()             *
//********************************************************************************************
// A slice is a SELF-CONTAINED, call-scoped relation: its own filters — the as-of
// PERIOD and the dimension FILTER — ride in the CONSTRUCTOR ("the filter before the
// Where"). You construct one, hand it to From(), and L3 reads it like any source
// (filter further, join it) — it never learns the rows are computed in RAM. The slice
// returns the register's REAL records, so the eight navigation methods forward to the
// register; the only degree of freedom is the period bound (last = MAX / "<=", first =
// MIN / ">="), fixed by the two derived types. The compute itself is the register's own
// ComputeSlice. A slice does NOT persist on the register — it lives for the one call
// that built it. See docs/query-language-arc.md §22.4d.

// base — shared slice logic; abstract (the period bound is the derived's job). The
// RAM-virtual-table plumbing + the register-forwarding navigation live in the shared
// ibComputedRegisterQueryable base; the slice adds the period bound + ComputeRows.
class BACKEND_API ibSliceQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectInformationRegister> {
public:
	ibSliceQueryable(const ibValueMetaObjectInformationRegister* reg,
	                 const ibValue& period = ibValue(), const ibValue& filter = ibValue())
		: ibComputedRegisterQueryable(reg), m_period(period), m_filter(filter) {}

	virtual wxString AggregateFn() const = 0;   // "MAX" (last) / "MIN" (first)
	virtual wxString CompareOp()   const = 0;   // "<="  (last) / ">="  (first)

	// the slice's rows — computed from the ctor filters through the register's ComputeSlice.
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

protected:
	ibValue m_period;   // as-of date (the "filter before Where")
	ibValue m_filter;   // dimension-name -> value structure
};

// last slice — most recent record on or before the date.
class BACKEND_API ibSliceLastQueryable : public ibSliceQueryable {
public:
	ibSliceLastQueryable(const ibValueMetaObjectInformationRegister* reg,
	                     const ibValue& period = ibValue(), const ibValue& filter = ibValue())
		: ibSliceQueryable(reg, period, filter) {}
	virtual wxString AggregateFn() const override { return wxT("MAX"); }
	virtual wxString CompareOp()   const override { return wxT("<="); }
};

// first slice — earliest record on or after the date.
class BACKEND_API ibSliceFirstQueryable : public ibSliceQueryable {
public:
	ibSliceFirstQueryable(const ibValueMetaObjectInformationRegister* reg,
	                      const ibValue& period = ibValue(), const ibValue& filter = ibValue())
		: ibSliceQueryable(reg, period, filter) {}
	virtual wxString AggregateFn() const override { return wxT("MIN"); }
	virtual wxString CompareOp()   const override { return wxT(">="); }
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectInformationRegister : public ibValueRecordSetObject {
	public:
	ibValueRecordSetObjectInformationRegister(const ibValueMetaObjectInformationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey) {
		m_members.Bind(this, &ibValueRecordSetObjectInformationRegister::FillMembers);
	}
	ibValueRecordSetObjectInformationRegister(const ibValueRecordSetObjectInformationRegister& source) :
		ibValueRecordSetObject(source) {
		m_members.Bind(this, &ibValueRecordSetObjectInformationRegister::FillMembers);
	}
public:

	//default methods
	virtual ibValueRecordSetObject* CopyRegisterValue() {
		return new ibValueRecordSetObjectInformationRegister(*this);
	}

	// WriteRecordSet / DeleteRecordSet inherited from
	// ibValueRecordSetObject (Phase B template-method).

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;

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
	ibValueRecordManagerObjectInformationRegister(const ibValueMetaObjectInformationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordManagerObject(metaObject, uniqueKey)
	{
		m_members.Bind(this, &ibValueRecordManagerObjectInformationRegister::FillMembers);
	}
	ibValueRecordManagerObjectInformationRegister(const ibValueRecordManagerObjectInformationRegister& source) :
		ibValueRecordManagerObject(source)
	{
		m_members.Bind(this, &ibValueRecordManagerObjectInformationRegister::FillMembers);
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

	void FillMembers(ibMemberTable& helper) const;

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