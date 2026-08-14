#ifndef __QUERY_EXCEPTION_H__
#define __QUERY_EXCEPTION_H__

// THE QUERY ENGINE'S OWN REFUSALS — L3 to L5, and nothing below.
//
// These used to live in databaseLayer/databaseLayerException.h, beside the DBMS varieties, which put
// a floor's exceptions in another floor's header and invited exactly the confusion the types exist to
// prevent: "the database refused" and "we could not build a query" are different events with
// different audiences, and telling them apart is the entire point (docs/exceptions.md §3).
//
// The division of labour, top to bottom:
//
//   * the DATABASE refused           -> ibBackendDatabaseException (databaseLayerException.h)
//     A fault the engine committed and reported: deadlock, constraint, lost connection. Its Kind
//     carries the handling rule, and retry semantics belong to it alone.
//
//   * the QUERY LAYER refused        -> ibBackendQueryException, here
//     Nothing is wrong with the database; OUR machinery could not do its job. Deliberately NOT a
//     subclass of the database family, so it can never inherit "safe to retry" — retrying a
//     mistranslation is an infinite loop.
//
//   * the QUERY ITSELF is wrong      -> ibBackendQuerySourceException, here
//     The machinery is fine and the text is not. Shown to whoever WROTE the query, and it carries
//     the position so a consumer can point at it.
//
//   * RIGHTS refused                 -> ibBackendAccessException (backend_exception.h)
//     Not a malfunction. A query that reads what the user may not read raises THAT, not one of
//     these — the caller may legitimately carry on.
//
//   * the USER interrupted           -> ibBackendInterruptException (backend_exception.h)
//     Not an error at all.

#include "backend/backend_exception.h"

// ibBackendQueryException — the query layer could not do its job.
//
// Raised by L3 (lowering, providers), L5 (the composer) and the L2-facing translation step. The TYPE
// says which tier refused; the Kind says what about it. Sits beside the database family under
// ibBackendException rather than under it, so DB retry semantics never leak in.
class BACKEND_API ibBackendQueryException : public ibBackendException
{
public:
	enum class Kind {
		TranslationFailure,  // could not render / prepare the IR as SQL for this dialect
		UnsupportedNode,     // an IR node with no dialect form and no fallback
		// (NoConnection MOVED to ibBackendSessionException on 2026-08-14 — "there is no connection to
		//  work on" is the SESSION refusing, not this tier failing at its job, and leaving the kind
		//  here as well would have been two names for one refusal with nothing to choose between them.)
		PoolExhausted,       // leak / saturation (carries pool attribution later)
	};

	Kind GetKind() const { return m_kind; }

	[[noreturn]] static void Throw(Kind kind, const wxString& message);

protected:
	ibBackendQueryException(Kind kind, const wxString& msg)
		: ibBackendException(msg), m_kind(kind) {}

private:
	Kind m_kind;
};

// The lowering / parser raise through Error(), which takes the FORMAT and its arguments the way
// every other variety in this codebase does (ibBackendCoreException::Error). Formatting at the
// callsite and passing a finished string would work, but it reads differently from every neighbour
// and loses the compile-time format checking wx's vararg machinery gives.

// ibBackendQuerySourceException — THE QUERY ITSELF DOES NOT HOLD UP.
//
// Raised by L4 (lexer, parser) and by the L3 lowering when it meets something the text asked for and
// nothing can answer: a token that cannot be there, a name that resolves to nothing, a form that
// cannot mean anything.
//
// A SUBCLASS AND NOT MERELY A KIND, for two reasons that are both about the catch site. Telling
// kinds apart inside one handler reads `catch (const ibBackendQueryException& e) { if (e.GetKind()
// != …) throw; }` — and one forgotten rethrow there silences somebody else's failure, an exhausted
// pool swallowed by a query editor. Catching by TYPE needs no rethrow at all. And the audiences
// differ: this one is shown to the author of the query, the others to whoever runs the system.
//
// Both grains stay available — catch the base for "anything the query layer refused", catch this one
// for "the query is wrong".
class BACKEND_API ibBackendQuerySourceException : public ibBackendQueryException
{
public:
	// WHERE IN THE QUERY, AS DATA. The span rides as numbers so a consumer can point AT it: the query
	// editor putting a caret on the offending token, a language service returning a diagnostic range.
	// It is in the message too, for a human reading a log — but a caller holding only the message
	// would have to parse a translated sentence to get the number back, and that breaks the first
	// time the wording changes. Zero = the position is not known.
	unsigned int GetLine()   const { return m_line; }
	unsigned int GetColumn() const { return m_col; }

	// The span first, then the format and its arguments — the same vararg shape the other varieties
	// use, so a callsite reads like every other raise in the codebase.
	WX_DEFINE_VARARG_FUNC(static void, ErrorAt, 3, (unsigned int, unsigned int, const wxFormatErrorString&),
		DoErrorAtWchar, DoErrorAtUtf8);

	// No position known (a name that resolves to nothing has no token behind it by the time L3 finds out).
	WX_DEFINE_VARARG_FUNC(static void, Error, 1, (const wxFormatErrorString&),
		DoErrorWchar, DoErrorUtf8);

private:
	ibBackendQuerySourceException(const wxString& msg, unsigned int line, unsigned int col)
		: ibBackendQueryException(Kind::TranslationFailure, msg), m_line(line), m_col(col) {}

#if !wxUSE_UTF8_LOCALE_ONLY
	static void DoErrorAtWchar(unsigned int line, unsigned int col, const wxChar* format, ...);
	static void DoErrorWchar(const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	static void DoErrorAtUtf8(unsigned int line, unsigned int col, const wxChar* format, ...);
	static void DoErrorUtf8(const wxChar* format, ...);
#endif

	unsigned int m_line = 0;
	unsigned int m_col  = 0;
};

// ibBackendQueryLinqException — THE PIPELINE REFUSED. The other L4 door: a query written as a chain
// of operations over a source rather than as text. What it rejects is shaped differently from what a
// parser rejects — not "this token cannot be here" but "this stage was handed something it cannot
// work with": a key selector that is not a function, a value that is not iterable, a lambda short of
// the arguments the stage passes it.
//
// It is a sibling of ibBackendQuerySourceException, not the same class, because the two doors fail
// at different things and are shown to their author differently: a text query has a position to put
// a caret on, a pipeline has a STAGE. And it is a subclass of the query family, so "anything the
// query layer refused" still catches both with one handler.
//
// It replaces a classification that already existed and was written by hand: all 39 refusals in
// procUnitLinq spelled a literal "LINQ: " prefix into their message. A prefix repeated at every
// callsite is a type wearing a disguise — it cannot be caught, cannot be checked by the compiler,
// and stays correct only as long as nobody forgets it.
class BACKEND_API ibBackendQueryLinqException : public ibBackendQueryException
{
public:
	WX_DEFINE_VARARG_FUNC(static void, Error, 1, (const wxFormatErrorString&),
		DoErrorWchar, DoErrorUtf8);

private:
	explicit ibBackendQueryLinqException(const wxString& msg)
		: ibBackendQueryException(Kind::UnsupportedNode, msg) {}

#if !wxUSE_UTF8_LOCALE_ONLY
	static void DoErrorWchar(const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	static void DoErrorUtf8(const wxChar* format, ...);
#endif
};

#endif // __QUERY_EXCEPTION_H__
