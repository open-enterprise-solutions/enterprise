#include "calculationRegister.h"
#include "chartOfCalculationTypes.h"   // ReadRelation / ReadDisplacementRelation — the relations the chart holds

#include "backend/appData.h"
#include "backend/metaData.h"   // GetAnyArrayObject — every calculation register a change may lead
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"
#include "backend/calculation/calculation.h"   // the calculation logic — this file reads and writes, calculation.h decides
#include "backend/query/dataQueryBuilder.h"                 // L3 door — the reads and the marks
#include "backend/databaseLayer/databaseQueryBuilder.h"     // L2 — what the database copies and names itself (the pieces)
#include "backend/databaseLayer/databaseMaterializeBuilder.h"   // ibFillKeyHashes — a wide mark key's digest, filled by the database
#include "backend/query/dbTableProvider.h"                  // ibDbTableProvider::GetValueAttribute — an L2 row read back as values
#include "backend/query/columnLayout.h"                     // ibOwnerRefField — the Displacing row's owner, as a field
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldOfRole / ibRegSameValueIR / ibRegCompositeIR

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>

////////////////////////////////////////////////////////////////////////////////////////////////////
//                                     THE RECALCULATIONS                                         //
////////////////////////////////////////////////////////////////////////////////////////////////////

// What a record SAYS is decided in calculation.h (ibCalcRecordFacts, ibCalcWhatChanged, ibCalcLedMarks);
// this file reads it off the register's columns, and writes back what the rules answer.

namespace {

using ibCalcValueOf = std::function<ibValue(const ibValueMetaObjectAttributeBase*)>;

// What a line says, as opposed to where it stands: not the recorder (the key the set is written under),
// not the line number (a position).
bool SaysSomething(const ibValueMetaObjectCalculationRegister* meta, const ibValueMetaObjectAttributeBase* attribute)
{
	const ibMetaID id = attribute->GetMetaID();
	return !meta->IsRegisterRecorder(id) && !meta->IsRegisterLineNumber(id);
}

ibCalcRecordFacts ReadFacts(const ibValueMetaObjectCalculationRegister* meta, const ibCalcValueOf& valueOf)
{
	ibCalcRecordFacts facts;
	for (const auto attribute : meta->GetGenericAttributeArrayObject())
		if (SaysSomething(meta, attribute))
			facts.line.push_back(valueOf(attribute));
	facts.recorder = valueOf(meta->GetRegisterRecorder());
	facts.type = valueOf(meta->GetCalculationType());
	for (const auto dimension : meta->GetDimensionArrayObject())
		facts.dims[dimension->GetName().Upper()] = valueOf(dimension);
	if (meta->IsUseActionPeriod()) {   // whole days, the end included — see ibCalcDayAfter
		facts.actionStart = ibCalcDayStart(valueOf(meta->GetActionPeriodStart()));
		facts.actionEnd = ibCalcDayAfter(valueOf(meta->GetActionPeriodEnd()));
		facts.actionPeriod = valueOf(meta->GetActionPeriod());
	}
	facts.storno = valueOf(meta->GetStorno()).GetBoolean();
	if (meta->IsUseBasePeriod()) {
		facts.baseStart = ibCalcDayStart(valueOf(meta->GetBasePeriodStart()));
		facts.baseEnd = ibCalcDayAfter(valueOf(meta->GetBasePeriodEnd()));
	}
	facts.registration = ibCalcDayStart(valueOf(meta->GetRegistrationPeriod()));
	return facts;
}

// The stored records a narrowed read of the register returns. NOT filtered by the reader's rights:
// which records are stale is a fact about the payroll, not about the person posting — a clerk allowed
// to correct a salary must mark the bonus it leads even where the bonus is not theirs to read.
std::vector<ibCalcRecordFacts> ReadStored(const ibValueMetaObjectCalculationRegister* meta,
	const std::function<void(ibDataQueryBuilder&)>& narrow)
{
	std::vector<ibCalcRecordFacts> out;
	ibDataQueryBuilder q;
	q.From(meta->GetQueryable());
	q.WithAccessPolicy(nullptr);
	narrow(q);
	ibReadPageRequest page;
	page.m_count = 0;   // every matching record
	ibDataQueryResult sel = q.Execute(page);
	while (sel.Next())
		out.push_back(ReadFacts(meta, [&sel](const ibValueMetaObjectAttributeBase* attribute) {
			return sel.GetValue(attribute->GetQueryColumn());
		}));
	return out;
}

// THIS recorder's marks in THIS register are resolved: its records here were just computed again.
void ClearOwnMarks(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder)
{
	for (const auto* recalculation : meta->GetRecalculationArrayObject()) {
		const ibBackendQueryColumn* objectColumn = recalculation->GetRecalculationObjectColumn();
		if (objectColumn == nullptr)
			continue;
		ibDataQueryBuilder clear;
		clear.From(recalculation->GetQueryable());
		clear.WithAccessPolicy(nullptr);
		clear.Where(objectColumn, recorder);
		if (!clear.Delete())   // deleting nothing is success; a refused statement is not
			ibBackendCoreException::Error(_("Register '%s': failed to clear the recalculation '%s' of this recorder"),
				meta->GetSynonym(), recalculation->GetSynonym());
	}
}

// The records of `dependent` that the `changed` records lead, marked in `dependent`'s recalculations.
// `dependent` may be the register that changed or any other: what decides is the Leading relation of
// ITS chart, which may name types of another chart (a deduction led by accruals). The recorder that is
// being written is never marked — it is not stale by its own change; its other registers' records are
// written in the same posting.
void MarkLedRecords(const ibValueMetaObjectCalculationRegister* dependent, const ibValue& recorder,
	const std::vector<ibCalcRecordFacts>& changed, const std::vector<ibCalcRecordFacts>& reversals)
{
	const auto recalculations = dependent->GetRecalculationArrayObject();
	if (recalculations.empty() || changed.empty())
		return;
	const ibValueMetaObjectCalculationRegister* meta = dependent;   // the register the reads and marks are about

	// ---- which types a change of these types leads: the chart's Leading relation --------------------
	const ibValueMetaObjectChartOfCalculationTypes* chart = meta->GetChartOfCalculationTypes();
	if (chart == nullptr)
		return;
	std::map<ibValue, int> typeIndex;
	std::vector<std::pair<int, int>> leads;   // {dependent, leading}
	chart->ReadRelation(chart->GetLeadingTable(), typeIndex, leads);
	if (leads.empty())
		return;

	std::set<int> changedTypes;
	for (const ibCalcRecordFacts& facts : changed) {
		const int type = ibCalcTypeOrdinal(typeIndex, facts.type);
		if (type >= 0)
			changedTypes.insert(type);
	}
	std::vector<ibValue> typeOf(typeIndex.size());
	for (const auto& entry : typeIndex)
		typeOf[entry.second] = entry.first;
	const std::set<int> dependentTypes = ibCalcTypesLedBy(changedTypes, leads);
	if (dependentTypes.empty())
		return;

	// ---- the records those types hold under OTHER recorders — the only ones that can be led ----------
	// (One OR of equalities rather than an IN: the calculation type is a reference, spread over several
	// fields, and an equality is the comparison every reader of such a column already knows.)
	ibQueryPredicatePtr ofDependentType;
	for (const int type : dependentTypes) {
		ibQueryCondition leaf;
		leaf.m_col = meta->GetCalculationType()->GetQueryColumn();
		leaf.m_op = ibQueryFilterOp::Equal;
		leaf.m_value = typeOf[type];
		ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
		ofDependentType = ofDependentType ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, ofDependentType, one) : one;
	}
	// ⭐ …AND ONLY THOSE THAT CAN MEET A CHANGED RECORD IN TIME. The rule (ibFindLedRecords) leads a record
	// whose action or base period meets a changed record's action period, or that shares its registration
	// period; this read used to take every record of the dependent types there ever was and let the rule
	// throw nearly all of them away — a corrected December salary read every bonus of every year. Asked
	// now as a superset of the rule in the statement itself: meeting the SPAN of the changed action periods
	// on either period, or registered within the span of their registration periods. The rule still
	// decides; it just no longer reads the history to do it.
	int64_t actionStart = 0, actionEnd = 0;
	int64_t registeredFirst = changed.front().registration, registeredLast = changed.front().registration;   // day starts
	for (const ibCalcRecordFacts& facts : changed) {
		ibCalcWiden(actionStart, actionEnd, facts.actionStart, facts.actionEnd);
		registeredFirst = std::min(registeredFirst, facts.registration);
		registeredLast = std::max(registeredLast, facts.registration);
	}
	const auto compare = [](const ibValueMetaObjectAttributeBase* attribute, ibQueryFilterOp op, const ibValue& value) {
		ibQueryCondition leaf;
		leaf.m_col = attribute->GetQueryColumn();
		leaf.m_op = op;
		leaf.m_value = value;
		return ibQueryPredicate::Leaf(leaf);
	};
	const auto within = [&](const ibValueMetaObjectAttributeBase* from, const ibValueMetaObjectAttributeBase* to,
		int64_t start, int64_t end) {   // [from, to] meets the half-open [start, end)
		return ibQueryPredicate::Compose(ibQueryPredicateKind::And,
			compare(from, ibQueryFilterOp::LessEqual, ibCalcLastDayOf(end)),
			compare(to, ibQueryFilterOp::GreaterEqual, ibValue(wxDateTime(wxLongLong(start)))));
	};
	// …and the base is met the way THIS register's base reads it — its chart's BaseDependence, the same
	// answer GetBase gives (calculation.h, ibFindLedRecords): by registration period a changed record
	// meets a base period it was REGISTERED in, whatever days it acts on.
	const bool baseByRegistration = chart->GetBaseDependence() == ibBaseDependence::eBaseByRegistrationPeriod;
	ibQueryPredicatePtr inTime = ibQueryPredicate::Compose(ibQueryPredicateKind::And,
		compare(meta->GetRegistrationPeriod(), ibQueryFilterOp::GreaterEqual, ibValue(wxDateTime(wxLongLong(registeredFirst)))),
		compare(meta->GetRegistrationPeriod(), ibQueryFilterOp::LessEqual, ibValue(wxDateTime(wxLongLong(registeredLast)))));
	if (baseByRegistration && meta->IsUseBasePeriod())
		inTime = ibQueryPredicate::Compose(ibQueryPredicateKind::Or, inTime,
			within(meta->GetBasePeriodStart(), meta->GetBasePeriodEnd(), registeredFirst,
				ibCalcDayAfter(ibValue(wxDateTime(wxLongLong(registeredLast))))));
	if (actionEnd > actionStart) {
		if (meta->IsUseActionPeriod())
			inTime = ibQueryPredicate::Compose(ibQueryPredicateKind::Or, inTime,
				within(meta->GetActionPeriodStart(), meta->GetActionPeriodEnd(), actionStart, actionEnd));
		if (meta->IsUseBasePeriod() && !baseByRegistration)
			inTime = ibQueryPredicate::Compose(ibQueryPredicateKind::Or, inTime,
				within(meta->GetBasePeriodStart(), meta->GetBasePeriodEnd(), actionStart, actionEnd));
	}

	// ⭐⭐ …AND ONLY THE RECORDS HOLDING THE VALUES THAT CHANGED. The rule leads a candidate only when it holds a
	// changed record's values on the recalculation's dimensions (ibFindLedRecords compares `key`), and this
	// read used to take every record of the dependent types in the span, for everyone: one sick leave of one
	// employee read the salaries of all of them, and the rule threw nearly every one away — a read no index
	// could serve either, since the lookup index leads with the dimensions (ibCalcLookupKey). Narrowed now by
	// the dimensions EVERY recalculation of this register keys on (by name, as the rule matches them), in lists
	// of at most kListPerRead keys, the way the pieces' window is read (KeepActualActionPeriods). A
	// recalculation keyed on nothing leaves nothing to narrow by, and then the span is read whole, as before.
	constexpr size_t kListPerRead = 64;   // a statement listing every key is not one a server takes
	std::vector<wxString> commonNames;   // upper-cased, as the facts keep them
	for (size_t i = 0; i < recalculations.size(); ++i) {
		std::vector<wxString> names;
		for (const ibValueMetaObjectDimension* dimension : recalculations[i]->GetDimensionArrayObject())
			names.push_back(dimension->GetName().Upper());
		if (i == 0)
			commonNames = names;
		else
			commonNames.erase(std::remove_if(commonNames.begin(), commonNames.end(), [&names](const wxString& name) {
				return std::find(names.begin(), names.end(), name) == names.end(); }), commonNames.end());
	}
	std::vector<const ibValueMetaObjectDimension*> narrowBy;   // this register's own columns for those names
	for (const wxString& name : commonNames)
		for (const ibValueMetaObjectDimension* dimension : meta->GetDimensionArrayObject())
			if (dimension->GetName().Upper() == name) {
				narrowBy.push_back(dimension);
				break;
			}

	std::vector<ibCalcRecordFacts> candidates;
	const auto readCandidates = [&](const ibQueryPredicatePtr& ofKeys) {
		for (ibCalcRecordFacts& facts : ReadStored(meta, [&](ibDataQueryBuilder& q) {
				q.Where(ofDependentType);
				q.Where(meta->GetRegisterRecorder()->GetQueryColumn(), ibQueryFilterOp::NotEqual, recorder);
				q.Where(inTime);
				if (ofKeys)
					q.Where(ofKeys);
			}))
			candidates.push_back(std::move(facts));
	};
	if (narrowBy.empty())
		readCandidates(nullptr);
	else {
		std::set<std::vector<ibValue>> keys;
		for (const ibCalcRecordFacts& facts : changed) {
			std::vector<ibValue> key;
			for (const ibValueMetaObjectDimension* dimension : narrowBy) {
				const auto found = facts.dims.find(dimension->GetName().Upper());
				key.push_back(found != facts.dims.end() ? found->second : ibValue());
			}
			keys.insert(std::move(key));
		}
		std::set<std::vector<ibValue>> list;
		const auto readList = [&]() {
			ibQueryPredicatePtr any;   // an OR of AND-ed dimension equalities
			for (const std::vector<ibValue>& key : list) {
				ibQueryPredicatePtr all;
				for (size_t d = 0; d < narrowBy.size(); ++d) {
					ibQueryCondition leaf;
					leaf.m_col = narrowBy[d]->GetQueryColumn();
					leaf.m_value = key[d];
					ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
					all = all ? ibQueryPredicate::Compose(ibQueryPredicateKind::And, all, one) : one;
				}
				any = any ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, any, all) : all;
			}
			readCandidates(any);
			list.clear();
		};
		for (const std::vector<ibValue>& key : keys) {
			list.insert(key);
			if (list.size() == kListPerRead)
				readList();
		}
		if (!list.empty())
			readList();
	}

	// ⭐ ONLY WHAT IS IN FORCE CAN BE STALE. A storno, and a record a later storno reversed, stand for
	// nothing any more: the correction beside the storno is the record a change leads. The stornos this
	// very write holds take part in the pairing (they reverse records of other recorders) without being
	// candidates themselves — a July run correcting June's bonus must not mark the June bonus it reverses.
	{
		const bool ownReversals = !reversals.empty();   // the caller passes them for the register written only
		std::vector<ibCalcStornoFact> positions;
		positions.reserve(candidates.size() + (ownReversals ? reversals.size() : 0));
		for (const ibCalcRecordFacts& facts : candidates)
			positions.push_back({ ibCalcPositionOf(facts), facts.registration, facts.storno });
		if (ownReversals)
			for (const ibCalcRecordFacts& facts : reversals)
				positions.push_back({ ibCalcPositionOf(facts), facts.registration, facts.storno });
		const std::vector<bool> live = ibCalcInForce(positions);
		std::vector<ibCalcRecordFacts> inForce;
		for (size_t i = 0; i < candidates.size(); ++i)
			if (live[i])
				inForce.push_back(std::move(candidates[i]));
		candidates.swap(inForce);
	}
	if (candidates.empty())
		return;

	// ---- per recalculation: ITS dimensions make the key, the rule picks (ibCalcLedMarks), written here -----
	for (const auto* recalculation : recalculations) {
		const ibBackendQueryColumn* objectColumn = recalculation->GetRecalculationObjectColumn();
		const ibBackendQueryColumn* typeColumn = recalculation->GetCalculationTypeColumn();
		if (objectColumn == nullptr || typeColumn == nullptr)
			continue;
		const ibBackendQueryColumn* monthColumn = recalculation->GetActionPeriodColumn();   // null: no months kept
		const std::vector<ibValueMetaObjectDimension*> dimensions = recalculation->GetDimensionArrayObject();
		std::vector<wxString> dimensionNames;   // upper-cased, as the facts keep them
		for (const ibValueMetaObjectDimension* dimension : dimensions)
			dimensionNames.push_back(dimension->GetName().Upper());

		// ⭐⭐ THE MARKS OF A WRITE GO IN ONE STATEMENT, NOT ONE EACH. A mark was an UPSERT per led
		// record — a statement built, rendered, prepared and run for every one, and a correction of a
		// month for forty thousand employees leads forty thousand of them. A mark is a KEY and carries
		// nothing else, so "upsert" is "insert unless it is there": the keys are gathered (the rule can
		// lead one record from several changes, and two records to one key), the ones already standing
		// are read in one statement per list of recorders, and the rest are written as the rows of ONE
		// insert — the door batches the rows of a statement, as the pieces' write does.
		std::set<std::vector<ibValue>> marks;   // object, type, dimension values, [month]
		for (const ibCalcLedMark& led : ibCalcLedMarks(changed, candidates, dimensionNames, typeIndex, leads, baseByRegistration)) {
			const ibCalcRecordFacts& stale = candidates[led.candidate];
			std::vector<ibValue> key{ stale.recorder, stale.type };
			for (size_t d = 0; d < dimensions.size(); ++d)
				key.push_back(d < led.values.size() ? led.values[d] : ibValue());
			if (monthColumn != nullptr)
				key.push_back(stale.actionPeriod);   // the position it is — its own month or a correction's
			marks.insert(std::move(key));
		}
		if (marks.empty())
			continue;

		const auto keyOfRow = [&](ibDataQueryResult& row) {
			std::vector<ibValue> key{ row.GetValue(objectColumn), row.GetValue(typeColumn) };
			for (const ibValueMetaObjectDimension* dimension : dimensions)
				key.push_back(row.GetValue(dimension->GetQueryColumn()));
			if (monthColumn != nullptr)
				key.push_back(row.GetValue(monthColumn));
			return key;
		};
		std::set<ibValue> recorders;
		for (const std::vector<ibValue>& key : marks)
			recorders.insert(key.front());
		std::vector<ibValue> list;
		const auto readStanding = [&]() {
			// One OR of equalities rather than an IN, as for the types above: the recorder is a reference.
			ibQueryPredicatePtr ofRecorders;
			for (const ibValue& recorder : list) {
				ibQueryCondition leaf;
				leaf.m_col = objectColumn;
				leaf.m_value = recorder;
				ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
				ofRecorders = ofRecorders ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, ofRecorders, one) : one;
			}
			ibDataQueryBuilder standing;
			standing.From(recalculation->GetQueryable());
			standing.WithAccessPolicy(nullptr);
			standing.Where(ofRecorders);
			ibReadPageRequest page;
			page.m_count = 0;
			ibDataQueryResult rows = standing.Execute(page);
			while (rows.Next())
				marks.erase(keyOfRow(rows));
			list.clear();
		};
		for (const ibValue& recorder : recorders) {
			list.push_back(recorder);
			if (list.size() == kListPerRead)
				readStanding();
		}
		if (!list.empty())
			readStanding();
		if (marks.empty())
			continue;

		ibDataQueryBuilder write;
		write.From(recalculation->GetQueryable());
		write.WithAccessPolicy(nullptr);
		bool any = false;
		for (const std::vector<ibValue>& key : marks) {
			if (any)
				write.NextRow();
			any = true;
			write.SetValue(objectColumn, key[0]);
			write.SetValue(typeColumn, key[1]);
			for (size_t d = 0; d < dimensions.size(); ++d)
				write.SetValue(dimensions[d]->GetQueryColumn(), key[2 + d]);
			if (monthColumn != nullptr)
				write.SetValue(monthColumn, key[2 + dimensions.size()]);
		}
		if (!write.Insert())
			ibBackendCoreException::Error(_("Register '%s': failed to mark records for the recalculation '%s'"),
				meta->GetSynonym(), recalculation->GetSynonym());

		// …and where the key is too wide for an index, its identity lives in a digest column the door
		// does not know how to fill (GetKeyHashColumn). The database digests the rows that have none, by
		// the expression a rebuilt totals table is finished with — the same function of the same fields.
		const wxString digest = recalculation->GetKeyHashColumn();
		if (!digest.IsEmpty()) {
			ibMaterializeSpec spec;
			spec.m_table = recalculation->GetPhysicalTableName();
			spec.m_keyHashColumn = digest;
			for (const ibBackendQueryColumn* column : recalculation->GetQueryable()->GetPrimaryKeyColumns())
				for (const wxString& field : ColumnFieldNames(column))
					spec.m_keyColumns.push_back(field);
			if (db_query == nullptr || !ibFillKeyHashes(*db_query, spec))
				ibBackendCoreException::Error(_("Register '%s': failed to identify the marks of the recalculation '%s'"),
					meta->GetSynonym(), recalculation->GetSynonym());
		}
	}
}

// Does any calculation register of this configuration keep recalculations at all — the one question
// that decides whether a write has to look at what it changes.
bool AnyRecalculations(const ibMetaData* metaData)
{
	if (metaData == nullptr)
		return false;
	for (const ibValueMetaObjectCalculationRegister* reg :
		metaData->GetAnyArrayObject<ibValueMetaObjectCalculationRegister>(g_metaCalculationRegisterCLSID))
		if (!reg->GetRecalculationArrayObject().empty())
			return true;
	return false;
}

// ⭐ THE SECOND WAY A MARK IS ANSWERED — by a correction in a later period, not by posting the past again.
// A storno this recorder holds reverses the records of its position registered before it; the marks their
// recorders carry for that position are answered, because the run that wrote the storno computed the
// position anew beside it. (The first way stays: posting the marked recorder itself resolves its own.)
void ResolveReversedMarks(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder,
	const std::vector<ibCalcRecordFacts>& reversals)
{
	const auto recalculations = meta->GetRecalculationArrayObject();
	if (recalculations.empty() || reversals.empty())
		return;
	for (const ibCalcRecordFacts& storno : reversals) {
		const std::vector<ibCalcRecordFacts> reversed = ReadStored(meta, [&](ibDataQueryBuilder& q) {
			q.Where(meta->GetCalculationType()->GetQueryColumn(), storno.type);
			for (const ibValueMetaObjectDimension* dimension : meta->GetDimensionArrayObject()) {
				const auto found = storno.dims.find(dimension->GetName().Upper());
				q.Where(dimension->GetQueryColumn(), found != storno.dims.end() ? found->second : ibValue());
			}
			if (meta->IsUseActionPeriod()) {
				q.Where(meta->GetActionPeriodStart()->GetQueryColumn(), ibValue(wxDateTime(wxLongLong(storno.actionStart))));
				q.Where(meta->GetActionPeriodEnd()->GetQueryColumn(), ibCalcLastDayOf(storno.actionEnd));
				q.Where(meta->GetActionPeriod()->GetQueryColumn(), storno.actionPeriod);
			}
			q.Where(meta->GetStorno()->GetQueryColumn(), ibValue(false));
			q.WhereCompare(meta->GetRegistrationPeriod()->GetQueryColumn(), ibQueryFilterOp::Less,
				ibValue(wxDateTime(wxLongLong(storno.registration))));
			q.Where(meta->GetRegisterRecorder()->GetQueryColumn(), ibQueryFilterOp::NotEqual, recorder);
		});
		std::set<ibValue> recorders;
		for (const ibCalcRecordFacts& facts : reversed)
			recorders.insert(facts.recorder);
		for (const ibValue& stale : recorders)
			for (const auto* recalculation : recalculations) {
				const ibBackendQueryColumn* objectColumn = recalculation->GetRecalculationObjectColumn();
				const ibBackendQueryColumn* typeColumn = recalculation->GetCalculationTypeColumn();
				if (objectColumn == nullptr || typeColumn == nullptr)
					continue;
				ibDataQueryBuilder answer;
				answer.From(recalculation->GetQueryable());
				answer.WithAccessPolicy(nullptr);
				answer.Where(objectColumn, stale);
				answer.Where(typeColumn, storno.type);
				for (const ibValueMetaObjectDimension* dimension : recalculation->GetDimensionArrayObject()) {
					const auto found = storno.dims.find(dimension->GetName().Upper());
					if (found != storno.dims.end())
						answer.Where(dimension->GetQueryColumn(), found->second);
				}
				// …and the storno's MONTH, where marks keep one: a correction answers the position it
				// corrects, and the same recorder's record of that type for another month stays marked. (A mark
				// written before marks kept a month has none, and is answered the first way — by posting its
				// recorder, which clears every mark it carries.)
				if (const ibBackendQueryColumn* monthColumn = recalculation->GetActionPeriodColumn())
					answer.Where(monthColumn, storno.actionPeriod);
				if (!answer.Delete())
					ibBackendCoreException::Error(_("Register '%s': failed to answer the recalculation '%s' of a corrected record"),
						meta->GetSynonym(), recalculation->GetSynonym());
			}
	}
}

// See the note on SaveData in the header; `changed` is what differs between before and after, `reversals`
// the stornos the recorder holds now. The marks go wherever the change leads: this register's
// recalculations and every other register's whose chart names the changed types as leading.
void KeepRecalculations(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder,
	const std::vector<ibCalcRecordFacts>& changed, const std::vector<ibCalcRecordFacts>& reversals)
{
	if (recorder.IsEmpty())
		return;
	ClearOwnMarks(meta, recorder);
	ResolveReversedMarks(meta, recorder, reversals);
	if (changed.empty())
		return;
	static const std::vector<ibCalcRecordFacts> kNone;
	for (const ibValueMetaObjectCalculationRegister* dependent :
		meta->GetMetaData()->GetAnyArrayObject<ibValueMetaObjectCalculationRegister>(g_metaCalculationRegisterCLSID))
		MarkLedRecords(dependent, recorder, changed, dependent == meta ? reversals : kNone);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//                                  THE ACTUAL ACTION PERIODS                                     //
////////////////////////////////////////////////////////////////////////////////////////////////////

// A record's values on the register's dimensions, in the register's order — the key displacement runs
// within: a sick leave of one employee cuts that employee's salary and nobody else's.
std::vector<ibValue> DimensionKey(const ibValueMetaObjectCalculationRegister* meta, const ibCalcRecordFacts& facts)
{
	std::vector<ibValue> key;
	for (const auto dimension : meta->GetDimensionArrayObject()) {
		const auto found = facts.dims.find(dimension->GetName().Upper());
		key.push_back(found != facts.dims.end() ? found->second : ibValue());
	}
	return key;
}

// "Any of these keys" — an OR of AND-ed dimension equalities; null when the register has no dimensions
// (every record then shares the one empty key).
ibQueryPredicatePtr AnyOfKeys(const ibValueMetaObjectCalculationRegister* meta, const std::set<std::vector<ibValue>>& keys)
{
	const std::vector<ibValueMetaObjectDimension*> dimensions = meta->GetDimensionArrayObject();
	if (dimensions.empty())
		return nullptr;
	ibQueryPredicatePtr any;
	for (const std::vector<ibValue>& key : keys) {
		ibQueryPredicatePtr all;
		for (size_t d = 0; d < dimensions.size() && d < key.size(); ++d) {
			ibQueryCondition leaf;
			leaf.m_col = dimensions[d]->GetQueryColumn();
			leaf.m_op = ibQueryFilterOp::Equal;
			leaf.m_value = key[d];
			ibQueryPredicatePtr one = ibQueryPredicate::Leaf(leaf);
			all = all ? ibQueryPredicate::Compose(ibQueryPredicateKind::And, all, one) : one;
		}
		if (all)
			any = any ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, any, all) : all;
	}
	return any;
}

// "Any of these lines of this recorder" — an OR of (recorder AND line), the recorder REPEATED in every
// branch. Said once beside a list of lines it reached the pieces' key (recorder, line, start) for the
// recorder alone, and the list was weighed against every piece the recorder holds — see RewriteWindow.
ibQueryPredicatePtr AnyOfLines(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder,
	const std::vector<ibValue>& lines)
{
	const auto equal = [](const ibBackendQueryColumn* column, const ibValue& value) {
		ibQueryCondition leaf;
		leaf.m_col = column;
		leaf.m_op = ibQueryFilterOp::Equal;
		leaf.m_value = value;
		return ibQueryPredicate::Leaf(leaf);
	};
	const ibBackendQueryColumn* recorderColumn = meta->GetRegisterRecorder()->GetQueryColumn();
	const ibBackendQueryColumn* lineColumn = meta->GetRegisterLineNumber()->GetQueryColumn();
	ibQueryPredicatePtr any;
	for (const ibValue& line : lines) {
		const ibQueryPredicatePtr one = ibQueryPredicate::Compose(ibQueryPredicateKind::And,
			equal(recorderColumn, recorder), equal(lineColumn, line));
		any = any ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, any, one) : one;
	}
	return any;
}

// ⭐⭐ THE PIECES ARE KEPT BY THE WRITE, like the recalculations — see ibCalcActualPeriodSourceDescriptor.
//
// Displacement runs ACROSS RECORDERS, within one set of dimension values: the salary is posted by the
// payroll, the sick leave by the sick-leave document, and the sick leave still cuts the salary. So a
// write does not only give ITS records their pieces — every record of the same employees may have
// changed shape, and all of theirs are recomputed with them. (The first version displaced inside one
// set and ignored the dimensions: one employee's absence cut every salary in the document, and no
// document could cut another's.)
//
// ⭐⭐ AND ONLY WITHIN THE WINDOW THE WRITE CAN REACH — not the whole history of the touched keys.
//
// It used to recompute every record of every touched employee, all the way back: MEASURED 2026-09-10, a
// payroll of 200 employees posted month after month rewrote 200, 400, … 1200 pieces, one write slower
// than the last, 52 s for six months in a debug build. What the window is, and why S1, W' and S are all
// a write needs, is in calculation.h (ibCalcReach, ibCalcPiecesInWindow); here S1's pieces are erased
// and written again, and what lies beyond W' is never read.
//
// ⭐ THE KEYS RIDE IN LISTS OF AT MOST kKeyListLimit. A sick leave touches one employee and the list
// narrows the read to that employee's month; a statement listing forty thousand keys is not a statement a
// server takes. It used to fall back to the window alone past the limit — every record of the month read
// and recomputed — which was the right call only while every record of the month was being recomputed
// anyway. Now only what a displacer touches is (step 3 below): two hundred sick leaves among forty thousand
// salaries are four lists of keys, each read through the register's lookup index, not the whole month.
constexpr size_t kKeyListLimit = 64;

// ⭐⭐ WHAT NOTHING DISPLACES, THE DATABASE COPIES — and that is nearly all a payroll writes.
//
// A record's pieces ARE the record unless a record of a type that displaces its own overlaps it. A payroll
// of forty thousand salaries in a month when two hundred people were ill holds two hundred records with
// anything to compute and 39 800 whose one piece is the record itself — and reading every one of them into
// memory to learn that was the costliest third of the write (MEASURED 2026-09-10, debug: 0.67 s of 2.0 s
// for a set of 200, read and computed only to be written back unchanged). So the database does what needs
// no computing, and names what does:
//   1. every record of this recorder goes into the pieces WHOLE — one INSERT … SELECT (CopyWhole);
//   2. the records of this recorder that a displacer overlaps are listed — a join through the chart's
//      Displacing section, the relation read as a table as the base reads its Base section
//      (DisplacedJoin);
//   3. the window pass runs over the keys of those records and of the displacers this write added or
//      removed — a displacer reshapes the records of OTHER recorders — and writes their pieces over the
//      whole copies.
// A write that nothing displaces and that changes no displacer — the ordinary payroll — is 1 and 2 alone.

// m — a record; x — a row of the Displacing section OWNED by m's type, i.e. naming a type that displaces
// m's; d — a record of the named type holding m's values in every dimension and meeting m's action period
// by day, both ends included. Null when the chart keeps no Displacing section: nothing displaces anything.
// Matches every record the rule cuts (ibComputeActionPeriodDisplacementByRelation) — a row naming its own
// type is left out as the rule leaves such an edge out — and may match one it then leaves whole (a
// displacer with no days of its own), which costs a recomputation and nothing else.
ibQueryRelPtr DisplacedJoin(const ibValueMetaObjectCalculationRegister* meta)
{
	const ibValueMetaObjectChartOfCalculationTypes* chart = meta->GetChartOfCalculationTypes();
	const ibValueMetaObjectCalculationTypeRelationTable* displacing = chart != nullptr ? chart->GetDisplacingTable() : nullptr;
	if (displacing == nullptr || !displacing->IsAllowed() || displacing->GetCalculationType() == nullptr)
		return nullptr;

	const ibBackendQueryColumn* named = displacing->GetCalculationType()->GetQueryColumn();
	const ibBackendQueryColumn* type = meta->GetCalculationType()->GetQueryColumn();
	const wxString typeId = ibRegFieldOfRole(type, ibColumnRole::ReferenceId);
	const wxString namedId = ibRegFieldOfRole(named, ibColumnRole::ReferenceId);
	if (typeId.IsEmpty() || namedId.IsEmpty())
		return nullptr;
	const ibQueryExprPtr owner = ibCol(wxT("x"), ibOwnerRefField());
	const ibQueryExprPtr onSection = ibBinOp(ibQueryBinOp::And, ibBinOp(ibQueryBinOp::Eq, owner, ibCol(wxT("m"), typeId)),
		ibNot(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("x"), namedId), owner)));

	ibQueryExprPtr onDisplacer;
	const auto also = [&onDisplacer](const ibQueryExprPtr& term) {
		if (term)
			onDisplacer = onDisplacer ? ibBinOp(ibQueryBinOp::And, onDisplacer, term) : term;
	};
	for (const ibColumnRole role : { ibColumnRole::ReferenceType, ibColumnRole::ReferenceId }) {
		const wxString held = ibRegFieldOfRole(type, role), names = ibRegFieldOfRole(named, role);
		if (!held.IsEmpty() && !names.IsEmpty())
			also(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("d"), held), ibCol(wxT("x"), names)));
	}
	for (const ibValueMetaObjectDimension* dimension : meta->GetDimensionArrayObject())
		also(ibRegSameValueIR(dimension->GetQueryColumn(), wxT("d"), dimension->GetQueryColumn(), wxT("m")));
	const wxString start = ibRegFieldOfRole(meta->GetActionPeriodStart()->GetQueryColumn(), ibColumnRole::Date);
	const wxString end = ibRegFieldOfRole(meta->GetActionPeriodEnd()->GetQueryColumn(), ibColumnRole::Date);
	const auto day = [](const wxString& q, const wxString& field) { return ibPeriodTrunc(ibCol(q, field), ibTotalsPeriod::Day); };
	also(ibBinOp(ibQueryBinOp::Le, day(wxT("d"), start), day(wxT("m"), end)));
	also(ibBinOp(ibQueryBinOp::Ge, day(wxT("d"), end), day(wxT("m"), start)));

	const wxString table = meta->GetPhysicalTableName();
	return ibJoin(ibJoin(ibScan(table, wxT("m")), ibScan(displacing->GetPhysicalTableName(), wxT("x")), onSection, ibQueryJoinType::Inner),
		ibScan(table, wxT("d")), onDisplacer, ibQueryJoinType::Inner);
}

// Step 1: this recorder's records, whole, as their own pieces — every one that is in force at all (its
// last day not before its first, the same test the rule applies to a period of no days). A STORNO is
// never in force (see calculation.h), so it is not copied; what one reverses is copied here and taken out
// by the window pass, which is where the pairing is decided (ListOwnReversed names those keys).
void CopyWhole(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder)
{
	std::vector<wxString> fields;
	std::vector<ibQueryProjItem> projection;
	for (const ibValueMetaObjectAttributeBase* attribute : meta->GetGenericAttributeArrayObject())
		for (const wxString& field : ibRegFieldsOf(attribute)) {
			fields.push_back(field);
			projection.push_back(ibQueryProjItem{ ibCol(wxT("m"), field), field });
		}
	const wxString start = ibRegFieldOfRole(meta->GetActionPeriodStart()->GetQueryColumn(), ibColumnRole::Date);
	const wxString end = ibRegFieldOfRole(meta->GetActionPeriodEnd()->GetQueryColumn(), ibColumnRole::Date);
	const wxString storno = ibRegFieldOfRole(meta->GetStorno()->GetQueryColumn(), ibColumnRole::Boolean);
	ibQueryExprPtr where = ibBinOp(ibQueryBinOp::And,
		ibRegCompositeIR(meta->GetRegisterRecorder()->GetQueryColumn(), meta->GetMetaData(), recorder, ibQueryBinOp::Eq, wxT("m")),
		ibBinOp(ibQueryBinOp::Le, ibPeriodTrunc(ibCol(wxT("m"), start), ibTotalsPeriod::Day), ibPeriodTrunc(ibCol(wxT("m"), end), ibTotalsPeriod::Day)));
	if (!storno.IsEmpty())
		where = ibBinOp(ibQueryBinOp::And, where, ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("m"), storno), ibConst(ibValue(false))));
	ibDatabaseQueryBuilder copy;
	if (copy.Execute(ibInsertSelect(meta->GetActualActionPeriodTableName(), fields,
			ibProject(ibFilter(ibScan(meta->GetPhysicalTableName(), wxT("m")), where), projection))) < 0)
		ibBackendCoreException::Error(_("Register '%s': failed to store its actual action periods"), meta->GetSynonym());
}

// Step 2: the keys of this recorder's records a displacer overlaps, and the span those records cover.
void ListOwnDisplaced(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder,
	std::set<std::vector<ibValue>>& keys, int64_t& windowStart, int64_t& windowEnd)
{
	const ibQueryRelPtr displaced = DisplacedJoin(meta);
	if (!displaced)
		return;
	std::vector<ibQueryProjItem> projection;
	std::vector<const ibValueMetaObjectAttributeBase*> read;
	for (const ibValueMetaObjectDimension* dimension : meta->GetDimensionArrayObject())
		read.push_back(dimension);
	read.push_back(meta->GetActionPeriodStart());
	read.push_back(meta->GetActionPeriodEnd());
	for (const ibValueMetaObjectAttributeBase* attribute : read)
		for (const wxString& field : ibRegFieldsOf(attribute))
			projection.push_back(ibQueryProjItem{ ibCol(wxT("m"), field), field });

	ibDatabaseQueryBuilder q;
	q.From(displaced);
	q.Where(ibRegCompositeIR(meta->GetRegisterRecorder()->GetQueryColumn(), meta->GetMetaData(), recorder, ibQueryBinOp::Eq, wxT("m")));
	q.Project(projection);
	ibQueryResult rs = q.Execute();
	while (rs.Next()) {
		std::vector<ibValue> key;
		for (size_t d = 0; d + 2 < read.size(); ++d) {
			ibValue value;
			ibDbTableProvider::GetValueAttribute(read[d], value, rs);
			key.push_back(value);
		}
		ibValue start, end;
		ibDbTableProvider::GetValueAttribute(meta->GetActionPeriodStart(), start, rs);
		ibDbTableProvider::GetValueAttribute(meta->GetActionPeriodEnd(), end, rs);
		keys.insert(std::move(key));
		ibCalcWiden(windowStart, windowEnd, ibCalcDayStart(start), ibCalcDayAfter(end));
	}
}

// The same POSITION as the storno rule reads it (calculation.h): the calculation type, every dimension,
// the action period and the month it is for — `m` against `s`.
ibQueryExprPtr SamePositionIR(const ibValueMetaObjectCalculationRegister* meta, const wxString& m, const wxString& s)
{
	ibQueryExprPtr same;
	const auto also = [&same](const ibQueryExprPtr& term) {
		if (term)
			same = same ? ibBinOp(ibQueryBinOp::And, same, term) : term;
	};
	also(ibRegSameValueIR(meta->GetCalculationType()->GetQueryColumn(), s, meta->GetCalculationType()->GetQueryColumn(), m));
	for (const ibValueMetaObjectDimension* dimension : meta->GetDimensionArrayObject())
		also(ibRegSameValueIR(dimension->GetQueryColumn(), s, dimension->GetQueryColumn(), m));
	for (const ibValueMetaObjectAttributeBase* period : { meta->GetActionPeriodStart(), meta->GetActionPeriodEnd(), meta->GetActionPeriod() })
		also(ibRegSameValueIR(period->GetQueryColumn(), s, period->GetQueryColumn(), m));
	return same;
}

// Step 2b: the keys of this recorder's records that a STORNO registered LATER reverses. Step 1 copied them
// whole; the window pass takes them out. (Posting again a month a later run has already corrected: its
// records stand reversed by that run's stornos, and the whole copy would have put them back in force.)
void ListOwnReversed(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder,
	std::set<std::vector<ibValue>>& keys, int64_t& windowStart, int64_t& windowEnd)
{
	const wxString storno = ibRegFieldOfRole(meta->GetStorno()->GetQueryColumn(), ibColumnRole::Boolean);
	const wxString registered = ibRegFieldOfRole(meta->GetRegistrationPeriod()->GetQueryColumn(), ibColumnRole::Date);
	const ibQueryExprPtr same = SamePositionIR(meta, wxT("m"), wxT("s"));
	if (storno.IsEmpty() || registered.IsEmpty() || !same)
		return;
	const ibQueryExprPtr on = ibBinOp(ibQueryBinOp::And, same,
		ibBinOp(ibQueryBinOp::And, ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("s"), storno), ibConst(ibValue(true))),
			ibBinOp(ibQueryBinOp::Gt, ibCol(wxT("s"), registered), ibCol(wxT("m"), registered))));

	std::vector<ibQueryProjItem> projection;
	std::vector<const ibValueMetaObjectAttributeBase*> read;
	for (const ibValueMetaObjectDimension* dimension : meta->GetDimensionArrayObject())
		read.push_back(dimension);
	read.push_back(meta->GetActionPeriodStart());
	read.push_back(meta->GetActionPeriodEnd());
	for (const ibValueMetaObjectAttributeBase* attribute : read)
		for (const wxString& field : ibRegFieldsOf(attribute))
			projection.push_back(ibQueryProjItem{ ibCol(wxT("m"), field), field });

	const wxString table = meta->GetPhysicalTableName();
	ibDatabaseQueryBuilder q;
	q.From(ibJoin(ibScan(table, wxT("m")), ibScan(table, wxT("s")), on, ibQueryJoinType::Inner));
	q.Where(ibRegCompositeIR(meta->GetRegisterRecorder()->GetQueryColumn(), meta->GetMetaData(), recorder, ibQueryBinOp::Eq, wxT("m")));
	q.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("m"), storno), ibConst(ibValue(false))));
	q.Project(projection);
	ibQueryResult rs = q.Execute();
	while (rs.Next()) {
		std::vector<ibValue> key;
		for (size_t d = 0; d + 2 < read.size(); ++d) {
			ibValue value;
			ibDbTableProvider::GetValueAttribute(read[d], value, rs);
			key.push_back(value);
		}
		ibValue start, end;
		ibDbTableProvider::GetValueAttribute(meta->GetActionPeriodStart(), start, rs);
		ibDbTableProvider::GetValueAttribute(meta->GetActionPeriodEnd(), end, rs);
		keys.insert(std::move(key));
		ibCalcWiden(windowStart, windowEnd, ibCalcDayStart(start), ibCalcDayAfter(end));
	}
}

// The pieces a narrowed delete of the pieces table names — a recorder's, or some lines of one.
void ErasePieces(const ibValueMetaObjectCalculationRegister* meta, const std::function<void(ibDataQueryBuilder&)>& narrow)
{
	ibDataQueryBuilder d;
	d.From(meta->GetActualActionPeriodQueryable());
	d.WithAccessPolicy(nullptr);
	narrow(d);
	if (!d.Delete())
		ibBackendCoreException::Error(_("Register '%s': failed to clear its actual action periods"), meta->GetSynonym());
}

// Step 3, the window pass, over the keys `ofKeys` names (null: a register with no dimensions, where every
// record shares the one empty key) and the window [windowStart, windowEnd) the step before found.
void RewriteWindow(const ibValueMetaObjectCalculationRegister* meta, const ibQueryPredicatePtr& ofKeys,
	int64_t windowStart, int64_t windowEnd, const std::map<ibValue, int>& typeIndex,
	const std::vector<std::pair<int, int>>& edges)
{
	// The records of the touched keys meeting [from, to) — whole: a piece row is its record's row with
	// its own bounds. Compared on the stored dates, both ends included. What calculation.h weighs
	// (`window`) and what is written back (`rows`) run side by side, one entry per record.
	const std::vector<ibValueMetaObjectAttributeBase*> columns = meta->GetGenericAttributeArrayObject();
	std::vector<ibCalcWindowRecord> window;
	std::vector<std::vector<ibValue>> rows;
	const auto readMeeting = [&](int64_t from, int64_t to) {
		window.clear();
		rows.clear();
		ibDataQueryBuilder q;
		q.From(meta->GetQueryable());
		q.WithAccessPolicy(nullptr);
		if (ofKeys)
			q.Where(ofKeys);
		q.WhereCompare(meta->GetActionPeriodStart()->GetQueryColumn(), ibQueryFilterOp::LessEqual, ibCalcLastDayOf(to));
		q.WhereCompare(meta->GetActionPeriodEnd()->GetQueryColumn(), ibQueryFilterOp::GreaterEqual, ibValue(wxDateTime(wxLongLong(from))));
		ibReadPageRequest page;
		page.m_count = 0;
		ibDataQueryResult sel = q.Execute(page);
		while (sel.Next()) {
			ibCalcWindowRecord r;
			for (const auto dimension : meta->GetDimensionArrayObject())
				r.key.push_back(sel.GetValue(dimension->GetQueryColumn()));
			r.kind = sel.GetValue(meta->GetCalculationType()->GetQueryColumn());
			r.type = ibCalcTypeOrdinal(typeIndex, r.kind);
			r.start = ibCalcDayStart(sel.GetValue(meta->GetActionPeriodStart()->GetQueryColumn()));
			r.end = ibCalcDayAfter(sel.GetValue(meta->GetActionPeriodEnd()->GetQueryColumn()));
			r.actionPeriod = sel.GetValue(meta->GetActionPeriod()->GetQueryColumn());
			r.registration = ibCalcDayStart(sel.GetValue(meta->GetRegistrationPeriod()->GetQueryColumn()));
			r.storno = sel.GetValue(meta->GetStorno()->GetQueryColumn()).GetBoolean();
			std::vector<ibValue> row;
			for (const ibValueMetaObjectAttributeBase* column : columns)
				row.push_back(sel.GetValue(column->GetQueryColumn()));
			window.push_back(std::move(r));
			rows.push_back(std::move(row));
		}
	};

	// S1 and W', then S — which IS S1 when nothing in S1 reaches past the window, the usual case for a
	// write of whole months: then the first read already holds everything, and a second would read it
	// again (0.4 s of a 3 s write of 200 records, measured in a debug build).
	readMeeting(windowStart, windowEnd);
	int64_t reachStart = windowStart, reachEnd = windowEnd;
	ibCalcReach(window, windowStart, windowEnd, reachStart, reachEnd);
	if (reachStart < windowStart || reachEnd > windowEnd)
		readMeeting(reachStart, reachEnd);

	// S1's pieces go — per recorder, by line, this recorder's whole copies among them. A piece is its
	// record's by (recorder, line); its own bounds say nothing about its record's, so the window cannot
	// pick them.
	const size_t recorderAt = [&]() {
		for (size_t c = 0; c < columns.size(); ++c)
			if (columns[c] == meta->GetRegisterRecorder()) return c;
		return columns.size();
	}();
	const size_t lineAt = [&]() {
		for (size_t c = 0; c < columns.size(); ++c)
			if (columns[c] == meta->GetRegisterLineNumber()) return c;
		return columns.size();
	}();
	if (recorderAt == columns.size() || lineAt == columns.size())
		return;
	//
	// ⭐⭐ EACH LINE LOOKED UP, NOT EACH PIECE WEIGHED. Written as one recorder equality beside a list of lines,
	// the delete reached the pieces' key for the recorder alone: the server walked every piece of that
	// recorder and tested the whole list against each. A run for forty thousand people holds 80 000 pieces,
	// and the window pass visits it once per list of keys — MEASURED 2026-09-11 (Release, Firebird trace):
	// 380 such deletes at 1.5 s, 563 s of a 950 s posting, quadratic in the size of the run (N/64 statements
	// of N pieces each). With the recorder inside every branch (AnyOfLines) each branch is a prefix of the
	// key, and the server looks each line up. The lists stay short, as the key lists do (kKeyListLimit).
	std::map<ibValue, std::vector<ibValue>> linesOf;   // recorder -> its lines in S1
	for (size_t i = 0; i < window.size(); ++i)
		if (ibCalcOverlaps(window[i].start, window[i].end, windowStart, windowEnd))
			linesOf[rows[i][recorderAt]].push_back(rows[i][lineAt]);
	constexpr size_t kLinesPerStatement = 64;
	for (const auto& entry : linesOf) {
		for (size_t from = 0; from < entry.second.size(); from += kLinesPerStatement) {
			const std::vector<ibValue> chunk(entry.second.begin() + from,
				entry.second.begin() + std::min(entry.second.size(), from + kLinesPerStatement));
			ErasePieces(meta, [&](ibDataQueryBuilder& d) { d.Where(AnyOfLines(meta, entry.first, chunk)); });
		}
	}

	// ⭐ ONE INSERT FOR THE WHOLE WRITE, not one per employee. It was one per dimension key: a payroll of two
	// hundred employees sent two hundred statements, and a write of seven such sets took 53 s in a debug
	// build (MEASURED 2026-09-10, 1400 records) — forty thousand employees would have been forty thousand
	// statements a month. The door already batches the rows of one statement.
	const std::vector<std::vector<ibActionInterval>> actual =
		ibCalcPiecesInWindow(typeIndex.size(), edges, window, windowStart, windowEnd);
	const ibMetaID startId = meta->GetActionPeriodStart()->GetMetaID();
	const ibMetaID endId = meta->GetActionPeriodEnd()->GetMetaID();
	ibDataQueryBuilder write;
	write.From(meta->GetActualActionPeriodQueryable());
	write.WithAccessPolicy(nullptr);
	bool any = false;
	for (size_t i = 0; i < actual.size(); ++i) {
		for (const ibActionInterval& piece : actual[i]) {   // empty for the context records — see calculation.h
			if (any)
				write.NextRow();
			for (size_t c = 0; c < columns.size(); ++c) {
				const ibMetaID id = columns[c]->GetMetaID();
				const ibValue value = id == startId ? ibValue(wxDateTime(wxLongLong(piece.start)))
					: id == endId ? ibCalcLastDayOf(piece.end)
					: rows[i][c];
				write.SetValue(columns[c]->GetQueryColumn(), value);
			}
			any = true;
		}
	}
	if (any && !write.Insert())
		ibBackendCoreException::Error(_("Register '%s': failed to store its actual action periods"), meta->GetSynonym());
}

void KeepActualActionPeriods(const ibValueMetaObjectCalculationRegister* meta, const ibValue& recorder,
	const std::vector<ibCalcRecordFacts>& changed)
{
	if (meta->GetActualActionPeriodQueryable() == nullptr || recorder.IsEmpty())
		return;

	// This recorder's pieces go whatever else happens — its lines may have been renumbered by the write,
	// or moved to another employee, and a piece keyed by an old (recorder, line) would outlive its record.
	// Then its records come back whole (step 1), and what a displacer overlaps is named (step 2).
	const ibBackendQueryColumn* recorderColumn = meta->GetRegisterRecorder()->GetQueryColumn();
	ErasePieces(meta, [&](ibDataQueryBuilder& d) { d.Where(recorderColumn, recorder); });
	CopyWhole(meta, recorder);

	std::set<std::vector<ibValue>> keys;
	int64_t windowStart = 0, windowEnd = 0;
	ListOwnDisplaced(meta, recorder, keys, windowStart, windowEnd);
	ListOwnReversed(meta, recorder, keys, windowStart, windowEnd);

	std::map<ibValue, int> typeIndex;
	std::vector<std::pair<int, int>> edges;
	meta->ReadDisplacementRelation(typeIndex, edges);

	// …and the displacers this write added or removed: whatever they overlap, under any recorder. A STORNO
	// added or removed is the same kind of change — it takes a record of another recorder out of force, or
	// gives it back — so its position is recomputed too.
	std::set<int> displacers;
	for (const auto& edge : edges)
		displacers.insert(edge.second);
	for (const ibCalcRecordFacts& facts : changed)
		if (facts.storno || displacers.count(ibCalcTypeOrdinal(typeIndex, facts.type)) != 0) {
			keys.insert(DimensionKey(meta, facts));
			ibCalcWiden(windowStart, windowEnd, facts.actionStart, facts.actionEnd);
		}
	if (keys.empty() || windowEnd <= windowStart)
		return;   // nothing displaced, no displacer changed: the whole copies are the pieces

	std::set<std::vector<ibValue>> list;
	for (const std::vector<ibValue>& key : keys) {
		list.insert(key);
		if (list.size() == kKeyListLimit) {
			RewriteWindow(meta, AnyOfKeys(meta, list), windowStart, windowEnd, typeIndex, edges);
			list.clear();
		}
	}
	if (!list.empty())
		RewriteWindow(meta, AnyOfKeys(meta, list), windowStart, windowEnd, typeIndex, edges);
}

} // namespace

// The recorder this set is addressed by, or empty when it is addressed by nothing (a set with no key
// clears the whole register — see DeleteData — and marks nothing: there is no one recorder to resolve).
static ibValue RecorderOfSet(const ibValueMetaObjectCalculationRegister* meta, const ibRowMetaValues& keys)
{
	if (meta == nullptr || meta->GetRegisterRecorder() == nullptr)
		return ibValue();
	const auto found = keys.find(meta->GetRegisterRecorder()->GetMetaID());
	return found != keys.end() ? found->second : ibValue();
}

bool ibValueRecordSetObjectCalculationRegister::SaveData(bool replace, bool clearTable)
{
	const ibValueMetaObjectCalculationRegister* meta = m_register;

	// ⭐ A RECORD IS REGISTERED AT THE START OF ITS PERIOD — the register's periodicity (a month unless
	// said otherwise). The registration period and the month-for action period name a PERIOD, so a date
	// inside one becomes its first day here, before anything reads the record: the storno's pairing (a
	// position holds the action period), the base by registration period, and every report that groups
	// by the month all meet one value per period instead of one per day somebody happened to type.
	if (meta != nullptr) {
		const ibTotalsPeriod grain = meta->GetPeriodicityUnit();
		std::vector<const ibValueMetaObjectAttributeBase*> periods{ meta->GetRegistrationPeriod() };
		if (meta->IsUseActionPeriod())
			periods.push_back(meta->GetActionPeriod());
		for (long row = 0; row < GetRowCount(); ++row) {
			const ibDataViewItem item = GetItem(row);
			for (const ibValueMetaObjectAttributeBase* period : periods) {
				ibValue value;
				if (!GetValueByMetaID(item, period->GetMetaID(), value) || value.IsEmpty())
					continue;
				const wxDateTime written = value.GetDateTime();
				const wxDateTime start = ibTruncateToPeriod(written, grain);
				if (start != written)
					SetValueByMetaID(item, period->GetMetaID(), ibValue(start));
			}
		}
	}

	const ibValue recorder = RecorderOfSet(meta, m_keyValues);
	const bool marks = meta != nullptr && !recorder.IsEmpty() && AnyRecalculations(meta->GetMetaData());
	const bool pieces = meta != nullptr && !recorder.IsEmpty() && meta->IsUseActionPeriod();

	// What the recorder held and is about to hold — taken BEFORE the write: under `replace` the stored
	// rows are about to be deleted, and the rows in memory are cleared by a successful write.
	std::vector<ibCalcRecordFacts> changed;
	std::vector<ibCalcRecordFacts> reversals;   // the stornos this set writes — a correction of another period
	if (marks || pieces) {
		const ibBackendQueryColumn* recorderColumn = meta->GetRegisterRecorder()->GetQueryColumn();
		std::vector<ibCalcRecordFacts> before, after;
		if (replace)
			before = ReadStored(meta, [&](ibDataQueryBuilder& q) { q.Where(recorderColumn, recorder); });
		const ibValueMetaObjectAttributeBase* recorderAttribute = meta->GetRegisterRecorder();
		for (long row = 0; row < GetRowCount(); ++row) {
			const ibDataViewItem item = GetItem(row);
			after.push_back(ReadFacts(meta, [&](const ibValueMetaObjectAttributeBase* attribute) {
				if (attribute == recorderAttribute)
					return recorder;   // a set's rows do not carry the key — the write stamps it on
				ibValue value;
				GetValueByMetaID(item, attribute->GetMetaID(), value);
				return value;
			}));
		}
		for (const ibCalcRecordFacts& facts : after)
			if (facts.storno)
				reversals.push_back(facts);
		changed = ibCalcWhatChanged(std::move(before), std::move(after));
	}

	if (!ibValueRecordSetObject::SaveData(replace, clearTable))
		return false;

	if (pieces)
		KeepActualActionPeriods(meta, recorder, changed);
	if (marks)
		KeepRecalculations(meta, recorder, changed, reversals);
	return true;
}

bool ibValueRecordSetObjectCalculationRegister::DeleteData()
{
	const ibValueMetaObjectCalculationRegister* meta = m_register;
	const ibValue recorder = RecorderOfSet(meta, m_keyValues);
	const bool marks = meta != nullptr && !recorder.IsEmpty() && AnyRecalculations(meta->GetMetaData());
	const bool pieces = meta != nullptr && !recorder.IsEmpty() && meta->IsUseActionPeriod();

	std::vector<ibCalcRecordFacts> changed;   // everything the recorder held: all of it goes
	if (marks || pieces) {
		const ibBackendQueryColumn* recorderColumn = meta->GetRegisterRecorder()->GetQueryColumn();
		changed = ReadStored(meta, [&](ibDataQueryBuilder& q) { q.Where(recorderColumn, recorder); });
	}

	if (!ibValueRecordSetObject::DeleteData())
		return false;

	if (pieces)
		KeepActualActionPeriods(meta, recorder, changed);
	if (marks)
		KeepRecalculations(meta, recorder, changed, {});   // a set cleared holds no stornos
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// 🛑⭐⭐ THE ORDER HERE IS THE ORDER OF FillMembers BELOW, and nothing checks it. A method is called by
// its INDEX in the member table, so a case label that sits one row off answers the wrong method. The
// import had Load and Unload ahead of Write here while FillMembers declares Write first — so a
// script's `rs.Write()` ran Load (casting its argument to a table: "Variable type does not support this
// operation"), `Load(t)` ran Unload, and `Unload()` WROTE THE SET. MEASURED 2026-09-10 with a
// first-chance exception stack: ThrowErrorTypeOperation <- ConvertToType<ibValueModel> <- this CallAsFunc.
//
// The accumulation register had this exact misordering at the snapshot the import was taken from
// (2026-08-16) and was corrected on 2026-09-04 (0ace1d0c); this copy predates the correction.
enum recordSet
{
	enAdd = 0,
	enCount,
	enClear,
	enWriteRecordSet,
	enLoad,
	enUnload,
	enModifiedRecordSet,
	enReadRecordSet,
	enSelectedRecordSet,
	enGetMetadataRecordSet,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordSetObjectCalculationRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Add"), wxT("Add()"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(replace : boolean)"));
	helper.AppendFunc(wxT("Load"), 1, wxT("Load(value : any table)"));
	helper.AppendFunc(wxT("Unload"), wxT("Unload()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Selected"), wxT("Selected()"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));

	// `Filter` is NOT declared here — see informationRegisterObject.cpp: it is an export variable of
	// the set, bound in InitializeObject, and that reaches this table on its own.
}

//////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObjectCalculationRegister::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObjectCalculationRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// 🛑⭐⭐ THE SET'S OWN PROPERTIES (Filter) LIVE ON THE BASE — delegate, never answer false outright.
	// This exact defect was fixed in the accumulation register on 2026-09-05; the calculation register was
	// imported from a snapshot of 2026-08-16, BEFORE that fix, and carried the old `return false` with it.
	// MEASURED 2026-09-10 through the registration journal: `TypeOf(rs.Filter)` answered Undefined here
	// and RecordSetRegisterKey on an accumulation register — so no record set of this register could be
	// pointed at its recorder, which is the one thing a register ALWAYS subordinate to one must do.
	return ibValueRecordSetObject::GetPropVal(lPropNum, pvarPropVal);
}

bool ibValueRecordSetObjectCalculationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	switch (lMethodNum)
	{
	case recordSet::enAdd:
		pvarRetValue = new ibValueRecordSetObjectRegisterReturnLine(this, GetItem(AppendRow()));
		return true;
	case recordSet::enCount:
		pvarRetValue = (unsigned int)GetRowCount();
		return true;
	case recordSet::enClear:
		ibValueModelStorage::Clear();
		// ⭐ CLEARING IS A CHANGE. Emptying the set is how a handler says "no movements" - and the
		// document's final write skips a set that is not modified, so an unmarked Clear would leave
		// yesterday's movements standing. For a calculation register this is the RECALCULATION path
		// itself - clear and rewrite - so without it a recalculated payroll kept the old figures.
		// (The accumulation register got this in 0ace1d0c, 2026-09-04; the import predates it.)
		Modify(true);
		return true;
	case recordSet::enLoad:
		LoadDataFromTable(paParams[0]->ConvertToType<ibValueModel>());
		return true;
	case recordSet::enUnload:
		pvarRetValue = SaveDataToTable();
		return true;
	case recordSet::enWriteRecordSet:
		WriteRecordSet(
			lSizeArray > 0 ?
			paParams[0]->GetBoolean() : true
		);
		return true;
	case recordSet::enModifiedRecordSet:
		pvarRetValue = m_objModified;
		return true;
	case recordSet::enReadRecordSet:
		Read();
		return true;
	case recordSet::enSelectedRecordSet:
		pvarRetValue = Selected();
		return true;
	case recordSet::enGetMetadataRecordSet:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}
