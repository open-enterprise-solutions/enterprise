#ifndef __CALCULATION_REGISTER_H__
#define __CALCULATION_REGISTER_H__

#include "commonObject.h"
#include "chartOfCalculationTypesEnum.h"                                          // ibCalcPeriodicity — the register's grain
#include "backend/propertyManager/property/propertyEnum.h"                        // …as a property
#include "backend/propertyManager/property/propertyChartOfCalculationTypes.h"   // the chart binding
#include "backend/propertyManager/property/propertyCalcSchedule.h"              // the schedule binding
#include "backend/query/queryable.h"
// The register-shared lowering: ibRegFilterPredicate / ibRegFlatLeaves / ibRegCompositeIR — a
// calculation register filters its dimensions by the same rule the other registers do, so the rule
// lives in one shared file.
#include "backend/metaCollection/partial/registerQueryLowering.h"
#include "backend/query/tempTableQueryable.h"   // ibTempColumn — the columns a reading adds to a record's own

#include <functional>   // ibCalcNarrowing
#include <memory>
#include <utility>      // std::pair — the Displacing section's named fields
#include <vector>

class ibValueMetaObjectCalculationRegister;
class ibValueMetaObjectChartOfCalculationTypes;
class ibValueMetaObjectResource;

// (What a period of a record MEANS in days — whole days, the end included — and every other rule the
// register's records are computed by, are in backend/calculation/calculation.h.)

// ⭐⭐ THE RAW MOVEMENTS, AND THE FACT READ OFF THEM (Max, 2026-09-14: "the raw table, the actual action period,
// the recalculations" — the base is got from the fact, and what stands at a position is the movements summed).
// `<Register>.ActualActionPeriod` is ONE ROW PER PIECE of what displacement leaves of a record,
// ActionPeriodStart / ActionPeriodEnd holding the piece's bounds: a salary for the 1st–31st with a sick leave on
// the 10th–20th is in force on the 1st–9th AND the 21st–31st, two rows. Read over the records alone, nothing kept
// beside them (the section "The fact — a reading of the records", below).

// THE ARGUMENTS OF A CALCULATION REGISTER'S VIRTUAL TABLE, IN ORDER — the time first, the condition last, the rule
// every register's tables follow (accumulationRegister.h, ibRegBalanceArg). Two times, as a balance and a
// turnover each take one (Max, 2026-09-14: "as of which moment, and for which period — then the condition"), and
// the interval NAMED for the period it is of, so which one is meant is never a question (Max: "for the ACTION
// period — so it is clear at once"):
//   Period                    — AS OF WHEN: a moment of registration, the deltas of the periods up to the one it
//                               falls in, in the register's periodicity — "June as it stood when June closed". A
//                               balance's Period, and the same word (Max: "it carries the same meaning"). Left out,
//                               every period;
//   BeginOfActionPeriod / EndOfActionPeriod — FOR WHICH DAYS OF ACTION: the record's action period meets them —
//                               the records in force some day between the two; a turnover's BeginOfPeriod /
//                               EndOfPeriod, of the ACTION period (Max). Left out, all of them.
// Each is optional; each narrows inside the statement, the interval the records alone (never a displacer).
namespace ibCalcViewArg { enum { Period = 0, BeginOfActionPeriod = 1, EndOfActionPeriod = 2, Condition = 3, Count }; }
static_assert(ibCalcViewArg::EndOfActionPeriod == ibCalcViewArg::BeginOfActionPeriod + 1, "the interval is one argument pair");
static_assert(ibCalcViewArg::Condition == ibCalcViewArg::Count - 1, "the condition is last");

class ibCalcFactSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibCalcFactSourceDescriptor(ibValueMetaObjectCalculationRegister* meta) : m_meta(meta) {}
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	// ⭐ THE CONDITION IS CONSUMED — resolved against the fact surface (GetConditionScope) and applied by the
	// reading itself: narrowing every table read where it can, and over the pieces exactly (ComputeRows).
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray,
	                                          const std::vector<ibQueryPredicatePtr>& conditions,
	                                          const ibQueryReadColumns& read) override;
	const ibBackendQueryable* GetConditionScope() const override;
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
	void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectCalculationRegister* m_meta;
};

// `<Register>.ScheduleData` — THE SCHEDULE, SUMMED FOR EACH RECORD: the record's own columns, as the register lays a
// record out, and for every numeric resource of the schedule register (ibCalcScheduleDescription) four sums of it —
// over the record's action period, over its actual action period (what displacement leaves, the fact's pieces), over
// its base period, over its registration period. A record finds its schedule rows through the schedule's links, one
// for every dimension of the schedule but its date (a register saves only a complete schedule, OnSaveMetaObject);
// a dimension of the schedule left unlinked is summed over. The arguments are the fact's (ibCalcViewArg): as of which
// registration period, for which days of action, then the condition — each narrowing the records read.
class ibCalcScheduleDataSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibCalcScheduleDataSourceDescriptor(ibValueMetaObjectCalculationRegister* meta) : m_meta(meta) {}
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	// THE CONDITION IS CONSUMED — resolved against the schedule data's surface and applied to the records read.
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray,
	                                          const std::vector<ibQueryPredicatePtr>& conditions,
	                                          const ibQueryReadColumns& read) override;
	const ibBackendQueryable* GetConditionScope() const override;
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
	void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectCalculationRegister* m_meta;
};

// `<Register>.Recalculation` — THE REGISTER'S MARKS, as a table: the recorder whose records a mark sends back, the
// calculation type, the month (where the register keeps action periods) and every dimension of the register. The
// register's own columns — the same names, types and fields as its records — so a mark is laid out as a record is,
// and a dimension added to the register is one more column here.
class BACKEND_API ibRecalculationQueryable : public ibBackendQueryable {
public:
	explicit ibRecalculationQueryable(const ibValueMetaObjectCalculationRegister* reg) : m_reg(reg) {}

	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;
	virtual wxString GetQueryTableName() const override;
	virtual const ibUniqueKey& GetQueryTableGuid() const override;
	virtual wxString GetQueryName() const override;
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;
	// What a mark is found by, and a clear deletes by: the recorder, the type, the dimensions, the month. All of them
	// — every column is the key.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override { return GetColumns(); }

private:
	const ibValueMetaObjectCalculationRegister* m_reg;
};

class ibRecalculationSourceDescriptor : public ibQueryableSourceDescriptor {
public:
	explicit ibRecalculationSourceDescriptor(ibValueMetaObjectCalculationRegister* meta) : m_meta(meta), m_queryable(meta) {}
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	const ibRecalculationQueryable* GetQueryable() const { return &m_queryable; }
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectCalculationRegister* m_meta;
	ibRecalculationQueryable              m_queryable;
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

	// A recalculation a configuration saved before 2026-09-14 holds as an object of its own — loaded, and let go
	// (the section "A recalculation saved as an object", below). Defined below this class, once it is complete.
	class ibValueMetaObjectRecalculation;

	ibValueMetaObjectCalculationRegister();
	virtual ~ibValueMetaObjectCalculationRegister();

	// …which is the one child a calculation register takes beyond the register-data base's: a configuration that
	// holds one has to open. It is never created anew (the object refuses it).
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		if (clsid == g_metaRecalculationCLSID)
			return clsid;
		return ibValueMetaObjectRegisterData::ResolveChild(clsid);
	}

	// ⭐ ACTION PERIOD — the semantic heart of a calculation register. When on, a calculation record is
	// not a point event but an INTERVAL [start, end] over which it is in force; records of competing
	// calculation types displace each other over overlapping action periods. When off, the record
	// carries only the registration period (when it was entered). See docs (calculation engine).
	bool IsUseActionPeriod() const { return m_propertyUseActionPeriod->GetValueAsBoolean(); }
	// The schedule the register is bound to — which information register, its value and its date.
	const ibCalcScheduleDescription& GetScheduleDesc() const { return m_propertySchedule->GetValueAsScheduleDesc(); }
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
	// year. The write normalizes nothing (Max, 2026-09-14: "nothing needs bringing to anything — the document
	// forms its deltas in the current month and writes that month into the registration period"); a reader
	// that compares periods truncates to this grain itself — the recalculations' "a later period".
	ibCalcPeriodicity GetPeriodicity() const { return m_propertyPeriodicity->GetValueAsEnum(); }
	virtual ibTotalsPeriod GetPeriodicityUnit() const override {
		switch (GetPeriodicity()) {
		case ibCalcPeriodicity::eCalcPeriodDay:     return ibTotalsPeriod::Day;
		case ibCalcPeriodicity::eCalcPeriodQuarter: return ibTotalsPeriod::Quarter;
		case ibCalcPeriodicity::eCalcPeriodYear:    return ibTotalsPeriod::Year;
		default:                                    return ibTotalsPeriod::Month;
		}
	}

	// STORNO — the record reverses an earlier one (a correction of a closed period, registered now).
	ibValueMetaObjectAttributePredefined* GetStorno()              const { return m_propertyAttributeStorno->GetMetaObject(); }

	// ⭐⭐ THE RECALCULATION IS THE REGISTER'S OWN (Max, 2026-09-14): switched on, the register keeps ONE table of marks
	// whose dimensions are its own — a dimension added to the register is a column added to the marks — and the
	// platform keeps it from the chart's Leading section (the section "The recalculation's marks", below). Nothing
	// else is declared: a recalculation with dimensions of its own could only repeat the register's, or name a
	// dimension its records cannot fill. Read as `CalculationRegister.<Register>.Recalculation`.
	bool IsUseRecalculation() const { return m_propertyUseRecalculation->GetValueAsBoolean(); }
	// …and it is there: switched on AND its holder numbered — the marks' table exists, and the source reads it.
	bool HasRecalculation() const { return IsUseRecalculation() && m_recalculation->GetMetaID() != 0; }
	// The marks' table — named after its identity's holder, as a derived table is, and identified by it.
	wxString GetRecalculationTableName() const {
		return wxString::Format(wxT("%s%i_%s"), GetClassName(), GetMetaID(), m_recalculation->GetName());
	}
	const ibValueMetaObjectRegisterTotals* GetRecalculationObject() const { return m_recalculation; }
	const ibRecalculationQueryable* GetRecalculationQueryable() const { return m_recalculationSource.GetQueryable(); }

	// The shape the fact publishes — built from the register's own attributes, touching no database,
	// so a query is drawn against it on a base never opened (the accumulation register's GetViewQueryable).
	const ibBackendQueryable* GetFactSurface() const;
	// The shape ScheduleData publishes: the record's columns and the schedule's sums (ibCalcScheduleDataSourceDescriptor).
	const ibBackendQueryable* GetScheduleDataSurface() const;

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

	// ⭐ BASE PERIOD — the interval whose already-computed results a dependent calculation reads as its
	// base (dependency-by-base-period). When on, the record carries [baseStart, baseEnd] naming the span
	// its base amount is summed over. Independent of the action period.
	bool IsUseBasePeriod() const { return m_propertyUseBasePeriod->GetValueAsBoolean(); }
	ibValueMetaObjectAttributePredefined* GetBasePeriodStart() const { return m_propertyAttributeBasePeriodStart->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetBasePeriodEnd()   const { return m_propertyAttributeBasePeriodEnd->GetMetaObject(); }

	// The registers whose types this register's chart may name — every calculation register bound to that chart
	// or to one of the charts its BaseCharts lists (the Base and Leading sections name types of those charts). A
	// recalculation reads its leading records from them.
	std::vector<const ibValueMetaObjectCalculationRegister*> GetRelatedRegisters() const;

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

	// The register's own table, plus its marks' when the recalculation is on — see calculationRegisterMetadataSchema.cpp.
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);
	virtual void OnPropertyRefresh() override;

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

	ibPropertyInnerModule<ibValueMetaObjectModule>* m_propertyObjectModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectModule>>(m_categoryContext, wxT("RecordSetModule"), _("Record set module"), _("Code that runs with a record set of the register: its BeforeWrite and OnWrite handlers, and the procedures they call. It runs for every set written, whoever writes it - a document's posting or a script."));
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyManagerModule = ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(m_categoryContext, wxT("ManagerModule"), _("Manager module"), _("Code of the register as a whole rather than of one set: its exported procedures and functions are called on the manager, as CalculationRegisters.<Name>.<Function>()."));

	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("PresetValues"), _("Preset values"));
	ibPropertyList* m_propertyDefFormList = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("DefaultFormList"), _("Default List Form"), _("The form the register's list opens with. Empty: the list form is generated from the register's fields."), &ibValueMetaObjectCalculationRegister::FillFormList);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));

	// ⭐⭐ WHICH CHART OF CALCULATION TYPES — the binding a calculation register cannot do without, and
	// the one the import had no way to author. `SetChartOfCalculationTypes` existed and nothing ever
	// called it, so `CalculationType` stayed untyped for the life of the register: no vocabulary of
	// calculation types, no displacement relation to read, and therefore no priority to fold with.
	// Exactly one chart, said the way the accounting register says its own (propertyChartOfAccounts.h).
	ibPropertyChartOfCalculationTypes* m_propertyChartOfCalculationTypes =
		ibPropertyObject::CreateProperty<ibPropertyChartOfCalculationTypes>(m_categoryData, wxT("ChartOfCalculationTypes"), _("Chart of calculation types"), _("The chart of calculation types the register is bound to. Every record's CalculationType is a reference into it, and its sections decide the calculation: Displacing - which types cut a record's days, Base - which results its base is made of, Leading - which changes make it stale. Required: the register is not saved without it."));

	// The grain of the registration period — see GetPeriodicity. A month unless said otherwise; a
	// configuration saved before the property existed reads as a month too (ibProperty::SetNodeValue:
	// an absent value leaves the constructor's standing).
	ibPropertyEnum<ibValueEnumCalcPeriodicity>* m_propertyPeriodicity =
		ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumCalcPeriodicity>>(m_categoryData, wxT("Periodicity"), _("Periodicity"), _("The grain of the registration period: day, month (the default), quarter or year. A record is registered for the start of its period; a reading as of a moment takes the periods up to the one the moment falls in, and a base without a base period spans the record's registration period at this grain."), ibCalcPeriodicity::eCalcPeriodMonth);

	// Action period configuration + its standard attributes (predefined). The attributes exist for the
	// life of the register (stable metaIDs -> stable fld<metaID> columns), but only enter the schema
	// when UseActionPeriod is on (see FillArrayObjectByPredefinedAttribute).
	// ⭐ THE PERIODS ARE DAYS, and an END IS INCLUSIVE: a sick leave 12.06–16.06 is five days (Max's
	// example, 2026-09-10 — "12.06.2023 0:00:00 .. 16.06.2023 0:00:00", five calendar days). A calculation
	// is in force over whole days; a second-precise value there only invites a time of day nothing reads.
	ibPropertyBoolean* m_propertyUseActionPeriod = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseActionPeriod"), _("Use action period"), _("Records carry an action period: the days a record is in force (ActionPeriodStart to ActionPeriodEnd, both inclusive) and the month it is for (ActionPeriod). Needed for displacement: the register's ActualActionPeriod reads what the Displacing types leave of each record. Switching it adds or drops the three columns."), false);

	// ⭐ THE SCHEDULE — right under the switch that governs it: a schedule is counted over a record's DAYS, and
	// the days are the action period's, so without an action period there is nothing to count it over. Declared
	// here because a category shows its properties in the order they are declared: each switch, then what it
	// opens. The binding is the information register a record's days are counted against, its resource that is
	// the value and its dimension that is the date, as one property (propertyCalcSchedule.h); shown only while
	// the action period is on (OnPropertyRefresh), and kept, not cleared, while it is off.
	ibPropertyCalcSchedule* m_propertySchedule =
		ibPropertyObject::CreateProperty<ibPropertyCalcSchedule>(m_categoryData, wxT("Schedule"), _("Schedule"), _("The schedule the register's calculations read: an information register holding the schedule, the resource that is its value (hours or days) and the dimension that is its date. The schedule data a record reads is this value summed over its days. Available with the action period."));
	ibPropertyContainer<>* m_propertyAttributeActionPeriod       = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriod"),       _("Action period"),       _("The period the record is FOR: a correction of June registered in July is for June. A record's position - dimensions, type, this period and its days - is what stornos, displacement and recalculation marks are matched by."), ibDateFractions::ibDateFractions_Date, false));
	ibPropertyContainer<>* m_propertyAttributeActionPeriodStart  = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriodStart"),  _("Action period start"),  _("The first day the record is in force (whole days, the day itself included). With the end it gives the days displacement cuts and the fact reads pieces of."), ibDateFractions::ibDateFractions_Date, true));
	ibPropertyContainer<>* m_propertyAttributeActionPeriodEnd    = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("ActionPeriodEnd"),    _("Action period end"),    _("The last day the record is in force, included: a sick leave 12.06-16.06 is five days. A record whose end is before its start has no days and cuts nothing."), ibDateFractions::ibDateFractions_Date, true));
	ibPropertyContainer<>* m_propertyAttributeRegistrationPeriod = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("RegistrationPeriod"), _("Registration period"), _("The period the record was entered in, at the start of the register's periodicity. A correction of a closed month is registered in the current one. Readings as of a moment (the Period argument of the virtual tables) and a base without a base period go by it."), ibDateFractions::ibDateFractions_Date, true));
	ibPropertyContainer<>* m_propertyAttributeStorno             = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("Storno"), _("Storno"), _("Marks a record that reverses an earlier one: the same position, the figures with the sign turned, registered now. A position's records net out - a storno counts minus one - so a displacer taken back cuts nothing; a storno also answers the recalculation marks of its position."), false, false));

	// Use recalculation (IsUseRecalculation), and the identity of the marks' table: a holder created with the register
	// and saved with it, the way a derived table's identity is held (registerQueryLowering.h). A register created
	// before it existed gets its number when recalculation is switched on (OnPropertyChanged).
	ibPropertyBoolean* m_propertyUseRecalculation = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseRecalculation"), _("Use recalculation"), _("Keep a table of recalculation marks for this register. A record written in any register whose types the Leading section names marks the records of this one it makes stale (same shared dimensions, meeting in time); the platform answers a mark when the marked recorder is written again or a storno corrects the position. Queried as CalculationRegister.<Register>.Recalculation. Switching it creates or drops the table."), false);
	ibValuePtr<ibValueMetaObjectRegisterTotals> m_recalculation{
		CreateMetaObjectAndSetParent<ibValueMetaObjectRegisterTotals>(wxT("Recalculation"), _("Recalculation")) };

	// The fact and the recalculation, as sources.
	ibCalcFactSourceDescriptor m_factSource{ this };
	ibCalcScheduleDataSourceDescriptor m_scheduleDataSource{ this };
	ibRecalculationSourceDescriptor m_recalculationSource{ this };

	// The shape the fact publishes, and what it was built from (GetFactSurface).
	mutable ibRegSurfaceCache m_surfaces;

	// The calculation-type standard attribute — an empty-typed reference whose type is set to the bound
	// chart of calculation types by SetChartOfCalculationTypes (like the recorder's type is set by its
	// posting documents). Always a predefined attribute of a calculation register.
	ibPropertyContainer<>* m_propertyAttributeCalculationType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("CalculationType"), _("Calculation type"), _("Which calculation type of the bound chart the record is. The chart's sections over this type decide what displaces the record, what its base is made of and which changes mark it for recalculation.")));

	ibPropertyBoolean* m_propertyUseBasePeriod = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("UseBasePeriod"), _("Use base period"), _("Records carry a base period (BasePeriodStart to BasePeriodEnd): the span GetBase reads the base records over, by action period or by registration as the chart's base dependence says. Off: the base spans the record's registration period at the register's periodicity."), false);
	// NOT required: only a record whose type has a base reads one — a salary line has no base period.
	ibPropertyContainer<>* m_propertyAttributeBasePeriodStart = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("BasePeriodStart"), _("Base period start"), _("The first day of the span the record's base is read over - a vacation paid from the three months before it, say. Only a record whose type has a Base section reads one; a salary line leaves it empty."), ibDateFractions::ibDateFractions_Date, false));
	ibPropertyContainer<>* m_propertyAttributeBasePeriodEnd   = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("BasePeriodEnd"),   _("Base period end"),   _("The last day of the span the record's base is read over, included."), ibDateFractions::ibDateFractions_Date, false));

	friend class ibValueRecordSetObjectCalculationRegister;

	friend class ibMetaData;
};

//********************************************************************************************
//*                          A recalculation saved as an object                              *
//********************************************************************************************

// ⚠ A CONFIGURATION SAVED BEFORE 2026-09-14 HOLDS ITS RECALCULATIONS AS OBJECTS OF THEIR OWN, each with dimension
// children, and a configuration has to open whatever it holds — the loader refuses a child nobody can build. This
// builds them: a name and its dimensions and nothing more, never created anew (OnCreateMetaObject refuses), and let
// go by its register as soon as the register has loaded (the register's OnLoadMetaObject, with a line in the
// journal). The marks are the register's now, switched on by "Use recalculation"; the table the object had is
// declared by nobody, and the schema's diff, which knows only what is declared, never drops it — the journal line
// names it for whoever does.

class ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation : public ibValueMetaObject {
public:

	//support icons
	virtual wxIcon GetIcon() const override;
	static wxIcon GetIconGroup();

	// Its dimensions — what it was saved with, and what has to load under it.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		return clsid == g_metaDimensionCLSID ? clsid : 0;
	}

	// Never made anew: the register's own recalculation is switched on instead.
	virtual bool OnCreateMetaObject(ibMetaData* /*metaData*/, int /*flags*/) override { return false; }
};

//********************************************************************************************
//*                              The recalculation's marks                                   *
//********************************************************************************************

// ⭐⭐ A RECALCULATION IS A SET OF ITS OWN, WRITTEN WITH THE RECORDS (Max, 2026-09-14: "a recalculation is a separate
// set inside the calculation register, which writes its data separately"). When a set of a register is written or
// deleted, the records it held and the records it holds are LEADING records: every record of a register keeping a
// recalculation that one of them leads gets a mark — its recorder, its type, its values on the register's dimensions
// (and the month) — unless the mark is there already. A leading record leads a record when its type is named in the
// led record's type's Leading section, it holds the led record's values on the dimensions the two registers share, and
// the two meet in time (ibFindLedRecords, calculation.h) — whenever either was registered: a salary of February
// changed marks the bonus of March computed on it.
//   * The recorder being written is never marked: it is not stale by its own change.
//   * A write is a clearing and a writing, not a difference (Max, 2026-09-14: "deleting and posting again is
//     clearing the movements"): what the recorder held leads as it goes, what it holds leads as it comes in. A set
//     written again as it was marks what its lines lead, as the reference does.
//   * A mark is answered by the platform, never cleared by hand (Max, 2026-09-14 — simpler than the reference, where
//     the configuration clears them): a recorder written again answers its own marks — its records were just computed
//     again; a STORNO answers the marks of its position — the run that wrote it computed the position anew beside it;
//     and before a set holding stornos is replaced or deleted, the positions they reverse are marked again, so a
//     correction taken back leaves its position as marked as it was before the correction came. A run therefore reads
//     what is left to correct as the marks plus the positions its own stornos already correct.
//   * One statement per register, in the database — no row trigger, which would run the same lookups once per
//     record: the leading records of the recorder found through the register's key, the led ones through its lookup
//     index (_DIX).

// A register whose records may lead the recalculation's, by physical names.
struct ibRecalculationLeading {
	wxString                                   m_table;
	std::vector<std::pair<wxString, wxString>> m_named;          // its type's fields, each with the Leading row's field of the same role
	std::vector<std::pair<wxString, wxString>> m_dimensions;     // the dimensions both registers hold: the led register's field, and this one's
	wxString                                   m_registration;
	wxString                                   m_active;
	wxString                                   m_start, m_end;   // its action period; empty when it keeps none
};

// The recalculation's reading by physical names only — filled from the metadata by ibRecalculationSpecOf, so a
// test can fill one by hand (ibCalcViewSpec is the same arrangement).
struct ibRecalculationSpec {
	wxString              m_table;                    // the led register's records
	std::vector<wxString> m_mark;                     // what a row names: the recorder, the type, the dimensions and the
	                                                  //   month — the led register's own fields, which are the marks' too
	wxString              m_typeId;                   // the type's reference id — what a Leading row's owner holds
	wxString              m_registration, m_active, m_storno;
	wxString              m_start, m_end;             // the action period; empty when it keeps none
	wxString              m_baseStart, m_baseEnd;     // the base period; empty when it keeps none
	bool                  m_baseByRegistration = false;      // the chart's base dependence, as ibFindLedRecords reads it
	wxString              m_leading, m_owner;         // the chart's Leading section, and the field holding a row's owner
	std::vector<ibRecalculationLeading> m_sources;    // every register whose types the chart may name
};

// What a mark is taken from, asked of a table alias: the predicate its rows must meet there. `source` is -1 for the
// led register's records — the ones the rows name — and otherwise a leading register's index in the spec's sources:
// the records that lead. Null — or a null answer — takes everything.
using ibRecalculationNarrowing = std::function<ibQueryExprPtr(const wxString& alias, int source)>;

// The records the leading records lead, one row each under the led register's field names (m_mark). Which records
// lead, and which may be led, is the narrowing's to say (a recorder's records, and not the recorder's own). Null when
// nothing can lead anything.
BACKEND_API ibQueryRelPtr ibRecalculationRelation(const ibRecalculationSpec& spec, const ibRecalculationNarrowing& narrowing);

// The things a write does to the marks (calculationRegisterObject.cpp), each one statement per register keeping them:
//   the records `recorder` holds in `written` now are leading records — every register whose recalculation `written`
//   leads marks what they lead (before the old records go, and after the new ones are in);
void ibRecalculationMarkLedBy(const ibValueMetaObjectCalculationRegister* written, const ibValue& recorder);
//   the recorder's stornos in `reg` answer the marks of their positions (after the write);
void ibRecalculationAnswerBy(const ibValueMetaObjectCalculationRegister* reg, const ibValue& recorder);
//   the positions the recorder's stornos in `reg` reverse are marked again (before its records are replaced or go);
void ibRecalculationMarkReversed(const ibValueMetaObjectCalculationRegister* reg, const ibValue& recorder);
//   and the recorder written again answers its own marks — its records were just computed again.
void ibRecalculationAnswerOwn(const ibValueMetaObjectCalculationRegister* reg, const ibValue& recorder);

// A register's recalculation by its physical names — empty when it keeps none, or its chart no Leading section.
BACKEND_API ibRecalculationSpec ibRecalculationSpecOf(const ibValueMetaObjectCalculationRegister* reg);

//********************************************************************************************
//*          The fact — a reading of the records                                              *
//********************************************************************************************

// ⭐⭐ NOTHING IS KEPT BESIDE THE RECORDS (Max, 2026-09-14). The document writes its movements and nothing else, and
// what the register is read as is a reading of them (calculationRegisterMetadataTotals.cpp). A table of totals kept
// by the records' triggers was built first, the way the accumulation and accounting registers keep theirs, and
// taken down after it was MEASURED on the 40 000-employee copy: a position is the dimensions, the type, the month
// and the two days, so nearly every record is a position of its own — 261 022 rows of totals for 261 057 records,
// nothing folded — and its one reader asked it only for the displacers, 6 981 records of the rare types (a leave,
// an absence), 2.7% of them. The triggers cost 6 s of a 98 s July posting to keep a copy of the register for that.
// So a displacer is found among the records themselves, through their lookup index (the dimensions and the type).
// A storno is what it is in accounting — the same position with the amounts turned round — so it NETS OUT: a
// position is IN FORCE while its active records outnumber its active stornos. An inactive record exists and counts
// for nothing.
// THE FACT — each RECORD with the days the displacers in force leave it: its pieces. A record,
// not a position (Max, 2026-09-14): the recorder, the line and the registration are on the row, a payroll asks
// for the days of its own records, and a storno is a row of its own with the pieces of the record it reverses and
// the figures turned round — summed, the two net out. Days are whole days, both ends included; a record with no
// days has no piece.
//
// ⭐⭐ WHO DISPLACES WHOM IS READ WITH THE FACT (Max, 2026-09-14), as an accounting register reads the analytics of
// an account from its chart: the displaced type's Displacing section is joined to the records when the fact is
// read. So a change to the section acts at once, on every record already written — nothing kept has to follow it.
//
// 🛑 A READER'S NARROWING GOES INSIDE. Firebird does not carry a condition from around a derived table into
// one holding window functions — MEASURED 2026-09-12 on Firebird 5.0.3, 545 600 records: a windowed read whole
// took 121 s, filtered by one employee around it 120 s, the same body with the employee inside 0.002 s.
// ⚠ AND IT KNOWS WHOSE ROWS IT NARROWS. A dimension narrows every table the relation reads: displacement stays
// within one set of dimension values. The rest of a record — its calculation type, its month, its recorder —
// narrows only the SUBJECT, the records whose rows the reading produces, and never a DISPLACER: a displacer is of
// another type and may be of another month, and narrowed with the subject the fact would lose what cuts it.

// The records and the Displacing section by physical names only, filled from the metadata by ibCalcViewSpecOf —
// nothing that reads it touches a metaobject, which is what lets a test fill one by hand.
struct ibCalcViewSpec {
	wxString              m_period;         // the registration period
	std::vector<wxString> m_dimensions;     // every dimension's fields       } a position, in this order, under
	std::vector<wxString> m_type;           // the calculation type's fields  }   the register's own field names
	std::vector<wxString> m_actionPeriod;   // the month-for's fields         }
	wxString              m_start, m_end;   // the action period's date fields }
	std::vector<wxString> m_resources;      // every resource — one field each
	wxString              m_typeId;         // the calculation type's reference id — what owns a Displacing row

	// The displaced type's Displacing section: a row owned by the displaced type names a type that displaces it.
	// No table — a chart without the section — and nothing displaces anything.
	wxString              m_displacing;     // its table
	wxString              m_owner;          // the row's owner — the displaced type's reference id
	std::vector<std::pair<wxString, wxString>> m_named;   // the displacer type: a record's field and the section's field holding it
	wxString              m_namedId;        // the section's field holding the displacer type's reference id

	// The records themselves — what the fact's rows are, one per record as the register keeps them (Max,
	// 2026-09-14: the recorder, the line, the registration, the base period, Active, Storno). Under the register's
	// own field names, as the position's above are.
	wxString              m_records;        // the register's table
	std::vector<wxString> m_recordKey;      // the recorder's fields and the line — what a record is, and its pieces are walked by
	std::vector<wxString> m_recordFields;   // the rest a record carries out: the base period, Active, Storno
	wxString              m_active;         // Active — the record counts
	wxString              m_storno;         // Storno

	// The type the action period's days are STORED in. A day a reading computes is cast back to it, so it
	// stands beside a stored one: Firebird's day truncation answers a TIMESTAMP where the field is a DATE, and a
	// CASE choosing between the two is refused ("Datatypes are not comparable" — the base met it first, MEASURED
	// 2026-09-13). Unset — a test's plain text dates — a computed day goes out as computed.
	bool                  m_typedDays = false;
	ibColumnType          m_dayType;
};

// The columns the fact adds to a record's own — named so no register field can be one of them.
constexpr const wxChar* ibCalcDisplacerTypePrefix = wxT("ib_y");  // a displacer's fields, each prefixed, in the fact's walk
constexpr const wxChar* ibCalcPositionFirst  = wxT("ib_ps");      // the fact: the record's own action period
constexpr const wxChar* ibCalcPositionLast   = wxT("ib_pe");

// A reader's narrowing, asked of a table alias: the predicate its rows must meet there — `subject` says whether
// the alias holds the records the reading produces or their displacers (above). Null — or a null answer —
// reads everything.
using ibCalcNarrowing = std::function<ibQueryExprPtr(const wxString& alias, bool subject)>;

// The fact, for a reader's FROM: a record's fields (m_recordKey, the position's, the registration, m_recordFields,
// the resources), the action period's two carrying a piece's bounds and ib_ps / ib_pe the record's own; every
// active record with days, stornos included.
BACKEND_API ibQueryRelPtr ibCalcFactRelation(const ibCalcViewSpec& spec, const ibCalcNarrowing& narrowing = nullptr);

// The records and the Displacing section by their physical names.
BACKEND_API ibCalcViewSpec ibCalcViewSpecOf(const ibValueMetaObjectCalculationRegister* reg);

//********************************************************************************************
//*                                        The base                                          *
//********************************************************************************************

// ⭐ THE BASE OF RECORDS — GetBase, a record's and the manager's alike, by the reference (Max, 2026-09-14): a function
// asked for what it sums and how, not a table. For each record asked about, the base registers' resources summed
// over their records of a type the record's Base rows name, holding the record's values where the dimensions are
// paired, and lying in the record's base period — by the days they are in force in it (a base by action period:
// the base register's fact) or whole when registered in it (by registration period); the chart says which. Broken
// down by the sections when any are asked for. The answer is a value table: "LineNumber", a column a figure, a
// column a section — one row a record, or a record and a section.
struct ibCalcBaseAsked
{
	// A column of the answer: its name, and the base registers' resources summed into it.
	struct ibFigure {
		wxString m_name;
		std::vector<std::pair<const ibValueMetaObjectCalculationRegister*, const ibValueMetaObjectResource*>> m_resources;
	};
	std::vector<ibFigure> m_figures;
	// A dimension of the asking register, and the base registers' dimensions its value is compared with.
	struct ibPairing {
		const ibValueMetaObjectDimension* m_own = nullptr;
		std::vector<std::pair<const ibValueMetaObjectCalculationRegister*, const ibValueMetaObjectDimension*>> m_base;
	};
	std::vector<ibPairing> m_dimensions;
	// The base registers' fields the base is broken down by.
	std::vector<std::pair<const ibValueMetaObjectCalculationRegister*, const ibValueMetaObjectAttributeBase*>> m_sections;
};

// A record asking for its base: its line, its type, its base period — or, where the register keeps none, the period
// it is registered in — and its values of the paired dimensions (in the order of ibCalcBaseAsked::m_dimensions). A
// record in hand asks as it stands — nothing is read for it.
struct ibCalcBaseRecord
{
	ibValue              m_line;
	ibValue              m_type;
	wxDateTime           m_from, m_to;
	wxDateTime           m_registration;
	std::vector<ibValue> m_dimensions;
};

// The script's arguments as the question: `resources` an array of "Register.Resource[, Register.Resource…]" (a
// column each), `dimensions` a structure of this register's dimensions to "Register.Dimension[, …]", `sections` an
// array of "Register.Field". The registers named must be ones this register takes a base from (GetRelatedRegisters).
BACKEND_API ibCalcBaseAsked ibCalcBaseAskedOf(const ibValueMetaObjectCalculationRegister* reg, const ibValue& resources,
	const ibValue& dimensions, const ibValue& sections);

// The base of each record, as the value table GetBase answers.
BACKEND_API ibValue ibCalcReadBase(const ibValueMetaObjectCalculationRegister* reg, const ibCalcBaseAsked& asked,
	const std::vector<ibCalcBaseRecord>& records);

//********************************************************************************************
//*                                  The fact, as a source                                   *
//********************************************************************************************

// ⭐⭐ `<Register>.ActualActionPeriod` — ONE ROW PER PIECE OF A RECORD, laid out as the register lays a record out:
// RegistrationPeriod, Recorder, LineNumber, the position's columns, the base period, Active, Storno and the
// resources; ActionPeriodStart / End carry the piece's bounds, PositionStart / PositionEnd the record's own action
// period. COMPUTED, as a slice and a balance on the live road are: the reader's conditions on the record's own
// fields — written in the parentheses or in the WHERE — narrow the reading inside (Firebird needs them there,
// above), and every condition is then the provider's to apply over the rows: a condition it cannot narrow by costs
// time, never an answer.
class ibCalcFactQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectCalculationRegister> {
public:
	ibCalcFactQueryable(const ibValueMetaObjectCalculationRegister* reg, const ibValue& moment = ibValue(),
		const ibValue& actionFrom = ibValue(), const ibValue& actionTo = ibValue(),
		const ibQueryPredicatePtr& condition = nullptr)
		: ibComputedRegisterQueryable(reg), m_moment(moment), m_actionFrom(actionFrom), m_actionTo(actionTo),
		  m_condition(condition) {}

	// The shape, on every road — metadata only.
	virtual const ibBackendQueryable* NavigationSource() const override { return m_reg->GetFactSurface(); }
	// A piece: its record, and its first day.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

private:
	ibValue m_moment;                   // the moment of registration read up to (ibCalcViewArg); empty — every period
	ibValue m_actionFrom, m_actionTo;   // the days of action the records meet; empty — any
	ibQueryPredicatePtr m_condition;    // written into the parentheses, on the surface's columns; null — none
};

// The companion of ScheduleData — its rows, computed: the records, their actual pieces, and the schedule's rows
// summed over each record's four periods.
class ibCalcScheduleDataQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectCalculationRegister> {
public:
	ibCalcScheduleDataQueryable(const ibValueMetaObjectCalculationRegister* reg, const ibValue& moment = ibValue(),
		const ibValue& actionFrom = ibValue(), const ibValue& actionTo = ibValue(),
		const ibQueryPredicatePtr& condition = nullptr)
		: ibComputedRegisterQueryable(reg), m_moment(moment), m_actionFrom(actionFrom), m_actionTo(actionTo),
		  m_condition(condition) {}

	virtual const ibBackendQueryable* NavigationSource() const override { return m_reg->GetScheduleDataSurface(); }
	// A record: its recorder and its line.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

private:
	ibValue m_moment;                   // the moment of registration read up to (ibCalcViewArg); empty — every period
	ibValue m_actionFrom, m_actionTo;   // the days of action the records meet; empty — any
	ibQueryPredicatePtr m_condition;    // written into the parentheses, on the record's columns; null — none
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

class ibValueRecordSetObjectCalculationRegister : public ibValueRecordSetObject {
	public:
	ibValueRecordSetObjectCalculationRegister(const ibValueMetaObjectCalculationRegister* metaObject, const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) :
		ibValueRecordSetObject(metaObject, uniqueKey) {
		m_members.Bind(this, &ibValueRecordSetObjectCalculationRegister::FillMembers);
	}
	ibValueRecordSetObjectCalculationRegister(const ibValueRecordSetObjectCalculationRegister& source) :
		ibValueRecordSetObject(source) {
		m_members.Bind(this, &ibValueRecordSetObjectCalculationRegister::FillMembers);
	}
public:

	//default methods
	virtual ibValueRecordSetObject* CopyRegisterValue() {
		return new ibValueRecordSetObjectCalculationRegister(*this);
	}

	const ibValueMetaObjectCalculationRegister* GetCalculationMetaObject() const {
		return static_cast<const ibValueMetaObjectCalculationRegister*>(GetMetaObject());
	}

	// A line of a calculation register — an ordinary register line that can say its base: GetBase(Resources,
	// Dimensions, Sections), asked of the line as it stands in the set, before anything is written ("The base",
	// above). Nested for the reason the accounting register's line is: the base line type is the set's.
	class ibValueCalculationLine : public ibValueRecordSetObjectRegisterReturnLine {
	public:
		ibValueCalculationLine(ibValueRecordSetObjectCalculationRegister* ownerTable = nullptr,
		                       const ibDataViewItem& line = ibDataViewItem())
			: ibValueRecordSetObjectRegisterReturnLine(ownerTable, line), m_ownerSet(ownerTable) {}

		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

		// The method's id — outside any metaID, as the accounting line's collections are.
		enum { eMethodGetBase = 0x52000001 };

	private:
		ibValueRecordSetObjectCalculationRegister* m_ownerSet;
	};

	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override {
		if (!line.IsOk())
			return nullptr;
		return new ibValueCalculationLine(this, line);
	}
	virtual ibValue GetEmptyRow() override {
		return new ibValueCalculationLine(this, ibDataViewItem());
	}
	// What its lines are called: a register line's names and GetBase.
	virtual void DescribeReturnLine(ibMemberTable& helper) const override;

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

	// The write marks what the records lead — the ones the recorder held, before they go, and the ones it holds
	// after (ibRecalculationMarkLedBy, "The recalculation's marks").
	virtual bool SaveData(bool replace = true, bool clearTable = true) override;
	virtual bool DeleteData() override;

	friend class ibValue;
	friend class ibValueMetaObjectCalculationRegister;
};

#endif
