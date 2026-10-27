////////////////////////////////////////////////////////////////////////////
//	L4-1 — text query language lexer (queryLexer.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLexer.h"

#include "core/exception.h"       // ibCoreException — a lexer refusal is still one
#include "core/query/queryRefusal.h"   // ibQueryRefusal — the text does not read: where and why, as data

#include "core/localization/dictionary.h"   // _() — the refusals' words

// The keyword and scalar-call tables the lexer matches against are the core's (core/query/queryKeywords.cpp):
// the thin client's editor reads the same words.

//////////////////////////////////////////////////////////////////////
// ibQueryLexer
//////////////////////////////////////////////////////////////////////

void ibQueryLexer::RaiseRefusal() const
{
	if (GetRefusalCount() == 0)
		return;

	const ibTranslateRefusal& refusal = GetRefusalAt(0);
	const unsigned int currPos = refusal.m_currPos;
	const unsigned int currLine = refusal.m_currLine;
	const ibString& errorDesc = refusal.m_errorDesc;

	// L4 refuses in its OWN variety, carrying the position as data — the text is what is wrong here,
	// not the machinery, and the caller that shows this is the one editing the query.
	if (errorDesc.empty())
		throw ibQueryRefusal(currPos, currLine,
			ibString::Format(_("Query lexical error at line %u (position %u)"), currLine, currPos));
	else
		throw ibQueryRefusal(currPos, currLine,
			ibString::Format(_("Query lexical error at line %u (position %u): %s"), currLine, currPos, errorDesc));
}

ibChar ibQueryLexer::PeekRawByte() const
{
	const unsigned int pos = GetCurrentPos();
	if (pos < GetBufferSize())
		return m_strBuffer[pos];
	return ibChar(0);
}

// THE LEXER'S OWN ANSWER, not a second opinion about it. Anything that lexes as one identifier and
// nothing else is a name the language can carry; everything else — a space, a dot, a bracket, a
// keyword, an empty string — is not. A lex error is simply "no": the caller asked whether this can
// be a name, and being unlexable is the loudest possible no.
bool ibQueryLexer::IsIdentifier(const ibString& text)
{
	if (text.IsEmpty())
		return false;

	try {
		ibQueryLexer lexer;
		const std::vector<ibQueryToken> tokens = lexer.Tokenize(text);
		return tokens.size() == 2                                   // the identifier + End
			&& tokens[0].m_kind == ibQueryTokenKind::Ident
			&& tokens[0].m_text.IsSameAs(text, true);               // it consumed the WHOLE text
	}
	catch (const ibCoreException&) {
		return false;
	}
}

// IN FIRST-APPEARANCE ORDER, because that is the order a person reads them in the query — a
// parameters page sorted by anything else makes them hunt. Repeats collapse: `&Period` written
// three times is ONE thing to fill in.
std::vector<ibString> ibQueryLexer::ParamNames(const ibString& queryText)
{
	std::vector<ibString> names;
	if (queryText.IsEmpty())
		return names;

	try {
		ibQueryLexer lexer;
		for (const ibQueryToken& token : lexer.Tokenize(queryText)) {
			if (token.m_kind != ibQueryTokenKind::Param || token.m_text.IsEmpty())
				continue;
			// Case-insensitively, like every other name in this layer.
			const auto seen = std::find_if(names.begin(), names.end(),
				[&token](const ibString& name) { return name.IsSameAs(token.m_text, false); });
			if (seen == names.end())
				names.push_back(token.m_text);
		}
	}
	catch (const ibCoreException&) {
		// Half-typed text has no parameters to speak of yet — see the header.
		names.clear();
	}
	return names;
}

std::vector<ibQueryToken> ibQueryLexer::Tokenize(const ibString& queryText)
{
	Load(queryText);            // resets buffer + position counters

	std::vector<ibQueryToken> out;
	ibString sUpper, sOrig;

	while (!IsEnd()) {          // IsEnd() skips spaces/comments first

		ibQueryToken t;
		t.m_line = GetCurrentLine() + 1;   // 0-based internally -> 1-based for users
		t.m_col  = GetCurrentPos();

		if (IsWord()) {
			sUpper.clear(); sOrig.clear();
			GetWord(&sUpper, &sOrig, /*realName*/false, /*get_point*/false);  // sUpper = uppercased, sOrig = original case
			const ibQueryKeyword kw = ibFindQueryKeyword(sUpper);
			if (kw != ibQueryKeyword::None) {
				t.m_kind = ibQueryTokenKind::Keyword;
				t.m_keyword = kw;
				t.m_text = sOrig;
			}
			else {
				t.m_kind = ibQueryTokenKind::Ident;
				t.m_text = sOrig;          // preserve case for metaobject / attribute resolution
			}
		}
		// ⭐ THE SETTERS ANSWER — AND THE ANSWER WAS BEING DROPPED.
		//
		// SetNumber / SetString / SetDate return bool: the parse IS the check (ibNumber::FromString and
		// friends). On failure they Reset() the value, so an ignored `false` does not leave the literal
		// alone — it leaves it EMPTY, and the query runs with an empty constant where the text says 15.
		// Silently: nothing downstream can tell "the author wrote nothing" from "the author wrote
		// something this could not read". The token knows its own text and position, so it refuses here.
		else if (IsNumber()) {
			sUpper.clear(); GetNumber(&sUpper); RaiseRefusal();
			t.m_kind = ibQueryTokenKind::Number;
			if (!t.m_literal.SetNumber(sUpper))
				throw ibQueryRefusal(GetCurrentPos(), GetCurrentLine() + 1,
					ibString::Format(_("Query: '%s' is not a valid number at line %u (position %u)"),
					sUpper, GetCurrentLine() + 1, GetCurrentPos()));
			t.m_text = sUpper;
		}
		else if (IsString()) {
			sUpper.clear(); GetString(&sUpper); RaiseRefusal();
			t.m_kind = ibQueryTokenKind::String;
			if (!t.m_literal.SetString(sUpper))
				throw ibQueryRefusal(GetCurrentPos(), GetCurrentLine() + 1,
					ibString::Format(_("Query: a string literal could not be read at line %u (position %u)"),
					GetCurrentLine() + 1, GetCurrentPos()));
			t.m_text = sUpper;
		}
		else if (IsDate()) {
			sUpper.clear(); GetDate(&sUpper); RaiseRefusal();
			t.m_kind = ibQueryTokenKind::Date;
			if (!t.m_literal.SetDate(sUpper))
				throw ibQueryRefusal(GetCurrentPos(), GetCurrentLine() + 1,
					ibString::Format(_("Query: '%s' is not a valid date at line %u (position %u)"),
					sUpper, GetCurrentLine() + 1, GetCurrentPos()));
			t.m_text = sUpper;
		}
		else if (IsByte(ibT('&'))) {           // &Name — a query parameter
			GetByte();                         // consume '&'
			if (!IsWord())
				throw ibQueryRefusal(GetCurrentPos(), GetCurrentLine() + 1,
					ibString::Format(_("Query: expected a parameter name after '&' at line %u (position %u)"),
					GetCurrentLine() + 1, GetCurrentPos()));
			sUpper.clear(); sOrig.clear();
			GetWord(&sUpper, &sOrig, /*realName*/false, /*get_point*/false);
			t.m_kind = ibQueryTokenKind::Param;
			t.m_text = sOrig;
		}
		else {
			// single delimiter / operator (multi-char operators glued when contiguous)
			ibChar c; GetByte(&c);
			switch (c) {
			case '<': {
				const ibChar n = PeekRawByte();
				if (n == ibT('=')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT("<="); }
				else if (n == ibT('>')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT("<>"); }
				else { t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT("<"); }
				break;
			}
			case '>': {
				const ibChar n = PeekRawByte();
				if (n == ibT('=')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT(">="); }
				else { t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT(">"); }
				break;
			}
			case '!': {
				const ibChar n = PeekRawByte();
				if (n == ibT('=')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT("<>"); }
				else { t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT("!"); }
				break;
			}
			case '=': t.m_kind = ibQueryTokenKind::Op; t.m_text = ibT("="); break;
			case '+': case '-': case '*': case '/': case '%':
				t.m_kind = ibQueryTokenKind::Op; t.m_text = ibString(c); break;
			default:
				t.m_kind = ibQueryTokenKind::Punct; t.m_text = ibString(c); break;   // , ( ) .
			}
		}

		RaiseRefusal();   // whatever else a primitive could not read
		out.push_back(std::move(t));
	}

	ibQueryToken end;
	end.m_kind = ibQueryTokenKind::End;
	end.m_line = GetCurrentLine() + 1;
	end.m_col  = GetCurrentPos();
	out.push_back(std::move(end));

	return out;
}
