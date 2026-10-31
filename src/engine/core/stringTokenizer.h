#ifndef __CORE_STRING_TOKENIZER_H__
#define __CORE_STRING_TOKENIZER_H__

#include "core/core.h"
#include "core/fstring.h"

#include <vector>

// wx's TOKENIZER, SPLIT AND JOIN on ibString — wxStringTokenizer, wxStringTokenize, wxSplit, wxJoin, with their modes
// and their answers: a call site keeps its shape and changes the names.

// The delimiters a tokenizer uses when it is given none: wx's wxDEFAULT_DELIMITERS.
inline constexpr const ibChar* ibDEFAULT_DELIMITERS = ibT(" \t\r\n");

enum ibStringTokenizerMode {
	ibTOKEN_INVALID = -1,
	ibTOKEN_DEFAULT,         // ibTOKEN_STRTOK when the delimiters are all white space, else ibTOKEN_RET_EMPTY
	ibTOKEN_RET_EMPTY,       // an empty token between two delimiters, none after the last one
	ibTOKEN_RET_EMPTY_ALL,   // …and the empty one after the last delimiter too
	ibTOKEN_RET_DELIMS,      // as ibTOKEN_RET_EMPTY, each token with the delimiter that ended it
	ibTOKEN_STRTOK           // no empty tokens at all, as strtok
};

class CORE_API ibStringTokenizer {
public:
	ibStringTokenizer() = default;
	ibStringTokenizer(const ibString& str, const ibString& delims = ibDEFAULT_DELIMITERS,
		ibStringTokenizerMode mode = ibTOKEN_DEFAULT);

	void SetString(const ibString& str, const ibString& delims = ibDEFAULT_DELIMITERS,
		ibStringTokenizerMode mode = ibTOKEN_DEFAULT);
	void Reinit(const ibString& str);

	bool HasMoreTokens() const;
	ibString GetNextToken();

	size_t CountTokens() const;
	ibString GetString() const { return m_string.Mid(m_pos); }   // what is left
	size_t GetPosition() const { return m_pos; }
	ibChar GetLastDelimiter() const { return m_lastDelim; }

private:
	bool AllowEmpty() const { return m_mode != ibTOKEN_STRTOK; }

	ibString              m_string;
	ibString              m_delims;
	ibStringTokenizerMode m_mode = ibTOKEN_INVALID;
	size_t                m_pos = 0;
	ibChar                m_lastDelim = 0;   // the delimiter that ended the last token; 0 when it ran to the end
};

// Every token at once.
CORE_API std::vector<ibString> ibStringTokenize(const ibString& str, const ibString& delims = ibDEFAULT_DELIMITERS,
	ibStringTokenizerMode mode = ibTOKEN_DEFAULT);

// One separator, an empty piece kept wherever two meet; a separator after `escape` is a character of the piece.
// `escape` 0: no escaping, every separator splits.
CORE_API std::vector<ibString> ibSplit(const ibString& str, ibChar sep, ibChar escape = ibT('\\'));

// The pieces with `sep` between them, a separator inside a piece escaped — so ibSplit gives them back.
CORE_API ibString ibJoin(const std::vector<ibString>& pieces, ibChar sep, ibChar escape = ibT('\\'));

#endif // __CORE_STRING_TOKENIZER_H__
