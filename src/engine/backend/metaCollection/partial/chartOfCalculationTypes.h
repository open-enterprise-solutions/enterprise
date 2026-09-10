#ifndef __CHART_OF_CALCULATION_TYPES_H__
#define __CHART_OF_CALCULATION_TYPES_H__

#include "commonObject.h"
#include "reference/reference.h"
#include "chartOfCalculationTypesEnum.h"
#include "chartOfCalculationTypesRelationTable.h"
#include "backend/propertyManager/property/propertyChartOfCalculationTypes.h"

#include <array>
#include <map>
#include <utility>
#include <vector>

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class ibValueMetaObjectChartOfCalculationTypes :
	public ibValueMetaObjectRecordDataHierarchyMutableRef {
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

	// ⭐ HOW THIS CHART'S TYPES ARE COMPUTED — three answers of the chart, each with the reader it had
	// been written for and never had (two of them were saved and loaded, and nothing asked):
	//   UseActionPeriod — the types are in force over DAYS, not only registered in a month. A register on
	//                     this chart may keep action periods only if this says so (it refuses to save
	//                     otherwise), which is also what gives the Displacing relation a reader.
	//   BaseDependence  — which period of a base record puts it into a base (GetBase).
	//   BaseCharts      — the other charts whose types the Base and Leading sections may name; this
	//                     chart's own types always may (TypeBaseAndLeading).
	// The sections themselves stay whatever these say: a section's table is not switched on and off with
	// a flag, since that would drop its rows with it — a flag decides what the rows MEAN, not whether they exist.
	bool IsUseActionPeriod() const { return m_propertyUseActionPeriod->GetValueAsBoolean(); }
	ibBaseDependence GetBaseDependence() const { return m_propertyBaseDependence->GetValueAsEnum(); }
	const ibMetaDescription& GetBaseCharts() const { return m_propertyBaseCharts->GetValueAsMetaDesc(); }

	// THE THREE RELATIONS, each a predefined section on every calculation type — the same shape a chart
	// of accounts uses for its analytics kinds. One row is one edge from the type that owns the row to
	// the type the row names:
	//   Displacing — "this type is displaced by the named one". The record-set write reads these edges
	//                AS THEY ARE (ibComputeActionPeriodDisplacementByRelation) — no rank is folded out of
	//                them, because a rank turns this partial relation into a total one.
	//   Base       — "the named type's results are part of this type's base" (GetBase).
	//   Leading    — "a change to the named type's records makes this type's records stale"
	//                (the recalculations).
	ibValueMetaObjectCalculationTypeRelationTable* GetDisplacingTable() const { return m_propertyDisplacingTable->GetMetaObject(); }
	ibValueMetaObjectCalculationTypeRelationTable* GetBaseTable() const { return m_propertyBaseTable->GetMetaObject(); }
	ibValueMetaObjectCalculationTypeRelationTable* GetLeadingTable() const { return m_propertyLeadingTable->GetMetaObject(); }

	// ONE READING FOR ALL THREE: every edge of `table` as {owner, named} ordinals into `typeIndex`, which
	// numbers each calculation type the first time it is met (and may already hold types from an
	// earlier call — the index is shared, so two relations read into one index compare by ordinal).
	// Not filtered by the reader's rights: a relation is a property of the CHART, and one that narrows
	// per user is a payroll that computes differently depending on who asks. (A type's ordinal in the
	// index is asked of calculation.h — ibCalcTypeOrdinal.)
	void ReadRelation(const ibValueMetaObjectCalculationTypeRelationTable* table,
		std::map<ibValue, int>& typeIndex, std::vector<std::pair<int, int>>& edges) const;

	//default constructor
	ibValueMetaObjectChartOfCalculationTypes();
	virtual ~ibValueMetaObjectChartOfCalculationTypes();

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

protected:

	//get metadata
	virtual const ibMetaData* GetMetaData() const { return m_metaData; }
	virtual ibMetaData* GetMetaData() { return m_metaData; }

	//searched array
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = {
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
		};
		return true;
	}

	virtual bool FillArrayObjectByPredefinedTable(
		std::vector<ibValueMetaObjectTableData*>& array) const {
		array.clear();
		for (ibValueMetaObjectCalculationTypeRelationTable* table : GetRelationTables())
			if (table->IsEnabled())   // a section never saved is not a table yet — see StampIfNeverSaved
				array.push_back(table);
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

	// The three sections, for the doors that treat them alike (the life cycle, the typing of their
	// column, the table list). Each door used to name the one section there was; a list named once is
	// what keeps the second and third from being forgotten at one of ten doors.
	std::array<ibValueMetaObjectCalculationTypeRelationTable*, 3> GetRelationTables() const {
		return { m_propertyDisplacingTable->GetMetaObject(), m_propertyBaseTable->GetMetaObject(), m_propertyLeadingTable->GetMetaObject() };
	}

	// The types the Base and Leading sections may name: this chart's own and those of every chart
	// BaseCharts lists. Asked at run, after every chart has registered its reference type, and again
	// when the list is edited — one method, so the two moments cannot type the sections differently.
	void TypeBaseAndLeading();
	std::array<ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>*, 3> GetRelationProperties() const {
		return { m_propertyDisplacingTable, m_propertyBaseTable, m_propertyLeadingTable };
	}

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

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyBoolean* m_propertyUseActionPeriod = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseActionPeriod"), _("Use action period"), false);
	ibPropertyEnum<ibValueEnumBaseDependence>* m_propertyBaseDependence = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumBaseDependence>>(m_categoryData, wxT("BaseDependence"), _("Base dependence"), ibBaseDependence::eBaseNone);
	ibPropertyChartOfCalculationTypes* m_propertyBaseCharts = ibPropertyObject::CreateProperty<ibPropertyChartOfCalculationTypes>(m_categoryData, wxT("BaseCharts"), _("Base charts of calculation types"), ibPropertyChoiceMode::Mult);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Predefined tabular sections "Displacing", "Base", "Leading" — one meta class, named by the property.
	// Created manually because ibPropertyContainer template can't pass args to non-default constructor via wxClassInfo
	ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>* m_propertyDisplacingTable =
		ibPropertyObject::CreateProperty<ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>>(m_categoryData, wxT("Displacing"), _("Displacing calculation types"));
	ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>* m_propertyBaseTable =
		ibPropertyObject::CreateProperty<ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>>(m_categoryData, wxT("Base"), _("Base calculation types"));
	ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>* m_propertyLeadingTable =
		ibPropertyObject::CreateProperty<ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>>(m_categoryData, wxT("Leading"), _("Leading calculation types"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));

	ibPropertyList* m_propertyDefFormObject = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormObject"), _("Default Object Form"), &ibValueMetaObjectChartOfCalculationTypes::FillFormObject);
	ibPropertyList* m_propertyDefFormFolder = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolder"), _("Default Folder Form"), &ibValueMetaObjectChartOfCalculationTypes::FillFormFolder);
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectChartOfCalculationTypes::FillFormList);
	ibPropertyList* m_propertyDefFormSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormSelect"), _("Default Select Form"), &ibValueMetaObjectChartOfCalculationTypes::FillFormSelect);
	ibPropertyList* m_propertyDefFormFolderSelect = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormFolderSelect"), _("Default Folder Select Form"), &ibValueMetaObjectChartOfCalculationTypes::FillFormFolderSelect);

	friend class ibValueRecordDataObjectChartOfCalculationTypes;
	friend class ibMetaData;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordDataObjectChartOfCalculationTypes : public ibValueRecordDataObjectHierarchyRef {
	public:
	ibValueRecordDataObjectChartOfCalculationTypes(const ibValueMetaObjectChartOfCalculationTypes* metaObject, const ibGuid& objGuid = wxNullGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
	ibValueRecordDataObjectChartOfCalculationTypes(const ibValueRecordDataObjectChartOfCalculationTypes& source);
public:

	//****************************************************************************
	//*                              Support id's                                *
	//****************************************************************************

	// SaveModify / FillObject / CopyObject / WriteObject / DeleteObject
	// inherited from ibValueRecordDataObjectHierarchyRef and
	// ibValueRecordDataObjectRef.

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	// Own methods (data members come from the base FillDataMembers); bound in the ctor.
	void FillMethods(ibMemberTable& helper) const;

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	// ShowFormValue / GetFormValue inherited from HierarchyRef.
protected:
	virtual ibFormID GetCurrentObjectFormID() const override {
		return m_objMode == ibObjectMode::OBJECT_ITEM
			? ibValueMetaObjectChartOfCalculationTypes::eFormObject
			: ibValueMetaObjectChartOfCalculationTypes::eFormFolder;
	}
public:

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

protected:
	friend class ibValue;
	friend class ibValueMetaObjectChartOfCalculationTypes;
};

#endif
