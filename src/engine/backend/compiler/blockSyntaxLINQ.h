#ifndef __BLOCK_SYNTAX_LINQ_H__
#define __BLOCK_SYNTAX_LINQ_H__

////////////////////////////////////////////////////////////////////////////
// LINQ'S BLOCK SYNTAX AS A THING THAT CAN BE BUILT — `From … In … Where … Select { … }`.
////////////////////////////////////////////////////////////////////////////
//
// The block is how a query is WRITTEN in this language - its BLOCK syntax, beside the chain one
// (linq.md, Grammar; the compiler reads it in CompileLinqBlock). Two things were missing
// for building one rather than typing it: a description of the block as parts — sources, conditions,
// grouping, order, fields — and one place that knows which clause may follow which. The second
// existed, inline in the completion walk (scriptComplete.cpp), which is exactly the question both a
// dropdown and a constructor ask; it lives here now and both read it.
//
// ⭐ THE WORDS ARE THE LEXER'S. Every keyword is spelled by ibTranslateCode::GetKeyWord, never by a
// string of this file's own, so a block written here reads the way the lexer reads it and a keyword
// the language gains is the language's before it is this file's.
//
// ⭐ THE WRITER CHECKS ITSELF AGAINST THE SAME TABLE. Render walks the clause words it writes through
// ibLinqClausesAfter; a block whose order the completion would not have offered is a defect in the
// writer, and it is refused rather than written.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"

#include <vector>

// WHICH QUERY WORDS MAY BE WRITTEN after `lastKeyword` — the last query keyword before the place in
// question (KEY_FROM … KEY_RESTRICT, codeDef.h), inside a query. After `From`/`Join` only `In`; after
// `On` only `Equals`; after `Group` only `By`; after `By` of a group only its `Into` - a group without
// a name ends the query; after `Into` the clauses that read the groups; after `Select` only
// `Distinct`; anywhere else the clauses: another `From` (a product), `Join`, `Where`, `Group`,
// `OrderBy`, `Skip`, `Take`, `Select`. Modifiers are answered only where the grammar demands them, never
// offered standalone.
BACKEND_API std::vector<int> ibLinqClausesAfter(int lastKeyword);

// One source of the block: `From alias In expression`, or `Join alias In expression On left Equals
// right`. A second source that is not a join is a product — `From` again, as the language spells it.
struct BACKEND_API ibLinqBlockSource {
	wxString m_alias;
	wxString m_expression;
	bool     m_join = false;
	wxString m_leftKey;      // a join's `On` side
	wxString m_rightKey;     // …and its `Equals` side
};

// One column of the answer: `name = expression` inside `Select { … }`. Also one key of a group — its
// name is the key's column and, with several keys, its field in the key.
struct BACKEND_API ibLinqBlockField {
	wxString m_name;
	wxString m_expression;
};

// WHAT A TOTAL DOES WITH THE ROWS OF ITS GROUP - the aggregates a group's `Values` answers by name
// (procUnitLINQ.cpp, the method table). Count reads no field; the others read one.
enum class ibLinqTotal { Sum, Count, Min, Max, Average };

// One total of a grouped answer: `name = <group>.Values.Sum(…)` - the column it makes, what it does,
// and the field of a ROW it reads, written as the rows are read before grouping (`o.Amount`).
struct BACKEND_API ibLinqBlockTotal {
	wxString    m_name;
	ibLinqTotal m_function = ibLinqTotal::Sum;
	wxString    m_expression;
};

// The method a total calls, in the language's spelling - `Sum`, `Count`, …
BACKEND_API wxString ibLinqTotalName(ibLinqTotal total);

// One key of the order. The language sets the direction per `OrderBy` clause (linq.md 0.5), so keys
// of one direction in a row share a clause and a change of direction opens the next one.
struct BACKEND_API ibLinqBlockOrder {
	wxString m_expression;
	bool     m_descending = false;
};

class BACKEND_API ibLinqBlock {
public:

	std::vector<ibLinqBlockSource> m_sources;
	std::vector<wxString>          m_conditions;       // one `Where` each, on the rows
	std::vector<ibLinqBlockOrder>  m_order;
	wxString                       m_skip;             // a number of rows, or a variable that holds one
	wxString                       m_take;
	std::vector<ibLinqBlockField>  m_fields;           // none: `Select <first alias>`, the row itself
	bool                           m_distinct = false;

	// ⭐ GROUPING (linq.md, GROUP BY). Rows with the same KEYS become one row of the answer. One key is
	// written as it is (`By o.Customer`); several are made into one value (`By New Structure("Customer,
	// Warehouse", o.Customer, o.Warehouse)`), which groups by its contents. A group has a NAME (`Into`):
	// what follows reads the groups, not the rows - `<name>.Key` and `<name>.Values`.
	//
	// So in a grouped block the answer's columns are the keys, then the totals, then `m_fields` - these
	// written as they are, over the group (`<name>.Values`). The conditions stay on the rows, before
	// grouping; `m_groupConditions` read the groups; the order sorts the groups - an order written by a
	// key's expression or a total's name is turned into the group's own words.
	//
	// With no name, the group is TERMINAL: the answer is the groups themselves, `Key` and `Values`, and
	// no column can be chosen (linq.md: `group X by K` is mutually exclusive with SELECT).
	std::vector<ibLinqBlockField>  m_groupKeys;
	std::vector<ibLinqBlockTotal>  m_totals;
	wxString                       m_groupInto;
	std::vector<wxString>          m_groupConditions;  // `Where` after `Into`, on the groups

	bool IsGrouped() const { return !m_groupKeys.empty(); }

	// The block, one clause a line, continuation lines opened with `indent`. Empty with `refusal` set
	// when there is nothing to write (no source) or a part is missing its words (an alias, a source,
	// a join key); a refusal says which. A single column with no name is written bare - `Select o.Name`.
	wxString Render(const wxString& indent, wxString& refusal) const;

	// The same text written EVEN WHERE A PART IS REFUSED, the parts as they stand - for a preview, which
	// must not go blank at the first wrong cell. `refusal` still names the first thing wrong; the text
	// is for showing, never for writing into a module (Render is that).
	wxString RenderDraft(const wxString& indent, wxString& refusal) const;

	// ⭐ AND BACK: the parts of a block already written, so a constructor can OPEN a query and not only
	// write a new one. Where the query is and where it ends is the COMPILER's to say (ibQueryOutline's
	// text, which it bracketed while reading); what is read here is that text, TOKENISED BY THE LEXER
	// (strings, comments and brackets are its business) and cut at the query words of depth zero by the
	// same table Render writes by. False with `refusal` when the text holds a part the block does not
	// model — a `Var` (let) clause, a second group — rather than a partial reading passed off as whole.
	//
	// The query ends at a `;`, at the brace closing `Select { … }` (and a `Distinct` after it), or with
	// the line of a bare `Select` - so `text` may run past it, and `consumed` says how much of it the
	// query is. That is what a caller replaces: the compiler's bracket ends where the compiler STOPPED,
	// which in a query left half-written (`On g. Equals c.`) is mid-way through it.
	static bool Parse(const wxString& text, ibLinqBlock& block, wxString& refusal, size_t* consumed = nullptr);

private:

	wxString Write(const wxString& indent, wxString& refusal, bool draft) const;
};

#endif
