#ifndef __QUERY_CONSTRUCTOR_MODEL_H__
#define __QUERY_CONSTRUCTOR_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// L4-1 — the query constructor's MODEL half.
////////////////////////////////////////////////////////////////////////////
//
// The constructor is a frontend VIEW over a backend MODEL, and this is the model: what may go
// into a query (the sources, their fields, their types). It lives here because it is METADATA,
// and metadata is the backend's — the shell above it is wx code that knows nothing beyond what
// this hands it.
//
// TWO RULES, and both are the reason this file exists rather than a helper in the dialog:
//
//   1. THE CATALOGUE IS A WALK, NEVER A LIST. The sources come from ibQueryableFactory, which is
//      where a metaobject REGISTERS itself when it runs. A written-out list of metaclasses would
//      make the next metatype invisible in the constructor exactly as a written-out list made it
//      invisible in the role editor (2026-08-06) — the same defect, one layer up.
//
//   2. FIELDS ARE ASKED OF THE SOURCE. A real source answers through its descriptor
//      (FillSourceExplorer — the same call a dynamic list fills its columns with); a NESTED table
//      (a subquery standing where a table would) answers with its own projections. Both are
//      answers to one question, so the shell asks the SOURCE which it is rather than branching on
//      a type — and a source kind added later needs no new branch up in the dialog.
//
// ⭐ WHAT IT DERIVES IS THE CORE'S (core/query/anyQueryConstructorModel.h) — the walk of a path, the fields of a
// select, a CAST's type: the client's model derives the same over the server's answers, so it is written once. What is
// here is what only the configuration answers — the catalogue, a virtual table's parameters, a condition's fields — and
// the four questions the core asks of a table, answered by the descriptors.
//
// See docs/private/query-constructor.md §4 / §5a / §5b.
//
////////////////////////////////////////////////////////////////////////////

#include "core/query/anyQueryConstructorModel.h"   // ibAnyQueryConstructorModel — what the model derives
#include "queryableFactory.h"

#include <vector>

class ibMetaData;

class BACKEND_API ibQueryConstructorModel : public ibAnyQueryConstructorModel
{
public:
	// `metaData` — the config the query runs on behalf of (its factory resolves the sources).
	// Null falls back to the global factory, which is the right answer with no config open.
	explicit ibQueryConstructorModel(const ibMetaData* metaData = nullptr);

	// EVERY source a query may name, grouped by nothing — the caller groups by m_path[0], which is
	// the metaclass kind, so the tree's top level is whatever the factory actually holds.
	std::vector<ibQueryConstructorSource> GetSources() const;

	// WHAT ARGUMENTS THIS SOURCE TAKES, in order — empty for an ordinary table, non-empty for a
	// virtual one (a register's balance / turnovers, an information register's slice). The shell
	// shows a parameter window only when this answers something, so a table that takes none never
	// grows a menu item about parameters it does not have.
	std::vector<ibQuerySourceParameter> GetSourceParameters(const ibQuerySource& source) const;

	// THE FIELDS A CONDITION PARAMETER MAY NAME — asked of the source, not decided here. A balance
	// answers with its dimensions only (a resource it folded cannot be filtered before folding); a
	// slice answers with everything it carries. Same shape as GetFields, so the expression editor
	// takes it without knowing which source it came from.
	// Fields admissible in a condition slot. `slot` empty = the general `Condition`; a named slot may
	// narrow it (an accounting register's account conditions admit ACCOUNTS only — the source consumes
	// them and folds by them, so a leaf about anything else cannot be applied at all).
	std::vector<ibQueryConstructorField> GetConditionFields(const ibQuerySource& source,
	                                                       const wxString& slot = wxEmptyString) const;

	// A TABLE'S OWN REFERENCE TYPE — its metaobject's empty reference, asked of the queryable the factory resolves;
	// 0 for a virtual table (a third segment) and for a table no reference points at.
	virtual ibClassID ReferenceOfTable(const std::vector<wxString>& path) const override;
	// …AND THE TABLE A REFERENCE NAMES — the descriptor the factory holds under the class's metaID.
	virtual wxString TableOfReference(ibClassID reference) const override;

protected:

	// A TABLE OF THE CONFIGURATION answers through its descriptor (FillSourceExplorer) — the very call a dynamic list
	// fills its columns with — asked with the arguments the FROM writes, by its id with none.
	virtual std::vector<ibQueryConstructorField> GetTableFields(const ibQuerySource& source) const override;
	virtual std::vector<ibQueryConstructorField> GetTableFields(ibMetaID table) const override;
	// The engine reads the class itself (clsid.h — the kind is in the id): a reference with a descriptor behind it.
	virtual std::vector<ibMetaID> ConvertToMetaIds(const std::vector<ibClassID>& clsids) const override;

private:
	ibQueryableFactory* Factory() const;

	const ibMetaData* m_metaData = nullptr;
};

// ⭐⭐ WHAT FIELDS A QUERY TEXT OFFERS — A TEXT AND A CONFIGURATION, AND NOTHING ELSE.
//
// This is the whole of what "resolve the source" means for anything that edits a query rather than
// runs one. It parses the package, takes the LAST statement — the one that produces the result, a
// package before it being temp tables handed in as context — and asks the model above for its
// fields.
//
// It lives HERE because that is the tier the question belongs to. It used to be a method on
// ibValueDataComposition, so a settings window had to hold a RUNNING composition to ask it — a
// runtime object with a source binding, a sheet and a fetch, borrowed from whatever host happened
// to have one — in order to learn something the text and the configuration already answer.
//
// `error` takes the parser's complaint when the caller shows one. Half-typed text is not a failure:
// it offers no fields YET, so the list comes back empty and the caller decides whether that is worth
// saying out loud.
BACKEND_API std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& text,
	const ibMetaData* metaData, wxString* error = nullptr);

#endif
