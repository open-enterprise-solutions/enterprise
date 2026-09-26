#ifndef __CREATE_REQUEST_H__
#define __CREATE_REQUEST_H__

////////////////////////////////////////////////////////////////////////////
//	Description : WHAT AN OBJECT IS CREATED WITH — a form and, with the same
//	              request, the source object that form is built over.
//
//	⭐ IT IS A CREATION REQUEST, NOT A CHOICE ONE, AND THAT IS THE WHOLE POINT
//	(Max, 2026-09-23: "the request for creating a form — or for opening one — IS
//	the request for creating the object"). A choice is one of the things an
//	object is created FOR, so ibCreateRequest is a PART of it; a report's own
//	key parameters are another, and they go in the map rather than through a new
//	door.
//
//	⭐ HANDED OVER AT CREATION, AND KEPT NOWHERE. A dynamic list reads its
//	settings exactly once — when it is built — so a narrowing that arrives after
//	that reaches nobody, and a copy of the request kept beside the composer would
//	be a second answer to what the list shows.
//
//	Why these live in one file. The select mode, the condition and the two
//	requests were spread over three headers, so a request had to forward-declare
//	a type it carries and a reader had to open three files to see one subject
//	(Max: "everything to do with the request lives there"; "the choice one is
//	part of the form one, they will probably live together").
//
//	The select mode's SCRIPT FACE stayed behind in metaObjectEnum.h, with the
//	other enumerations a module can name: that is a different subject — what a
//	script may spell — and the registration and wire name are unchanged either
//	way.
////////////////////////////////////////////////////////////////////////////

#include "backend/compiler/value.h"    // a condition narrows ROWS by a parameter's runtime VALUE
#include "backend/uniqueKey.h"         // the key a form is opened under

#include <map>

// What a choice form is allowed to hand back — items, folders, or either. A hierarchical
// source is the one that can tell them apart; a flat one only ever answers with items.
enum ibSelectMode {
	ibSelectMode_Items = 1,
	ibSelectMode_Folders,
	ibSelectMode_FoldersAndItems
};

// ⭐ THE CONDITION A FIELD IS CHOSEN UNDER, ANSWERED ONCE. Four callers ask it — the choice form, the
// quick-choice drop-down, the adjustment of a value on write, and the filter of a list or column — and
// four answers would drift, on somebody else's configuration, months later
// (docs/private/choice-links.md § 7). ibChoiceLinkResolver produces it.
//
// ⭐ A CLASSIFICATION, NOT A FILTER. The two halves are different things: the type decides WHICH LIST
// opens, the filter decides WHAT IS SHOWN in it. A caller handed only a filter would have to re-derive
// the type from somewhere, and would get it wrong the first time the type was the only thing that
// changed.
struct ibChoiceCondition {

	// 🛑 A SETTLED TYPE STOOD HERE AND NARROWED NOTHING. The resolver computed it — what the governing
	// field settles the chosen field's type to — and the only reader it ever had was a report: a list is
	// narrowed by the parameters below (valueDynamicList.cpp), and the control has already chosen which
	// metaobject opens before it asks for a condition. What a field may HOLD is decided where the value
	// is written, by the one verb (ibValue::AdjustValue), so the second machinery for it is gone
	// (measured and removed 2026-09-24).

	// ⭐⭐ THE CHOICE PARAMETERS, AS THE PAIRS THEY ARE — a field of what is being chosen, and the
	// value it must equal. That is what an author wrote (`Owner ← Counterparty`) and what the list is
	// CREATED with; there is nothing in between for either end to get wrong.
	//
	// 🛑 THIS WAS AN ibFilterDescription, AND THAT WAS A ROAD TOO MANY. A filter description is what a
	// composer is BUILT FROM, so handing one to a list that already exists changes a description
	// nobody reads again — the composer had already been made from the old one, and nothing re-runs it
	// (Max, 2026-09-23: "the filters will not work, it is a composer; you have to pass these settings
	// at the moment the source object is formed"). The pairs go to `AddFilter` at creation, the same
	// door a folder-select list is born with its `IsFolder = true` through.
	//
	// Empty: nothing narrows the rows.
	std::map<wxString, ibValue> m_parameters;
};

// ⭐ EVERYTHING ONE CHOICE IS ASKED WITH, IN ONE ARGUMENT. `ProcessChoice` used to take the form name
// and the select mode side by side, and what narrows the list would have been a third — then a fourth,
// because this is the kind of question that grows: a choice is asked by a form field, a table cell, a
// filter row and a script, and each of them knows one more thing about it than the last. A structure
// moves that growth OFF the signature.
struct ibCreateRequest {

	// (The form's NAME is not here. Which form opens is a question about the FORM, not about the
	//  choice — a list form and an object form are named the same way — so it lives one storey up,
	//  on ibFormRequest.)

	// What may be chosen: items, folders, or both — the field's own declaration.
	ibSelectMode m_selectMode = ibSelectMode::ibSelectMode_Items;

	// ⭐ WHAT NARROWS THE LIST, HELD BY VALUE. This was a pointer, which is safe for exactly as long as
	// the call is synchronous — the condition lives on the stack of whoever is asking. A request
	// travels now: from the control that asks, through the metaobject, into the creation of a list. A
	// request is a message; a message carries its contents.
	ibChoiceCondition m_condition;

	ibCreateRequest() = default;
	ibCreateRequest(ibSelectMode selectMode, const ibChoiceCondition& condition = ibChoiceCondition())
		: m_selectMode(selectMode), m_condition(condition) {}
};

// ⭐⭐ WHAT A FORM IS OPENED WITH, IN THREE PARTS — and the three are three because they come from
// three different places (Max, 2026-09-23, laying it out):
//
//     the form's opening parameters
//       · the CREATION parameters — filters, owners
//       · the parameters each CUSTOM object makes up for itself
//
// …with the form's NAME above both, because which form opens is a question about the form and not
// about anything inside it.
//
// ⭐ THE PLATFORM KNOWS THE SHAPE OF THE FIRST and not of the second, and that is the whole reason
// they are apart. A choice narrowing is the same arrangement in every configuration ever written, so
// it is named and typed and a caller cannot spell it wrong. What a report hands its own form is not
// the platform's to enumerate, and a structure that tried would grow a member per idea.
struct ibFormRequest {

	// WHICH form. The one the author picked; empty = the metaobject's own.
	wxString m_formName;

	// …AND WHICH WINDOW OF IT — the key the form is opened under. A second opening under the same key
	// brings the window already open to the front instead of making another; empty = the key follows the
	// source (the ordinary case: one window per object). It travelled beside the request through every
	// form-opening signature, and it is an opening parameter like the name (Max, 2026-09-24).
	ibUniqueKey m_formGuid;

	// THE CREATION PARAMETERS — filters and owners. What narrows the source object this form is built
	// over; default-constructed when nothing is being chosen.
	ibCreateRequest m_create;

	// …and the parameters of whatever custom thing is being opened, by name: a report's own key, a
	// period to open in, a row to stand on. A script writes these as a Structure.
	std::map<wxString, ibValue> m_values;

	ibFormRequest() = default;

	// 🛑⭐⭐ EXPLICIT, AND IT HAD TO BECOME SO. Implicit, this constructor turned every `wxString` into
	// a request carrying NOTHING BUT A NAME — so `CreateAndBuildForm(request.m_formName, …)`, written
	// at twelve form-creation sites across the catalogs, all three charts, documents, enumerations and
	// jobs, compiled and read as if it passed the request on. It did not: the choice condition was
	// dropped at the door, every selection form was built without it, and a record created from a
	// narrowed list could not know what narrowed it (Max, 2026-09-23: "the new element cannot read the
	// filter that gave birth to it, and you said you did exactly this — I think you did nothing").
	//
	// A conversion is the wrong shape for this: a name is PART of a request, not another way of
	// spelling one, and the compiler is the only reader that checks every site. Spelling
	// `ibFormRequest(name)` where a name really is all there is costs one word and says so.
	explicit ibFormRequest(const wxString& formName) : m_formName(formName) {}
	ibFormRequest(const wxString& formName, const ibUniqueKey& formGuid)
		: m_formName(formName), m_formGuid(formGuid) {}
	ibFormRequest(const wxString& formName, const ibCreateRequest& create)
		: m_formName(formName), m_create(create) {}

	// A named value, or an empty one — a form asks for what it knows about and is not broken by a
	// caller that did not send it.
	ibValue Get(const wxString& name) const {
		const std::map<wxString, ibValue>::const_iterator found = m_values.find(name);
		return found != m_values.end() ? found->second : ibValue();
	}

	bool Has(const wxString& name) const { return m_values.find(name) != m_values.end(); }

	void Set(const wxString& name, const ibValue& value) { m_values[name] = value; }
};

#endif // !__CREATE_REQUEST_H__
