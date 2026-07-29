#ifndef __ACCUMULATION_REGISTER_H__
#define __ACCUMULATION_REGISTER_H__

#include "commonObject.h"
#include "accumulationRegisterEnum.h"
#include "backend/query/queryable.h"          // ibComputedRegisterQueryable<TReg> — shared base for the balance / turnover virtual tables
#include "backend/query/tempTableQueryable.h" // ibDbTempTableQueryable — a named physical relation; what a VIEW is to L3

#include <map>
#include <memory>

class ibValueMetaObjectAccumulationRegister;
class ibBalanceQueryable;
class ibTurnoverQueryable;
class ibBalanceAndTurnoverQueryable;

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

// The THIRD virtual table: balance AND turnover, per period. Reported per resource as opening
// balance / receipt / expense / turnover / closing balance — the readings a stock report is
// actually made of. Its parameters are the interval plus the PERIODICITY the rows roll up to.
class ibAccumRegisterBalanceAndTurnoverDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibAccumRegisterBalanceAndTurnoverDescriptor(ibValueMetaObjectAccumulationRegister* reg) : m_reg(reg) {}
	~ibAccumRegisterBalanceAndTurnoverDescriptor() override;
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
private:
	ibValueMetaObjectAccumulationRegister*          m_reg;
	std::unique_ptr<ibBalanceAndTurnoverQueryable>  m_companion;
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
		return m_propertyAttributeRecordType->GetMetaObject();
	}

	bool IsRegisterRecordType(const ibMetaID& id) const {
		return id == (*m_propertyAttributeRecordType)->GetMetaID();
	}

	///////////////////////////////////////////////////////////////////

	ibRegisterType GetRegisterType() const {
		return m_propertyRegisterType->GetValueAsEnum();
	}

	// The totals table — a DIFFERENT one per register kind, `_T` for balances and `_Tn` for
	// turnovers, because the two hold genuinely different shapes: a balance register keeps receipt
	// and expense apart, a turnover-only register has no second side at all.
	wxString GetRegisterTableNameDB(ibRegisterType rType) const {
		wxASSERT(m_metaId != 0);
		return wxString::Format(rType == ibRegisterType::eBalances ? wxT("%s%i_T") : wxT("%s%i_Tn"),
			GetClassName(), GetMetaID());
	}

	wxString GetRegisterTableNameDB() const { return GetRegisterTableNameDB(GetRegisterType()); }

	// ============================================================================
	// The totals table AS A METAOBJECT. It carries no data and declares nothing — it exists to hold
	// an IDENTITY, which is exactly what a derived table lacked.
	//
	// The schema differ matches a table by id and never by name, so the totals table needs an id that
	// belongs to no one else. It used to be derived arithmetically from the register's own metaID,
	// and that is what this replaces: metaIDs are small sequential integers, so `metaID ^ 1` was the
	// id of the NEIGHBOURING metaobject — ibSchemaSnapshot::Shared matches on id alone and hands back
	// whatever table already carries it, name ignored, so the totals declaration poured its columns
	// into an unrelated table. Moving to a high bit made the collision unreachable but kept the
	// shape: a private convention that every future totals table would need its own band of.
	//
	// A metaobject answers both questions at once. ibMetaData::GenerateNewID walks EVERY child in the
	// tree — predefined children included, since CreateMetaObjectAndSetParent really does AddChild
	// them — so the id is unique BY CONSTRUCTION rather than by a range nobody has claimed yet. And
	// it is stable across saves, because the holder property writes this object's whole node (id
	// included) inside the register's own node. A second and a third totals table (the accounting
	// register wants several) cost one more child each, not one more bit.
	//
	// Nested rather than global: it is not a metaobject anyone creates, references or sees — it is
	// part of what an accumulation register IS. It stays out of ResolveChild, so it never appears in
	// the metadata tree, in copy/paste or in the child serialization walk. And it declares no table
	// of its own: a totals table's SHAPE is a function of the register's dimensions and resources, so
	// the register declares it (accumulationRegisterSchema.cpp) and reads the identity from here.
	//
	// Held as a plain owning reference, not as a property. A property is the object inspector's road
	// — a name, a label, a category, an editor — and there is nothing here to show or to edit. What
	// is needed is the reference itself, so that is all it is.
	// ============================================================================
	class ibValueMetaObjectTotals : public ibValueMetaObject {
	public:
		ibValueMetaObjectTotals(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString,
			const wxString& comment = wxEmptyString) : ibValueMetaObject(name, synonym, comment) {
		}
		virtual ~ibValueMetaObjectTotals() {}
	};

	// TWO of them, one per register kind — and that is the point of holding the identity here rather
	// than computing it. A balance register keeps receipt and expense apart, a turnover-only one has
	// no second side at all, so the two are genuinely different tables (`_T` / `_Tn`). While one id
	// served both, switching the kind read as "the same table, renamed" and the differ emitted ALTER
	// + CREATE INDEX against a table that did not exist. With an object per kind the switch IS what
	// it always was — one table dropped, the other created empty — with no rule to state anywhere:
	// ContributeTables declares the active one, and the other id simply stops being present.
	ibValueMetaObjectTotals* GetTotalsObject(ibRegisterType rType) const {
		return rType == ibRegisterType::eBalances
			? static_cast<ibValueMetaObjectTotals*>(m_totalsBalances)
			: static_cast<ibValueMetaObjectTotals*>(m_totalsTurnovers);
	}

	ibValueMetaObjectTotals* GetTotalsObject() const { return GetTotalsObject(GetRegisterType()); }

	///////////////////////////////////////////////////////////////////

	// Balance / turnover compute — the register's OWN aggregate-query knowledge (the
	// signed SUM over the movement table, built as L2 IR). The balance / turnover
	// companion queryables (friends) call these through m_reg; each returns a RAM table
	// the L3 door reads. Mirrors the information register's ComputeSlice. Period bound:
	// balance = as-of "<="; turnover = [begin, end].
	ibQueryRamTable ComputeBalance(const ibValue& cPeriod, const ibValue& cFilter) const;
	ibQueryRamTable ComputeTurnover(const ibValue& cBegin, const ibValue& cEnd, const ibValue& cFilter) const;

	// Balance AND turnover, per period at `unit` granularity: opening / receipt / expense /
	// turnover / closing per resource. Built from TWO server-side aggregates — the balance as it
	// stood entering the interval, and the turnovers grouped by the truncated period — then rolled
	// forward in memory, because each period's opening balance is the previous period's closing.
	// That running step is inherently sequential, but it walks PERIODS (tens), not movements.
	ibQueryRamTable ComputeBalanceAndTurnover(const ibValue& cBegin, const ibValue& cEnd,
	                                          ibTotalsPeriod unit, const ibValue& cFilter) const;

	///////////////////////////////////////////////////////////////////
	//  Materialised read surface — the totals bundle and the views over it
	///////////////////////////////////////////////////////////////////

	// STRUCTURE: the movements table (base) PLUS the derived totals bundle — the totals table, the
	// triggers that keep it current, and the read views that are its public surface. Declaring it
	// here is what turns the machinery on for this register; the shape it declares is the only
	// thing L2-2 and the regenerator ever read.
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	// Physical names of the read views — the register's table plus a suffix, by convention. These
	// are what a queryable points at, so they are API the moment a configuration is applied.
	wxString GetBalanceViewName() const            { return GetPhysicalTableName() + wxT("_Balance"); }
	wxString GetTurnoverViewName() const           { return GetPhysicalTableName() + wxT("_Turnovers"); }
	wxString GetBalanceAndTurnoverViewName() const { return GetPhysicalTableName() + wxT("_BalanceAndTurnovers"); }

	// The granularity totals are STORED at — NOT the periodicity of a reading, which is a QUERY
	// parameter (the caller asks for daily / weekly / monthly rows, or for none at all and gets the
	// movements). This is the floor under those readings: a projection is derivable only into a unit
	// no finer than what is stored, and everything below the floor is answered from the movement
	// table anyway, which is also where per-recorder and per-record granularity comes from.
	//
	// Day, and not as a placeholder: it is where compression is still large (a key usually sees many
	// movements a day) while the coverage includes everything a totals reading is actually asked.
	// One place, read by the schema declaration and by the view source alike, so the columns a view
	// HAS and the columns a reader EXPECTS cannot drift apart.
	ibTotalsPeriod GetTotalsPeriodUnit() const { return ibTotalsPeriod::Day; }

	// Is this register's totals row split across shards? A schema question, answered by the
	// designer's switch — turning it on or off changes the totals KEY, so it takes effect through
	// an Apply, and the regenerator rebuilds the table because the key shape changed.
	bool IsTotalsSplitEnabled() const { return m_propertySplitTotals->GetValueAsBoolean(); }

	// How many shards a split register uses. Fixed rather than configurable: the number that
	// matters is "more than one", and every extra shard is paid on every read of every row. Eight
	// spreads the writers wide enough that collisions become rare, without making the read fold
	// noticeable. An engine that cannot hash a connection id (SQLite) collapses this to one.
	static constexpr unsigned int kTotalsShardCount = 8;

	// Which columns a read surface exposes. Named rather than a pair of flags because there are
	// three shapes and they are not independent — and because naming a column a surface does not
	// have is how a reader gets a silent empty value instead of an error.
	enum class ibViewShape
	{
		Balance,              // dimensions + <res>_Balance            — no period, folded over all of it
		Turnovers,            // period (+ coarser projections) + dimensions + receipt / expense / turnover
		BalanceAndTurnovers   // dimensions + opening / receipt / expense / turnover / closing, one row per key
	};

	// A read surface as an ORDINARY DB SOURCE. ibDbTempTableQueryable already is exactly that — a
	// named physical relation with generic columns, read by the standard physical provider — so
	// none of these needs a queryable class of its own. Built on demand and cached: the shape
	// depends only on the register's metadata, and surfaces are read far more often than metadata
	// changes.
	//
	// BalanceAndTurnovers has no VIEW behind it — it is a query over the turnovers view — but it
	// still needs a column description, and it is the same kind of description. `viewName` is then
	// only a cache key.
	const ibBackendQueryable* GetViewQueryable(const wxString& viewName, ibViewShape shape) const;

	// Is the materialised surface usable? False when the driver cannot maintain derived state
	// (ODBC) — the register then answers from live aggregation, correct at any scale and only
	// slower. Every reader must ask, because the answer decides which path it takes.
	bool HasMaterializedViews() const;

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
			array.push_back(m_propertyAttributeRecordType->GetMetaObject());
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

	// SPLIT TOTALS — spread one logical totals row across several physical ones, so concurrent
	// posters stop queueing on the same row. OFF by default, and deliberately a switch rather than
	// anything automatic: the benefit is local (it relieves the HOT key) while the cost is global
	// (every read of every row sums the shards), so only someone who has profiled the register
	// knows whether it pays. Nothing in the metadata can tell where the contention is.
	//
	// (An information register has no counterpart: it holds a slice, not accumulated sums, so there
	// is nothing to split. An accounting register will carry this same switch.)
	ibPropertyBoolean* m_propertySplitTotals = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("SplitTotals"), _("Split totals"), false);

	// The two totals tables — held for their IDENTITY (see ibValueMetaObjectTotals above). Predefined
	// children: created with the register in its constructor, pinned to it for life, serialized as
	// sub-nodes of the register's own node. Which of the two is declared follows the register kind,
	// so switching the kind is a DROP of one and a CREATE of the other rather than an ALTER of
	// something that was never there.
	ibValuePtr<ibValueMetaObjectTotals> m_totalsBalances;
	ibValuePtr<ibValueMetaObjectTotals> m_totalsTurnovers;

	ibPropertyContainer<>* m_propertyAttributeRecordType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("RecordType"), _("Record type"), wxEmptyString, g_enumRecordTypeCLSID, false, ibValueEnumAccumulationRegisterRecordType::CreateDefEnumValue()));

	friend class ibBalanceQueryable;
	friend class ibTurnoverQueryable;

	friend class ibMetaData;

	// L4 custom virtual-table descriptors — registered alongside the base records descriptor on
	// run, dropped on close. Reached as AccumulationRegister.<Name>.Balance / .Turnovers.
	ibAccumRegisterBalanceDescriptor  m_balance { this };
	ibAccumRegisterTurnoverDescriptor m_turnover{ this };
	ibAccumRegisterBalanceAndTurnoverDescriptor m_balanceAndTurnover{ this };

	// Cached view sources, keyed by view name. Mutable: building one is pure derivation from
	// metadata, so a const read may fill the cache without the register being logically modified.
	mutable std::map<wxString, std::unique_ptr<ibDbTempTableQueryable>> m_viewSources;
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

// ============================================================================
// The three virtual tables. Each has TWO backings and one observable:
//
//   materialised — the source IS a DERIVED TABLE over the totals view. The parameters (the date,
//                  the interval, the dimension filter) travel INSIDE that subquery, so the
//                  selection happens there, before the outer query engine sees anything. A join to
//                  a catalog is then an ordinary SQL join, and only matching rows ever leave the
//                  server. This is the path that matters: reading balances is the most
//                  latency-critical operation in the system.
//   live         — no materialised surface (ODBC): the rows are computed in RAM from the
//                  movements. Correct at any scale, only slower — and the oracle a parity test
//                  measures the other path against.
//
// Which one is in use is invisible upstream: the same columns, the same rows, the same numbers.
// ============================================================================

// balance — resource balances as of a date.
class BACKEND_API ibBalanceQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectAccumulationRegister> {
public:
	ibBalanceQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                   const ibValue& period = ibValue(), const ibValue& filter = ibValue())
		: ibComputedRegisterQueryable(reg), m_period(period), m_filter(filter) {}

	virtual bool IsComputedInRam() const override { return !m_reg->HasMaterializedViews(); }
	virtual ibBackendQueryProvider& GetProvider() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
	virtual const ibBackendQueryable* NavigationSource() const override;

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

	virtual bool IsComputedInRam() const override { return !m_reg->HasMaterializedViews(); }
	virtual ibBackendQueryProvider& GetProvider() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
	virtual const ibBackendQueryable* NavigationSource() const override;

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue m_begin;
	ibValue m_end;
	ibValue m_filter;
};

// balance AND turnover — per period over [begin, end], rolled up to `unit`. Reports, per resource,
// the opening balance / receipt / expense / turnover / closing balance. Not a third stored shape:
// it is the balance readings and the turnover reading presented side by side, which is exactly why
// it needs no storage of its own.
class BACKEND_API ibBalanceAndTurnoverQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectAccumulationRegister> {
public:
	// `unitGiven` false = no periodicity was asked for: report ONE row per key over the whole
	// interval. True = break the interval into periods of `unit`.
	ibBalanceAndTurnoverQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                              const ibValue& begin = ibValue(), const ibValue& end = ibValue(),
	                              ibTotalsPeriod unit = ibTotalsPeriod::Month, bool unitGiven = false,
	                              const ibValue& filter = ibValue())
		: ibComputedRegisterQueryable(reg), m_begin(begin), m_end(end), m_unit(unit),
		  m_unitGiven(unitGiven), m_filter(filter) {}

	// The server path answers the UNPERIODISED question — one row per key, opening / turnover /
	// closing over the whole interval — in a single grouped pass.
	//
	// A PERIODISED reading is a different computation: each period opens where the previous one
	// closed, which is a running balance the conditional sums cannot express. It is served by the
	// live path, which folds the periods explicitly. Routing it there is not a limitation quietly
	// admitted — it is the alternative to accepting the parameter and silently ignoring it, which
	// is what this did before and which would have returned a plausible, wrong single row.
	virtual bool IsComputedInRam() const override { return m_unitGiven || !m_reg->HasMaterializedViews(); }
	virtual ibBackendQueryProvider& GetProvider() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
	virtual const ibBackendQueryable* NavigationSource() const override;

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue        m_begin;
	ibValue        m_end;
	ibTotalsPeriod m_unit;     // the READ granularity — a query parameter, not a schema property
	bool           m_unitGiven;
	ibValue        m_filter;
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

inline ibAccumRegisterBalanceAndTurnoverDescriptor::~ibAccumRegisterBalanceAndTurnoverDescriptor() = default;

inline wxString ibAccumRegisterBalanceAndTurnoverDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_reg->GetClassType());
}

inline wxString ibAccumRegisterBalanceAndTurnoverDescriptor::GetName() const
{
	return m_reg->GetName() + wxT(".BalanceAndTurnovers");
}

inline const ibBackendQueryable* ibAccumRegisterBalanceAndTurnoverDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray)
{
	// (begin, end, periodicity, filter). Periodicity is the READ granularity and rides here rather
	// than on the metaobject — one register serves monthly, weekly and quarterly readings of the
	// same data with no schema change. Absent = Month.
	const ibValue begin  = (lSizeArray > 0 && paParams != nullptr && paParams[0] != nullptr) ? *paParams[0] : ibValue();
	const ibValue end    = (lSizeArray > 1 && paParams != nullptr && paParams[1] != nullptr) ? *paParams[1] : ibValue();
	const ibValue period = (lSizeArray > 2 && paParams != nullptr && paParams[2] != nullptr) ? *paParams[2] : ibValue();
	const ibValue filter = (lSizeArray > 3 && paParams != nullptr && paParams[3] != nullptr) ? *paParams[3] : ibValue();

	// Whether a periodicity was GIVEN is as meaningful as its value: absent means "one row per key
	// over the whole interval", which is a different computation, not a default granularity.
	ibTotalsPeriod unit = ibTotalsPeriod::Month;
	bool unitGiven = false;
	if (period.GetType() == TYPE_NUMBER) {
		const long n = period.GetInteger();
		if (n >= static_cast<long>(ibTotalsPeriod::Second) && n <= static_cast<long>(ibTotalsPeriod::Year)) {
			unit = static_cast<ibTotalsPeriod>(n);
			unitGiven = true;
		}
	}

	m_companion = std::make_unique<ibBalanceAndTurnoverQueryable>(m_reg, begin, end, unit, unitGiven, filter);
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