#include "databaseQueryParser.h"

bool IsEmptyQuery(const wxString& strQuery)
{
	if (strQuery.IsEmpty())
		return false;

	for (auto& c : strQuery) {

		// Remove all query delimiting semi-colons
		if (c != wxT(' ') &&
			c != wxT(';'))
			return false;
	}

	return true;
}

wxArrayString ParseQueries(const wxString& strQuery)
{
	// thread_local instead of a lock: each thread reuses its own buffer
	// allocations across calls, so there's nothing shared to serialise
	// (was a wxCriticalSection over shared statics). bInQuote is a plain
	// local now — it's per-parse state, and the old static leaked it
	// across calls on an unbalanced quote.
	thread_local wxArrayString arrString; arrString.Empty();
	thread_local wxString strRaw; strRaw.Clear();
	bool bInQuote = false;

	for (const auto& c : strQuery) {

		strRaw += c;

		if (c == wxT('\'')) {
			bInQuote = !bInQuote;
		}
		else if (c == wxT(';') && !bInQuote)
		{
			if (!IsEmptyQuery(strRaw))
				arrString.Add(strRaw);

			strRaw.Clear();
		}
	}

	if (!strRaw.IsEmpty()) {

		thread_local wxString str; str.Clear();
		str << strRaw << wxT(";");
		if (!IsEmptyQuery(str))
			arrString.Add(str);

		strRaw.Clear();
	}

	return arrString;
}

