#ifndef __ACCOUNTING_REGISTER_H__
#define __ACCOUNTING_REGISTER_H__

#include "commonObject.h"
#include "accountingRegisterEnum.h"
#include "backend/propertyManager/property/propertyChartOfAccounts.h"
#include "backend/query/queryable.h"          // ibComputedRegisterQueryable<TReg> — the shared base for a computed virtual table
#include "backend/query/tempTableQueryable.h" // ibDbTempTableQueryable — a named relation; what a SHAPE is to L3
// The register-shared lowering: ibRegBound / ibRegFold / ibRegFilterPredicate / ibRegFlatLeaves. Shared
// on purpose — an accounting register reads a boundary and a periodicity by exactly the same words the
// accumulation register does, and a copy per register is how two registers come to disagree.
#include "backend/metaCollection/partial/registerQueryLowering.h"

#include <map>
#include <memory>

class ibValueMetaObjectAccountingRegister;
// The bound chart — named here, complete only where it is read (chartOfAccounts.h). A register asks it
// three questions (the account's type, how many analytics, what a slot may hold) and never includes it
// for that: the answers arrive through this pointer.
class ibValueMetaObjectChartOfAccounts;

// ============================================================================
// THE FIGURES, IN ONE PLACE.
//
// A reading builds its column as `<Resource>` + a suffix; the compute spells the same word to
// aggregate it; the manager spells it again to name a column in the table a script gets back. Three
// spellings of one name is how `Resource1_TurnoverDr` and `Resource1TurnoverDr` become two columns
// for one figure, each answering with nothing.
//
// ⚠ THE TWO SIDES ARE NEVER FOLDED INTO ONE FIGURE HERE. Whether a balance collapses into a single
// signed number is decided by the ACCOUNT's type at read time (§4.7) — active / passive fold, active-
// passive does not — so the shape always carries both sides and the fold is a projection of them.
// ⭐ BUILT FROM THE SHARED WORDS, never spelled again. A figure is `<what>` + `<which side>`, and both
// halves live in registerQueryLowering.h — the same `Balance` an accumulation register reports, said of
// one side of an entry. Writing "BalanceDr" out here would be a second spelling of a word that already
// exists, and the two would drift the first time either changed.
namespace ibAcctFigure {
	inline const wxString BalanceDr        = ibRegSidedFigure(ibRegFigure::Balance, false);
	inline const wxString BalanceCr        = ibRegSidedFigure(ibRegFigure::Balance, true);
	inline const wxString TurnoverDr       = ibRegSidedFigure(ibRegFigure::Turnover, false);
	inline const wxString TurnoverCr       = ibRegSidedFigure(ibRegFigure::Turnover, true);
	inline const wxString Turnover         = ibRegFigure::Turnover;   // DrCrTurnovers — one figure per PAIR of accounts
	inline const wxString OpeningBalanceDr = ibRegSidedFigure(ibRegFigure::OpeningBalance, false);
	inline const wxString OpeningBalanceCr = ibRegSidedFigure(ibRegFigure::OpeningBalance, true);
	inline const wxString ClosingBalanceDr = ibRegSidedFigure(ibRegFigure::ClosingBalance, false);
	inline const wxString ClosingBalanceCr = ibRegSidedFigure(ibRegFigure::ClosingBalance, true);
}

// The five virtual tables. Named as a shape rather than as three flags, for the reason the neighbour
// states: naming a column a surface does not have is how a reader gets a silent empty instead of an
// error, and the shape is what decides which columns exist.
enum class ibAcctShape
{
	Balance,              // accounts (+ the requested breakdown) + <res>BalanceDr / BalanceCr, at a moment
	Turnovers,            // the same key + <res>TurnoverDr / TurnoverCr over an interval
	DrCrTurnovers,        // the CORRESPONDENCE matrix — one row per (debit account, credit account) pair
	BalanceAndTurnovers,  // opening / turnover / closing, one row per key
	Records               // the movement lines themselves, with the slots widened into a column per kind
};

// ============================================================================
// ⭐⭐ THE ARGUMENTS ARE A LAYOUT, NOT A COUNTED LIST — and the layout is COMPUTED, because the
// signature genuinely differs between the two modes of this register.
//
// A one-sided register has ONE account and ONE breakdown per row; a correspondence register names both
// sides on the line, so it takes a debit account AND a credit account, each with its own breakdown and
// its own condition. Two shapes, and the neighbour's answer to this (a namespace of hand-numbered
// slots plus static_asserts that the order holds) cannot express a signature that changes.
//
// So the ONE function below answers "which slot is which" and every reader asks it: the descriptor
// that declares the parameters to the outside, and the one that reads a call's arguments. Two lists
// that must agree, replaced by one list both are derived from — which is the only arrangement in which
// they cannot drift apart. (The neighbour's off-by-one, where a declared periodicity pushed the filter
// into another slot and every call silently lost its condition, is exactly what this shape prevents.)
//
// ⚠ THE ORDER, AND WHY: period first (a moment, or an interval), then the ACCOUNTS, then the requested
// BREAKDOWN, then the condition, then the refinements. Accounts and breakdown sit before the condition
// because they are not filtering — they decide what a ROW IS and, for the breakdown, which columns the
// result even has. A new parameter is appended among the refinements, never before the condition.
struct ibAcctArgs
{
	int m_begin        = -1;   // the moment (Balance) or the start of the interval
	int m_end          = -1;
	int m_periodicity  = -1;
	int m_fillMethod   = -1;   // BalanceAndTurnovers — what to do with a period nothing moved in
	int m_accountDr    = -1;   // a CONDITION over the account; in a one-sided register simply THE account
	int m_accountCr    = -1;
	int m_kindsDr      = -1;   // the requested breakdown: one kind or an ARRAY of them, in the caller's order
	int m_kindsCr      = -1;
	int m_condition    = -1;
	int m_order        = -1;   // Records — how the lines come out
	int m_top          = -1;   // Records — how many
	int m_count        = 0;

	// ⭐⭐ THE ORDER IS GROUPED BY SIDE, NOT BY KIND OF ARGUMENT — and that is the whole correction.
	//
	// It used to read "both accounts, then both breakdowns, then the condition, then the periodicity",
	// which is tidy in the struct and wrong at the callsite: an author writes a SIDE at a time — this
	// account, broken down like THIS — and a positional call is read in the order it is written. The
	// periodicity sat LAST for the same reason (it was added last), while it belongs immediately after
	// the interval it cuts.
	//
	// ⚠ A POSITIONAL LAYOUT IS A CONTRACT WITH EVERY EXISTING CALL. Moving a slot silently re-reads a
	// query nobody edited — the neighbouring register was bitten by exactly this, when a declared
	// periodicity pushed every call's condition into another slot and the condition was then read as a
	// breakdown list and dropped. There are no third-party configurations yet; this is the window in
	// which the order can still be made right.
	static ibAcctArgs For(ibAcctShape shape, bool correspondence)
	{
		ibAcctArgs a;
		int slot = 0;

		// The interval — or a single MOMENT, which is what a balance stands at.
		a.m_begin = slot++;
		if (shape != ibAcctShape::Balance)
			a.m_end = slot++;

		// How the interval is CUT, right after the interval itself. A listing of lines is not cut at
		// all: a movement is already as fine as this data gets.
		if (shape == ibAcctShape::Turnovers || shape == ibAcctShape::DrCrTurnovers
		    || shape == ibAcctShape::BalanceAndTurnovers)
			a.m_periodicity = slot++;

		// …and what to do with a period NOTHING moved in — a question only the symbiosis can ask,
		// because only it reports a balance per period that a movement never touched.
		if (shape == ibAcctShape::BalanceAndTurnovers)
			a.m_fillMethod = slot++;

		// ⭐⭐ TWO DIFFERENT "BOTH SIDES", AND CONFLATING THEM IS THE MISTAKE TO AVOID.
		//
		// A ROW may name two accounts — that is the correspondence matrix, where a line genuinely is a
		// pair. A balance or a turnover is NOT that: it reports ONE account per row (its debit figure
		// and its credit figure), even in a correspondence register, because "the balance of account
		// 51" is a question about one account. What a second account is there for in a TURNOVER is a
		// FILTER — the turnovers of 51 in correspondence with 62 — and a filter is not a column.
		//
		// So a BALANCE takes one side and nothing else: there is no such thing as "the balance of 51
		// against 62", because a balance is not a movement and has no other end.
		const bool bothSides = (shape == ibAcctShape::DrCrTurnovers)
		                    || (correspondence && shape == ibAcctShape::Turnovers);

		// A side is ONE GROUP: the account and the breakdown OF that account, adjacent, because that is
		// how the question is asked. Records takes neither — it lists lines as written, and the
		// breakdown it reports is the line's own.
		if (shape != ibAcctShape::Records) {
			a.m_accountDr = slot++;
			a.m_kindsDr   = slot++;
		}

		// ⚠ THE GENERAL CONDITION SITS BETWEEN THE TWO SIDES FOR TURNOVERS, and after both for the
		// matrix. That is not a whim of layout: a turnover asks "the debit side, so filtered, in
		// correspondence with THAT credit side", and the filter belongs to the reading rather than to
		// either side; the matrix names both sides symmetrically first and filters the pair afterwards.
		const bool conditionLast = (shape == ibAcctShape::DrCrTurnovers);
		if (!conditionLast)
			a.m_condition = slot++;

		if (bothSides) {
			a.m_accountCr = slot++;
			a.m_kindsCr   = slot++;
		}

		if (conditionLast)
			a.m_condition = slot++;

		// A LISTING is the one reading that answers with lines rather than figures, so it is also the
		// only one that can be asked for an ORDER and a COUNT — a fold has no line to order and
		// answers with every group it found.
		if (shape == ibAcctShape::Records) {
			a.m_order = slot++;
			a.m_top   = slot++;
		}

		a.m_count = slot;
		return a;
	}
};

// ⭐⭐ A CALL, READ ONCE. A virtual table named in a query and the same reading called from a script
// are two ENTRANCES to one thing, and they must hand over the same arguments — the same moment, the
// same accounts, the same requested breakdown, the same condition. Two parsers would agree until the
// day a parameter is added to one of them.
struct ibAcctCallArgs
{
	ibRegBound           m_begin;      // the moment (Balance) or the start of the interval
	ibRegBound           m_end;
	// The ACCOUNT slots are CONDITIONS now: a predicate the source consumes itself, carrying the
	// unfold word so the reading can fold as well as select (queryableFactory.h, m_consumedBySource).
	ibQueryPredicatePtr  m_accountDr;
	ibQueryPredicatePtr  m_accountCr;
	std::vector<ibValue> m_kindsDr;
	std::vector<ibValue> m_kindsCr;
	ibQueryPredicatePtr  m_filter;      // the DIMENSION half, already a predicate
	// ⭐ THE CONDITION AS IT ARRIVED, kept because half of it cannot be lowered yet. A condition may
	// name a DIMENSION (a column, resolved once) or an ACCOUNT DIMENSION — and the latter is a
	// question about the SLOTS, which differ per side: the debit pass must ask the debit slots and the
	// credit pass the credit ones. So that half is built per pass, where the side is known.
	ibValue              m_condition;
	ibRegFold            m_fold;

	// ⭐ WHAT TO DO WITH A PERIOD NOTHING MOVED IN — asked only of the symbiosis, because only it
	// reports a balance for a period no movement touched. `Movements` (the default) reports the
	// periods that have movements; `MovementsAndPeriodBoundaries` reports every period of the
	// interval, carrying the balance across the empty ones.
	bool                 m_fillEmptyPeriods = false;

	// A LISTING answers with lines, so it is the one reading that can be ordered and capped. Empty /
	// zero mean "as they come" and "all of them" — the same answers the arguments' absence gives.
	ibValue              m_order;
	long                 m_top = 0;
};

// The requested breakdown as a list: one kind, an ARRAY of them in the caller's order, or nothing at
// all — which means "as the ACCOUNT declares them" and is not the same as an empty list.
BACKEND_API std::vector<ibValue> ibAcctReadKinds(const ibValue& given);

// Read a call's arguments by the layout of this shape. `paParams` is the positional argument array as
// both the query door and the script dispatcher hand it over.
BACKEND_API ibAcctCallArgs ibAcctParseCall(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape,
                                           ibValue** paParams, long lSizeArray);

class ibAcctBalanceQueryable;
class ibAcctTurnoverQueryable;
class ibAcctDrCrTurnoverQueryable;
class ibAcctBalanceAndTurnoverQueryable;
class ibAcctRecordsQueryable;

// L4 virtual-table source descriptors. Owned by the register as fields, registered under
// "<Register>.Balance" / ".Turnovers" / ".DrCrTurnovers" / ".BalanceAndTurnovers" /
// ".RecordsWithAccountDimensions"; CreateQueryable builds the call-scoped companion from the
// arguments and the base owns it. One class, because all five differ only in their SHAPE — the thing
// they publish and the compute they run — and that difference is a member, not a hierarchy.
class ibAcctSourceDescriptor : public ibQueryableSourceDescriptor
{
public:
	ibAcctSourceDescriptor(ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape)
		: m_reg(reg), m_shape(shape) {}
	~ibAcctSourceDescriptor() override;

	wxString GetNamespace() const override;
	wxString GetName() const override;
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) override;

	// ⭐⭐ THE SAME CALL, WITH THE ACCOUNT CONDITIONS THIS SOURCE CONSUMES ITSELF. They do not reach a
	// WHERE: a reading asked for accounts «in hierarchy» reports the subordinates UNDER the account
	// that was named, and a filter applied around it can only remove rows, never fold them.
	const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray,
	                                          const std::vector<ibQueryPredicatePtr>& conditions) override;

	// …resolved against the MOVEMENTS, which is where the account column lives whichever surface a
	// given pass then reads — and which exists before the call's companion does.
	const ibBackendQueryable* GetConditionScope() const override;

	// WHAT COLUMNS THIS TABLE HAS, asked without running it — the query constructor's catalogue.
	// Answered from the SHAPE, which is metadata plus the call's arguments and touches no database.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

	// …AND WITH THE CALL'S ARGUMENTS, because the REQUESTED KINDS decide which columns exist. That is
	// the structural difference from every other register in the tree: the output schema follows the
	// arguments of the call, not the metaobject.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
	                        const std::vector<ibValue>& args) const override;
	void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;
	void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer, const wxString& slot) const override;

private:
	ibValueMetaObjectAccountingRegister* m_reg;
	ibAcctShape                          m_shape;

	// ⚠ THE CONSUMED CONDITIONS, FOR THE LENGTH OF ONE CALL. The two entrances share one body — the
	// plain `CreateQueryable` is what actually builds — so the predicates are handed across here and
	// cleared immediately after. Not state: a descriptor holds no call between calls, and a value left
	// behind would attach itself to the NEXT one, which is the quiet kind of wrong.
	ibQueryPredicatePtr                  m_pendingAccountDr;
	ibQueryPredicatePtr                  m_pendingAccountCr;
};

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

	// Is this the KIND half of a dimension pair — `AccountDimension<i>Kind`, either side? Asked by
	// IDENTITY against the slots themselves, never by reading the role back out of the name.
	bool IsAccountDimensionKindColumn(const ibValueMetaObjectAttributeBase* attribute) const {
		if (attribute == nullptr)
			return false;
		for (const ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)
			if (slot == attribute) return true;
		for (const ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKindsCr)
			if (slot == attribute) return true;
		return false;
	}

	// ⭐ AVAILABLE, BUT NOT SHOWN — the kinds. A movement's meaning is its VALUES; opening a list with
	// the kind beside every value doubles the columns to repeat what the value already says, and the
	// row becomes unreadable. The column stays in the source, so anyone who wants it puts it back.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

	// Give the analytics slots the types their chart declares. Called from the RUN phase and from the
	// moment the chart binding changes — both, because editing does not run a configuration and a slot
	// left untyped between the two produces columns nothing can be written into (see the definition).
	void ApplyAccountDimensionSlotTypes();

	// One line = a whole posting (both accounts named) rather than one side of one.
	bool IsCorrespondence() const { return m_propertyCorrespondence->GetValueAsBoolean(); }

	// ⭐⭐ ONE CHART OF ACCOUNTS, AND ONLY ONE.
	//
	// A register keeps ONE book. Its account column is typed by that chart, its analytics count comes
	// from that chart, and the contour of every slot comes from the characteristic chart THAT chart is
	// bound to — three answers that must come from one source or they contradict each other. Two charts
	// would mean two different maxima and two different contours for one physical column set, and the
	// only ways out of that are to pick one silently or to take the widest, both of which are the
	// engine deciding something the author did not say.
	//
	// The same rule one level down: a chart of accounts has ONE chart of characteristic types.
	//
	// Null while nothing is chosen — a register under construction is a legitimate state; what is not
	// legitimate is SAVING one with two, and that is refused at the write (OnSaveMetaObject), where an
	// import cannot walk around it.
	const ibValueMetaObjectChartOfAccounts* GetChartOfAccounts() const;

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

	// The CREDIT side of the same pair — populated only in correspondence mode, where one line names
	// both accounts and therefore carries two independent breakdowns. Null outside it, and that is the
	// answer rather than an error: there is no second side to ask about.
	ibValueMetaObjectAttributePredefined* GetRegisterAccountDimensionCr(unsigned int idx) const {
		return idx < m_accountDimensionSlotsCr.size() ? m_accountDimensionSlotsCr[idx] : nullptr;
	}

	ibValueMetaObjectAttributePredefined* GetRegisterAccountDimensionKindCr(unsigned int idx) const {
		return idx < m_accountDimensionKindsCr.size() ? m_accountDimensionKindsCr[idx] : nullptr;
	}

	// The CREDIT account, in correspondence mode; null in a one-sided register, where the inherited
	// Account is the only one and the side is said by RecordType.
	ibValueMetaObjectAttributePredefined* GetRegisterAccountCr() const { return m_accountCr; }

	// A side's slot, asked once so no reading branches on the side itself.
	ibValueMetaObjectAttributePredefined* GetAccountDimensionSlot(bool creditSide, unsigned int idx) const {
		return creditSide ? GetRegisterAccountDimensionCr(idx) : GetRegisterAccountDimension(idx);
	}

	ibValueMetaObjectAttributePredefined* GetAccountDimensionKindSlot(bool creditSide, unsigned int idx) const {
		return creditSide ? GetRegisterAccountDimensionKindCr(idx) : GetRegisterAccountDimensionKind(idx);
	}

	// ============================================================================
	// THE READINGS — the register's own aggregate knowledge, one function per virtual table.
	//
	// Each returns a RAM table the companion queryable hands to L3 through ComputeRows, exactly as the
	// accumulation register's ComputeBalance / ComputeTurnover do. What differs is where they READ:
	// the neighbour reads its materialised totals view, and this register has none yet, so these read
	// the MOVEMENTS through the same L3 door (ibDataQueryBuilder). Correct at any scale, and the
	// oracle a totals bundle will later be measured against — the shape a caller sees does not change
	// when the totals arrive (§7 of the arc).
	//
	// `kinds` is the requested breakdown, IN THE CALLER'S ORDER: column i reports the value filed under
	// kinds[i], whichever slot happens to hold it on each account. Empty means "as the ACCOUNT declares
	// them" — the slots are then projected as they stand, which is the same order the account's kinds
	// table has, and an account using fewer kinds simply leaves the tail columns empty.
	ibQueryRamTable ComputeBalance(const ibRegBound& bound, const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	                               const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                               const ibQueryPredicatePtr& filter, const ibValue& condition = ibValue()) const;
	ibQueryRamTable ComputeTurnover(const ibRegBound& begin, const ibRegBound& end,
	                                const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	                                const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                                const ibQueryPredicatePtr& filter, const ibRegFold& fold, const ibValue& condition = ibValue()) const;
	ibQueryRamTable ComputeDrCrTurnover(const ibRegBound& begin, const ibRegBound& end,
	                                    const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	                                    const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                                    const ibQueryPredicatePtr& filter, const ibValue& condition = ibValue()) const;
	ibQueryRamTable ComputeBalanceAndTurnover(const ibRegBound& begin, const ibRegBound& end,
	                                          const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	                                          const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                                          const ibQueryPredicatePtr& filter, const ibRegFold& fold,
	                                          const ibValue& condition = ibValue(), bool fillEmptyPeriods = false) const;
	ibQueryRamTable ComputeRecords(const ibRegBound& begin, const ibRegBound& end,
	                               const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                               const ibQueryPredicatePtr& filter, const ibValue& condition = ibValue(),
	                               const ibValue& order = ibValue(), long top = 0) const;

	// ⭐⭐ THE CORRESPONDENCE READING, BUILT ONCE AND ENDED TWICE.
	//
	// `DrCrTurnovers` is the one reading that can never stand on the totals — a totals row is keyed by
	// ONE account and the other side of the movement was never stored beside it — so it groups the
	// MOVEMENTS. That was never the RAM part of it: the GROUP BY and the sums have always gone to the
	// server. What was RAM is the ENDING — the rows were materialised before anything could be
	// composed with them.
	//
	// So the query is assembled once, and the two endings take it from there: rows for the script
	// door, a RELATION for a query that wants to join or filter over it. Two assemblies would be two
	// chances for the two doors to answer differently for the same call, which is precisely the class
	// of defect this register has already paid for twice.
	ibQueryRelPtr BuildDrCrTurnoverRelation(const ibRegBound& begin, const ibRegBound& end,
	                                        const ibQueryPredicatePtr& accountDr, const ibQueryPredicatePtr& accountCr,
	                                        const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                                        const ibQueryPredicatePtr& filter, const ibValue& condition = ibValue()) const;

	// The movement LISTING, same arrangement — built once, ended as rows or as a relation. This one
	// PROJECTS rather than folds (a line is already as fine as the data gets), which is why the door
	// lowers it through the read path and not the GROUP BY one.
	ibQueryRelPtr BuildRecordsRelation(const ibRegBound& begin, const ibRegBound& end,
	                                   const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                                   const ibQueryPredicatePtr& filter, const ibValue& condition = ibValue(),
	                                   const ibValue& order = ibValue(), long top = 0) const;


	// ⭐⭐ THE SHAPE OF A VIRTUAL TABLE — metadata plus the CALL'S ARGUMENTS, and no database.
	//
	// Every other register in the tree answers this from its metaobject alone. This one cannot: the
	// requested kinds decide how many breakdown columns there are and what they are called, so the
	// same virtual table has a different shape per call. That is the whole of §7, and it is why the
	// cache below is keyed by the kinds as well as by the shape.
	//
	// Two consumers, one answer: the companion navigates through it (so a query can name its columns)
	// and the compute seeds its RAM table from it (so the rows are findable by those very columns). A
	// second derivation of "what columns does this table have" is how a source comes to publish one set
	// and return another.
	const ibBackendQueryable* GetShapeQueryable(ibAcctShape shape,
	                                            const std::vector<ibValue>& kindsDr,
	                                            const std::vector<ibValue>& kindsCr,
	                                            const ibRegFold& fold = ibRegFold()) const;

	// ⭐ THE BREAKDOWN COLUMNS ARE POSITIONAL, ALWAYS — `AccountDimension1..N`, and in correspondence
	// mode `AccountDimensionDr1..N` / `AccountDimensionCr1..N`. What the caller's list of kinds decides
	// is WHICH kind lands in which position, not what the position is called: ask for contract then
	// counterparty and column 1 is the contract, ask for nothing and column 1 is whatever the ACCOUNT
	// declared first. Naming the column after the kind would put a data value (a characteristic's
	// presentation, in the user's own language, spaces and all) into the field list of a query.
	static wxString AccountDimensionColumnName(const wxString& sidePrefix, unsigned int no);
	static wxString AccountDimensionKindColumnName(const wxString& sidePrefix, unsigned int no);

	// ⭐ THE SIDE IS PART OF THE NAME — for the ACCOUNT exactly as it already is for its dimensions.
	//
	// `Account` when the register is one-sided (the side is said by RecordType, so the name carries
	// none), `AccountDr` / `AccountCr` when a line names both. The debit account was the one name
	// that never took the prefix: it stayed `Account` beside `AccountCr`, so a correspondence line
	// read as "an account, and a credit account" rather than as the two sides of one entry — and
	// which of the two the bare one was could only be learnt from documentation.
	//
	// Spelled here, beside the dimension's own rule, so a side added or a prefix changed is one edit.
	static wxString AccountColumnName(const wxString& sidePrefix) { return wxT("Account") + sidePrefix; }
	static wxString AccountColumnSynonym(const wxString& sidePrefix) {
		if (sidePrefix == wxT("Dr")) return _("Debit account");
		if (sidePrefix == wxT("Cr")) return _("Credit account");
		return _("Account");
	}

	// The prefix THIS register's debit side currently carries — empty unless a line names both
	// accounts. Asked rather than recomputed at each site: it is the same question the dimension
	// slots are named from, and the two must never disagree.
	wxString GetDebitSidePrefix() const { return IsCorrespondence() ? wxT("Dr") : wxEmptyString; }

	// ⭐ WHAT AN OWN ATTRIBUTE IS, handed over BESIDE it rather than left to be guessed.
	//
	// Most walkers do not care — a load is a load. The typing pass cannot not care: a KIND takes a
	// reference to a characteristic and a VALUE takes the chart's whole composition, and those are
	// different declarations. Told only the attribute, it would have to read the ROLE back out of the
	// NAME (`EndsWith("Kind")`) — the classification re-derived by spelling, in the one place that
	// would never be told when the spelling changed.
	enum class ibOwnRole {
		DimensionKind,    // AccountDimension<i>Kind — a reference to a characteristic
		DimensionValue,   // AccountDimension<i>     — the chart's composition, narrowed per kind at write
		AccountCr         // the credit account — same type as the debit one, one declaration for both
	};

	// ⭐⭐ EVERY ATTRIBUTE THIS REGISTER OWNS, IN ONE WALK.
	//
	// The slots are created here, contributed as columns here, and then have to be visited by EIGHT
	// separate walks — load, save, delete, before-run, after-run, before-close, after-close, and the
	// typing pass. Written out eight times, the credit side was missed in every one of them: its
	// columns existed in the table and its type was never set, so a correspondence register published
	// breakdown columns that admit nothing. Eight hand-written lists, one of them right.
	//
	// So there is one list, and it is this. A slot added, a side added, a predefined attribute added —
	// every walker learns about it at once, because there is nothing else to teach.
	template <typename TVisitor>
	bool ForEachOwnAttribute(TVisitor visit) const {
		for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKinds)   if (!visit(slot, ibOwnRole::DimensionKind))  return false;
		for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlots)   if (!visit(slot, ibOwnRole::DimensionValue)) return false;
		for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionKindsCr) if (!visit(slot, ibOwnRole::DimensionKind))  return false;
		for (ibValueMetaObjectAttributePredefined* slot : m_accountDimensionSlotsCr) if (!visit(slot, ibOwnRole::DimensionValue)) return false;
		if (m_accountCr != nullptr && !visit(m_accountCr, ibOwnRole::AccountCr)) return false;
		return true;
	}

	// Bring the slot set in line with the chart of accounts. Slots are created once and REUSED:
	// a metaID is the physical column name (fld<metaID>), so a slot that came back with a fresh id
	// would be a different column and the data in the old one unreachable. Lowering the count
	// therefore deactivates from the tail rather than destroying, and raising it again finds the
	// very same slots waiting. Growth is append-only for the same reason.
	void SyncAccountDimensionSlots();

	///////////////////////////////////////////////////////////////////

	wxString GetRegisterTableNameDB() const {
		wxString className = GetClassName();
		wxASSERT(m_metaId != 0);
		return wxString::Format("%s%i_T", className, GetMetaID());
	}

	// ============================================================================
	//  The materialised totals — TWO GUARDED ACCUMULATIONS, one per side
	// ============================================================================
	//
	// ⭐⭐ WHY TWO TABLES AND NOT ONE WITH TWO COLUMNS. A totals row is keyed by the account it is
	// about, and in a CORRESPONDENCE register one movement is about TWO accounts: it raises the debit
	// turnover of one and the credit turnover of another, each with its own analytical breakdown. Two
	// different keys, therefore two accumulations — which is exactly what "two guarded accumulations"
	// meant. A one-sided register has one key per row (the side is said by RecordType), so it keeps
	// ONE table and tells the sides apart in the accumulated VALUE.
	//
	// ⚠ WHAT IS STORED IS TURNOVER, NEVER BALANCE — and that is forced by the data, not chosen. The
	// balance key is NARROWER than the turnover key and by how much is decided per account
	// (`SummaryOnly` on a row of its kinds table), so a stored balance would need a key that changes
	// with data the schema cannot see. Turnover is stored at the full key; a balance is that turnover
	// folded up to a moment, with the turnovers-only breakdowns dropped at read time.
	// ⭐⭐ NAMED AFTER THE OBJECT, NOT AFTER A SETTING.
	//
	// This used to spell the side into the name — "_Tt" one-sided, "_TtDr" / "_TtCr" in correspondence
	// — which made the name a FUNCTION OF A CHECKBOX. Switching correspondence renamed both tables at
	// once, and the old name became unspellable: nothing could compute it any more, so the differ
	// could neither find the tables nor drop them, and the maintenance was rebuilt against a name that
	// no longer existed ("Table unknown ...._TTDR" on the trigger, with the tables already gone).
	//
	// The side is a property of the TOTALS OBJECT, not of the register's settings: there are two of
	// them, predefined, named "DebitTotals" and "CreditTotals", and those names do not change when a
	// checkbox does. Taking the name from the object gives both things at once — a name that survives
	// any toggling, and one that says what the table IS to whoever opens the database with a query
	// tool. AccountingRegister1005_DebitTotals reads; AccountingRegister1005_Tt1012 does not.
	//
	// A user can now switch correspondence as often as they like: the debit table keeps its name and
	// only the credit one comes and goes, which is exactly what the two sides mean.
	//
	// ⚠ Existing bases carry the old names — this is a one-time rename, not a migration the engine
	// performs. Nothing reads the old spelling any more, so an old base must be re-created.
	wxString GetTotalsTableNameDB(bool creditSide) const {
		wxASSERT(m_metaId != 0);
		const ibValueMetaObjectRegisterTotals* totals = GetTotalsObject(creditSide);
		wxASSERT(totals != nullptr);   // a side without its totals object is refused before this (schema)
		return wxString::Format(wxT("%s%i_%s"), GetClassName(), GetMetaID(),
			totals != nullptr ? totals->GetName() : wxString(creditSide ? wxT("CreditTotals") : wxT("DebitTotals")));
	}

	// The read view over a side's totals. A view is an ordinary named relation, so nothing upstream
	// learns that the numbers come from a trigger-maintained table.
	wxString GetTurnoverViewName(bool creditSide) const {
		return GetTotalsTableNameDB(creditSide) + wxT("_Turnovers");
	}

	ibValueMetaObjectRegisterTotals* GetTotalsObject(bool creditSide) const {
		return creditSide ? static_cast<ibValueMetaObjectRegisterTotals*>(m_totalsCr)
		                  : static_cast<ibValueMetaObjectRegisterTotals*>(m_totalsDr);
	}

	// STRUCTURE: the movements table (base) PLUS the derived totals bundle — the tables, the triggers
	// that keep them current, and the read views that are their public surface.
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	// Is the materialised surface usable? False when the driver cannot maintain derived state (ODBC) —
	// the register then answers from live aggregation over the movements, correct at any scale and only
	// slower. Every reader must ask, because the answer decides which path it takes.
	bool HasMaterializedViews() const;

	// The granularity totals are STORED at — the floor under every reading, not the periodicity of one.
	// Day, for the reason the neighbour states: compression is still large (a key sees many movements a
	// day) while everything an accounting report actually asks for is day or coarser. Anything finer —
	// and anything per recorder or per line — is answered from the MOVEMENTS.
	ibTotalsPeriod GetTotalsPeriodUnit() const { return ibTotalsPeriod::Day; }

	// A read surface as an ORDINARY DB SOURCE, built on demand and cached. Same arrangement as the
	// shape cache above, including the retired list: a reader may hold a pointer into one that has
	// been rebuilt.
	const ibBackendQueryable* GetTurnoverViewQueryable(bool creditSide) const;

	// Is this register's totals row split across shards? A schema question, answered by the designer's
	// switch — turning it on or off changes the totals KEY, so it takes effect through an Apply and the
	// regenerator rebuilds the table because a re-keyed row cannot be migrated in place.
	bool IsTotalsSplitEnabled() const { return m_propertySplitTotals->GetValueAsBoolean(); }

	// How many shards a split register uses. Fixed rather than configurable: the number that matters is
	// "more than one", and every extra shard is paid on every read of every row.
	static constexpr unsigned int kTotalsShardCount = 8;

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
	// ⭐⭐ THE STANDARD ATTRIBUTES, IN THE ORDER A PERSON READS THEM:
	//
	//     Period · Recorder · LineNumber · Active · Account · <all the KINDS> · <all the VALUES>
	//
	// The order is not decoration — this list is what the designer shows, what a generated form lays
	// out and what a `SELECT *` returns. WHEN a movement happened, WHAT wrote it, whether it is in
	// force, WHAT it is against, and only then the analytical breakdown: kinds together, values
	// together, so a reader sees the vocabulary of the breakdown before its contents.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRegisterData::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributePeriod->GetMetaObject());
		array.push_back(m_propertyAttributeRecorder->GetMetaObject());
		array.push_back(m_propertyAttributeLineNumber->GetMetaObject());
		array.push_back(m_propertyAttributeLineActive->GetMetaObject());
		// WHAT A LINE IS MADE OF depends on whether it is one side or a whole posting.
		//
		// One-sided: RecordType says which side this row is, and there is a single Account.
		// Correspondence: the row names BOTH accounts and needs no side flag — which side a figure
		// belongs to is said by which account it sits against.
		//
		// ⭐⭐ BUT THE COLUMN LIST DOES NOT FOLLOW THE SETTING — ALL THREE ARE ALWAYS DECLARED.
		//
		// Conditioning it made the movements table gain and lose columns as a checkbox was clicked,
		// and a column that disappears takes its DATA with it: switch correspondence off and every
		// credit account ever recorded is dropped, switch it back and the column returns EMPTY. Worse
		// for the engine, the two snapshots then disagree about a predefined attribute that never
		// actually left the configuration, which is how "column FLDnnnn_RTRef does not exist" reached
		// the credit totals trigger three edits later.
		//
		// Both accounts and the record type therefore always exist. What the setting decides is what
		// gets WRITTEN: a one-sided register fills RecordType and leaves AccountCr empty, a
		// correspondence one fills both accounts and leaves RecordType empty. Two spare columns per
		// register cost nothing; a column that comes and goes costs the data in it.
		array.push_back(m_propertyAttributeRecordType->GetMetaObject());
		array.push_back(m_propertyAttributeAccount->GetMetaObject());   // the DEBIT account in correspondence
		if (m_accountCr != nullptr)
			array.push_back(m_accountCr);
		// Only the ACTIVE slots are part of the object — that is what makes the count a schema
		// decision: a slot outside it contributes no column, so lowering the number drops one.
		//
		// ⚠ ALL THE KINDS, THEN ALL THE VALUES — not pair by pair. The kinds are the vocabulary of the
		// breakdown and the values are what was filed under it, and a list that alternates between the
		// two reads as eight unrelated fields instead of two groups of four.
		for (unsigned int idx = 0; idx < m_accountDimensionCount; idx++)
			array.push_back(m_accountDimensionKinds[idx]);
		for (unsigned int idx = 0; idx < m_accountDimensionCount; idx++)
			array.push_back(m_accountDimensionSlots[idx]);

		// The credit side exists only in correspondence mode, and then it is the same shape again.
		for (unsigned int idx = 0; idx < m_accountDimensionKindsCr.size(); idx++)
			array.push_back(m_accountDimensionKindsCr[idx]);
		for (unsigned int idx = 0; idx < m_accountDimensionSlotsCr.size(); idx++)
			array.push_back(m_accountDimensionSlotsCr[idx]);
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
	// ON by default: double entry is what an accounting register is FOR, and a one-sided one is the
	// special case (a register of quantities, a memo book). Defaulting to off meant every new register
	// started as the exception and had to be corrected into the rule.
	ibPropertyBoolean* m_propertyCorrespondence = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("Correspondence"), _("Correspondence"), true);

	ibPropertyChartOfAccounts* m_propertyChartOfAccounts = ibPropertyObject::CreateProperty<ibPropertyChartOfAccounts>(m_categoryData, wxT("ChartOfAccounts"), _("Chart of accounts"));

	// SPLIT TOTALS — spread one logical totals row across several physical ones, so concurrent posters
	// stop queueing on the same row. OFF by default, and deliberately a switch rather than anything
	// automatic: the benefit is local (it relieves the HOT key) while the cost is global (every read of
	// every row sums the shards), so only somebody who has profiled the register knows whether it pays.
	//
	// An accounting register wants it MORE than its neighbour, not less: a posting run hits the same
	// few accounts — cash, revenue, VAT — from every document, so the hot key is the norm rather than
	// the exception. The mechanism is the accumulation register's, unchanged: the shard column joins
	// the KEY, which is what makes several physical rows legal for one logical one.
	// ON by default. Splitting spreads the totals rows a concurrent writer lands on, so two sessions
	// posting at once stop queueing behind the same row; the cost is rows that a read folds back
	// together, which the read does anyway. A register is written to concurrently as the ordinary
	// case, so the contended shape is the one to start from.
	ibPropertyBoolean* m_propertySplitTotals = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("SplitTotals"), _("Split totals"), true);

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

	// L4 virtual-table descriptors — registered beside the base records descriptor on run, dropped on
	// close. Five of them, and DrCrTurnovers only in correspondence mode: a one-sided line discards
	// which debit answered which credit at WRITE time, so no reading can recover the pair (§7a). A
	// table that would answer with a self-join over the recorder — M debits against N credits, each
	// amount counted N times — is not a cheaper equivalent; it is a wrong number.
	ibAcctSourceDescriptor m_balance            { this, ibAcctShape::Balance };
	ibAcctSourceDescriptor m_turnover           { this, ibAcctShape::Turnovers };
	ibAcctSourceDescriptor m_drCrTurnover       { this, ibAcctShape::DrCrTurnovers };
	ibAcctSourceDescriptor m_balanceAndTurnover { this, ibAcctShape::BalanceAndTurnovers };
	ibAcctSourceDescriptor m_records            { this, ibAcctShape::Records };

	// The totals tables — held for their IDENTITY (see ibValueMetaObjectRegisterTotals). Predefined
	// children: born WITH the register, pinned to it for life, serialised as sub-nodes of the
	// register's own node. Which of them declares a table follows the correspondence mode, so switching
	// it is a DROP plus a CREATE rather than an ALTER of something that was never there.
	//
	// Declared with their initialiser HERE rather than assigned in a constructor: every constructor
	// then brings them into being, and there is no second one to forget — the same way every property
	// on this class is declared.
	// ⚠ BRACES, not `=`: ibValuePtr's constructor from a raw T* is explicit, and `=` here is
	// copy-initialisation, which may not pick an explicit constructor. MSVC accepts it anyway;
	// GCC and Clang refuse the whole translation unit. Direct-initialisation is the portable
	// spelling of the same thing (docs/portability.md).
	ibValuePtr<ibValueMetaObjectRegisterTotals> m_totalsDr {
		CreateMetaObjectAndSetParent<ibValueMetaObjectRegisterTotals>(wxT("DebitTotals"), _("Debit totals")) };
	ibValuePtr<ibValueMetaObjectRegisterTotals> m_totalsCr {
		CreateMetaObjectAndSetParent<ibValueMetaObjectRegisterTotals>(wxT("CreditTotals"), _("Credit totals")) };

	// Built surfaces — the per-call shapes AND the two turnover views, in one cache: they are the same
	// kind of thing under two names, and both are keyed by a string the builder chooses (the whole
	// call for a shape, the view's name for a view). The cache, the signature check and the RETIRED
	// list are `ibRegSurfaceCache` (registerQueryLowering.h), shared with the accumulation register.
	mutable ibRegSurfaceCache m_surfaces;

	friend class ibValueRecordSetObjectAccountingRegister;
	friend class ibAcctBalanceQueryable;
	friend class ibAcctTurnoverQueryable;
	friend class ibAcctDrCrTurnoverQueryable;
	friend class ibAcctBalanceAndTurnoverQueryable;
	friend class ibAcctRecordsQueryable;
	friend class ibMetaData;
};

//********************************************************************************************
//*  The companion queryables — call-scoped virtual tables over the movements                *
//********************************************************************************************
//
// ⭐⭐ ONE BASE, FIVE READINGS. What actually differs between them is exactly two things: WHICH SHAPE
// each publishes, and WHAT it computes. Everything else — which provider vends the rows, which columns
// a query may name — is one answer, written once. (The accumulation register learned this the hard
// way: three copies of that answer, and the navigation swung with the road on one of them.)
//
// Every one of them is computed in RAM today, because this register has no materialised surface yet.
// That is a property of the READING, not of its interface: when the totals bundle lands (§7 of the
// arc, "two guarded accumulations"), a reading overrides GetSourceRelation and becomes a derived table
// on the server, and nothing above it changes — the same columns, the same rows, the same numbers.
class BACKEND_API ibAcctTotalsQueryable : public ibComputedRegisterQueryable<ibValueMetaObjectAccountingRegister> {
public:
	ibAcctTotalsQueryable(const ibValueMetaObjectAccountingRegister* reg, ibAcctShape shape,
	                      const std::vector<ibValue>& kindsDr, const std::vector<ibValue>& kindsCr,
	                      const ibValue& condition = ibValue())
		: ibComputedRegisterQueryable(reg), m_shape(shape), m_kindsDr(kindsDr), m_kindsCr(kindsCr),
		  m_condition(condition) {}

	// The condition as it arrived. Its DIMENSION half is already a predicate (m_filter on each
	// reading); its ACCOUNT-DIMENSION half is a question about the slots and is built per pass, where
	// the side is known — see ibAcctCallArgs.
	const ibValue& Condition() const { return m_condition; }

	// ⭐ NAVIGATE THROUGH THE SHAPE THIS READING PUBLISHES, never through the register. The register's
	// own columns are the MOVEMENT columns — AccountDimension1, AccountDimension1Kind, RecordType — and
	// reporting those for a balance row is the one answer that is never true of a virtual table: a query
	// would be told this source has `AccountDimension1` and no `AmountBalanceDr` at all.
	virtual const ibBackendQueryable* NavigationSource() const override {
		return m_reg->GetShapeQueryable(m_shape, m_kindsDr, m_kindsCr, Fold());
	}

	// ⭐ THE GRANULARITY IS PART OF THE SHAPE, so the shape has to be able to ask for it. A reading
	// that folds the interval WHOLE has no period column at all — offering one would promise a date
	// the rows will not carry — while a monthly reading has exactly one. Readings that take no
	// periodicity answer with the default, which is precisely "the interval whole".
	virtual ibRegFold Fold() const { return ibRegFold(); }

	// ⭐⭐ CAN THIS CALL BE ANSWERED BY THE SERVER — asked per READING and per CALL, defaulting to NO.
	//
	// The default is the whole safety of the arrangement: a reading that has not been taught the
	// server road keeps computing in RAM exactly as it does today, and the door never asks it for a
	// relation it cannot build. Answering yes globally would hand the door a NULL relation for four
	// readings out of five and let it scan the movements table raw — a balance query returning
	// movement lines, which is a wrong answer that still looks like rows.
	//
	// A reading says yes only when EVERY question in the call is one the stored surface can hold:
	// the driver maintains it at all, the shape is one table (correspondence keeps two, one per
	// side), the breakdown was not asked for BY KIND (that needs the slot CASE), and the fold is not
	// finer than the stored grain. Anything else is a plausible wrong number, so it takes the RAM road.
	virtual bool CanReadOnServer() const { return false; }

	// The two answers that follow from it. Computed in RAM unless this call can go to the server; and
	// then the ordinary PHYSICAL provider, so the source behaves like any other relation.
	virtual bool IsComputedInRam() const override { return !CanReadOnServer(); }
	virtual ibBackendQueryProvider& GetProvider() const override {
		return IsComputedInRam() ? ibComputedRegisterQueryable::GetProvider() : ibBackendQueryable::GetProvider();
	}

protected:
	ibAcctShape         m_shape;
	std::vector<ibValue> m_kindsDr;   // the requested breakdown, in the CALLER's order — column i is kinds[i]
	std::vector<ibValue> m_kindsCr;
	ibValue              m_condition; // the condition as it arrived — its slot half is built per pass
};

// Balance — debit and credit balance per account (+ the requested breakdown), at a moment.
class BACKEND_API ibAcctBalanceQueryable : public ibAcctTotalsQueryable {
public:
	ibAcctBalanceQueryable(const ibValueMetaObjectAccountingRegister* reg,
	                       const ibRegBound& bound = ibRegBound(),
	                       const ibQueryPredicatePtr& accountDr = nullptr, const ibQueryPredicatePtr& accountCr = nullptr,
	                       const std::vector<ibValue>& kindsDr = {}, const std::vector<ibValue>& kindsCr = {},
	                       const ibQueryPredicatePtr& filter = nullptr, const ibValue& condition = ibValue())
		: ibAcctTotalsQueryable(reg, ibAcctShape::Balance, kindsDr, kindsCr, condition),
		  m_bound(bound), m_accountDr(accountDr), m_accountCr(accountCr), m_filter(filter) {}

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	// ⭐⭐ THE SERVER ROAD. A balance is the turnovers folded UP TO a moment — the same stored surface
	// the turnover reading stands on, read with one more condition (`UpToTo`). What it needs beyond
	// the neighbour's spec is a JOIN: how the two sides fold is a rule the CHART holds, not the
	// surface, so the relation is built AROUND the materialised read rather than inside it.
	virtual bool CanReadOnServer() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
private:
	ibRegBound          m_bound;      // the moment — a date, or a date AND the document at it
	ibQueryPredicatePtr m_accountDr;   // a CONDITION over the account, consumed by this reading
	ibQueryPredicatePtr m_accountCr;
	ibQueryPredicatePtr m_filter;

	// The gate asks the DATA one question (is any kind marked turnovers-only), and a companion is
	// CALL-SCOPED — so the answer is cached for exactly the life of this reading and no longer.
	// -1 = not asked yet.
	mutable int         m_serverRoad = -1;
};

// Turnovers — debit and credit turnover over an interval, optionally cut into periods.
class BACKEND_API ibAcctTurnoverQueryable : public ibAcctTotalsQueryable {
public:
	ibAcctTurnoverQueryable(const ibValueMetaObjectAccountingRegister* reg,
	                        const ibRegBound& begin = ibRegBound(), const ibRegBound& end = ibRegBound(),
	                        const ibQueryPredicatePtr& accountDr = nullptr, const ibQueryPredicatePtr& accountCr = nullptr,
	                        const std::vector<ibValue>& kindsDr = {}, const std::vector<ibValue>& kindsCr = {},
	                        const ibQueryPredicatePtr& filter = nullptr, const ibRegFold& fold = ibRegFold(), const ibValue& condition = ibValue())
		: ibAcctTotalsQueryable(reg, ibAcctShape::Turnovers, kindsDr, kindsCr, condition),
		  m_begin(begin), m_end(end), m_accountDr(accountDr), m_accountCr(accountCr),
		  m_filter(filter), m_fold(fold) {}

	virtual ibRegFold Fold() const override { return m_fold; }
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	// ⭐⭐ THE FIRST READING ON THE SERVER ROAD, and the mechanism is the accumulation register's —
	// its read spec, its arm cut, its boundary tuple — not a second one written here. What it buys
	// beyond speed is the half of a BOUNDARY that the RAM road cannot say: a moment that names a
	// DOCUMENT. A movement is owned by its recorder and dated beside it, so a register's row is
	// ordered by period · recorder · line number (GetPrimaryKeyColumns says exactly that), and "the
	// turnovers up to THIS document" is a comparison over that order — which the spec already
	// carries (m_boundaryTail) and a column-shaped predicate cannot express at all.
	virtual bool CanReadOnServer() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
private:
	ibRegBound          m_begin, m_end;
	ibQueryPredicatePtr m_accountDr, m_accountCr;   // conditions, consumed here
	ibQueryPredicatePtr m_filter;
	ibRegFold           m_fold;   // the READ granularity — a query parameter, not a schema property
};

// DrCrTurnovers — the correspondence matrix: one row per (debit account, credit account) pair.
// ⚠ EXISTS ONLY IN CORRESPONDENCE MODE, and that is a decision rather than a gap (§7a).
class BACKEND_API ibAcctDrCrTurnoverQueryable : public ibAcctTotalsQueryable {
public:
	ibAcctDrCrTurnoverQueryable(const ibValueMetaObjectAccountingRegister* reg,
	                            const ibRegBound& begin = ibRegBound(), const ibRegBound& end = ibRegBound(),
	                            const ibQueryPredicatePtr& accountDr = nullptr, const ibQueryPredicatePtr& accountCr = nullptr,
	                            const std::vector<ibValue>& kindsDr = {}, const std::vector<ibValue>& kindsCr = {},
	                            const ibQueryPredicatePtr& filter = nullptr, const ibValue& condition = ibValue())
		: ibAcctTotalsQueryable(reg, ibAcctShape::DrCrTurnovers, kindsDr, kindsCr, condition),
		  m_begin(begin), m_end(end), m_accountDr(accountDr), m_accountCr(accountCr), m_filter(filter) {}

	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	// ⭐⭐ ON THE SERVER, AND NOT FROM THE TOTALS. This reading groups the MOVEMENTS — a totals row is
	// keyed by ONE account and the other side was never stored beside it — and it always did so on the
	// server. What it gains here is the ENDING: a relation instead of materialised rows, so a query
	// that joins to it or filters over it stays one statement.
	virtual bool CanReadOnServer() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
private:
	ibRegBound          m_begin, m_end;
	ibQueryPredicatePtr m_accountDr, m_accountCr;   // conditions, consumed here
	ibQueryPredicatePtr m_filter;
};

// BalanceAndTurnovers — opening / debit / credit / closing in one row.
class BACKEND_API ibAcctBalanceAndTurnoverQueryable : public ibAcctTotalsQueryable {
public:
	ibAcctBalanceAndTurnoverQueryable(const ibValueMetaObjectAccountingRegister* reg,
	                                  const ibRegBound& begin = ibRegBound(), const ibRegBound& end = ibRegBound(),
	                                  const ibQueryPredicatePtr& accountDr = nullptr, const ibQueryPredicatePtr& accountCr = nullptr,
	                                  const std::vector<ibValue>& kindsDr = {}, const std::vector<ibValue>& kindsCr = {},
	                                  const ibQueryPredicatePtr& filter = nullptr, const ibRegFold& fold = ibRegFold(),
	                                  const ibValue& condition = ibValue(), bool fillEmptyPeriods = false)
		: ibAcctTotalsQueryable(reg, ibAcctShape::BalanceAndTurnovers, kindsDr, kindsCr, condition),
		  m_begin(begin), m_end(end), m_accountDr(accountDr), m_accountCr(accountCr),
		  m_filter(filter), m_fold(fold), m_fillEmptyPeriods(fillEmptyPeriods) {}

	virtual ibRegFold Fold() const override { return m_fold; }
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	// THE SERVER ROAD — the same stored surface read THREE WAYS AT ONCE: what was carried in
	// (`BeforeFrom`), what moved (`InRange`), what remains (`UpToTo`). One pass, no join between them
	// and no window; the only join is the one the balance halves need, to the chart of accounts.
	virtual bool CanReadOnServer() const override;
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
private:
	ibRegBound          m_begin, m_end;
	ibQueryPredicatePtr m_accountDr, m_accountCr;   // conditions, consumed here
	ibQueryPredicatePtr m_filter;
	ibRegFold           m_fold;
	// ⭐ A PERIOD NOTHING MOVED IN IS STILL A PERIOD — when asked for. The balance carried across it
	// is the whole reason this table exists, so "movements and period boundaries" reports the empty
	// ones too, carrying the closing balance of the previous period into them.
	bool                m_fillEmptyPeriods = false;

	mutable int         m_serverRoad = -1;   // see ibAcctBalanceQueryable — call-scoped, -1 = not asked
};

// RecordsWithAccountDimensions — the movement LINES themselves, with the dimension slots widened
// into a column per requested kind. Not a total and never will be: recorder and line number are
// precisely what a fold discards, so a table that reports them can only be the movements (§7a).
class BACKEND_API ibAcctRecordsQueryable : public ibAcctTotalsQueryable {
public:
	ibAcctRecordsQueryable(const ibValueMetaObjectAccountingRegister* reg,
	                       const ibRegBound& begin = ibRegBound(), const ibRegBound& end = ibRegBound(),
	                       const std::vector<ibValue>& kindsDr = {}, const std::vector<ibValue>& kindsCr = {},
	                       const ibQueryPredicatePtr& filter = nullptr, const ibValue& condition = ibValue(),
	                       const ibValue& order = ibValue(), long top = 0)
		: ibAcctTotalsQueryable(reg, ibAcctShape::Records, kindsDr, kindsCr, condition),
		  m_begin(begin), m_end(end), m_filter(filter), m_order(order), m_top(top) {}

	// A row here IS a movement line, so it carries the period, the document and the line within it —
	// which is exactly what the Record granularity names. Nothing is folded, so nothing is dropped.
	virtual ibRegFold Fold() const override {
		ibRegFold fold; fold.m_kind = ibRegGranularity::Record; return fold;
	}
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

	// ⭐ A PROJECTION, SO IT NEVER NEEDED THE TOTALS — and now it does not need RAM either. The rows
	// were always read from the movements on the server; what they gained is the ending.
	virtual bool CanReadOnServer() const override { return m_reg != nullptr; }
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const override;
private:
	ibRegBound          m_begin, m_end;
	ibQueryPredicatePtr m_filter;
	ibValue             m_order;      // field names, in the order they sort; empty = as they come
	long                m_top = 0;    // 0 = all of them
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

	const ibValueMetaObjectAccountingRegister* GetAccountingMetaObject() const {
		return static_cast<const ibValueMetaObjectAccountingRegister*>(GetMetaObject());
	}

	// ⭐⭐ THE DIMENSIONS OF ONE LINE, ADDRESSED BY KIND — never by the slot's number.
	//
	//     movement.AccountDimensionDr[Kinds.Contractor] = contractorRef
	//
	// Position is not the author's business: which slot a kind occupies is decided by the ORDER of the
	// kinds table on the ACCOUNT, and the same kind sits in different slots on different accounts.
	// Addressing by kind is what makes one posting, written once, work for every account that admits
	// that kind.
	//
	// ⭐ AND THE ASSIGNMENT IS AN ADJUST. The kind carries its own `Type` (a characteristic SELECTS from
	// what the chart permits), so the value handed in is adjusted to that type on the way into the slot
	// — the same appliance a table column uses when a value is written into it. The kind is the FILTER
	// that narrows the slot; the slot's own type is the whole contour, which is what the column admits.
	//
	// ⚠ It holds the LINE'S POSITION, not the line object. A line is a temporary the caller owns, and a
	// collection that outlived one would be reading freed memory the moment a script kept it in a
	// variable — so each access asks the record set for the row again.
	class ibValueAccountDimensions : public ibValueDynamicMembers {
	public:
		ibValueAccountDimensions(ibValueRecordSetObjectAccountingRegister* recordSet = nullptr,
		                         const ibDataViewItem& line = ibDataViewItem(), bool creditSide = false);
		virtual ~ibValueAccountDimensions();

		virtual bool IsEmpty() const { return false; }

		void FillMembers(ibMemberTable& helper) const;
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		// `[kind]` — the two halves of the pair are written together: the kind lands in its own column
		// beside the value, which is what makes a stored movement self-describing.
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		// ⭐⭐ AND BY NAME, LIKE A STRUCTURE: `row.AccountDimensionDr.Contractor = value`.
		//
		// The names are not declared anywhere — they are the KINDS THE ACCOUNT DECLARES, read off its
		// kinds table at the moment of the call, which is why they are resolved dynamically instead of
		// being built into a member table. A posting written this way says what it means at a glance,
		// and it still goes through the one road every write takes: find the slot holding that kind or
		// take a free one, write the pair, adjust the value to the kind's own type.
		//
		// ⚠ The account has to be filled in FIRST — the names come from it. That is not an ordering
		// quirk to hide: until the row names an account, there is no such thing as "its analytics".
		virtual long     FindProp(const wxString& strPropName) const override;
		virtual long     GetNProps() const override;
		virtual wxString GetPropName(const long lPropNum) const override;
		virtual bool     IsPropReadable(const long lPropNum) const override { return true; }
		virtual bool     IsPropWritable(const long lPropNum) const override { return true; }
		virtual bool     SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
		virtual bool     GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

		virtual wxString GetString() const;

		// Empty the whole side — every kind and every value of it. Reached as `Clear()` from a script,
		// and as the meaning of assigning nothing to the collection.
		void Clear();

	private:
		// The kinds THIS ROW'S ACCOUNT declares, in its own order, each with the name a script writes.
		// Read per call: the names are data, and until the row names an account there is no such thing
		// as "its analytics".
		std::vector<std::pair<wxString, ibValue>> DeclaredKinds() const;

		ibValueRecordSetObjectAccountingRegister* m_recordSet;
		ibDataViewItem                            m_line;
		bool                                      m_creditSide;
	};

	// A line of an accounting register — an ordinary register line PLUS the dimension collections. The
	// class is nested because the base line type is protected on the record set: a line belongs to its
	// set, and this one belongs to this set.
	class ibValueAccountingLine : public ibValueRecordSetObjectRegisterReturnLine {
	public:
		ibValueAccountingLine(ibValueRecordSetObjectAccountingRegister* ownerTable = nullptr,
		                      const ibDataViewItem& line = ibDataViewItem());
		virtual ~ibValueAccountingLine();

		void FillMembers(ibMemberTable& helper) const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

		// The synthetic property ids for the dimension collections — outside any metaID, because these
		// are not attributes: they are a VIEW over the slot pairs, which are.
		enum {
			ePropAccountDimension   = 0x51000001,
			ePropAccountDimensionDr = 0x51000002,
			ePropAccountDimensionCr = 0x51000003,
		};

	private:
		ibValueRecordSetObjectAccountingRegister* m_ownerSet;
	};

	void FillMembers(ibMemberTable& helper) const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// ⭐ THE DOUBLE-ENTRY CHECK, at the moment a posting becomes data. Debits must equal credits — that
	// is what makes it accounting rather than a list of amounts — and it is asked of the BALANCE-BEARING
	// resources only (a quantity legitimately differs between the sides). Off-balance accounts are
	// exempt: that circuit exists precisely to stay OUT of the balance, so it is a separator and its
	// entries have no counterpart by definition.
	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true) override;

private:

	// Raises when the sides disagree, naming the resource and the difference. A set that is written
	// through the RECORDER carries one document's postings, which is exactly the scope the rule is
	// stated over.
	void CheckDoubleEntry() const;
};

#endif
