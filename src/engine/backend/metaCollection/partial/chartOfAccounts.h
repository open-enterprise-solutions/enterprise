#ifndef __CHART_OF_ACCOUNTS_H__
#define __CHART_OF_ACCOUNTS_H__

#include "commonObject.h"
#include "reference/reference.h"
#include "chartOfAccountsEnum.h"
#include "chartOfAccountsDimensionKindsTable.h"
#include "backend/metaCollection/accountingKind/metaAccountingKindObject.h"
#include "backend/propertyManager/property/propertyChartOfCharacteristicTypes.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectChartOfAccounts : public ibValueMetaObjectRecordDataHierarchyMutableRef {
	public:
private:
	enum
	{
		ID_METATREE_EDIT_PREDEFINED = 19002,
	};

	enum
	{
		eFormObject = 1,
		eFormList,
		eFormSelect,
		eFormFolder,
		eFormFolderSelect
	};

	virtual ibFormTypeList GetFormType() const override {
		ibFormTypeList formList;
		formList.AppendItem(wxT("FormObject"), _("Form object"), eFormObject);
		formList.AppendItem(wxT("FormFolder"), _("Form group"), eFormFolder);
		formList.AppendItem(wxT("FormList"), _("Form list"), eFormList);
		formList.AppendItem(wxT("FormSelect"), _("Form select"), eFormSelect);
		formList.AppendItem(wxT("FormGroupSelect"), _("Form group select"), eFormFolderSelect);
		return formList;
	}

public:

	// Own attributes accessors
	ibValueMetaObjectAttributePredefined* GetAccountType() const { return m_propertyAttributeAccountType->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetOffBalance() const { return m_propertyAttributeOffBalance->GetMetaObject(); }
	// HOW MANY account dimension slots a register on this chart builds — SCHEMA, and therefore a
	// property of the chart, not an attribute of an account. A number sitting in a data row cannot
	// decide how many columns a table has; changing this one is an ordinary restructuring.
	// How many kinds a given account actually uses is a different question, answered by the row
	// count of that account's own kinds table.
	unsigned int GetMaxAccountDimensionCount() const { return m_propertyMaxAccountDimensionCount->GetValueAsUInteger(); }

	ibValueMetaObjectAccountDimensionKindsTable* GetAccountDimensionKindsTable() const { return m_propertyAccountDimensionKindsTable->GetMetaObject(); }

	// ⭐⭐ THE KINDS SECTION, UNFOLDED INTO COLUMNS — the LIST needs what the object already has as rows.
	//
	// An account's analytics kinds live in a tabular section, which is right for the card: a section is
	// how a variable number of rows is edited. A LIST cannot show a section — and showing which kinds an
	// account carries is exactly what a chart of accounts list is read for, filter included.
	//
	// Both halves of the unfolding are already known, which is what makes it possible at all: HOW MANY
	// columns there are is `MaxAccountDimensionCount` (schema, declared on the chart), and WHAT each one
	// holds is the same reference type the section's own kind column carries. So column N is row N —
	// the position IS the correspondence, the same rule the register's slots follow.
	unsigned int GetAccountDimensionKindColumnCount() const {
		return static_cast<unsigned int>(m_accountDimensionKindColumns.size());
	}
	ibValueMetaObjectAttributePredefined* GetAccountDimensionKindColumn(unsigned int idx) const {
		return idx < m_accountDimensionKindColumns.size() ? m_accountDimensionKindColumns[idx] : nullptr;
	}

	// Bring the column set in line with the declared ceiling. Slots are created once and REUSED: a
	// metaID is the physical column name (fld<metaID>), so a column that came back with a fresh id
	// would be a different column with the old one's data unreachable.
	void SyncAccountDimensionKindColumns();

	// READ, NEVER WRITTEN — added to what the base already refuses (the object's own reference).
	// The kinds live in the section; these columns are its copy, refreshed from it on every save, so
	// an assignment here would be a second author for one fact and would vanish at the next write.
	virtual bool IsReadOnlyAttribute(const ibMetaID& id) const override {
		if (ibValueMetaObjectRecordDataHierarchyMutableRef::IsReadOnlyAttribute(id))
			return true;
		for (const ibValueMetaObjectAttributePredefined* column : m_accountDimensionKindColumns) {
			if (column != nullptr && column->GetMetaID() == id)
				return true;
		}
		return false;
	}

	// --- the kinds of accounting this chart declares -------------------------------------------------
	//
	// ⭐⭐ TWO LISTS, NEVER ONE. An account is kept in a kind of accounting ("this account is a currency
	// account"); a BREAKDOWN of that account is kept in one too ("the quantity is kept by item"). A
	// register's resource names one of each, so the two can never be offered as a single list.
	//
	// Asked of the chart every time rather than stored: a kind declared, renamed or deleted in the tree
	// is answered for here, and nothing downstream keeps a copy to fall out of step with it.

	std::vector<ibValueMetaObjectAccountingKind*> GetAccountingKindArrayObject(
		std::vector<ibValueMetaObjectAccountingKind*> array = std::vector<ibValueMetaObjectAccountingKind*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectAccountingKind>(array, { g_metaAccountingKindCLSID });
		return array;
	}

	std::vector<ibValueMetaObjectAccountDimensionAccountingKind*> GetAccountDimensionAccountingKindArrayObject(
		std::vector<ibValueMetaObjectAccountDimensionAccountingKind*> array = std::vector<ibValueMetaObjectAccountDimensionAccountingKind*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectAccountDimensionAccountingKind>(array, { g_metaAccountDimensionAccountingKindCLSID });
		return array;
	}

	// The chart HOSTS both — which is what puts them in the tree under branches of their own, beside
	// the attributes and the tabular sections.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaAccountingKindCLSID || clsid == g_metaAccountDimensionAccountingKindCLSID)
			return clsid;
		return ibValueMetaObjectRecordDataHierarchyMutableRef::ResolveChild(clsid);
	}

	// An account's kind is a FIELD OF THE ACCOUNT, so it joins the walk every column is built from.
	// (A breakdown's kind is not here: its column belongs to the dimension-kinds table, which asks for
	// it itself — see ibValueMetaObjectAccountDimensionKindsTable.)
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataHierarchyMutableRef::GetGenericAttributeArrayObject(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAccountingKindCLSID });
		return array;
	}

	// (Nothing to add for the FIND: it matches an attribute by TYPE, and a kind is one — see
	//  ibValueMetaObjectRecordData::FindAnyAttributeObjectByFilter.)

	// -------------------------------------------------------------------------------------------------

	// Chart of Characteristic Types binding (determines the values an account dimension may hold)
	ibPropertyChartOfCharacteristicTypes* GetChartOfCharacteristicTypes() const { return m_propertyChartOfCharacteristicTypes; }

	//default constructor
	ibValueMetaObjectChartOfAccounts();
	virtual ~ibValueMetaObjectChartOfAccounts();

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

	//get attribute code
	virtual ibValueMetaObjectAttributeBase* GetAttributeForCode() const {
		return m_propertyAttributeCode->GetMetaObject();
	}

	//create associate value
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const;

#pragma region _form_builder_h_
	//support form
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetFolderForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
	virtual ibBackendValueForm* GetFolderSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion

	//descriptions...


	//get module object in compose object
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return m_propertyObjectModule->GetMetaObject(); }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return m_propertyManagerModule->GetMetaObject(); }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	// Declares this chart's tables AND attaches the analytics-ceiling rule to the kinds section, so the
	// differ can refuse a lowering the data cannot survive before it touches anything. Body in
	// chartOfAccountsMetadata.cpp.
	virtual void ContributeTables(class ibSchemaSnapshot& out) const override;

	// Give the analytics-kinds column the reference type its binding names. Called on LOAD, on the user's
	// PICK and on SAVE — every point the binding can have arrived — because the schema is computed off
	// these metaobjects and a column typed only at run time made the two disagree.
	void ApplyAccountDimensionKindType();

protected:

	// Additive contract — chains to HierarchyMutableRef. ChartOfAccounts
	// appends accounting-specific attributes (AccountType, OffBalance,
	// Quantitative, Currency, MaxAccountDimensionCount) on top of the inherited
	// Hierarchy + MutableRef set.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataHierarchyMutableRef::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeAccountType->GetMetaObject());
		array.push_back(m_propertyAttributeOffBalance->GetMetaObject());
		// The unfolded kinds — ordinary attributes from here on, which is the whole point: the list
		// shows them, a filter reads them and a query selects them, with nothing taught about sections.
		for (ibValueMetaObjectAttributePredefined* column : m_accountDimensionKindColumns)
			array.push_back(column);
		return true;
	}

	virtual bool FillArrayObjectByPredefinedTable(
		std::vector<ibValueMetaObjectTableData*>& array) const {
		array = { m_propertyAccountDimensionKindsTable->GetMetaObject() };
		return true;
	}

	//searched array
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = {
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
		};
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create empty object
	virtual ibValueRecordDataObjectHierarchyRef* CreateObjectRefValue(ibObjectMode mode, const ibGuid& guid = wxNullGuid) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);
	virtual void ProcessCommand(unsigned int id);

private:

	bool FillFormObject(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormObject == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormFolder(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormFolder == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormList(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormList == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormSelect(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormSelect == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	bool FillFormFolderSelect(ibPropertyList* prop) {
		for (auto object : GetFormArrayObject()) {
			if (!object->IsAllowed()) continue;
			if (eFormFolderSelect == object->GetTypeForm()) {
				prop->AppendItem(object->GetName(), object->GetMetaID(), object->GetIcon(), object);
			}
		}
		return true;
	}

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ObjectModule"), _("Object module"), _("Code of one account: its write handlers (BeforeWrite, OnWrite, SetNewCode...) and the procedures they call. Runs wherever an account is written - a form, a script or a background job."));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"), _("Code of the chart as a whole rather than of one account: its exported procedures and functions are called on the manager, as ChartsOfAccounts.<Name>.<Function>()."));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), _("The form an account opens with. Empty: the form is generated from the account's attributes."), &ibValueMetaObjectChartOfAccounts::FillFormObject);
	ibPropertyList* m_propertyDefFormFolder = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolder"), _("Default Folder Form"), _("The form a folder of accounts opens with. Empty: a generated form."), &ibValueMetaObjectChartOfAccounts::FillFormFolder);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), _("The form the chart's list opens with. Empty: the list form is generated."), &ibValueMetaObjectChartOfAccounts::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), _("The form used to choose an account for a field of this type. Empty: the list form opens in choice mode."), &ibValueMetaObjectChartOfAccounts::FillFormSelect);
	ibPropertyList* m_propertyDefFormFolderSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolderSelect"), _("Default Folder Select Form"), _("The form used to choose a folder - for an account's Parent. Empty: the list form opens showing folders only."), &ibValueMetaObjectChartOfAccounts::FillFormFolderSelect);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Own predefined attributes for Chart of Accounts

	ibPropertyCategory* m_categoryAccounting = ibPropertyObject::CreatePropertyCategory(wxT("Accounting"), _("Accounting"));

	ibPropertyContainer<>* m_propertyAttributeAccountType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccounting,
		// Folder_Item like its neighbours: a folder in a chart of accounts is an account that has
		// children, so it has a side of its own. Leaving it Item-only was an asymmetry against
		// OffBalance / Quantitative / Currency, which are all declared for both.
		// ⭐⭐ FILL-CHECKED. An account with no side is an account nothing can be posted to: every
		// posting asks which side the amount moves, and the answer is this attribute. Left optional it
		// could be saved empty, and the refusal arrived much later — at posting time, about a document,
		// naming an account somebody entered weeks earlier.
		//
		// Through the ATTRIBUTE's own flag rather than a rule written at the write path: fill-check is
		// the mechanism the platform already has for "this must be filled in", it shows the field as
		// required in the form, and a rule of ours would be a second answer to the same question.
		ibValueMetaObjectCompositeData::CreateSpecialType(wxT("AccountType"), _("Account type"), _("The account's side: Active (its balance is a debit), Passive (a credit) or Active/Passive (either, shown as it falls). Required - every posting asks which side an amount moves, and an account without a side takes none."), g_enumAccountTypeCLSID, /*fillCheck*/ true, ibValueEnumAccountType::CreateDefEnumValue(), ibItemMode::ibItemMode_Folder_Item));

	ibPropertyContainer<>* m_propertyAttributeOffBalance = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("OffBalance"), _("Off-balance"), _("An off-balance account is a separate circuit outside the double entry: a posting may name it on one side only, with no correspondent account, and its figures are not summed into the balance that debits and credits must agree on."), ibItemMode::ibItemMode_Folder_Item));

	// (QUANTITATIVE AND CURRENCY WERE DECLARED HERE, as two boolean fields the engine handed every
	//  chart. They are gone: they are exactly what an ACCOUNTING FLAG is, and hardcoding two of them
	//  decided for the author what his accounting keeps track of — while nothing in the engine ever
	//  read either one. A chart declares its own now, by name, and a register's dimension or resource
	//  says which one its figure is kept under. Off-balance stays a field of the metatype: the balance
	//  check reads it, so it is the engine's question and not the author's. Max, 2026-09-16.)

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Chart of Characteristic Types binding — the CONTOUR: which values an account dimension of this
	// chart may ever hold. A KIND (a row of the table below) selects from that contour; it never
	// declares a type of its own.
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));

	// ⭐ THESE TWO BELONG TO ACCOUNTING, NOT TO "DATA". They are what makes this chart an accounting
	// one: which values an account's analytics may hold, and how many slots there are. Sitting under
	// "Data" they were invisible where a reader looks for them — the Accounting section showed the
	// account's own flags and nothing about its analytics, so the section read as if it were the whole
	// story and the two questions that shape every register built on this chart were elsewhere.
	ibPropertyChartOfCharacteristicTypes* m_propertyChartOfCharacteristicTypes = ibPropertyObject::CreateProperty<ibPropertyChartOfCharacteristicTypes>(m_categoryAccounting, wxT("ChartOfCharacteristicTypes"), _("Chart of characteristic types"), _("The chart whose items are the account dimension kinds (the analytics an account may be kept by, such as counterparty or item) and whose types say what values each kind may hold. An account picks its kinds from it in Account dimension kinds."));

	// The two answers stand side by side on purpose, because they are different questions:
	// the chart above says WHICH VALUES an account dimension may hold, this number says HOW MANY
	// dimension slots exist. Neither is derivable from the other — the same characteristic chart
	// serves charts of accounts with different analytical depth.
	ibPropertyUInteger* m_propertyMaxAccountDimensionCount = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryAccounting,
		wxT("MaxAccountDimensionCount"), _("Max account dimension count"), _("How many dimension slots an account of this chart can have (3 by default). An accounting register on the chart gets that many dimension columns on each side; an account uses as many as it lists kinds."), 3);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Predefined tabular section "AccountDimensionKinds" — own meta class with predefined columns
	// Created manually because ibPropertyContainer template can't pass args to non-default constructor via wxClassInfo
	ibPropertyContainer<ibValueMetaObjectAccountDimensionKindsTable>* m_propertyAccountDimensionKindsTable =
		ibPropertyObject::CreateProperty<ibPropertyContainer<ibValueMetaObjectAccountDimensionKindsTable>>(m_categoryAccounting, wxT("AccountDimensionKinds"), _("Account dimension kinds"), _("Each account's analytics: the dimension kinds it is kept by, one row per slot in order - row N fills the register's dimension N. A row may say Summary only: turnovers are kept by that kind, balances are not broken down by it."));

	// The kinds section unfolded — one attribute per position, count declared by MaxAccountDimensionCount.
	// Created by SyncAccountDimensionKindColumns and never destroyed: lowering the ceiling deactivates
	// from the tail, raising it again finds the very same columns (their metaIDs name real DB columns).
	std::vector<ibValueMetaObjectAttributePredefined*> m_accountDimensionKindColumns;

	friend class ibValueRecordDataObjectChartOfAccounts;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectChartOfAccounts : public ibValueRecordDataObjectHierarchyRef {
	public:
	ibValueRecordDataObjectChartOfAccounts(const ibValueMetaObjectChartOfAccounts* metaObject, const ibGuid& objGuid = wxNullGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
	ibValueRecordDataObjectChartOfAccounts(const ibValueRecordDataObjectChartOfAccounts& source);
public:

	// SaveModify / FillObject / CopyObject / WriteObject / DeleteObject
	// inherited from ibValueRecordDataObjectHierarchyRef and
	// ibValueRecordDataObjectRef.

	// NO MORE KINDS THAN THERE ARE SLOTS.
	//
	// A user adds the analytics an account needs — contractor, contract, later order — and that
	// works because the slots were sown in advance. Row N+1 has no slot to be written into, so
	// accepting it would mean accepting a value with nowhere to go.
	//
	// Enforced on the WRITE, not in the form: a limit that lives only in the interface is not a
	// limit — a data processor, an import or a paste writes the same table without passing through
	// it. The form may refuse earlier for comfort; this is what makes it true.
	virtual bool SaveData() override;

	// Own methods (data members come from the base FillDataMembers); bound in the ctor.
	void FillMethods(ibMemberTable& helper) const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	// ShowFormValue / GetFormValue inherited from HierarchyRef.
protected:
	virtual ibFormID GetCurrentObjectFormID() const override {
		return m_objMode == ibObjectMode::OBJECT_ITEM
			? ibValueMetaObjectChartOfAccounts::eFormObject
			: ibValueMetaObjectChartOfAccounts::eFormFolder;
	}
public:

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectChartOfAccounts;
};

#endif
