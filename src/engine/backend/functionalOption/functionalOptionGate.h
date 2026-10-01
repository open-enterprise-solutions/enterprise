#ifndef __FUNCTIONAL_OPTION_GATE_H__
#define __FUNCTIONAL_OPTION_GATE_H__

// ⭐⭐ IS THIS AVAILABLE IN THIS BASE — the question a functional option answers, and the only one. One
// word for it everywhere: an object, a column, a control, a table, a filter field is AVAILABLE, or it is
// not — offered by the interface, or neither shown nor selectable there.
//
// There are three ways for a part of the configuration not to be there, and they are three different
// questions with three different owners:
//
//   deleted     — the author removed it                     (IsDeleted: gone from the structure)
//   disabled    — its owner's declaration switches it off   (metaDisableFlag: no column, no query sees it)
//   UNAVAILABLE — this base does not use it                 (here: the column, the data and every query stay)
//
// The third is NOT the first two. A switched-off option leaves its objects in the metadata and in the
// schema, so a query naming one of its fields still runs and the data written before is still there when
// the option comes back on. Folding it into IsAllowed would have dropped the column at the next
// restructuring and made every query that names it fail — which is exactly why it is a question of its
// own, asked by the places that OFFER things: a form's controls, a list's columns, a section's items.
//
// ⭐ ONE VALUE PER BASE, READ WITH NO RIGHT ASKED. What the interface offers is a fact of the installation,
// the same for every user; what a user may see is the roles' question, and a refusal. An option never
// refuses anything — it only does not offer it.
//
// ⭐ THE DESIGNER SEES EVERYTHING. The gate answers "available" there, and wherever there is no application
// at all (a test), so nothing an author edits can disappear from under them. A composition's run is the one
// exception: what it prints is what a person sees, so it asks as the application (AsApplication, below).
//
// The values are read once per open configuration and kept; the write door of an option drops them
// (OnAfterValueWrite), so the next form built in this process follows the new value. Another client reads
// it at its next start — no timer, so the moment a change arrives is one anybody can name. The designer
// reads them at the start of each run it composes as the application (AsApplication, below).

#include "backend/backend_core.h"   // ibMetaID

#include <map>
#include <set>

class BACKEND_API ibFunctionalOptionGate {
public:

	// ⭐ THE RULE, and nothing else — no metadata, no database, so it can be pinned by a test.
	//
	// A member of no option is available. A member of options is available while ANY of them is on: a field
	// two parts of the system use is needed while either of them is used. An option the configuration does
	// not have (deleted since the member was saved) counts for nothing either way.
	static bool IsMemberAvailable(const std::set<ibMetaID>& memberOptions, const std::map<ibMetaID, bool>& optionValues);

	// ⭐ AN OPTION WORKS ONLY UNDER THE ONE IT REQUIRES — pure as well. Its effective value is its own AND the
	// effective value of what it requires, along the whole chain. A chain that comes back to itself stops
	// there (the options in the loop are simply ANDed); a requirement the configuration does not have counts
	// for nothing. `required` maps an option to the one it requires; an option absent from it requires none.
	static std::map<ibMetaID, bool> EffectiveValues(const std::map<ibMetaID, bool>& own,
		const std::map<ibMetaID, ibMetaID>& required);

	// ⭐ A FIELD WHOSE EVERY TYPE IS AN UNAVAILABLE OBJECT goes with them — the `Contract` of an invoice when the
	// catalog of contracts is not used, with nobody listing the field itself. A type that names no single
	// object (a primitive, a family such as `CatalogRef`, a characteristic, an external object) keeps the field.
	static bool IsTypeAvailable(const class ibMetaData* metaData, const struct ibTypeDescription& typeDesc);

	// Available, unless it or anything above it (an attribute's object) is unavailable in this base.
	//
	// ⚠ ASKED OF A METAOBJECT, never of a number. A binding's path is not a list of configuration ids: its
	// first step is a FORM attribute, numbered 1, 2, 3 within the form, and a value table's columns are
	// numbered within the table — either collides with a configuration metaID sooner or later. So a control
	// asks the column it is bound to, and the column asks its metaobject (ibBackendSourceColumn::IsAvailable).
	static bool IsAvailable(const class ibValueMetaObject* object);

	// An element of a form that names its options itself (ibPropertyFunctionalOptions): available while any
	// of them is on, by the same rule; an empty list is available whatever the options are.
	static bool IsAvailable(const class ibMetaData* metaData, const std::set<ibMetaID>& options);

	// Whether anything at all is unavailable in this configuration — the cheap question a control asks
	// before it walks its source for the column to ask the real one. False in the designer, but for a run.
	static bool AnyUnavailable(const class ibMetaData* metaData);

	// What was answered about this configuration is dropped; the next question reads the values again.
	static void Forget(const class ibMetaData* metaData);

	// ⭐ THE APPLICATION'S VIEW, INSIDE THE DESIGNER — for what PRODUCES what a person sees: a composition's run,
	// which opens it itself (ibDataDBComposer::RefreshSourceFields), whoever called the run. While one is
	// open on this thread the gate answers as the running application does, not with "the designer sees
	// everything" — which stays the rule for what an author edits. In the designer the outermost scope reads
	// the values afresh: the base as it stands now, not as it stood when the designer first asked. A scope, so
	// it cannot outlive its use.
	class BACKEND_API AsApplication {
	public:
		AsApplication();
		~AsApplication();
		AsApplication(const AsApplication&) = delete;
		AsApplication& operator=(const AsApplication&) = delete;
	};
};

#endif // !__FUNCTIONAL_OPTION_GATE_H__
