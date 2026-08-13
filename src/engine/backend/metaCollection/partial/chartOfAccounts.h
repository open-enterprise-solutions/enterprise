#ifndef __CHART_OF_ACCOUNTS_H__
#define __CHART_OF_ACCOUNTS_H__

#include "commonObject.h"
#include "reference/reference.h"
#include "chartOfAccountsEnum.h"
#include "chartOfAccountsDimensionKindsTable.h"
#include "backend/propertyManager/property/propertyChartOfCharacteristicTypes.h"

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectChartOfAccounts : public ibValueMetaObjectRecordDataHierarchyMutableRef {
	public:
private:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
		ID_METATREE_OPEN_MANAGER = 19001,
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
	ibValueMetaObjectAttributePredefined* GetQuantitative() const { return m_propertyAttributeQuantitative->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetCurrency() const { return m_propertyAttributeCurrency->GetMetaObject(); }
	// HOW MANY account dimension slots a register on this chart builds — SCHEMA, and therefore a
	// property of the chart, not an attribute of an account. A number sitting in a data row cannot
	// decide how many columns a table has; changing this one is an ordinary restructuring.
	// How many kinds a given account actually uses is a different question, answered by the row
	// count of that account's own kinds table.
	unsigned int GetMaxAccountDimensionCount() const { return m_propertyMaxAccountDimensionCount->GetValueAsUInteger(); }

	ibValueMetaObjectAccountDimensionKindsTable* GetAccountDimensionKindsTable() const { return m_propertyAccountDimensionKindsTable->GetMetaObject(); }

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
	wxString GetDataPresentation(const ibValueDataObject* objValue) const;

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
		array.push_back(m_propertyAttributeQuantitative->GetMetaObject());
		array.push_back(m_propertyAttributeCurrency->GetMetaObject());
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
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("ObjectModule"), _("Object module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectChartOfAccounts::FillFormObject);
	ibPropertyList* m_propertyDefFormFolder = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolder"), _("Default Folder Form"), &ibValueMetaObjectChartOfAccounts::FillFormFolder);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectChartOfAccounts::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectChartOfAccounts::FillFormSelect);
	ibPropertyList* m_propertyDefFormFolderSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolderSelect"), _("Default Folder Select Form"), &ibValueMetaObjectChartOfAccounts::FillFormFolderSelect);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Own predefined attributes for Chart of Accounts

	ibPropertyCategory* m_categoryAccounting = ibPropertyObject::CreatePropertyCategory(wxT("Accounting"), _("Accounting"));

	ibPropertyContainer<>* m_propertyAttributeAccountType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccounting,
		// Folder_Item like its neighbours: a folder in a chart of accounts is an account that has
		// children, so it has a side of its own. Leaving it Item-only was an asymmetry against
		// OffBalance / Quantitative / Currency, which are all declared for both.
		ibValueMetaObjectCompositeData::CreateSpecialType(wxT("AccountType"), _("Account type"), wxEmptyString, g_enumAccountTypeCLSID, false, ibValueEnumAccountType::CreateDefEnumValue(), ibItemMode::ibItemMode_Folder_Item));

	ibPropertyContainer<>* m_propertyAttributeOffBalance = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("OffBalance"), _("Off-balance"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	ibPropertyContainer<>* m_propertyAttributeQuantitative = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("Quantitative"), _("Quantitative"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	ibPropertyContainer<>* m_propertyAttributeCurrency = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryAccounting,
		ibValueMetaObjectCompositeData::CreateBoolean(wxT("Currency"), _("Currency accounting"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Chart of Characteristic Types binding — the CONTOUR: which values an account dimension of this
	// chart may ever hold. A KIND (a row of the table below) selects from that contour; it never
	// declares a type of its own.
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyChartOfCharacteristicTypes* m_propertyChartOfCharacteristicTypes = ibPropertyObject::CreateProperty<ibPropertyChartOfCharacteristicTypes>(m_categoryData, wxT("ChartOfCharacteristicTypes"), _("Chart of characteristic types"));

	// The two answers stand side by side on purpose, because they are different questions:
	// the chart above says WHICH VALUES an account dimension may hold, this number says HOW MANY
	// dimension slots exist. Neither is derivable from the other — the same characteristic chart
	// serves charts of accounts with different analytical depth.
	ibPropertyUInteger* m_propertyMaxAccountDimensionCount = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryData,
		wxT("MaxAccountDimensionCount"), _("Max account dimension count"), 3);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Predefined tabular section "AccountDimensionKinds" — own meta class with predefined columns
	// Created manually because ibPropertyContainer template can't pass args to non-default constructor via wxClassInfo
	ibPropertyContainer<ibValueMetaObjectAccountDimensionKindsTable>* m_propertyAccountDimensionKindsTable =
		ibPropertyObject::CreateProperty<ibPropertyContainer<ibValueMetaObjectAccountDimensionKindsTable>>(m_categoryAccounting, wxT("AccountDimensionKinds"), _("Account dimension kinds"));

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
