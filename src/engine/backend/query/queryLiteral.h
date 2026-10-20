#ifndef __BACKEND_QUERY_LITERAL_H__
#define __BACKEND_QUERY_LITERAL_H__

////////////////////////////////////////////////////////////////////////////
//	Description : A QUERY'S CONSTANT AND THE ENGINE'S VALUE — the two doors between them
////////////////////////////////////////////////////////////////////////////
//
// The query's AST holds a constant as the core's ibQueryLiteral (core/query/queryLiteral.h): what the text wrote, or
// a value a run put there. The engine reads its own value back and puts its own in through these two.

#include "core/query/queryLiteral.h"
#include "backend/compiler/value.h"

// The engine's value of a query's constant — the held one when a run put it there, else the text's.
inline ibValue ibQueryLiteralValue(const ibQueryLiteral& literal)
{
	if (literal.GetHeld())
		return ibValue::FromAnyValue(literal.GetHeld());
	return ibValue(literal.GetText());
}

// A query's constant of an engine's value: a primitive is written as the text would write it, anything else (a
// reference, an enumeration) is held.
inline ibQueryLiteral ibQueryLiteralOf(const ibValue& value)
{
	switch (value.GetType()) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:    return ibQueryLiteral(ibLexValue(value.GetType()));
	case ibValueTypes::TYPE_BOOLEAN: return ibQueryLiteral(ibLexValue(value.GetBoolean()));
	case ibValueTypes::TYPE_NUMBER:  return ibQueryLiteral(ibLexValue(value.GetNumber()));
	case ibValueTypes::TYPE_DATE:    return ibQueryLiteral(ibLexValue(value.GetDate()));
	case ibValueTypes::TYPE_STRING:  return ibQueryLiteral(ibLexValue(wxString(value.GetString())));
	default:                         return ibQueryLiteral(ibAnyValuePtr<ibAnyValue>(value.ToAnyValue()));
	}
}

#endif
