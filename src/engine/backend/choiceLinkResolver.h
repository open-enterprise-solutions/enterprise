#ifndef __CHOICE_LINK_RESOLVER_H__
#define __CHOICE_LINK_RESOLVER_H__

#include "core/description/choiceLinkDescription.h"
#include "backend/createRequest.h"   // ibSelectMode, ibCreateRequest — what a choice is asked with (ibChoiceCondition, what this produces, is value.h's)
#include "backend/tabularDataObject.h"              // the TABLE shape of a source, its row hop, and ibDataViewItem
                                                    // complete — the holder below keeps one BY VALUE

class BACKEND_API ibSourceDataObject;

// ⭐⭐ WHO HOLDS THE FIELDS — IN THE TWO SHAPES THE TREE ALREADY KEEPS, AND NO THIRD. A source comes
// either scalar (ibSourceDataObject — an object, a form's source) or tabular (ibTabularDataObject plus
// the row), and that split is the tree's own: each of the two declares its own GetValueByPath, and
// tabularDataObject.h calls itself "the TABLE mirror of ibSourceDataObject" in as many words.
//
// 🛑 SO THIS IS NOT A THIRD SOURCE TYPE. It holds one of the two and asks it; it stores no path
// convention, adds nothing to a control and nothing to the schema. The caller builds it from what it
// already has — a form hands its source, a table column hands its model and the row being edited — and
// everything below is written once (Max, 2026-09-23: "converge the roads, I don't want to maintain two").
struct BACKEND_API ibChoiceHolder {

	ibSourceDataObject* m_source = nullptr;    // the object — for a table column, the one above the row
	ibTabularDataObject* m_table = nullptr;    // the table and the row, where the holder is a row
	ibDataViewItem m_row;

	ibChoiceHolder() = default;
	explicit ibChoiceHolder(ibSourceDataObject* source) : m_source(source) {}
	ibChoiceHolder(ibTabularDataObject* table, const ibDataViewItem& row) : m_table(table), m_row(row) {}

	bool IsOk() const { return m_source != nullptr || m_table != nullptr; }

	// The value a link's path names, and the empty value written back into a field it governs.
	bool GetValue(const ibSourceDescription& path, ibValue& value) const;
	bool SetValue(const ibMetaID& id, const ibValue& value);

	// ⭐⭐ WHAT A FIELD STANDING HERE SAYS — by id, the ROW FIRST and the object after it, as GetValue reads:
	// a column is the row's, and a column may also be bound to a field of the object above it. The source
	// answers for its own fields (ibSourceDataObject::GetChoiceParameters) and says nothing of one it has
	// not; ids are the configuration's own, so a row saying nothing is a row not having the field.
	const ibChoiceParametersDescription& GetChoiceParameters(const ibMetaID& id) const;
	const ibTranslateString& GetFormat(const ibMetaID& id) const;
	ibSelectMode GetSelectMode(const ibMetaID& id) const;

	const class ibMetaData* GetMetaData() const;
};

// ⭐⭐ THE VALUES COME FROM THE HOLDER, AND THE HOLDER IS THE CALLER'S. A link's path is an
// ibSourceDescription — the same hops a bound control walks — so resolving it is asking that source for
// the path through the door it already answers (`GetValueByPath`). The hops are the same whether the
// holder is an object, a form's source or a row of a table.
//
// 🛑 A SECOND WAY OF READING A PATH WAS WRITTEN HERE AND TAKEN OUT AGAIN (2026-09-23). It lived on the
// CONTROL — a pair of verbs so that a table column could answer from the row it was editing — and it
// dragged value fetching onto the type factory, which is the SCHEMA and must know nothing about runtime
// values. Both verbs are gone, backend_type.h is untouched, and the two shapes a source really comes in
// are the tree's own (ibChoiceHolder above).
class BACKEND_API ibChoiceLinkResolver {
public:

	// ⭐⭐ WHICH FIELDS CAN GOVERN, SAID ONCE — by the type the field declares. Any field that has a type
	// can: whatever stands in it knows what it brings a value to — a type description by what it
	// describes, a chosen kind by its own `Type`, anything else by its own class (ibValue::AdjustValue).
	// Only a field declared with no type at all has nothing to give. The designer offers exactly the
	// fields that answer true.
	static bool CanGovern(const ibTypeDescription& type);

	// One condition per row, over the field of the target the row names — by
	// whatever the row's source holds, empty included.
	static bool ResolveParameters(const ibChoiceParametersDescription& params, const ibChoiceHolder& holder,
		ibChoiceCondition& condition);

	// ⭐⭐ WHAT NARROWS A CHOICE, ASKED OF THE FIELD — the parameters, which say what is shown in the list.
	//
	// 🛑 THE LINK BY TYPE IS NOT HERE, AND NOT BECAUSE IT WAS FORGOTTEN. It used to settle a TYPE into
	// this condition, and that type narrowed nothing: the list reads the parameters and nothing else
	// (valueDynamicList.cpp), and the control has already chosen which metaobject opens before it asks.
	// A link by type decides what a field may HOLD, and that is answered where the value is written —
	// once, by the one verb (Adjust below, ibValue::AdjustValue). Two machineries said one thing, and
	// the one that said it to nobody is gone (Max, 2026-09-24: "tear it out").
	//
	// ⭐ THE FIELD IS NAMED BY ID, and the holder's source answers its parameters: they ARE the field's, so
	// a caller that takes them off it and passes the pieces is doing the field's arithmetic in a window
	// (Max, 2026-09-23, reading the type control). A binding names its field by its LEAF
	// (ibSourceDescription::GetLeaf).
	//
	// A field the holder does not answer for — a control bound to nothing, a filter cell, a script's
	// value — carries no condition, which is the honest answer there rather than a refusal.
	static ibChoiceCondition Resolve(const ibChoiceHolder& holder, const ibMetaID& field);
	// ⭐ …AND A CONTROL'S OWN PARAMETERS OVER THE FIELD'S (#616): a field reached through the dot has no attribute to
	// declare them, so its control does — in the form's paths, read in `paramsHolder`. A parameter both name is narrowed
	// by the control's, the nearer word. Asked by every road a control's value is found by — its choice form, its quick
	// choice and the text typed into it — so typing finds no more than the form shows (Max, 2026-11-09).
	static ibChoiceCondition Resolve(const ibChoiceHolder& holder, const ibMetaID& field,
		const ibChoiceParametersDescription& params, const ibChoiceHolder& paramsHolder);

	// ⭐⭐ A NEW RECORD MADE IN A NARROWED LIST IS BORN MATCHING IT. The list a person pressed Add in
	// shows only contracts of one counterparty; a new row without that counterparty is not merely
	// inconvenient, it VANISHES from the list the moment it is written — created in front of somebody
	// and then not there (Max, 2026-09-23: "make it so the filters are substituted in").
	//
	// ⭐ AND NOTHING NEW IS NEEDED FOR IT. A choice parameter already IS the pair `field = value`, which
	// is exactly what filling is; the request the list was made with is sitting on its form. Every pair
	// is written — the owner, a flag, whatever the parameters name — where the new record has the field.
	static void Fill(ibChoiceHolder& holder, const ibCreateRequest& request);

	// IS A FIELD LINKED TO `edited` — its value chosen within it, read off what governs the field: its link by type,
	// or a parameter row that does not say Keep. The rule ClearLinked empties by, said once, BY PATH: the whole path
	// names a field, and two reached through the dot may end in one id. Asked by a source of each of its own fields
	// when it lists the linked ones (ibSourceDataObject::GetLinked), its field's id one hop; and of a control's own
	// links, in the form's paths (ibTypeControlFactory::ClearLinked). A field is not linked to itself — the asker,
	// who knows which field it is asking about, passes that one over.
	static bool IsLinked(const ibChoiceTypeLinkDescription& link, const ibChoiceParametersDescription& params,
		const ibSourceDescription& edited);

	// ⭐⭐ A VALUE WAS WRITTEN INTO ONE OF THIS SOURCE'S FIELDS — and every field chosen WITHIN the old
	// one is now a contract with the previous counterparty. They are emptied, to the empty value of
	// their own type, unless their row says to keep them (Max, 2026-09-23: "clear it, of course — set
	// the default value").
	//
	// ⭐ THE SOURCE IS THE HOLDER, so a form field and a table cell take the same road: the form's
	// object for the one, the row for the other, and this is told neither apart.
	//
	// ⭐ AND IT FOLLOWS THROUGH: a field emptied here may itself govern a third, whose value is now just
	// as stale. The walk carries on until nothing more is emptied — a chain nobody would think to write
	// a handler for, and the first one a person builds by accident.
	//
	// ⭐ AND A HEADER FIELD REACHES THE ROWS: where the holder is the object, every row of a tabular section
	// whose column is chosen within the edited field takes the same step, with the row as the holder.
	static void ClearLinked(ibChoiceHolder& holder, const ibMetaID& edited);

	// ⭐⭐ THE VALUE, ADJUSTED TO WHAT ITS LINK SETTLES ON — the ONE line a holder writes through. A
	// field's CONTOUR (what it may ever hold) is the attribute's own answer and needs nobody's help;
	// the CONCRETE type is whatever stands in the governing field at this moment, which only the holder
	// can read. Here, so that a value written by a script, by a record set on the server or by a person
	// in a form all pass the same narrowing.
	//
	// 🛑 IT WAS PUT ON THE CONTROL FIRST, and that is a rule the server does not have: a form is one
	// way a value arrives and not the way it arrives most (Max, 2026-09-23: "you have done it for the
	// control — and if I do it at runtime, on the server?"). Where the data is, not where the window is.
	//
	// ⭐ THE LINK IS HANDED IN BY WHOEVER WRITES, who has just found the field it writes and its link with
	// it: asked again here by id, the field was looked up twice more on every write of every cell.
	//
	// True: the link settled the value, `adjusted` is it — and nothing narrows it after (see the body). False:
	// no link, or nothing chosen in the governing field — the field's own ordinary adjustment applies, and
	// that is the writer's, who holds the field.
	static bool Adjust(const ibChoiceHolder& holder, const ibChoiceTypeLinkDescription& link,
		const ibValue& value, ibValue& adjusted);
};

#endif
