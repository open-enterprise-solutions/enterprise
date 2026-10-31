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
	{ ibQueryKeyword::Select,   ibT("SELECT")   },
	{ ibQueryKeyword::From,     ibT("FROM")     },
	{ ibQueryKeyword::As,       ibT("AS")       },
	{ ibQueryKeyword::Where,    ibT("WHERE")    },
	{ ibQueryKeyword::Order,    ibT("ORDER")    },
	{ ibQueryKeyword::By,       ibT("BY")       },
	{ ibQueryKeyword::Asc,      ibT("ASC")      },
	{ ibQueryKeyword::Desc,     ibT("DESC")     },
	{ ibQueryKeyword::Group,    ibT("GROUP")    },
	{ ibQueryKeyword::Having,   ibT("HAVING")   },
	{ ibQueryKeyword::Distinct, ibT("DISTINCT") },
	{ ibQueryKeyword::Top,      ibT("TOP")      },
	{ ibQueryKeyword::Allowed,  ibT("ALLOWED")  },
	{ ibQueryKeyword::Into,     ibT("INTO")     },
	{ ibQueryKeyword::Onto,     ibT("ONTO")     },
	{ ibQueryKeyword::Link,     ibT("LINK")     },
	{ ibQueryKeyword::Drop,     ibT("DROP")     },
	{ ibQueryKeyword::Index,    ibT("INDEX")    },
	{ ibQueryKeyword::For,      ibT("FOR")      },
	{ ibQueryKeyword::Update,   ibT("UPDATE")   },
	{ ibQueryKeyword::Totals,   ibT("TOTALS")   },
	{ ibQueryKeyword::Hierarchy,    ibT("HIERARCHY")     },
	{ ibQueryKeyword::HierarchyOnly,ibT("HIERARCHYONLY") },
	{ ibQueryKeyword::Elements, ibT("ELEMENTS") },
	{ ibQueryKeyword::Overall,  ibT("OVERALL")  },
	{ ibQueryKeyword::Periods,  ibT("PERIODS")  },
	{ ibQueryKeyword::Split,    ibT("SPLIT")    },
	{ ibQueryKeyword::Join,     ibT("JOIN")     },
	{ ibQueryKeyword::Inner,    ibT("INNER")    },
	{ ibQueryKeyword::Left,     ibT("LEFT")     },
	{ ibQueryKeyword::Right,    ibT("RIGHT")    },
	{ ibQueryKeyword::Full,     ibT("FULL")     },
	{ ibQueryKeyword::Outer,    ibT("OUTER")    },
	{ ibQueryKeyword::On,       ibT("ON")       },
	{ ibQueryKeyword::Union,    ibT("UNION")    },
	{ ibQueryKeyword::All,      ibT("ALL")      },
	{ ibQueryKeyword::And,      ibT("AND")      },
	{ ibQueryKeyword::Or,       ibT("OR")       },
	{ ibQueryKeyword::Not,      ibT("NOT")      },
	{ ibQueryKeyword::In,       ibT("IN")       },
	{ ibQueryKeyword::Is,       ibT("IS")       },
	{ ibQueryKeyword::Null,     ibT("NULL")     },
	{ ibQueryKeyword::Like,     ibT("LIKE")     },
	{ ibQueryKeyword::Between,  ibT("BETWEEN")  },
	{ ibQueryKeyword::Refs,     ibT("REFS")     },
	{ ibQueryKeyword::True,     ibT("TRUE")     },
	{ ibQueryKeyword::False,    ibT("FALSE")    },
	{ ibQueryKeyword::Undefined, ibT("UNDEFINED") },
	{ ibQueryKeyword::Case,     ibT("CASE")     },
	{ ibQueryKeyword::When,     ibT("WHEN")     },
	{ ibQueryKeyword::Then,     ibT("THEN")     },
	{ ibQueryKeyword::Else,     ibT("ELSE")     },
	{ ibQueryKeyword::End,      ibT("END")      },
	{ ibQueryKeyword::IsNull,   ibT("ISNULL")   },
	{ ibQueryKeyword::Sum,      ibT("SUM")      },
	{ ibQueryKeyword::Count,    ibT("COUNT")    },
	{ ibQueryKeyword::Min,      ibT("MIN")      },
	{ ibQueryKeyword::Max,      ibT("MAX")      },
	{ ibQueryKeyword::Avg,      ibT("AVG")      },
	{ ibQueryKeyword::Value,    ibT("VALUE")    },
	{ ibQueryKeyword::Cast,     ibT("CAST")     },
	{ ibQueryKeyword::Over,      ibT("OVER")       },
	{ ibQueryKeyword::Partition, ibT("PARTITION")  },
	{ ibQueryKeyword::Rows,      ibT("ROWS")       },
	{ ibQueryKeyword::Range,     ibT("RANGE")      },
	{ ibQueryKeyword::RowNumber, ibT("ROW_NUMBER") },
	{ ibQueryKeyword::Rank,      ibT("RANK")       },
	{ ibQueryKeyword::DenseRank, ibT("DENSE_RANK") },
};

// THE SCALAR CALLS, in the same shape and beside the keywords they are deliberately NOT part of
// (queryKeywords.h says why). One row per call: the enum, the word, and how many arguments it takes
// — the arity travels WITH the word so the parser, the palette and the syntax helper cannot each
// hold a different idea of what `DATEADD` needs.
namespace {

struct ibQueryScalarFnEntry
{
	ibQueryScalarFn m_fn;
	const ibChar*   m_text;
	size_t          m_minArgs;
	size_t          m_maxArgs;
	// Which argument names a PERIOD UNIT (0 = none, 1-based otherwise). A property of the call: the
	// unit is second in BEGINOFPERIOD / DATEADD and third in DATEDIFF, and a parser that assumed one
	// position would read the other silently wrong.
	size_t          m_unitArg;
};

const ibQueryScalarFnEntry s_queryScalarFnsEN[] =
{
	{ ibQueryScalarFn::Year,            ibT("YEAR"),            1, 1, 0 },
	{ ibQueryScalarFn::Quarter,         ibT("QUARTER"),         1, 1, 0 },
	{ ibQueryScalarFn::Month,           ibT("MONTH"),           1, 1, 0 },
	{ ibQueryScalarFn::DayOfYear,       ibT("DAYOFYEAR"),       1, 1, 0 },
	{ ibQueryScalarFn::Day,             ibT("DAY"),             1, 1, 0 },
	{ ibQueryScalarFn::Week,            ibT("WEEK"),            1, 1, 0 },
	{ ibQueryScalarFn::WeekDay,         ibT("WEEKDAY"),         1, 1, 0 },
	{ ibQueryScalarFn::Hour,            ibT("HOUR"),            1, 1, 0 },
	{ ibQueryScalarFn::Minute,          ibT("MINUTE"),          1, 1, 0 },
	{ ibQueryScalarFn::Second,          ibT("SECOND"),          1, 1, 0 },
	{ ibQueryScalarFn::BeginOfPeriod,   ibT("BEGINOFPERIOD"),   2, 2, 2 },
	{ ibQueryScalarFn::EndOfPeriod,     ibT("ENDOFPERIOD"),     2, 2, 2 },
	{ ibQueryScalarFn::DateAdd,         ibT("DATEADD"),         3, 3, 2 },
	{ ibQueryScalarFn::DateDiff,        ibT("DATEDIFF"),        3, 3, 3 },
	{ ibQueryScalarFn::DateTime,        ibT("DATETIME"),        3, 6, 0 },
	{ ibQueryScalarFn::Substring,       ibT("SUBSTRING"),       3, 3, 0 },
	{ ibQueryScalarFn::ValueType,       ibT("VALUETYPE"),       1, 1, 0 },
	{ ibQueryScalarFn::Type,            ibT("TYPE"),            1, 1, 0 },
	{ ibQueryScalarFn::Presentation,    ibT("PRESENTATION"),    1, 1, 0 },
	{ ibQueryScalarFn::RefPresentation, ibT("REFPRESENTATION"), 1, 1, 0 },
	{ ibQueryScalarFn::Grouping,        ibT("GROUPING"),        1, 1, 0 },
	{ ibQueryScalarFn::RecordAutoNumber,ibT("RECORDAUTONUMBER"),0, 0, 0 },
};

const ibQueryScalarFnEntry* ScalarEntry(ibQueryScalarFn fn)
{
	for (const ibQueryScalarFnEntry& e : s_queryScalarFnsEN)
		if (e.m_fn == fn)
			return &e;
	return nullptr;
}

} // namespace

ibQueryScalarFn ibFindQueryScalarFn(const ibString& upperWord)
{
	static const std::map<ibString, ibQueryScalarFn> s_map = [] {
		std::map<ibString, ibQueryScalarFn> m;
		for (const ibQueryScalarFnEntry& e : s_queryScalarFnsEN)
			m[e.m_text] = e.m_fn;
		return m;
	}();
	const auto it = s_map.find(upperWord);
	return it != s_map.end() ? it->second : ibQueryScalarFn::None;
}

const ibString& ibQueryScalarFnText(ibQueryScalarFn fn)
{
	static const std::map<ibQueryScalarFn, ibString> s_map = [] {
		std::map<ibQueryScalarFn, ibString> m;
		for (const ibQueryScalarFnEntry& e : s_queryScalarFnsEN)
			m[e.m_fn] = e.m_text;
		return m;
	}();
	static const ibString s_empty;
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

ibString ibAllQueryScalarFns()
{
	ibString out;
	for (const ibQueryScalarFnEntry& entry : s_queryScalarFnsEN) {
		if (!out.IsEmpty())
			out += ibT(" ");
		out += entry.m_text;
	}
	return out;
}

namespace {

// uppercase spelling -> keyword (built once); keyword -> spelling (diagnostics).
std::map<ibString, ibQueryKeyword>& KeywordByText()
{
	static std::map<ibString, ibQueryKeyword> s_map = [] {
		std::map<ibString, ibQueryKeyword> m;
		for (const ibQueryKeyWordEntry& e : s_queryKeyWordsEN)
			m[e.m_text] = e.m_kw;
		return m;
	}();
	return s_map;
}

std::map<ibQueryKeyword, ibString>& TextByKeyword()
{
	static std::map<ibQueryKeyword, ibString> s_map = [] {
		std::map<ibQueryKeyword, ibString> m;
		for (const ibQueryKeyWordEntry& e : s_queryKeyWordsEN)
			m[e.m_kw] = e.m_text;
		return m;
	}();
	return s_map;
}

} // namespace

ibQueryKeyword ibFindQueryKeyword(const ibString& upperWord)
{
	const std::map<ibString, ibQueryKeyword>& m = KeywordByText();
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

const ibString& ibQueryKeywordText(ibQueryKeyword kw)
{
	static const ibString s_empty;
	const std::map<ibQueryKeyword, ibString>& m = TextByKeyword();
	auto it = m.find(kw);
	return it != m.end() ? it->second : s_empty;
}

// THE WHOLE ACTIVE TABLE, for the editor's highlighting. Asked of the table rather than typed out
// again: a keyword added to the language lights up the day it is added, and a localized table
// lights up ITS words — a hand-written list in the editor would highlight last year's language.
ibString ibAllQueryKeywords()
{
	ibString out;
	for (const ibQueryKeyWordEntry& entry : s_queryKeyWordsEN) {
		if (!out.IsEmpty())
			out += ibT(" ");
		out += entry.m_text;
	}
	// …AND THE CONTEXTUAL WORDS, which the lexer does not reserve but a person reads as words of the language:
	// `ROLE` and the words of a role (`<expr> ROLE PERIOD 1 AS …`). Painted as words, still free as names —
	// the painter matches the case they are written in, so a field called `Period` stays a name to the eye too.
	out += ibT(" ROLE");
	for (const ibBalanceRole role : { ibBalanceRole::None, ibBalanceRole::Moment, ibBalanceRole::Dimension,
	                                  ibBalanceRole::Opening, ibBalanceRole::Closing }) {
		out += ibT(" ");
		out += ibBalanceRoleWord(role);
	}
	return out;
}
