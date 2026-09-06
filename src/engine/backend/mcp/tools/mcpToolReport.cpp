////////////////////////////////////////////////////////////////////////////
//	Description : the composer — a report, read as the tree it is
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE LAYERS, AND WHERE THIS SITS.
//
//     the SCHEMA      what can be read at all — sources, fields, parameters
//       ↑ the COMPOSER  what to do with it — outputs, levels, resources
//         ↑ the SETTINGS  variants and the reader's own, over the composer
//
// A composer is NOT a second query language. It states a report in the words a
// person uses — group by warehouse, then by product, total the quantity — and
// RENDERS DOWN into an ordinary query. The text is the one seam downward, which
// is why nothing below has to be told that reports exist.
//
// So the useful place to stand is OVER the composer: hand it a structure and let
// it render. That also settles who judges what, and none of the judges are new:
//
//     is the field there?      the query constructor's own answer (query_fields)
//     does the expression hold? the COMPILER (CheckExpression wraps it in a
//                               function and compiles it)
//     does the composition render? the composer
//     is the rendered text right?  query_check — ParsePackage + CheckNames
//
// This file is the READ half: the tree as it stands. Building in it is the next
// verb, and it will refuse a path the source never named — every useful thing
// learned today came out of a refusal, and everything that agreed silently was
// wrong.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/compositionDescription.h"
#include "backend/metaCollection/metaComposerObject.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/genericData.h"   // ResolveQueryConstant — a named item as a value
#include "backend/objCtor.h"                      // ibCtorMetaValueType — a type looked up by name
#include "backend/typeDescription.h"              // …and the description a parameter carries
#include "backend/metadataConfiguration.h"
#include "backend/query/queryConstructorModel.h"   // ibQueryFieldsOfText — what the TEXT offers
#include "backend/query/queryException.h"          // …and the judge that refuses one
#include "backend/query/queryLowering.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryable.h"

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The composer's NodeId - a report declares one, metadata_get on the report "
			  "lists it among its children."), /*required*/ true);
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("What to call it - how a reader will pick it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgVariant()
{
	static const ibArg s_a(wxT("variant"), ibArg::Kind::Text,
		ibMcpText("Which variant it belongs to. Omit for the author's."));
	return s_a;
}

// ⭐ EVERYTHING THAT ADDS HAS TO BE ABLE TO TAKE BACK. A verb with no inverse leaves a caller that
// added the wrong thing with no way out but the settings window — and an assistant has no hands
// there. The vocabulary already says it this way: section_include takes `remove`, predefined_add
// takes `delete` (2026-09-01, after a mistaken call left a nameless output in a report that no
// tool could remove).
const ibArg& ArgRemove()
{
	static const ibArg s_a(wxT("remove"), ibArg::Kind::Flag,
		ibMcpText("Take it OUT instead of putting it in. Off by default."));
	return s_a;
}

const ibArg& ArgComparison()
{
	static const ibArg s_a(wxT("comparison"), ibArg::Kind::Text,
		ibMcpText("How to compare - equal (the default), notEqual, greater, less, greaterEqual, lessEqual, "
			  "contains, in, inHierarchy."),
		/*required*/ false,
		{ wxT("equal"), wxT("notEqual"), wxT("greater"), wxT("less"), wxT("greaterEqual"),
		  wxT("lessEqual"), wxT("contains"), wxT("in"), wxT("inHierarchy") });
	return s_a;
}

const ibArg& ArgDescending()
{
	static const ibArg s_a(wxT("descending"), ibArg::Kind::Flag,
		ibMcpText("Largest first. Off means ascending, which is the ordinary case."));
	return s_a;
}

const ibArg& ArgType()
{
	static const ibArg s_a(wxT("type"), ibArg::Kind::Text,
		ibMcpText("What kind of value it holds, in the words type_list answers with - Date, Number, "
			  "String, Boolean, CatalogRef.Warehouses. Not deduced from the query: say it."));
	return s_a;
}

const ibArg& ArgForUser()
{
	static const ibArg s_a(wxT("forUser"), ibArg::Kind::Flag,
		ibMcpText("Put it on the report's form for the person to fill in before generating. Off means the "
			  "author sets it and the reader never sees it - which is how a query branch is "
			  "switched on and off."));
	return s_a;
}

const ibArg& ArgAt()
{
	static const ibArg s_a(wxT("at"), ibArg::Kind::Whole,
		ibMcpText("Where in the order it goes, 1 for first. Omit to append - and the order of these IS the "
			  "order of the columns on the page."));
	return s_a;
}

const ibArg& ArgSynonym()
{
	static const ibArg s_a(wxT("synonym"), ibArg::Kind::Text,
		ibMcpText("What the PERSON sees in the picker, in their language. Omit and the name is read out "
			  "loud instead."));
	return s_a;
}

const ibArg& ArgAdd()
{
	static const ibArg s_a(wxT("add"), ibArg::Kind::Flag,
		ibMcpText("Make a NEW variant of this name, starting from the settings of the one named by "
			  "`variant` (or the first). Off by default, which renames instead."));
	return s_a;
}

const ibArg& ArgOutput()
{
	static const ibArg s_a(wxT("output"), ibArg::Kind::Text,
		ibMcpText("Which output."), /*required*/ true);
	return s_a;
}

const ibArg& ArgGroupBy()
{
	static const ibArg s_a(wxT("groupBy"), ibArg::Kind::Text,
		ibMcpText("The field to group by. Omit for a DETAIL level - the rows themselves."));
	return s_a;
}

const ibArg& ArgColumns()
{
	static const ibArg s_a(wxT("columns"), ibArg::Kind::Flag,
		ibMcpText("Put it across the columns instead of down the rows."));
	return s_a;
}

const ibArg& ArgFunction()
{
	static const ibArg s_a(wxT("function"), ibArg::Kind::Text,
		ibMcpText("SUM, MIN, MAX, COUNT... Omit when `path` is a whole expression."));
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(wxT("path"), ibArg::Kind::Text,
		ibMcpText("The field to fold, or the expression when no function is given."), /*required*/ true);
	return s_a;
}

// ⭐⭐ SEVERAL AT ONCE, BECAUSE A REPORT'S COLUMNS ARE A LIST AND ALWAYS WERE. Saying one field per
// call made a seven-column trial balance seven round trips of the same verb — and the ORDER of
// those columns is the point of the verb, so the caller was spelling out a list one element at a
// time and hoping nothing interleaved (measured 2026-09-02: sixteen calls to build one report, seven
// of them this).
//
// The order of the array IS the order of the columns, which is the same rule as before, said once.
// …and the SINGLE one stops being required once `paths` can carry it. Same name, same meaning: what
// changes is that a call is complete with either, and the gate that refuses a call missing a
// required argument (ibMcpMissingArgument) must be told so, or it refuses every list.
const ibArg& ArgOnePath()
{
	static const ibArg s_a(wxT("path"), ibArg::Kind::Text,
		ibMcpText("The field to show. One; use `paths` for several, in the order they should appear."));
	return s_a;
}

const ibArg& ArgPaths()
{
	static const ibArg s_a(wxT("paths"), ibArg::Kind::Many,
		ibMcpText("Several fields at once, instead of `path` - and their order is the order of the "
			  "columns. One that the query does not offer is refused by name and the rest are "
			  "still added, so a typo costs one field rather than the call."));
	return s_a;
}

const ibArg& ArgOver()
{
	static const ibArg s_a(wxT("over"), ibArg::Kind::Text,
		ibMcpText("The grouping it is computed over. Omit for the ladder - one figure per heading, "
			  "following whatever the reader regrouped."));
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		ibMcpText("The query. An empty one is allowed and clears it."), /*required*/ true);
	return s_a;
}

// ⭐⭐ THE WHOLE COMPOSITION, AND ITS SHAPE COMES FROM AN EMPTY ONE.
//
// The schema does not describe this structure in prose — it carries a real, empty composition
// written by the same ib*Memory pair that reads it back. So what a caller is shown, what report_get
// answers with and what report_set will accept are one thing, and none of them is a description of
// the other two.
const ibArg& ArgComposition()
{
	static const ibArg s_a(wxT("composition"), ibArg::Kind::Node,
		ibMcpText("The whole composition, in the shape report_get answers with. Read it, change what you "
		  "need and send it back - the example below is an empty one, written by the same reader "
		  "that will take yours."),
		/*required*/ true, std::vector<wxString>(),
		[](ibDataValue& shape) {
			return ibCompositionDescriptionMemory::WriteNode(shape, ibCompositionDescription());
		});
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// report_get
//---------------------------------------------------------------------------
class ibMcpToolReportGet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_get"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("reading the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("A composer as the tree it is: the query it reads, the fields and resources it "
			"declares, and its outputs - each with the levels it groups by, down the rows and "
			"across the columns. This is what a report IS; the query below it is what the "
			"composer renders into.\n"
			  "\n"
			"AND WHAT IT ANSWERS IS compose_run - the same schema, executed, with its figures. That "
			"is the other half of working on a report and it is where you go when somebody says a "
			"number is wrong: this verb says what the report is, that one says what it produced, and "
			"compose_settings says WHOSE settings they were looking at when they said it. The tree "
			"this answers with also goes straight into compose_run's `schema`, so a change can be "
			"tried before it is written anywhere.\n"
			  "\n"
			"NOTE - THE PARAMETER VALUES IN HERE ARE PACKED, and a packed value does not read as what it "
			"is - a date, a reference and an enum member all arrive as a small node. `value_unpack` "
			"says what one actually holds: its type, its presentation, its identifier. That is the "
			"verb for 'what is this report filtering by, exactly', and `value_pack` is its mirror "
			"when you need to STATE one - a period above all, which no JSON scalar can carry.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectComposer* composer =
			object->ConvertToType<ibValueMetaObjectComposer>();
		if (composer == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a composer. A report declares one under itself."),
				object->GetName());
			return false;
		}

		const ibCompositionDescription& composition = composer->GetCompositionDesc();

		result.SetValue(wxT("composer"), object->GetName());

		// ⭐⭐ THE COMPOSITION SAYS ITSELF, AND SERIALISATION LIVES IN ONE PLACE.
		//
		// ibCompositionDescriptionMemory::WriteNode is how a composition is written to a FILE, and
		// it writes ALL of it: the query, the main table when there is one, the parameters, the
		// resources, the selects with their ids, the author's chosen columns, and every variant —
		// each of which carries its settings, its structure, its outputs, and the levels, filters,
		// sorts and groups inside those. Eight members of the ib*Memory family, each already
		// written and each already the thing the format depends on.
		//
		// 🛑 A HUNDRED AND THIRTY LINES HERE RE-DERIVED THE SAME THING from the description's
		// fields, and had drifted in the way that shape always drifts — quietly and in the reader's
		// favour. `m_selected`, the columns the author chose to show, was not reported AT ALL,
		// while a gate one file over exists to warn a caller that it forgot to set them; the select
		// ids were dropped, so a caller could read a select and had no way to address it.
		//
		// ⭐ AND THE POINT IS NOT THE LINES. Max, 2026-09-01: *"so that serialisation is changed in
		// ONE place — otherwise we will be guessing where it drifted, and that is very hard."* A
		// field added to a level now reaches a caller because it reaches the file; there is no
		// second reader to remember. What is read here is also what can be handed back.
		// ⚠ AND IT ANSWERS WHETHER IT COULD. The family returns a bool — *did this read, did this
		// write* — and dropping it is how a caller comes to hold a composition that was only
		// partly said, with nothing to tell it apart from a composer that is genuinely empty.
		if (!ibCompositionDescriptionMemory::WriteNode(result, composition)) {
			refusal = wxString::Format(
				ibMcpText("'%s' could not describe itself. The composition is there; reading it out failed."),
				object->GetName());
			return false;
		}

		if (composition.m_variants.empty()
			|| composition.m_variants.front().m_settings.m_structure.empty())
			result.SetValue(wxT("note"),
				ibMcpText("The composer declares no output yet - nothing would be produced."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportGet);

namespace {

// ⭐ WHAT THIS COMPOSITION CAN TALK ABOUT — derived from the QUERY TEXT and the
// configuration, and from nothing else.
//
// The same door the settings window uses. It used to be a method on a RUNNING
// composition, so a window had to hold a live report — a source binding, a sheet,
// a fetch — to learn something the text already answers; it was moved to the
// query tier for exactly that reason, and asking it here costs nothing.
//
// Half-typed text is not a failure: it offers no fields YET. So an empty list and
// a parser complaint are different answers and are reported as different things.
std::vector<ibQueryConstructorField> FieldsOf(const ibCompositionDescription& composition,
	wxString& fault)
{
	return ibQueryFieldsOfText(composition.m_query, activeMetaData, &fault);
}

// The composer a caller named, with the refusals said in words.
ibValueMetaObjectComposer* FindComposer(const ibDataNode& params, wxString& refusal)
{
	ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
	if (object == nullptr)
		return nullptr;

	ibValueMetaObjectComposer* composer = object->ConvertToType<ibValueMetaObjectComposer>();
	if (composer == nullptr) {
		refusal = wxString::Format(
			ibMcpText("'%s' is not a composer. A report declares one under itself."), object->GetName());
		return nullptr;
	}

	return composer;
}

// ⭐⭐ THE REFUSAL THAT MATTERS. A grouping by a path the query never projected is
// the one mistake that survives everything: it stores, it saves, and it produces
// an empty report at the moment somebody runs it. So it is refused HERE, with the
// list of what the query does offer — which is the answer the caller needs next.
bool PathIsOffered(const ibCompositionDescription& composition,
	const wxString& path, wxString& refusal)
{
	wxString fault;
	const std::vector<ibQueryConstructorField> fields = FieldsOf(composition, fault);

	if (!fault.IsEmpty()) {
		refusal = wxString::Format(
			ibMcpText("The composer's query cannot be read, so nothing can be checked against it: %s"),
			fault);
		return false;
	}

	wxString available;
	for (const ibQueryConstructorField& field : fields) {
		if (field.m_name.IsSameAs(path, false))
			return true;
		available << (available.IsEmpty() ? wxT("") : wxT(", ")) << field.m_name;
	}

	refusal = available.IsEmpty()
		? ibMcpText("The composer's query projects no fields yet - give it a query first.")
		: wxString::Format(ibMcpText("'%s' is not a field this query offers. It has: %s."),
			path, available);
	return false;
}

// Every write lands in a VARIANT, because that is where settings live — see the
// note in report_get. Named, or the first one, which is the author's.
// ⭐⭐ WHAT THIS COMPOSER IS STILL MISSING - said after EVERY change, not discovered when somebody
// runs the report and sees an empty page.
//
// 🛑 THE TWO THAT ARE ALWAYS FORGOTTEN, and both were forgotten here on the first day of building
// with these verbs (Max, 2026-09-02: *"you keep missing them - you do not name the variant, and
// you do not say the selected fields"*):
//   · a variant with NO NAME - nothing for a person to pick, and the configuration will refuse to
//     save (ibValueMetaObjectComposer::OnSaveMetaObject);
//   · NO SELECTED FIELDS - a report that groups and totals and shows no columns at all.
// Neither is an error at the moment it happens, which is exactly why it survives to the end: the
// composer is valid, the calls all succeeded, and the emptiness appears in front of a user.
//
// So every report_* verb answers with this. Not a refusal - the state is legitimate WHILE
// building, one call at a time - but never silent, and phrased as the next thing to do.
// ⭐⭐ AND THE SAME QUESTION IS ASKED OF A WHOLE CONFIGURATION (config_check), which is why the
// complaints are computed apart from the way they are said. One list of what a composition lacks:
// the report verbs answer it after every change, the audit asks it of every composer there is, and
// a rule added here is in both by construction rather than by remembering.
void ComposerComplaints(const ibCompositionDescription& composition,
	std::vector<wxString>& missing)
{
	const auto say = [&missing](const wxString& what) { missing.push_back(what); };

	if (composition.m_query.IsEmpty())
		say(ibMcpText("no query - report_query says what this composes over"));

	for (size_t index = 0; index < composition.m_variants.size(); index++) {
		if (composition.m_variants[index].m_name.IsEmpty()) {
			say(wxString::Format(
				ibMcpText("variant %i has no name - report_variant names it, and the configuration will "
				  "not save while it is nameless"), (int)index + 1));
			break;
		}
	}

	bool anyOutput = false;
	bool anySelected = false;

	for (const ibVariantDescription& variant : composition.m_variants) {
		anyOutput = anyOutput || !variant.m_settings.m_structure.empty();
		anySelected = anySelected || !variant.m_settings.m_selected.empty();
	}

	if (!composition.m_selected.empty())
		anySelected = true;

	if (!anyOutput)
		say(ibMcpText("no output - report_output adds one, and a composer without one produces nothing"));

	if (!anySelected)
		say(ibMcpText("nothing is SHOWN - report_select says which fields become columns, in their order; "
			  "grouping and totalling alone compose headings with no figures under them"));

}

void ibMcpSayComposerComplaints(const ibCompositionDescription& composition, ibDataNode& result)
{
	std::vector<wxString> complaints;
	ComposerComplaints(composition, complaints);

	if (complaints.empty())
		return;

	std::vector<ibDataValue> missing;
	for (const wxString& one : complaints)
		missing.push_back(ibDataValue::String(one));

	result.AddField(wxT("incomplete"), ibDataValue::Array(missing));
	result.SetValue(wxT("nextStep"),
		ibMcpText("The lines above are what this composer still lacks to produce a report somebody can "
		  "read."));
}

ibVariantDescription* VariantOf(ibCompositionDescription& composition,
	const wxString& name, wxString& refusal)
{
	if (composition.m_variants.empty())
		composition.m_variants.emplace_back();

	if (name.IsEmpty())
		return &composition.m_variants.front();

	for (ibVariantDescription& variant : composition.m_variants) {
		if (variant.m_name.IsSameAs(name, false))
			return &variant;
	}

	wxString available;
	for (const ibVariantDescription& variant : composition.m_variants) {
		available << (available.IsEmpty() ? wxT("") : wxT(", "))
			<< (variant.m_name.IsEmpty() ? ibMcpText("(the author's)") : variant.m_name);
	}

	refusal = wxString::Format(ibMcpText("This composer has no variant called '%s'. It has: %s."),
		name, available);
	return nullptr;
}

// ⭐⭐ THE THING A REPORT IS BUILT WITHOUT, AND NOBODY NOTICES UNTIL IT IS RUN.
//
// A composer with outputs, levels and resources but NO SELECTED FIELDS is complete enough to
// compose and produces a report with its groupings and no figures — every heading in place and
// every column empty. Nothing refuses, nothing is logged; the answer to "did it work" is yes.
//
// So the verbs that build a report say it as they go. Not a refusal — an unfinished report is a
// legitimate intermediate state, and the fields may well be the next call — but a warning that
// travels back with the very answer that would otherwise read as done.
// ⚠ AND THE TABLE IT ASKS IS `m_selected`, NOT `m_selects` — two names one letter apart and two
// different things. `m_selects` holds what a field is CALLED (a title, a renaming) and is empty for
// nearly every report; `m_selected` is the author's answer to "which columns do I want to see",
// and THAT is what was missing. Checked the wrong one first, and the wrong one is empty on healthy
// reports — a warning that fired on everything would have been worse than none.
// THE COMPARISON AS A WORD. Spelled out in the schema and matched here, so the two cannot drift -
// and so a caller reading the description knows the whole vocabulary without guessing at numbers.
bool ComparisonFromWord(const wxString& word, ibComparisonKind& kind)
{
	struct { const wxChar* m_word; ibComparisonKind m_kind; } static const s_words[] = {
		{ wxT("equal"),        ibComparisonKind_Equal },
		{ wxT("notEqual"),     ibComparisonKind_NotEqual },
		{ wxT("greater"),      ibComparisonKind_Greater },
		{ wxT("less"),         ibComparisonKind_Less },
		{ wxT("greaterEqual"), ibComparisonKind_GreaterEqual },
		{ wxT("lessEqual"),    ibComparisonKind_LessEqual },
		{ wxT("contains"),     ibComparisonKind_Contains },
		{ wxT("in"),           ibComparisonKind_In },
		{ wxT("inHierarchy"),  ibComparisonKind_InHierarchy },
	};

	for (const auto& entry : s_words) {
		if (word.IsSameAs(entry.m_word, false)) {
			kind = entry.m_kind;
			return true;
		}
	}

	return false;
}

// ⭐⭐ THE VALUE A FILTER COMPARES AGAINST, and this is where most of the work is. A filter on a
// warehouse is compared with a REFERENCE, not with the warehouse's name - so a caller sending text
// has to have it turned into one, and the only honest way to know what to turn it into is to ask
// the FIELD what type it holds.
//
// ⚠ A PREDEFINED ITEM IS THE ORDINARY CASE - "only the main warehouse", "everything but the
// scrap account" - and it is the one a caller can actually name in a call, because a predefined
// item has a name in the configuration where an ordinary row has only a guid. So: a reference
// field takes the predefined item of that name; anything else takes the scalar as it stands.
bool ValueForPath(const ibCompositionDescription& composition, const wxString& path,
	const ibDataNode& params, ibValue& value, wxString& refusal)
{
	const ibDataValue* given = params.FindField(ibMcpValueArgument().Name());

	if (given == nullptr) {
		refusal = ibMcpText("A filter needs a value to compare against.");
		return false;
	}

	const wxString text = given->Kind() == ibDataKind::String ? given->AsString() : wxString();

	// ⭐ A REFERENCE IS WRITTEN THE WAY A SCRIPT WRITES ONE - "Catalog.Warehouses.MainWarehouse",
	// kind, object, then the predefined item's name (or `EmptyRef` for the empty reference). Three
	// parts, because a name alone cannot say which catalogue it belongs to, and the caller already
	// knows all three from the tree.
	if (text.Find(wxT('.')) != wxNOT_FOUND) {

		const wxString kind = text.BeforeFirst(wxT('.'));
		const wxString rest = text.AfterFirst(wxT('.'));
		const wxString objectName = rest.BeforeLast(wxT('.'));
		const wxString member = rest.AfterLast(wxT('.'));

		if (!objectName.IsEmpty() && !member.IsEmpty()) {

			ibValueMetaObject* target = ibFindMetaObject(activeMetaData, kind, objectName);
			ibValueMetaObjectGenericData* owner =
				dynamic_cast<ibValueMetaObjectGenericData*>(target);

			if (owner == nullptr) {
				refusal = wxString::Format(
					ibMcpText("There is no %s called '%s' - metadata_tree shows what there is."),
					kind, objectName);
				return false;
			}

			// ASKED OF THE OBJECT ITSELF, which is the same door a query uses for a named constant:
			// it knows its predefined items and it knows what its empty reference is.
			if (!owner->ResolveQueryConstant(member, value)) {
				refusal = wxString::Format(
					ibMcpText("'%s' has no predefined item called '%s'. predefined_list shows what it has, "
					  "and 'EmptyRef' is the empty reference."), objectName, member);
				return false;
			}

			return true;
		}
	}

	// EVERYTHING ELSE AS IT ARRIVED - a number stays a number, a date a date, a word a word.
	switch (given->Kind()) {
		case ibDataKind::Number: value = ibValue(given->AsNumber()); break;
		case ibDataKind::Bool:   value = ibValue(given->AsBool()); break;
		case ibDataKind::Date: {
			const s64 ms = given->AsDate();
			value = ibValue(ms != 0 ? wxDateTime(wxLongLong(ms)) : wxDateTime());
			break;
		}
		default: value = ibValue(text); break;
	}

	return true;
}

// (⭐ THIS WARNING GREW INTO ibMcpSayComposerComplaints above, and for a reason worth keeping: it
//  named ONE omission, and the same session then produced reports missing a DIFFERENT one - a
//  variant with no name. A check that reports one fault teaches the caller that everything else is
//  fine. The general one asks every question a finished composer has to answer, and every verb
//  that changes a composer ends with it.)

} // namespace

// THE SAME LIST, FOR WHOEVER IS NOT IN THIS FILE — the configuration-wide audit asks it of every
// composition there is (config_check). Written here, beside the verbs that answer with it, so
// "what a composer still lacks" has one definition and cannot come to differ from itself.
void ibMcpComposerComplaints(const ibCompositionDescription& composition,
	std::vector<wxString>& missing)
{
	ComposerComplaints(composition, missing);
}

//---------------------------------------------------------------------------
// report_fields
//---------------------------------------------------------------------------
class ibMcpToolReportFields : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_fields"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("looking at what the composer '%s' can group by"),
			ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("What a composer's query projects - the fields anything in its settings may "
			"name. Ask before grouping or totalling: a path this does not list will store "
			"happily and produce an empty report when somebody runs it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		wxString fault;
		const std::vector<ibQueryConstructorField> fields =
			FieldsOf(composer->GetCompositionDesc(), fault);

		std::vector<ibDataValue> out;
		for (const ibQueryConstructorField& field : fields) {

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("name"), field.m_name);
			if (!field.m_presentation.IsEmpty() && field.m_presentation != field.m_name)
				node->SetValue(wxT("title"), field.m_presentation);
			if (field.m_reference)
				node->AddField(wxT("reference"), ibDataValue::Bool(true));

			out.push_back(ibDataValue::Child(node));
		}

		result.AddField(wxT("fields"), ibDataValue::Array(out));

		// A COMPLAINT AND AN EMPTY LIST ARE DIFFERENT ANSWERS. Half-typed text
		// offers nothing YET; unreadable text offers nothing AT ALL, and only the
		// second is something to fix.
		if (!fault.IsEmpty())
			result.SetValue(wxT("problem"), fault);
		else if (out.empty())
			result.SetValue(wxT("note"),
				ibMcpText("The query projects nothing yet - set the composer's query first."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportFields);

//---------------------------------------------------------------------------
// report_output
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// report_filter
//---------------------------------------------------------------------------
//
// ⭐ WHAT THE REPORT LEAVES OUT - and it belongs to the SETTING, not to the query. A filter written
// into the query text is the author's decision forever; a filter here is the reader's, travels
// with the variant, and can be changed without touching what the report reads.
//
// ⚠ WHERE IT SITS DECIDES WHAT IT DOES (see the composition's own note): on the whole setting it
// narrows everything; on a LEVEL it narrows that grouping alone, which is how "only these
// warehouses, but every product under them" is expressed.
class ibMcpToolReportFilter : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_filter"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("filtering the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Narrow what a report shows - a selection kept in the SETTING rather than written "
			"into the query, so it belongs to the variant and a reader can change it. Give the "
			"field, how to compare (equal, notEqual, greater, less, greaterEqual, lessEqual, "
			"contains, in, inHierarchy) and the value. Pass remove:true with the same path to take "
			"it out. A REFERENCE is written as a script writes one - "
			"'Catalog.Warehouses.MainWarehouse', with 'EmptyRef' for the empty one - and anything "
			"else is taken as it arrives: a number, a date, a word.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgPath(), ArgComparison(), ibMcpValueArgument(), ArgVariant(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		const wxString path = ArgPath().Text(params);
		if (path.IsEmpty()) {
			refusal = ibMcpText("Name the field to filter by - report_fields lists what the query offers.");
			return false;
		}

		std::vector<ibFilterNodeDescription>& nodes = variant->m_settings.m_filter.m_nodes;

		// ⚠ THE FIELD IS THE LEFT OPERAND, not a member of the node: a condition is left, comparison,
		// right, and either side may be a field or a literal. The one being matched here is always
		// the left, because that is the side a caller names.
		const auto found = std::find_if(nodes.begin(), nodes.end(),
			[&path](const ibFilterNodeDescription& node) {
				return node.m_left.m_path.IsSameAs(path, false); });

		if (ArgRemove().Flag(params)) {

			if (found == nodes.end()) {
				refusal = wxString::Format(ibMcpText("There is no filter on '%s'."), path);
				return false;
			}

			nodes.erase(found);
		}
		else {
			if (!PathIsOffered(composition, path, refusal))
				return false;

			// THE WORD, NOT A NUMBER. A comparison spelled out is one a caller can get right from
			// the description; an integer is one they get right by luck.
			const wxString said = ArgComparison().Text(params);
			ibComparisonKind comparison = ibComparisonKind_Equal;

			if (!said.IsEmpty() && !ComparisonFromWord(said, comparison)) {
				refusal = wxString::Format(
					ibMcpText("'%s' is not a comparison. Use equal, notEqual, greater, less, greaterEqual, "
					  "lessEqual, contains, in or inHierarchy."), said);
				return false;
			}

			ibValue value;
			if (!ValueForPath(composition, path, params, value, refusal))
				return false;

			if (found != nodes.end()) {
				found->m_comparison = comparison;
				found->m_right.m_value = value;
			}
			else {
				ibFilterDescription::Append(nodes, path, comparison, value);
			}
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		std::vector<ibDataValue> shown;
		for (const ibFilterNodeDescription& node : nodes)
			shown.push_back(ibDataValue::String(node.m_left.m_path));

		result.AddField(wxT("filters"), ibDataValue::Array(shown));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportFilter);

//---------------------------------------------------------------------------
// report_order
//---------------------------------------------------------------------------
//
// ⭐ THE ORDER ROWS COME OUT IN - and, like the filter, a SETTING rather than a line of the query.
// The order in this list is the order of precedence: first by warehouse, then by amount within it.
class ibMcpToolReportOrder : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_order"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("ordering the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Say what the rows are sorted by - kept in the setting, so it belongs to the "
			"variant. Fields are applied in the order they are added: the first is the primary "
			"sort, the next breaks its ties. `descending` reverses one; remove:true takes one out. "
			"Sorting a GROUPING by one of its own resources is the usual request - 'the biggest "
			"warehouses first' - and it is this verb, not a level.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgPath(), ArgDescending(), ArgVariant(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		const wxString path = ArgPath().Text(params);
		if (path.IsEmpty()) {
			refusal = ibMcpText("Name the field to sort by.");
			return false;
		}

		std::vector<ibSortLineDescription>& lines = variant->m_settings.m_sort.m_lines;

		const auto found = std::find_if(lines.begin(), lines.end(),
			[&path](const ibSortLineDescription& line) {
				return line.m_path.IsSameAs(path, false); });

		if (ArgRemove().Flag(params)) {

			if (found == lines.end()) {
				refusal = wxString::Format(ibMcpText("The rows are not sorted by '%s'."), path);
				return false;
			}

			lines.erase(found);
		}
		else {
			if (!PathIsOffered(composition, path, refusal))
				return false;

			const bool ascending = !ArgDescending().Flag(params);

			if (found != lines.end())
				found->m_ascending = ascending;
			else
				lines.push_back({ path, ascending });
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		std::vector<ibDataValue> order;
		for (const ibSortLineDescription& line : lines) {
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("path"), line.m_path);
			entry->SetValue(wxT("direction"),
				wxString(line.m_ascending ? wxT("ascending") : wxT("descending")));
			order.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("order"), ibDataValue::Array(order));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportOrder);

//---------------------------------------------------------------------------
// report_parameter
//---------------------------------------------------------------------------
//
// ⭐⭐ WHAT THE REPORT ASKS FOR BEFORE IT CAN RUN. A period is the standing case: a report over a
// turnover table without dates composes nothing anybody wants, so the parameter is not optional
// decoration - it is the question the report puts to the person, and it belongs on their screen.
//
// TWO KINDS, AND BOTH ARE ORDINARY:
//   · FOR THE READER - period, organisation, warehouse. Ticked `forUser`, it appears on the form
//     and the person fills it in before generating.
//   · FOR THE AUTHOR - a value set once, in the composition, that the reader never sees. Used as a
//     SWITCH: a query can carry `WHERE (&WithReserves = FALSE OR …)` and a whole branch is turned
//     off by a parameter rather than by a second query text.
//
// ⚠ THE TYPE IS NOT INFERRED FROM THE QUERY (a known gap, 2026-09-02). Say it here - otherwise the
// form has a field it does not know how to draw, and a date parameter arrives as an empty string.
class ibMcpToolReportParameter : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_parameter"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("declaring the parameter '%s'"), ArgName().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Declare what a composer's query asks for - a period, an organisation, a switch - "
			"and say who fills it in. With `forUser` it appears on the report's form for the "
			"person to set before generating; without, it is the author's own value, which is also "
			"how a query branch is switched off (`WHERE (&Flag = FALSE OR ...)`). GIVE IT A TYPE: it "
			"is not deduced from the query text, and a parameter with no type is a field the form "
			"cannot draw. remove:true takes one out.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgName(), ArgType(), ArgForUser(), ibMcpValueArgument(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibMetaData* metaData = activeMetaData;
		ibCompositionDescription composition = composer->GetCompositionDesc();

		const wxString name = ArgName().Text(params);

		auto found = std::find_if(composition.m_parameters.begin(), composition.m_parameters.end(),
			[&name](const ibParameterDescription& parameter) {
				return parameter.m_name.IsSameAs(name, false); });

		if (ArgRemove().Flag(params)) {

			if (found == composition.m_parameters.end()) {
				refusal = wxString::Format(ibMcpText("This composer has no parameter called '%s'."), name);
				return false;
			}

			// ⚠ A PARAMETER THE QUERY MENTIONS CANNOT BE REMOVED - the text is one of its two
			// authors, and dropping it here only means the next re-parse puts it back.
			if (found->m_fromQuery) {
				refusal = wxString::Format(
					ibMcpText("'%s' comes from the query text - take it out of the query instead."), name);
				return false;
			}

			composition.m_parameters.erase(found);
		}
		else {
			if (found == composition.m_parameters.end()) {
				composition.m_parameters.push_back(ibParameterDescription());
				found = composition.m_parameters.end() - 1;
				found->m_name = name;
			}

			// THE TYPE, WHEN GIVEN - by the same words type_list answers with, so one vocabulary
			// covers attributes, dimensions and parameters alike.
			const wxString typeName = ArgType().Text(params);

			if (!typeName.IsEmpty()) {

				ibClassID clsid = 0;
				if (const ibCtorMetaValueType* ctor = metaData->GetTypeCtor(typeName))
					clsid = ctor->GetClassType();
				else if (const ibCtorAbstractType* builtin = ibValue::GetAvailableCtor(typeName))
					clsid = builtin->GetClassType();

				if (clsid == 0) {
					refusal = wxString::Format(
						ibMcpText("'%s' is not a type this configuration knows. type_list shows the "
						  "names."), typeName);
					return false;
				}

				found->m_type = ibTypeDescription();
				found->m_type.SetDefaultMetaType(clsid);
			}

			if (params.FindField(ArgForUser().Name()) != nullptr)
				found->m_userSettable = ArgForUser().Flag(params);

			// The value is stored as it arrived - the blob the composition keeps, which is what a
			// reader's setting will later carry too.
			if (const ibDataValue* value = params.FindField(ibMcpValueArgument().Name()))
				found->m_value.SetField(wxT("value"), *value);
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		std::vector<ibDataValue> declared;

		for (const ibParameterDescription& parameter : composition.m_parameters) {
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), parameter.m_name);
			entry->AddField(wxT("forUser"), ibDataValue::Bool(parameter.m_userSettable));
			entry->AddField(wxT("fromQuery"), ibDataValue::Bool(parameter.m_fromQuery));
			declared.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("parameters"), ibDataValue::Array(declared));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportParameter);

//---------------------------------------------------------------------------
// report_select
//---------------------------------------------------------------------------
//
// 🛑⭐ THE VERB THAT WAS MISSING, AND ITS ABSENCE PRODUCED EMPTY REPORTS. A composer said what to
// GROUP by and what to FOLD, and nothing said what to SHOW - so a report composed its headings
// with no columns under them. The platform warned about it in words on every resource added, and
// a warning nobody can act on is worse than none: it names a fault with no door to fix it
// (measured 2026-09-02 - two reports built through these verbs, both empty by construction).
//
// ⭐⭐ AND THE LIST IS ORDERED, WHICH IS THE OTHER HALF OF WHAT IT DOES. The order of the selected
// fields IS the order of the columns on the page, left to right (Max, 2026-09-02). So this verb
// appends by default and takes a position when the order matters - there is no separate "move a
// column" mechanism to keep in step.
//
// ⚠ AND IT MAKES THE QUERY SMALLER FOR FREE: a field nobody selected takes no part in the
// selection - not read, not fetched, not rendered.
class ibMcpToolReportSelect : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_select"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("choosing what the report shows in '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Say WHICH FIELDS the report shows, and in what order - the columns a person sees. "
			"A composer that groups and totals but selects nothing composes headings with no "
			"figures under them, so this belongs beside report_level and report_resource rather "
			"than after them. The order of these is the order of the columns; `at` inserts at a "
			"position instead of appending. Pass remove:true with the same path to take one out. "
			"report_fields lists what the query offers.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgOnePath(), ArgPaths(), ArgAt(), ArgVariant(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		// ONE FIELD OR MANY, read into one list so the loop below is the only place that knows how.
		std::vector<wxString> wanted;

		if (const ibDataValue* many = params.FindField(ArgPaths().Name())) {
			if (many->Kind() == ibDataKind::Array)
				for (const ibDataValue& one : many->AsArray())
					if (one.Kind() == ibDataKind::String && !one.AsString().IsEmpty())
						wanted.push_back(one.AsString());
		}

		if (const wxString path = ArgOnePath().Text(params); !path.IsEmpty())
			wanted.push_back(path);

		if (wanted.empty()) {
			refusal = ibMcpText("Name the field to show - `path` for one, `paths` for several. "
				  "report_fields lists what the query projects.");
			return false;
		}

		std::vector<ibSelectedFieldDescription>& selected = variant->m_settings.m_selected;

		// ⭐ ONE BAD NAME DOES NOT UNDO THE REST, and it is not swallowed either: each is reported
		// on by name, and the answer below shows the order that actually resulted. A call that
		// refused six good columns because the seventh was misspelled would teach a caller to go
		// back to one field per call, which is what this is here to end.
		std::vector<ibDataValue> refused;
		s32 at = (s32)ArgAt().Whole(params);

		for (const wxString& path : wanted) {

			const auto found = std::find_if(selected.begin(), selected.end(),
				[&path](const ibSelectedFieldDescription& field) {
					return field.m_path.IsSameAs(path, false); });

			wxString fault;

			if (ArgRemove().Flag(params)) {

				if (found == selected.end())
					fault = wxString::Format(ibMcpText("'%s' is not shown here."), path);
				else
					selected.erase(found);
			}
			// ⚠ A FIELD THE QUERY DOES NOT PROJECT stores happily and shows nothing - the same
			// silent emptiness a level with a wrong path produces, which is why both are checked
			// against what the query actually offers rather than accepted on trust.
			else if (!PathIsOffered(composition, path, fault)) {
				// fault already says what the query does offer
			}
			else if (found != selected.end()) {
				fault = wxString::Format(
					ibMcpText("'%s' is already shown - remove it first to put it somewhere else."), path);
			}
			else if (at > 0 && (size_t)at <= selected.size()) {
				selected.insert(selected.begin() + (at - 1), ibSelectedFieldDescription::Field(path));
				at++;   // …and the next of the list goes after it, keeping the given order
			}
			else {
				selected.push_back(ibSelectedFieldDescription::Field(path));
			}

			if (!fault.IsEmpty())
				refused.push_back(ibDataValue::String(fault));
		}

		// EVERY ONE OF THEM WRONG IS A REFUSAL, not a result: nothing changed, and answering
		// `shows` as though something had would be a lie the caller acts on.
		if (refused.size() == wanted.size()) {
			refusal = refused.front().AsString();
			return false;
		}

		if (!refused.empty())
			result.AddField(wxT("refused"), ibDataValue::Array(refused));

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		// ANSWERED WITH THE WHOLE ORDER, because that is what was changed - a caller adding three
		// columns sees the left-to-right result without asking again.
		std::vector<ibDataValue> shown;
		for (const ibSelectedFieldDescription& field : selected)
			shown.push_back(ibDataValue::String(field.IsAuto() ? wxT("(auto)") : field.m_path));

		result.AddField(wxT("shows"), ibDataValue::Array(shown));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportSelect);

//---------------------------------------------------------------------------
// report_variant
//---------------------------------------------------------------------------
//
// ⭐⭐ ONE REPORT ANSWERS SEVERAL QUESTIONS, and this is how. A variant is a NAMED setting over the
// same query - "Sales", "Sales with gross margin", "Sales by manager" - and the person running the
// report picks one by that name. Building three reports for those is three queries to keep in step
// for one question asked three ways (Max, 2026-09-02).
//
// 🛑 THE NAME IS NOT DECORATION, which is why this verb exists at all. It is the whole of what a
// variant adds to a setting, and a nameless one is a variant nobody can choose - so the composer
// now refuses to save with one (ibValueMetaObjectComposer::OnSaveMetaObject). A rule with no door
// to satisfy it would be a trap; this is the door.
class ibMcpToolReportVariant : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_variant"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ArgAdd().Flag(params)
			? wxString::Format(ibMcpText("adding the variant '%s'"), ArgName().Text(params))
			: wxString::Format(ibMcpText("naming a variant of the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Name a composer's variant, or add another one. A variant is a NAMED setting over "
			"the same query - 'Sales', 'Sales with gross margin' - and the name is what the person "
			"running the report picks it by, so a composer will not save with a nameless one. "
			"Without `add` it names the variant you point at (the first, unless `variant` says "
			"otherwise); with `add` it makes a new one starting from that variant's settings, "
			"which is how a second view of the same figures is built. remove:true takes one out.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgName(), ArgSynonym(), ArgVariant(), ArgAdd(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		const wxString name = ArgName().Text(params);
		const wxString synonym = ArgSynonym().Text(params);

		if (composition.m_variants.empty())
			composition.m_variants.emplace_back();

		const auto named = [&composition](const wxString& wanted) {
			return std::find_if(composition.m_variants.begin(), composition.m_variants.end(),
				[&wanted](const ibVariantDescription& v) { return v.m_name.IsSameAs(wanted, false); });
		};

		// TAKEN OUT — by name, and never the last one: a composer with no variant has nowhere to
		// keep the settings it composes on.
		if (ArgRemove().Flag(params)) {

			const auto found = named(name);
			if (found == composition.m_variants.end()) {
				refusal = wxString::Format(ibMcpText("This composer has no variant called '%s'."), name);
				return false;
			}

			if (composition.m_variants.size() == 1) {
				refusal = ibMcpText("That is the only variant there is - a composer composes on one, so "
					"rename it rather than removing it.");
				return false;
			}

			composition.m_variants.erase(found);
		}
		else if (ArgAdd().Flag(params)) {

			if (named(name) != composition.m_variants.end()) {
				refusal = wxString::Format(
					ibMcpText("This composer already has a variant called '%s' - a name is how one is "
					  "picked, so two of them cannot share it."), name);
				return false;
			}

			// ⭐ STARTED FROM ANOTHER ONE, because that is what a second view IS: the same report
			// with something added or taken away. Building it from empty would mean stating the
			// whole structure again, and the two would drift the first time the first one changed.
			const ibVariantDescription* from =
				VariantOf(composition, ArgVariant().Text(params), refusal);
			if (from == nullptr)
				return false;

			ibVariantDescription made = *from;
			made.m_name = name;
			made.m_synonym = synonym;

			composition.m_variants.push_back(made);
		}
		else {
			ibVariantDescription* variant =
				VariantOf(composition, ArgVariant().Text(params), refusal);
			if (variant == nullptr)
				return false;

			if (!name.IsEmpty()) {
				const auto clash = named(name);
				if (clash != composition.m_variants.end() && &(*clash) != variant) {
					refusal = wxString::Format(
						ibMcpText("Another variant is already called '%s'."), name);
					return false;
				}
				variant->m_name = name;
			}

			if (params.FindField(ArgSynonym().Name()) != nullptr)
				variant->m_synonym = synonym;
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		// ANSWERED WITH THE WHOLE LIST, so a caller sees what the picker will show without asking
		// again - and sees at once whether anything is still nameless.
		std::vector<ibDataValue> variants;

		for (const ibVariantDescription& variant : composition.m_variants) {
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), variant.m_name);
			if (!variant.m_synonym.IsEmpty())
				entry->SetValue(wxT("synonym"), variant.m_synonym);
			variants.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("variants"), ibDataValue::Array(variants));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportVariant);

class ibMcpToolReportOutput : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_output"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("adding an output to the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Add an output to a composer - one report shape. A composer with no output "
			"produces nothing, so this is the first thing after the query. Pass remove:true with "
			"its name to take one out again.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgName(), ArgVariant(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		const wxString name = ArgName().Text(params);

		// TAKEN OUT, when that is what was asked — see ArgRemove.
		if (ArgRemove().Flag(params)) {

			auto& structure = variant->m_settings.m_structure;

			const auto found = std::find_if(structure.begin(), structure.end(),
				[&name](const ibOutputDescription& output) { return output.m_name.IsSameAs(name, false); });

			if (found == structure.end()) {
				refusal = wxString::Format(
					ibMcpText("This variant has no output called '%s'."), name);
				return false;
			}

			structure.erase(found);

			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);

			result.AddField(wxT("removed"), ibDataValue::Bool(true));
			result.SetValue(wxT("output"), name);
			return true;
		}

		for (const ibOutputDescription& existing : variant->m_settings.m_structure) {
			if (existing.m_name.IsSameAs(name, false)) {
				refusal = wxString::Format(
					ibMcpText("This variant already has an output called '%s'."), name);
				return false;
			}
		}

		ibOutputDescription output;
		output.m_name = name;
		variant->m_settings.m_structure.push_back(output);

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		result.AddField(wxT("added"), ibDataValue::Bool(true));
		result.SetValue(wxT("output"), name);
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportOutput);

//---------------------------------------------------------------------------
// report_level
//---------------------------------------------------------------------------
class ibMcpToolReportLevel : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_level"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("grouping '%s' by %s"),
			ibMcpNameOf(params), ArgGroupBy().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Add a grouping level to an output - down the ROWS by default, across the "
			"COLUMNS when asked, which is what makes a cross table. The path must be one the "
			"query projects: report_fields lists them, and a path that is not there is refused.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgOutput(), ArgGroupBy(), ArgColumns(), ArgVariant() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		const wxString outputName = ArgOutput().Text(params);

		ibOutputDescription* output = nullptr;
		wxString available;
		for (ibOutputDescription& candidate : variant->m_settings.m_structure) {
			available << (available.IsEmpty() ? wxT("") : wxT(", ")) << candidate.m_name;
			if (candidate.m_name.IsSameAs(outputName, false))
				output = &candidate;
		}

		if (output == nullptr) {
			refusal = available.IsEmpty()
				? ibMcpText("This variant has no output yet - add one with report_output.")
				: wxString::Format(ibMcpText("There is no output called '%s'. It has: %s."),
					outputName, available);
			return false;
		}

		const wxString path = ArgGroupBy().Text(params);

		ibLevelDescription level;

		if (path.IsEmpty()) {
			// A DETAIL LEVEL groups by nothing on purpose: it is the rows. Said by
			// its KIND rather than by an empty grouping, so "nothing yet" and
			// "nothing, deliberately" stay different things.
			level.m_kind = ibCompositionLevelKind::Details;
		}
		else {
			if (!PathIsOffered(composition, path, refusal))
				return false;

			level.m_kind = ibCompositionLevelKind::Grouping;
			level.m_settings.m_group.Append(path, ibQueryDimUnfold::Elements);
		}

		const bool columns = ArgColumns().Flag(params);
		if (columns) output->m_columnGroups.push_back(level);
		else         output->m_rowGroups.push_back(level);

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		result.AddField(wxT("added"), ibDataValue::Bool(true));
		result.SetValue(wxT("output"), outputName);
		result.SetValue(wxT("where"), wxString(columns ? wxT("columns") : wxT("rows")));
		ibMcpSayComposerComplaints(composition, result);
		if (!path.IsEmpty())
			result.SetValue(wxT("groupBy"), path);
		else
			result.AddField(wxT("detail"), ibDataValue::Bool(true));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportLevel);

//---------------------------------------------------------------------------
// report_resource
//---------------------------------------------------------------------------
class ibMcpToolReportResource : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_resource"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("totalling %s in '%s'"),
			ArgPath().Text(params), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Declare a resource - what the levels FOLD. Either a function over a field "
			"(SUM over Quantity) or a whole expression, which is checked by the compiler. The "
			"resource has no caption of its own: it is built on a field, and the field holds "
			"the title. Pass remove:true with the same path to take one out again.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgFunction(), ArgPath(), ArgName(), ArgOver(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		const wxString function = ArgFunction().Text(params);
		const wxString path = ArgPath().Text(params);

		// TAKEN OUT, when that is what was asked — the inverse this verb was missing. Matched on
		// the PATH, which is what a resource is about; the alias is optional and the function is
		// how it folds, so neither identifies one.
		if (ArgRemove().Flag(params)) {

			auto& resources = composition.m_resources;

			const auto found = std::find_if(resources.begin(), resources.end(),
				[&path](const ibResourceDescription& resource) { return resource.m_path.IsSameAs(path, false); });

			if (found == resources.end()) {
				refusal = wxString::Format(ibMcpText("This composer has no resource over '%s'."), path);
				return false;
			}

			resources.erase(found);

			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);

			result.AddField(wxT("removed"), ibDataValue::Bool(true));
			result.SetValue(wxT("path"), path);
			return true;
		}

		// ⭐ TWO CASES, TWO JUDGES. A function over a FIELD is checked against what
		// the query projects; a bare expression is checked by the COMPILER, because
		// that is what will run it. Sending the second to the first would refuse
		// every legitimate expression, and the reverse would accept every typo.
		if (!function.IsEmpty()) {
			if (!PathIsOffered(composition, path, refusal))
				return false;
		}

		ibResourceDescription resource;
		resource.m_func = function;
		resource.m_path = path;
		resource.m_alias = ArgName().Text(params);
		resource.m_scope = ArgOver().Text(params);

		composition.m_resources.push_back(resource);

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		result.AddField(wxT("added"), ibDataValue::Bool(true));
		if (function.IsEmpty()) {
			result.SetValue(wxT("expression"), path);
			result.SetValue(wxT("note"),
				ibMcpText("An expression is not checked here - script_check compiles it, which is what "
				  "the settings window does before it closes."));
		}
		else {
			result.SetValue(wxT("function"), function);
			result.SetValue(wxT("path"), path);
		}

		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportResource);

//---------------------------------------------------------------------------
// report_query
//---------------------------------------------------------------------------
//
// ⭐ THE THING EVERY OTHER VERB HERE STANDS ON, and the one that was missing. A composer's settings
// could be read and edited — outputs, levels, resources — while the QUERY they are settings OVER
// could only be read. A report was therefore inspectable and unbuildable: the first step of the
// branch had no door.
//
// ⭐⭐ AND IT REFUSES BEFORE IT WRITES. Every grouping and every resource is a path INTO this text;
// storing a query that does not resolve would leave the composer holding settings that point at
// nothing, and each of them would then be refused one at a time with no sign of the common cause.
// So the query is judged first — by the same parser and the same name-check query_check uses, which
// is what "the composer is the judge" means in practice — and the composer is left untouched when
// the answer is no.
//
class ibMcpToolReportQuery : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_query"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("setting a composer's query");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The query a composer composes OVER - what it reads before anything is grouped or "
			"totalled. Refused, and nothing stored, when the text does not parse or names "
			"something this configuration does not have: query_sources and query_fields say what "
			"it does have, and report_fields then lists what the stored query offers to group and "
			"total by.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		// The KIND is checked, the way module_write checks it: an argument that is not a string
		// reads as empty and would silently clear the query instead of setting it.
		const ibDataValue* incoming = params.FindField(ArgText().Name());

		if (incoming == nullptr || incoming->Kind() != ibDataKind::String) {
			refusal = ibMcpText("'text' must be a string holding the query. Nothing was written.");
			return false;
		}

		const wxString text = incoming->AsString();

		// AN EMPTY QUERY IS A LEGITIMATE ASK — it is how a composer is cleared — and there is
		// nothing in it to judge.
		if (!text.IsEmpty()) {

			try {
				ibQueryParser parser;
				const ibQueryPackage package = parser.ParsePackage(text);

				// Against THIS configuration, named explicitly: a tool is not standing inside one.
				const ibSourceMetaDataScope resolveAgainst(activeMetaData);
				ibQueryLowering::CheckNames(package, std::map<wxString, ibValue>());
			}
			// THE TWO VARIETIES, CAUGHT BY TYPE — a name that does not exist, and a text that does not
			// parse. This used to read the POSITION to tell them apart (0:0 meant a name), which
			// stopped being true the moment an unresolved source learnt to point at its own FROM.
			catch (const ibBackendQueryNameException& e) {
				refusal = wxString::Format(ibMcpText("The query names something this configuration does "
					"not have: %s. Nothing was written."), e.GetErrorDescription());
				return false;
			}
			catch (const ibBackendQuerySourceException& e) {
				refusal = wxString::Format(ibMcpText("The query does not parse at %i:%i - %s. Nothing was "
					"written."), (int)e.GetLine(), (int)e.GetColumn(), e.GetErrorDescription());
				return false;
			}
			catch (const ibBackendException& e) {
				refusal = wxString::Format(
					ibMcpText("The query was refused: %s. Nothing was written."), e.GetErrorDescription());
				return false;
			}
		}

		ibCompositionDescription composition = composer->GetCompositionDesc();
		composition.m_query = text;
		composer->SetCompositionDesc(composition);

		activeMetaData->Modify(true);

		result.SetValue(wxT("composer"), composer->GetName());
		result.AddField(wxT("characters"), ibDataValue::Int((s64)text.Length()));

		// ⭐ WHAT IT NOW OFFERS, in the same breath. The next question after "the query is set" is
		// always "so what can I group by", and answering it here saves the round trip — and shows
		// immediately whether the text projects what the caller thought it did.
		wxString fault;
		const std::vector<ibQueryConstructorField> fields =
			ibQueryFieldsOfText(text, activeMetaData, &fault);

		std::vector<ibDataValue> names;
		for (const ibQueryConstructorField& field : fields)
			names.push_back(ibDataValue::String(field.m_name));   // the name the AST carries

		result.AddField(wxT("fields"), ibDataValue::Array(names));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportQuery);

//---------------------------------------------------------------------------
// report_set
//---------------------------------------------------------------------------
//
// ⭐⭐ THE OTHER HALF OF report_get, AND DELIBERATELY THE SAME SHAPE.
//
// Everything else in this file states one thing at a time in words — add an output, add a level,
// set a resource — which is the right way to build a report by hand and the wrong way to move one.
// A caller that has READ a composition has the whole of it already; asking it to replay that
// through nine verbs is asking it to translate a structure into a sequence and back.
//
// So this takes what report_get gave. Max, 2026-09-01: *"you can hand it the schema it works out,
// and fill an already-existing one with it."* — which is literally what happens below:
// ibCompositionDescriptionMemory::ReadNode fills the composer's LIVE description from the node,
// field by field, with the same reader the file uses.
//
// ⭐ AND THE SHAPE IS PUBLISHED, not documented. The argument carries an EMPTY composition written
// by the same family (see ibMcpArgument::m_shape), so `tools/list` shows a caller the exact
// structure it may send — produced by the thing that reads it, never by a second description of it.
//
// ⚠ IT REPLACES, and says so. A composition is one value; there is no merging a half of it, and a
// caller that wants a change reads, edits and sends back — which is why the read exists.
class ibMcpToolReportSet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_set"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("setting the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Replace a composer's WHOLE composition with one you were given by report_get - "
			"the query, the selects, the resources, the parameters and every variant with its "
			"outputs and levels. Read it, change what you need, send it back. The other report_* "
			"verbs each state ONE thing and are for building by hand; this is for moving a report "
			"that already exists, and it replaces rather than merges.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgComposition() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectComposer* composer =
			object->ConvertToType<ibValueMetaObjectComposer>();
		if (composer == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a composer. A report declares one under itself."),
				object->GetName());
			return false;
		}

		const ibDataNode* given = params.FindChild(ArgComposition().Name());
		if (given == nullptr) {
			refusal = ibMcpText("No composition given. report_get answers with the shape this takes.");
			return false;
		}

		// ⭐ FILLED INTO A FRESH ONE, THEN PLACED. Reading straight into the live description would
		// leave a half-read composition standing in the configuration if the read failed partway —
		// and a partly-read report is the kind of wrong that looks like an edit.
		ibCompositionDescription composition;

		if (!ibCompositionDescriptionMemory::ReadNode(*given, composition, activeMetaData)) {
			refusal = ibMcpText("That is not a composition this platform can read. Send back the shape "
				"report_get gives, with your changes in it.");
			return false;
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		// ANSWERED WITH WHAT IT NOW HOLDS, read back off the composer rather than echoed from the
		// argument: what was asked for and what was taken are different facts.
		result.SetValue(wxT("composer"), composer->GetName());

		if (!ibCompositionDescriptionMemory::WriteNode(result, composer->GetCompositionDesc())) {
			refusal = ibMcpText("The composition was placed, but could not be read back to confirm it.");
			return false;
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportSet);
