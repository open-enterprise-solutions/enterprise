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

// (ibRegisterFoldOffersColumn — "does a reading at this granularity produce that column" — is a
//  TEMPLATE in registerQueryLowering.h now, so it needs no forward declaration here: the include
//  above brings it in, and it is instantiated per register at the callsite.)

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
	// ⭐⭐ NAMED AFTER THE TOTALS OBJECT — same rule as the accounting register's, and for the same
	// reason. This used to spell the register's KIND into the name ("_T" for balances, "_Tn" for
	// turnovers), which made the name a function of a setting: switching the kind renamed the table,
	// the old name became unspellable, and the drop-then-create the switch relies on could no longer
	// find what it was supposed to drop.
	//
	// The two totals objects are predefined and named BalanceTotals / TurnoverTotals, so their names
	// carry the meaning without carrying the setting — and they read in a query tool, which "_Tn" does
	// not. Kept deliberately in step with AccountingRegister's DebitTotals / CreditTotals: two
	// registers naming the same kind of thing by two different rules is a difference nobody chose.
	//
	// ⚠ One-time rename for existing bases, not a migration — an old base must be re-created.
	wxString GetRegisterTableNameDB(ibRegisterType rType) const {
		wxASSERT(m_metaId != 0);
		const ibValueMetaObjectTotals* totals = GetTotalsObject(rType);
		return wxString::Format(wxT("%s%i_%s"), GetClassName(), GetMetaID(),
			totals != nullptr ? totals->GetName()
			                  : wxString(rType == ibRegisterType::eBalances ? wxT("BalanceTotals") : wxT("TurnoverTotals")));
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
	// The class itself now lives beside the register family (commonObject.h,
	// ibValueMetaObjectRegisterTotals): an accounting register needs the very same identity holder —
	// two of them, one per side — and a second class with an identical body is how two registers come
	// to differ in a detail nobody meant to change. The name stays what this register's code calls it.
	using ibValueMetaObjectTotals = ibValueMetaObjectRegisterTotals;

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
	// ⭐ THE PERIODICITY IS THE GROUPING KEY OF THE FOLD, not a filter applied after it. The fold
	// says what a row is: the whole interval, the register's own period, a calendar unit, or a
	// movement's own identity. Any calendar unit is legitimate here because this can read the
	// MOVEMENTS, which carry the real instant — the stored-totals floor limits the view's
	// projections, not this.
	ibQueryRamTable ComputeTurnover(const ibValue& cBegin, const ibValue& cEnd, const ibQueryPredicatePtr& cFilter,
	                                const ibRegFold& cFold = ibRegFold()) const;

	// Balance AND turnover, per period at `unit` granularity: opening / receipt / expense /
	// turnover / closing per resource. Built from TWO server-side aggregates — the balance as it
	// stood entering the interval, and the turnovers grouped by the truncated period — then rolled
	// forward in memory, because each period's opening balance is the previous period's closing.
	// That running step is inherently sequential, but it walks PERIODS (tens), not movements.
	ibQueryRamTable ComputeBalanceAndTurnover(const ibValue& cBegin, const ibValue& cEnd,
	                                          const ibRegFold& cFold, const ibQueryPredicatePtr& cFilter) const;

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
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

	/**
	* Property events
	*/
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	// Additive contract — RegisterData base is empty. AccumulationRegister appends its line
	// attributes: when a movement happened, whether it is in force, which way it moves, what wrote
	// it and on which line.
	//
	// ⭐ RECORD TYPE ONLY WHERE IT MEANS SOMETHING. Receipt / expense is a statement about a BALANCE:
	// it says which way the running figure moves. A turnover-only register keeps no running figure —
	// every movement is one more thing added to a period's total — so there is no way for a movement
	// to face, and offering the field would invite a filter over a distinction the register does not
	// make. (Max, 2026-08-14, deciding it explicitly: "if there are balances, then for turnovers we
	// do not output the movement kind — it makes no sense there".)
	//
	// ⚠ The column therefore comes and goes with the register kind, and a column that disappears
	// takes its data with it — the accounting register states that trap in full (accountingRegister.h,
	// the same method) and answers it by declaring all three columns always. Here the answer is
	// deliberately the other one, and the difference is that the two register kinds are different
	// TABLES rather than one table with a flag: switching the kind already drops one totals table and
	// creates the other, so the movements' record type is not the thing that makes the switch lossy.
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
	// ON by default — same reasoning as the accounting register's: concurrent posting is the ordinary
	// case, and splitting keeps two writers off the same totals row. Kept in step with it deliberately;
	// two registers differing in this by accident would be a difference nobody chose.
	ibPropertyBoolean* m_propertySplitTotals = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("SplitTotals"), _("Split totals"), true);

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
	// THE BUILT VIEWS, and the shape each was built from. The cache, the signature check and the
	// retirement of a replaced surface are `ibRegSurfaceCache` (registerQueryLowering.h) — one
	// mechanism for all three surface builders, since only the key and the columns ever differed.
	mutable ibRegSurfaceCache m_surfaces;
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

	// ⭐ WHAT MAKES ONE ROW OF TOTALS THE SAME ROW. The totals surface is asked this like any other
	// source — the upsert that maintains the table matches on it — and it must answer for ITSELF:
	// a totals row is not identified the way a movement is. A movement is recorder + line + period;
	// a total is PERIOD + DIMENSIONS, which is precisely what a total means, the folded value of
	// every movement sharing them.
	//
	// 🛑 Left unanswered, the base returned an empty list, the renderer built `MATCHING ()`, and
	// Firebird refused the statement — so a configuration that merely added a dimension could not
	// be applied at all.
	//
	// ⚠ Resources are NOT here: they are what is being accumulated. Keying on a value would make
	// every new amount a new row instead of adding to one.
	// 🛑 IT ANSWERED WITH THE MOVEMENT'S KEY, and the sentence above says why that is wrong — but the
	// code did not do what the sentence says. `GetGenericDimensionArrayObject()` is the RECORD SET's
	// dimension list, and for an accumulation register that list is the RECORDER (see
	// FillArrayObjectByDimension). So a totals surface answered "period + recorder", and since a read
	// with no ORDER BY of its own is ordered BY THE PRIMARY KEY (ibDataQueryBuilder::EffectiveSort),
	// every plain read of a balance was sorted by two columns that surface does not have. Firebird:
	// "Column unknown FLD1043_D" — measured on a warehouse configuration, 2026-09-02.
	//
	// The declared DIMENSIONS are what a total is folded by, and they are asked for by their own name.
	//
	// ⭐ AND THE PERIOD ONLY WHERE THE SHAPE HAS ONE. A balance is one moment, not an interval cut
	// into units, so its surface carries no period column at all (accumulationRegisterMetadataSchema:
	// `View(…, withPeriod: false)`) — the same rule GetViewQueryable asks, asked here too, because a
	// key naming a column the surface does not publish is not a key.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override
	{
		std::vector<const ibBackendQueryColumn*> cols;

		if (m_reg == nullptr)
			return cols;

		return KeyColumns(ibRegFold());   // a balance is one moment: no period, no movement identity
	}

protected:

	// ⭐⭐ THE PERIODICITY DECIDES WHAT A ROW IS — a ladder, and the key climbs it with the reading
	// (Max). Folded WHOLE the answer is one row per key with no date on it; by a PERIOD, one row per
	// period; by the RECORDER a row IS a movement, so the recorder is part of what makes it that row;
	// by the RECORD, the line number with it. The dimensions are in every rung, because a total is
	// what the movements sharing them fold into. The accounting register works the same way.
	//
	// ⚠ ASKED OF WHAT THE FOLD PRODUCES, NOT OF WHAT IT OFFERS TO NAME. The two are the same on every
	// rung but one: `Auto` means nobody has decided yet, so the field tree offers every projection —
	// while the reading itself still folds the interval WHOLE (ibRegFold::IsWholeInterval). Take the
	// offer as the key and the ORDER BY names a period the derived table does not project, which is
	// precisely the fault this was written to cure. HasPeriod / FromMovements / HasLineNumber are the
	// fold's own answers about what it produces, and they are what a key is made of.
	//
	// 🛑 AND NOT BY ASKING THE VOCABULARY EITHER. A first attempt filtered these through
	// ResolveColumnByName — a different question again (what may be NAMED, not what makes a row the
	// same row), a name lookup per column on every read, and it validated the SURFACE's column while
	// putting the register's ATTRIBUTE in the key.
	std::vector<const ibBackendQueryColumn*> KeyColumns(const ibRegFold& fold) const
	{
		std::vector<const ibBackendQueryColumn*> cols;

		if (m_reg == nullptr)
			return cols;

		if (fold.HasPeriod() && m_reg->HasPeriod() && m_reg->GetRegisterPeriod() != nullptr)
			cols.push_back(m_reg->GetRegisterPeriod());

		if (fold.FromMovements() && m_reg->HasRecorder()) {
			if (m_reg->GetRegisterRecorder() != nullptr)
				cols.push_back(m_reg->GetRegisterRecorder());
			if (fold.HasLineNumber() && m_reg->GetRegisterLineNumber() != nullptr)
				cols.push_back(m_reg->GetRegisterLineNumber());
		}

		for (const ibValueMetaObjectAttributeBase* dimension : m_reg->GetDimensionArrayObject())
			cols.push_back(dimension);

		return cols;
	}

public:

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

	// ⚠ NO KEY OF ITS OWN. A balance reads the SAME materialised table the totals surface keys —
	// stored per period (the grain is a day), so a row there is period + dimensions and nothing
	// else. The as-of date narrows which rows are read; it does not change what a row is.
	//
	// (Written here because the temptation was real and wrong: the information register's SLICE is
	// keyed by dimensions alone, and carrying that across to a balance by analogy would key this
	// surface without its period — folding every day into one row of valid SQL and wrong numbers.
	// The two look alike and are not: a slice has no stored table behind it.)

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue m_period;   // as-of date
	ibQueryPredicatePtr m_filter;   // the condition, already converted (ibRegFilterPredicate)
};

// turnover — resource turnovers (and receipts / expenses) over [begin, end].
class BACKEND_API ibTurnoverQueryable : public ibAccumulationTotalsQueryable {
public:
	// The FOLD says what one row is: the whole interval (nothing asked for), a period, a calendar
	// unit, or a movement's own identity — the same shape BalanceAndTurnovers reports, with only
	// the turnover side of it.
	ibTurnoverQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                    const ibValue& begin = ibValue(), const ibValue& end = ibValue(),
	                    const ibQueryPredicatePtr& filter = nullptr,
	                    const ibRegFold& fold = ibRegFold())
		: ibAccumulationTotalsQueryable(reg, ibViewShape::Turnovers),
		  m_begin(begin), m_end(end), m_filter(filter), m_fold(fold) {}

	// WHICH ROAD — beside the neighbour's, because the two answer the same question and this is the
	// SHORTER answer: a turnover looks at no row outside its own period, so a periodicity costs it a
	// GROUP BY and nothing else. (The periodicity used to be a reason to compute in RAM here, which
	// is how the easy case stayed in memory while the hard one — the running balance next door —
	// already ran on the server.)
	virtual bool IsComputedInRam() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;

	// ⭐⭐ A COLUMN THE GRANULARITY DOES NOT PRODUCE IS NOT A COLUMN OF THIS READING.
	//
	// The VIEW's vocabulary holds every projection the surface can make — that is what a view is —
	// so resolving through it accepted `PeriodMonth` on a reading that folds the interval whole, and
	// the reading then produced no such column. Nothing raised: the field simply was not in the
	// result. A query that names a field and gets neither the field nor a complaint is the worst of
	// the three possible answers.
	//
	// Refusing here makes the resolver raise "unknown attribute" NAMING the field — which is what an
	// author who changed the periodicity and left the old fields behind needs to be told.
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override
	{
		return ibRegisterFoldOffersColumn(m_reg, name, m_fold)
			? ibAccumulationTotalsQueryable::ResolveColumnByName(name) : nullptr;
	}

	// SELECT * asks the same question of every column, so it is answered the same way.
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override
	{
		std::vector<const ibBackendQueryColumn*> out;
		for (const ibBackendQueryColumn* col : ibAccumulationTotalsQueryable::GetColumns())
			if (col != nullptr && ibRegisterFoldOffersColumn(m_reg, col->GetName(), m_fold))
				out.push_back(col);
		return out;
	}

	// …AND THE KEY FOLLOWS THE SAME FOLD. Read whole, this reports one row per key with no date on
	// it, so a period in the key would order the answer by a column it does not project; folded by
	// the recorder, the movement's own identity is part of what a row IS.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override
	{
		return KeyColumns(m_fold);
	}

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue        m_begin;
	ibValue        m_end;
	ibQueryPredicatePtr m_filter;
	ibRegFold      m_fold;       // the READ granularity — a query parameter, not a schema property
};

// balance AND turnover — per period over [begin, end], rolled up to `unit`. Reports, per resource,
// the opening balance / receipt / expense / turnover / closing balance. Not a third stored shape:
// it is the balance readings and the turnover reading presented side by side, which is exactly why
// it needs no storage of its own.
class BACKEND_API ibBalanceAndTurnoverQueryable : public ibAccumulationTotalsQueryable {
public:
	// The FOLD decides whether the interval is reported whole (one row per key) or broken into
	// periods — and, when it names a movement's identity, read from the movements.
	ibBalanceAndTurnoverQueryable(const ibValueMetaObjectAccumulationRegister* reg,
	                              const ibValue& begin = ibValue(), const ibValue& end = ibValue(),
	                              const ibRegFold& fold = ibRegFold(),
	                              const ibQueryPredicatePtr& filter = nullptr)
		: ibAccumulationTotalsQueryable(reg, ibViewShape::BalanceAndTurnovers),
		  m_begin(begin), m_end(end), m_fold(fold), m_filter(filter) {}

	// The server path answers the UNPERIODISED question — one row per key, opening / turnover /
	// closing over the whole interval — in a single grouped pass.
	//
	// A PERIODISED reading is a different computation: each period opens where the previous one
	// closed, which is a running balance the conditional sums cannot express. It used to be served
	// by the live path for exactly that reason; since 2026-08-20 the IR HAS a window node, so the
	// accumulation is the server's wherever the engine can rank — and the live path remains the
	// answer where it cannot, and the oracle the server road is checked against either way.
	//
	// Out of line because the answer depends on the CONNECTION (ibCanPushWindow), not only on the
	// register and the fold.
	virtual bool IsComputedInRam() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;

	// Same rule as the turnover reading — see ibTurnoverQueryable::ResolveColumnByName.
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override
	{
		return ibRegisterFoldOffersColumn(m_reg, name, m_fold)
			? ibAccumulationTotalsQueryable::ResolveColumnByName(name) : nullptr;
	}

	// …and its key, for the same reason: the fold decides whether a row of this answer is per period,
	// per movement, or one per key over the whole interval.
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override
	{
		return KeyColumns(m_fold);
	}

	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override
	{
		std::vector<const ibBackendQueryColumn*> out;
		for (const ibBackendQueryColumn* col : ibAccumulationTotalsQueryable::GetColumns())
			if (col != nullptr && ibRegisterFoldOffersColumn(m_reg, col->GetName(), m_fold))
				out.push_back(col);
		return out;
	}

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;
private:
	ibValue        m_begin;
	ibValue        m_end;
	ibRegFold      m_fold;     // the READ granularity — a query parameter, not a schema property
	ibQueryPredicatePtr m_filter;
};

// --- L4 descriptor method bodies (the register + balance / turnover companions are complete) ---

// (The rule below moved to registerQueryLowering.h on 2026-08-13: which columns a GRANULARITY
//  produces is the same sentence for every register that folds an interval -- day, second,
//  recorder, line -- and two copies of it are two answers to "does this reading have a Period".)

inline void ibFillExplorerFromRegisterView(const ibValueMetaObjectAccumulationRegister* reg,
	const wxString& viewName, ibValueMetaObjectAccumulationRegister::ibViewShape shape,
	ibSourceDataObject::ibSourceExplorer& explorer, const ibRegFold& fold = ibRegFold())
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
	//
	// ⚠ ASKED OF THE REGISTER, NOT LISTED HERE. This used to be a hand-written list — period,
	// dimensions, resources — and the turnovers view publishes two columns the list did not know: the
	// RECORDER and the LINE NUMBER (accumulationRegisterMetadataSchema.cpp, the movement arm's own
	// identity). Wherever the granularity offers those columns (Auto / Recorder / Record) they were
	// appended as plain synthetic triples, so the recorder lost its picture and stopped saying it
	// holds a reference — the very loss this comment describes, one row further down the same list.
	// FindAnyAttributeObjectByFilter is the register's own economical find — one walk over the children
	// with the id compared as it goes — so an attribute added tomorrow is found without anybody editing
	// a list, and nothing is allocated per column to answer it.
	const auto attributeById = [reg](const ibMetaID& id) -> const ibValueMetaObjectAttributeBase* {
		return reg->FindAnyAttributeObjectByFilter(id);
	};

	const wxString periodName = reg->GetRegisterPeriod() != nullptr
		? reg->GetRegisterPeriod()->GetName() : wxString();

	for (const ibBackendQueryColumn* col : view->GetColumns()) {
		if (col == nullptr)
			continue;
		if (!periodName.IsEmpty() && !ibRegisterViewColumnFits(col->GetName(), periodName, fold,
				reg->HasRecorder() && reg->GetRegisterRecorder()   != nullptr ? reg->GetRegisterRecorder()->GetName()   : wxString(),
				reg->HasRecorder() && reg->GetRegisterLineNumber() != nullptr ? reg->GetRegisterLineNumber()->GetName() : wxString()))
			continue;
		if (const ibValueMetaObjectAttributeBase* attribute = attributeById(col->GetColumnId()))
			explorer.AppendColumn(attribute, /*enabled*/ true, /*visible*/ true);
		else
			explorer.AppendColumn(col);
	}
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
namespace ibRegTurnoverArg  { enum { Begin = 0, End = 1, Periodicity = 2, Filter = 3, Count }; }
namespace ibRegBalTurnArg   { enum { Begin = 0, End = 1, Periodicity = 2, FillMethod = 3, Filter = 4, Count }; }

// ⭐ AND THE ONE RULE ACROSS ALL THREE: THE PERIOD, THEN HOW IT IS CUT, THEN THE CONDITION.
//
// Every table asks for its period first — a moment, or an interval. What comes next is the
// arguments that are ABOUT that period: how it is cut into rows, and what to do with a period
// nothing moved in. The condition follows them, because it is about the ROWS rather than about the
// interval, and it is the last thing that is true of every reading.
//
// ⚠ CORRECTED 2026-08-13 (owner): the periodicity used to sit LAST, on the argument that an option
// nobody states belongs where leaving it out is free. That is true of a DEFAULT and false of an
// ORDER — an author writes the interval and immediately says how to cut it, and the accounting
// register's own tables (and every reference implementation) put it third. Two registers whose
// arguments run in different orders are two things to remember instead of one.
//
// A new parameter is therefore inserted where it BELONGS by subject, and every call is re-read
// against the new order — which is safe today only because there are no third-party configurations
// yet. Moving a slot silently re-reads a query nobody edited: that is exactly how the periodicity
// once pushed the filter aside and nobody noticed.
//
// Stated to the compiler rather than to the reader, because a convention that is only written down is
// a convention that gets broken by the person who did not read it. `DescribeParameters` declares this
// same order to the outside world and sits a few lines below — the two lists must agree.
static_assert(ibRegBalanceArg::Filter == ibRegBalanceArg::Count - 1,      "the condition is last");
static_assert(ibRegTurnoverArg::Periodicity == ibRegTurnoverArg::End + 1, "how the interval is cut follows the interval");
static_assert(ibRegBalTurnArg::Periodicity  == ibRegBalTurnArg::End + 1,  "how the interval is cut follows the interval");
static_assert(ibRegBalTurnArg::FillMethod   == ibRegBalTurnArg::Periodicity + 1, "the fill method is about the periods too");
static_assert(ibRegTurnoverArg::Filter == ibRegTurnoverArg::Count - 1,    "the condition is last");
static_assert(ibRegBalTurnArg::Filter  == ibRegBalTurnArg::Count - 1,     "the condition is last");

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
// (The list itself moved to registerQueryLowering.h — the accounting register reports the same
// figures, differing only in that each has a SIDE, and two copies of the words is how two registers
// come to disagree about what a column is called.)

// ⚠ ibRegArg moved to registerQueryLowering.h with the rest of the CALL helpers — reading slot N of
// an argument list is the same question for every register, and the accounting one had written its
// own (`ArgAt`, in two overloads) under another name.
//
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
	moment.m_description = _("AS OF WHEN - one moment, not an interval: this table answers what is on hand "
	                  "at it. It may name a DOCUMENT instead of a date, which is how \"the balance "
	                  "as of this receipt\" is asked when three of them share a day. Left out, the "
	                  "balance is as of the last movement there is.");
	// The register's OWN period type, not a hand-written "date": asked of the attribute, so a
	// register that ever dates its rows differently needs nothing changed here.
	if (m_reg != nullptr && m_reg->GetRegisterPeriod() != nullptr)
		moment.m_type = m_reg->GetRegisterPeriod()->GetTypeDesc();
	out.push_back(moment);

	ibQuerySourceParameter condition;
	condition.m_name      = wxT("Condition");
	condition.m_description      = _("A condition on the DIMENSIONS, applied inside the reading - so it "
	                          "narrows what is folded rather than dropping rows after the fold.");
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
	const ibRegFold fold = ibReadRegisterFold(period);

	return MakeCompanion<ibTurnoverQueryable>(paParams, lSizeArray, m_reg, begin, end, filter, fold);
}

inline void ibAccumRegisterTurnoverDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	ibFillExplorerFromRegisterView(m_reg, m_reg->GetTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers, explorer);
}

// THE PERIODICITY IS THE THIRD ARGUMENT — (begin, end, periodicity, condition). Read as the word the
// constructor wrote; anything else (a parameter, nothing at all) leaves it empty, which means "not
// decided" and shows every projection. `ibRegisterFoldOfArgs` reads it BY THE NAMED SLOT and lives in
// registerQueryLowering.h with the other call helpers.
inline void ibAccumRegisterTurnoverDescriptor::FillSourceExplorer(
	ibSourceDataObject::ibSourceExplorer& explorer, const std::vector<ibValue>& args) const
{
	ibFillExplorerFromRegisterView(m_reg, m_reg->GetTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers, explorer,
		ibRegisterFoldOfArgs(args, ibRegTurnoverArg::Periodicity));
}

// The interval pair, the condition slot and the periodicity list are the SAME declaration for every
// register that vends a folded interval — they live in registerQueryLowering.h
// (ibFillRegisterIntervalParameters / ibAppendRegisterConditionParameter /
// ibAppendRegisterPeriodicityParameter). The period TYPE is asked of this register's own attribute at
// the callsite, which is the one part that genuinely differs.
inline ibTypeDescription ibRegisterPeriodType(const ibValueMetaObjectAccumulationRegister* reg)
{
	return (reg != nullptr && reg->GetRegisterPeriod() != nullptr)
		? reg->GetRegisterPeriod()->GetTypeDesc() : ibTypeDescription();
}


inline void ibAccumRegisterTurnoverDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	// THE INTERVAL, HOW IT IS CUT, THEN THE CONDITION — see ibRegTurnoverArg for why that order, and
	// note that this list is read POSITIONALLY: it must run in exactly the enum's sequence.
	ibFillRegisterIntervalParameters(ibRegisterPeriodType(m_reg), out);
	ibAppendRegisterPeriodicityParameter(out);
	ibAppendRegisterConditionParameter(out);
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
	const ibRegFold fold = ibReadRegisterFold(period);

	return MakeCompanion<ibBalanceAndTurnoverQueryable>(paParams, lSizeArray,
		m_reg, begin, end, fold, filter);
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
		ibRegisterFoldOfArgs(args, ibRegBalTurnArg::Periodicity));
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
	// THE INTERVAL, HOW IT IS CUT, WHAT TO DO WITH AN EMPTY PERIOD, THEN THE CONDITION — the sequence
	// ibRegBalTurnArg declares, and this list is read positionally against it.
	ibFillRegisterIntervalParameters(ibRegisterPeriodType(m_reg), out);
	ibAppendRegisterPeriodicityParameter(out);

	ibQuerySourceParameter fillMethod;
	fillMethod.m_name    = wxT("FillMethod");
	fillMethod.m_description = _("Whether the opening and closing balances are reported beside the "
	                             "movements, or the movements alone. Boundaries are what make this "
	                             "table different from Turnovers - ask for Movements only when the "
	                             "balances are genuinely not wanted.");
	fillMethod.m_choices = { wxT("MovementsAndBoundaries"), wxT("Movements") };   // a word, see the periodicity
	// ⭐ LEFT OUT, THE BOUNDARIES ARE THERE. This table exists to say where the quantity STOOD as the
	// period opened and as it closed; a reading without those is a narrower thing somebody asks for
	// on purpose, not the natural state of it. So the empty box means movements AND boundaries, and
	// the list is ordered the same way — what happens by default reads first.
	fillMethod.m_default = wxT("MovementsAndBoundaries");
	out.push_back(fillMethod);

	ibAppendRegisterConditionParameter(out);
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