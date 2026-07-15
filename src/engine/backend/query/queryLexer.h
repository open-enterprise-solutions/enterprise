#ifndef __QUERY_LEXER_H__
#define __QUERY_LEXER_H__

// L4-1 — text query language lexer.
//
// REUSES ibTranslateCode's UTF-8-aware character primitives (SkipSpaces /
// GetWord / GetNumber / GetString / GetDate / GetByte) — "a tokenizer on the OES
// lexer idioms" — but classifies words against the QUERY keyword table
// (queryKeywords.h), not the script one. Because the primitives are already
// UTF-8 / Unicode aware, Cyrillic metaobject / attribute identifiers
// (Goods, Warehouse) tokenize for free.
//
// On a malformed literal the primitives signal through SetError -> DoSetError
// (whose base body is a no-op the script path overrides); we override DoSetError
// to THROW ibBackendCoreException with line / position, so a lex error aborts
// loudly. Throw-by-value, catch-by-const-ref.
//
// See docs/query-language-arc.md §14 / §23.

#include "backend/compiler/translateCode.h"   // ibTranslateCode (char primitives) + ibValue
#include "queryKeywords.h"

#include <vector>

// What a token IS. (Op = a comparison / arithmetic operator carrying its text;
// Punct = a structural delimiter — comma / paren / dot / star.)
enum class ibQueryTokenKind
{
	Keyword, Ident, Number, String, Date, Param, Op, Punct, End
};

// One lexer token. Identifiers carry their ORIGINAL case in m_text (metaobject /
// attribute name resolution is case-insensitive but the original is preserved);
// literals carry a ready ibValue in m_literal.
struct ibQueryToken
{
	ibQueryTokenKind m_kind    = ibQueryTokenKind::End;
	ibQueryKeyword   m_keyword = ibQueryKeyword::None;   // kind == Keyword
	wxString         m_text;        // Ident / Param name (original case); Op / Punct text
	ibValue          m_literal;     // Number / String / Date constant
	unsigned int     m_line    = 0; // 1-based source line (diagnostics)
	unsigned int     m_col     = 0; // character offset of the token start (diagnostics)

	bool IsKeyword(ibQueryKeyword kw) const {
		return m_kind == ibQueryTokenKind::Keyword && m_keyword == kw;
	}
	bool IsPunct(wxChar c) const {
		return m_kind == ibQueryTokenKind::Punct && m_text.length() == 1 && m_text[0] == c;
	}
	bool IsOp(const wxChar* op) const {
		return m_kind == ibQueryTokenKind::Op && m_text == op;
	}
	bool IsEnd() const { return m_kind == ibQueryTokenKind::End; }
};

class BACKEND_API ibQueryLexer : protected ibTranslateCode
{
public:
	ibQueryLexer() = default;

	// Tokenize the whole query text into a token vector terminated by one
	// End token. Throws ibBackendCoreException (line / position) on a lex error.
	std::vector<ibQueryToken> Tokenize(const wxString& queryText);

protected:
	// The base body is a no-op (the script compiler overrides it). We THROW so a
	// malformed numeric / string / date literal does not silently return false.
	void DoSetError(int codeError,
		const wxString& strFileName, const wxString& strModuleName, const wxString& strDocPath,
		unsigned int currPos, unsigned int currLine,
		const wxString& errorDesc = wxEmptyString) const override;

private:
	// Immediate next char WITHOUT skipping whitespace — so multi-char operators
	// (<=, >=, <>, !=) only glue when contiguous. 0 at end of buffer.
	wxChar PeekRawByte() const;
};

#endif
