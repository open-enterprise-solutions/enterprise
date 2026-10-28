#include "databaseQueryParser.h"

bool IsEmptyQuery(const ibString& strQuery)
{
	if (strQuery.IsEmpty())
		return false;

	for (auto& c : strQuery) {

		// Remove all query delimiting semi-colons
		if (c != ibT(' ') &&
			c != ibT(';'))
			return false;
	}

	return true;
}

std::vector<ibString> ParseQueries(const ibString& strQuery)
{
	// thread_local instead of a lock: each thread reuses its own buffer
	// allocations across calls, so there's nothing shared to serialise
	// (was a wxCriticalSection over shared statics). bInQuote is a plain
	// local now — it's per-parse state, and the old static leaked it
	// across calls on an unbalanced quote.
	thread_local std::vector<ibString> arrString; arrString.clear();
	thread_local ibString strRaw; strRaw.Clear();
	bool bInQuote = false;

	for (const auto& c : strQuery) {

		strRaw += c;

		if (c == ibT('\'')) {
			bInQuote = !bInQuote;
		}
		else if (c == ibT(';') && !bInQuote)
		{
			if (!IsEmptyQuery(strRaw))
				arrString.push_back(strRaw);

			strRaw.Clear();
		}
	}

	if (!strRaw.IsEmpty()) {

		thread_local ibString str; str.Clear();
		str << strRaw << ibT(";");
		if (!IsEmptyQuery(str))
			arrString.push_back(str);

		strRaw.Clear();
	}

	return arrString;
}

