#ifndef __CALCULATION_REGISTER_H__
#define __CALCULATION_REGISTER_H__

#include "commonObject.h"
#include "chartOfCalculationTypesEnum.h"                                          // ibCalcPeriodicity — the register's grain
#include "backend/propertyManager/property/propertyEnum.h"                        // …as a property
#include "backend/propertyManager/property/propertyChartOfCalculationTypes.h"   // the chart binding
#include "backend/query/queryable.h"
// The register-shared lowering: ibRegFilterPredicate / ibRegFlatLeaves / ibRegCompositeIR — a
// calculation register filters its dimensions by the same rule the other registers do, so the rule
// lives in one shared file.
#include "backend/metaCollection/partial/registerQueryLowering.h"
#include "backend/query/tempTableQueryable.h"   // ibSchemaTableQueryable — the actual action periods as a source

#include <map>       // the displacement relation's type index
#include <memory>
#include <utility>   // std::pair — displacement edges
#include <vector>

class ibValueMetaObjectCalculationRegister;
class ibValueMetaObjectChartOfCalculationTypes;
class ibValueMetaObjectResource;
class ibCalcBaseSourceDescriptor;   // the base, as a source — defined below the register

// (What a period of a record MEANS in days — whole days, the end included — and every other rule the
// register's records are computed by, are in backend/calculation/calculation.h.)

// ⭐⭐ THE ACTUAL ACTION PERIOD, AS A SOURCE — `CalculationRegister.<Register>.ActualActionPeriod`.
//
// The register's OWN fields, ONE ROW PER PIECE of what is left of a record's action period after
// displacement, with ActionPeriodStart / ActionPeriodEnd holding the piece's bounds. A salary for the
// 1st–31st with a sick leave on the 10th–20th is in force on the 1st–9th AND the 21st–31st: two actual
// periods of one record, two rows here (Max, 2026-09-10 — the classic register offers exactly this
// virtual table, with no separate "actual" fields at all).
//
// 🛑 IT REPLACED TWO STORED COLUMNS that held one span per record. A record displaced in the middle
// then read back as its whole month again — its bounding span — which is indistinguishable from "not
// displaced". The pieces are stored as whole rows in a derived table the register maintains on every
// write (calculationRegisterObject.cpp), so reading them is a plain scan with no join and no recompute.
class ibCalcActualPeriodSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibCalcActualPeriodSourceDescriptor(ibValueMetaObjectCalculationRegister* meta) : m_meta(meta) {}
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectCalculationRegister* m_meta;
};

class ibValueMetaObjectCalculationRegister : public ibValueMetaObjectRegisterData {
	public:
private:
	// A LIST FORM ONLY. A calculation register holds MOVEMENTS — every record belongs to its recorder and
	// is written as that recorder's set — so there is no record to open on its own, no record form and no
	// record manager (Max, 2026-09-10: "no record manager, because it is a movement"). The value 2 stays
	// what it was, because a saved form carries it.
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

	// THE REGISTER'S OWN SUBORDINATE. A recalculation exists only inside a calculation register — the way
	// a record manager exists only inside an information register — so its class is this one's (Max,
	// 2026-09-10). Defined below this class, once it is complete.
	class ibValueMetaObjectRecalculation;

	ibValueMetaObjectCalculationRegister();
	virtual ~ibValueMetaObjectCalculationRegister();

	// A calculation register additionally owns Recalculation subordinate objects —
	// child tables keyed by (recalc object, dimensions). Everything else (Dimension/Resource/Attribute)
	// stays with the register-data base.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaRecalculationCLSID)
			return clsid;
		return ibValueMetaObjectRegisterData::ResolveChild(clsid);
	}

	// The register answers for its own children, the way it does for dimensions and resources. The
	// import had the designer's tree call FillArrayObjectByFilter directly — it is protected, and for
	// the reason the neighbours show: WHICH children are a recalculation is this class's own fact, and
	// a caller that filters for itself is a second place that has to be kept in step with ResolveChild.
	// Inline below the recalculation: the filter needs it complete, and this class is not exported, so
	// the designer's tree compiles the body itself.
	std::vector<ibValueMetaObjectRecalculation*> GetRecalculationArrayObject(
		std::vector<ibValueMetaObjectRecalculation*> array = std::vector<ibValueMetaObjectRecalculation*>()) const;

	// ⭐ ACTION PERIOD — the semantic heart of a calculation register. When on, a calculation record is
	// not a point event but an INTERVAL [start, end] over which it is in force; records of competing
	// calculation types displace each other over overlapping action periods. When off, the record
	// carries only the registration period (when it was entered). See docs (calculation engine).
	bool IsUseActionPeriod() const { return m_propertyUseActionPeriod->GetValueAsBoolean(); }
	ibValueMetaObjectAttributePredefined* GetActionPeriodStart()   const { return m_propertyAttributeActionPeriodStart->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetActionPeriodEnd()     const { return m_propertyAttributeActionPeriodEnd->GetMetaObject(); }
	// The period the action belongs to (the month a salary is FOR, whatever days of it the record covers).
	ibValueMetaObjectAttributePredefined* GetActionPeriod()        const { return m_propertyAttributeActionPeriod->GetMetaObject(); }

	// ⭐ THE REGISTRATION PERIOD IS THE REGISTER'S ONE PERIOD — the period a record is registered in, held
	// as its first day. There is no `Period` beside it (Max, 2026-09-10: "the registration period, it
	// seems, is the only one needed"): the register family's second-precise Period is the time of a
	// movement, and a calculation record is registered in a period, not at a moment.
	ibValueMetaObjectAttributePredefined* GetRegistrationPeriod()  const { return m_propertyAttributeRegistrationPeriod->GetMetaObject(); }

	// ⭐ …AND THE PERIOD IS OF THE REGISTER'S PERIODICITY — a day, a month (the default), a quarter or a
	// year. The record-set write registers a record at the start of the period its date falls in, the
	// month-for ActionPeriod likewise, so every reader — the storno's pairing, the base by registration
	// period, a report grouping by the month — meets one value per period. (Removed in the night of 2026-09-10 as a
	// property nothing read; back the same day with the write as its reader, at Max's word: "add the
	// periodicity, let it default to month".)
	ibCalcPeriodicity GetPeriodicity() const { return m_propertyPeriodicity->GetValueAsEnum(); }
	ibTotalsPeriod GetPeriodicityUnit() const {
		switch (GetPeriodicity()) {
		case ibCalcPeriodicity::eCalcPeriodDay:     return ibTotalsPeriod::Day;
		case ibCalcPeriodicity::eCalcPeriodQuarter: return ibTotalsPeriod::Quarter;
		case ibCalcPeriodicity::eCalcPeriodYear:    return ibTotalsPeriod::Year;
		default:                                    return ibTotalsPeriod::Month;
		}
	}

	// STORNO — the record reverses an earlier one (a correction of a closed period, registered now).
	ibValueMetaObjectAttributePredefined* GetStorno()              const { return m_propertyAttributeStorno->GetMetaObject(); }

	// The ACTUAL action periods — see ibCalcActualPeriodSourceDescriptor above. A derived table: the
	// register's own columns, one row per piece, keyed by (recorder, line, piece start). Its identity
	// is a metaobject of its own (the same holder the accumulation register's totals use), so the differ
	// matches it by an id nobody else owns. Present only when the register uses an action period.
	wxString GetActualActionPeriodTableName() const { return GetPhysicalTableName() + wxT("_AP"); }
	ibMetaID GetActualActionPeriodTableId() const { return m_actualPeriods->GetMetaID(); }
	const ibBackendQueryable* GetActualActionPeriodQueryable() const;

	// ⭐ CALCULATION TYPE — the standard attribute every calculation record carries: which calculation
	// type of the bound chart this record is. The chart's relations over it (Displacing, Base, Leading)
	// are what displacement, the base and the recalculations are decided by. The register is BOUND to exactly one chart of calculation types;
	// binding sets the CalculationType attribute's type to a reference into that chart.
	ibValueMetaObjectAttributePredefined* GetCalculationType() const { return m_propertyAttributeCalculationType->GetMetaObject(); }
	void SetChartOfCalculationTypes(const ibMetaID& chartMetaID);   // binds the register to a chart of calc types

	// The bound chart itself, resolved from the property. Null when nothing is chosen — and the save
	// refuses that state, because every column below the binding is derived from it.
	const ibValueMetaObjectChartOfCalculationTypes* GetChartOfCalculationTypes() const;

	// Apply the binding the property names onto the CalculationType attribute. Called wherever the
	// binding can have changed — on load, on reload, and when the property itself changes — because a
	// binding that is only applied at one of those goes stale at the other two.
	void ApplyChartBinding();

	// ⭐⭐ THE DISPLACEMENT RELATION OF THIS REGISTER'S CHART — read once, as data. The record-set write is
	// its one reader: it keeps the actual action periods, and every other road that needs displacement
	// (GetBase, a query) reads those stored pieces instead of displacing again. Three roads used to
	// displace, each by "priority = the order the rows came in", and a report disagreed with the write.
	//
	// typeIndex — each calculation type met in the relation, mapped to a dense ordinal;
	// edges — {displaced, displacer} in those ordinals, one per row of a type's Displacing section.
	// The reading itself is the chart's (ReadRelation — the same one the Base and Leading sections are
	// read with); what this adds is WHICH chart, and a type the relation never names takes ordinal -1
	// (ibCalcTypeOrdinal) — such a record neither cuts nor is cut.
	void ReadDisplacementRelation(std::map<ibValue, int>& typeIndex, std::vector<std::pair<int, int>>& edges) const;

	// ⭐ BASE PERIOD — the interval whose already-computed results a dependent calculation reads as its
	// base (dependency-by-base-period). When on, the record carries [baseStart, baseEnd] naming the span
	// its base amount is summed over. Independent of the action period.
	bool IsUseBasePeriod() const { return m_propertyUseBasePeriod->GetValueAsBoolean(); }
	ibValueMetaObjectAttributePredefined* GetBasePeriodStart() const { return m_propertyAttributeBasePeriodStart->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetBasePeriodEnd()   const { return m_propertyAttributeBasePeriodEnd->GetMetaObject(); }

	// The registers this one can take a base FROM — each one a source `<this>.Base<that>`: every
	// calculation register bound to this register's chart or to one of the charts its BaseCharts lists
	// (the charts whose types the Base section may name). None when this register keeps no base period.
	std::vector<const ibValueMetaObjectCalculationRegister*> GetBaseRegisters() const;

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

	//has record manager — a calculation register is always subordinate to a recorder, so it is
	//written as a record SET, never as an independent record.
	virtual bool HasRecordManager() const { return false; }

	//has recorder — ALWAYS: a calculation register is by definition subordinate to a recorder.
	//has period — NO: its rows are dated by the REGISTRATION period (GetRegistrationPeriod), not by the
	//register family's second-precise Period, which is therefore not a column of it at all — so it is not
	//part of the key (recorder + line), of the period index, or of the list's order.
	virtual bool HasPeriod() const { return false; }
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

	// The register's own table plus its recalculations' — see calculationRegisterMetadataSchema.cpp.
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	// Additive contract — a calculation register is ALWAYS subordinate to a recorder, so the recorder
	// family's attributes (active flag, recorder, line number) are always part of it, with no WriteMode
	// gate. NOT the family's Period — see HasPeriod.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRegisterData::FillArrayObjectByPredefinedAttribute(array);

		array.emplace_back(m_propertyAttributeLineActive->GetMetaObject());
		array.emplace_back(m_propertyAttributeRecorder->GetMetaObject());
		array.emplace_back(m_propertyAttributeLineNumber->GetMetaObject());
		array.emplace_back(m_propertyAttributeCalculationType->GetMetaObject());   // the calculation type — always present
		array.emplace_back(m_propertyAttributeStorno->GetMetaObject());            // storno — always present

		// 🛑 THE REGISTRATION PERIOD IS NOT AN ACTION-PERIOD ATTRIBUTE — every calculation record has one:
		// it is the month the record belongs to, the one period a calculation register has of its own.
		// It sat under the gate below, so a register without an action period (a deductions register —
		// a deduction is not "in force" over days) had no registration period at all. MEASURED
		// 2026-09-10: `Aggregate object field not found 'RegistrationPeriod'` on the first line written.
		array.emplace_back(m_propertyAttributeRegistrationPeriod->GetMetaObject());

		// Action-period standard attributes become columns only when the register uses an action
		// period — otherwise a calculation record is a point event and these would be dead columns.
		if (m_propertyUseActionPeriod->GetValueAsBoolean()) {
			array.emplace_back(m_propertyAttributeActionPeriod->GetMetaObject());
			array.emplace_back(m_propertyAttributeActionPeriodStart->GetMetaObject());
			array.emplace_back(m_propertyAttributeActionPeriodEnd->GetMetaObject());
		}

		if (m_propertyUseBasePeriod->GetValueAsBoolean()) {
			array.emplace_back(m_propertyAttributeBasePeriodStart->GetMetaObject());
			array.emplace_back(m_propertyAttributeBasePeriodEnd->GetMetaObject());
		}

		return true;
	}

	//get dimension keys — always keyed by the recorder (subordinate register)
	virtual bool FillArrayObjectByDimension(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyAttributeRecorder->GetMetaObject() };
		return true;
	}

	//create manager
	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const;

	//create record set — and no record manager (the base answers null): see the form list above
	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const;

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const;

	//get command section
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Combined; }

	//load & save metaData from DB

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	//get default form — the list, whatever is asked: there is no record to create on its own
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {
		return GetListForm();
	}

private:

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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), &ibValueMetaObjectCalculationRegister::FillFormList);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));

	// ⭐⭐ WHICH CHART OF CALCULATION TYPES — the binding a calculation register cannot do without, and
	// the one the import had no way to author. `SetChartOfCalculationTypes` existed and nothing ever
	// called it, so `CalculationType` stayed untyped for the life of the register: no vocabulary of
	// calculation types, no displacement relation to read, and therefore no priority to fold with.
	// Exactly one chart, said the way the accounting register says its own (propertyChartOfAccounts.h).
	ibPropertyChartOfCalculationTypes* m_propertyChartOfCalculationTypes =
		ibPropertyObject::CreateProperty<ibPropertyChartOfCalculationTypes>(m_categoryData, wxT("ChartOfCalculationTypes"), _("Chart of calculation types"));

	// The grain of the registration period — see GetPeriodicity. A month unless said otherwise; a
	// configuration saved before the property existed reads as a month too (ibProperty::SetNodeValue:
	// an absent value leaves the constructor's standing).
	ibPropertyEnum<ibValueEnumCalcPeriodicity>* m_propertyPeriodicity =
		ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumCalcPeriodicity>>(m_categoryData, wxT("Periodicity"), _("Periodicity"), ibCalcPeriodicity::eCalcPeriodMonth);

	// Action period configuration + its standard attributes (predefined). The attributes exist for the
	// life of the register (stable metaIDs -> stable fld<metaID> columns), but only enter the schema
	// when UseActionPeriod is on (see FillArrayObjectByPredefinedAttribute).
	// ⭐ THE PERIODS ARE DAYS, and an END IS INCLUSIVE: a sick leave 12.06–16.06 is five days (Max's
	// example, 2026-09-10 — "12.06.2023 0:00:00 .. 16.06.2023 0:00:00", five calendar days). A calculation
	// is in force over whole days; a second-precise value there only invites a time of day nothing reads.
	ibPropertyBoolean* m_propertyUseActionPeriod = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseActionPeriod"), _("Use action period"), false);
	ibPropertyContainer<>* m_propertyAttributeActionPeriod       = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriod"),       _("Action period"),       wxEmptyString, ibDateFractions::ibDateFractions_Date, false));
	ibPropertyContainer<>* m_propertyAttributeActionPeriodStart  = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriodStart"),  _("Action period start"),  wxEmptyString, ibDateFractions::ibDateFractions_Date, true));
	ibPropertyContainer<>* m_propertyAttributeActionPeriodEnd    = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriodEnd"),    _("Action period end"),    wxEmptyString, ibDateFractions::ibDateFractions_Date, true));
	ibPropertyContainer<>* m_propertyAttributeRegistrationPeriod = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("RegistrationPeriod"), _("Registration period"), wxEmptyString, ibDateFractions::ibDateFractions_Date, true));
	ibPropertyContainer<>* m_propertyAttributeStorno             = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("Storno"), _("Storno"), wxEmptyString, false, false));

	// The actual action periods' table identity (see GetActualActionPeriodTableId) — a predefined child,
	// declared where every other predefined part of the register is, which is what reserves its id —
	// and its source.
	ibValuePtr<ibValueMetaObjectRegisterTotals> m_actualPeriods{
		CreateMetaObjectAndSetParent<ibValueMetaObjectRegisterTotals>(wxT("ActualActionPeriods"), _("Actual action periods")) };
	mutable std::unique_ptr<ibSchemaTableQueryable> m_actualPeriodsQueryable;   // rebuilt on every run
	ibCalcActualPeriodSourceDescriptor m_actualPeriodsSource{ this };

	// The base from each register GetBaseRegisters names, as a source — built afresh on every run (the
	// registers a base may come from are configuration, and a run is where it may have changed).
	std::vector<std::unique_ptr<ibCalcBaseSourceDescriptor>> m_baseSources;

	// The calculation-type standard attribute — an empty-typed reference whose type is set to the bound
	// chart of calculation types by SetChartOfCalculationTypes (like the recorder's type is set by its
	// posting documents). Always a predefined attribute of a calculation register.
	ibPropertyContainer<>* m_propertyAttributeCalculationType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("CalculationType"), _("Calculation type"), wxEmptyString));

	ibPropertyBoolean* m_propertyUseBasePeriod = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseBasePeriod"), _("Use base period"), false);
	// NOT required: only a record whose type has a base reads one — a salary line has no base period.
	ibPropertyContainer<>* m_propertyAttributeBasePeriodStart = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("BasePeriodStart"), _("Base period start"), wxEmptyString, ibDateFractions::ibDateFractions_Date, false));
	ibPropertyContainer<>* m_propertyAttributeBasePeriodEnd   = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("BasePeriodEnd"),   _("Base period end"),   wxEmptyString, ibDateFractions::ibDateFractions_Date, false));

	friend class ibValueRecordSetObjectCalculationRegister;

	friend class ibMetaData;
};

//********************************************************************************************
//*                                   Recalculation                                          *
//********************************************************************************************

// A SUBORDINATE metaobject of a calculation register. It holds a set of DIMENSION children and
// contributes ONE physical DB table keyed by those dimensions plus a reference to the record being
// recalculated. Shaped after the DB-backed tabular section (ibValueMetaObjectTableDataRef): it vends an
// L4 queryable + a parent-qualified physical table and registers itself as a query source on run.

// ⭐⭐ THE TWO STANDARD COLUMNS OF A RECALCULATION ROW — the record being recalculated (its recorder) and
// its calculation type. Each has two halves, and they come from two different owners:
//
//   IDENTITY — the recalculation's own predefined attribute: a number from the configuration's counter,
//   handed out at creation and saved with it (so the schema differ can track the column, and the
//   counter's seed walk sees the number and never hands it out again), and `fld<id>` as its field.
//
//   TYPE — the REGISTER'S, asked each time: the recorder attribute's type for the object, the
//   calculation-type attribute's for the type. The recorder's type is not settled when the register
//   runs: every document that posts into the register appends itself to it in ITS OWN run, in an order
//   nobody fixes, so a copy taken at any one moment would miss the documents that ran after it. Asked at
//   the moment of asking, the answer is the one the register itself gives — the way a document's moment
//   asks the date and the reference (ibRecorderQueryable::ibBackendColumnPointInTime).
//
// 🛑 THE IMPORT HAD THEM AS RAW single-field references, and neither can be one. A raw reference has a
// CONSTANT target: the object's was `reference_to_clsid(<the register's id>)` — a reference to the
// REGISTER, built from an id by hand, where the value is a document; the type's target was 0, which
// stores a guid and no way to know what it points at. Their ids were `metaID | 0x20000000` and
// `| 0x40000000` — positive bands, the scheme queryColumn.h retired because a band has to be read
// before every addition and nothing makes anybody read it.
//
// ⭐ AND A THIRD, WHEN THE REGISTER KEEPS ACTION PERIODS: THE MONTH THE MARKED RECORD IS FOR (2026-09-11).
// A mark named (recorder, type, dimension values), and one payroll run holds two positions of one type for
// one employee — its own month and a correction of an earlier one — so a mark on either sent BOTH back to
// be computed. The month is what tells the two apart (it is part of a position, calculation.h), and it is
// written beside the type. Its type is lent by the register's ActionPeriod.
class BACKEND_API ibRecalculationStandardColumn : public ibBackendQueryColumn {
public:
	enum class Role { RecalculationObject, CalculationType, ActionPeriod };

	ibRecalculationStandardColumn(const ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation* meta, Role role) : m_meta(meta), m_role(role) {}

	wxString GetName() const override;
	wxString GetSynonym() const override;
	wxString GetPhysicalName() const override;
	ibMetaID GetColumnId() const override;
	ibTypeDescription& GetTypeDesc() const override;

private:
	const ibValueMetaObjectAttributeBase* Identity() const;

	const ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation* m_meta;
	Role                                                                        m_role;
};

// ibRecalculationQueryable — the L3 queryable for a recalculation's physical table. It navigates over
// the recalculation metaobject: the columns are the two standard ones (the recalculation object and the
// calculation type) plus the dimension children. The table is parent-qualified.
class BACKEND_API ibRecalculationQueryable : public ibBackendQueryable {
public:
	explicit ibRecalculationQueryable(const ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation* meta)
		: m_meta(meta),
		  m_recalcObject(meta, ibRecalculationStandardColumn::Role::RecalculationObject),
		  m_calcType(meta, ibRecalculationStandardColumn::Role::CalculationType),
		  m_actionPeriod(meta, ibRecalculationStandardColumn::Role::ActionPeriod) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;
	virtual wxString GetQueryTableName() const override;
	virtual const ibUniqueKey& GetQueryTableGuid() const override;
	virtual wxString GetQueryName() const override;
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;
	// ONE KEY AUTHORITY (queryable.h): what identifies a row is the PRIMARY KEY, and there is no
	// second question. The import carried a GetIdentitySort override — the duplicate this tree
	// removed deliberately, because a consumer wanting a key then reached for the tail of a sort.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;

	// The standard columns — members, because a queryable hands out column POINTERS (a temporary would
	// leave every caller holding a dangling one). Null while the recalculation has no identity for them
	// (a recalculation saved before they had one — see the metaobject's note), so a caller that would
	// otherwise write a field named `fld0` gets nothing to write into.
	const ibBackendQueryColumn* RecalculationObjectColumn() const;
	const ibBackendQueryColumn* CalculationTypeColumn() const;
	// Null as well when the register keeps no action periods — there is no month to name then.
	const ibBackendQueryColumn* ActionPeriodColumn() const;

private:
	const ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation* m_meta;
	ibRecalculationStandardColumn                                               m_recalcObject;
	ibRecalculationStandardColumn                                               m_calcType;
	ibRecalculationStandardColumn                                               m_actionPeriod;
};

// ibRecalculationSourceDescriptor — the recalculation's L4 source descriptor. It CONTAINS the vended
// queryable and is parent-qualified: ns = the parent calculation register's kind, name =
// "<Register>.<Recalculation>", reached as the 3-segment source. Methods are out-of-line (the
// recalculation is incomplete here).
class BACKEND_API ibRecalculationSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibRecalculationSourceDescriptor(ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation* meta);
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	const ibRecalculationQueryable* GetQueryable() const { return &m_queryable; }   // typed: the recalculation reads its two standard columns off it
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation* m_meta;
	ibRecalculationQueryable                                              m_queryable;
};

// ibValueMetaObjectRecalculation — the metaobject, nested in the calculation register it belongs to. It
// holds dimension children and vends a single physical table. Not exported, like its owner: nothing
// outside the backend reaches its members, and the designer's tree only lists it.
class ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation : public ibValueMetaObjectCompositeData, public ibBackendQueryableHolder {
public:

	ibValueMetaObjectRecalculation();
	virtual ~ibValueMetaObjectRecalculation();

	//support icons
	virtual wxIcon GetIcon() const override;
	static wxIcon GetIconGroup();

	// A recalculation accepts DIMENSION children only.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaDimensionCLSID)
			return clsid;
		return 0;
	}

	//the metaobject VENDS its queryable (via the L4 source descriptor it owns).
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }

	//physical DB table — parent-qualified: "<Register><id>_RC<id>".
	virtual wxString GetPhysicalTableName() const {
		ibValueMetaObject* parentMeta = GetParent();
		wxASSERT(parentMeta);
		return wxString::Format(wxT("%s%i_RC%i"),
			parentMeta->GetClassName(),
			parentMeta->GetMetaID(),
			GetMetaID()
		);
	}

	// The IDENTITY halves of the two standard columns (see ibRecalculationStandardColumn). A number of 0
	// means a recalculation saved before they existed: it has no such columns until it is created anew.
	const ibValueMetaObjectAttributeBase* GetRecalculationObject() const { return m_propertyRecalculationObject->GetMetaObject(); }
	const ibValueMetaObjectAttributeBase* GetCalculationType() const { return m_propertyCalculationType->GetMetaObject(); }
	const ibValueMetaObjectAttributeBase* GetActionPeriod() const { return m_propertyActionPeriod->GetMetaObject(); }

	// The register this recalculation belongs to — its parent, and never anything else (ResolveChild),
	// lifted the way a child lifts its parent (GetParentAsType): for the columns' type and the month's question.
	const ibValueMetaObjectCalculationRegister* GetRegister() const;

	// Does a mark here name the month its record is for — the register keeps action periods, and this
	// recalculation has an identity for the column (see StampActionPeriodIfNeverSaved).
	bool KeepsActionPeriod() const;

	// …and the COLUMNS themselves — what a reader or a writer of this table names.
	const ibBackendQueryColumn* GetRecalculationObjectColumn() const { return m_queryable.GetQueryable()->RecalculationObjectColumn(); }
	const ibBackendQueryColumn* GetCalculationTypeColumn() const { return m_queryable.GetQueryable()->CalculationTypeColumn(); }
	const ibBackendQueryColumn* GetActionPeriodColumn() const { return m_queryable.GetQueryable()->ActionPeriodColumn(); }

	// ⭐ WHERE A MARK'S IDENTITY LIVES WHEN THE INDEX CANNOT HOLD THE KEY — empty in the ordinary case.
	// A recorder, a type and a few reference dimensions pass Firebird's sixteen index segments quickly (a
	// reference is three fields), and past them the uniqueness moves into a digest column, as a register's
	// totals key does (ibDeclareDerivedKey). Asked by the schema that declares the column and by the write
	// that fills it, through one question (ibDerivedKeyNeedsHash), so the two cannot disagree.
	wxString GetKeyHashColumn() const;

	//the dimension children of this recalculation.
	std::vector<ibValueMetaObjectDimension*> GetDimensionArrayObject() const {
		std::vector<ibValueMetaObjectDimension*> array;
		FillArrayObjectByFilter<ibValueMetaObjectDimension>(array, { g_metaDimensionCLSID });
		return array;
	}

	// The abstract contract of ibValueMetaObjectCompositeData: the generic attribute list. A
	// recalculation's columns are its dimension children (plus the synthetic reference columns, which
	// are vended, not declared attributes) — the schema builder / query generator meet them here.
	using ibValueMetaObjectCompositeData::GetGenericAttributeArrayObject;
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID });
		return array;
	}

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags) override;
	virtual bool OnLoadMetaObject(ibMetaData* metaData) override;
	virtual bool OnSaveMetaObject(int flags) override;
	virtual bool OnDeleteMetaObject() override;

	//for designer
	virtual bool OnReloadMetaObject() override;

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;
	virtual bool OnAfterCloseMetaObject() override;

public:

	// Declare the recalculation table (dimension columns + the two synthetic reference columns + the
	// lookup index). It does NOT recurse — its children are dimensions, which are its own columns.
	//
	// PUBLIC, as it is on the base. The import narrowed it to protected, which was enough while the
	// only caller was the base's own walk — and that walk never reaches a recalculation, because the
	// register above it bears a table and therefore does not recurse. The one caller that must exist
	// is the register itself (calculationRegisterMetadataSchema.cpp).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// The two identities, where they have one. Deliberately NOT in GetGenericAttributeArrayObject: an
	// attribute's own column would answer with the attribute's own (empty) type, and the columns that
	// answer with the register's are the queryable's — every road to this table goes through those.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		for (ibValueMetaObjectAttributeBase* identity : { m_propertyRecalculationObject->GetMetaObject(),
				m_propertyCalculationType->GetMetaObject(), m_propertyActionPeriod->GetMetaObject() })
			if (identity != nullptr && identity->GetMetaID() != 0)
				array.push_back(identity);
		return true;
	}

private:

	// ⚠ THE MONTH'S COLUMN WAS BORN AFTER RECALCULATIONS WERE FIRST SAVED, so a saved one has no node for it
	// and loads it at id 0. The id is handed out in before-run, and ONLY by the copy that saves itself —
	// schema-authority.md § 6.1, the rule the chart's sections follow (StampIfNeverSaved): stamped in the
	// copy that mirrors the database too, the column would be in both snapshots and never be added.
	bool StampActionPeriodIfNeverSaved(int flags);

	// The month's attribute once it has a number; null while it is 0 — not part of this configuration yet,
	// and run at 0 it would register under 0, so it takes no part in the events until then.
	ibValueMetaObjectAttributeBase* NumberedActionPeriod() const {
		ibValueMetaObjectAttributeBase* month = m_propertyActionPeriod->GetMetaObject();
		return month != nullptr && month->GetMetaID() != 0 ? month : nullptr;
	}

	ibPropertyCategory* m_categoryStandard = ibPropertyObject::CreatePropertyCategory(wxT("Standard"), _("Standard"));
	ibPropertyContainer<>* m_propertyRecalculationObject = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryStandard,
		CreateEmptyType(wxT("RecalculationObject"), _("Recalculation object"), wxEmptyString, ibItemMode::ibItemMode_Item));
	ibPropertyContainer<>* m_propertyCalculationType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryStandard,
		CreateEmptyType(wxT("CalculationType"), _("Calculation type"), wxEmptyString, ibItemMode::ibItemMode_Item));
	ibPropertyContainer<>* m_propertyActionPeriod = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryStandard,
		CreateEmptyType(wxT("ActionPeriod"), _("Action period"), wxEmptyString, ibItemMode::ibItemMode_Item));

	// the L4 source descriptor — CONTAINS the vended queryable (stable for this recalculation's life)
	// and is registered with the factory on run / close; GetQueryable() forwards to it.
	ibRecalculationSourceDescriptor m_queryable{ this };

	friend class ibRecalculationQueryable;
	friend class ibRecalculationSourceDescriptor;
	friend class ibMetaData;
};

inline std::vector<ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation*>
ibValueMetaObjectCalculationRegister::GetRecalculationArrayObject(std::vector<ibValueMetaObjectRecalculation*> array) const
{
	FillArrayObjectByFilter<ibValueMetaObjectRecalculation>(array, { g_metaRecalculationCLSID });
	return array;
}

//********************************************************************************************
//*                                  The base, as a source                                   *
//********************************************************************************************

// ⭐⭐ THE BASE, AS A SOURCE — `CalculationRegister.<Register>.Base<BaseRegister>(Condition)` (Max,
// 2026-09-11: "move the base onto a queryable"). The register's own records, each with the base it takes
// from <BaseRegister>: one `Base<Resource>` column per resource of the base register, summed over the
// record's base period. ONE ROAD: a module's GetBase builds this very table and reads it through the door,
// as a slice's manager reads its slice — so a report and a payroll run cannot read two different bases.
// Until now the base was reachable from a script alone, and a report showing what a bonus was computed
// on had nothing to read it from.
//
// COMPUTED IN MEMORY, AND NOT BY CHOICE: which base pieces meet which record is one statement on the
// server (ibCalcReadBase), but each piece's share is a division, and a NUMERIC divided in SQL keeps its
// operands' scale and cuts at the cent on every piece. So the rows are made here, and the query around
// them joins, groups and filters them as it does any computed table (a register's balance, a slice).
//
// ⭐ THE CONDITION IS THE SOURCE'S OWN. Written in the parentheses it chooses WHICH RECORDS take a base,
// and the base is computed for those alone; written in the WHERE around the table it chooses among rows
// already computed for the whole register — correct, and as slow as the register is long.

class ibCalcBaseQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectCalculationRegister> {
public:
	ibCalcBaseQueryable(const ibValueMetaObjectCalculationRegister* reg, const ibValueMetaObjectCalculationRegister* base,
		const ibQueryPredicatePtr& condition);

	// The register's columns, and the base's after them.
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;

	// The records the condition chooses (every record, with none), each with its base. Refuses a chart that
	// takes no base, and a base by action period from a register that keeps none; a record whose type takes
	// no base reads 0.
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	const std::vector<std::shared_ptr<ibBackendColumnRawDB>>& GetBaseColumns() const { return m_baseColumns; }

private:
	const ibValueMetaObjectCalculationRegister*        m_base;
	ibQueryPredicatePtr                                m_condition;     // which records take a base — consumed here
	std::vector<std::shared_ptr<ibBackendColumnRawDB>> m_baseColumns;   // Base<Resource>, shared: a column is shared by its owner
};

class ibCalcBaseSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	ibCalcBaseSourceDescriptor(const ibValueMetaObjectCalculationRegister* reg, const ibValueMetaObjectCalculationRegister* base)
		: m_reg(reg), m_base(base) {}

	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray,
		const std::vector<ibQueryPredicatePtr>& conditions) override;
	// The condition names the REGISTER'S fields — which records take a base — so it is resolved against
	// the register, which exists before any companion does.
	const ibBackendQueryable* GetConditionScope() const override;
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

private:
	const ibValueMetaObjectCalculationRegister* m_reg;
	const ibValueMetaObjectCalculationRegister* m_base;
	ibQueryPredicatePtr                         m_pendingCondition;   // the consumed condition, for the length of one call
	mutable std::unique_ptr<ibCalcBaseQueryable> m_catalogue;         // the explorer's columns: no condition, never read
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectCalculationRegister : public ibValueRecordSetObject {
	public:
	ibValueRecordSetObjectCalculationRegister(const ibValueMetaObjectCalculationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey), m_register(metaObject) {
		m_members.Bind(this, &ibValueRecordSetObjectCalculationRegister::FillMembers);
	}
	ibValueRecordSetObjectCalculationRegister(const ibValueRecordSetObjectCalculationRegister& source) :
		ibValueRecordSetObject(source), m_register(source.m_register) {
		m_members.Bind(this, &ibValueRecordSetObjectCalculationRegister::FillMembers);
	}
public:

	//default methods
	virtual ibValueRecordSetObject* CopyRegisterValue() {
		return new ibValueRecordSetObjectCalculationRegister(*this);
	}

protected:
	// ⭐⭐ THE RECALCULATIONS ARE KEPT BY THE WRITE ITSELF, inside its transaction — not left to whoever
	// posts. A set that is written or cleared CHANGES records, and two things follow from that at once:
	//   - this recorder's own marks are RESOLVED: its records were just computed again, from the bases
	//     and displacements as they stand now;
	//   - the records of OTHER recorders that this change leads (the Leading relation of THEIR chart, the
	//     recalculation's dimensions, meeting in time — ibFindLedRecords) are MARKED, one row per
	//     (recorder, calculation type, dimension values) — in this register's recalculations and in any
	//     other calculation register's whose chart names the changed types as leading (a deduction led
	//     by accruals lives in another register, over another chart).
	// "What was changed" is the records the recorder held before AND the ones it holds after: a
	// correction that removes a line leads its dependents exactly as one that adds a line does.
	// A person — or a payroll processing — then reads a recalculation like any source and re-posts the
	// recorders it names; re-posting is what clears them.
	// The same write keeps the ACTUAL ACTION PERIODS (ibCalcActualPeriodSourceDescriptor): the pieces of
	// every record of the employees it touched, across recorders.
	virtual bool SaveData(bool replace = true, bool clearTable = true) override;
	virtual bool DeleteData() override;

public:

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
	friend class ibValueMetaObjectCalculationRegister;

private:
	// The register this set writes, as it was handed in — the base keeps it only as a register in general.
	const ibValueMetaObjectCalculationRegister* m_register;
};

#endif
