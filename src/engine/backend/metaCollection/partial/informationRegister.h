#ifndef __INFORMATION_REGISTER_H__
#define __INFORMATION_REGISTER_H__

#include "commonObject.h"
#include "informationRegisterEnum.h"
#include "backend/query/queryable.h"   // ibComputedRegisterQueryable<TReg> — shared base for the slice / balance / turnover virtual tables
// The register-shared lowering: ibRegFilterPredicate / ibRegFlatLeaves / ibRegCompositeIR. An
// information register filters its dimensions by the same rule an accumulation register does, and an
// accounting register will too — so the rule lives in one file for all three.
#include "backend/metaCollection/partial/registerQueryLowering.h"

#include <memory>

class ibValueMetaObjectInformationRegister;
class ibSliceLastQueryable;
class ibSliceFirstQueryable;

// WHICH END OF THE INTERVAL a slice takes — the last record at or before the moment, or the first at
// or after it. The one fact that distinguishes the two slices; everything else (which way the period
// is compared, which end of a key's periods is the boundary) is derived from it, in one place.
enum class ibSliceEnd { Last, First };

// L4 virtual-table source descriptor for the information register's slices — templated on the
// slice companion (ibSliceLastQueryable / ibSliceFirstQueryable). Owned by the register as a
// field; registered under "<Register>.SliceLast" / ".SliceFirst". CreateQueryable BUILDS the
// call-scoped companion from the params (as-of period, dimension filter) and OWNS it. Method
// bodies are at the BOTTOM of this header (where the companions are complete).
template <typename TSlice>
class ibInfoRegisterSliceDescriptor : public ibQueryableSourceDescriptor
{
public:
	ibInfoRegisterSliceDescriptor(ibValueMetaObjectInformationRegister* reg, const wxString& suffix)
		: m_reg(reg), m_suffix(suffix) {}
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	// WHAT COLUMNS A SLICE HAS, without running it. A slice REPORTS the register's own columns —
	// its companion does not redirect navigation, unlike the accumulation register's views — so the
	// answer is the register's, asked once and not restated here. Without this the base answers
	// nothing and the slice stands in a catalogue as a leaf with no fields to select.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
	// Slice(moment, condition). The condition may name ANYTHING the slice carries — unlike a
	// balance, a slice folds nothing: it picks one row per key, and every column of that row is
	// still a column, attributes included. So FillConditionExplorer is left at the base, which
	// answers with the whole source.
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
private:
	ibValueMetaObjectInformationRegister* m_reg;
	wxString                              m_suffix;
	// (no companion member: the base owns what MakeCompanion builds — queryableFactory.h)
};

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

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events: 
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//for designer 
	// Register / unregister `.SliceLast` and `.SliceFirst` from the periodicity as it is NOW. A slice
	// is defined by the period, so a non-periodic register has none — and periodicity is a property
	// that can be switched after the register has run.
	void SyncSliceSources();

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
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

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

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	//get default form
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {

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
	// (table / dimensions / the boundary self-join). The slice companion queryable
	// (ibSliceQueryable, a friend) calls it through m_reg, and ONE parameter says which slice this
	// is: the aggregate and the comparison both follow from it. Returns the slice table.
	ibQueryRamTable ComputeSlice(const ibValue& cPeriod, const ibQueryPredicatePtr& cFilter, ibSliceEnd end) const;

	friend class ibSliceQueryable;
	friend class ibValueRecordSetObjectInformationRegister;
	friend class ibValueRecordManagerObjectInformationRegister;

	friend class ibMetaData;

	// L4 custom virtual-table descriptors — registered alongside the base records descriptor
	// (RegisterData::m_queryable) on run, dropped on close. Reached as Information
	// Register.<Name>.SliceLast / .SliceFirst.
	ibInfoRegisterSliceDescriptor<ibSliceLastQueryable>  m_sliceLast { this, wxT("SliceLast") };
	ibInfoRegisterSliceDescriptor<ibSliceFirstQueryable> m_sliceFirst{ this, wxT("SliceFirst") };
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
	                 const ibValue& period = ibValue(), const ibQueryPredicatePtr& filter = nullptr)
		: ibComputedRegisterQueryable(reg), m_period(period), m_filter(filter) {}

	// ⭐⭐ ONE QUESTION: WHICH END OF THE INTERVAL.
	//
	// A slice is the record nearest a moment — the LAST one at or before it, or the FIRST one at or
	// after it. Everything else follows from that single fact: which way the period is compared, and
	// which end of a key's periods is the boundary.
	//
	// It used to travel as TWO STRINGS handed down separately (`"MAX"`/`"MIN"` and `"<="`/`">="`),
	// re-parsed at the bottom by a chain of string comparisons. Two spellings of one fact: either
	// could be changed without the other, and the chain quietly defaulted to `<=` for anything it did
	// not recognise — so a typo in one of them produced a wrong slice rather than an error.
	virtual ibSliceEnd End() const = 0;

	// ⭐ WHAT MAKES ONE ROW THE SAME ROW — three shapes, and the register says which it is.
	//
	//   subordinate to a recorder : period + line number + recorder,  then dimensions
	//   periodic, independent     : period,                           then dimensions
	//   non-periodic              :                                   dimensions alone
	//
	// 🛑 ASKED, NOT ASSUMED. Periodicity and WriteMode are settings — an information register can
	// be any of these, and the key is a different list in each. Writing one of them flat is how two
	// documents' rows fold into one, or how every moment of a periodic register collapses onto the
	// last: valid SQL, wrong data, nothing to notice.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override
	{
		std::vector<const ibBackendQueryColumn*> cols;

		if (m_reg == nullptr)
			return cols;

		if (m_reg->HasRecorder()) {
			if (m_reg->GetRegisterPeriod()     != nullptr) cols.push_back(m_reg->GetRegisterPeriod()->GetQueryColumn());
			if (m_reg->GetRegisterLineNumber() != nullptr) cols.push_back(m_reg->GetRegisterLineNumber()->GetQueryColumn());
			if (m_reg->GetRegisterRecorder()   != nullptr) cols.push_back(m_reg->GetRegisterRecorder()->GetQueryColumn());
		}
		else if (m_reg->HasPeriod() && m_reg->GetRegisterPeriod() != nullptr) {
			cols.push_back(m_reg->GetRegisterPeriod()->GetQueryColumn());
		}

		for (auto* dimension : m_reg->GetDimensionArrayObject())
			if (dimension != nullptr)
				cols.push_back(dimension->GetQueryColumn());

		return cols;
	}

	// the slice's rows — computed from the ctor filters through the register's ComputeSlice.
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

protected:
	ibValue m_period;   // as-of date (the "filter before Where")
	ibQueryPredicatePtr m_filter;   // the condition, already converted (ibRegFilterPredicate)
};

// last slice — most recent record on or before the date.
class BACKEND_API ibSliceLastQueryable : public ibSliceQueryable {
public:
	ibSliceLastQueryable(const ibValueMetaObjectInformationRegister* reg,
	                     const ibValue& period = ibValue(), const ibQueryPredicatePtr& filter = nullptr)
		: ibSliceQueryable(reg, period, filter) {}
	virtual ibSliceEnd End() const override { return ibSliceEnd::Last; }
};

// first slice — earliest record on or after the date.
class BACKEND_API ibSliceFirstQueryable : public ibSliceQueryable {
public:
	ibSliceFirstQueryable(const ibValueMetaObjectInformationRegister* reg,
	                      const ibValue& period = ibValue(), const ibQueryPredicatePtr& filter = nullptr)
		: ibSliceQueryable(reg, period, filter) {}
	virtual ibSliceEnd End() const override { return ibSliceEnd::First; }
};

// ibInfoRegisterSliceDescriptor — method bodies (the register + slice companions are complete here).
template <typename TSlice>
wxString ibInfoRegisterSliceDescriptor<TSlice>::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_reg->GetClassType());
}

template <typename TSlice>
wxString ibInfoRegisterSliceDescriptor<TSlice>::GetName() const
{
	return m_reg->GetName() + wxT(".") + m_suffix;
}

template <typename TSlice>
const ibBackendQueryable* ibInfoRegisterSliceDescriptor<TSlice>::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	// build the call-scoped slice companion from the params (as-of period, dimension filter) and own it
	const ibValue period = (lSizeArray > 0 && paParams != nullptr && paParams[0] != nullptr) ? *paParams[0] : ibValue();
	// The runtime value becomes the condition right here, at the door — the same converter every
	// register uses, so everything below sees a predicate.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_reg,
		(lSizeArray > 1 && paParams != nullptr && paParams[1] != nullptr) ? *paParams[1] : ibValue());
	// Built and KEPT by the base — the same call gives the same object back, and two slices as of two
	// different dates in one query both stay alive (queryableFactory.h, MakeCompanion).
	return this->template MakeCompanion<TSlice>(paParams, lSizeArray, m_reg, period, filter);
}

template <typename TSlice>
void ibInfoRegisterSliceDescriptor<TSlice>::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_reg != nullptr)
		m_reg->FillSourceExplorer(explorer);
}

template <typename TSlice>
void ibInfoRegisterSliceDescriptor<TSlice>::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	ibQuerySourceParameter moment;
	moment.m_name = wxT("Period");
	moment.m_description = _("The moment the slice is taken at - one row per set of dimensions, "
	                         "carrying the record in force then. Left out, the slice is as of now "
	                         "for a last slice, and as of the register's first record for a first "
	                         "one.");
	if (m_reg != nullptr && m_reg->GetRegisterPeriod() != nullptr)
		moment.m_type = m_reg->GetRegisterPeriod()->GetTypeDesc();
	out.push_back(moment);

	ibQuerySourceParameter condition;
	condition.m_name      = wxT("Condition");
	condition.m_description = _("A condition on the DIMENSIONS, applied while the slice is taken - "
	                            "so it chooses which keys are sliced, not which of the finished "
	                            "rows survive.");
	condition.m_condition = true;
	out.push_back(condition);
}

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
	virtual const ibSourceExplorer* GetSourceExplorer() const;

#pragma region _form_builder_h_
	//support show 
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
#pragma endregion

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectInformationRegister;
};

#endif 