#ifndef __QUERY_DEBUG_H__
#define __QUERY_DEBUG_H__

////////////////////////////////////////////////////////////////////////////
//	Description : WHAT A QUERY MEASUREMENT MEASURED - the queries, and the statements the database got
////////////////////////////////////////////////////////////////////////////
//
// ONE STRUCTURE FOR BOTH SIDES (Max, 2026-11-14: *"it lives in the core"*). The engine's query measurement
// (backend/query/queryProfiler.h) answers with it, the debugger carries it under a profiler's line (ibProfileData), and
// the studio and an assistant read it as it came — nobody keeps a copy of its shape.
//
// The queries of the query language that ran and, under each, the statements it sent the database; and the
// statements sent past the language — an object written or deleted, the platform's own reads. Times are microseconds.

#include <cstdint>
#include <vector>

#include "core/memory/memoryStream.h"   // ibWriterMemory / ibReaderMemory — the wire format below

struct ibQueryDebugData {

	// A statement as the database got it — its text, the plan the database chose for it (empty: the driver does not tell
	// it), which database, how often it was sent, the rows read back or changed, and the database's time running it,
	// preparing included, and handing the rows over. …And its slowest run, with the values it was sent with, each as
	// SQL writes it ('text', 42, NULL…), so that run can be sent again (Max, 2026-11-15): none for a text that takes none.
	struct ibStatement {
		ibString              m_text;
		ibString              m_plan;
		ibString              m_dbms;
		std::uint64_t         m_count = 0;
		std::uint64_t         m_rows = 0;
		std::int64_t          m_run = 0;
		std::int64_t          m_fetch = 0;
		std::int64_t          m_slowest = 0;
		std::vector<ibString> m_values;
	};

	// A query of the language — what it reads from (FROM and JOIN), how often it ran, its whole time, and of it the
	// database's: its statements run, and the rows read while it ran. The rest is the engine's own, in memory.
	struct ibQuery {
		ibString                 m_source;
		std::uint64_t            m_count = 0;
		std::int64_t             m_total = 0;
		std::int64_t             m_database = 0;
		std::vector<ibStatement> m_statements;
	};

	std::vector<ibQuery>     m_queries;      // by their time descending
	std::vector<ibStatement> m_statements;   // …and the statements of no query, likewise

	bool IsEmpty() const { return m_queries.empty() && m_statements.empty(); }

	// The wire format lives with the struct, the writer above its reader.
	void Write(ibWriterMemory& to) const {
		const auto writeStatements = [&to](const std::vector<ibStatement>& statements) {
			to.w_u32(static_cast<std::uint32_t>(statements.size()));
			for (const ibStatement& statement : statements) {
				to.w_stringZ(statement.m_text);
				to.w_stringZ(statement.m_plan);
				to.w_stringZ(statement.m_dbms);
				to.w_u64(statement.m_count);
				to.w_u64(statement.m_rows);
				to.w_s64(statement.m_run);
				to.w_s64(statement.m_fetch);
				to.w_s64(statement.m_slowest);
				to.w_u32(static_cast<std::uint32_t>(statement.m_values.size()));
				for (const ibString& value : statement.m_values)
					to.w_stringZ(value);
			}
		};
		to.w_u32(static_cast<std::uint32_t>(m_queries.size()));
		for (const ibQuery& query : m_queries) {
			to.w_stringZ(query.m_source);
			to.w_u64(query.m_count);
			to.w_s64(query.m_total);
			to.w_s64(query.m_database);
			writeStatements(query.m_statements);
		}
		writeStatements(m_statements);
	}

	void Read(const ibReaderMemory& from) {
		const auto readStatements = [&from](std::vector<ibStatement>& statements) {
			statements.resize(from.r_u32());
			for (ibStatement& statement : statements) {
				from.r_stringZ(statement.m_text);
				from.r_stringZ(statement.m_plan);
				from.r_stringZ(statement.m_dbms);
				statement.m_count = from.r_u64();
				statement.m_rows  = from.r_u64();
				statement.m_run     = from.r_s64();
				statement.m_fetch   = from.r_s64();
				statement.m_slowest = from.r_s64();
				statement.m_values.resize(from.r_u32());
				for (ibString& value : statement.m_values)
					from.r_stringZ(value);
			}
		};
		m_queries.resize(from.r_u32());
		for (ibQuery& query : m_queries) {
			from.r_stringZ(query.m_source);
			query.m_count    = from.r_u64();
			query.m_total    = from.r_s64();
			query.m_database = from.r_s64();
			readStatements(query.m_statements);
		}
		readStatements(m_statements);
	}
};

#endif
