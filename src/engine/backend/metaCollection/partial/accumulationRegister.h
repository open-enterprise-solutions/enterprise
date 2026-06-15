#ifndef __ACCUMULATION_REGISTER_H__
#define __ACCUMULATION_REGISTER_H__

#include "commonObject.h"
#include "accumulationRegisterEnum.h"
#include "backend/query/queryable.h"   // ibComputedRegisterQueryable<TReg> — shared base for the balance / turnover virtual tables

#include <memory>

class ibValueMetaObjectAccumulationRegister;
class ibBalanceQueryable;
class ibTurnoverQueryable;

// L4 virtual-table source descriptors for the accumulation register — balances (as-of period +
// dimension filter) and turnovers (begin/end range + filter). Owned by the register as fields;
// registered under "<Register>.Balance" / ".Turnovers". CreateQueryable BUILDS the call-scoped
// companion from the params and OWNS it. Method bodies are inline at the BOTTOM of this header
// (where the companions are complete).
class ibAccumRegisterBalanceDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibAccumRegisterBalanceDescriptor(ibValueMetaObjectAccumulationRegister* reg) : m_reg(reg) {}
	~ibAccumRegisterBalanceDescriptor() override;
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
private:
	ibValueMetaObjectAccumulationRegister* m_reg;
	std::unique_ptr<ibBalanceQueryable>    m_companion;
};

class ibAccumRegisterTurnoverDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibAccumRegisterTurnoverDescriptor(ibValueMetaObjectAccumulationRegister* reg) : m_reg(reg) {}
	~ibAccumRegisterTurnoverDescriptor() override;
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
private:
	ibValueMetaObjectAccumulationRegister* m_reg;
	std::unique_ptr<ibTurnoverQueryable>   m_companion;
};

class ibValueMetaObjectAccumulationRegister : public ibValueMetaObjectRegisterData {
	public:
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

	// Balance / turnover compute — the register's OWN aggregate-query knowledge (the
	// signed SUM over the movement table, built as L2 IR). The balance / turnover
	// companion queryables (friends) call these through m_reg; each returns a RAM table
	// the L3 door reads. Mirrors the information register's ComputeSlice. Period bound:
	// balance = as-of "<="; turnover = [begin, end].
	ibQueryRamTable ComputeBalance(const ibValue& cPeriod, const ibValue& cFilter) const;
	ibQueryRamTable ComputeTurnover(const ibValue& cBegin, const ibValue& cEnd, const ibValue& cFilter) const;

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
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	//create associate value 
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey) const;
#pragma endregion

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	// Additive contract — RegisterData base is empty. AccumulationRegister
	// appends its line attributes; Balances mode adds the RecordType
	// (Debit / Credit) flag, Turnovers mode omits it.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRegisterData::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeLineActive->GetMetaObject());
		array.push_back(m_propertyAttributePeriod->GetMetaObject());
		if (GetRegisterType() == ibRegisterType::eBalances)
			array.push_back(m_propertyAttributibRecordType->GetMetaObject());
		array.push_back(m_propertyAttributeRecorder->GetMetaObject());
		array.push_back(m_propertyAttributeLineNumber->GetMetaObject());
		return true;
	}

	//get dimension keys 
	virtual bool FillArrayObjectByDimension(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyAttributeRecorder->GetMetaObject() };
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create record set
	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectAccumulationRegister::FillFormList);
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyEnum<ibValueEnumAccumulationRegisterType>* m_propertyRegisterType = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumAccumulationRegisterType>>(m_categoryData, wxT("RegisterType"), _("Register type"), ibRegisterType::eBalances);

	ibPropertyContainer<>* m_propertyAttributibRecordType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("RecordType"), _("Record type"), wxEmptyString, g_enumRecordTypeCLSID, false, ibValueEnumAccumulationRegisterRecordType::CreateDefEnumValue()));

	friend class ibBalanceQueryable;
	friend class ibTurnoverQueryable;

	friend class ibMetaData;

	// L4 custom virtual-table descriptors — registered alongside the base records descriptor on
	// run, dropped on close. Reached as AccumulationRegister.<Name>.Balance / .Turnovers.
	ibAccumRegisterBalanceDescriptor  m_balance { this };
	ibAccumRegisterTurnoverDescriptor m_turnover{ this };
};

//********************************************************************************************
//*  Balance / turnover companion queryables — call-scoped RAM virtual tables                *
//********************************************************************************************
// Mirrors the information-register slice (see ibSliceQueryable): the as-of PERIOD
// (balance) or the [begin, end] RANGE (turnover) plus the dimension FILTER ride in the
// CONSTRUCTOR. You hand one to From() and L3 reads it like any source — it never learns
// the rows are computed in RAM. The compute itself is the register's own ComputeBalance /
// ComputeTurnover; the RAM-virtual-table plumbing + the register-forwarding navigation
// live in the shared ibComputedRegisterQueryable base. Call-scoped — not persisted.

// balance — resource balances as of a date.
class BACKEND_API ibBalanceQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectAccumulationRegister> {
public:
	ibBalanceQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                   const ibValue& period = ibValue(), const ibValue& filter = ibValue())
		: ibComputedRegisterQueryable(reg), m_period(period), m_filter(filter) {}
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue m_period;   // as-of date
	ibValue m_filter;   // dimension-name -> value structure
};

// turnover — resource turnovers (and receipts / expenses) over [begin, end].
class BACKEND_API ibTurnoverQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectAccumulationRegister> {
public:
	ibTurnoverQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                    const ibValue& begin = ibValue(), const ibValue& end = ibValue(),
	                    const ibValue& filter = ibValue())
		: ibComputedRegisterQueryable(reg), m_begin(begin), m_end(end), m_filter(filter) {}
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue m_begin;
	ibValue m_end;
	ibValue m_filter;
};

// --- L4 descriptor method bodies (the register + balance / turnover companions are complete) ---

inline ibAccumRegisterBalanceDescriptor::~ibAccumRegisterBalanceDescriptor() = default;

inline wxString ibAccumRegisterBalanceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_reg->GetClassType());
}

inline wxString ibAccumRegisterBalanceDescriptor::GetName() const
{
	return m_reg->GetName() + wxT(".Balance");
}

inline const ibBackendQueryable* ibAccumRegisterBalanceDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	const ibValue period = (lSizeArray > 0 && paParams != nullptr && paParams[0] != nullptr) ? *paParams[0] : ibValue();
	const ibValue filter = (lSizeArray > 1 && paParams != nullptr && paParams[1] != nullptr) ? *paParams[1] : ibValue();
	m_companion = std::make_unique<ibBalanceQueryable>(m_reg, period, filter);
	return m_companion.get();
}

inline ibAccumRegisterTurnoverDescriptor::~ibAccumRegisterTurnoverDescriptor() = default;

inline wxString ibAccumRegisterTurnoverDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_reg->GetClassType());
}

inline wxString ibAccumRegisterTurnoverDescriptor::GetName() const
{
	return m_reg->GetName() + wxT(".Turnovers");
}

inline const ibBackendQueryable* ibAccumRegisterTurnoverDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	const ibValue begin  = (lSizeArray > 0 && paParams != nullptr && paParams[0] != nullptr) ? *paParams[0] : ibValue();
	const ibValue end    = (lSizeArray > 1 && paParams != nullptr && paParams[1] != nullptr) ? *paParams[1] : ibValue();
	const ibValue filter = (lSizeArray > 2 && paParams != nullptr && paParams[2] != nullptr) ? *paParams[2] : ibValue();
	m_companion = std::make_unique<ibTurnoverQueryable>(m_reg, begin, end, filter);
	return m_companion.get();
}

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectAccumulationRegister : public ibValueRecordSetObject {
	public:
	ibValueRecordSetObjectAccumulationRegister(const ibValueMetaObjectAccumulationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey)
	{
		m_members.Bind(this, &ibValueRecordSetObjectAccumulationRegister::FillMembers);
	}

	ibValueRecordSetObjectAccumulationRegister(const ibValueRecordSetObjectAccumulationRegister& source) :
		ibValueRecordSetObject(source)
	{
		m_members.Bind(this, &ibValueRecordSetObjectAccumulationRegister::FillMembers);
	}

	// WriteRecordSet / DeleteRecordSet inherited from
	// ibValueRecordSetObject (Phase B template-method).

	//////////////////////////////////////////////////////////////////////////////

	virtual bool SaveVirtualTable();
	virtual bool DeleteVirtualTable();

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
};

#endif 