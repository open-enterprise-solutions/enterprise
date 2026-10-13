////////////////////////////////////////////////////////////////////////////
//	Description : The query language's words — its keyword and scalar-call tables (queryKeywords.h)
////////////////////////////////////////////////////////////////////////////

#include "core/query/queryKeywords.h"
#include "core/query/queryBalanceRole.h"   // ibBalanceRoleWord — the contextual words the painter shows as words

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
	{ ibQueryKeyword::Undefined, wxT("UNDEFINED") },
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

bool ibQueryScalarArgIsWord(ibQueryScalarFn fn, size_t index)
{
	if (fn == ibQueryScalarFn::Type)
		return index == 0;             // its one argument is the name of a type
	size_t unit = 0;
	return ibQueryScalarFnUnitArg(fn, unit) && unit == index;
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
	// …AND THE CONTEXTUAL WORDS, which the lexer does not reserve but a person reads as words of the language:
	// `ROLE` and the words of a role (`<expr> ROLE PERIOD 1 AS …`). Painted as words, still free as names —
	// the painter matches the case they are written in, so a field called `Period` stays a name to the eye too.
	out += wxT(" ROLE");
	for (const ibBalanceRole role : { ibBalanceRole::None, ibBalanceRole::Moment, ibBalanceRole::Dimension,
	                                  ibBalanceRole::Opening, ibBalanceRole::Closing }) {
		out += wxT(" ");
		out += ibBalanceRoleWord(role);
	}
	return out;
}
