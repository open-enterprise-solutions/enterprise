#include "choiceLinkResolver.h"

#include "backend/metaData.h"                                            // a parameter names a field of the list it opens
#include "backend/srcDataObject.h"                                       // the path door every source answers, and what its fields say
#include "backend/backend_localization.h"                                // ibTranslateString — a field's format

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

// ⭐ THE ROW FIRST, THE OBJECT AFTER IT, AS GetValue READS. A column's holder carries both the row it edits
// and the form's object over it, and the fields standing HERE are first the row's — the columns. This
// answered with the object whenever there was one, so a column looking for its own field searched the
// DOCUMENT's attributes, found nothing, and fell back to its bare declaration: a characteristic column
// offered no type to choose and drew `False` as blank (2026-09-23: the table's value column was looked for
// among the document's fields and not found, with the row standing right there as its holder). A row that
// says nothing does not have the field, and the object above it is asked — a column bound to the header.
//
// ⭐ SAID ONCE for every question (AskRowFirst): each brings only what it asks and what "says something" is
// for its answer. Written out three times, the order was three copies to keep in step with GetValue's.
namespace {

template <typename Answer, typename Ask, typename Said>
Answer AskRowFirst(const ibChoiceHolder& holder, const Ask& ask, const Said& said, Answer none)
{
	if (holder.m_table != nullptr) {
		Answer answer = ask(*holder.m_table);
		if (said(answer) || holder.m_source == nullptr)
			return answer;
	}
	return holder.m_source != nullptr ? ask(*holder.m_source) : none;
}

}

const ibChoiceParametersDescription& ibChoiceHolder::GetChoiceParameters(const ibMetaID& id) const
{
	static const ibChoiceParametersDescription s_none;
	return AskRowFirst<const ibChoiceParametersDescription&>(*this,
		[&id](const auto& level) -> const ibChoiceParametersDescription& { return level.GetChoiceParameters(id); },
		[](const ibChoiceParametersDescription& params) { return params.IsOk(); }, s_none);
}

const ibTranslateString& ibChoiceHolder::GetFormat(const ibMetaID& id) const
{
	static const ibTranslateString s_none;
	return AskRowFirst<const ibTranslateString&>(*this,
		[&id](const auto& level) -> const ibTranslateString& { return level.GetFormat(id); },
		[](const ibTranslateString& format) { return !format.IsEmpty(); }, s_none);
}

ibSelectMode ibChoiceHolder::GetSelectMode(const ibMetaID& id) const
{
	return AskRowFirst<ibSelectMode>(*this,
		[&id](const auto& level) { return level.GetSelectMode(id); },
		[](ibSelectMode mode) { return mode != ibSelectMode::ibSelectMode_Items; }, ibSelectMode::ibSelectMode_Items);
}

const ibMetaData* ibChoiceHolder::GetMetaData() const
{
	if (m_table != nullptr)
		return m_table->GetSourceMetaData();
	return m_source != nullptr ? m_source->GetSourceMetaData() : nullptr;
}

bool ibChoiceLinkResolver::CanGovern(const ibTypeDescription& type)
{
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
	return type.IsOk();
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
			ibJournalWarning(ibT("choice.param"), ibT("row names #%d, which this configuration has not - skipped"),
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
			ibJournalWarning(ibT("choice.param"), ibT("'%s' takes its value from #%d, which this holder does not answer - skipped"),
				parameter->GetName(), row.m_source.GetLeaf());
			continue;
		}

		ibJournalInfo(ibT("choice.param"), ibT("'%s' = %s [%s] (from #%d)"),
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
	const ibSourceExplorer* fields = holder.m_source != nullptr ? holder.m_source->GetSourceExplorer() : nullptr;
	if (fields == nullptr)
		return;

	// ⚠ SAID EVEN WHEN THERE IS NOTHING TO SAY, and that is the point of this one. Reported as "the
	// substitution does not work", the journal showed no `choice.fill` line at all — which reads as
	// "never called" and is not the same thing as "called with an empty request". It was the second,
	// and the line that would have separated them did not exist because it only ever printed per
	// parameter. A decision with nothing in it is still a decision.
	ibJournalInfo(ibT("choice.fill"), ibT("'%s': %d parameter(s) to write in"),
		fields->GetSourceName(), static_cast<int>(request.m_condition.m_parameters.size()));

	// 🛑⭐ THE FIELDS THE SOURCE ITSELF LISTS, and it has to be them. `FindAnyObjectByFilter` on the
	// configuration searches its DIRECT CHILDREN, and the field this whole arc is about — a subordinate
	// catalog's `Owner` — is not one: it is a PREDEFINED attribute of the catalog. So the commonest narrowing
	// there was named a field that lookup could not see, nothing was filled, and a contract created in a list
	// showing one counterparty's was born with no counterparty (Max, 2026-09-23: "the substitution of the
	// value by the filter does not work"). The source's explorer lists every field it has, predefined ones
	// included, and a second way of finding a field is how one road comes to know about a field the other
	// does not.
	for (const std::pair<const ibString, ibValue>& parameter : request.m_condition.m_parameters) {
		const ibSourceExplorer* field = fields->FindByName(parameter.first);
		if (field == nullptr || field->IsTableSection()) {
			// The list was narrowed by a field of what is being SHOWN, and the thing being MADE has no
			// field of that name — so the new record cannot be born matching the list, and says so.
			ibJournalInfo(ibT("choice.fill"), ibT("'%s' names no field of '%s' - not filled"),
				parameter.first, fields->GetSourceName());
			continue;
		}

		ibJournalInfo(ibT("choice.fill"), ibT("'%s' = %s"), parameter.first, parameter.second.GetString());
		holder.SetValue(field->GetSourceId(), parameter.second);
	}
}

ibChoiceCondition ibChoiceLinkResolver::Resolve(const ibChoiceHolder& holder, const ibMetaID& field)
{
	ibChoiceCondition condition;
	ResolveParameters(holder.GetChoiceParameters(field), holder, condition);
	return condition;
}

ibChoiceCondition ibChoiceLinkResolver::Resolve(const ibChoiceHolder& holder, const ibMetaID& field,
	const ibChoiceParametersDescription& params, const ibChoiceHolder& paramsHolder)
{
	ibChoiceCondition condition = Resolve(holder, field);
	ResolveParameters(params, paramsHolder, condition);
	return condition;
}

bool ibChoiceLinkResolver::Adjust(const ibChoiceHolder& holder, const ibChoiceTypeLinkDescription& link,
	const ibValue& value, ibValue& adjusted)
{
	// ⭐ A FIELD NO LINK GOVERNS IS ADJUSTED EXACTLY AS IT WAS BEFORE THERE WERE LINKS — by the writer, who
	// holds the field. This runs on every write of every field — a posting pass filling a record set runs it
	// once per cell — and nearly every field has no link: building a condition for it cost a heap node per
	// write for nothing (2026-09-24, measured on the write bench).
	if (!link.IsOk())
		return false;

	// ⭐⭐ THE GOVERNING VALUE BRINGS THE VALUE, AND THAT IS THE WHOLE ANSWER — ibValue::AdjustValue, the
	// same verb the accounting register's dimension uses for a subconto. Nothing here reads a type off
	// anything: what stands in the governing field decides what it takes (a type description by what it
	// describes, a reference through the metaobject that governs it, anything else by its own class), and
	// what comes back is a value of that type, admitted or empty.
	//
	// 🛑 AND IT IS NOT NARROWED TWICE. The field's own adjustment used to be applied on top, and after the
	// governing value has spoken there is only the ONE option left (Max, 2026-09-24): a second pass either
	// changes nothing or destroys what the governor just decided, and it copies the value again on a road
	// a posting pass walks once per cell.
	//
	// 🛑 IT ALSO REPLACED A SEARCH: this used to settle a TYPE — walking a kind's attributes for the first
	// one that could hold a type description, two casts to get there — and then adjust by the merge of
	// that with the field's declaration. All of it is gone; the one question is enough.
	ibValue governing;
	if (!holder.GetValue(link.m_source, governing) || governing.IsEmpty())
		return false;

	governing.AdjustOutValue(value, adjusted);

	// 🛑⭐ A VALUE THAT WENT IN AND DID NOT COME OUT — said out loud, because this is the shape the
	// whole evening of 2026-09-23 took. Narrowing answers with the EMPTY value of the type it settled
	// on whenever the value is not admitted there (ibValueTypeDescription::AdjustValue), so a contour
	// computed wrongly does not refuse, does not throw and does not log: it hands back a blank, the
	// write reports success, and the field is simply empty on the next read. Nobody can find that by
	// reading code that looks right — hence a WARNING, with what was lost in it.
	ibJournalIf {
		if (!value.IsEmpty() && adjusted.IsEmpty())
			ibJournalWarning(ibT("choice.adjust"), ibT("%s was not admitted under #%d - written empty"),
				value.GetString(), link.m_source.GetLeaf());
	}

	return true;
}

// The type it was chosen under changed: the value is of the wrong type now, and there is no question to
// ask about it. A parameter row is gentler — it carries the author's answer.
bool ibChoiceLinkResolver::IsLinked(const ibChoiceTypeLinkDescription& link, const ibChoiceParametersDescription& params,
	const ibSourceDescription& edited)
{
	if (!edited.IsOk())
		return false;
	if (link.m_source == edited)
		return true;
	for (const ibChoiceParameterRowDescription& row : params.m_rows) {
		if (row.m_source == edited && row.m_onChange != ibChoiceParameterOnChange::Keep)
			return true;
	}
	return false;
}

// ⭐⭐ THE SOURCE'S OWN FIELDS, EMPTIED WHERE THEY WERE CHOSEN WITHIN THE EDITED ONE. Everything this
// needs is already written down and asked of whoever holds it: the source says which of its fields are
// linked to the edited one (GetLinked) and, by id, what governs each. No cast, no metaobject, and no
// second idea of where the fields are — a row of a tabular section answers exactly as an object does.
void ibChoiceLinkResolver::ClearLinked(ibChoiceHolder& holder, const ibMetaID& edited)
{
	if (!holder.IsOk() || edited == 0)
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
	//
	// ⭐ THE LEVEL THE FIELD IS ON SAYS WHICH OF ITS FIELDS ARE LINKED — the row's table for a column, the
	// object for its own field — in one walk over them (GetLinked). Asked here field by field, by id, an
	// object looked every field up twice on every changed write. Each linked field takes the step below,
	// in the holder it stands in: the edited one's, or a row of a table under it.
	const auto clear = [&edited](ibChoiceHolder& owner, const auto& level, const ibMetaID& field) {
		const ibChoiceTypeLinkDescription& link = level.GetTypeLink(field);

		// ⭐ THE EMPTY VALUE OF ITS OWN TYPE, not a bare undefined: a field declared as one reference
		// shows an empty reference of it, which is what the control was showing before anything was
		// chosen. A composite field has no single such value, and undefined is the honest one there.
		// What the field may HOLD decides it, not what it declares: a characteristic declares one type
		// and holds its chart's several — which is the field's own adjustment of nothing, its source's.
		//
		// ⭐ ALREADY CLEARED IS NOT CLEARED AGAIN — but "cleared" means what the write WOULD store, and that
		// is the empty value narrowed by the field's own link: a characteristic under a numeric kind is 0,
		// not undefined. Writing the row's kind into a new row used to empty an already empty cell all the
		// same — a write per row for no change (2026-09-24, the write bench). Judged by the raw empty value
		// instead, the skip left a new row's characteristic UNDEFINED under a chosen kind, so clicking it
		// asked for a type the kind had already settled (Max, 2026-09-24: "I re-choose the kind, and the
		// value on the right still offers the type choice").
		ibValue cleared;
		if (!Adjust(owner, link, ibValue(), cleared))
			cleared = level.AdjustValue(field, ibValue());
		ibSourceDescription at;
		at.AppendSource(field);
		ibValue current;
		if (owner.GetValue(at, current) && current.GetClassType() == cleared.GetClassType() && current == cleared)
			return;

		// ⚠ SAID ONLY WHEN SOMETHING WAS THERE. An empty cell getting its kind's empty value lost nothing —
		// it is a new row typed, and in a posting pass that is every row: the line alone was 3 s of 7 on
		// twenty thousand register lines given a kind (2026-09-24, the write bench, Debug).
		if (!current.IsEmpty())
			ibJournalInfo(ibT("choice.clear"), ibT("#%d emptied - #%d changed under it"), field, edited);

		// Written through the source's own hop gate — the door every kind of source answers, so a
		// row and an object are not told apart here either.
		owner.SetValue(field, cleared);
	};

	// A ROW'S FIELDS ARE ITS TABLE'S COLUMNS, and a row has no tables below it.
	if (holder.m_table != nullptr) {
		for (const ibMetaID& field : holder.m_table->GetLinked(edited))
			clear(holder, *holder.m_table, field);
		return;
	}

	for (const ibMetaID& field : holder.m_source->GetLinked(edited))
		clear(holder, *holder.m_source, field);

	// 🛑⭐ A COLUMN MAY BE CHOSEN WITHIN A FIELD OF THE OBJECT ABOVE IT — a contract in a line, filtered by
	// the counterparty in the header — and this pass stopped at the object's own fields: the header changed,
	// and every line kept a contract with somebody the document no longer names (issue #650). Between two
	// header fields, and between two cells of one row, the same link worked.
	//
	// ⭐ SO THE SAME STEP, WITH EACH ROW AS THE HOLDER. Written through the row's own hop gate, as a cell
	// edited in that row would be, so whatever was chosen within the emptied cell goes too — within that
	// row. The tables are the ones the object itself says it has (its explorer, the node a form walks), and
	// their values come through the object's own hop gate: nothing here knows which kinds have tables.
	//
	// ⚠ A ROW IS NOT WALKED FOR NOTHING: a table with no column linked to the edited field is passed over
	// without touching a row — this runs on every changed write of the header — and the linked columns are
	// asked once per table, not once per row.
	const ibSourceExplorer* fields = holder.m_source->GetSourceExplorer();
	if (fields == nullptr)
		return;
	for (unsigned int idx = 0; idx < fields->GetHelperCount(); ++idx) {
		const ibSourceExplorer* node = fields->GetHelper(idx);
		if (node == nullptr || !node->IsTableSection())
			continue;

		ibValue value;
		ibTabularDataObject* table = nullptr;
		if (!holder.m_source->GetValueBySourceHop(ibSourceHop{ node->GetSourceId() }, value) || !value.ConvertToValue(table))
			continue;

		const std::vector<ibMetaID> columns = table->GetLinked(edited);
		if (columns.empty())
			continue;

		for (long row = 0; row < table->GetRowCount(); ++row) {
			ibChoiceHolder line(table, table->GetItem(row));
			line.m_source = holder.m_source;   // a cell's link may read the header, as GetValue reads it
			for (const ibMetaID& column : columns)
				clear(line, *table, column);
		}
	}
}
