#include "databaseMemoryBase.h"

#include "backend/appHost.h"   // ibApplicationHost::GetQueryMemoryManager — the process's, which a space is taken from
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <limits>

// ⭐ THE FEATURES ARE WHAT THE INTERPRETER DOES — a query is shaped by them before it is built (ibCanPushRollup,
// ibCanPushWindow, …), so a feature declared and not done is a query refused at run time, and one done and not declared
// is a road never taken. Nothing here is spelled: no text is written for this base.
const ibDialectDictionary& ibDatabaseLayerMemory::Dialect()
{
	static const ibDialectDictionary s_dialect = [] {
		ibDialectDictionary d;
		d.m_features.m_window            = true;
		d.m_features.m_cte               = true;
		d.m_features.m_recursiveCte      = true;
		d.m_features.m_recursiveCteUnion = true;
		d.m_features.m_fullOuterJoin     = true;
		d.m_features.m_rollup            = true;
		d.m_features.m_grouping          = true;
		d.m_features.m_multiRowValues    = true;
		d.m_features.m_inMemory          = true;
		// m_antiJoin stays false: NOT EXISTS is asked per outer row here, which is what the flag says it is not.
		return d;
	}();
	return s_dialect;
}

const ibTempTableDialect& ibDatabaseLayerMemory::TempDialect()
{
	static const ibTempTableDialect s_temp = [] {
		ibTempTableDialect t;
		t.m_strategy  = ibTempTableDialect::Strategy::AdHocCreate;
		t.m_autoDrops = false;   // the manager drops it through its pinning scope, as on SQLite
		return t;
	}();
	return s_temp;
}

ibDatabaseLayerMemory::ibDatabaseLayerMemory()
	: ibDatabaseLayer()
	, m_interpreter(std::make_unique<ibQueryInterpreter>(*this))
	, m_open(true)
{
}

ibDatabaseLayerMemory::~ibDatabaseLayerMemory() = default;

bool ibDatabaseLayerMemory::Open(const ibString&)
{
	m_open = true;
	return true;
}

bool ibDatabaseLayerMemory::Close()
{
	m_open = false;
	return true;
}

bool ibDatabaseLayerMemory::IsOpen()
{
	return m_open;
}

void ibDatabaseLayerMemory::Cancel()
{
	m_cancelled = true;
}

ibDatabaseLayer* ibDatabaseLayerMemory::Clone()
{
	return new ibDatabaseLayerMemory();
}

bool ibDatabaseLayerMemory::TableExists(const ibString& table)
{
	std::lock_guard<std::mutex> lock(m_tables.m_mutex);
	return m_tables.m_byName.find(table) != m_tables.m_byName.end();
}

bool ibDatabaseLayerMemory::ViewExists(const ibString&)
{
	return false;
}

std::vector<ibString> ibDatabaseLayerMemory::GetTables()
{
	std::lock_guard<std::mutex> lock(m_tables.m_mutex);
	std::vector<ibString> names;
	names.reserve(m_tables.m_byName.size());
	for (const auto& table : m_tables.m_byName)
		names.push_back(table.first);
	return names;
}

std::vector<ibString> ibDatabaseLayerMemory::GetViews()
{
	return {};
}

std::vector<ibString> ibDatabaseLayerMemory::GetColumns(const ibString& table)
{
	std::lock_guard<std::mutex> lock(m_tables.m_mutex);
	const ibTableMap::const_iterator found = m_tables.m_byName.find(table);
	return found != m_tables.m_byName.end() ? found->second.m_fields : std::vector<ibString>();
}

const ibDialectDictionary& ibDatabaseLayerMemory::GetDialect() const
{
	return Dialect();
}

const ibTempTableDialect* ibDatabaseLayerMemory::GetTempTableDialect() const
{
	return &TempDialect();
}

ibQueryInterpreter* ibDatabaseLayerMemory::GetQueryInterpreter()
{
	return m_interpreter.get();
}

// ⭐ SQL TEXT IS REFUSED IN WORDS — every road that writes a statement as text (a module's own SQL, a trigger, the
// schema builder's DDL) has no meaning for a base that is rows in a process, and an answer of "nothing happened" would
// be a write that silently did not take place.
[[noreturn]] static void RefuseText(const ibString& text)
{
	ibDatabaseLayerException::Throw(ibBackendDatabaseException::Kind::Syntax, DATABASE_LAYER_UNSUPPORTED_OPERATION,
		ibEmptyString, ibString::Format(_("A base in memory reads no SQL text: a statement reaches it as the query layer builds it.\nThe text was: %s"), text));
}

int ibDatabaseLayerMemory::DoRunQuery(const ibString& strQuery, bool)
{
	RefuseText(strQuery);
}

ibDatabaseResultSet* ibDatabaseLayerMemory::DoRunQueryWithResults(const ibString& strQuery)
{
	RefuseText(strQuery);
}

ibPreparedStatement* ibDatabaseLayerMemory::DoPrepareStatement(const ibString& strQuery)
{
	RefuseText(strQuery);
}

void ibDatabaseLayerMemory::DoBeginTransaction(const ibTxOptions&)
{
	std::lock_guard<std::mutex> lock(m_tables.m_mutex);
	m_snapshot = std::make_unique<ibTableMap>(m_tables.m_byName);
}

void ibDatabaseLayerMemory::DoCommit()
{
	m_snapshot.reset();
}

void ibDatabaseLayerMemory::DoRollBack()
{
	std::lock_guard<std::mutex> lock(m_tables.m_mutex);
	if (m_snapshot)
		m_tables.m_byName = std::move(*m_snapshot);
	m_snapshot.reset();
}

// ==========================================================================
// ibQueryMemoryManager
// ==========================================================================

namespace {

// The pool, given its first connection, and the seed's holder over it — before the seed's scope binds that connection.
ibDatabaseConnectionHolder* SeededOver(ibConnectionPool& pool, ibSingleConnectionHolder& holder)
{
	pool.Init(std::make_shared<ibDatabaseLayerMemory>(), std::numeric_limits<std::size_t>::max(), /*minIdle=*/0);
	holder.SetPool(&pool);
	return &holder;
}

} // namespace

ibQueryMemoryManager::ibQueryMemoryManager(ib::AppDataCtorToken owner)
	: m_pool(std::make_unique<ibConnectionPool>(owner))
	, m_seed(SeededOver(*m_pool, m_seedHolder))
{
}

ibQueryMemoryManager::~ibQueryMemoryManager() = default;

// ==========================================================================
// ibMemorySpace
// ==========================================================================

namespace {

// The holder over the process's queries' memory — set before the space's scope takes its connection through it.
ibDatabaseConnectionHolder* OverTheQueryMemoryManager(ibSingleConnectionHolder& holder)
{
	holder.SetPool(ibApplicationHost::GetQueryMemoryManager().GetConnectionPool());
	return &holder;
}

} // namespace

ibMemorySpace::ibMemorySpace()
	: m_scope(OverTheQueryMemoryManager(m_holder))
{
}

// ⭐ THE SPACE GOES WITH THE EXECUTION, whatever its copies left — one that failed half-made, or all of them at the wall
// of memory, where dropping them would itself ask for memory: the pool forgets it, and the next execution is given a
// space of its own, empty because it is new.
ibMemorySpace::~ibMemorySpace()
{
	m_scope.Discard();
}
