////////////////////////////////////////////////////////////////////////////
//	Description : ibTempTableManager — the universal (driver-agnostic) L3 lifetime
//	              owner for a DB temporary table. Materialises an ibQueryRamTable
//	              intermediate into a real temp table on the holder's PINNED
//	              connection (the holder is the lifetime anchor), vends an
//	              ibDbTempTableQueryable read/joined server-side like any DB source,
//	              and DROPs it on destruction. Driven ENTIRELY by the L1 dictionary
//	              (ibTempTableDialect + the main dialect type-map) — no per-driver
//	              fork; a driver with no temp dialect (Firebird) makes Materialise
//	              return null and the caller stays on the RAM composer.
//	              See docs/temp-db.md.
////////////////////////////////////////////////////////////////////////////

#include "tempTableManager.h"
#include "queryRamTable.h"                                  // ibQueryRamTable / ibQueryRamColumn
#include "columnLayout.h"                                   // ColumnFieldNames + ibColumnCodec::WriteValue — the column's physical field spread + codec
#include "backend/databaseLayer/databaseQueryBuilder.h"    // ibDatabaseQueryBuilder + DDL/DML factories + ibQueryStatement (L2)

#include <atomic>
#include <vector>
#include <map>
#include <mutex>

namespace {

// The temp table stores every column in the METADATA STORAGE FORMAT (the same physical spread a real
// table uses): one DDL column per ColumnFieldNames field, typed by suffix; the cell decomposed by the
// SAME SetValueColumn the DB write path uses. So a temp row is byte-identical to a real-table row and
// reference / enum / variant values round-trip (keys AND outputs) via GetValueColumn — no lossy form.

// DDL type for one physical SPREAD field, keyed by its layout ROLE (not by re-parsing the name suffix —
// ibFieldSuffix already owns the role<->suffix lettering). Deliberately WIDE: the temp holds heterogeneous
// RAM intermediates, so slack (blob ref-key, number(38,10), string(4000)) avoids truncation; the tags +
// raw fall to integer. Output is identical to the former suffix parse.
ibColumnType WidenForTemp(ibColumnRole role)
{
	switch (role) {
	case ibColumnRole::ReferenceId: return ibTypeBlob();
	case ibColumnRole::Number:      return ibTypeNumber(38, 10);
	case ibColumnRole::Date:        return ibTypeDate();
	case ibColumnRole::Boolean:     return ibTypeBoolean();
	case ibColumnRole::String:      return ibTypeString(4000);
	default:                        return ibTypeInteger();   // Discriminator / Enum / ReferenceType / Raw
	}
}

// Decompose ONE cell into its per-field bound exprs (Const / Blob), in ColumnFieldNames order — REUSING
// the DB write decomposition (a capture-only ibQueryStatement records each SetParam* as an IR node).
std::vector<ibQueryExprPtr> DecomposeCell(const ibBackendQueryColumn* col, const ibMetaData* meta, const ibValue& cell)
{
	const std::vector<wxString> fields = ColumnFieldNames(col);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibColumnCodec::WriteValue(col, meta, cell, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
	std::vector<ibQueryExprPtr> out;
	out.reserve(fields.size());
	for (size_t i = 0; i < fields.size(); ++i)
		out.push_back((i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()));
	return out;
}

// --- runtime capability probe, cached per holder (temp-db.md §6) -------------------------------
// Static capability = the dialect's presence. DYNAMIC capability = "this connection can actually
// CREATE a temp table" (DDL right / not a read-only replica / temp-space available). We probe it ONCE
// per holder with a trivial CREATE+DROP and remember — so a real materialise never discovers
// incapability mid-bulk-insert (pay the failed million-row insert, then redo in RAM).
std::mutex                                  s_probeMutex;
std::map<ibDatabaseConnectionHolder*, bool> s_probeCache;

bool ProbeTempCapability(ibDatabaseConnectionHolder* holder, const ibTempTableDialect& dialect)
{
	{
		std::lock_guard<std::mutex> lock(s_probeMutex);
		const auto it = s_probeCache.find(holder);
		if (it != s_probeCache.end())
			return it->second;
	}

	bool capable = false;
	try {
		static std::atomic<unsigned long> s_probeSeq{ 0 };
		const wxString name = wxString::Format(wxT("oes_tmp_probe_%lu"), ++s_probeSeq);
		std::vector<ibDdlColumn> col(1);
		col[0].m_name = wxT("probe");
		col[0].m_type = ibTypeInteger();
		ibDatabaseQueryBuilder q(holder);
		q.Execute(ibCreateTempTable(name, col, dialect.m_createPrefix, dialect.m_onCommitClause));
		try { ibDatabaseQueryBuilder qd(holder); qd.Execute(ibDropTable(name, /*ifExists*/ true)); } catch (...) {}
		capable = true;
	}
	catch (...) {
		capable = false;
	}

	std::lock_guard<std::mutex> lock(s_probeMutex);
	s_probeCache[holder] = capable;
	return capable;
}

} // namespace

std::unique_ptr<ibTempTableManager> ibTempTableManager::Materialise(ibDatabaseConnectionHolder* holder,
                                                                    const ibQueryRamTable& rows,
                                                                    const ibMetaData* metaData)
{
	if (rows.Columns().empty())
		return nullptr;

	// Pin the holder's connection for the table's whole life — the temp table lives on THIS connection
	// and every later statement (fill / join / drop) must land on it.
	ibConnectionScope scope(holder);
	if (!scope)
		return nullptr;   // no connection (pool passive / saturated) -> RAM

	// Capability = the L1 temp dialect's presence. nullptr => the driver has no DB temp tables (FB) =>
	// the caller stays on the RAM composer.
	const ibTempTableDialect* dialect = scope->GetTempTableDialect();
	if (dialect == nullptr)
		return nullptr;
	if (!ProbeTempCapability(holder, *dialect))
		return nullptr;   // dialect present but this connection can't create temp tables (no DDL right / read-only) -> RAM

	static std::atomic<unsigned long> s_seq{ 0 };
	const wxString tableName = wxString::Format(wxT("oes_tmp_%lu"), ++s_seq);

	// Metadata-format columns: one ibTempColumn per logical column (real type), each EXPANDING to its
	// physical spread via DescribeColumnLayout — the ONE authority for the field set, ORDER and role.
	// That flattened slot list drives BOTH the CREATE (name + type-by-role) and the INSERT column order,
	// so the temp mirrors a real table from a single source.
	std::vector<ibTempColumn> tempCols;
	tempCols.reserve(rows.Columns().size());
	for (const ibQueryRamColumn& c : rows.Columns())
		tempCols.emplace_back(c.m_name, c.m_type, c.m_id);

	std::vector<ibColumnSlot> slots;   // flattened spread over all columns
	for (const ibTempColumn& tc : tempCols)
		for (const ibColumnSlot& s : DescribeColumnLayout(&tc))
			slots.push_back(s);

	std::vector<wxString> allFields;   // slot names — the INSERT column order
	allFields.reserve(slots.size());
	for (const ibColumnSlot& s : slots)
		allFields.push_back(s.m_name);

	// Decompose a whole row into its flattened per-field bound exprs (column order = allFields).
	auto buildRow = [&](long r) {
		std::vector<ibQueryExprPtr> vals;
		vals.reserve(allFields.size());
		size_t ci = 0;
		for (const ibQueryRamColumn& c : rows.Columns()) {
			std::vector<ibQueryExprPtr> fv = DecomposeCell(&tempCols[ci], metaData, rows.GetCell(r, c.m_id));
			for (auto& e : fv) vals.push_back(std::move(e));
			++ci;
		}
		return vals;
	};

	// CREATE + fill on the pinned connection (nested ibDatabaseQueryBuilder over the same holder
	// inherits this scope's connection). Any runtime failure -> drop the half-built table, return null
	// (graceful RAM fallback — the fast path is opportunistic, correctness always lands).
	try {
		std::vector<ibDdlColumn> ddlCols;
		ddlCols.reserve(slots.size());
		for (const ibColumnSlot& s : slots) {
			ibDdlColumn dc; dc.m_name = s.m_name; dc.m_type = WidenForTemp(s.m_role);
			ddlCols.push_back(std::move(dc));
		}
		ibDatabaseQueryBuilder qCreate(holder);
		qCreate.Execute(ibCreateTempTable(tableName, ddlCols, dialect->m_createPrefix, dialect->m_onCommitClause));

		// Bulk-fill in CHUNKS — one multi-row INSERT per chunk (row 0 carries the column list as
		// assignments, the rest ride as extra VALUES tuples). The chunk caps params/row to stay under
		// driver placeholder limits (PG ~65535, SQLite 999); per-row would be RowCount round-trips.
		const long total  = rows.RowCount();
		const long kChunk = 50;   // each row is now several physical fields per logical column
		for (long start = 0; start < total; start += kChunk) {
			const long end = (start + kChunk < total) ? start + kChunk : total;

			ibDmlStatement ins(ibDmlKind::Insert);
			ins.m_table = tableName;
			std::vector<ibQueryExprPtr> first = buildRow(start);
			for (size_t k = 0; k < allFields.size() && k < first.size(); ++k)
				ins.m_assignments.push_back(ibDmlAssign{ allFields[k], first[k] });
			for (long r = start + 1; r < end; ++r)
				ins.m_extraRows.push_back(buildRow(r));

			ibDatabaseQueryBuilder qIns(holder);
			qIns.Execute(ins);
		}
	}
	catch (...) {
		try { ibDatabaseQueryBuilder qDrop(holder); qDrop.Execute(ibDropTable(tableName, /*ifExists*/ true)); }
		catch (...) {}
		return nullptr;
	}

	// Refresh optimiser statistics on the freshly-filled temp (ANALYZE) so the DBMS plans any
	// JOIN against the temp's REAL cardinality, not a default estimate — the actual optimisation
	// the materialisation exists to enable (docs/temp-db.md). Goes through the L2 door as a
	// first-class statement (ibDdlKind::Analyze, dialect-rendered) so it is reusable beyond temp
	// and carries NO per-driver fork here; a driver with no ANALYZE renders empty and no-ops. The
	// nested builder inherits this scope's pinned connection (the temp lives on it). Best-effort:
	// a failure leaves default estimates — the temp is still correct, only the plan is less informed.
	try { ibDatabaseQueryBuilder qAnalyze(holder); qAnalyze.Execute(ibAnalyzeTable(tableName)); }
	catch (...) {}

	auto queryable = std::make_unique<ibDbTempTableQueryable>(tableName, std::move(tempCols), metaData);
	return std::unique_ptr<ibTempTableManager>(
		new ibTempTableManager(std::move(scope), holder, *dialect, tableName, std::move(queryable)));
}

ibTempTableManager::ibTempTableManager(ibConnectionScope scope, ibDatabaseConnectionHolder* holder,
                                       ibTempTableDialect dialect, wxString tableName,
                                       std::unique_ptr<ibDbTempTableQueryable> queryable)
	: m_scope(std::move(scope))
	, m_holder(holder)
	, m_dialect(std::move(dialect))
	, m_tableName(std::move(tableName))
	, m_queryable(std::move(queryable))
{
}

ibTempTableManager::~ibTempTableManager()
{
	// m_scope is still alive in the dtor BODY (members destroy after it), so the connection is still
	// pinned — a nested ibDatabaseQueryBuilder over m_holder lands on the right connection to DROP.
	// Skip when the dialect auto-drops at session / commit end. Best-effort — never throw from a dtor.
	if (m_tableName.empty() || m_dialect.m_autoDrops)
		return;
	try {
		ibDatabaseQueryBuilder q(m_holder);
		q.Execute(ibDropTable(m_tableName, /*ifExists*/ true));
	}
	catch (...) {}
}
