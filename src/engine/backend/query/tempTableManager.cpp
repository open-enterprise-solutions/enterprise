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
#include "dbTableProvider.h"                                // ibDbTableProvider::WriteFieldsOf / SetValueColumn (metadata-format fill)
#include "backend/databaseLayer/databaseQueryBuilder.h"    // ibDatabaseQueryBuilder + DDL/DML factories + ibQueryStatement (L2)

#include <atomic>
#include <vector>
#include <map>
#include <mutex>

namespace {

// The temp table stores every column in the METADATA STORAGE FORMAT (the same physical spread a real
// table uses): one DDL column per WriteFieldsOf field, typed by suffix; the cell decomposed by the
// SAME SetValueColumn the DB write path uses. So a temp row is byte-identical to a real-table row and
// reference / enum / variant values round-trip (keys AND outputs) via GetValueColumn — no lossy form.

// DDL type for one physical SPREAD field, keyed by its suffix:
//   _RRRef -> blob ; _N -> number ; _D -> date ; _B -> boolean ; _S -> string ; _TYPE/_E/_RTRef -> integer.
ibColumnType DdlTypeForField(const wxString& field)
{
	if (field.EndsWith(wxT("_RRRef"))) return ibTypeBlob();
	if (field.EndsWith(wxT("_N")))     return ibTypeNumber(38, 10);
	if (field.EndsWith(wxT("_D")))     return ibTypeDate();
	if (field.EndsWith(wxT("_B")))     return ibTypeBoolean();
	if (field.EndsWith(wxT("_S")))     return ibTypeString(4000);
	return ibTypeInteger();   // _TYPE / _E / _RTRef
}

// Decompose ONE cell into its per-field bound exprs (Const / Blob), in WriteFieldsOf order — REUSING
// the DB write decomposition (a capture-only ibQueryStatement records each SetParam* as an IR node).
std::vector<ibQueryExprPtr> DecomposeCell(const ibBackendQueryColumn* col, const ibMetaData* meta, const ibValue& cell)
{
	const std::vector<wxString> fields = ibDbTableProvider::WriteFieldsOf(col, meta);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibDbTableProvider::SetValueColumn(col, meta, cell, &capture, pos);
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
	// physical spread (WriteFieldsOf) — that flattened field list drives BOTH the CREATE and the INSERT
	// column order, so the temp mirrors a real table.
	std::vector<ibTempColumn> tempCols;
	tempCols.reserve(rows.Columns().size());
	for (const ibQueryRamColumn& c : rows.Columns())
		tempCols.emplace_back(c.m_name, c.m_type, c.m_id);

	std::vector<wxString> allFields;   // flattened spread over all columns — the INSERT/CREATE column order
	for (const ibTempColumn& tc : tempCols)
		for (const wxString& f : ibDbTableProvider::WriteFieldsOf(&tc, metaData))
			allFields.push_back(f);

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
		ddlCols.reserve(allFields.size());
		for (const wxString& f : allFields) {
			ibDdlColumn dc; dc.m_name = f; dc.m_type = DdlTypeForField(f);
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
