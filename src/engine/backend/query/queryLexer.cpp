////////////////////////////////////////////////////////////////////////////
//	L4-1 — text query language lexer (queryLexer.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLexer.h"

#include "queryException.h"   // ibBackendQuerySourceException — L4 refuses in its own variety

#include <map>

//////////////////////////////////////////////////////////////////////
// Keyword table (English canon) + lookup
//////////////////////////////////////////////////////////////////////

// The ACTIVE keyword set. English today; a UK/RU table is added later and
// selected at runtime (the enum stays the same, so the parser is locale-blind).
static const ibQueryKeyWordEntry s_queryKeyWordsEN[] =
{
	{ ibQueryKeyword::Select,   wxT("SELECT")   },
	{ ibQueryKeyword::From,     wxT("FROM")     },
	{ ibQueryKeyword::As,       wxT("AS")       },
	{ ibQueryKeyword::Where,    wxT("WHERE")    },
	{ ibQueryKeyword::Order,    wxT("ORDER")    },
	{ ibQueryKeyword::By,       wxT("BY")       },
	{ ibQueryKeyword::Asc,      wxT("ASC")      },
	{ ibQueryKeyword::Desc,     wxT("DESC")     },
	{ ibQueryKeyword::Group,    wxT("GROUP")    },
	{ ibQueryKeyword::Having,   wxT("HAVING")   },
	{ ibQueryKeyword::Distinct, wxT("DISTINCT") },
	{ ibQueryKeyword::Top,      wxT("TOP")      },
	{ ibQueryKeyword::Allowed,  wxT("ALLOWED")  },
	{ ibQueryKeyword::Into,     wxT("INTO")     },
	{ ibQueryKeyword::Onto,     wxT("ONTO")     },
	{ ibQueryKeyword::Link,     wxT("LINK")     },
	{ ibQueryKeyword::Drop,     wxT("DROP")     },
	{ ibQueryKeyword::Index,    wxT("INDEX")    },
	{ ibQueryKeyword::For,      wxT("FOR")      },
	{ ibQueryKeyword::Update,   wxT("UPDATE")   },
	{ ibQueryKeyword::Totals,   wxT("TOTALS")   },
	{ ibQueryKeyword::Hierarchy,    wxT("HIERARCHY")     },
	{ ibQueryKeyword::HierarchyOnly,wxT("HIERARCHYONLY") },
	{ ibQueryKeyword::Elements, wxT("ELEMENTS") },
	{ ibQueryKeyword::Overall,  wxT("OVERALL")  },
	{ ibQueryKeyword::Periods,  wxT("PERIODS")  },
	{ ibQueryKeyword::Split,    wxT("SPLIT")    },
	{ ibQueryKeyword::Join,     wxT("JOIN")     },
	{ ibQueryKeyword::Inner,    wxT("INNER")    },
	{ ibQueryKeyword::Left,     wxT("LEFT")     },
	{ ibQueryKeyword::Right,    wxT("RIGHT")    },
	{ ibQueryKeyword::Full,     wxT("FULL")     },
	{ ibQueryKeyword::Outer,    wxT("OUTER")    },
	{ ibQueryKeyword::On,       wxT("ON")       },
	{ ibQueryKeyword::Union,    wxT("UNION")    },
	{ ibQueryKeyword::All,      wxT("ALL")      },
	{ ibQueryKeyword::And,      wxT("AND")      },
	{ ibQueryKeyword::Or,       wxT("OR")       },
	{ ibQueryKeyword::Not,      wxT("NOT")      },
	{ ibQueryKeyword::In,       wxT("IN")       },
	{ ibQueryKeyword::Is,       wxT("IS")       },
	{ ibQueryKeyword::Null,     wxT("NULL")     },
	{ ibQueryKeyword::Like,     wxT("LIKE")     },
	{ ibQueryKeyword::Between,  wxT("BETWEEN")  },
	{ ibQueryKeyword::Refs,     wxT("REFS")     },
	{ ibQueryKeyword::True,     wxT("TRUE")     },
	{ ibQueryKeyword::False,    wxT("FALSE")    },
	{ ibQueryKeyword::Case,     wxT("CASE")     },
	{ ibQueryKeyword::When,     wxT("WHEN")     },
	{ ibQueryKeyword::Then,     wxT("THEN")     },
	{ ibQueryKeyword::Else,     wxT("ELSE")     },
	{ ibQueryKeyword::End,      wxT("END")      },
	{ ibQueryKeyword::IsNull,   wxT("ISNULL")   },
	{ ibQueryKeyword::Sum,      wxT("SUM")      },
	{ ibQueryKeyword::Count,    wxT("COUNT")    },
	{ ibQueryKeyword::Min,      wxT("MIN")      },
	{ ibQueryKeyword::Max,      wxT("MAX")      },
	{ ibQueryKeyword::Avg,      wxT("AVG")      },
	{ ibQueryKeyword::Value,    wxT("VALUE")    },
	{ ibQueryKeyword::Cast,     wxT("CAST")     },
	{ ibQueryKeyword::Over,      wxT("OVER")       },
	{ ibQueryKeyword::Partition, wxT("PARTITION")  },
	{ ibQueryKeyword::Rows,      wxT("ROWS")       },
	{ ibQueryKeyword::Range,     wxT("RANGE")      },
	{ ibQueryKeyword::RowNumber, wxT("ROW_NUMBER") },
	{ ibQueryKeyword::Rank,      wxT("RANK")       },
	{ ibQueryKeyword::DenseRank, wxT("DENSE_RANK") },
};

// THE SCALAR CALLS, in the same shape and beside the keywords they are deliberately NOT part of
// (queryKeywords.h says why). One row per call: the enum, the word, and how many arguments it takes
// — the arity travels WITH the word so the parser, the palette and the syntax helper cannot each
// hold a different idea of what `DATEADD` needs.
namespace {

struct ibQueryScalarFnEntry
{
	ibQueryScalarFn m_fn;
	const wxChar*   m_text;
	size_t          m_minArgs;
	size_t          m_maxArgs;
	// Which argument names a PERIOD UNIT (0 = none, 1-based otherwise). A property of the call: the
	// unit is second in BEGINOFPERIOD / DATEADD and third in DATEDIFF, and a parser that assumed one
	// position would read the other silently wrong.
	size_t          m_unitArg;
};

const ibQueryScalarFnEntry s_queryScalarFnsEN[] =
{
	{ ibQueryScalarFn::Year,            wxT("YEAR"),            1, 1, 0 },
	{ ibQueryScalarFn::Quarter,         wxT("QUARTER"),         1, 1, 0 },
	{ ibQueryScalarFn::Month,           wxT("MONTH"),           1, 1, 0 },
	{ ibQueryScalarFn::DayOfYear,       wxT("DAYOFYEAR"),       1, 1, 0 },
	{ ibQueryScalarFn::Day,             wxT("DAY"),             1, 1, 0 },
	{ ibQueryScalarFn::Week,            wxT("WEEK"),            1, 1, 0 },
	{ ibQueryScalarFn::WeekDay,         wxT("WEEKDAY"),         1, 1, 0 },
	{ ibQueryScalarFn::Hour,            wxT("HOUR"),            1, 1, 0 },
	{ ibQueryScalarFn::Minute,          wxT("MINUTE"),          1, 1, 0 },
	{ ibQueryScalarFn::Second,          wxT("SECOND"),          1, 1, 0 },
	{ ibQueryScalarFn::BeginOfPeriod,   wxT("BEGINOFPERIOD"),   2, 2, 2 },
	{ ibQueryScalarFn::EndOfPeriod,     wxT("ENDOFPERIOD"),     2, 2, 2 },
	{ ibQueryScalarFn::DateAdd,         wxT("DATEADD"),         3, 3, 2 },
	{ ibQueryScalarFn::DateDiff,        wxT("DATEDIFF"),        3, 3, 3 },
	{ ibQueryScalarFn::DateTime,        wxT("DATETIME"),        3, 6, 0 },
	{ ibQueryScalarFn::Substring,       wxT("SUBSTRING"),       3, 3, 0 },
	{ ibQueryScalarFn::ValueType,       wxT("VALUETYPE"),       1, 1, 0 },
	{ ibQueryScalarFn::Type,            wxT("TYPE"),            1, 1, 0 },
	{ ibQueryScalarFn::Presentation,    wxT("PRESENTATION"),    1, 1, 0 },
	{ ibQueryScalarFn::RefPresentation, wxT("REFPRESENTATION"), 1, 1, 0 },
	{ ibQueryScalarFn::Grouping,        wxT("GROUPING"),        1, 1, 0 },
	{ ibQueryScalarFn::RecordAutoNumber,wxT("RECORDAUTONUMBER"),0, 0, 0 },
};

const ibQueryScalarFnEntry* ScalarEntry(ibQueryScalarFn fn)
{
	for (const ibQueryScalarFnEntry& e : s_queryScalarFnsEN)
		if (e.m_fn == fn)
			return &e;
	return nullptr;
}

} // namespace

ibQueryScalarFn ibFindQueryScalarFn(const wxString& upperWord)
{
	static const std::map<wxString, ibQueryScalarFn> s_map = [] {
		std::map<wxString, ibQueryScalarFn> m;
		for (const ibQueryScalarFnEntry& e : s_queryScalarFnsEN)
			m[e.m_text] = e.m_fn;
		return m;
	}();
	const auto it = s_map.find(upperWord);
	return it != s_map.end() ? it->second : ibQueryScalarFn::None;
}

const wxString& ibQueryScalarFnText(ibQueryScalarFn fn)
{
	static const std::map<ibQueryScalarFn, wxString> s_map = [] {
		std::map<ibQueryScalarFn, wxString> m;
		for (const ibQueryScalarFnEntry& e : s_queryScalarFnsEN)
			m[e.m_fn] = e.m_text;
		return m;
	}();
	static const wxString s_empty;
	const auto it = s_map.find(fn);
	return it != s_map.end() ? it->second : s_empty;
}

bool ibQueryScalarFnArity(ibQueryScalarFn fn, size_t& outMin, size_t& outMax)
{
	const ibQueryScalarFnEntry* e = ScalarEntry(fn);
	if (e == nullptr)
		return false;
	outMin = e->m_minArgs; outMax = e->m_maxArgs;
	return true;
}

bool ibQueryScalarFnUnitArg(ibQueryScalarFn fn, size_t& outIndex)
{
	const ibQueryScalarFnEntry* e = ScalarEntry(fn);
	if (e == nullptr || e->m_unitArg == 0)
		return false;
	outIndex = e->m_unitArg - 1;   // the table counts from one, the argument vector from zero
	return true;
}

wxString ibAllQueryScalarFns()
{
	wxString out;
	for (const ibQueryScalarFnEntry& entry : s_queryScalarFnsEN) {
		if (!out.IsEmpty())
			out += wxT(" ");
		out += entry.m_text;
	}
	return out;
}

namespace {

// uppercase spelling -> keyword (built once); keyword -> spelling (diagnostics).
std::map<wxString, ibQueryKeyword>& KeywordByText()
{
	static std::map<wxString, ibQueryKeyword> s_map = [] {
		std::map<wxString, ibQueryKeyword> m;
		for (const ibQueryKeyWordEntry& e : s_queryKeyWordsEN)
			m[e.m_text] = e.m_kw;
		return m;
	}();
	return s_map;
}

std::map<ibQueryKeyword, wxString>& TextByKeyword()
{
	static std::map<ibQueryKeyword, wxString> s_map = [] {
		std::map<ibQueryKeyword, wxString> m;
		for (const ibQueryKeyWordEntry& e : s_queryKeyWordsEN)
			m[e.m_kw] = e.m_text;
		return m;
	}();
	return s_map;
}

} // namespace

ibQueryKeyword ibFindQueryKeyword(const wxString& upperWord)
{
	const std::map<wxString, ibQueryKeyword>& m = KeywordByText();
	auto it = m.find(upperWord);
	return it != m.end() ? it->second : ibQueryKeyword::None;
}

bool ibIsAggregateKeyword(ibQueryKeyword kw)
{
	switch (kw) {
	case ibQueryKeyword::Sum:
	case ibQueryKeyword::Count:
	case ibQueryKeyword::Min:
	case ibQueryKeyword::Max:
	case ibQueryKeyword::Avg:
		return true;
	default:
		return false;
	}
}

bool ibIsRankingKeyword(ibQueryKeyword kw)
{
	switch (kw) {
	case ibQueryKeyword::RowNumber:
	case ibQueryKeyword::Rank:
	case ibQueryKeyword::DenseRank:
		return true;
	default:
		return false;
	}
}

bool ibDistinctMattersFor(ibQueryKeyword aggregate)
{
	switch (aggregate) {
	case ibQueryKeyword::Sum:
	case ibQueryKeyword::Avg:
	case ibQueryKeyword::Count:
		return true;   // duplicates carry weight
	default:
		return false;  // MIN / MAX answer the same value however often it occurs
	}
}

const wxString& ibQueryKeywordText(ibQueryKeyword kw)
{
	static const wxString s_empty;
	const std::map<ibQueryKeyword, wxString>& m = TextByKeyword();
	auto it = m.find(kw);
	return it != m.end() ? it->second : s_empty;
}

// THE WHOLE ACTIVE TABLE, for the editor's highlighting. Asked of the table rather than typed out
// again: a keyword added to the language lights up the day it is added, and a localized table
// lights up ITS words — a hand-written list in the editor would highlight last year's language.
wxString ibAllQueryKeywords()
{
	wxString out;
	for (const ibQueryKeyWordEntry& entry : s_queryKeyWordsEN) {
		if (!out.IsEmpty())
			out += wxT(" ");
		out += entry.m_text;
	}
	return out;
}

//////////////////////////////////////////////////////////////////////
// ibQueryLexer
//////////////////////////////////////////////////////////////////////

void ibQueryLexer::DoSetError(int /*codeError*/,
	const wxString& /*strFileName*/, const wxString& /*strModuleName*/, const wxString& /*strDocPath*/,
	unsigned int currPos, unsigned int currLine,
	const wxString& errorDesc) const
{
	// L4 refuses in its OWN variety, carrying the position as data — the text is what is wrong here,
	// not the machinery, and the caller that shows this is the one editing the query.
	if (errorDesc.empty())
		ibBackendQuerySyntaxException::ErrorAt(currLine, currPos,
			_("Query lexical error at line %u (position %u)"), currLine, currPos);
	else
		ibBackendQuerySyntaxException::ErrorAt(currLine, currPos,
			_("Query lexical error at line %u (position %u): %s"), currLine, currPos, errorDesc);
}

wxChar ibQueryLexer::PeekRawByte() const
{
	const unsigned int pos = GetCurrentPos();
	if (pos < GetBufferSize())
		return m_strBuffer[pos];
	return wxChar(0);
}

// THE LEXER'S OWN ANSWER, not a second opinion about it. Anything that lexes as one identifier and
// nothing else is a name the language can carry; everything else — a space, a dot, a bracket, a
// keyword, an empty string — is not. A lex error is simply "no": the caller asked whether this can
// be a name, and being unlexable is the loudest possible no.
bool ibQueryLexer::IsIdentifier(const wxString& text)
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
	catch (const ibBackendException&) {
		return false;
	}
}

// IN FIRST-APPEARANCE ORDER, because that is the order a person reads them in the query — a
// parameters page sorted by anything else makes them hunt. Repeats collapse: `&Period` written
// three times is ONE thing to fill in.
std::vector<wxString> ibQueryLexer::ParamNames(const wxString& queryText)
{
	std::vector<wxString> names;
	if (queryText.IsEmpty())
		return names;

	try {
		ibQueryLexer lexer;
		for (const ibQueryToken& token : lexer.Tokenize(queryText)) {
			if (token.m_kind != ibQueryTokenKind::Param || token.m_text.IsEmpty())
				continue;
			// Case-insensitively, like every other name in this layer.
			const auto seen = std::find_if(names.begin(), names.end(),
				[&token](const wxString& name) { return name.IsSameAs(token.m_text, false); });
			if (seen == names.end())
				names.push_back(token.m_text);
		}
	}
	catch (const ibBackendException&) {
		// Half-typed text has no parameters to speak of yet — see the header.
		names.clear();
	}
	return names;
}

std::vector<ibQueryToken> ibQueryLexer::Tokenize(const wxString& queryText)
{
	Load(queryText);            // resets buffer + position counters

	std::vector<ibQueryToken> out;
	wxString sUpper, sOrig;

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
			sUpper.clear(); GetNumber(&sUpper);
			t.m_kind = ibQueryTokenKind::Number;
			if (!t.m_literal.SetNumber(sUpper))
				ibBackendQuerySyntaxException::ErrorAt(GetCurrentLine() + 1, GetCurrentPos(),
					_("Query: '%s' is not a valid number at line %u (position %u)"),
					sUpper, GetCurrentLine() + 1, GetCurrentPos());
			t.m_text = sUpper;
		}
		else if (IsString()) {
			sUpper.clear(); GetString(&sUpper);
			t.m_kind = ibQueryTokenKind::String;
			if (!t.m_literal.SetString(sUpper))
				ibBackendQuerySyntaxException::ErrorAt(GetCurrentLine() + 1, GetCurrentPos(),
					_("Query: a string literal could not be read at line %u (position %u)"),
					GetCurrentLine() + 1, GetCurrentPos());
			t.m_text = sUpper;
		}
		else if (IsDate()) {
			sUpper.clear(); GetDate(&sUpper);
			t.m_kind = ibQueryTokenKind::Date;
			if (!t.m_literal.SetDate(sUpper))
				ibBackendQuerySyntaxException::ErrorAt(GetCurrentLine() + 1, GetCurrentPos(),
					_("Query: '%s' is not a valid date at line %u (position %u)"),
					sUpper, GetCurrentLine() + 1, GetCurrentPos());
			t.m_text = sUpper;
		}
		else if (IsByte(wxT('&'))) {           // &Name — a query parameter
			GetByte();                         // consume '&'
			if (!IsWord())
				ibBackendQuerySyntaxException::ErrorAt(GetCurrentLine() + 1, GetCurrentPos(),
					_("Query: expected a parameter name after '&' at line %u (position %u)"),
					GetCurrentLine() + 1, GetCurrentPos());
			sUpper.clear(); sOrig.clear();
			GetWord(&sUpper, &sOrig, /*realName*/false, /*get_point*/false);
			t.m_kind = ibQueryTokenKind::Param;
			t.m_text = sOrig;
		}
		else {
			// single delimiter / operator (multi-char operators glued when contiguous)
			wxUniChar c; GetByte(&c);
			switch (c.GetValue()) {
			case '<': {
				const wxChar n = PeekRawByte();
				if (n == wxT('=')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT("<="); }
				else if (n == wxT('>')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT("<>"); }
				else { t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT("<"); }
				break;
			}
			case '>': {
				const wxChar n = PeekRawByte();
				if (n == wxT('=')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT(">="); }
				else { t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT(">"); }
				break;
			}
			case '!': {
				const wxChar n = PeekRawByte();
				if (n == wxT('=')) { GetByte(); t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT("<>"); }
				else { t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT("!"); }
				break;
			}
			case '=': t.m_kind = ibQueryTokenKind::Op; t.m_text = wxT("="); break;
			case '+': case '-': case '*': case '/': case '%':
				t.m_kind = ibQueryTokenKind::Op; t.m_text = wxString(c); break;
			default:
				t.m_kind = ibQueryTokenKind::Punct; t.m_text = wxString(c); break;   // , ( ) .
			}
		}

		out.push_back(std::move(t));
	}

	ibQueryToken end;
	end.m_kind = ibQueryTokenKind::End;
	end.m_line = GetCurrentLine() + 1;
	end.m_col  = GetCurrentPos();
	out.push_back(std::move(end));

	return out;
}
