#include "core/stringTokenizer.h"

// Read off wx's own (src/common/tokenzr.cpp, arrstr.cpp), so a token list comes out the same as it did.

ibStringTokenizer::ibStringTokenizer(const ibString& str, const ibString& delims, ibStringTokenizerMode mode)
{
	SetString(str, delims, mode);
}

void ibStringTokenizer::SetString(const ibString& str, const ibString& delims, ibStringTokenizerMode mode)
{
	if (mode == ibTOKEN_DEFAULT) {
		// white space only: strtok; anything else: an empty token between two delimiters is a token
		mode = ibTOKEN_STRTOK;
		for (const ibChar c : delims) {
			if (!ibIsSpace(c)) {
				mode = ibTOKEN_RET_EMPTY;
				break;
			}
		}
	}
	m_delims = delims;
	m_mode = mode;
	Reinit(str);
}

void ibStringTokenizer::Reinit(const ibString& str)
{
	m_string = str;
	m_pos = 0;
	m_lastDelim = 0;
}

bool ibStringTokenizer::HasMoreTokens() const
{
	if (m_mode == ibTOKEN_INVALID)
		return false;
	if (m_string.find_first_not_of(m_delims, m_pos) != ibString::npos)
		return true;
	switch (m_mode) {
	case ibTOKEN_RET_EMPTY:
	case ibTOKEN_RET_DELIMS:
		// the first empty token, even when only delimiters follow it
		return !m_string.IsEmpty() && m_pos == 0;
	case ibTOKEN_RET_EMPTY_ALL:
		// the empty one after the last delimiter: m_lastDelim is 0 once a token ran to the end
		return m_pos < m_string.Len() || m_lastDelim != 0;
	default:
		return false;
	}
}

ibString ibStringTokenizer::GetNextToken()
{
	ibString token;
	do {
		if (!HasMoreTokens())
			break;
		const size_t end = m_string.find_first_of(m_delims, m_pos);
		if (end == ibString::npos) {
			token = m_string.Mid(m_pos);
			m_pos = m_string.Len();
			m_lastDelim = 0;
		}
		else {
			token = m_string.Mid(m_pos, end - m_pos + (m_mode == ibTOKEN_RET_DELIMS ? 1 : 0));
			m_pos = end + 1;
			m_lastDelim = m_string[end];
		}
	} while (!AllowEmpty() && token.IsEmpty());
	return token;
}

size_t ibStringTokenizer::CountTokens() const
{
	ibStringTokenizer copy(*this);
	size_t count = 0;
	while (copy.HasMoreTokens()) {
		copy.GetNextToken();
		++count;
	}
	return count;
}

std::vector<ibString> ibStringTokenize(const ibString& str, const ibString& delims, ibStringTokenizerMode mode)
{
	std::vector<ibString> tokens;
	ibStringTokenizer tokenizer(str, delims, mode);
	while (tokenizer.HasMoreTokens())
		tokens.push_back(tokenizer.GetNextToken());
	return tokens;
}

std::vector<ibString> ibSplit(const ibString& str, ibChar sep, ibChar escape)
{
	if (escape == 0)
		return ibStringTokenize(str, ibString(sep), ibTOKEN_RET_EMPTY_ALL);

	std::vector<ibString> pieces;
	ibString piece;
	ibChar prev = 0;
	for (const ibChar c : str) {
		if (c == sep) {
			if (prev == escape)
				piece[piece.Len() - 1] = sep;   // the escape goes, the separator stays a character
			else {
				pieces.push_back(piece);
				piece.clear();
			}
		}
		else
			piece += c;
		prev = c;
	}
	// the last piece, which there always is unless the text is empty
	if (!piece.IsEmpty() || prev == sep)
		pieces.push_back(piece);
	return pieces;
}

ibString ibJoin(const std::vector<ibString>& pieces, ibChar sep, ibChar escape)
{
	ibString text;
	for (size_t n = 0; n < pieces.size(); ++n) {
		if (n != 0) {
			// an escape ending the text would escape the separator: it is doubled, so it stays a character
			if (escape != 0 && !text.IsEmpty() && text.Last() == escape)
				text += escape;
			text += sep;
		}
		if (escape == 0) {
			text += pieces[n];
			continue;
		}
		for (const ibChar c : pieces[n]) {
			if (c == sep)
				text += escape;
			text += c;
		}
	}
	return text;
}
