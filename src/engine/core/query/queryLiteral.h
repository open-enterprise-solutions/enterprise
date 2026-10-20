#ifndef __QUERY_LITERAL_H__
#define __QUERY_LITERAL_H__

////////////////////////////////////////////////////////////////////////////
//	Description : A CONSTANT OF A QUERY — what a token and an expression of the query's AST carry
////////////////////////////////////////////////////////////////////////////
//
// The query's lexer, parser and AST name no runtime: the thin client reads a query's text as the engine does, and
// it has no engine under it. So a constant is the core's:
//   * a constant the TEXT wrote (a number, a string, a date, True / False, Null, Undefined) is an ibLexValue, as a
//     module's constant is (the translator's);
//   * a constant a RUN put there (a LINQ capture folded in, a bytecode constant — a reference among them) is the
//     side's own value, held through the core's counted pointer: the engine reads its own back
//     (backend/query/queryLiteral.h).
// Whoever only reads what it says — a renderer, a check — reads Get(), whichever it is.
//
////////////////////////////////////////////////////////////////////////////

#include "core/compiler/lexValue.h"   // ibLexValue — a constant as text writes one
#include "core/anyValue.h"            // ibAnyValuePtr — a side's value, held

class CORE_API ibQueryLiteral {
public:

	ibQueryLiteral() = default;
	ibQueryLiteral(const ibLexValue& text) : m_text(text) {}
	explicit ibQueryLiteral(const ibAnyValuePtr<ibAnyValue>& held) : m_held(held) {}

	// What it says — the side's value when one is held, else what the text wrote.
	const ibAnyValue& Get() const { return m_held ? *m_held : static_cast<const ibAnyValue&>(m_text); }

	// The side's value, when a run put one here; empty for a constant the text wrote.
	const ibAnyValuePtr<ibAnyValue>& GetHeld() const { return m_held; }
	// The constant the text wrote (TYPE_EMPTY when a side's value is held).
	const ibLexValue& GetText() const { return m_text; }

	ibValueTypes GetType() const { return Get().GetType(); }
	bool GetBoolean() const { return Get().GetBoolean(); }
	ibNumber GetNumber() const { return Get().GetNumber(); }
	ibDateTime GetDate() const { return Get().GetDate(); }
	ibString GetString() const { return Get().GetString(); }

	// WRITTEN AS TEXT — what the lexer and the parser put here: false when the text is not one (ibLexValue's).
	void SetType(ibValueTypes type);   // Null / Undefined
	bool SetBoolean(const wxString& text);
	bool SetNumber(const wxString& text);
	bool SetDate(const wxString& text);
	bool SetString(const wxString& text);

private:

	ibLexValue                m_text;
	ibAnyValuePtr<ibAnyValue> m_held;
};

#endif
