////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register — the derived TOTALS bundle declaration (L3-2)
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"

#include "backend/backend_exception.h"                            // ibBackendCoreException — a missing side stops the apply
#include "backend/query/schemaSnapshot.h"
#include "backend/query/queryColumn.h"
#include "backend/query/columnLayout.h"                          // DescribeColumnLayout -- physical fields WITH their canonical types
#include "backend/query/tempTableQueryable.h"                       // ibSchemaTableQueryable — the totals table as a source
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/resource/metaResourceObject.h"
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegValueField
#include "backend/databaseLayer/databaseMaterializeBuilder.h"       // ibCanMaterialize — ask L2-2, never a dialect
#include "backend/appData.h"                                        // db_query
#include "backend/valueInfo.h"                                      // ibReference — the width of a zero id
#include "backend/metaData.h"                                       // ibMetaData whole — AdjustValue(…, account->GetMetaData()) must see it is no ibValue
#include "backend/system/value/valueType.h"                         // ibValueTypeDescription::AdjustValue — the account's empty reference

// ⭐⭐ THE STORED NAME OF A SIDE'S TURNOVER COLUMN — `<the resource's own field>TurnoverDr|Cr`, spelled
// HERE and only here, for the CREATE VIEW that makes it and for the source a query reads the view
// through (GetTurnoverViewQueryable). The twin of ibAccumFigureField, and the same story: cb1edbae moved
// the view's columns from the resource's user name to its field and left the source asking for
// `AmountTurnoverDr`, which no view applied since then has.
static wxString ibAcctTurnoverField(const wxString& resourceField, bool credit)
{
	return resourceField + ibRegSidedFigure(ibRegFigure::Turnover, credit);
}

// ============================================================================
// What this file DECLARES (it renders nothing):
//
//   mov      — the movements, contributed by the base. SOURCE data: dumped, restored, never derived.
//   totals   — one row per (period, ACCOUNT, its analytical breakdown, the register's dimensions),
//              holding each resource's turnover on ONE side. DERIVED: never moved, regenerated on
//              demand, kept current by trigger.
//   views    — the read surface. Ordinary relations upstream.
//
// ⭐⭐ TWO ACCUMULATIONS, ONE PER SIDE — and that is what the shape of accounting forces.
//
// A totals row is keyed by the account it is ABOUT. In a correspondence register one movement is about
// two accounts at once: it raises the debit turnover of one and the credit turnover of another, each
// under its own analytical breakdown. Two different keys cannot be one upsert, so there are two tables
// and two deltas. A one-sided register has one key per row (the side is said by RecordType), so it
// keeps ONE table and branches in the accumulated VALUE instead — exactly the way the accumulation
// register separates receipt from expense.
//
// ⭐⭐ WHAT IS STORED IS TURNOVER, NEVER BALANCE, and this is forced rather than preferred. The balance
// key is NARROWER than the turnover key, and by how much is decided per account — the "turnovers only"
// flag sits in a row of that account's kinds table, i.e. in data the schema cannot see. A stored
// balance would therefore need a key that changes as a user ticks a checkbox. Turnover is stored at the
// full key; a balance is that turnover folded up to a moment, with the turnovers-only breakdowns
// dropped at READ time (accountingRegisterMetadataTotals.cpp).
//
// Everything below speaks L2-2's vocabulary (Value / Difference). The words "debit" and "credit"
// appear only in COLUMN NAMES, which is metadata's business; the layer that renders the SQL never
// learns them.
// ============================================================================

void ibValueMetaObjectAccountingRegister::ContributeTables(ibSchemaSnapshot& out) const
{
	// The movements table, its indexes and its columns.
	ibValueMetaObjectRegisterData::ContributeTables(out);

	// ⭐⭐ ONE CHART OF ACCOUNTS, REFUSED BEFORE THE FIRST STATEMENT.
	//
	// Everything this register's schema is made of comes from that chart: the Account column's type,
	// how many analytics slots exist, and — through the chart's own characteristic chart — what a slot
	// may hold. None, and those are three unanswerable questions; two, and they are answered twice.
	//
	// ⚠ WHY HERE AND NOT ON SAVE. Raising from inside OnSaveMetaObject leaves the configuration write
	// transaction open — the exception unwinds past whatever would have closed it — and the NEXT save
	// waits on that lock until it times out as a deadlock on sys_config_save, naming neither the rule
	// nor the register. A rule attached to the declaration runs before any statement, so a refusal
	// costs nothing and says what it is about.
	{
		const wxString registerName = GetName();
		const unsigned int chartCount = m_propertyChartOfAccounts->GetValueAsMetaDesc().GetTypeCount();

		out.Shared(GetMetaID(), GetPhysicalTableName()).m_beforeChange =
			[registerName, chartCount](ibRestructureInfo* report) -> bool {
				if (chartCount == 1)
					return true;
				if (report != nullptr) {
					report->AppendError(chartCount == 0
						? wxString::Format(_("accounting register '%s': a chart of accounts is required - the account type, the number of analytics and their type all come from it"), registerName)
						: wxString::Format(_("accounting register '%s': only ONE chart of accounts may be used - two would answer the same questions twice"), registerName));
				}
				return false;
			};
	}

	// A register with NO RESOURCES has nothing to total: movements can be written, but no figure will
	// ever be accumulated from them and every report over it reads empty.
	//
	// WARN rather than refuse — it is a legitimate intermediate state (a register is built up over
	// several edits), and it is also a state nobody wants to KEEP. Silence about it is how one ships.
	if (GetResourceArrayObject().empty()) {
		RestructureWarning(wxString::Format(
			_("Register '%s' has no resources: no totals are maintained for it"), GetName()));
		return;
	}

	// ⭐⭐ A SLOT WITH NO TYPE IS A COLUMN NOTHING CAN ENTER, AND IT USED TO BE BUILT IN SILENCE.
	//
	// The analytics slots are typed from the characteristic chart bound to the chart of accounts: the
	// KIND half is a reference to a characteristic, the VALUE half is whatever that chart says a
	// characteristic may be. When that binding answers nothing — no chart bound, or the object not
	// resolvable at the moment the register was run — the typing step simply does not fire, and the
	// slot keeps an empty type description.
	//
	// The database shows it plainly: the slot gets its discriminator column and NOTHING else, so no
	// value of any type can ever be written into it. The apply, meanwhile, reports success. Worse, the
	// next apply sees the slot typed on one side of the diff and empty on the other, and tries to drop
	// columns that were never created — which is where "column FLDnnnn_RTRef does not exist" comes
	// from, three edits away from the cause.
	//
	// A WARNING and not a refusal: a configuration is built up in steps, and a chart bound after the
	// register is an ordinary order of work. What must not happen is silence.
	{
		unsigned int untyped = 0;
		for (unsigned int idx = 0; idx < GetAccountDimensionCount(); idx++) {
			const ibValueMetaObjectAttributeBase* kindSlot = GetAccountDimensionKindSlot(false, idx);
			const ibValueMetaObjectAttributeBase* slot     = GetAccountDimensionSlot(false, idx);
			// Asked of the attribute, never counted here — the same predicate the recorder rule uses
			// (metaAttributeObject.h). Counting classes at the callsite is how one question grows
			// three spellings.
			if ((kindSlot != nullptr && kindSlot->IsEmptyTypeDesc())
			 || (slot     != nullptr && slot->IsEmptyTypeDesc()))
				++untyped;
		}
		if (untyped != 0)
			RestructureWarning(wxString::Format(
				_("Register '%s': %u analytics slot(s) have no type - the chart of accounts names no characteristic chart, so nothing can be written into them"),
				GetName(), untyped));
	}

	const wxString periodField = ibRegValueField(GetRegisterPeriod());
	const bool correspondence = IsCorrespondence();

	// ONE SIDE'S TABLE. Called once for a one-sided register (which keeps both figures in it) and
	// twice for a correspondence one, where each side is keyed by its own account and its own
	// breakdown.
	//
	// 🛑 THERE WAS A SECOND GRAIN — the same table per account alone, beside this one per account and its
	// analytics — and no reading ever stood on it: every one reads the breakdown grain, and a total per
	// account is that grain summed. It cost two tables and two sets of triggers on every posting, and was
	// removed (Max, 2026-09-16: "if the tables are really superfluous, remove them").
	const auto declareSide = [&](bool creditSide) {
		const ibValueMetaObjectRegisterTotals* totals = GetTotalsObject(creditSide);
		// The credit side is keyed by the CREDIT account, always — the setting says whether rows are
		// written here, not what identifies them. Keying it by the debit account while correspondence
		// was off gave the table one shape and its maintenance another.
		const ibValueMetaObjectAttributeBase* account = creditSide
			? GetRegisterAccountCr() : GetRegisterAccount();

		// ⭐⭐ A MISSING SIDE IS A REFUSAL, NOT A SKIP.
		//
		// This used to return quietly, and the result was half a register that looked entirely
		// healthy: a CORRESPONDENCE register whose credit account had not been created got its debit
		// totals table, its debit triggers and its debit view, no credit anything, and an apply that
		// reported success. Nothing said a side was missing — not the ledger, not the log — so the
		// first sign of it is a balance that never has a credit turnover, months later, in data.
		//
		// Both are structural: the totals object is created with the register, and the credit account
		// is created the moment correspondence is switched on. Either one absent while correspondence
		// says otherwise means the metadata is inconsistent with itself, and the only correct answer
		// is to stop the apply and say which half is missing (docs/exceptions.md §5a).
		// (The credit side is only ever asked for when correspondence is on — see the two calls at the
		// end of this function — so there is no legitimate "this side does not exist" case to allow.)
		if (totals == nullptr || account == nullptr)
			ibBackendCoreException::Error(
				_("Accounting register '%s': the %s side declares no %s - the configuration is inconsistent"),
				GetName(),
				creditSide ? _("credit") : _("debit"),
				totals == nullptr ? _("totals table") : _("account"));

		const wxString totalsName = GetTotalsTableNameDB(creditSide);
		ibSchemaTable& t = out.Shared(totals->GetMetaID(), totalsName);

		// --- structure: the period, the account, its breakdown, the register's dimensions ----------
		const ibBackendQueryColumn* periodCol = t.Scaffold(ibBackendColumnRawDB::Date(periodField));
		t.Add(account->GetQueryColumn());   // same physical fields as the movements, so a trigger reads NEW.<field> directly

		std::vector<const ibBackendQueryColumn*> keyCols;
		keyCols.push_back(periodCol);
		keyCols.push_back(account->GetQueryColumn());

		// ⭐ THE BREAKDOWN IS PART OF THE KEY, KIND AND VALUE BOTH. The kind column is not decoration
		// here: the same slot holds a counterparty on one account and an item on another, so a key made
		// of values alone would merge two different breakdowns into one row. Storing the kind is also
		// what lets a READING drop a turnovers-only cut later — it can see what it is dropping.
		for (unsigned int idx = 0; idx < GetAccountDimensionCount(); idx++) {
			const ibValueMetaObjectAttributeBase* kindSlot = GetAccountDimensionKindSlot(creditSide, idx);
			const ibValueMetaObjectAttributeBase* slot     = GetAccountDimensionSlot(creditSide, idx);
			if (kindSlot == nullptr || slot == nullptr)
				continue;
			t.Add(kindSlot->GetQueryColumn());
			t.Add(slot->GetQueryColumn());
			keyCols.push_back(kindSlot->GetQueryColumn());
			keyCols.push_back(slot->GetQueryColumn());
		}

		// ⭐⭐ A NON-BALANCE DIMENSION KEYS EACH SIDE BY ITS OWN HALF. Balanced, it holds one value for the
		// whole entry and both totals tables key by that one column — the organisation a line belongs to
		// is the same on either side of it. Non-balanced, the two sides hold different values (the
		// currency received is not the currency paid), so the credit totals must be keyed by the credit
		// half; keyed by the debit one they would file a credit turnover under the other side's value.
		for (const auto dimension : GetDimensionArrayObject()) {
			const ibBackendQueryColumn* field = GetRegisterDimension(creditSide, dimension);
			if (field == nullptr)
				continue;
			t.Add(field);
			keyCols.push_back(field);
		}

		// SPLIT TOTALS: the shard column joins the KEY, which is what makes several physical rows legal
		// for one logical key. It exists only when the switch is on — turning the switch changes the key
		// shape, so it takes an Apply and the regenerator rebuilds the table.
		//
		// The column carries an IDENTITY rather than being scaffold: turning the switch ADDS a physical
		// column to an existing table (and turning it back REMOVES one), and scaffold columns are
		// created with their table and never migrated. The totals object's own metaID names it — there
		// is exactly one shard column per totals table and it belongs to that table.
		const bool split = IsTotalsSplitEnabled();
		const bool sharded = ibRegSplitIntoKey(t, totals, ShardColumnName(), split, keyCols);

		// ⭐⭐ THE KEY IS DECLARED; HOW IT IS HELD UNIQUE IS THE ENGINE'S QUESTION, ASKED THERE.
		//
		// This register's key is the widest in the tree — the period, the account, a (kind, value) PAIR
		// per analytic, the register's dimensions — and every reference among them is three physical
		// fields. At four analytics that is some twenty-one index segments against Firebird's sixteen:
		// "too many keys defined for index", which on Firebird rolls the whole apply back and names a
		// table that is not the problem. Nothing about the key can be shortened — the kind belongs in it
		// (§ above) as much as the value does — so the identity is carried by a hash column instead, and
		// ibDeclareDerivedKey decides which of the two shapes this engine gets.
		//
		// The totals object's metaID names that column, through the high band no metaID occupies — the
		// same arrangement the shard column uses one paragraph up, and for the same reason: the column
		// has to be ADDABLE to a table that already exists.
		ibDeclareDerivedKey(t, totalsName, keyCols, totals->GetMetaID() | 0x40000000);

		// A SIDE IS DECLARED WHOLE — table, columns, triggers and all — or not at all: the credit side
		// exists in correspondence mode only (see the declaration at the end). Which movements land in a
		// side that exists is the delta's guard further down, and that guard reads the ROW.
		ibSchemaMaterialize& m = t.Derived(GetQueryable());
		m.Split(sharded ? kTotalsShardCount : 1u);   // the COLUMN decides, not the setting — see ibRegSplitIntoKey

		// STORED GRANULARITY = DAY. It is the FLOOR on what can be read back — a projection is
		// derivable only into a unit no finer than the stored one — and everything below it (an hour, a
		// recorder, a line) is answered from the MOVEMENTS, which is where those questions belong.
		m.Period(periodField, GetRegisterPeriod()->GetQueryColumn(), wxT("{row}.") + periodField, GetTotalsPeriodUnit());

		// ⭐⭐ AN INACTIVE MOVEMENT EXISTS AND COUNTS FOR NOTHING. Active separates a record that is
		// THERE from a record that is IN FORCE: an entry written but not in effect occupies its row and
		// must not move a figure. Declared as the delta's GUARD — the one place that meaning belongs,
		// so no reading has to remember it.
		//
		// ⚠ THE GUARD IS AN EXPRESSION, AND IT HAS TO BE A BOOLEAN ONE. A "boolean" attribute is stored
		// as a SMALLINT, so `WHERE NEW.fld…_B` is a field, not a condition — Firebird refuses it with
		// "invalid usage of boolean expression", and the whole CREATE TRIGGER fails at apply time. The
		// comparison is written out.
		ibRegGuardInForce(m, GetRegisterActive());

		// ⚠ A CORRESPONDENCE ROW WITH AN EMPTY SIDE CONTRIBUTES NOTHING TO THAT SIDE. An off-balance
		// entry names one account legitimately (§ 4.5), and without this guard the other side's table
		// would accumulate a row keyed by an EMPTY account — a bucket that is not an account at all and
		// that every reading would then have to learn to ignore.
		//
		// ⚠ Written as a comparison for the same reason, and against the reference's TYPE field: an
		// empty reference is an all-zero guid rather than NULL, so "is there an account here" is
		// `_RTRef <> 0` — the same test the hierarchy rules use when they ask whether a row has a
		// parent.
		// ⭐⭐ AND THE SAME GUARD ANSWERS THE CORRESPONDENCE SETTING, so nothing else has to.
		//
		// It used to be conditional on the flag, which forced everything around it to be conditional
		// too: the credit table declared only in correspondence mode, its maintenance installed and
		// uninstalled as the flag moved, the table dropped and re-created to empty it. Each of those
		// made the schema a function of a checkbox, and each produced its own failure — a name that
		// could no longer be computed, a baseline that knew an object the target had stopped
		// declaring, a trigger built against columns that were not there.
		//
		// The ROW answers which movement a side takes: a one-sided register never fills AccountCr, so
		// `{row}.fld<AccountCr>_RTRef <> 0` is false for every movement it will ever write. WHETHER the
		// credit side is declared is the mode's since 2026-09-16 — see the declaration at the end: a
		// one-sided register no longer lists the credit account, so no trigger may be built against it.
		//
		// 🛑 AND AN EMPTY ACCOUNT IS AN EMPTY REFERENCE, which `_RTRef <> 0` alone does not see. The side an
		// off-balance entry leaves unnamed is written like any empty reference — its tag, the chart's table,
		// a ZERO id — so its `_RTRef` is the chart's and the row went into that side's totals under an empty
		// account: `Balance` grew a row crediting 1 000 to nobody and the books stopped agreeing by that much
		// (measured 2026-09-17 on the rehearsal ledger). A filled account and an empty one differ in the id
		// alone, so the guard asks it (a byte string, spelled by the engine — {binary:}); `_RTRef <> 0` stays
		// for a cell nobody wrote. The regeneration asks the same question of the value: not an empty
		// reference of the account's type, and not an unwritten cell.
		{
			// The fields are asked by their ROLE — the layout names them, this does not spell them.
			const wxString typeRefField = ibRegFieldOfRole(account->GetQueryColumn(), ibColumnRole::ReferenceType);
			const wxString idField      = ibRegFieldOfRole(account->GetQueryColumn(), ibColumnRole::ReferenceId);

			if (!typeRefField.IsEmpty() && !idField.IsEmpty()) {
				const wxString zeroId(wxT('0'), sizeof(ibReference) * 2);
				// The empty account of THIS register's configuration - the one the schema is declared for, which
				// in the designer is not necessarily the active one.
				const ibValue emptyAccount = ibValueTypeDescription::AdjustValue(account->GetQueryColumn()->GetTypeDesc(), account->GetMetaData());
				m.Guard(wxT("({row}.") + typeRefField + wxT(" <> 0) AND ({row}.") + idField + wxT(" <> {binary:") + zeroId + wxT("})"),
					ibRegSideNamed(account->GetQueryColumn(), emptyAccount));
			}
		}

		m.Key(account->GetQueryColumn());
		// The maintenance keys by what the table is keyed by — the breakdown with it.
		for (unsigned int idx = 0; idx < GetAccountDimensionCount(); idx++) {
			const ibValueMetaObjectAttributeBase* kindSlot = GetAccountDimensionKindSlot(creditSide, idx);
			const ibValueMetaObjectAttributeBase* slot     = GetAccountDimensionSlot(creditSide, idx);
			if (kindSlot != nullptr) m.Key(kindSlot->GetQueryColumn());
			if (slot     != nullptr) m.Key(slot->GetQueryColumn());
		}
		// …and the maintenance keys the same way the table is keyed — the credit side by the credit
		// half of every non-balance dimension. The two must never disagree: the key the trigger writes
		// by IS the key the table stores by.
		for (const auto dimension : GetDimensionArrayObject()) {
			if (const ibBackendQueryColumn* field = GetRegisterDimension(creditSide, dimension))
				m.Key(field);
		}

		// --- the stored columns + what a movement contributes to each ------------------------------
		struct Figure { wxString m_field, m_name; bool m_credit; };
		std::vector<Figure> figures;

		for (const auto res : GetResourceArrayObject()) {
			if (res == nullptr)
				continue;

			// ⭐⭐ EVERY FIGURE IS KEPT HERE, and the breakdown's kind of accounting does not change that.
			// It is worth saying why, because the opposite looks right: a resource names a breakdown's
			// kind, so surely only those figures belong in a table keyed by the breakdown?
			//
			// Not while there is ONE breakdown grain. A row here carries every slot at once, so a
			// figure stored in it is broken down by all of them, and a figure kept OUT of it is simply
			// missing — the readings that project a breakdown take their numbers from this surface and
			// would report nothing where the account plainly has a figure. What the kind decides is which
			// breakdown a reading OFFERS it under, and that is a question for the reading, not the storage.
			//
			// ⏭ It becomes a storage question the day the totals are kept per SET OF ANALYTICS (a table per
			// breakdown, the reference's shape): then a figure belongs in the sets whose kinds keep it,
			// and the account's own kinds table — a tick per row — is what says which. The maintenance
			// can carry that: its guard is free SQL over the movement row.
			const wxString resField = ibRegValueField(res);

			// Accumulating columns carry an IDENTITY (not scaffold), so the differ can add and drop
			// them as the register gains resources. The two ids derive from the resource's own metaID —
			// the second through a HIGH bit, the only range no metaID occupies (they are small
			// sequential integers, so a low-bit tweak lands on the neighbouring metaobject's id).
			const auto declareColumn = [&](bool credit) {
				const wxString name = resField + (credit ? wxT("_Cr") : wxT("_Dr"));
				const ibMetaID id   = credit ? (res->GetMetaID() | 0x40000000) : res->GetMetaID();
				// The figure is stored in the RESOURCE's own precision (ibRegAccumulatorColumn) — declared
				// flat, the column was NUMERIC(18,0) and a resource carrying kopecks lost them on the way
				// INTO the totals, whatever the movements held.
				const ibBackendQueryColumn* c = ibRegAccumulatorColumn(t, name, id, res);
				// 🛑 THE SECOND FIELD IS THE PHYSICAL BASE the view's columns are spelled from
				// (`+ "TurnoverCr"` …), so it reaches CREATE VIEW as a real SQL identifier and must be
				// ASCII. Taken from the resource's USER name, a Cyrillic resource made Firebird refuse
				// the deferred view and the failed rollback then deadlocked the apply — the same defect
				// as in accumulationRegisterMetadataSchema.cpp, in the register that copies its shape.
				// The user name stays the logical query name; the query layer maps it.
				figures.push_back({ name, resField, credit });
				return c;
			};

			// ⭐⭐ WHAT THE MOVEMENT IS READ FROM IS THE SIDE'S OWN FIELD. A balanced figure is one column
			// and both sides accumulate it; a SPLIT one is two — the quantity that left the credit
			// account and the quantity that reached the debit one — so each side's delta reads its own
			// half. The STORED column keeps the resource's name and id either way: the table is the
			// side's already (that is what a side's totals table is), and the figure in it is the same
			// figure, whichever field of the movement fed it.
			const ibBackendQueryColumn* movField = GetRegisterResource(creditSide, res);
			const wxString movValueField = ibRegValueField(movField);

			if (correspondence) {
				// The row IS a posting: what it contributes to THIS table is the whole amount, and which
				// side that is was decided by which account keyed the table.
				const ibBackendQueryColumn* c = declareColumn(creditSide);
				m.Accumulate(c, wxT("{row}.") + movValueField, ibQueryColumnExpr::Col(movField));
				continue;
			}

			// One-sided: the record type splits one movement into the two sides. The accumulate stays
			// UNCONDITIONAL — only the VALUE branches — so the trigger needs no procedural IF and the
			// delta template stays the one every engine uses.
			//
			// ⚠ THE TAG IS THE ENUM'S ORDINAL, SPELLED THROUGH THE ENUM. `ibAccountingRecordType`
			// declares Debit first, so a debit movement stores 0 — and the neighbouring register was
			// bitten by exactly this, filing every receipt as an expense because a literal disagreed
			// with the enum's order.
			const ibValueMetaObjectAttributeBase* recordType = GetRegisterRecordType();
			if (recordType == nullptr)
				continue;

			const wxString recField = ibRegValueField(recordType);
			const int debitTag = static_cast<int>(ibAccountingRecordType::eDebit);
			const wxString debitTagText = wxString::Format(wxT("%i"), debitTag);
			// The rebuild's condition is a TYPED comparison through the query layer — it needs the enum's
			// VALUE, not its ordinal (a number's tag is on no movement, so every row fell to the credit
			// side on a rebuild). The neighbouring register measured it: accumulationRegisterMetadataSchema.
			const ibValue debit = ibValue::CreateEnumObject<ibValueEnumAccountingRegisterRecordType>(ibAccountingRecordType::eDebit);

			const ibBackendQueryColumn* cDr = declareColumn(/*credit*/ false);
			const ibBackendQueryColumn* cCr = declareColumn(/*credit*/ true);

			// One-sided, the SIDE is the record type rather than the table, so each branch reads the
			// half that branch is about — the debit one from the debit field, the credit one from the
			// credit field. Balanced, both halves are the same column and this reads as it always did.
			const ibBackendQueryColumn* movDr = GetRegisterResource(/*creditSide*/ false, res);
			const ibBackendQueryColumn* movCr = GetRegisterResource(/*creditSide*/ true,  res);
			const wxString movDrField = ibRegValueField(movDr);
			const wxString movCrField = ibRegValueField(movCr);

			m.Accumulate(cDr,
				wxT("CASE WHEN {row}.") + recField + wxT(" = ") + debitTagText + wxT(" THEN {row}.") + movDrField + wxT(" ELSE 0 END"),
				ibQueryColumnExpr::Case(
					{ { ibQueryPredicate::Leaf(ibQueryCondition{ recordType->GetQueryColumn(), ibQueryFilterOp::Equal, debit }),
					    ibQueryColumnExpr::Col(movDr) } },
					ibQueryColumnExpr::Const(ibValue(0.0))));
			m.Accumulate(cCr,
				wxT("CASE WHEN {row}.") + recField + wxT(" = ") + debitTagText + wxT(" THEN 0 ELSE {row}.") + movCrField + wxT(" END"),
				ibQueryColumnExpr::Case(
					{ { ibQueryPredicate::Leaf(ibQueryCondition{ recordType->GetQueryColumn(), ibQueryFilterOp::Equal, debit }),
					    ibQueryColumnExpr::Const(ibValue(0.0)) } },
					ibQueryColumnExpr::Col(movCr)));
		}

		// --- the totals table AS A SOURCE -----------------------------------------------------------
		// Declared BY a metaobject but not one itself, so nothing else vends a queryable for it — and
		// both L3-4 operations (regeneration, the shard fold) gate on exactly that, returning success
		// having touched nothing when it is absent. Built from the table's OWN declaration, after every
		// column exists, so the source cannot drift from the schema it describes. It is handed
		// `keyCols` — the same list the unique index is built from — so the table answers for its
		// own identity and no writer has to reassemble one out of the parts it can see.
		ibRegSelfSourceFromDeclaration(t, GetMetaData(), keyCols);

		// --- the read view --------------------------------------------------------------------------
		// TURNOVERS per period, per key. There is no balance VIEW: a balance is this surface folded up
		// to a moment with the turnovers-only breakdowns dropped, and both of those are read-time
		// questions about data (§ 4.6). Storing a second surface for it would mean storing a key that
		// changes when a user ticks a checkbox.
		{
			ibMaterializeView& v = m.View(GetTurnoverViewName(creditSide), /*withPeriod*/ true);

			// ⭐⭐ THE SECOND ARM: THIS VIEW ALSO CARRIES THE MOVEMENTS. A maintained total is complete
			// only down to the grain it is stored at (a day); everything that happened INSIDE the
			// current day is in the movements and nowhere else. So a balance "at noon" is the stored
			// total at midnight plus this morning's movements, and there is no third place to get the
			// second half from. The recorder and the line number ride along because they are what a
			// boundary INSIDE one instant compares against, when three documents share a date.
			if (HasRecorder() && GetRegisterRecorder() != nullptr && GetRegisterLineNumber() != nullptr) {
				v.m_withMovements = true;
				// NAME AND TYPE TOGETHER — the stored arm has neither of these columns and stands a
				// null in their place, and a null has to be CAST or the view will not compile.
				// DescribeColumnLayout is the same spread ColumnFieldNames walks, carrying the type.
				for (const ibColumnSlot& s : DescribeColumnLayout(GetRegisterRecorder()->GetQueryColumn()))
					v.m_movementColumns.push_back({ s.m_name, s.m_type });
				for (const ibColumnSlot& s : DescribeColumnLayout(GetRegisterLineNumber()->GetQueryColumn()))
					v.m_movementColumns.push_back({ s.m_name, s.m_type });
			}

			for (const Figure& figure : figures)
				v.m_columns.push_back({ ibAcctTurnoverField(figure.m_name, figure.m_credit),
				                        figure.m_field, wxString(), ibMaterializeAgg::Value });
		}
	};

	// THE CREDIT SIDE ONLY WHERE A LINE HAS ONE. It is keyed by the credit account and its breakdown,
	// and those are listed only in correspondence mode (FillArrayObjectByPredefinedAttribute) — a credit
	// table declared over a one-sided register would build its trigger against columns the movements
	// no longer have. No reading of a one-sided register reads it (SidesOf).
	declareSide(/*creditSide*/ false);
	if (correspondence)
		declareSide(/*creditSide*/ true);
}

// ============================================================================
// The views AS SOURCES
// ============================================================================

bool ibValueMetaObjectAccountingRegister::HasMaterializedViews() const
{
	// Presence of the driver's materialization dialect IS the capability: without it no view was ever
	// created, so a reader must fall back to live aggregation over the movements rather than query a
	// relation that does not exist.
	return db_query != nullptr && ibCanMaterialize(*db_query);
}

const ibBackendQueryable* ibValueMetaObjectAccountingRegister::GetTurnoverViewQueryable(bool creditSide) const
{
	const wxString viewName = GetTurnoverViewName(creditSide);

	// Cached beside the shapes, and keyed by WHAT IT WAS BUILT FROM as they are: a view built before
	// the register's attributes were read would otherwise be kept, empty, for the life of the session —
	// the neighbour's own scar, and the cheapest way not to repeat it is the same signature check.
	wxString builtFrom;
	ibRegSignAttribute(builtFrom, GetRegisterPeriod());
	ibRegSignAttribute(builtFrom, creditSide && IsCorrespondence() ? GetRegisterAccountCr() : GetRegisterAccount());
	for (unsigned int idx = 0; idx < GetAccountDimensionCount(); idx++) {
		ibRegSignAttribute(builtFrom, GetAccountDimensionKindSlot(creditSide, idx));
		ibRegSignAttribute(builtFrom, GetAccountDimensionSlot(creditSide, idx));
	}
	// The SIDE's field, since that is what this surface publishes: a dimension going balanced changes
	// the column it carries, and a signature blind to that would keep the old one for the session.
	for (const auto dimension : GetDimensionArrayObject())
		ibRegSignColumn(builtFrom, GetRegisterDimension(creditSide, dimension));
	for (const auto resource  : GetResourceArrayObject())  ibRegSignAttribute(builtFrom, resource);

	return m_surfaces.Obtain(viewName, builtFrom, viewName, GetMetaData(),
		[&](std::vector<ibTempColumn>& columns)
	{

	// ⭐ TWO NAMES, AND THEY ARE NOT THE SAME NAME. The generated table stores `fld1124_D`; a query
	// writes `Period`. Handing the storage name out as the column's name puts the physical schema
	// straight into a field list and makes the table impossible to write against by hand.
	if (GetRegisterPeriod() != nullptr)
		columns.push_back(ibRegAttributeColumn(GetRegisterPeriod()));

	// The account, the breakdown pairs and the dimensions keep their METAIDs, so a composed read
	// reaches them by the attribute exactly as on the movements table — the view is interchangeable
	// with the register as a source, never a parallel vocabulary.
	const ibValueMetaObjectAttributeBase* account = creditSide && IsCorrespondence()
		? GetRegisterAccountCr() : GetRegisterAccount();
	if (account != nullptr)
		columns.push_back(ibRegAttributeColumn(account));

	for (unsigned int idx = 0; idx < GetAccountDimensionCount(); idx++) {
		const ibValueMetaObjectAttributeBase* kindSlot = GetAccountDimensionKindSlot(creditSide, idx);
		const ibValueMetaObjectAttributeBase* slot     = GetAccountDimensionSlot(creditSide, idx);
		if (kindSlot != nullptr)
			columns.push_back(ibRegAttributeColumn(kindSlot));
		if (slot != nullptr)
			columns.push_back(ibRegAttributeColumn(slot));
	}

	// ⭐ PUBLISHED AS THE SIDE KEEPS IT — the credit surface carries the credit half of a non-balance
	// dimension and nothing else, because that is the column its rows are keyed by. Published under the
	// dimension itself it named a column the relation does not have, exactly as the account would if it
	// were published as `Account` on the credit side instead of `AccountCr`.
	for (const auto dimension : GetDimensionArrayObject())
		if (const ibValueMetaObjectAttributeBase* field = GetFieldOnSide(creditSide, dimension))
			columns.push_back(ibRegAttributeColumn(field));

	// The movement arm's own identity — published exactly as an attribute is, because on the source
	// table that is what it is. Null on every stored row, which is also how a reader tells the two arms
	// apart.
	if (HasRecorder() && GetRegisterRecorder() != nullptr && GetRegisterLineNumber() != nullptr) {
		columns.push_back(ibRegAttributeColumn(GetRegisterRecorder()));
		columns.push_back(ibRegAttributeColumn(GetRegisterLineNumber()));
	}

	// The figures. `add` spells BOTH names from one suffix — `AmountTurnoverDr` for a query to write,
	// `Amount_Dr` for the table to keep — so the pair cannot drift.
	const auto addFigure = [&](const ibValueMetaObjectAttributeBase* resource, bool credit) {
		// ⭐⭐ THE NAME THE VIEW EXPOSES, WHICH IS THE ONLY ONE THIS SOURCE CAN ASK FOR.
		//
		// The figure is STORED as `fld1217_N_Dr` and the view publishes it under the alias
		// `AmountTurnoverDr` (see v.m_columns above — alias from the resource's name, reading the
		// stored column). This queryable is built over the VIEW, so the stored spelling is not a
		// name it can use: the relation being read simply has no such column.
		//
		// 🛑 It carried the stored one, and the path had never been walked to find out — a report
		// over this register failed earlier on a doubled suffix and never reached the name. Both
		// halves of that are fixed here: ONE spelling, and the kind that says it is one field
		// (`-206 FLD1217_N_DR`, then `FLD1217_N_DR_N`, measured 2026-08-31).
		//
		// ⚠ AND THE VIEW'S COLUMN IS NO LONGER THE WORD. Since cb1edbae the view keeps the figure under
		// the resource's FIELD (`fld1217_NTurnoverDr`, ibAcctTurnoverField) and the word is only what a
		// query writes — so the two names part here: the query name from the word, the stored one from
		// the function the view's CREATE is spelled with.
		const wxString figureName =
			resource->GetName() + ibRegSidedFigure(ibRegFigure::Turnover, credit);

		columns.push_back(ibTempColumn(
			figureName, ibAcctTurnoverField(ibRegValueField(resource), credit),
			// Numbered over the resource, by side (ibRegDerivedColumnId).
			resource->GetTypeDesc(), ibRegDerivedColumnId(resource->GetMetaID(), credit ? 2 : 1),
			// …and the caption, from the same pair the name is built from.
			ibRegColumnCaptionOf(resource->GetSynonym(), ibRegSidedCaption(ibRegFigure::Turnover, credit)),
			// …and the picture of what it is a figure OF — its resource.
			ibBackendQueryColumn::Kind::Computed, resource->GetColumnIcon()));
	};

	for (const auto resource : GetResourceArrayObject()) {
		if (resource == nullptr)
			continue;
		if (IsCorrespondence()) {
			addFigure(resource, creditSide);   // this side's table holds this side's figure only
			continue;
		}
		addFigure(resource, /*credit*/ false);
		addFigure(resource, /*credit*/ true);
	}

	});
}
