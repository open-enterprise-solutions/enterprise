#ifndef __ACCOUNTING_REGISTER_H__
#define __ACCOUNTING_REGISTER_H__

#include "commonObject.h"
#include "accountingRegisterEnum.h"
#include "backend/propertyManager/property/propertyChartOfAccounts.h"

class ibValueMetaObjectAccountingRegister : public ibValueMetaObjectRegisterData {
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

public:

	// Predefined attribute accessors
	ibValueMetaObjectAttributePredefined* GetRegisterRecordType() const {
		return m_propertyAttributeRecordType->GetMetaObject();
	}

	ibValueMetaObjectAttributePredefined* GetRegisterAccount() const {
		return m_propertyAttributeAccount->GetMetaObject();
	}

	// HOW MANY dimension slots this register currently has — the number the chart of accounts
	// declares, not a constant of the implementation.
	unsigned int GetAccountDimensionCount() const { return m_accountDimensionCount; }

	// One line = a whole posting (both accounts named) rather than one side of one.
	bool IsCorrespondence() const { return m_propertyCorrespondence->GetValueAsBoolean(); }

	// A SLOT IS A PAIR, and the two halves take different types.
	//
	//   Kind  — a reference to an ELEMENT of the chart of characteristic types ("Contractor").
	//           It says which breakdown this figure is filed under, and it is what a reading
	//           filters by.
	//   Value — the characteristic's VALUE, typed by the chart's own composition ("OOO Romashka").
	//
	// The kind is STORED beside the value rather than looked up from the account's kinds table by
	// position: an old row then still says what its value was a kind OF, so re-ordering an
	// account's kinds cannot silently change the meaning of data already written — and a reading
	// needs no join per slot per row.
	ibValueMetaObjectAttributePredefined* GetRegisterAccountDimension(unsigned int idx) const {
		return idx < m_accountDimensionCount ? m_accountDimensionSlots[idx] : nullptr;
	}

	ibValueMetaObjectAttributePredefined* GetRegisterAccountDimensionKind(unsigned int idx) const {
		return idx < m_accountDimensionCount ? m_accountDimensionKinds[idx] : nullptr;
	}

	// Bring the slot set in line with the chart of accounts. Slots are created once and REUSED:
	// a metaID is the physical column name (fld<metaID>), so a slot that came back with a fresh id
	// would be a different column and the data in the old one unreachable. Lowering the count
	// therefore deactivates from the tail rather than destroying, and raising it again finds the
	// very same slots waiting. Growth is append-only for the same reason.
	void SyncAccountDimensionSlots();

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

	// Additive contract — RegisterData base has no predefined attrs of
	// its own (returns false with empty array); AccountingRegister
	// provides the full posting-line attribute set.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRegisterData::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeLineActive->GetMetaObject());
		array.push_back(m_propertyAttributePeriod->GetMetaObject());
		// WHAT A LINE IS MADE OF depends on whether it is one side or a whole posting.
		//
		// One-sided: RecordType says which side this row is, and there is a single Account.
		// Correspondence: the row names BOTH accounts and needs no side flag — which side a figure
		// belongs to is said by which account it sits against.
		if (IsCorrespondence()) {
			array.push_back(m_propertyAttributeAccount->GetMetaObject());   // the DEBIT account
			if (m_accountCr != nullptr)
				array.push_back(m_accountCr);
		}
		else {
			array.push_back(m_propertyAttributeRecordType->GetMetaObject());
			array.push_back(m_propertyAttributeAccount->GetMetaObject());
		}
		// Only the ACTIVE slots are part of the object — that is what makes the count a schema
		// decision: a slot outside it contributes no column, so lowering the number drops one.
		// Kind first, then value: the pair reads in the order it is filled in.
		for (unsigned int idx = 0; idx < m_accountDimensionCount; idx++) {
			array.push_back(m_accountDimensionKinds[idx]);
			array.push_back(m_accountDimensionSlots[idx]);
		}

		// The credit side exists only in correspondence mode, and then it is the same shape again.
		for (unsigned int idx = 0; idx < m_accountDimensionKindsCr.size(); idx++) {
			array.push_back(m_accountDimensionKindsCr[idx]);
			array.push_back(m_accountDimensionSlotsCr[idx]);
		}
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

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectAccountingRegister::FillFormList);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Chart of Accounts binding — determines the type of Account field
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	// CORRESPONDENCE — whether a line is one SIDE or a whole POSTING.
	//
	// Off: the line carries RecordType (Debit/Credit) and one Account; a posting is two rows under
	// one recorder. On: the line carries AccountDr and AccountCr with one amount, and the dimension
	// slots double — because the two sides have independent analytical breakdowns.
	//
	// It is a property rather than a preference: a chessboard and correspondence turnovers cannot be
	// expressed at all by a line that names only one side, and no reading can recover the pairing
	// afterwards — a join over the recorder produces every debit against every credit.
	ibPropertyBoolean* m_propertyCorrespondence = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("Correspondence"), _("Correspondence"), false);

	ibPropertyChartOfAccounts* m_propertyChartOfAccounts = ibPropertyObject::CreateProperty<ibPropertyChartOfAccounts>(m_categoryData, wxT("ChartOfAccounts"), _("Chart of accounts"));

	// Predefined attributes: RecordType (Debit/Credit)
	ibPropertyContainer<>* m_propertyAttributeRecordType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateSpecialType(wxT("RecordType"), _("Record type"), wxEmptyString, g_enumAccountingRecordTypeCLSID, false, ibValueEnumAccountingRegisterRecordType::CreateDefEnumValue()));

	// Predefined attribute: Account (reference to Chart of Accounts - polymorphic)
	ibPropertyContainer<>* m_propertyAttributeAccount = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Account"), _("Account"), wxEmptyString, false, ibItemMode::ibItemMode_Item));

	// THE DIMENSION SLOTS — created by SyncAccountDimensionSlots, not declared here.
	//
	// The vector holds every slot ever created; m_accountDimensionCount says how many of them are
	// currently part of the register. The two differ after the chart of accounts lowers its number:
	// the surplus stays alive (its id belongs to its column for good) and simply stops being
	// contributed, so the column drops at the next restructuring and comes back untouched if the
	// number is raised again.
	// Parallel by construction: index i is one slot, its kind and its value.
	//
	// ARITHMETIC, so nobody is surprised at restructuring: one dimension is TWO columns (kind +
	// value). Six dimensions are twelve columns on a side; with correspondence on there are two
	// sides, so twenty-four. The value half is itself composite (a type tag plus one column per
	// admissible type in the contour), which multiplies the physical count again.
	std::vector<ibValueMetaObjectAttributePredefined*> m_accountDimensionKinds;
	std::vector<ibValueMetaObjectAttributePredefined*> m_accountDimensionSlots;

	// The CREDIT account — created only in correspondence mode, beside the inherited Account, which
	// then means the debit one. Not a fixed member for the same reason the slots are not: whether
	// it exists at all is a declaration, and its column has to appear and disappear with it.
	ibValueMetaObjectAttributePredefined* m_accountCr = nullptr;

	// The CREDIT side — populated only in correspondence mode, where one line names both accounts
	// and therefore carries two independent analytical breakdowns.
	std::vector<ibValueMetaObjectAttributePredefined*> m_accountDimensionKindsCr;
	std::vector<ibValueMetaObjectAttributePredefined*> m_accountDimensionSlotsCr;

	unsigned int m_accountDimensionCount = 0;

	// Predefined attributes: the account dimension VALUE slots.
	//
	// A slot is a POSITION, not a thing — the same slot holds an item on one account and a
	// counterparty on another — so the name is numbered and claims nothing more. What gives a
	// slot meaning is the KIND standing beside it, which lives in data (a row of the account's
	// AccountDimensionKinds table).
	//
	// The TYPE of a slot is the chart of characteristic types' own composition — everything a
	// characteristic may ever hold. NOT a reference to an element of that chart: an element IS a
	// kind, and a kind is what the neighbouring column carries. Storing one where the other
	// belongs would let "Contractors" be written where "OOO Romashka" is meant.
	//
	// Their NUMBER is declared by the chart of ACCOUNTS and is therefore schema: changing it is
	// an ordinary restructuring.

	friend class ibValueRecordSetObjectAccountingRegister;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectAccountingRegister : public ibValueRecordSetObject {
	public:
	ibValueRecordSetObjectAccountingRegister(const ibValueMetaObjectAccountingRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey) { m_members.Bind(this, &ibValueRecordSetObjectAccountingRegister::FillMembers); }

	ibValueRecordSetObjectAccountingRegister(const ibValueRecordSetObjectAccountingRegister& source) :
		ibValueRecordSetObject(source) { m_members.Bind(this, &ibValueRecordSetObjectAccountingRegister::FillMembers); }

	// WriteRecordSet / DeleteRecordSet inherited from
	// ibValueRecordSetObject (Phase B template-method).


	void FillMembers(ibMemberTable& helper) const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
};

#endif
