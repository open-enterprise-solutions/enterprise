#ifndef OES_TESTS_QUERY_ROAD_H
#define OES_TESTS_QUERY_ROAD_H

// ⭐ THE ROAD A QUERY TOOK, AS A TEST STATES IT. A shape the server must answer — a fold, a join, a stack of branches,
// a filter — is told by what crossed the wire: the statements sent and the rows they read back. On the server's road
// the rows are the answer's (the groups, the joined rows, the survivors); fallen to memory they are every row of every
// source, and a stack of branches is a statement per branch. The journal's `query.road` line says it for a person, and
// a release build compiles it out (ibJournalInfo under NDEBUG); this is what a test holds, in any build.
//
// Measured by the query measurement the profiler is made of (backend/query/queryProfiler.h) — the instrument a person
// reads in the studio, not a second one.

#include <cstdint>
#include <ostream>
#include <utility>
#include <vector>

#include "backend/query/queryProfiler.h"

// What a run sent to the database.
struct SentToDatabase {
	std::uint64_t         m_statements = 0;   // runs sent: every text as often as it went
	std::uint64_t         m_rows = 0;         // rows they read back — or changed, for a write
	std::vector<ibString> m_texts;            // each text once
};

// `run` under a measurement of its own. A cursor tells its rows when it is read to its end or let go, so what `run`
// reads is read through or let go inside it.
template <class Run>
SentToDatabase MeasureSent(Run&& run)
{
	ibQueryProfiler measured;
	std::forward<Run>(run)();
	measured.Stop();

	SentToDatabase sent;
	const auto add = [&sent](const ibDatabaseProfiler::ibSums& statements) {
		for (const auto& [text, sum] : statements) {
			sent.m_statements += sum.m_count;
			sent.m_rows       += sum.m_rows;
			sent.m_texts.push_back(text);
		}
	};
	for (const auto& [place, sums] : measured.Places()) {
		for (const auto& [source, query] : sums.m_queries)
			add(query.m_statements);
		add(sums.m_statements);
	}
	return sent;
}

// …and said in a failed assertion: the counts, then the texts.
inline std::ostream& operator<<(std::ostream& out, const SentToDatabase& sent)
{
	out << sent.m_statements << " statement(s), " << sent.m_rows << " row(s)";
	for (const ibString& text : sent.m_texts)
		out << "\n  " << text;
	return out;
}

#endif // OES_TESTS_QUERY_ROAD_H
