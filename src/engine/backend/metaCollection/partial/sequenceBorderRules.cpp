////////////////////////////////////////////////////////////////////////////
//	Description : the three rules of a sequence's border (docs/private/sequence-arc.md)
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ THE BORDER MOVES BY WHAT THE CONFIGURATION WROTE, AND BY NOTHING IT HAS TO GUESS. A document's
// own registrations name the keys it belongs to; the engine reads them and moves those keys' borders.
// It never invents a dimension value — which is the whole reason there is no auto-registration here.
//
//   FORWARD, one step, and only as a document is posted IN ITS TURN: nothing of that key stands
//           between the border and this document, so the border becomes this document's moment.
//   BACK, whenever a document at or before the border is touched — written again, posted again,
//           unposted, deleted: the border goes to the registration before it, or away entirely.
//
// ⚠ AND NEVER FORWARD BY ITSELF over documents that merely happen to be posted. They are posted —
// but they were posted BEFORE the document in front of them changed, which is exactly what puts them
// in doubt. Walking the border over them would erase the break at the moment it appears.
//
// ⚠ HOW TWO MOMENTS ARE COMPARED HERE. The moment is a column of the selection
// (ibSequenceMomentColumn) and a query orders by it; but the lexicographic decomposition that makes
// `WHERE Moment < &Point` run lives inside the provider and is not offered to callers, and the value
// codec binds by a value's TAG, which a moment has none of. So these rules sift in SQL by the PERIOD
// — a plain date column, and an indexed one — and settle the tie among the few rows standing at the
// very same period IN MEMORY, where a moment compares itself (ibValuePointInTime::CompareValueLS).
// Exact, and no second spelling of the order.

#include "sequence.h"

#include "backend/metaData.h"
#include "backend/backend_exception.h"                               // ibBackendCoreException — a refusal says who refused
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/partial/registerQueryLowering.h"    // ibRegCompositeIR, ibRegFieldOfRole

namespace {

const wxString kRows = wxT("r"), kBorder = wxT("b");

// One registration of the recorder: the key it names, and the moment it stands at.
struct ibSequenceRegistration {
	std::vector<ibValue> m_key;
	ibValue              m_moment;
};

wxString PeriodField(const ibValueMetaObjectSequence* seq)
{
	return ibRegFieldOfRole(seq->GetRegisterPeriod()->GetQueryColumn(), ibColumnRole::Date);
}

// ⭐ A REGISTRATION COUNTS WHILE IT IS ACTIVE — the same question every register reading asks of a
// row. An inactive registration stays in the table and takes part in nothing: a border must not come
// to stand on a row that says it does not count.
ibQueryExprPtr Counts(const ibValueMetaObjectSequence* seq, const wxString& alias)
{
	return ibBinOp(ibQueryBinOp::Ne, ibCol(alias, ibRegValueField(seq->GetRegisterActive()->GetQueryColumn())),
		ibConst(ibValue(0)));
}

ibQueryExprPtr AndCounts(const ibValueMetaObjectSequence* seq, ibQueryExprPtr where, const wxString& alias)
{
	const ibQueryExprPtr counts = Counts(seq, alias);
	return where ? ibBinOp(ibQueryBinOp::And, where, counts) : counts;
}

// The date half of a moment — what the SQL sift goes by.
ibValue PeriodOf(const ibValue& moment)
{
	ibValuePointInTime* point = nullptr;
	ibValue value = moment;
	if (value.ConvertToValue(point) && point != nullptr)
		return ibValue(point->m_date);
	return moment;   // a bare date is a legitimate moment: the instant itself
}

// `key` said as a condition over `alias`: every dimension equal to the value the registration holds.
ibQueryExprPtr SameKey(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key, const wxString& alias)
{
	const ibMetaData* metaData = seq->GetMetaData();
	ibQueryExprPtr all;
	size_t at = 0;
	for (const ibValueMetaObjectDimension* dimension : seq->GetDimensionArrayObject()) {
		if (at >= key.size())
			break;
		if (const ibQueryExprPtr one = ibRegCompositeIR(dimension->GetQueryColumn(), metaData, key[at], ibQueryBinOp::Eq, alias))
			all = all ? ibBinOp(ibQueryBinOp::And, all, one) : one;
		++at;
	}
	return all;   // null for a sequence with no dimensions: one key, and the whole table is it
}

// The period and the recorder, projected under their own field names — what a moment is read out of.
std::vector<ibQueryProjItem> MomentProjection(const ibValueMetaObjectSequence* seq, const wxString& alias)
{
	std::vector<ibQueryProjItem> proj;
	std::set<wxString> projected;
	for (const ibBackendQueryColumn* part : { seq->GetRegisterPeriod()->GetQueryColumn(),
	                                          seq->GetRegisterRecorder()->GetQueryColumn() })
		for (const wxString& field : ColumnFieldNames(part))
			if (projected.insert(field).second)
				proj.push_back(ibQueryProjItem{ ibCol(alias, field), field });
	return proj;
}

// The registrations this recorder holds — read after they are written, or before they are cleared.
std::vector<ibSequenceRegistration> RegistrationsOf(const ibValueMetaObjectSequence* seq, const ibValue& recorder)
{
	std::vector<ibSequenceRegistration> rows;
	const ibMetaData* metaData = seq->GetMetaData();
	const ibQueryExprPtr byRecorder = ibRegCompositeIR(seq->GetRegisterRecorder()->GetQueryColumn(),
		metaData, recorder, ibQueryBinOp::Eq, kRows);
	if (byRecorder == nullptr)
		return rows;

	std::vector<ibQueryProjItem> proj = MomentProjection(seq, kRows);
	std::set<wxString> projected;
	for (const ibQueryProjItem& item : proj)
		projected.insert(item.m_alias);
	for (const ibValueMetaObjectDimension* dimension : seq->GetDimensionArrayObject())
		for (const wxString& field : ColumnFieldNames(dimension->GetQueryColumn()))
			if (projected.insert(field).second)
				proj.push_back(ibQueryProjItem{ ibCol(kRows, field), field });

	ibDatabaseQueryBuilder q;
	q.From(ibScan(seq->GetQueryable()->GetQueryTableName(), kRows));
	q.Where(AndCounts(seq, byRecorder, kRows));
	q.Project(proj);
	ibQueryResult rs = q.Execute();
	while (rs.Next()) {
		ibSequenceRegistration one;
		for (const ibValueMetaObjectDimension* dimension : seq->GetDimensionArrayObject()) {
			ibValue value;
			const ibBackendQueryColumn* column = dimension->GetQueryColumn();
			column->ReadValue(column->GetPhysicalName(), metaData, value, rs);
			one.m_key.push_back(value);
		}
		if (seq->GetMomentColumn()->ReadValue(wxEmptyString, metaData, one.m_moment, rs))
			rows.push_back(std::move(one));
	}
	return rows;
}

// Where the border of `key` stands now — empty when the key has none yet.
ibValue BorderOf(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key)
{
	ibValue moment;
	if (!seq->HasBorders())
		return moment;

	ibDatabaseQueryBuilder q;
	q.From(ibScan(seq->GetBordersTableName(), kBorder));
	if (const ibQueryExprPtr same = SameKey(seq, key, kBorder))
		q.Where(same);
	q.Project(MomentProjection(seq, kBorder));
	ibQueryResult rs = q.Execute();
	if (rs.Next())
		seq->GetMomentColumn()->ReadValue(wxEmptyString, seq->GetMetaData(), moment, rs);
	return moment;
}

// The moments of `key` standing at exactly this period — the handful a tie is settled among.
std::vector<ibValue> MomentsAt(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key, const ibValue& period)
{
	std::vector<ibValue> moments;
	const ibMetaData* metaData = seq->GetMetaData();
	ibQueryExprPtr where = ibRegCompositeIR(seq->GetRegisterPeriod()->GetQueryColumn(), metaData, period, ibQueryBinOp::Eq, kRows);
	if (where == nullptr)
		return moments;
	if (const ibQueryExprPtr same = SameKey(seq, key, kRows))
		where = ibBinOp(ibQueryBinOp::And, same, where);

	ibDatabaseQueryBuilder q;
	q.From(ibScan(seq->GetQueryable()->GetQueryTableName(), kRows));
	q.Where(AndCounts(seq, where, kRows));
	q.Project(MomentProjection(seq, kRows));
	ibQueryResult rs = q.Execute();
	while (rs.Next()) {
		ibValue moment;
		if (seq->GetMomentColumn()->ReadValue(wxEmptyString, metaData, moment, rs))
			moments.push_back(moment);
	}
	return moments;
}

// The last period of `key` standing strictly before this one — empty when there is none.
ibValue PeriodBefore(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key, const ibValue& period)
{
	ibValue found;
	const ibMetaData* metaData = seq->GetMetaData();
	const ibBackendQueryColumn* column = seq->GetRegisterPeriod()->GetQueryColumn();
	ibQueryExprPtr where = ibRegCompositeIR(column, metaData, period, ibQueryBinOp::Lt, kRows);
	if (where == nullptr)
		return found;
	if (const ibQueryExprPtr same = SameKey(seq, key, kRows))
		where = ibBinOp(ibQueryBinOp::And, same, where);

	ibDatabaseQueryBuilder q;
	q.From(ibScan(seq->GetQueryable()->GetQueryTableName(), kRows));
	q.Where(AndCounts(seq, where, kRows));
	q.Project({ ibQueryProjItem{ ibFunc(wxT("MAX"), { ibCol(kRows, PeriodField(seq)) }), PeriodField(seq) } });
	ibQueryResult rs = q.Execute();
	// 🛑 READ AS THE DATE IT IS, NOT THROUGH THE COLUMN'S CODEC. The projection above is one field — MAX over
	// the period's date — and the codec reads a TAGGED cell: finding no `_TYPE` beside it, it answers "field
	// not in the result set" with the type's EMPTY value and a false nobody looked at. An empty date here
	// reads as "nothing stands before this document", so every retreat that had to cross into an earlier
	// period took the border AWAY instead of back: posting a document behind the border, and even posting
	// again the very document the border stood on, left the key with no border at all — and a border that
	// is gone sends the restoring run over the key's whole history (measured 2026-09-20: R1, R2, R3 posted
	// in turn, R1.5 posted behind them - the border was expected at R1 and was gone).
	// A MAX over no rows is NULL, which is the one case that really means "nothing before".
	if (rs.Next() && !rs.IsResultNull(PeriodField(seq))) {
		const wxDateTime earlier = rs.GetResultDate(PeriodField(seq));
		if (earlier.IsValid())
			found = ibValue(earlier);
	}
	return found;
}

// The greatest moment of `key` standing strictly before `moment` — where a retreat lands. Empty when
// the key has nothing before it at all: the border then goes away.
ibValue LastBefore(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key, const ibValue& moment)
{
	const auto greatestBelow = [&moment](const std::vector<ibValue>& candidates) {
		ibValue best;
		for (const ibValue& one : candidates)
			if (one.CompareValueLS(moment) < 0 && (best.IsEmpty() || best.CompareValueLS(one) < 0))
				best = one;
		return best;
	};

	// First among its own period — the tie the SQL sift cannot settle…
	const ibValue period = PeriodOf(moment);
	const ibValue sameDay = greatestBelow(MomentsAt(seq, key, period));
	if (!sameDay.IsEmpty())
		return sameDay;

	// …then the period before it, whose rows are all below by construction.
	const ibValue earlier = PeriodBefore(seq, key, period);
	if (earlier.IsEmpty())
		return ibValue();
	const std::vector<ibValue> candidates = MomentsAt(seq, key, earlier);
	ibValue best;
	for (const ibValue& one : candidates)
		if (best.IsEmpty() || best.CompareValueLS(one) < 0)
			best = one;
	return best;
}

// Is there a registration of `key` standing between the border and `moment`? Then this document is
// not next in line, and the border stays where it is.
bool AnythingBetween(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key,
	const ibValue& border, const ibValue& moment)
{
	const ibMetaData* metaData = seq->GetMetaData();
	const ibBackendQueryColumn* column = seq->GetRegisterPeriod()->GetQueryColumn();
	const ibValue period = PeriodOf(moment);

	// Whole periods in between — counted in the database.
	ibQueryExprPtr where = ibRegCompositeIR(column, metaData, period, ibQueryBinOp::Lt, kRows);
	if (where == nullptr)
		return false;
	if (!border.IsEmpty())
		if (const ibQueryExprPtr after = ibRegCompositeIR(column, metaData, PeriodOf(border), ibQueryBinOp::Gt, kRows))
			where = ibBinOp(ibQueryBinOp::And, where, after);
	if (const ibQueryExprPtr same = SameKey(seq, key, kRows))
		where = ibBinOp(ibQueryBinOp::And, same, where);

	ibDatabaseQueryBuilder q;
	q.From(ibScan(seq->GetQueryable()->GetQueryTableName(), kRows));
	q.Where(AndCounts(seq, where, kRows));
	q.Project({ ibQueryProjItem{ ibFunc(wxT("COUNT"), { ibCol(kRows, PeriodField(seq)) }), wxT("ib_between") } });
	ibQueryResult rs = q.Execute();
	if (rs.Next() && rs.GetResultNumber(wxT("ib_between")) > 0)
		return true;

	// …and the rows standing at the two edge periods, where only a moment can tell.
	const auto standsBetween = [&border, &moment](const ibValue& one) {
		return one.CompareValueLS(moment) < 0 && (border.IsEmpty() || border.CompareValueLS(one) < 0);
	};
	for (const ibValue& one : MomentsAt(seq, key, period))
		if (standsBetween(one))
			return true;
	if (!border.IsEmpty())
		for (const ibValue& one : MomentsAt(seq, key, PeriodOf(border)))
			if (standsBetween(one))
				return true;
	return false;
}

// A value said as the fields that hold it — the write half of ibRegCompositeIR, through the same
// codec: the caller names a column and a value, and the layout tier says which fields carry it.
std::vector<ibDmlAssign> AssignmentsFor(const ibBackendQueryColumn* column, const ibMetaData* metaData, const ibValue& value)
{
	std::vector<ibDmlAssign> out;
	const std::vector<ibColumnSlot> slots = DescribeColumnLayout(column);
	std::vector<wxString> fields;
	fields.reserve(slots.size());
	for (const ibColumnSlot& slot : slots)
		fields.push_back(slot.m_name);

	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibColumnCodec::WriteValue(column, metaData, value, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
	for (size_t i = 0; i < fields.size(); ++i)
		out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	return out;
}

} // namespace

// ---- what the write path knocks on -----------------------------------------------------------------

void ibSequenceBorderWritten(const ibValueMetaObjectSequence* seq, const ibValue& recorder)
{
	if (seq == nullptr || !seq->HasBorders())
		return;
	for (const ibSequenceRegistration& row : RegistrationsOf(seq, recorder)) {
		const ibValue border = BorderOf(seq, row.m_key);
		if (!border.IsEmpty() && border.CompareValueLS(row.m_moment) >= 0) {
			// It stands at or before the border: everything after it is in doubt again — unless the
			// sequence says the configuration keeps that decision (IsAutomaticBorder).
			if (seq->IsAutomaticBorder())
				ibSequenceBorderSet(seq, row.m_key, LastBefore(seq, row.m_key, row.m_moment));
			continue;
		}
		if (!AnythingBetween(seq, row.m_key, border, row.m_moment))
			ibSequenceBorderSet(seq, row.m_key, row.m_moment);   // next in line: one step forward
	}
}

void ibSequenceBorderCleared(const ibValueMetaObjectSequence* seq, const ibValue& recorder)
{
	// Clearing a document's registrations can only ever send the border BACK, so a sequence whose
	// retreat the configuration keeps has nothing to do here.
	if (seq == nullptr || !seq->HasBorders() || !seq->IsAutomaticBorder())
		return;
	for (const ibSequenceRegistration& row : RegistrationsOf(seq, recorder)) {
		const ibValue border = BorderOf(seq, row.m_key);
		if (!border.IsEmpty() && border.CompareValueLS(row.m_moment) >= 0)
			ibSequenceBorderSet(seq, row.m_key, LastBefore(seq, row.m_key, row.m_moment));
	}
}

// Said outright — by the rules above, and by `Sequences.<Name>.SetBorder(…)`. An empty moment takes
// the border away: the key has nothing anybody has vouched for.
void ibSequenceBorderSet(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key, const ibValue& moment)
{
	if (seq == nullptr || !seq->HasBorders())
		return;
	const ibMetaData* metaData = seq->GetMetaData();
	const wxString table = seq->GetBordersTableName();

	if (moment.IsEmpty()) {
		ibDatabaseQueryBuilder drop;
		drop.Execute(ibDelete(table, SameKey(seq, key, wxEmptyString)));
		return;
	}

	// The moment written as the two things that hold it: the period, and the document standing there.
	ibValue period = PeriodOf(moment), recorder;
	{
		ibValuePointInTime* point = nullptr;
		ibValue value = moment;
		if (value.ConvertToValue(point) && point != nullptr)
			recorder = point->m_reference;
	}

	std::vector<ibDmlAssign> assignments;
	std::vector<wxString> matchKeys;
	size_t at = 0;
	for (const ibValueMetaObjectDimension* dimension : seq->GetDimensionArrayObject()) {
		if (at >= key.size())
			break;
		for (const ibDmlAssign& one : AssignmentsFor(dimension->GetQueryColumn(), metaData, key[at])) {
			assignments.push_back(one);
			matchKeys.push_back(one.m_column);   // the dimensions are the key a border is matched by
		}
		++at;
	}
	for (const ibDmlAssign& one : AssignmentsFor(seq->GetRegisterPeriod()->GetQueryColumn(), metaData, period))
		assignments.push_back(one);
	for (const ibDmlAssign& one : AssignmentsFor(seq->GetRegisterRecorder()->GetQueryColumn(), metaData, recorder))
		assignments.push_back(one);

	ibDatabaseQueryBuilder set;
	if (set.Execute(ibUpsert(table, assignments, matchKeys)) < 0)
		ibBackendCoreException::Error(_("Sequence '%s': failed to move the border"), seq->GetSynonym());
}

ibValue ibSequenceBorderGet(const ibValueMetaObjectSequence* seq, const std::vector<ibValue>& key)
{
	return seq != nullptr ? BorderOf(seq, key) : ibValue();
}
