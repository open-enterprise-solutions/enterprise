#ifndef _FRMCLIENT_BACKEND_QUERY_KEYWORDS_H__
#define _FRMCLIENT_BACKEND_QUERY_KEYWORDS_H__

// THE QUERY LANGUAGE'S WORDS — the engine's keyword set (backend/query/queryKeywords.h): the lexer's and the parser's,
// which are the server's. What the client's windows name of it is the keyword a lowering answers with and its spelling;
// no answer is ever given here (ibQueryLowering), so none is spelled.

#include <wx/string.h>

enum class ibQueryKeyword;

inline const wxString& ibQueryKeywordText(ibQueryKeyword WXUNUSED(kw))
{
	static const wxString none;
	return none;
}

#endif
