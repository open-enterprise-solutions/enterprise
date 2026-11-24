////////////////////////////////////////////////////////////////////////////
//	Description : ibTempTableManager — the universal (driver-agnostic) L3 lifetime
//	              owner for a DB temporary table. Materialises an ibQueryRamTable
//	              intermediate into a real temp table on the holder's PINNED
//	              connection (the holder is the lifetime anchor), vends an
//	              ibDbTempTableQueryable read/joined server-side like any DB source,
//	              and DROPs it on destruction. Driven ENTIRELY by the L1 dictionary
//	              (ibTempTableDialect + the main dialect type-map) — no per-driver
//	              fork; a driver with no temp dialect (Firebird) makes Materialise
//	              return null and the caller takes its road in memory. The base in
//	              memory is a base like any (ibQueryMemoryManager, a space of it ibMemorySpace): its copies are made here too.
//	              See docs/private/temp-db.md.
////////////////////////////////////////////////////////////////////////////

#include "tempTableManager.h"
#include "backend/query/queryRamTable.h"                    // ibQueryRamTable / ibQueryRamColumn
#include "backend/query/columnLayout.h"                     // DescribeColumnLayout — the column's physical field spread
#include "backend/databaseLayer/databaseQueryBuilder.h"    // ibDatabaseQueryBuilder + DDL factories + ibBatchInsert (L2)
#include "backend/databaseLayer/connectionHolder.h"        // the base a holder takes its connections from — what the probe is kept by
#include "backend/query/dataQueryBuilder.h"                // a table's rows read for its copy
#include "backend/query/queryException.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <vector>
#include <map>
#include <mutex>

namespace {

// The temp table stores every column in the METADATA STORAGE FORMAT (the same physical spread a real
// table uses): one DDL column per DescribeColumnLayout field, typed by role; the cell bound by the column
// itself (BindValue), as the DB write path binds it. So a temp row is byte-identical to a real-table row and
// reference / enum / variant values round-trip (keys AND outputs) via ReadValue — no lossy form.

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
	case ibColumnRole::Schedule:
	case ibColumnRole::TypeDescription:
	case ibColumnRole::Value:       return ibTypeBlob();      // a value serialised: its bytes (the codec reads them back)
	case ibColumnRole::ReferenceType: return ibTypeBigInt();  // a clsid: 64 bits, its kind in the top byte (DescribeLayout)
	default:                        return ibTypeInteger();   // Discriminator / Enum / Raw
	}
}

// --- runtime capability probe, cached per base (temp-db.md §6) ---------------------------------
// Static capability = the dialect's presence. DYNAMIC capability = "this connection can actually
// CREATE a temp table" (DDL right / not a read-only replica / temp-space available). We probe it ONCE
// per base with a trivial CREATE+DROP and remember — so a real materialise never discovers
// incapability mid-bulk-insert (pay the failed million-row insert, then redo in RAM).
//
// ⚠ BY THE BASE, NOT BY THE HOLDER. Whether a table can be created is the base's answer (its rights, a replica), and
// a holder is short-lived: a session's address, freed, is the next session's — of another base, perhaps — which then
// read the first one's answer. A base's pool stands while the base does. A base in memory is never probed: its tables
// are rows in the process, and a table is always made there.
std::mutex                       s_probeMutex;
std::map<ibConnectionPool*, bool> s_probeCache;

bool ProbeTempCapability(ibDatabaseConnectionHolder* holder, const std::shared_ptr<ibDatabaseLayer>& connection,
                         const ibTempTableDialect& dialect)
{
	if (connection->GetDialect().m_features.m_inMemory)
		return true;
	ibConnectionPool* const base = holder != nullptr ? holder->GetPool() : nullptr;
	{
		std::lock_guard<std::mutex> lock(s_probeMutex);
		const auto it = s_probeCache.find(base);
		if (it != s_probeCache.end())
			return it->second;
	}

	bool capable = false;
	try {
		static std::atomic<unsigned long> s_probeSeq{ 0 };
		const ibString name = ibString::Format(ibT("oes_tmp_probe_%lu"), ++s_probeSeq);
		std::vector<ibDdlColumn> col(1);
		col[0].m_name = ibT("probe");
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
	s_probeCache[base] = capable;
	return capable;
}

} // namespace

// ⭐⭐ CAN, ASKED BEFORE THE ROWS ARE PAID FOR. Every promote site computed its source first and only then
// called Materialise, which on a driver with no temp tables answers null on its first lines — and that
// driver is Firebird, the default engine. So the computed source was read whole, held, and thrown away,
// and the RAM road it fell back to read it again: the payroll sheet read its 86 thousand accrual lines
// twice, 37 s each in Debug (MEASURED 2026-09-12). The question is the same one Materialise asks, the
// dialect's presence and then the probe, only asked of a caller that has nothing in hand yet.
bool ibTempTableManager::CanMaterialise(ibDatabaseConnectionHolder* holder)
{
	ibConnectionScope scope(holder);
	if (!scope)
		return false;
	const ibTempTableDialect* dialect = scope->GetTempTableDialect();
	return dialect != nullptr && ProbeTempCapability(holder, scope.shared(), *dialect);
}

std::unique_ptr<ibTempTableManager> ibTempTableManager::Materialise(ibDatabaseConnectionHolder* holder,
                                                                    const ibQueryRamTable& rows,
                                                                    const ibMetaData* metaData)
{
	// Metadata-format columns: one ibTempColumn per logical column (real type), each EXPANDING to its
	// physical spread via DescribeColumnLayout — the ONE authority for the field set, ORDER and role.
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns;
	columns.reserve(rows.Columns().size());
	for (const ibQueryRamColumn& c : rows.Columns())
		columns.push_back(std::make_shared<ibTempColumn>(c.m_name, c.m_type, c.m_id, metaData));
	static std::atomic<unsigned long> s_seq{ 0 };
	return Materialise(holder, ibString::Format(ibT("oes_tmp_%lu"), ++s_seq), std::move(columns), {}, rows, metaData);
}

std::unique_ptr<ibTempTableManager> ibTempTableManager::Materialise(ibDatabaseConnectionHolder* holder,
                                                                    const ibQueryRamTable& rows,
                                                                    const std::shared_ptr<const ibBackendQueryable>& source)
{
	if (source == nullptr)
		return nullptr;
	// The source's columns the rows hold — a source computed whole hands every one, a read hands those it was asked for.
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : source->GetQueryColumns())
		if (col != nullptr && std::any_of(rows.Columns().begin(), rows.Columns().end(),
		                                  [&col](const ibQueryRamColumn& held) { return held.m_id == col->GetColumnId(); }))
			columns.push_back(col);
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys;
	for (const std::shared_ptr<const ibBackendQueryColumn>& key : source->GetPrimaryKeyColumns())
		if (std::find(columns.begin(), columns.end(), key) != columns.end())
			keys.push_back(key);
	static std::atomic<unsigned long> s_seq{ 0 };
	return Materialise(holder, ibString::Format(ibT("oes_copy_%lu"), ++s_seq), std::move(columns), std::move(keys), rows,
		source->GetMetaData());
}

std::unique_ptr<ibTempTableManager> ibTempTableManager::Materialise(ibDatabaseConnectionHolder* holder,
                                                                    const std::shared_ptr<const ibBackendQueryable>& table,
                                                                    const std::vector<ibValue>& keys,
                                                                    ibDatabaseConnectionHolder* from)
{
	if (table == nullptr)
		return nullptr;
	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns = table->GetQueryColumns();
	ibQueryRamTable rows;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns)
		rows.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());
	if (!keys.empty()) {
		// The rows the keys name, each column by its own read — the batch that says what references are reads the same way.
		ibDataQueryBuilder read(from);
		read.From(table);
		read.WhereKeyIn(keys);
		for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns)
			read.Select(col, ibEmptyString);
		ibReadPageRequest page;
		page.m_count = 0;   // every row named
		ibDataQueryResult found = read.Execute(page);
		while (found.Next()) {
			const long row = rows.AppendRow();
			for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns)
				rows.SetCell(row, col->GetColumnId(), found.GetValue(col));
		}
	}
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> keyColumns;
	for (const std::shared_ptr<const ibBackendQueryColumn>& key : table->GetPrimaryKeyColumns())
		if (std::find(columns.begin(), columns.end(), key) != columns.end())
			keyColumns.push_back(key);
	return Materialise(holder, table->GetQueryTableName(), columns, std::move(keyColumns), rows, table->GetMetaData());
}

std::unique_ptr<ibTempTableManager> ibTempTableManager::Materialise(ibDatabaseConnectionHolder* holder, const ibString& tableName,
                                                                    std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns,
                                                                    std::vector<std::shared_ptr<const ibBackendQueryColumn>> keys,
                                                                    const ibQueryRamTable& rows, const ibMetaData* metaData)
{
	// ⭐ ONE FIELD, ONE COLUMN — here, where every copy is laid out. A column laid over fields others already give — a
	// document's point in time is its date and its reference, a journal's document type its reference's type — is read
	// off them in the copy as in the table; written again, the copy held each of those fields twice, and a walk into it
	// was refused as ambiguous ("Column 'fld1006_RTRef' is ambiguous", 2026-11-19). Said once, on the copy of a table
	// only, the rows of a source computed in memory went past it ("Column 'Ref_RTRef' is ambiguous", a journal of no
	// document, 2026-11-24).
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> laid;
	std::vector<ibString> given;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns) {
		const std::vector<ibString> fields = ColumnFieldNames(*col);
		if (!fields.empty() && std::all_of(fields.begin(), fields.end(),
				[&given](const ibString& f) { return std::find(given.begin(), given.end(), f) != given.end(); }))
			continue;
		given.insert(given.end(), fields.begin(), fields.end());
		laid.push_back(col);
	}
	columns = std::move(laid);
	std::erase_if(keys, [&columns](const std::shared_ptr<const ibBackendQueryColumn>& key) {
		return std::find(columns.begin(), columns.end(), key) == columns.end();
	});
	if (columns.empty())
		return nullptr;

	// Pin the holder's connection for the table's whole life — the temp table lives on THIS connection
	// and every later statement (fill / join / drop) must land on it.
	ibConnectionScope scope(holder);
	if (!scope)
		return nullptr;   // no connection (pool passive / saturated) -> RAM

	// Capability = the L1 temp dialect's PRESENCE, read here as facts rather than through CanMaterialise:
	// the CREATE's lexical bits come from them two lines down. (CanMaterialise exists for the callers that
	// must know BEFORE computing their rows; this is the insurance for one that did not ask.) nullptr =>
	// the driver has no DB temp tables (FB) => the caller takes its road in memory.
	const ibTempTableDialect* dialect = scope->GetTempTableDialect();
	if (dialect == nullptr)
		return nullptr;
	if (!ProbeTempCapability(holder, scope.shared(), *dialect))
		return nullptr;   // dialect present but this connection can't create temp tables (no DDL right / read-only) -> RAM
	const bool interprets = scope->GetDialect().m_features.m_inMemory;

	// That flattened slot list drives BOTH the CREATE (name + type-by-role) and the INSERT column order,
	// so the temp mirrors a real table from a single source.
	std::vector<ibColumnSlot> slots;      // flattened spread over all columns
	std::vector<int>          fieldsOf;   // …how many of them each column binds
	std::vector<ibDdlColumn>  ddlCols;    // …and what each is created as
	fieldsOf.reserve(columns.size());
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns) {
		std::vector<ibColumnSlot> layout = DescribeColumnLayout(*col);
		fieldsOf.push_back(static_cast<int>(layout.size()));
		for (ibColumnSlot& s : layout) {
			// A raw column's field is created as it is declared, as a real table creates it: a key binds its sixteen
			// bytes (ibBackendColumnRawDB::BindValue), which the INTEGER of a field of no declared type would refuse.
			ibDdlColumn dc;
			dc.m_name = s.m_name;
			dc.m_type = s.m_role == ibColumnRole::Raw && col->AsRawColumn() != nullptr ? s.m_type : WidenForTemp(s.m_role);
			ddlCols.push_back(std::move(dc));
			slots.push_back(std::move(s));
		}
	}

	std::vector<ibString> allFields;   // slot names — the INSERT column order
	allFields.reserve(slots.size());
	for (const ibColumnSlot& s : slots)
		allFields.push_back(s.m_name);

	// CREATE + fill on the pinned connection (nested ibDatabaseQueryBuilder over the same holder
	// inherits this scope's connection). Any runtime failure, or a row the database refused -> drop the
	// half-built table, return null (graceful RAM fallback — the fast path is opportunistic, correctness always lands).
	bool               filled = false;
	std::exception_ptr failure;
	try {
		ibDatabaseQueryBuilder qCreate(holder);
		qCreate.Execute(ibCreateTempTable(tableName, ddlCols, dialect->m_createPrefix, dialect->m_onCommitClause));

		// ⭐ THE ROWS GO AS A REGISTER'S ROWS GO (ibBatchInsert): each column binds its value into its own fields — a
		// raw key as its bytes, a moment as its halves — and the batch carries them as the base takes rows: into its
		// cells for a base in memory, with no IR constant made for a value. As multi-row INSERTs of them, a capture
		// and a constant for each field of each row, the copy was three quarters of a turnover by correspondence read in
		// memory (sampled 2026-11-18, 132 402 rows, Debug).
		ibBatchInsert batch(tableName, allFields, holder);
		const long total   = rows.RowCount();
		bool       refused = false;
		for (long r = 0; r < total && !refused; ++r) {
			int position = 1;
			for (std::size_t c = 0; c < columns.size(); ++c) {
				const int first = position;
				columns[c]->BindValue(batch, metaData, rows.GetCell(r, columns[c]->GetColumnId()), position);
				// ONE VALUE PER FIELD, EXACTLY — the bind is positional: a column that binds another number of values
				// than it declares fields moves every value after it into somebody else's field (the register writes
				// keep the same rule, dbTableProvider.cpp).
				if (position - first != fieldsOf[c])
					ibBackendCoreException::Error(
						_("A batched write captured %d values for a column declaring %d fields"), position - first, fieldsOf[c]);
			}
			refused = batch.RunQuery() < 0;
		}
		filled = !refused && batch.Flush() >= 0;
	}
	catch (...) {
		failure = std::current_exception();
	}
	if (!filled) {
		try { ibDatabaseQueryBuilder qDrop(holder); qDrop.Execute(ibDropTable(tableName, /*ifExists*/ true)); }
		catch (...) {}
		// A base in memory has no incapability to fall back from: what failed there is said as it is.
		if (interprets && failure)
			std::rethrow_exception(failure);
		return nullptr;
	}

	// Refresh optimiser statistics on the freshly-filled temp (ANALYZE) so the DBMS plans any
	// JOIN against the temp's REAL cardinality, not a default estimate — the actual optimisation
	// the materialisation exists to enable (docs/private/temp-db.md). Goes through the L2 door as a
	// first-class statement (ibDdlKind::Analyze, dialect-rendered) so it is reusable beyond temp
	// and carries NO per-driver fork here; a driver with no ANALYZE renders empty and no-ops. The
	// nested builder inherits this scope's pinned connection (the temp lives on it). Best-effort:
	// a failure leaves default estimates — the temp is still correct, only the plan is less informed.
	try { ibDatabaseQueryBuilder qAnalyze(holder); qAnalyze.Execute(ibAnalyzeTable(tableName)); }
	catch (...) {}

	const auto queryable = std::make_shared<ibDbTempTableQueryable>(tableName, std::move(columns), std::move(keys), metaData);
	return std::unique_ptr<ibTempTableManager>(
		new ibTempTableManager(std::move(scope), holder, *dialect, tableName, queryable));
}

ibTempTableManager::ibTempTableManager(ibConnectionScope scope, ibDatabaseConnectionHolder* holder,
                                       ibTempTableDialect dialect, ibString tableName,
                                       const std::shared_ptr<const ibDbTempTableQueryable>& queryable)
	: m_scope(std::move(scope))
	, m_holder(holder)
	, m_dialect(std::move(dialect))
	, m_tableName(std::move(tableName))
	, m_queryable(queryable)
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
