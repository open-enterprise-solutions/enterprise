#include "choiceLinkResolver.h"

#include "backend/metaData.h"
#include "backend/metaCollection/partial/reference/reference.h"          // the reference a governing field holds
#include "backend/metaCollection/attribute/metaAttributeObject.h"        // a field answers what governs it
#include "backend/system/value/valueType.h"                              // ibValueTypeDescription
#include "backend/srcDataObject.h"                                       // the path door every source answers
#include "backend/backend_type.h"                                        // ibBackendTypeSourceFactory — the binding that names the field
#include "backend/diagnostics/journal.h"                                 // ibJournal — see the note below

// ⭐⭐ THIS ROAD SAYS WHAT IT DECIDED, AND IT DID NOT (2026-09-23). Four reports came in on one evening —
// "the value does not react", "every other time", "the substitution does not work", "the characteristic
// is written empty" — and the journal held nothing from here at all: db, query, docview, ui.form, and
// not one line about a link. There was nothing to read, so every one of them had to be guessed at.
//
// Which is the wrong way round for a mechanism whose whole subject is a value read SOMEWHERE ELSE: the
// answer depends on which holder was handed in and what stood in a neighbouring field at that instant,
// and neither survives to be looked at afterwards. So each decision says what it decided, once, with
// the ids in it — Debug only, compiled away entirely in Release (Max: "you have a technology journal,
// you can check why").

// THE ONE WAY A LINK READS A VALUE — the holder's own path door, the same one a bound control reads
// through, so a link sees exactly what the form shows rather than a second opinion of it. Each shape
// answers with the walker it already declares: the scalar one from itself, the table from its row.
bool ibChoiceHolder::GetValue(const ibSourceDescription& path, ibValue& value) const
{
	if (!path.IsOk())
		return false;

	// ⭐⭐ THE ROW FIRST, THE OBJECT AFTER IT — and a column holds BOTH. A link by type is simply the
	// choice of a field, of the tabular section or of the header (Max, 2026-09-23), and those are two
	// different holders: the neighbouring cell lives in the row, the organisation lives in the object
	// above it. A column fills both slots, so a path that names neither its own row nor nothing falls
	// through to the object rather than being answered with an empty value.
	//
	// ⚠ IN THIS ORDER, and it matters: a section's column and a header attribute may share an id only
	// by accident, and the row is what the person is standing in.
	if (m_table != nullptr && m_table->GetValueByPath(m_row, path.m_listSource, 0, value))
		return true;

	return m_source != nullptr && m_source->GetValueByPath(path.m_listSource, value);
}

// A field of THIS holder, by id — one hop, through the hop gate both shapes declare.
// ⚠ AND WRITING GOES TO THE ROW ONLY, where a row is what the holder is. Clearing follows an edit made
// IN a row, and a column emptying a field of the object above it would reach across every other row —
// what that field was chosen within is a question for the control standing on it.
bool ibChoiceHolder::SetValue(const ibMetaID& id, const ibValue& value)
{
	const ibSourceHop hop{ id };
	if (m_table != nullptr)
		return m_table->SetValueBySourceHop(m_row, hop, value);
	return m_source != nullptr && m_source->SetValueBySourceHop(hop, value);
}

// ⭐ THE ROW FIRST, AS IN GetValue ABOVE. A column's holder carries both the row it edits and the form's
// object over it, and the fields standing HERE are the row's — the columns. This answered with the
// object whenever there was one, so a column looking for its own field searched the DOCUMENT's attributes,
// found nothing, and fell back to its bare declaration: a characteristic column offered no type to choose
// and drew `False` as blank (2026-09-23: the table's value column was looked for among the document's
// fields and not found, with the row standing right there as its holder).
const ibValueMetaObjectCompositeData* ibChoiceHolder::GetFields() const
{
	if (m_table != nullptr)
		return m_table->GetSourceMetaObject();
	return m_source != nullptr ? m_source->GetSourceMetaObject() : nullptr;
}

const ibMetaData* ibChoiceHolder::GetMetaData() const
{
	if (m_table != nullptr)
		return m_table->GetSourceMetaData();
	return m_source != nullptr ? m_source->GetSourceMetaData() : nullptr;
}

namespace {

// THE FIELD A PATH NAMES, among the holder's own — the leaf hop is the field, the hops before it are
// how it was reached. Null when the path leaves this holder or names something gone.
const ibValueMetaObjectAttributeBase* FieldOfPath(const ibChoiceHolder& holder, const ibSourceDescription& path)
{
	if (!path.IsOk())
		return nullptr;

	const ibMetaID named = path.GetLeaf();
	const auto findIn = [named](const ibValueMetaObjectCompositeData* fields) -> const ibValueMetaObjectAttributeBase* {
		if (fields == nullptr)
			return nullptr;
		for (const ibValueMetaObjectAttributeBase* field : fields->GetGenericAttributeArrayObject()) {
			if (field->GetMetaID() == named)
				return field;
		}
		return nullptr;
	};

	// The holder's own fields — the row's, where there is a row — and then the object above it: a column may
	// be bound to a header attribute, and a link may name one, in the same order GetValue reads them.
	if (const ibValueMetaObjectAttributeBase* field = findIn(holder.GetFields()))
		return field;
	return holder.m_table != nullptr && holder.m_source != nullptr
		? findIn(holder.m_source->GetSourceMetaObject()) : nullptr;
}

}

const ibValueMetaObjectAttributeBase* ibChoiceLinkResolver::FieldOf(const ibChoiceHolder& holder,
	const ibBackendTypeSourceFactory* bound)
{
	if (bound == nullptr)
		return nullptr;

	// ⭐ A DOTTED PATH — `Counterparty.Parent`. The leaf lives inside what the head refers to, and the
	// binding's own walk is what resolves it; for a metadata source it hands back the attribute itself.
	if (const ibValueMetaObjectAttributeBase* leaf =
		dynamic_cast<const ibValueMetaObjectAttributeBase*>(bound->WalkSource(bound->GetSourceDesc())))
		return leaf;

	// 🛑 …AND A WHOLE-ATTRIBUTE BINDING ANSWERS NULL TO THAT WALK, BY CONTRACT — "null = a whole-
	// attribute binding (length 1) or a broken path" (backend_type.h). Which is EVERY ordinary field on
	// a form: a one-hop path. Asking only the walk therefore found nothing for the commonest binding
	// there is, so the choice of a contract opened with no owner condition at all — no refusal, no
	// message, the whole list (measured 2026-09-23, standing in ibCreateHierarchyList with the
	// parameters empty).
	//
	// The leaf of a one-hop path IS its head, and it stands among the HOLDER's own fields — which is
	// the SAME question a link's path asks, so it is the same lookup and not a second spelling of it.
	// The tree already answers this pair both ways in one breath: ibVariantDataSource::
	// RefreshTypeFromSource takes the walked leaf's type, or the head's when there is no leaf.
	return FieldOfPath(holder, bound->GetSourceDesc());
}

bool ibChoiceLinkResolver::CanGovern(const ibValueMetaObjectAttributeBase* field)
{
	if (field == nullptr || field->IsDeleted())
		return false;

	// ⭐⭐ A FIELD CAN GIVE ITS TYPE IF IT HAS ONE, AND THAT IS THE WHOLE TEST. A link by type is the
	// choice of the COLUMN the type is pulled from (Max, 2026-09-23: "a link by type — I just point at
	// the column I pull it from, and I have never once managed to do it"). What is pulled is
	// `GetTypeValueDesc`: for an ordinary field its own declaration, for a characteristic the list its
	// chart declares, and for a field holding a type description whatever it holds.
	//
	// 🛑 THIS REFUSED EVERYTHING BUT A CHARACTERISTIC, TWICE OVER — first by naming the chart's class,
	// then by asking whether a field's two type answers differ. Both were the same mistake wearing
	// different clothes: they made the mechanism be ABOUT characteristics, and on a configuration with
	// no characteristic beside the field the list came up empty, so the link could not be set at all.
	// A refusal that cannot be satisfied is not a safeguard.
	//
	// An empty type description is the one thing that cannot be pulled: a field that holds nothing has
	// nothing to give.
	return !field->IsEmptyTypeDesc();
}

namespace {

// ⭐ WHAT THE GOVERNING VALUE SAYS, READ THE WAY THE REGISTER ALREADY READS IT. The accounting register has
// been doing exactly this on WRITE since the day account dimensions were addressed by kind: the kind's
// own `Type` attribute IS a description of types, reached by the id the chart declares for it
// (accountingRegisterObject.cpp). This is that reading, where the CHOICE can ask it too — the same
// answer at the moment a person fills the field and at the moment the value is stored.
//
// ⚠ `said` — the journal hears the answer at the moment somebody CHOOSES, not on every write. A posting
// pass narrows a value per cell, and in Debug the journal is a file: a line per write was ten thousand
// lines for ten thousand rows and a good part of their time (2026-09-24, the write bench).
bool SettledType(const ibChoiceTypeLinkDescription& link, const ibChoiceHolder& holder, ibTypeDescription& settled,
	bool said)
{
	ibValue governing;
	if (!holder.GetValue(link.m_source, governing) || governing.IsEmpty()) {
		// ⭐⭐ NOTHING IS CHOSEN THERE, SO NOTHING NARROWS — the link is as good as absent, and the field
		// takes every type its own declaration admits: a characteristic, everything its chart declares
		// (Max, 2026-09-23: "if the characteristic sees the linked field is empty, it accepts all the data
		// types there are among the kinds of characteristics").
		if (said)
			ibJournalInfo(wxT("choice.type"), wxT("governing #%d holds nothing - the field takes its whole contour"),
				link.m_source.GetLeaf());
		return false;
	}

	// The field holds a type description outright.
	ibValueTypeDescription* held = nullptr;
	if (governing.ConvertToValue(held) && held != nullptr) {
		settled = held->m_typeDesc;
		return true;
	}

	// ⭐⭐ …AND THE THING THAT WAS CHOSEN NARROWS THE CONTOUR TO ONE ANSWER — WHATEVER IT IS. A reference
	// standing in the governing field is asked for the field OF ITS OWN that holds a type description,
	// and that is the answer: what this field may hold. A chart of characteristic types is one such kind,
	// not the mechanism — a catalogue of barcode kinds with a type attribute is the same arrangement
	// (Max, 2026-09-23: "a characteristic is simply a special case").
	//
	// ⚠ A kind that says nothing settles nothing: the field keeps its own contour.
	ibValueReferenceDataObject* reference = nullptr;
	if (!governing.ConvertToValue(reference) || reference == nullptr)
		return false;

	const ibValueMetaObjectGenericData* kind = reference->GetSourceMetaObject();
	if (kind == nullptr)
		return false;

	for (const ibValueMetaObjectAttributeBase* field : kind->GetGenericAttributeArrayObject()) {
		if (field->IsDeleted() || !field->GetTypeDesc().ContainType(g_valueTypeDescriptionCLSID))
			continue;

		ibValue declared;
		reference->GetValueByMetaID(field->GetMetaID(), declared);

		ibValueTypeDescription* limit = nullptr;
		if (!declared.ConvertToValue(limit) || limit == nullptr || !limit->m_typeDesc.IsOk())
			continue;   // this kind carries the field but has not been told a type: nothing to narrow by

		if (said)
			ibJournalInfo(wxT("choice.type"), wxT("governing #%d ('%s') declares %u type(s) through '%s'"),
				link.m_source.GetLeaf(), kind->GetName(), limit->m_typeDesc.GetClsidCount(), field->GetName());

		settled = limit->m_typeDesc;
		return true;
	}

	// ⭐⭐ AND OTHERWISE THE TYPE IS THE TYPE OF WHAT IS THERE. Two ordinary COMPOSITE fields linked to
	// each other: whoever put a catalogue reference in the first one sees the second offering that
	// catalogue (Max, 2026-09-23). The two branches above are the cases where the value does not mean
	// ITSELF: a type description means what it describes, and a kind means what it declares.
	settled = ibTypeDescription(governing.GetClassType());
	return true;
}

// ⭐⭐ WHICH PART OF THE FIELD THE LINK DECIDES (docs/private/choice-links.md § 6). A field that names no
// governed type is decided whole. A composite field that names one gives up only THAT type to the link:
// its other types stay as declared, with their own qualifiers — a value field of `Characteristic |
// Units` governed through its characteristic still takes a unit whatever kind is chosen.
ibTypeDescription ibMergeGoverned(const ibTypeDescription& contour, ibClassID governed, const ibTypeDescription& settled)
{
	if (governed == 0 || !contour.ContainType(governed))
		return settled;

	ibTypeDescription merged = settled;
	for (const ibClassID& clsid : contour.GetClsidList()) {
		if (clsid == governed || merged.ContainType(clsid))
			continue;
		merged.AppendMetaType(clsid);

		// …and a primitive the settled type did not bring keeps the qualifier the field declared for it.
		switch (ibValue::GetVTByID(clsid)) {
		case ibValueTypes::TYPE_NUMBER: merged.m_typeData.m_number = contour.m_typeData.m_number; break;
		case ibValueTypes::TYPE_DATE:   merged.m_typeData.m_date   = contour.m_typeData.m_date;   break;
		case ibValueTypes::TYPE_STRING: merged.m_typeData.m_string = contour.m_typeData.m_string; break;
		default: break;
		}
	}
	return merged;
}

}

bool ibChoiceLinkResolver::ResolveType(const ibValueMetaObjectAttributeBase* field, const ibChoiceHolder& holder,
	ibChoiceCondition& condition)
{
	const ibChoiceTypeLinkDescription& link = field->GetTypeLink();
	ibTypeDescription settled;
	if (!link.IsOk() || !SettledType(link, holder, settled, true))
		return false;

	condition.m_type = ibMergeGoverned(field->GetTypeValueDesc(), link.m_governedType, settled);
	return true;
}

// ⭐ ONE CONDITION PER ROW, over the field of the target the row names — and the condition is an ordinary
// one. The filter tree has held a field against a value since it was written, so nothing here is a
// special kind of condition: what is special is only where the value came from.
//
// 🛑⭐ AND IT IS APPLIED WHATEVER THE VALUE IS. A row is in use, so it narrows; what stands in its source —
// a counterparty, `False`, nothing at all — is simply the value it narrows BY (Max, 2026-09-23: "empty is
// a normal situation too; what matters is that the row is in use, the value does not matter"). This used
// to refuse the whole choice as "not filled in yet" whenever the source was empty, and IsEmpty is true
// for `False` and `0` as well — so a boolean flag holding False could never open the list it narrows.
bool ibChoiceLinkResolver::ResolveParameters(const ibChoiceParametersDescription& params, const ibChoiceHolder& holder,
	ibChoiceCondition& condition)
{
	const ibMetaData* metaData = holder.GetMetaData();
	if (!params.IsOk() || metaData == nullptr)
		return false;

	for (const ibChoiceParameterRowDescription& row : params.m_rows) {
		if (!row.IsOk())
			continue;

		// The parameter is held by id and spelled into the condition by NAME, because that is what a
		// filter path is. An id the configuration no longer holds names nothing, and a condition over
		// nothing would quietly pass every row.
		//
		// 🛑 ASKED DEEP, and the flag is not a detail: without it the search is the configuration's
		// DIRECT CHILDREN — the catalogs and documents themselves — and a parameter is a field two
		// levels down, so it was never found FOR ANY ROW. Every row fell out of the loop, the filter
		// came out empty, the request carried no condition, and the choice list opened showing
		// everything. Nothing refused; the list simply was not narrowed (measured 2026-09-23 on a
		// document whose contracts showed every counterparty's).
		const ibValueMetaObject* parameter = metaData->FindAnyObjectByFilter(row.m_parameter, true);
		if (parameter == nullptr || parameter->IsDeleted()) {
			// A row naming a field the configuration no longer holds narrows nothing — and said nothing,
			// which is how this arc spent an afternoon opening unnarrowed lists with no refusal anywhere.
			ibJournalWarning(wxT("choice.param"), wxT("row names #%d, which this configuration has not - skipped"),
				row.m_parameter);
			continue;
		}

		// ⭐ …AND THE OTHER END BY ITS OWN QUESTION: the source is not looked up, it is READ, so what says it is
		// gone is the read. An EMPTY source still narrows (above); a source the holder does not answer is not
		// empty, it is not there — and the unread value used to go into the condition all the same, as an
		// undefined the list compares like the column's empty value. With the document's `Export` deleted, the
		// contracts' `Export = Export` row narrowed the choice to the contracts holding False: the exported one
		// was gone from it and a contract made from that list was born not exported, with no word anywhere
		// (2026-09-24, on a copy of the owner demo).
		ibValue value;
		if (!holder.GetValue(row.m_source, value)) {
			ibJournalWarning(wxT("choice.param"), wxT("'%s' takes its value from #%d, which this holder does not answer - skipped"),
				parameter->GetName(), row.m_source.GetLeaf());
			continue;
		}

		ibJournalInfo(wxT("choice.param"), wxT("'%s' = %s [%s] (from #%d)"),
			parameter->GetName(), value.GetString(), value.GetClassName(), row.m_source.GetLeaf());

		condition.m_parameters[parameter->GetName()] = value;
	}

	return !condition.m_parameters.empty();
}

// The pairs written into a record that is being made under them — see the header for why. A parameter
// names its field BY NAME (that is what a filter path is), so the field is found by name among the
// fields of what is being created; a name that answers nothing is skipped rather than guessed at.
void ibChoiceLinkResolver::Fill(ibChoiceHolder& holder, const ibCreateRequest& request)
{
	const ibValueMetaObjectCompositeData* fields = holder.GetFields();
	if (fields == nullptr)
		return;

	// ⚠ SAID EVEN WHEN THERE IS NOTHING TO SAY, and that is the point of this one. Reported as "the
	// substitution does not work", the journal showed no `choice.fill` line at all — which reads as
	// "never called" and is not the same thing as "called with an empty request". It was the second,
	// and the line that would have separated them did not exist because it only ever printed per
	// parameter. A decision with nothing in it is still a decision.
	ibJournalInfo(wxT("choice.fill"), wxT("'%s': %d parameter(s) to write in"),
		fields->GetName(), static_cast<int>(request.m_condition.m_parameters.size()));

	// 🛑⭐ THE SAME LIST THE REST OF THIS FILE ASKS, and it has to be. `FindAnyObjectByFilter` searches
	// DIRECT CHILDREN, and the field this whole arc is about — a subordinate catalog's `Owner` — is not
	// one: it is a PREDEFINED attribute, kept in a property container and reached only through
	// `GetGenericAttributeArrayObject` (predefined + common + plain). So the commonest narrowing there
	// is named a field this lookup could not see, nothing was filled, and a contract created in a list
	// showing one counterparty's was born with no counterparty (Max, 2026-09-23: "the substitution of
	// the value by the filter does not work"). A second way of finding a field is how one road comes to
	// know about a field the other does not.
	const std::vector<ibValueMetaObjectAttributeBase*> declared = fields->GetGenericAttributeArrayObject();

	for (const std::pair<const wxString, ibValue>& parameter : request.m_condition.m_parameters) {
		const ibValueMetaObjectAttributeBase* field = nullptr;
		for (const ibValueMetaObjectAttributeBase* candidate : declared) {
			if (candidate != nullptr && !candidate->IsDeleted() && candidate->GetName() == parameter.first) {
				field = candidate;
				break;
			}
		}

		if (field == nullptr) {
			// The list was narrowed by a field of what is being SHOWN, and the thing being MADE has no
			// field of that name — so the new record cannot be born matching the list, and says so.
			ibJournalInfo(wxT("choice.fill"), wxT("'%s' names no field of '%s' - not filled"),
				parameter.first, fields->GetName());
			continue;
		}

		ibJournalInfo(wxT("choice.fill"), wxT("'%s' = %s"), parameter.first, parameter.second.GetString());
		holder.SetValue(field->GetMetaID(), parameter.second);
	}
}

ibChoiceCondition ibChoiceLinkResolver::Resolve(const ibChoiceHolder& holder,
	const ibValueMetaObjectAttributeBase* field)
{
	ibChoiceCondition condition;
	if (field == nullptr)
		return condition;

	// IN THE ORDER THEY ARE APPLIED, and the order is not an implementation detail: the type decides
	// WHICH list opens, the conditions narrow what is shown IN it.
	ResolveType(field, holder, condition);
	ResolveParameters(field->GetChoiceParameters(), holder, condition);
	return condition;
}

ibValue ibChoiceLinkResolver::Adjust(const ibChoiceHolder& holder,
	const ibValueMetaObjectAttributeBase* field, const ibValue& value)
{
	if (field == nullptr)
		return value;

	// ⭐ A FIELD NO LINK GOVERNS IS ADJUSTED EXACTLY AS IT WAS BEFORE THERE WERE LINKS. This runs on every
	// write of every field — a posting pass filling a record set runs it once per cell — and nearly every
	// field has no link: building a condition for it cost a heap node per write for nothing (2026-09-24,
	// measured on the write bench).
	const ibChoiceTypeLinkDescription& link = field->GetTypeLink();
	if (!link.IsOk())
		return field->AdjustValue(value);

	// The same reading ResolveType makes, without a condition to carry it and without a journal line: this
	// is a write, and the answer is only the type.
	ibTypeDescription settled;
	const ibValue adjusted = SettledType(link, holder, settled, false)
		? field->AdjustValue(value, ibMergeGoverned(field->GetTypeValueDesc(), link.m_governedType, settled))
		: field->AdjustValue(value);

	// 🛑⭐ A VALUE THAT WENT IN AND DID NOT COME OUT — said out loud, because this is the shape the
	// whole evening of 2026-09-23 took. Narrowing answers with the EMPTY value of the type it settled
	// on whenever the value is not admitted there (ibValueTypeDescription::AdjustValue), so a contour
	// computed wrongly does not refuse, does not throw and does not log: it hands back a blank, the
	// write reports success, and the field is simply empty on the next read. Nobody can find that by
	// reading code that looks right — hence a WARNING, with what was lost in it.
	ibJournalIf {
		if (!value.IsEmpty() && adjusted.IsEmpty())
			ibJournalWarning(wxT("choice.adjust"), wxT("'%s' (#%d): %s was not admitted here - written empty"),
				field->GetName(), field->GetMetaID(), value.GetString());
	}

	return adjusted;
}

// ⭐⭐ THE SOURCE'S OWN FIELDS, EMPTIED WHERE THEY WERE CHOSEN WITHIN THE EDITED ONE. Everything this
// needs is already written down and asked of whoever holds it: the source names its metaobject, the
// metaobject lists its fields, and each field says what governs it. No cast, and no second idea of
// where the fields are — a row of a tabular section answers exactly as an object does.
void ibChoiceLinkResolver::ClearLinked(ibChoiceHolder& holder, const ibMetaID& edited)
{
	const ibValueMetaObjectCompositeData* fields = holder.GetFields();
	const ibMetaData* metaData = holder.GetMetaData();
	if (fields == nullptr || metaData == nullptr || edited == 0)
		return;

	// ⭐ ONE PASS, AND THE CHAIN FOLLOWS BY ITSELF. Emptying a field makes it a changed field in its turn,
	// and whatever was chosen within THAT is stale for the same reason — but the emptied value is written
	// through the record's own write (the hop gate below), which is the very write that called this. So
	// the next link of the chain is cleared by the next call, and a configuration that names itself in a
	// circle stops where a value is already empty: writing it again is no change.
	//
	// 🛑 A HAND-BUILT CHAIN STOOD HERE AS WELL — a queue of changed ids and a list of visited ones, with
	// the whole field list rebuilt for every step — and it walked the same chain the writes were already
	// walking. It ran on every changed write, which in a posting pass is nearly every write: two vectors
	// and a fresh field list per cell for nothing (2026-09-24, measured on the write bench).
	for (const ibValueMetaObjectAttributeBase* field : fields->GetGenericAttributeArrayObject()) {
		if (field->IsDeleted() || field->GetMetaID() == edited)
			continue;

		// The type it was chosen under changed: the value is of the wrong type now, and there is no
		// question to ask about it. A parameter row is gentler — it carries the author's answer.
		bool clear = field->GetTypeLink().m_source.GetLeaf() == edited;
		for (const ibChoiceParameterRowDescription& row : field->GetChoiceParameters().m_rows) {
			if (row.m_source.GetLeaf() == edited && row.m_onChange != ibChoiceParameterOnChange::Keep)
				clear = true;
		}
		if (!clear)
			continue;

		// ⭐ THE EMPTY VALUE OF ITS OWN TYPE, not a bare undefined: a field declared as one reference
		// shows an empty reference of it, which is what the control was showing before anything was
		// chosen. A composite field has no single such value, and undefined is the honest one there.
		// What the field may HOLD decides it, not what it declares: a characteristic declares one type
		// and holds its chart's several.
		const ibTypeDescription& typeDesc = field->GetTypeValueDesc();
		const ibValue empty = typeDesc.GetClsidCount() == 1 ? metaData->CreateObject(typeDesc.GetFirstClsid()) : ibValue();

		// ⭐ ALREADY CLEARED IS NOT CLEARED AGAIN — but "cleared" means what the write WOULD store, and that
		// is the empty value narrowed by the field's own link: a characteristic under a numeric kind is 0,
		// not undefined. Writing the row's kind into a new row used to empty an already empty cell all the
		// same — a write per row for no change (2026-09-24, the write bench). Judged by the raw empty value
		// instead, the skip left a new row's characteristic UNDEFINED under a chosen kind, so clicking it
		// asked for a type the kind had already settled (Max, 2026-09-24: "I re-choose the kind, and the
		// value on the right still offers the type choice").
		const ibValue cleared = Adjust(holder, field, empty);
		ibSourceDescription at;
		at.AppendSource(field->GetMetaID());
		ibValue current;
		if (holder.GetValue(at, current) && current.GetClassType() == cleared.GetClassType() && current == cleared)
			continue;

		// ⚠ SAID ONLY WHEN SOMETHING WAS THERE. An empty cell getting its kind's empty value lost nothing —
		// it is a new row typed, and in a posting pass that is every row: the line alone was 3 s of 7 on
		// twenty thousand register lines given a kind (2026-09-24, the write bench, Debug).
		if (!current.IsEmpty())
			ibJournalInfo(wxT("choice.clear"), wxT("'%s' (#%d) emptied - #%d changed under it"),
				field->GetName(), field->GetMetaID(), edited);

		// Written through the source's own hop gate — the door every kind of source answers, so a
		// row and an object are not told apart here either.
		holder.SetValue(field->GetMetaID(), cleared);
	}
}
