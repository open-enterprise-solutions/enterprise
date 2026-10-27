#ifndef __QUERY_SOURCE_PARAMETER_H__
#define __QUERY_SOURCE_PARAMETER_H__

////////////////////////////////////////////////////////////////////////////
//	Description : ONE PARAMETER OF A SOURCE CALL - as the source declares it and every window reads it
////////////////////////////////////////////////////////////////////////////
//
// The engine's descriptors declare it (backend/query/queryableFactory.h, DescribeParameters); the windows that offer a
// virtual table's arguments read it — the desktop's beside the model, the client's from the server's answer. One schema
// for both, so it lives here.

#include "core/description/typeDescription.h"   // ibTypeDescription — what a value argument holds

#include <vector>

// ONE PARAMETER OF A SOURCE CALL — `Balance(&Period, Warehouse = &Store)`.
//
// A virtual table takes its arguments IN ORDER and, until now, read them positionally out of
// paParams[i] with nobody else knowing what `i` meant: not the constructor (which therefore had no
// way to offer them), not the name check (which could not see a missing or surplus argument), not a
// person reading the text. The source is the only one who knows; this is it saying so.
//
// EVERY ARGUMENT IS AN EXPRESSION — `&Period`, a literal, `BegOfMonth(&Date)`, `Warehouse = &Store`.
// The kind does not say "expression or not"; it says WHERE ITS NAMES ARE LOOKED UP and WHAT SORT OF
// ANSWER is expected:
//
//   * VALUE     — a scalar, and it is settled BEFORE THE QUERY RUNS. That is the whole reason no
//                 column is in scope here — not this table's, and certainly not another table of the
//                 query. The order is: the arguments are evaluated, THEN the virtual table produces
//                 its rows from them, THEN the engine joins and filters. A parameter that named a
//                 column would be asking for a value from rows that do not exist yet.
//                 So what may stand here is a parameter, a literal, or a computation over them
//                 (`BegOfMonth(&Date)`). `m_type` says what it holds, and that also decides how it
//                 is edited: a date gets a date box, a registered enumeration (a turnover table's
//                 periodicity: second … year) gets its own list. Deliberately NOT a separate
//                 "periodicity" kind — that would restate the enumeration in a widget, and the next
//                 member added to it would have to be added twice.
//   * CONDITION — a predicate. Identifiers in it are THIS TABLE'S columns; `&name` is still an
//                 ordinary query parameter, so the two cannot be confused (the ampersand is the
//                 distinction). Which columns are admissible is a question of its own —
//                 FillConditionExplorer — because a balance may be filtered by its dimensions
//                 but not by a resource it folded, while a slice admits every attribute it carries.
struct ibQuerySourceParameter
{
	ibString          m_name;                 // Period / Begin / End / Periodicity / Condition
	bool              m_condition = false;    // false = value expression, true = predicate
	ibTypeDescription m_type;                 // VALUE only: what it holds — and therefore how it is edited
	bool              m_required = false;

	// ⭐ A CLOSED SET OF VALUES, WHEN THE ARGUMENT HAS ONE — the periodicity a turnover rolls up to,
	// the way a balance-and-turnovers table is filled. Declared BY THE SOURCE, because the source is
	// the only one that knows what it accepts; a window that carried the list would be holding a
	// second copy of the source's own vocabulary, and the two would part company on the first change.
	//
	// Empty means "any expression the language can write" — a moment, a parameter, a computation.
	// Non-empty makes the argument a CHOICE, and the editor shows exactly these and nothing else.
	std::vector<ibString> m_choices;

	// ⭐ WHAT THIS ARGUMENT DECIDES, in one line, when the name alone is not enough — declared by
	// the source for the same reason the choices are: it is the only one that knows.
	//
	// 🛑 `Periodicity` is why this exists. The word names two different things one floor apart: the
	// register's STORED grain (a metadata property, whose words are "Non periodic / Within second /
	// Within day") and the granularity of a READING (this, whose words are Period / Recorder /
	// Day / Month / …). A newcomer reads one list and writes it into the other, and both refusals
	// are correct and neither says the two are different questions (measured over MCP, 2026-09-03).
	// Empty for an argument whose name says it all.
	ibString m_description;

	// ⭐⭐ THE SOURCE CONSUMES THIS CONDITION ITSELF — it is not folded into the WHERE around it.
	//
	// An ordinary condition slot is sugar: the predicate written there is ANDed into the query's own
	// WHERE, and the source never learns of it. That is right when the condition only SELECTS rows.
	// It is wrong when the source has to ACT on it — an accounting register asked for accounts «in
	// hierarchy» reports the subordinate accounts UNDER the one that was named, and a filter applied
	// around the reading cannot fold anything: it can only remove rows the reading already produced.
	//
	// Set, the lowering resolves the predicate against `GetConditionScope()` and hands it to
	// `CreateQueryable` by slot position instead of into the WHERE. Nothing else changes: the source
	// is then the only place that condition exists, which is what makes the fold possible at all.
	bool m_consumedBySource = false;

	// AND WHAT THE SOURCE USES WHEN THE ARGUMENT IS LEFT OUT. Shown to the author rather than left to
	// be guessed: an empty box that quietly means "Auto" teaches nothing, and the first surprise
	// arrives when somebody sets it explicitly and the result changes.
	ibString              m_default;

};

#endif // __QUERY_SOURCE_PARAMETER_H__
