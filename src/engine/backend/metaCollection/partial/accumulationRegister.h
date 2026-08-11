#ifndef __ACCUMULATION_REGISTER_H__
#define __ACCUMULATION_REGISTER_H__

#include "commonObject.h"
#include "accumulationRegisterEnum.h"
#include "backend/stringUtils.h"   // CompareString — the one case-insensitive name comparison
#include "backend/query/queryable.h"          // ibComputedRegisterQueryable<TReg> — shared base for the balance / turnover virtual tables
#include "backend/query/tempTableQueryable.h" // ibDbTempTableQueryable — a named physical relation; what a VIEW is to L3
// The register-shared lowering: ibRegFilterPredicate / ibRegFlatLeaves / ibRegCompositeIR. Shared on
// purpose — an information register filters its dimensions by exactly the same rule, and an
// accounting register will too; a copy per register is how three registers come to disagree about
// what a filter is.
#include "backend/metaCollection/partial/registerQueryLowering.h"

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
	// WHAT COLUMNS THIS TABLE HAS, asked without running it — the catalogue of a query
	// constructor, and any other reader that wants the shape rather than the rows. Answered
	// from the VIEW's shape, which is metadata-only, so no companion is built and no database
	// is touched. (Left unimplemented, the base returns nothing, and a virtual table shows in
	// the tree as a childless leaf — visible, addable, and with no field to select.)
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
	// Balance(moment, condition) — see the bodies at the bottom of this header.
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectAccumulationRegister* m_reg;
	// (no companion member: the base owns what MakeCompanion builds — queryableFactory.h)
};

class ibAccumRegisterTurnoverDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibAccumRegisterTurnoverDescriptor(ibValueMetaObjectAccumulationRegister* reg) : m_reg(reg) {}
	~ibAccumRegisterTurnoverDescriptor() override;
	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
	// …AND WITH THE CALL'S ARGUMENTS, because the periodicity decides which columns exist.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
	                        const std::vector<ibValue>& args) const override;
	// Turnovers(<begin>, <end>, <periodicity>, <condition>) — the interval, how finely it is read,
	// and what to fold over.
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectAccumulationRegister* m_reg;
	// (no companion member — see the balance descriptor above)
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
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
	                        const std::vector<ibValue>& args) const override;
	// BalanceAndTurnovers(<begin>, <end>, <periodicity>, <condition>) — the interval, the granularity
	// the rows roll up to, and what to fold over.
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
private:
	ibValueMetaObjectAccumulationRegister*          m_reg;
	// (no companion member — see the balance descriptor above)
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
	ibQueryRamTable ComputeBalance(const ibValue& cPeriod, const ibQueryPredicatePtr& cFilter) const;
	// ⭐ THE PERIODICITY IS THE GROUPING KEY OF THE FOLD, not a filter applied after it. `unitGiven`
	// false = one row per key over the whole interval; true = one row per key AND per period of
	// `unit`, the period travelling out as a `Period` column. Any unit is legitimate here because
	// this reads the MOVEMENTS, which carry the real instant — the stored-totals floor limits the
	// view's projections, not this.
	ibQueryRamTable ComputeTurnover(const ibValue& cBegin, const ibValue& cEnd, const ibQueryPredicatePtr& cFilter,
	                                ibTotalsPeriod unit = ibTotalsPeriod::Month, bool unitGiven = false) const;

	// Balance AND turnover, per period at `unit` granularity: opening / receipt / expense /
	// turnover / closing per resource. Built from TWO server-side aggregates — the balance as it
	// stood entering the interval, and the turnovers grouped by the truncated period — then rolled
	// forward in memory, because each period's opening balance is the previous period's closing.
	// That running step is inherently sequential, but it walks PERIODS (tens), not movements.
	ibQueryRamTable ComputeBalanceAndTurnover(const ibValue& cBegin, const ibValue& cEnd,
	                                          ibTotalsPeriod unit, const ibQueryPredicatePtr& cFilter) const;

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
	// A BUILT VIEW AND THE SHAPE IT WAS BUILT FROM. The count is the register's dimensions plus its
	// resources at build time: a cached view whose register has grown (or was asked for before it had
	// been read at all) is rebuilt rather than handed out stale. See GetViewQueryable.
	struct ibRegisterViewCache
	{
		std::unique_ptr<ibDbTempTableQueryable> m_view;
		wxString                                m_builtFrom;   // names + types, in order
	};
	mutable std::map<wxString, ibRegisterViewCache> m_viewSources;
	// Views replaced by a rebuild. Kept because a reader may still hold a pointer into one — the same
	// lifetime rule the call-scoped companions follow.
	mutable std::vector<std::unique_ptr<ibDbTempTableQueryable>> m_retiredViews;
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

// ⭐⭐ ONE TOTALS COMPANION, THREE READINGS.
//
// Balance, Turnovers and BalanceAndTurnovers are not three kinds of table. They are one virtual table
// over one register, read three ways — and what actually differs between them is exactly two things:
// WHICH SHAPE each publishes, and WHAT it computes. Everything else (which provider vends the rows,
// which columns a query may name) was written out three times, and three copies of one answer is how
// they came to disagree: the navigation swung with the road on one of them and not on the others.
//
// So the shape is DECLARED by each reading and the shared answers live here, once.
class BACKEND_API ibAccumulationTotalsQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectAccumulationRegister> {
public:
	using ibViewShape = ibValueMetaObjectAccumulationRegister::ibViewShape;

	ibAccumulationTotalsQueryable(const ibValueMetaObjectAccumulationRegister* reg, ibViewShape shape)
		: ibComputedRegisterQueryable(reg), m_shape(shape) {}

	// ⚠ THE OUTPUT SHAPE, NOT THE READ SOURCE — and the two are genuinely different. A balance is
	// FOLDED OUT OF the turnovers surface, so it reads one view and publishes another; that is why
	// each reading still names its own source in GetSourceRelation while the shape it publishes is
	// declared once, here.
	//
	// Metadata only: the view queryable is built from the register's dimensions and resources without
	// touching a database, so it is the right answer on the materialised road AND the live one.
	virtual const ibBackendQueryable* NavigationSource() const override;
	// Materialised => the ordinary PHYSICAL provider, so the source behaves like any relation.
	virtual ibBackendQueryProvider& GetProvider() const override;

protected:
	ibViewShape m_shape;
};

// balance — resource balances as of a date.
class BACKEND_API ibBalanceQueryable : public ibAccumulationTotalsQueryable {
public:
	ibBalanceQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                   const ibValue& period = ibValue(), const ibQueryPredicatePtr& filter = nullptr)
		: ibAccumulationTotalsQueryable(reg, ibViewShape::Balance), m_period(period), m_filter(filter) {}

	virtual bool IsComputedInRam() const override { return !m_reg->HasMaterializedViews(); }
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue m_period;   // as-of date
	ibQueryPredicatePtr m_filter;   // the condition, already converted (ibRegFilterPredicate)
};

// turnover — resource turnovers (and receipts / expenses) over [begin, end].
class BACKEND_API ibTurnoverQueryable : public ibAccumulationTotalsQueryable {
public:
	// `unitGiven` false = no periodicity was asked for: ONE row per key over the whole interval.
	// True = a row per key AND per period of `unit` — the same shape BalanceAndTurnovers reports,
	// with only the turnover side of it.
	ibTurnoverQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                    const ibValue& begin = ibValue(), const ibValue& end = ibValue(),
	                    const ibQueryPredicatePtr& filter = nullptr,
	                    ibTotalsPeriod unit = ibTotalsPeriod::Month, bool unitGiven = false)
		: ibAccumulationTotalsQueryable(reg, ibViewShape::Turnovers),
		  m_begin(begin), m_end(end), m_filter(filter),
		  m_unit(unit), m_unitGiven(unitGiven) {}

	// The materialised surface holds turnovers at the STORED grain, so an unperiodised read IS the
	// view. A PERIODISED one folds over a different key and is served by the live path, which groups
	// by the truncated period — the same routing BalanceAndTurnovers makes, and for the same reason:
	// accepting the parameter and answering at the wrong grain would return a plausible wrong number.
	//
	// Safe to route now because a companion navigates through its VIEW'S SHAPE on both roads
	// (NavigationSource) and the RAM table is seeded from that same shape — so the columns a query
	// names are there whichever road the read takes.
	virtual bool IsComputedInRam() const override { return m_unitGiven || !m_reg->HasMaterializedViews(); }
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue        m_begin;
	ibValue        m_end;
	ibQueryPredicatePtr m_filter;
	ibTotalsPeriod m_unit;       // the READ granularity — a query parameter, not a schema property
	bool           m_unitGiven;
};

// balance AND turnover — per period over [begin, end], rolled up to `unit`. Reports, per resource,
// the opening balance / receipt / expense / turnover / closing balance. Not a third stored shape:
// it is the balance readings and the turnover reading presented side by side, which is exactly why
// it needs no storage of its own.
class BACKEND_API ibBalanceAndTurnoverQueryable : public ibAccumulationTotalsQueryable {
public:
	// `unitGiven` false = no periodicity was asked for: report ONE row per key over the whole
	// interval. True = break the interval into periods of `unit`.
	ibBalanceAndTurnoverQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                              const ibValue& begin = ibValue(), const ibValue& end = ibValue(),
	                              ibTotalsPeriod unit = ibTotalsPeriod::Month, bool unitGiven = false,
	                              const ibQueryPredicatePtr& filter = nullptr)
		: ibAccumulationTotalsQueryable(reg, ibViewShape::BalanceAndTurnovers),
		  m_begin(begin), m_end(end), m_unit(unit),
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
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue        m_begin;
	ibValue        m_end;
	ibTotalsPeriod m_unit;     // the READ granularity — a query parameter, not a schema property
	bool           m_unitGiven;
	ibQueryPredicatePtr m_filter;
};

// --- L4 descriptor method bodies (the register + balance / turnover companions are complete) ---

// THE SHAPE OF A VIRTUAL TABLE, for the three descriptors at once. A balance / turnover table
// exposes the columns of its VIEW, and that view's column set is built from the register's own
// dimensions and resources (accumulationRegisterSchema.cpp) and cached — metadata only, so this
// answer costs nothing and works on a base that has never been opened. Deliberately NOT the
// companion's GetColumns(): in RAM mode a companion navigates through the register itself and
// would report the MOVEMENT columns, which is the one answer that would mislead here.
// ⭐⭐ THE PERIODICITY DECIDES WHICH COLUMNS EXIST, and this is the rule, written where the columns
// are handed out:
//
//     empty / Auto   every projection the table can make — Period, PeriodSecond … PeriodYear,
//                    and (once the movements path exists) Recorder and LineNumber. Nothing has been
//                    decided, so everything is on offer and the author picks.
//     Period         one column: the period itself.
//     a unit         one column: the period, rolled to that unit. Months asked for, months given —
//                    the finer projections are not part of that reading and showing them would
//                    promise rows the query will not return.
//     Recorder       the period and the document it came from.
//     Record         the period, the document, and the line within it.
//
// `unit` empty means "not decided" — including an argument written as a parameter, whose value only
// exists at run time. That is deliberately the same case as Auto: the shape a query is drawn against
// must be the widest one it might turn out to have, never a guess at which.
inline bool ibRegisterViewColumnFits(const wxString& columnName, const wxString& periodName,
	const wxString& unit)
{
	// Not a period projection (a dimension, a resource) — always there, whatever the granularity.
	if (!columnName.StartsWith(periodName))
		return true;

	// ⚠ NOTHING ASKED FOR MEANS NO PERIOD AT ALL — not "the period, undecided". Left out, this table
	// reads the interval WHOLE: one row per key, begin to end, with no date on it. That is what the
	// engine already does (no unit given → no grouping by period), and the column list has to say the
	// same thing. Showing a `Period` column over a reading that has no period is the window promising
	// a value the rows will not carry.
	if (unit.IsEmpty())
		return false;

	// AUTO is the opposite: nothing has been DECIDED, so every projection the table can make is on
	// offer and the author picks one.
	if (stringUtils::CompareString(unit, wxT("Auto")))
		return true;

	// Anything else names ONE granularity, and the reading then has ONE period column — `Period`,
	// rolled to it. The coarser projections are not part of that reading.
	return stringUtils::CompareString(columnName, periodName);
}

inline void ibFillExplorerFromRegisterView(const ibValueMetaObjectAccumulationRegister* reg,
	const wxString& viewName, ibValueMetaObjectAccumulationRegister::ibViewShape shape,
	ibSourceDataObject::ibSourceExplorer& explorer, const wxString& unit = wxEmptyString)
{
	if (reg == nullptr)
		return;
	const ibBackendQueryable* view = reg->GetViewQueryable(viewName, shape);
	if (view == nullptr)
		return;

	// ⭐⭐ A DIMENSION IN A VIEW IS THE DIMENSION, not a copy of it.
	//
	// The view's columns are built as plain (name, type, id) triples, which is right for the ones
	// that are genuinely DERIVED — `PeriodMonth`, `Resource1Turnover`: nothing in the metadata stands
	// behind those. It is wrong for a dimension: that column IS the register's own attribute, kept
	// deliberately under its metaID so a read reaches it exactly as on the movements table.
	//
	// Handed over as a synthetic triple it lost everything the attribute knows: its own picture, and
	// — the visible half — the fact that it holds a REFERENCE. A dimension typed as a reference could
	// not be unfolded in the catalogue, while the same dimension one node up, on the register itself,
	// unfolded fine. Two answers to "what is this column" from one column.
	//
	// So the metaobject is handed over where there is one. The id is the key, because the id is
	// exactly what the view builder promised to keep.
	const auto attributeById = [reg](const ibMetaID& id) -> const ibValueMetaObjectAttributeBase* {
		if (id == 0)
			return nullptr;
		if (reg->GetRegisterPeriod() != nullptr && reg->GetRegisterPeriod()->GetMetaID() == id)
			return reg->GetRegisterPeriod();
		for (const ibValueMetaObjectAttributeBase* dimension : reg->GetDimensionArrayObject())
			if (dimension != nullptr && dimension->GetMetaID() == id) return dimension;
		for (const ibValueMetaObjectAttributeBase* resource : reg->GetResourceArrayObject())
			if (resource != nullptr && resource->GetMetaID() == id) return resource;
		return nullptr;
	};

	const wxString periodName = reg->GetRegisterPeriod() != nullptr
		? reg->GetRegisterPeriod()->GetName() : wxString();

	for (const ibBackendQueryColumn* col : view->GetColumns()) {
		if (col == nullptr)
			continue;
		if (!periodName.IsEmpty() && !ibRegisterViewColumnFits(col->GetName(), periodName, unit))
			continue;
		if (const ibValueMetaObjectAttributeBase* attribute = attributeById(col->GetColumnId()))
			explorer.AppendColumn(attribute, /*enabled*/ true, /*visible*/ true);
		else
			explorer.AppendColumn(col);
	}
}

// Reads the periodicity argument (the word the constructor wrote, or a raw unit number). DEFINED
// further down, beside the parameter it is the other half of — declared here because the turnover
// descriptor below reads its argument before that point.
inline void ibReadRegisterPeriodicity(const ibValue& given, ibTotalsPeriod& unit, bool& unitGiven);

// The word a unit is written as — the same spelling the parameter offers and the view's projection
// columns are named after, so a message can name a column and be right about it.
// ⭐⭐ THE UNIT VOCABULARY, WRITTEN ONCE.
//
// The same ten words were in three places: a `switch` here, a table in the schema builder (which
// spells the view's projection columns), and a hand-written list of choices in DescribeParameters.
// Three copies of one dictionary, and each reader of a word would have gone on compiling while the
// other two changed — the drift would have shown up as a column nobody could name.
//
// ⚠ The ORDER matters and is not alphabetical: it is coarseness, ascending. The schema offers only
// the projections COARSER than what the totals store (`u > GetTotalsPeriodUnit()`) — an hour cannot
// be recovered from a day already summed — and that comparison is on the enum, so the enum's order
// is the meaning. Keep new units in their place on that scale.
inline const std::vector<std::pair<ibTotalsPeriod, wxString>>& ibRegisterUnits()
{
	static const std::vector<std::pair<ibTotalsPeriod, wxString>> s_units = {
		{ ibTotalsPeriod::Second,   wxT("Second")   }, { ibTotalsPeriod::Minute,   wxT("Minute")   },
		{ ibTotalsPeriod::Hour,     wxT("Hour")     }, { ibTotalsPeriod::Day,      wxT("Day")      },
		{ ibTotalsPeriod::Week,     wxT("Week")     }, { ibTotalsPeriod::TenDays,  wxT("TenDays")  },
		{ ibTotalsPeriod::Month,    wxT("Month")    }, { ibTotalsPeriod::Quarter,  wxT("Quarter")  },
		{ ibTotalsPeriod::HalfYear, wxT("HalfYear") }, { ibTotalsPeriod::Year,     wxT("Year")     },
	};
	return s_units;
}

// The word a unit is written as — a lookup in that one table, not a second switch over it.
inline wxString ibRegisterUnitWord(ibTotalsPeriod unit)
{
	for (const std::pair<ibTotalsPeriod, wxString>& u : ibRegisterUnits())
		if (u.first == unit)
			return u.second;
	return wxString();
}

// ⭐⭐ THE ARGUMENTS HAVE NAMES, AND THE NAMES ARE THESE.
//
// A virtual table's call is read by POSITION, and the positions differ per table — Balance takes two,
// Turnovers four, BalanceAndTurnovers five. Written as `paParams[2]` at the callsite, that is a
// number nobody can check: when the periodicity was declared, it took slot 2 and pushed the filter to
// 3, and the code kept reading the filter out of 2 — handing the source a WORD where a filter belongs
// and losing the filter entirely. It compiled, it ran, and it was silently wrong.
//
// Naming them puts the order in ONE place, beside DescribeParameters which declares the same order to
// the outside. Two lists that must agree, kept adjacent, is the least a positional call can ask for.
namespace ibRegBalanceArg   { enum { Period = 0, Filter = 1, Count }; }
namespace ibRegTurnoverArg  { enum { Begin = 0, End = 1, Filter = 2, Periodicity = 3, Count }; }
namespace ibRegBalTurnArg   { enum { Begin = 0, End = 1, Filter = 2, Periodicity = 3, FillMethod = 4, Count }; }

// ⭐ AND THE ONE RULE ACROSS ALL THREE: PERIOD, THEN CONDITION, THEN THE REFINEMENTS.
//
// Every table asks for its period first — a moment, or an interval. Then the condition, because
// filtering is what a reader almost always wants and almost never wants to count commas to reach.
// The periodicity and the fill method come LAST precisely because they are the ones nobody states as
// a rule: an argument that is usually left out belongs where leaving it out costs nothing.
//
// A new parameter is therefore appended AFTER the refinements or inserted among them — never before
// the condition, which would silently shift every existing call's filter into a slot that means
// something else. That is exactly how the periodicity once pushed the filter aside and nobody noticed.
//
// Stated to the compiler rather than to the reader, because a convention that is only written down is
// a convention that gets broken by the person who did not read it. `DescribeParameters` declares this
// same order to the outside world and sits a few lines below — the two lists must agree.
static_assert(ibRegBalanceArg::Filter  == ibRegBalanceArg::Count - 1,  "the condition follows the period");
static_assert(ibRegTurnoverArg::Filter == ibRegTurnoverArg::Begin + 2, "the condition follows the period");
static_assert(ibRegBalTurnArg::Filter  == ibRegBalTurnArg::Begin + 2,  "the condition follows the period");
static_assert(ibRegTurnoverArg::Periodicity > ibRegTurnoverArg::Filter, "refinements come last");
static_assert(ibRegBalTurnArg::Periodicity  > ibRegBalTurnArg::Filter,  "refinements come last");
static_assert(ibRegBalTurnArg::FillMethod   > ibRegBalTurnArg::Filter,  "refinements come last");

// ONE reader for a slot: present, non-null, in range — or an empty value, which every reading below
// treats as "not asked for".
// ⭐⭐ THE FIGURE SUFFIXES, IN ONE PLACE.
//
// A view builds its columns as `<Resource>` + a suffix; the readings spell the same word again to
// aggregate them; the manager spells it a third time to name a column in the value table it returns.
// Three spellings of one name, and the day one of them changes the other two go on compiling and
// answer with nothing — which is how `Resource1_Turnover` and `Resource1Turnover` came to be two
// different columns for one figure.
//
// ⚠ The PERIOD is deliberately NOT here. Its column is named after the register's own period
// attribute (`GetRegisterPeriod()->GetName()`), so the name belongs to the metadata, not to this
// list — writing `"Period"` as a literal works only for a register whose attribute happens to be
// called that.
namespace ibRegFigure {
	inline constexpr const wxChar* Turnover       = wxT("Turnover");
	inline constexpr const wxChar* Receipt        = wxT("Receipt");
	inline constexpr const wxChar* Expense        = wxT("Expense");
	inline constexpr const wxChar* Balance        = wxT("Balance");
	inline constexpr const wxChar* OpeningBalance = wxT("OpeningBalance");
	inline constexpr const wxChar* ClosingBalance = wxT("ClosingBalance");
}

inline ibValue ibRegArg(ibValue** paParams, long lSizeArray, int slot)
{
	return (paParams != nullptr && slot < lSizeArray && paParams[slot] != nullptr)
		? *paParams[slot] : ibValue();
}

// ⚠ The filter converter and the leaf walker moved to registerQueryLowering.h — they are the SAME
// question for every register (an information register filters its dimensions exactly as this one
// does), and a copy per register is how two registers come to disagree about what a filter is.

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
	const ibValue period = ibRegArg(paParams, lSizeArray, ibRegBalanceArg::Period);
	// The runtime value becomes the condition right here, at the door — everything below sees a predicate.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_reg, ibRegArg(paParams, lSizeArray, ibRegBalanceArg::Filter));
	// Built and KEPT by the base — the same call gives the same object back, and a query that reads
	// this table twice keeps both alive (queryableFactory.h, MakeCompanion).
	return MakeCompanion<ibBalanceQueryable>(paParams, lSizeArray, m_reg, period, filter);
}

inline void ibAccumRegisterBalanceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	ibFillExplorerFromRegisterView(m_reg, m_reg->GetBalanceViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Balance, explorer);
}

// Balance(<moment>, <condition>) — the simplest shape there is: WHEN, and OVER WHAT.
inline void ibAccumRegisterBalanceDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	ibQuerySourceParameter moment;
	moment.m_name = wxT("Period");
	// The register's OWN period type, not a hand-written "date": asked of the attribute, so a
	// register that ever dates its rows differently needs nothing changed here.
	if (m_reg != nullptr && m_reg->GetRegisterPeriod() != nullptr)
		moment.m_type = m_reg->GetRegisterPeriod()->GetTypeDesc();
	out.push_back(moment);

	ibQuerySourceParameter condition;
	condition.m_name      = wxT("Condition");
	condition.m_condition = true;
	out.push_back(condition);
}

// A BALANCE IS FILTERED BY ITS DIMENSIONS, never by a resource. The resource is what the table
// folded — filtering by it would ask the engine to select on a sum it has not computed yet, and the
// honest place for that question is a condition over the RESULT, not an argument of the source.
inline void ibAccumRegisterBalanceDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_reg == nullptr)
		return;
	for (const ibValueMetaObjectAttributeBase* dimension : m_reg->GetDimensionArrayObject())
		if (dimension != nullptr)
			explorer.AppendColumn(dimension, /*enabled*/ true, /*visible*/ true);
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
	const ibValue begin  = ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::Begin);
	const ibValue end    = ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::End);
	const ibValue period = ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::Periodicity);
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_reg, ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::Filter));

	// ⭐ THE PERIODICITY IS THE GROUPING KEY OF THE FOLD. Nothing = the interval read WHOLE, one row
	// per key; a unit = a row per key AND per period, the period travelling out as `Period`.
	//
	// This used to REFUSE and hand the reader the query to write by hand ("select PeriodMonth and
	// group by it") — true advice, and still a boundary standing in front of a table that could
	// plainly do what was asked. The fold is the one BalanceAndTurnovers already performs, and the
	// parameter reaches it now.
	//
	// No floor applies. The stored totals are kept per day and that limits the VIEW's period
	// projections, but a periodised read is served from the MOVEMENTS, which carry the real instant —
	// so an hour is as answerable as a month.
	ibTotalsPeriod unit = ibTotalsPeriod::Month;
	bool unitGiven = false;
	ibReadRegisterPeriodicity(period, unit, unitGiven);

	return MakeCompanion<ibTurnoverQueryable>(paParams, lSizeArray, m_reg, begin, end, filter, unit, unitGiven);
}

inline void ibAccumRegisterTurnoverDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	ibFillExplorerFromRegisterView(m_reg, m_reg->GetTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers, explorer);
}

// THE PERIODICITY IS THE THIRD ARGUMENT — (begin, end, periodicity, condition). Read as the word the
// constructor wrote; anything else (a parameter, nothing at all) leaves it empty, which means "not
// decided" and shows every projection.
inline wxString ibRegisterPeriodicityWord(const std::vector<ibValue>& args)
{
	return args.size() > 2 && args[2].GetType() == TYPE_STRING ? args[2].GetString() : wxString();
}

inline void ibAccumRegisterTurnoverDescriptor::FillSourceExplorer(
	ibSourceDataObject::ibSourceExplorer& explorer, const std::vector<ibValue>& args) const
{
	ibFillExplorerFromRegisterView(m_reg, m_reg->GetTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers, explorer,
		ibRegisterPeriodicityWord(args));
}

// THE INTERVAL A TURNOVER IS COUNTED OVER — the two ends said separately, because they are two
// different moments and a reader must be able to hand a parameter to each. The type is the
// register's OWN period type, asked of the attribute rather than written down as "a date".
inline void ibFillRegisterIntervalParameters(const ibValueMetaObjectAccumulationRegister* reg,
	std::vector<ibQuerySourceParameter>& out)
{
	const ibTypeDescription period =
		(reg != nullptr && reg->GetRegisterPeriod() != nullptr)
			? reg->GetRegisterPeriod()->GetTypeDesc() : ibTypeDescription();

	ibQuerySourceParameter begin;
	begin.m_name = wxT("BeginOfPeriod");
	begin.m_type = period;
	out.push_back(begin);

	ibQuerySourceParameter end;
	end.m_name = wxT("EndOfPeriod");
	end.m_type = period;
	out.push_back(end);
}

// AND THE CONDITION, always last and always a condition slot — the same shape a balance has, so
// one habit covers every virtual table of this register.
inline void ibAppendRegisterConditionParameter(std::vector<ibQuerySourceParameter>& out)
{
	ibQuerySourceParameter condition;
	condition.m_name      = wxT("Condition");
	condition.m_condition = true;
	out.push_back(condition);
}

// Turnovers(<begin>, <end>, <periodicity>, <condition>).
//
// ⚠ THE PERIODICITY IS DECLARED BUT NOT YET TYPED. It is a closed set of granularities (Period /
// Recorder / Record / Second … Year / Auto), which means it wants a REGISTERED ENUMERATION and a
// quick choice over it, not a box to type a number into — and, past that, it decides WHICH COLUMNS
// this table has (Recorder brings the recorder, Record brings the line number, Auto brings them
// all). Declaring it now makes the argument reachable and the window able to show it; the type and
// the column rule are the next piece of work, and they belong together.
// ⭐ THE GRANULARITY A TURNOVER ROLLS UP TO — a CLOSED set, declared by the source that reads it.
//
// `Period` is the whole interval: one row per key, which is what the table means with no granularity
// given at all. The rest cut the interval into units and give a row per unit.
//
// `Recorder` and `Record` are the two below the interval — a row per document, a row per line — and
// they are read from the MOVEMENTS rather than from the rolled-up totals, which the read path does
// not do yet. They are offered because they are part of what this table IS; asking for one gets the
// engine's own sentence about what is missing, which is worth more than a list that quietly omits
// half the answer.
// ⭐ THE PERIODICITY ARGUMENT, READ. It arrives as the WORD the constructor wrote (`"Month"`) — the
// same word `DescribeParameters` offered — or as the raw unit number a hand-written call may carry.
// `Period` and an absent argument are the same thing: the interval whole, which is why "given" is
// carried separately from the value.
//
// ⚠ `Recorder`, `Record` and `Auto` are named by the table and NOT read from the rolled-up totals:
// they need the movements, one row per document or per line. Rather than quietly rolling up to
// something else, the engine says which one was asked for and that it is not built — a wrong number
// silently returned is the one outcome a register may never produce.
inline void ibReadRegisterPeriodicity(const ibValue& given, ibTotalsPeriod& unit, bool& unitGiven)
{
	unitGiven = false;
	if (given.GetType() == TYPE_NUMBER) {
		const long n = given.GetInteger();
		if (n >= static_cast<long>(ibTotalsPeriod::Second) && n <= static_cast<long>(ibTotalsPeriod::Year)) {
			unit = static_cast<ibTotalsPeriod>(n);
			unitGiven = true;
		}
		return;
	}
	if (given.GetType() != TYPE_STRING)
		return;   // absent, or something this argument does not take: the interval whole

	// CASE-INSENSITIVE THROUGH THE ONE HELPER THE ENGINE ALREADY USES — `stringUtils::CompareString`,
	// the same one the bytecode resolver and the value system compare names by. A second spelling of
	// "the same word" is how two parts of a program start disagreeing about what a name is.
	const wxString word = given.GetString();
	if (word.IsEmpty() || stringUtils::CompareString(word, wxT("Period")))
		return;

	static const std::pair<const wxChar*, ibTotalsPeriod> kUnits[] = {
		{ wxT("Second"),   ibTotalsPeriod::Second   }, { wxT("Minute"),   ibTotalsPeriod::Minute  },
		{ wxT("Hour"),     ibTotalsPeriod::Hour     }, { wxT("Day"),      ibTotalsPeriod::Day     },
		{ wxT("Week"),     ibTotalsPeriod::Week     }, { wxT("TenDays"),  ibTotalsPeriod::TenDays },
		{ wxT("Month"),    ibTotalsPeriod::Month    }, { wxT("Quarter"),  ibTotalsPeriod::Quarter },
		{ wxT("HalfYear"), ibTotalsPeriod::HalfYear }, { wxT("Year"),     ibTotalsPeriod::Year    },
	};
	for (const auto& u : kUnits)
		if (stringUtils::CompareString(word, u.first)) { unit = u.second; unitGiven = true; return; }

	ibBackendCoreException::Error(
		_("periodicity '%s' is read from the register's movements (a row per document, per line), "
		  "which this register does not do yet: use Period or one of Second..Year"), word);
}

// ⭐ THE SAME LIST ON BOTH TABLES, because it is the same question: at what granularity is this
// interval read. Turnovers and balance-and-turnovers both take it.
//
// ⚠ I SHORTENED IT ONCE, on the reasoning that a list which offers a unit and then refuses it is
// worse than a short one. That reasoning is about the ENGINE's gap and the list is about the TABLE:
// hiding the units made the window quietly disagree with what the table is. Where the computation is
// missing the engine says so precisely, in its own words — and that is the right place for it.
inline void ibAppendRegisterPeriodicityParameter(std::vector<ibQuerySourceParameter>& out)
{
	// ⚠ A WORD, NOT A REGISTERED TYPE — and that was tried and dropped on purpose (2026-08-11).
	//
	// Turning this into an enumeration looked right by the usual rule ("a closed set is a type"), and
	// it is wrong here for a reason the rule does not cover: the value is written in the query text as
	// a KEYWORD (`…Turnovers(&From, &To, Month)`). A registered type would add a SECOND spelling of the
	// same thing — `RegisterPeriodicity.Month` beside `Month` — plus a clsid that is permanent the
	// moment any data carries it. Two spellings and an irreversible key, bought for a list of thirteen
	// words that has exactly one reader. The list IS the closed set, and the source declares it.
	ibQuerySourceParameter periodicity;
	periodicity.m_name = wxT("Periodicity");
	// The units come from the ONE vocabulary; the four that are not units are named here because
	// that is what they are — `Period` and `Auto` are answers about the shape of the reading, and
	// `Record` / `Recorder` name a movement's own identity rather than a calendar interval.
	periodicity.m_choices = { wxT("Period"), wxT("Record"), wxT("Recorder"), wxT("Auto") };
	for (const std::pair<ibTotalsPeriod, wxString>& u : ibRegisterUnits())
		periodicity.m_choices.push_back(u.second);
	// ⚠ NO DEFAULT, because there is no value that stands in for "left out". Empty is its own answer:
	// the interval read whole, no period column, one row per key. Writing `Period` in the box would
	// mean something different — a row PER PERIOD — so a default here would quietly change the query
	// the moment somebody accepted it.
	periodicity.m_default.clear();
	out.push_back(periodicity);
}

inline void ibAccumRegisterTurnoverDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	// PERIOD, CONDITION, then the refinements — see ibRegTurnoverArg for why that order.
	ibFillRegisterIntervalParameters(m_reg, out);
	ibAppendRegisterConditionParameter(out);
	ibAppendRegisterPeriodicityParameter(out);
}

// A TURNOVER IS FILTERED BY ITS DIMENSIONS, never by a resource — the same rule as the balance, and
// for the same reason: the resource is what the table folded, and a condition over a fold belongs
// to the RESULT, not to an argument of the source.
inline void ibAccumRegisterTurnoverDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_reg == nullptr)
		return;
	for (const ibValueMetaObjectAttributeBase* dimension : m_reg->GetDimensionArrayObject())
		if (dimension != nullptr)
			explorer.AppendColumn(dimension, /*enabled*/ true, /*visible*/ true);
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
	// Periodicity is the READ granularity and rides here rather than on the metaobject — one register
	// serves monthly, weekly and quarterly readings of the same data with no schema change.
	// Absent = Month. The condition is FIFTH; it was read from the fourth while the fill method was
	// undeclared, which is precisely why these slots have names now.
	const ibValue begin  = ibRegArg(paParams, lSizeArray, ibRegBalTurnArg::Begin);
	const ibValue end    = ibRegArg(paParams, lSizeArray, ibRegBalTurnArg::End);
	const ibValue period = ibRegArg(paParams, lSizeArray, ibRegBalTurnArg::Periodicity);
	const ibValue fill   = ibRegArg(paParams, lSizeArray, ibRegBalTurnArg::FillMethod);
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_reg, ibRegArg(paParams, lSizeArray, ibRegBalTurnArg::Filter));

	// ⚠ THE DEFAULT IS THE ONE WITH THE BOUNDARIES, and that is what this table produces: each row
	// carries the balance as its period opens and as it closes. Asking for MOVEMENTS ONLY is the
	// narrower reading — the one that drops those — and it is not built, so it is told rather than
	// answered with the wider shape. (Silently returning more than was asked for is as wrong as
	// returning less; it just looks harmless until somebody sums the column.)
	if (fill.GetType() == TYPE_STRING && stringUtils::CompareString(fill.GetString(), wxT("Movements")))
		ibBackendCoreException::Error(
			_("fill method 'Movements' drops the opening and closing balance rows, which this table "
			  "does not do yet: leave it out for movements and period boundaries"));

	// Whether a periodicity was GIVEN is as meaningful as its value: absent means "one row per key
	// over the whole interval", which is a different computation, not a default granularity.
	ibTotalsPeriod unit = ibTotalsPeriod::Month;
	bool unitGiven = false;
	ibReadRegisterPeriodicity(period, unit, unitGiven);

	return MakeCompanion<ibBalanceAndTurnoverQueryable>(paParams, lSizeArray,
		m_reg, begin, end, unit, unitGiven, filter);
}

inline void ibAccumRegisterBalanceAndTurnoverDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	ibFillExplorerFromRegisterView(m_reg, m_reg->GetBalanceAndTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::BalanceAndTurnovers, explorer);
}

inline void ibAccumRegisterBalanceAndTurnoverDescriptor::FillSourceExplorer(
	ibSourceDataObject::ibSourceExplorer& explorer, const std::vector<ibValue>& args) const
{
	ibFillExplorerFromRegisterView(m_reg, m_reg->GetBalanceAndTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::BalanceAndTurnovers, explorer,
		ibRegisterPeriodicityWord(args));
}

// BalanceAndTurnovers(<begin>, <end>, <periodicity>, <fill method>, <condition>) — FIVE, and the
// two in the middle are what makes this table different from the turnovers beside it.
//
// The PERIODICITY carries more weight here: it is the granularity each opening / closing balance is
// taken AT, not merely how the turnover rows are grouped.
//
// The FILL METHOD answers a question only this table has, and it is about the ENDS of the interval.
//
// The rows of this table are built from the turnovers plus a running balance, so most of them stand
// for something that actually MOVED — they have a recorder. Two do not: one fixes the balance as the
// period OPENS and one as it CLOSES. They carry no recorder because nothing happened at that moment;
// they are the statement of where the quantity stood.
//
// So the method is: movements alone, or movements AND those two boundary rows. A report that opens
// with "there were 40 in stock" and closes with "there are 12" needs them; a list of what moved does
// not, and would read as though two phantom documents had been posted.
//
// A closed set of two, so it wants a registered enumeration and a quick choice, like the periodicity;
// declared now so the argument is reachable and the window can show it.
inline void ibAccumRegisterBalanceAndTurnoverDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	// PERIOD, CONDITION, then the refinements — see ibRegBalTurnArg for why that order.
	ibFillRegisterIntervalParameters(m_reg, out);
	ibAppendRegisterConditionParameter(out);
	ibAppendRegisterPeriodicityParameter(out);

	ibQuerySourceParameter fillMethod;
	fillMethod.m_name    = wxT("FillMethod");
	fillMethod.m_choices = { wxT("MovementsAndBoundaries"), wxT("Movements") };   // a word, see the periodicity
	// ⭐ LEFT OUT, THE BOUNDARIES ARE THERE. This table exists to say where the quantity STOOD as the
	// period opened and as it closed; a reading without those is a narrower thing somebody asks for
	// on purpose, not the natural state of it. So the empty box means movements AND boundaries, and
	// the list is ordered the same way — what happens by default reads first.
	fillMethod.m_default = wxT("MovementsAndBoundaries");
	out.push_back(fillMethod);
}

inline void ibAccumRegisterBalanceAndTurnoverDescriptor::FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_reg == nullptr)
		return;
	for (const ibValueMetaObjectAttributeBase* dimension : m_reg->GetDimensionArrayObject())
		if (dimension != nullptr)
			explorer.AppendColumn(dimension, /*enabled*/ true, /*visible*/ true);
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