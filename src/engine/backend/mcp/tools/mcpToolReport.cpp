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

#include "backend/backend_composition.h"
#include "core/composition/compositionTheme.h"      // ibCompositionThemes — the palettes report_other_settings offers
#include "backend/system/value/valueColour.h"        // report_conditional_appearance — a colour, kept as the value it is
#include "backend/system/value/valueFont.h"          // …a font
#include "core/description/spreadsheetDescription.h"             // …said against the report's own (ibDefaultSpreadsheetFont())
#include "backend/composition/drivers/compositionDriver.h"    // …as the parts it changes (ibCompositionFont)
#include "backend/system/value/valueSpreadsheet.h"   // …and an alignment, the sheet's own enumeration
#include "backend/metaCollection/metaComposerObject.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/genericData.h"   // ResolveQueryConstant — a named item as a value
#include "backend/objCtor.h"                      // ibCtorMetaValueType — a type looked up by name
#include "core/description/typeDescription.h"              // …and the description a parameter carries
#include "backend/metadataConfiguration.h"
#include "backend/query/queryConstructorModel.h"   // ibQueryFieldsOfText — what the TEXT offers
#include "backend/query/queryException.h"          // …and the judge that refuses one
#include "backend/query/queryLowering.h"
#include "core/query/queryParser.h"
#include "core/query/queryRender.h"    // ibQueryColumnFromPath — a dotted path laid out as hops, as the composer lays it
#include "core/stringTokenizer.h"
#include "backend/query/queryable.h"

#include <algorithm>   // std::remove_if — taking a grouping level back out

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgId()
{
	static const ibArg s_a(ibT("id"), ibArg::Kind::Whole,
		ibMcpText("The composer's NodeId - a report declares one, metadata_get on the report "
			  "lists it among its children."), /*required*/ true);
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(ibT("name"), ibArg::Kind::Text,
		ibMcpText("What to call it - how a reader will pick it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgVariant()
{
	static const ibArg s_a(ibT("variant"), ibArg::Kind::Text,
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
	static const ibArg s_a(ibT("remove"), ibArg::Kind::Flag,
		ibMcpText("Take it OUT instead of putting it in. Off by default."));
	return s_a;
}

// THE COMPARISON AS A WORD. Spelled out in the schema and matched here, so the two cannot drift -
// and so a caller reading the description knows the whole vocabulary without guessing at numbers.
// ONE TABLE: the schema's choices, the descriptions and the refusals are all read off it.
struct ibComparisonWord { const ibChar* m_word; ibComparisonKind m_kind; };
const std::vector<ibComparisonWord>& ComparisonWords()
{
	static const std::vector<ibComparisonWord> s_words = {
		{ ibT("equal"),        ibComparisonKind_Equal },
		{ ibT("notEqual"),     ibComparisonKind_NotEqual },
		{ ibT("greater"),      ibComparisonKind_Greater },
		{ ibT("less"),         ibComparisonKind_Less },
		{ ibT("greaterEqual"), ibComparisonKind_GreaterEqual },
		{ ibT("lessEqual"),    ibComparisonKind_LessEqual },
		{ ibT("contains"),     ibComparisonKind_Contains },
		{ ibT("in"),           ibComparisonKind_In },
		{ ibT("inHierarchy"),  ibComparisonKind_InHierarchy },
		{ ibT("filled"),       ibComparisonKind_Filled },
		{ ibT("notFilled"),    ibComparisonKind_NotFilled },
	};
	return s_words;
}

// …as a list a sentence can carry: "equal, notEqual, …, notFilled".
ibString ComparisonWordList()
{
	ibString text;
	for (const ibComparisonWord& entry : ComparisonWords())
		text += (text.IsEmpty() ? ibString() : ibString(ibT(", "))) + entry.m_word;
	return text;
}

const ibArg& ArgComparison()
{
	static const ibArg s_a(ibT("comparison"), ibArg::Kind::Text,
		ibString::Format(ibMcpText("How to compare - %s; equal is the default. filled and notFilled ask whether "
			"the field has a value at all (none = NULL or the empty value of its type) and take no value."),
			ComparisonWordList()),
		/*required*/ false,
		[] {
			std::vector<ibString> words;
			for (const ibComparisonWord& entry : ComparisonWords())
				words.push_back(entry.m_word);
			return words;
		}());
	return s_a;
}

const ibArg& ArgDescending()
{
	static const ibArg s_a(ibT("descending"), ibArg::Kind::Flag,
		ibMcpText("Largest first. Off means ascending, which is the ordinary case."));
	return s_a;
}

const ibArg& ArgType()
{
	static const ibArg s_a(ibT("type"), ibArg::Kind::Text,
		ibMcpText("What kind of value it holds, in the words type_list answers with - Date, Number, "
			  "String, Boolean, CatalogRef.Warehouses. Not deduced from the query: say it."));
	return s_a;
}

const ibArg& ArgForUser()
{
	static const ibArg s_a(ibT("forUser"), ibArg::Kind::Flag,
		ibMcpText("Put it on the report's form for the person to fill in before generating. Off means the "
			  "author sets it and the reader never sees it - which is how a query branch is "
			  "switched on and off."));
	return s_a;
}

const ibArg& ArgAt()
{
	static const ibArg s_a(ibT("at"), ibArg::Kind::Whole,
		ibMcpText("Where in the order it goes, 1 for first. Omit to append - and the order of these IS the "
			  "order of the columns on the page."));
	return s_a;
}

const ibArg& ArgSynonym()
{
	static const ibArg s_a(ibT("synonym"), ibArg::Kind::Text,
		ibMcpText("What the PERSON sees in the picker - in EVERY language the configuration declares, in "
			  "one string: `en = 'Main'; ru = '...'; uk = '...';`. A plain text is refused where there "
			  "are several languages: it would fill the configuration's own one and leave the rest blank. "
			  "Omit and the name is read out loud instead."));
	return s_a;
}

const ibArg& ArgAdd()
{
	static const ibArg s_a(ibT("add"), ibArg::Kind::Flag,
		ibMcpText("Make a NEW variant of this name, starting from the settings of the one named by "
			  "`variant` (or the first). Off by default, which renames instead."));
	return s_a;
}

const ibArg& ArgOutput()
{
	static const ibArg s_a(ibT("output"), ibArg::Kind::Text,
		ibMcpText("Which output."), /*required*/ true);
	return s_a;
}

const ibArg& ArgGroupBy()
{
	static const ibArg s_a(ibT("groupBy"), ibArg::Kind::Text,
		ibMcpText("The field to group by. Omit for a DETAIL level - the rows themselves. "
			"With `remove`, the level to take out, named by this same field."));
	return s_a;
}

// ⭐⭐ SEVERAL FIELDS IN ONE LEVEL, because an object's own facts are not a hierarchy.
//
// A level's grouping has always been a LIST (ibLevelDescription::m_group is Appended to), and this
// tool only ever put one thing in it — so the only way to show an asset's inventory number, method
// and life beside its name was a level each, and the report came out as a four-deep ladder repeating
// one row four times. They are not four questions; they are one row's four columns.
const ibArg& ArgGroupByMany()
{
	static const ibArg s_a(ibT("groupByAll"), ibArg::Kind::Many,
		ibMcpText("Several fields forming ONE level, in the order they read - "
			"[\"FixedAsset\", \"InventoryNumber\", \"Method\"] gives one line per asset carrying all "
			"three, where a level each would nest them and repeat the row. Use this whenever the "
			"extra fields are FACTS OF the thing already grouped rather than groupings of their own; "
			"a level each is right only when each one genuinely subdivides the one above it."));
	return s_a;
}

const ibArg& ArgColumns()
{
	static const ibArg s_a(ibT("columns"), ibArg::Kind::Flag,
		ibMcpText("Put it across the columns instead of down the rows."));
	return s_a;
}

// ⭐ A LEVEL BY PERIODS — `BY Period PERIODS(Month, &From, &To)` in the query's words, the settings window's
// periodicity in a person's. It was the one thing a level holds that no verb could say, so a report by month
// was built by rewriting the whole composition with report_set (2026-09-29).
const ibArg& ArgPeriods()
{
	static const std::vector<ibString> s_units = [] {
		std::vector<ibString> units;
		for (const auto& unit : ibPeriodUnits())
			units.push_back(unit.second);
		return units;
	}();
	static const ibArg s_a(ibT("periods"), ibArg::Kind::Text,
		ibMcpText("Group a DATE field by periods of this length - one heading per month, per day - instead of by "
			"its every value. One field only, named by `groupBy`."), false, s_units);
	return s_a;
}

const ibArg& ArgPeriodsFrom()
{
	static const ibArg s_a(ibT("periodsFrom"), ibArg::Kind::Text,
		ibMcpText("With `periods`: the first period shown even with nothing in it - a parameter (`&From`) or a "
			"date. Omit to start at the earliest period in the data."));
	return s_a;
}

const ibArg& ArgPeriodsTo()
{
	static const ibArg s_a(ibT("periodsTo"), ibArg::Kind::Text,
		ibMcpText("With `periods`: the last period shown - a parameter (`&To`) or a date. Omit to end at the "
			"latest period in the data."));
	return s_a;
}

const ibArg& ArgFunction()
{
	static const ibArg s_a(ibT("function"), ibArg::Kind::Text,
		ibMcpText("SUM, MIN, MAX, COUNT... Omit when `path` is a whole expression."));
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(ibT("path"), ibArg::Kind::Text,
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
	static const ibArg s_a(ibT("path"), ibArg::Kind::Text,
		ibMcpText("The field to show. One; use `paths` for several, in the order they should appear."));
	return s_a;
}

const ibArg& ArgPaths()
{
	static const ibArg s_a(ibT("paths"), ibArg::Kind::Many,
		ibMcpText("Several fields at once, instead of `path` - and their order is the order of the "
			  "columns. One that the query does not offer is refused by name and the rest are "
			  "still added, so a typo costs one field rather than the call."));
	return s_a;
}

const ibArg& ArgOver()
{
	static const ibArg s_a(ibT("over"), ibArg::Kind::Text,
		ibMcpText("The grouping it is computed over. Omit for the ladder - one figure per heading, "
			  "following whatever the reader regrouped."));
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(ibT("text"), ibArg::Kind::Text,
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
	static const ibArg s_a(ibT("composition"), ibArg::Kind::Node,
		ibMcpText("The whole composition, in the shape report_get answers with. Read it, change what you "
		  "need and send it back - the example below is an empty one, written by the same reader "
		  "that will take yours."),
		/*required*/ true, std::vector<ibString>(),
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

	ibString GetName() const override { return ibT("report_get"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("reading the composer '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectComposer* composer =
			object->ConvertToType<ibValueMetaObjectComposer>();
		if (composer == nullptr) {
			refusal = ibString::Format(
				ibMcpText("'%s' is not a composer. A report declares one under itself."),
				object->GetName());
			return false;
		}

		const ibCompositionDescription& composition = composer->GetCompositionDesc();

		result.SetValue(ibT("composer"), object->GetName());

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
			refusal = ibString::Format(
				ibMcpText("'%s' could not describe itself. The composition is there; reading it out failed."),
				object->GetName());
			return false;
		}

		if (composition.m_variants.empty()
			|| composition.m_variants.front().m_settings.m_structure.empty())
			result.SetValue(ibT("note"),
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
	ibString& fault)
{
	return ibQueryFieldsOfText(composition.m_query, activeMetaData, &fault);
}

// The composer a caller named, with the refusals said in words.
ibValueMetaObjectComposer* FindComposer(const ibDataNode& params, ibString& refusal)
{
	ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
	if (object == nullptr)
		return nullptr;

	ibValueMetaObjectComposer* composer = object->ConvertToType<ibValueMetaObjectComposer>();
	if (composer == nullptr) {
		refusal = ibString::Format(
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
	const ibString& path, ibString& refusal)
{
	ibString fault;
	const std::vector<ibQueryConstructorField> fields = FieldsOf(composition, fault);

	if (!fault.IsEmpty()) {
		refusal = ibString::Format(
			ibMcpText("The composer's query cannot be read, so nothing can be checked against it: %s"),
			fault);
		return false;
	}

	// ⭐ A PATH, NOT A NAME — walked as the composer walks it when it runs (ibDataDBComposer::WalkPath: where it
	// starts among the query's fields, then each hop by its type), so a field of what a selected reference points
	// at, `Goods.Unit`, is offered exactly as far as a run reads it. Compared with the names alone, every such
	// field was refused, and the query got edited to select what a dot already reaches (2026-10-28).
	const ibQueryAstExprPtr column = ibQueryColumnFromPath(path);
	if (column && !column->m_path.empty()
		&& !ibQueryConstructorModel(activeMetaData).WalkPath(fields, column->m_path).m_name.IsEmpty())
		return true;

	ibString available, references;
	for (const ibQueryConstructorField& field : fields) {
		available << (available.IsEmpty() ? ibT("") : ibT(", ")) << field.m_name;
		if (field.m_reference)
			references << (references.IsEmpty() ? ibT("") : ibT(", ")) << field.m_name;
	}

	refusal = available.IsEmpty()
		? ibMcpText("The composer's query projects no fields yet - give it a query first.")
		: ibString::Format(ibMcpText("'%s' is not a field this query offers. It has: %s."),
			path, available);

	// …and says where a dot goes, because the field asked for is most often one behind a reference.
	if (!references.IsEmpty())
		refusal << ibString::Format(ibMcpText(" A dot reaches the fields of what a reference points at, as deep as "
			"it goes - '%s.<field>'; the references here: %s."), references.BeforeFirst(ibT(',')), references);
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
	std::vector<ibString>& missing)
{
	const auto say = [&missing](const ibString& what) { missing.push_back(what); };

	if (composition.m_query.IsEmpty())
		say(ibMcpText("no query - report_query says what this composes over"));

	for (size_t index = 0; index < composition.m_variants.size(); index++) {
		if (composition.m_variants[index].m_name.IsEmpty()) {
			say(ibString::Format(
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
	std::vector<ibString> complaints;
	ComposerComplaints(composition, complaints);

	if (complaints.empty())
		return;

	std::vector<ibDataValue> missing;
	for (const ibString& one : complaints)
		missing.push_back(ibDataValue::String(one));

	result.AddField(ibT("incomplete"), ibDataValue::Array(missing));
	result.SetValue(ibT("nextStep"),
		ibMcpText("The lines above are what this composer still lacks to produce a report somebody can "
		  "read."));
}

ibVariantDescription* VariantOf(ibCompositionDescription& composition,
	const ibString& name, ibString& refusal)
{
	if (composition.m_variants.empty())
		composition.m_variants.emplace_back();

	if (name.IsEmpty())
		return &composition.m_variants.front();

	for (ibVariantDescription& variant : composition.m_variants) {
		if (variant.m_name.IsSameAs(name, false))
			return &variant;
	}

	ibString available;
	for (const ibVariantDescription& variant : composition.m_variants) {
		available << (available.IsEmpty() ? ibT("") : ibT(", "))
			<< (variant.m_name.IsEmpty() ? ibMcpText("(the author's)") : variant.m_name);
	}

	refusal = ibString::Format(ibMcpText("This composer has no variant called '%s'. It has: %s."),
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
// (ComparisonWords — the vocabulary — stands with the arguments at the top of the file: the schema is built from it.)

// THE WORD A CALLER SAID — none is `equal`; one that names no comparison is refused here, in the one sentence every
// verb that takes a comparison answers with.
bool ComparisonFromWord(const ibString& word, ibComparisonKind& kind, ibString& refusal)
{
	kind = ibComparisonKind_Equal;
	if (word.IsEmpty())
		return true;
	for (const ibComparisonWord& entry : ComparisonWords()) {
		if (word.IsSameAs(entry.m_word, false)) {
			kind = entry.m_kind;
			return true;
		}
	}

	refusal = ibString::Format(ibMcpText("'%s' is not a comparison. Use one of: %s."), word, ComparisonWordList());
	return false;
}

// …and back, for an answer that reads a condition out in the words it was said in.
ibString WordOfComparison(ibComparisonKind kind)
{
	for (const ibComparisonWord& entry : ComparisonWords())
		if (entry.m_kind == kind)
			return entry.m_word;
	return ibEmptyString;
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
bool ValueForPath(const ibCompositionDescription& composition, const ibString& path,
	const ibDataNode& params, ibValue& value, ibString& refusal)
{
	auto given = params.FindField(ibMcpValueArgument().Name());

	if (!given) {
		refusal = ibMcpText("A filter needs a value to compare against.");
		return false;
	}

	const ibString text = given->Kind() == ibDataKind::String ? given->AsString() : ibString();

	// ⭐ A REFERENCE IS WRITTEN THE WAY A SCRIPT WRITES ONE - "Catalog.Warehouses.MainWarehouse",
	// kind, object, then the predefined item's name (or `EmptyRef` for the empty reference). Three
	// parts, because a name alone cannot say which catalogue it belongs to, and the caller already
	// knows all three from the tree.
	if (text.Find(ibT('.')) != wxNOT_FOUND) {

		const ibString kind = text.BeforeFirst(ibT('.'));
		const ibString rest = text.AfterFirst(ibT('.'));
		const ibString objectName = rest.BeforeLast(ibT('.'));
		const ibString member = rest.AfterLast(ibT('.'));

		if (!objectName.IsEmpty() && !member.IsEmpty()) {

			ibValueMetaObject* target = ibFindMetaObject(activeMetaData, kind, objectName);
			ibValueMetaObjectGenericData* owner =
				dynamic_cast<ibValueMetaObjectGenericData*>(target);

			if (owner == nullptr) {
				refusal = ibString::Format(
					ibMcpText("There is no %s called '%s' - metadata_tree shows what there is."),
					kind, objectName);
				return false;
			}

			// ASKED OF THE OBJECT ITSELF, which is the same door a query uses for a named constant:
			// it knows its predefined items and it knows what its empty reference is.
			if (!owner->ResolveQueryConstant(member, value)) {
				refusal = ibString::Format(
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
		case ibDataKind::Date:   value = ibValue(given->AsDate()); break;   // the wall-clock reading; the empty date is "no date"
		default: value = ibValue(text); break;
	}

	return true;
}

// WHOSE SETTINGS — a node of an output, or the report's when no output is named (SettingsOf).
const ibArg& ArgNodeOutput()
{
	static const ibArg s_a(ibT("output"), ibArg::Kind::Text,
		ibMcpText("Set it on a NODE of this output - the level `groupBy` names, or the output's DETAIL level "
			"without it. Omit to set the REPORT's own, which every node inherits."));
	return s_a;
}

// ⭐ WHOSE SETTINGS A CALL SPEAKS OF — the settings window's own "Settings of:": the report's, or a NODE's (the level
// `groupBy` names in the output `output` names, else that output's DETAIL level). One finder for every verb said per
// node (report_other_settings, report_conditional_appearance), so "which node" is answered once. Null, with the
// refusal, where there is none.
ibSettingsDescription* SettingsOf(ibVariantDescription& variant, const ibDataNode& params, ibString& refusal)
{
	if (!ArgNodeOutput().Given(params))
		return &variant.m_settings;

	const ibString outputName = ArgNodeOutput().Text(params);
	ibOutputDescription* output = nullptr;
	ibString available;
	for (ibOutputDescription& candidate : variant.m_settings.m_structure) {
		available << (available.IsEmpty() ? ibT("") : ibT(", ")) << candidate.m_name;
		if (candidate.m_name.IsSameAs(outputName, false))
			output = &candidate;
	}
	if (output == nullptr) {
		refusal = available.IsEmpty()
			? ibMcpText("This variant has no output yet - add one with report_output.")
			: ibString::Format(ibMcpText("There is no output called '%s'. It has: %s."), outputName, available);
		return nullptr;
	}
	const ibString groupBy = ArgGroupBy().Text(params);
	ibLevelDescription* node = nullptr;
	ibString has;
	for (std::vector<ibLevelDescription>* axis : { &output->m_rowGroups, &output->m_columnGroups })
		for (ibLevelDescription& level : *axis) {
			for (const ibGroupLineDescription& field : level.m_settings.m_group.m_lines)
				has << (has.IsEmpty() ? ibT("") : ibT(", ")) << field.m_path;
			if (node != nullptr)
				continue;
			if (groupBy.IsEmpty() ? level.m_kind == ibCompositionLevelKind::Details
			                      : std::any_of(level.m_settings.m_group.m_lines.begin(),
			                                    level.m_settings.m_group.m_lines.end(),
			                                    [&groupBy](const ibGroupLineDescription& field) {
			                                        return field.m_path.IsSameAs(groupBy, false); }))
				node = &level;
		}
	if (node == nullptr) {
		refusal = groupBy.IsEmpty()
			? ibMcpText("This output has no detail level - name the grouping with `groupBy`.")
			: has.IsEmpty()
				? ibMcpText("This output has no level grouped by that - it has no groupings yet.")
				: ibString::Format(ibMcpText("This output has no level grouped by that. It groups by: %s."), has);
		return nullptr;
	}
	return &node->m_settings;
}

// ⭐⭐ A CONDITION AS A TREE — the filter's own shape, said and read back the same way by report_filter and
// report_conditional_appearance (Max, 2026-09-30: "≠ X OR not filled" could not be said at all, and a rule built in
// the window with a group read back as an EMPTY condition — its groups were skipped). An array of entries joined by
// AND; an entry is a LINE or a GROUP. What is read back is what can be sent.
const ibArg& ArgCondition()
{
	static const ibArg s_a(ibT("condition"), ibArg::Kind::Any,
		ibString::Format(ibMcpText("The whole condition as a tree - an array of entries, joined by AND. A LINE is "
			"{\"path\": a field, \"comparison\": %s, \"value\": what to compare with - or \"valuePath\": another field of "
			"the same row - and \"use\": false to keep it switched off}; filled / notFilled take neither. A GROUP is "
			"{\"group\": \"and\" | \"or\" | \"not\", \"lines\": [entries]}. `Supplier` is not X or has none: "
			"[{\"group\": \"or\", \"lines\": [{\"path\": \"Supplier\", \"comparison\": \"notEqual\", \"value\": \"...\"}, "
			"{\"path\": \"Supplier\", \"comparison\": \"notFilled\"}]}]. An empty array holds on every row. It replaces the "
			"whole condition; the answer reads it back in this same shape."), ComparisonWordList()));
	return s_a;
}

// …AND A CONDITION OF ONE LINE, the everyday case — its field; the rest of the line is `comparison` and `value`.
const ibArg& ArgLinePath()
{
	static const ibArg s_a(ibT("path"), ibArg::Kind::Text,
		ibMcpText("The field of a ONE-LINE condition, with `comparison` and `value`. For several lines, a group or a "
			"field compared with a field, give `condition` instead."));
	return s_a;
}

// THE GROUP'S WORD — and, with none, not a group.
const std::vector<std::pair<const ibChar*, ibFilterGroupKind>>& GroupWords()
{
	static const std::vector<std::pair<const ibChar*, ibFilterGroupKind>> s_words = {
		{ ibT("and"), ibFilterGroupKind_And }, { ibT("or"), ibFilterGroupKind_Or }, { ibT("not"), ibFilterGroupKind_Not } };
	return s_words;
}

// THE TREE AS IT WAS SAID — every entry checked before anything is kept: a field the query does not offer, a word
// that is no comparison, a value that is no value of the field refuse the whole call.
bool ConditionFromTree(const ibCompositionDescription& composition, const ibDataValue& said,
	std::vector<ibFilterNodeDescription>& into, ibString& refusal)
{
	if (said.Kind() != ibDataKind::Array) {
		refusal = ibMcpText("`condition` is an ARRAY of entries - [] holds on every row.");
		return false;
	}
	for (const ibDataValue& entry : said.AsArray()) {
		if (entry.Kind() != ibDataKind::Child || !entry.AsChild()) {
			refusal = ibMcpText("Each entry of `condition` is an object - a line {path, comparison, value} or a "
				"group {group, lines}.");
			return false;
		}
		const ibDataNode line = *entry.AsChild();
		auto use = line.FindField(ibT("use"));
		const bool on = !use || use->Kind() != ibDataKind::Bool || use->AsBool();

		if (auto group = line.FindField(ibT("group"))) {
			const ibString word = group->Kind() == ibDataKind::String ? group->AsString() : ibString();
			const auto kind = std::find_if(GroupWords().begin(), GroupWords().end(),
				[&word](const std::pair<const ibChar*, ibFilterGroupKind>& known) { return word.IsSameAs(known.first, false); });
			if (kind == GroupWords().end()) {
				refusal = ibString::Format(ibMcpText("'%s' is not a group - and, or or not."), word);
				return false;
			}
			std::vector<ibFilterNodeDescription> children;   // read apart — `into` does not move under `added`
			auto lines = line.FindField(ibT("lines"));
			if (lines && !ConditionFromTree(composition, *lines, children, refusal))
				return false;
			ibFilterNodeDescription& added = ibFilterDescription::AppendGroup(into, kind->second);
			added.m_use = on;
			added.m_children = std::move(children);
			continue;
		}

		auto pathSaid = line.FindField(ibT("path"));
		const ibString path = pathSaid && pathSaid->Kind() == ibDataKind::String ? pathSaid->AsString() : ibString();
		if (path.IsEmpty()) {
			refusal = ibMcpText("A line of `condition` names its field - \"path\".");
			return false;
		}
		if (!PathIsOffered(composition, path, refusal))
			return false;
		auto comparisonSaid = line.FindField(ibT("comparison"));
		const ibString word = comparisonSaid && comparisonSaid->Kind() == ibDataKind::String
			? comparisonSaid->AsString() : ibString();
		ibComparisonKind comparison = ibComparisonKind_Equal;
		if (!ComparisonFromWord(word, comparison, refusal))
			return false;
		ibFilterNodeDescription& added = ibFilterDescription::Append(into, path, comparison, ibDataNode(), on);
		if (!ibComparisonTakesValue(comparison))
			continue;   // «filled» asks the field alone
		auto valuePath = line.FindField(ibT("valuePath"));
		if (valuePath && valuePath->Kind() == ibDataKind::String && !valuePath->AsString().IsEmpty()) {
			if (!PathIsOffered(composition, valuePath->AsString(), refusal))
				return false;
			added.m_right.m_path = valuePath->AsString();   // a field of the same row
		}
		else {
			ibValue value;
			if (!ValueForPath(composition, path, line, value, refusal))
				return false;
			ibStoreValue(added.m_right.m_value, value);
		}
	}
	return true;
}

// …AND READ BACK IN THE SAME SHAPE — a group as a group, a switched-off entry as one, a root that is not an AND as
// the one group it stands for. A value is read through the configuration the composition belongs to.
std::vector<ibDataValue> TreeOfCondition(const std::vector<ibFilterNodeDescription>& nodes, const ibMetaData* metaData)
{
	std::vector<ibDataValue> entries;
	for (const ibFilterNodeDescription& node : nodes) {
		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		if (node.m_kind == ibFilterNodeKind_Group) {
			for (const auto& known : GroupWords())
				if (known.second == node.m_groupKind)
					entry->SetValue(ibT("group"), ibString(known.first));
			entry->AddField(ibT("lines"), ibDataValue::Array(TreeOfCondition(node.m_children, metaData)));
		}
		else {
			entry->SetValue(ibT("path"), node.m_left.m_path);
			entry->SetValue(ibT("comparison"), WordOfComparison(node.m_comparison));
			if (!ibComparisonTakesValue(node.m_comparison))
				;   // «filled» has none to read out
			else if (node.m_right.IsField())
				entry->SetValue(ibT("valuePath"), node.m_right.m_path);
			else
				entry->SetValue(ibT("value"), ibString(ibStoredValue(node.m_right.m_value, metaData).GetString()));
		}
		if (!node.m_use)
			entry->AddField(ibT("use"), ibDataValue::Bool(false));
		entries.push_back(ibDataValue::Child(entry));
	}
	return entries;
}

ibDataValue TreeOfCondition(const ibFilterDescription& filter, const ibMetaData* metaData)
{
	if (filter.m_rootKind == ibFilterGroupKind_And)
		return ibDataValue::Array(TreeOfCondition(filter.m_nodes, metaData));
	ibFilterNodeDescription root;
	root.m_kind = ibFilterNodeKind_Group;
	root.m_groupKind = filter.m_rootKind;
	root.m_children = filter.m_nodes;
	return ibDataValue::Array(TreeOfCondition(std::vector<ibFilterNodeDescription>{ root }, metaData));
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
	std::vector<ibString>& missing)
{
	ComposerComplaints(composition, missing);
}

//---------------------------------------------------------------------------
// report_fields
//---------------------------------------------------------------------------
class ibMcpToolReportFields : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("report_fields"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("looking at what the composer '%s' can group by"),
			ibMcpNameOf(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibString fault;
		const std::vector<ibQueryConstructorField> fields =
			FieldsOf(composer->GetCompositionDesc(), fault);

		std::vector<ibDataValue> out;
		for (const ibQueryConstructorField& field : fields) {

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(ibT("name"), field.m_name);
			if (!field.m_presentation.IsEmpty() && field.m_presentation != field.m_name)
				node->SetValue(ibT("title"), field.m_presentation);
			if (field.m_reference)
				node->AddField(ibT("reference"), ibDataValue::Bool(true));

			out.push_back(ibDataValue::Child(node));
		}

		result.AddField(ibT("fields"), ibDataValue::Array(out));

		// A COMPLAINT AND AN EMPTY LIST ARE DIFFERENT ANSWERS. Half-typed text
		// offers nothing YET; unreadable text offers nothing AT ALL, and only the
		// second is something to fix.
		if (!fault.IsEmpty())
			result.SetValue(ibT("problem"), fault);
		else if (out.empty())
			result.SetValue(ibT("note"),
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

	ibString GetName() const override { return ibT("report_filter"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("filtering the composer '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibString::Format(ibMcpText("Narrow what a report shows - a selection kept in the SETTING rather than written "
			"into the query, so it belongs to the variant and a reader can change it. On the report by default; name an "
			"`output` (and a `groupBy`) to narrow that node's rows only. One line: the field, how to compare (%s) and the "
			"value - none for filled / notFilled; the same path again changes that line, remove:true takes it out. "
			"Several lines, a group (or / not) or a field compared with a field: give the whole `condition` as a tree, "
			"which replaces what was there. A REFERENCE is written as a script writes one - "
			"'Catalog.Warehouses.MainWarehouse', with 'EmptyRef' for the empty one - and anything "
			"else is taken as it arrives: a number, a date, a word. With nothing to say, the condition is read back, "
			"as the tree `condition` takes."), ComparisonWordList());
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgNodeOutput(), ArgGroupBy(),
			ArgLinePath(), ArgComparison(), ibMcpValueArgument(), ArgCondition(), ArgVariant(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		// WHOSE — the report's own settings, or a node's (SettingsOf), as every verb said per node finds them.
		ibSettingsDescription* const settings = SettingsOf(*variant, params, refusal);
		if (settings == nullptr)
			return false;
		ibFilterDescription& filter = settings->m_filter;

		const ibString path = ArgLinePath().Text(params);
		const auto tree = params.FindField(ArgCondition().Name());
		if (tree && !path.IsEmpty()) {
			refusal = ibMcpText("Say the condition one way - the whole `condition` as a tree, or one line by `path`.");
			return false;
		}

		std::vector<ibFilterNodeDescription>& nodes = filter.m_nodes;

		// ⚠ THE FIELD IS THE LEFT OPERAND, not a member of the node: a condition is left, comparison,
		// right, and either side may be a field or a literal. The one being matched here is always
		// the left, because that is the side a caller names.
		const auto found = std::find_if(nodes.begin(), nodes.end(),
			[&path](const ibFilterNodeDescription& node) {
				return node.m_left.m_path.IsSameAs(path, false); });

		if (tree) {
			// THE WHOLE TREE, checked entry by entry before it replaces anything.
			std::vector<ibFilterNodeDescription> said;
			if (!ConditionFromTree(composition, *tree, said, refusal))
				return false;
			filter.m_rootKind = ibFilterGroupKind_And;
			nodes = std::move(said);
		}
		else if (path.IsEmpty()) {
			if (ArgRemove().Flag(params)) {
				refusal = ibMcpText("Say which line to take out - its `path`.");
				return false;
			}
		}
		else if (ArgRemove().Flag(params)) {

			if (found == nodes.end()) {
				refusal = ibString::Format(ibMcpText("There is no filter on '%s'."), path);
				return false;
			}

			nodes.erase(found);
		}
		else {
			if (!PathIsOffered(composition, path, refusal))
				return false;

			// THE WORD, NOT A NUMBER. A comparison spelled out is one a caller can get right from
			// the description; an integer is one they get right by luck.
			ibComparisonKind comparison = ibComparisonKind_Equal;
			if (!ComparisonFromWord(ArgComparison().Text(params), comparison, refusal))
				return false;

			ibValue value;   // …none for «filled»: it asks the field alone
			if (ibComparisonTakesValue(comparison) && !ValueForPath(composition, path, params, value, refusal))
				return false;

			if (found != nodes.end()) {
				found->m_comparison = comparison;
				found->m_right = ibFilterOperandDescription();   // a value said now, not the field it may have been
				ibStoreValue(found->m_right.m_value, value);
			}
			else {
				ibFilterDescription::Append(nodes, path, comparison, ibStoreValue(value));
			}
		}

		if (tree || !path.IsEmpty()) {
			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);
		}

		// THE CONDITION AS IT STANDS NOW — the whole tree, in the shape `condition` takes (groups and switched-off
		// lines included: a list of paths hid both).
		result.SetValue(ibT("settingsOf"), ibString(ArgNodeOutput().Given(params) ? ibT("node") : ibT("report")));
		result.AddField(ibT("condition"), TreeOfCondition(filter, composer->GetMetaData()));
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

	ibString GetName() const override { return ibT("report_order"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("ordering the composer '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		const ibString path = ArgPath().Text(params);
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
				refusal = ibString::Format(ibMcpText("The rows are not sorted by '%s'."), path);
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
			entry->SetValue(ibT("path"), line.m_path);
			entry->SetValue(ibT("direction"),
				ibString(line.m_ascending ? ibT("ascending") : ibT("descending")));
			order.push_back(ibDataValue::Child(entry));
		}

		result.AddField(ibT("order"), ibDataValue::Array(order));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportOrder);

//---------------------------------------------------------------------------
// report_other_settings
//---------------------------------------------------------------------------
//
// ⭐⭐ HOW THE REPORT BEHAVES AS A WHOLE — the settings window's Other settings page (ibOutputParameter): its
// theme, its heading. Part of a VARIANT like the filter, and set on the report or a node the way the filter is (Max,
// 2026-09-29: "the other settings are part of the variant, exactly as the filters, only set within a node"):
// on the report, or on a node, where a grouping paints its own rows. Said through the page's own door
// (ibParameterValuesDescription::Say), one argument per parameter of the platform's list, the way report_field
// says a field's appearance — so a heading set here and one set on the page are set the same way.
class ibMcpToolReportOtherSettings : public ibMcpTool {

	static const ibArg& ArgTheme() {
		static const std::vector<ibString> s_ids = [] {
			std::vector<ibString> ids;
			for (const ibCompositionTheme* theme : ibCompositionThemes())
				ids.push_back(theme->m_id);
			ids.push_back(ibEmptyString);   // …and empty, which gives it back — see ShowWordsOrEmpty
			return ids;
		}();
		static const ibArg a(ibT("theme"), ibArg::Kind::Text,
			ibMcpText("The palette the report is painted in - the header, the headings by their depth, the records, "
				"the grid, the title. On a node it tints that node's rows only. Empty gives it back to the report's. "
				"Omit to leave it."),
			false, s_ids);
		return a;
	}
	static const ibArg& ArgTitle() {
		static const ibArg a(ibT("title"), ibArg::Kind::Text,
			ibMcpText("The report's heading, printed large over the table. Several languages in the synonym form - "
				"`en = 'Stock by warehouse'; ru = '...'` - are read in the reader's. Empty gives back the report's "
				"own name. Omit to leave it. The report's only."));
		return a;
	}
	// THE THREE WORDS, in ibShowMode's order — a word's place IS the mode it says.
	static const std::vector<ibString>& ShowWords() {
		static const std::vector<ibString> s_words = { ibT("auto"), ibT("show"), ibT("hide") };
		return s_words;
	}
	// ⚠ …AND EMPTY BESIDE THEM, as a word the gate lets through: a closed set is held to its words
	// (ibMcpArgumentFault), and "empty gives it back" has to be one of them to be said at all.
	static const std::vector<ibString>& ShowWordsOrEmpty() {
		static const std::vector<ibString> s_words = { ibT("auto"), ibT("show"), ibT("hide"), ibEmptyString };
		return s_words;
	}
	static const ibArg& ArgShowTitle() {
		static const ibArg a(ibT("showTitle"), ibArg::Kind::Text,
			ibMcpText("Print the heading: `show`, `hide`, or `auto` (it prints). The report's only. Omit to leave it."),
			false, ShowWordsOrEmpty());
		return a;
	}
	static const ibArg& ArgShowFilter() {
		static const ibArg a(ibT("showFilter"), ibArg::Kind::Text,
			ibMcpText("Print the filter's conditions under the heading: `show`, `hide`, or `auto` (they print). The "
				"report's only. Omit to leave it."),
			false, ShowWordsOrEmpty());
		return a;
	}
	static const ibArg& ArgShowParameters() {
		static const ibArg a(ibT("showParameters"), ibArg::Kind::Text,
			ibMcpText("Print the values the reader filled in - `Period: ...` - under the heading: `show`, `hide`, or "
				"`auto` (they do not print). The report's only. Omit to leave it."),
			false, ShowWordsOrEmpty());
		return a;
	}

	// WHICH ARGUMENT SAYS WHICH PARAMETER — the platform's list, in its order.
	static const std::vector<std::pair<ibOutputParameter, const ibArg*>>& Words() {
		static const std::vector<std::pair<ibOutputParameter, const ibArg*>> s_words = {
			{ ibOutputParameter::Theme,          &ArgTheme() },
			{ ibOutputParameter::Title,          &ArgTitle() },
			{ ibOutputParameter::ShowTitle,      &ArgShowTitle() },
			{ ibOutputParameter::ShowFilter,     &ArgShowFilter() },
			{ ibOutputParameter::ShowParameters, &ArgShowParameters() },
		};
		return s_words;
	}

public:

	ibString GetName() const override { return ibT("report_other_settings"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("setting how '%s' is printed"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("The report's OTHER SETTINGS - the settings window's page after Sort: its THEME (the colour "
			"palette), its TITLE and whether the title, the filter and the reader's parameter values are printed "
			"over the table. Part of a variant, like the filter. On the report by default; name an `output` (and a "
			"`groupBy`) to set a node, which holds only a theme - a grouping tinted apart from the rest. A node "
			"that says nothing takes the report's. Each argument omitted leaves its parameter as it is; an empty "
			"one gives it back to the report's. With none, the settings named are read back.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgNodeOutput(), ArgGroupBy(), ArgTheme(),
			ArgTitle(), ArgShowTitle(), ArgShowFilter(), ArgShowParameters(), ArgVariant() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		// WHOSE — the report's own settings, or a node's (SettingsOf).
		ibSettingsDescription* const settings = SettingsOf(*variant, params, refusal);
		if (settings == nullptr)
			return false;
		ibOutputParametersDescription* said = &settings->m_outputParameters;
		const ibOutputParameterScope scope = ArgNodeOutput().Given(params)
			? ibOutputParameterScope::Node : ibOutputParameterScope::Report;

		// EACH PARAMETER ITS OWN ARGUMENT — omitted leaves it, empty gives it back to the report's. Everything
		// is checked before anything is said, so a refused call changes nothing.
		const std::vector<ibOutputParameter>& offered = ibOutputParameters(scope);
		std::vector<std::pair<ibOutputParameter, ibValue>> words;
		for (const auto& word : Words()) {
			if (!word.second->Given(params))
				continue;
			if (std::find(offered.begin(), offered.end(), word.first) == offered.end()) {
				refusal = ibString::Format(ibMcpText("A node holds only a theme - `%s` is the report's: say it "
					"without `output`."), word.second->Name());
				return false;
			}
			// (A word outside a closed set never gets here — the gate refuses it by name, ibMcpArgumentFault.)
			const ibString text = word.second->Text(params);
			ibValue value;   // empty — given back to the report's
			if (text.IsEmpty())
				;
			else if (word.first == ibOutputParameter::Theme)
				value = ibValue(ibString(ibCompositionThemeById(text).m_id));   // its id as the platform spells it
			else if (ibOutputParameterShows(word.first)) {
				const std::vector<ibString>& modes = ShowWords();
				const auto found = std::find_if(modes.begin(), modes.end(),
					[&text](const ibString& mode) { return mode.IsSameAs(text, false); });
				value = ibValue(static_cast<int>(found - modes.begin()));   // the words stand in ibShowMode's order
			}
			else
				value = ibValue(text);
			words.emplace_back(word.first, value);
		}
		for (const auto& word : words)
			said->Say(word.first, !word.second.IsEmpty(), ibStoreValue(word.second));

		if (!words.empty()) {
			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);
		}

		// WHAT THEY SAY NOW — only what they say; the rest is the report's above them.
		result.SetValue(ibT("settingsOf"), ibString(scope == ibOutputParameterScope::Node ? ibT("node") : ibT("report")));
		for (const auto& word : Words()) {
			if (!said->Says(word.first))
				continue;
			const ibValue value = ibStoredValue(said->ValueInForce(word.first), composer->GetMetaData());
			result.SetValue(word.second->Name(), ibOutputParameterShows(word.first)
				? ShowWords()[std::min<size_t>(static_cast<size_t>(std::max(0, value.GetInteger())), 2)]
				: ibString(value.GetString()));
		}
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportOtherSettings);

//---------------------------------------------------------------------------
// report_conditional_appearance
//---------------------------------------------------------------------------
//
// ⭐⭐ WHAT STANDS OUT, AND WHEN — the settings window's Conditional appearance page. A rule is a CONDITION (a filter,
// the very one report_filter says), the FIELDS it paints (none = the whole line) and the APPEARANCE it paints them
// with, said through the appearance's own door (ibParameterValuesDescription::Say) with the values the page's
// choosers keep — so a rule said here and one set there are the same rule. Part of a variant like the filter, said
// on the report's settings or on a node's, like the other settings (SettingsOf).
class ibMcpToolReportConditionalAppearance : public ibMcpTool {

	static const ibArg& ArgRuleAt() {
		static const ibArg a(ibT("at"), ibArg::Kind::Whole,
			ibMcpText("Which rule, 1 for the first - to change it, or with `remove` to take it out. Omit to add a new one "
				"at the end. The rules apply in their order, a later one over an earlier one."));
		return a;
	}
	static const ibArg& ArgConditionPath() {
		static const ibArg a(ibT("path"), ibArg::Kind::Text,
			ibMcpText("The field of a ONE-LINE condition - with `comparison` and `value`, as report_filter takes them. "
				"Empty makes the rule hold on every row. Several lines or a group: give `condition` instead. Omit both "
				"to leave the condition as it is."));
		return a;
	}
	static const ibArg& ArgUse() {
		static const ibArg a(ibT("use"), ibArg::Kind::Flag,
			ibMcpText("Switch the rule on (true) or off (false) - it stays written either way. Omit to leave it."));
		return a;
	}
	static const ibArg& ArgFields() {
		static const ibArg a(ibT("fields"), ibArg::Kind::Many,
			ibMcpText("The columns it paints, by field as report_fields lists them - [\"Amount\"]. An empty list paints "
				"the whole line. Omit to leave them as they are."));
		return a;
	}
	static const ibArg& ArgBackgroundColour() {
		static const ibArg a(ibT("backgroundColour"), ibArg::Kind::Text,
			ibMcpText("The cells' fill: `R,G,B` - `255,235,156` - or `#FFEB9C`. Empty takes it away. Omit to leave it."));
		return a;
	}
	static const ibArg& ArgTextColour() {
		static const ibArg a(ibT("textColour"), ibArg::Kind::Text,
			ibMcpText("The text's colour: `R,G,B` - `192,0,0` - or `#C00000`. Empty takes it away. Omit to leave it."));
		return a;
	}
	static const ibArg& ArgFont() {
		static const ibArg a(ibT("font"), ibArg::Kind::Text,
			ibMcpText("The text's font, in words: `bold`, `italic`, `underlined`, `strikethrough`, a size, a face - "
				"`bold 12`, `italic Arial`. Only what is said is laid on the cell's own font: `italic` on a bold heading "
				"keeps it bold, and the report's size stays unless a size is said. Empty takes it away. Omit to leave it."));
		return a;
	}
	// THE THREE WORDS, and empty beside them — see ShowWordsOrEmpty in report_other_settings.
	static const ibArg& ArgHorizontalAlignment() {
		static const ibArg a(ibT("horizontalAlignment"), ibArg::Kind::Text,
			ibMcpText("Where the text stands in the cell: `left`, `center` or `right`. Empty takes it away. Omit to leave it."),
			false, { ibT("left"), ibT("center"), ibT("right"), ibEmptyString });
		return a;
	}
	static const ibArg& ArgText() {
		static const ibArg a(ibT("text"), ibArg::Kind::Text,
			ibMcpText("What the cells say INSTEAD of their value. Several languages in the synonym form - "
				"`en = 'Over the limit'; ru = '...'` - are read in the reader's. Empty takes it away. Omit to leave it."));
		return a;
	}
	static const ibArg& ArgFormat() {
		static const ibArg a(ibT("format"), ibArg::Kind::Text,
			ibMcpText("How the cells write their value - a format string (`NFD=2`, `DF=dd.MM.yyyy`; format_string reads "
				"one out and builds one). Empty takes it away. Omit to leave it."));
		return a;
	}

	// WHICH ARGUMENT SAYS WHICH PARAMETER — the platform's list, in its order.
	static const std::vector<std::pair<ibAppearanceParameter, const ibArg*>>& Words() {
		static const std::vector<std::pair<ibAppearanceParameter, const ibArg*>> s_words = {
			{ ibAppearanceParameter::Format,              &ArgFormat() },
			{ ibAppearanceParameter::BackgroundColour,    &ArgBackgroundColour() },
			{ ibAppearanceParameter::TextColour,          &ArgTextColour() },
			{ ibAppearanceParameter::Font,                &ArgFont() },
			{ ibAppearanceParameter::HorizontalAlignment, &ArgHorizontalAlignment() },
			{ ibAppearanceParameter::Text,                &ArgText() },
		};
		return s_words;
	}

	// A WORD AS THE VALUE ITS PARAMETER KEEPS — a colour, a font, a member of the sheet's alignment, else the text as
	// said. False, with the refusal, for a word that names no such value.
	static bool ValueOfWord(ibAppearanceParameter parameter, const ibString& word, ibValue& value, ibString& refusal)
	{
		switch (parameter) {
		case ibAppearanceParameter::BackgroundColour:
		case ibAppearanceParameter::TextColour: {
			ibColour colour;
			if (!colour.FromString(word)) {
				refusal = ibString::Format(ibMcpText("'%s' is not a colour - write it `R,G,B` (`255,235,156`) or `#FFEB9C`."), word);
				return false;
			}
			value = ibValue(new ibValueColour(colour));
			return true;
		}
		case ibAppearanceParameter::Font: {
			// OVER THE REPORT'S OWN FONT, word by word — what the words do not say stays that font's, so the rule
			// says exactly the words (ibCompositionFont::Of reads them back out of it).
			ibFont font = ibDefaultSpreadsheetFont();
			ibString face;
			for (const ibString& one : ibSplit(word, ibT(' '), ibT('\0'))) {
				long size = 0;
				if (one.IsEmpty())
					continue;
				if (one.IsSameAs(ibT("bold"), false))
					font.m_weight = ibFONTWEIGHT_BOLD;
				else if (one.IsSameAs(ibT("italic"), false))
					font.m_style = ibFONTSTYLE_ITALIC;
				else if (one.IsSameAs(ibT("underlined"), false))
					font.m_underlined = true;
				else if (one.IsSameAs(ibT("strikethrough"), false))
					font.m_strikethrough = true;
				else if (one.ToLong(&size) && size > 0)
					font.m_pointSize = size;
				else
					face << (face.IsEmpty() ? ibT("") : ibT(" ")) << one;
			}
			// The face as said: the fonts are the screen's that draws the report, and the server has none.
			if (!face.IsEmpty())
				font.m_faceName = face;
			value = ibValue(new ibValueFont(font));
			return true;
		}
		case ibAppearanceParameter::HorizontalAlignment:
			// (A word outside the three never gets here — the gate refuses it by name, ibMcpArgumentFault.)
			value = ibValue::CreateEnumObject<ibValueEnumSpreadsheetHorizontalAlignment>(
				word.IsSameAs(ibT("right"), false) ? ibAlignmentHorz_Right
				: word.IsSameAs(ibT("center"), false) ? ibAlignmentHorz_Center : ibAlignmentHorz_Left);
			return true;
		default:
			value = ibValue(word);
			return true;
		}
	}

	// …and back — what the answer reads out, in the words a call says it in.
	static ibString WordOfValue(ibAppearanceParameter parameter, ibValue value)
	{
		switch (parameter) {
		case ibAppearanceParameter::Font: {
			// …WHAT IT SAYS, and only that: the parts it changes of the report's own font, in the words it takes.
			ibValueFont* font = nullptr;
			if (!value.ConvertToValue(font) || font == nullptr || !font->m_font.IsOk())
				return ibString();
			const ibCompositionFont said = ibCompositionFont::Of(font->m_font, ibDefaultSpreadsheetFont());
			ibString words;
			const auto add = [&words](const ibString& one) { words << (words.IsEmpty() ? ibT("") : ibT(" ")) << one; };
			if (said.IsBold())
				add(ibT("bold"));
			if (said.IsItalic())
				add(ibT("italic"));
			if (said.m_underlined)
				add(ibT("underlined"));
			if (said.m_strikethrough)
				add(ibT("strikethrough"));
			if (said.m_pointSize > 0)
				add(ibString::Format(ibT("%d"), said.m_pointSize));
			if (!said.m_face.IsEmpty())
				add(said.m_face);
			return words;
		}
		case ibAppearanceParameter::HorizontalAlignment: {
			const ibSpreadsheetAlignmentHorz horizontal = value.ConvertToEnumValue<ibSpreadsheetAlignmentHorz>();
			return horizontal == ibAlignmentHorz_Right ? ibT("right")
				: horizontal == ibAlignmentHorz_Center ? ibT("center") : ibT("left");
		}
		default:
			return ibString(value.GetString());   // a colour reads `R,G,B`, a text and a format as they were said
		}
	}

public:

	ibString GetName() const override { return ibT("report_conditional_appearance"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("saying what stands out in '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("The report's CONDITIONAL APPEARANCE - the settings window's page after Sort: rules that paint "
			"the rows a condition holds on. A rule is a CONDITION (one line - a field, a comparison and a value - or the "
			"whole `condition` as a tree with groups, as report_filter takes them; none = every row), the FIELDS it "
			"paints (none = the whole line) and the APPEARANCE - a fill, a text colour, a font, an alignment, a text said "
			"instead of the value, a format; `use` switches it off and on. Part of a variant, like the "
			"filter. On the report by default; name an `output` (and a `groupBy`) to set a node's, which paints that "
			"node's rows only, over the report's. One rule per call: omit `at` to add one, give it to change that rule "
			"(each argument omitted leaves its part as it is, an empty one takes it away), with remove:true to take it "
			"out. With nothing to say, the rules of the settings named are read back. A list shown by the same settings is painted "
			"the same way.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgNodeOutput(), ArgGroupBy(), ArgRuleAt(),
			ArgConditionPath(), ArgComparison(), ibMcpValueArgument(), ArgCondition(), ArgFields(), ArgBackgroundColour(),
			ArgTextColour(), ArgFont(), ArgHorizontalAlignment(), ArgText(), ArgFormat(), ArgUse(), ArgVariant(),
			ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		ibSettingsDescription* const settings = SettingsOf(*variant, params, refusal);
		if (settings == nullptr)
			return false;
		std::vector<ibConditionalAppearanceRuleDescription>& rules = settings->m_conditionalAppearance.m_rules;

		// THE RULE — the one `at` names, or a new one.
		const std::int64_t at = ArgRuleAt().Given(params) ? ArgRuleAt().Whole(params) : 0;
		if (ArgRuleAt().Given(params) && (at < 1 || static_cast<size_t>(at) > rules.size())) {
			refusal = rules.empty()
				? ibMcpText("These settings have no rules yet - omit `at` to add one.")
				: ibString::Format(ibMcpText("There is no rule %d - these settings have %d."),
					static_cast<int>(at), static_cast<int>(rules.size()));
			return false;
		}

		bool changed = false;
		if (ArgRemove().Flag(params)) {
			if (at == 0) {
				refusal = ibMcpText("Say which rule to take out - `at`, 1 for the first.");
				return false;
			}
			rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(at - 1));
			changed = true;
		}
		else {
			// EVERYTHING IS CHECKED BEFORE ANYTHING IS SAID, on a copy — a refused call changes nothing.
			ibConditionalAppearanceRuleDescription rule = at > 0 ? rules[static_cast<size_t>(at - 1)]
				: ibConditionalAppearanceRuleDescription();
			bool says = false;

			const auto tree = params.FindField(ArgCondition().Name());
			if (tree && ArgConditionPath().Given(params)) {
				refusal = ibMcpText("Say the condition one way - the whole `condition` as a tree, or one line by `path`.");
				return false;
			}
			if (tree) {
				says = true;
				std::vector<ibFilterNodeDescription> said;
				if (!ConditionFromTree(composition, *tree, said, refusal))
					return false;
				rule.m_condition = ibFilterDescription();
				rule.m_condition.m_nodes = std::move(said);
			}
			if (ArgUse().Given(params)) {
				says = true;
				rule.m_use = ArgUse().Flag(params);
			}

			if (ArgConditionPath().Given(params)) {
				says = true;
				rule.m_condition = ibFilterDescription();
				const ibString path = ArgConditionPath().Text(params);
				if (!path.IsEmpty()) {
					if (!PathIsOffered(composition, path, refusal))
						return false;
					ibComparisonKind comparison = ibComparisonKind_Equal;
					if (!ComparisonFromWord(ArgComparison().Text(params), comparison, refusal))
						return false;
					ibValue value;   // …none for «filled»: it asks the field alone
					if (ibComparisonTakesValue(comparison) && !ValueForPath(composition, path, params, value, refusal))
						return false;
					ibFilterDescription::Append(rule.m_condition.m_nodes, path, comparison, ibStoreValue(value));
				}
			}

			if (auto many = params.FindField(ArgFields().Name())) {
				says = true;
				rule.m_fields.clear();
				if (many->Kind() == ibDataKind::Array)
					for (const ibDataValue& one : many->AsArray()) {
						if (one.Kind() != ibDataKind::String || one.AsString().IsEmpty())
							continue;
						const ibString path = one.AsString();
						// A resource is painted by the name it answers to, as report_select shows one.
						if (!std::any_of(composition.m_resources.begin(), composition.m_resources.end(),
						                 [&path](const ibResourceDescription& r) { return r.AnswersTo().IsSameAs(path, false); })
						    && !PathIsOffered(composition, path, refusal))
							return false;
						rule.m_fields.push_back(path);
					}
			}

			for (const auto& word : Words()) {
				if (!word.second->Given(params))
					continue;
				says = true;
				const ibString text = word.second->Text(params);
				ibValue value;   // empty — taken away
				if (!text.IsEmpty() && !ValueOfWord(word.first, text, value, refusal))
					return false;
				rule.m_appearance.Say(word.first, !value.IsEmpty(), ibStoreValue(value));
			}

			if (says) {
				if (rule.m_appearance.IsEmpty()) {
					refusal = ibMcpText("A rule with no appearance paints nothing - give it a `backgroundColour`, a "
						"`textColour`, a `font`, a `horizontalAlignment`, a `text` or a `format`.");
					return false;
				}
				if (at > 0)
					rules[static_cast<size_t>(at - 1)] = std::move(rule);
				else
					rules.push_back(std::move(rule));
				changed = true;
			}
		}

		if (changed) {
			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);
		}

		// THE RULES AS THEY STAND NOW, in their order — each read out in the words a call says it in.
		result.SetValue(ibT("settingsOf"), ibString(ArgNodeOutput().Given(params) ? ibT("node") : ibT("report")));
		std::vector<ibDataValue> shown;
		for (size_t i = 0; i < rules.size(); ++i) {
			const ibConditionalAppearanceRuleDescription& rule = rules[i];
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(ibT("at"), static_cast<std::int32_t>(i + 1));
			if (!rule.m_use)
				entry->AddField(ibT("use"), ibDataValue::Bool(false));
			// THE WHOLE CONDITION, groups included, in the shape `condition` takes. (It read the lines of the top only,
			// and a rule built in the window with a group came back as an EMPTY condition — one that holds everywhere.)
			entry->AddField(ibT("condition"), TreeOfCondition(rule.m_condition, composer->GetMetaData()));
			std::vector<ibDataValue> fields;
			for (const ibString& field : rule.m_fields)
				fields.push_back(ibDataValue::String(field));
			entry->AddField(ibT("fields"), ibDataValue::Array(fields));
			for (const auto& word : Words())
				if (rule.m_appearance.Says(word.first))
					entry->SetValue(word.second->Name(),
						WordOfValue(word.first, ibStoredValue(rule.m_appearance.ValueInForce(word.first), composer->GetMetaData())));
			shown.push_back(ibDataValue::Child(entry));
		}
		result.AddField(ibT("rules"), ibDataValue::Array(shown));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportConditionalAppearance);

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

	ibString GetName() const override { return ibT("report_parameter"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("declaring the parameter '%s'"), ArgName().Text(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibMetaData* metaData = activeMetaData;
		ibCompositionDescription composition = composer->GetCompositionDesc();

		const ibString name = ArgName().Text(params);

		auto found = std::find_if(composition.m_parameters.begin(), composition.m_parameters.end(),
			[&name](const ibParameterDescription& parameter) {
				return parameter.m_name.IsSameAs(name, false); });

		if (ArgRemove().Flag(params)) {

			if (found == composition.m_parameters.end()) {
				refusal = ibString::Format(ibMcpText("This composer has no parameter called '%s'."), name);
				return false;
			}

			// ⚠ A PARAMETER THE QUERY MENTIONS CANNOT BE REMOVED - the text is one of its two
			// authors, and dropping it here only means the next re-parse puts it back.
			if (found->m_fromQuery) {
				refusal = ibString::Format(
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
			const ibString typeName = ArgType().Text(params);

			if (!typeName.IsEmpty()) {

				const ibCtorAbstractType* const named = metaData->GetAvailableCtor(typeName);
				const ibClassID clsid = named != nullptr ? named->GetClassType() : 0;

				if (clsid == 0) {
					refusal = ibString::Format(
						ibMcpText("'%s' is not a type this configuration knows. type_list shows the "
						  "names."), typeName);
					return false;
				}

				found->m_type = ibTypeDescription();
				found->m_type.SetDefaultMetaType(clsid);
			}

			if (params.Has(ArgForUser().Name()))
				found->m_userSettable = ArgForUser().Flag(params);

			// ⭐ THE VALUE IS STORED AS THE PACKED NODE ITSELF - the shape ibStoredValue reads back when the
			// composition runs, and the shape compose_run takes. A packed value (value_pack's answer, or
			// what report_get shows) arrives as an OBJECT, so it is a CHILD of the arguments; a scalar is
			// a field, and packs itself. This used to look for a field only - so a packed date, the one
			// way to state a period, was never found - and to nest a scalar under "value", a shape
			// nothing reads: a default set here never applied (the payroll demo, 2026-09-10).
			//
			// 🛑 AND A VALUE OF A TYPE THE PARAMETER DOES NOT DECLARE IS REFUSED HERE, not stored. compose_run
			// refuses it (composeRunSchema.cpp, the same question), so storing it only moved the refusal to the
			// run: a date written as text went in as a String, every call said yes, and the report would not
			// compose (2026-09-29).
			ibValue given;
			auto packed = params.FindChild(ibMcpValueArgument().Name());
			auto scalar = !packed ? params.FindField(ibMcpValueArgument().Name()) : std::nullopt;
			if (packed)
				given = ibStoredValue(*packed, metaData);
			else if (scalar) {
				switch (scalar->Kind()) {
					case ibDataKind::String: given = ibValue(scalar->AsString()); break;
					case ibDataKind::Number: given = ibValue(scalar->AsNumber()); break;
					case ibDataKind::Bool:   given = ibValue(scalar->AsBool()); break;
					default: break;
				}
			}
			if ((packed || scalar) && found->m_type.GetClsidCount() > 0 && !given.IsEmpty()
			    && !found->m_type.ContainType(given.GetClassType())) {
				refusal = ibString::Format(
					ibMcpText("'%s' holds a value of the type it declares, and this is not one. A date is not "
						"written as text here: make it with value_pack {type, value} and send what it answers "
						"under `packed`."), name);
				return false;
			}
			if (packed)
				found->m_value = *packed;
			else if (scalar)
				ibStoreValue(found->m_value, given);
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		std::vector<ibDataValue> declared;

		for (const ibParameterDescription& parameter : composition.m_parameters) {
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(ibT("name"), parameter.m_name);
			entry->AddField(ibT("forUser"), ibDataValue::Bool(parameter.m_userSettable));
			entry->AddField(ibT("fromQuery"), ibDataValue::Bool(parameter.m_fromQuery));
			declared.push_back(ibDataValue::Child(entry));
		}

		result.AddField(ibT("parameters"), ibDataValue::Array(declared));
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

	ibString GetName() const override { return ibT("report_select"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("choosing what the report shows in '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
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
		std::vector<ibString> wanted;

		if (auto many = params.FindField(ArgPaths().Name())) {
			if (many->Kind() == ibDataKind::Array)
				for (const ibDataValue& one : many->AsArray())
					if (one.Kind() == ibDataKind::String && !one.AsString().IsEmpty())
						wanted.push_back(one.AsString());
		}

		if (const ibString path = ArgOnePath().Text(params); !path.IsEmpty())
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
		std::int32_t at = (std::int32_t)ArgAt().Whole(params);

		for (const ibString& path : wanted) {

			const auto found = std::find_if(selected.begin(), selected.end(),
				[&path](const ibSelectedFieldDescription& field) {
					return field.m_path.IsSameAs(path, false); });

			ibString fault;

			if (ArgRemove().Flag(params)) {

				if (found == selected.end())
					fault = ibString::Format(ibMcpText("'%s' is not shown here."), path);
				else
					selected.erase(found);
			}
			// ⚠ A FIELD THE QUERY DOES NOT PROJECT stores happily and shows nothing - the same
			// silent emptiness a level with a wrong path produces, which is why both are checked
			// against what the query actually offers rather than accepted on trust.
			//
			// ⭐ …UNLESS IT NAMES A RESOURCE. A figure the composition declares (`COUNT(DISTINCT
			// Employee) AS People`) is shown by the name it answers to - that is what the composer asks
			// the selection about - and it is no field of the query. Refused here, a resource written as
			// an expression could never be shown: the payroll demo's head count stayed off the page
			// (2026-09-10).
			else if (!std::any_of(composition.m_resources.begin(), composition.m_resources.end(),
			                      [&path](const ibResourceDescription& r) { return r.AnswersTo().IsSameAs(path, false); })
			         && !PathIsOffered(composition, path, fault)) {
				// fault already says what the query does offer
			}
			else if (found != selected.end()) {
				fault = ibString::Format(
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
			result.AddField(ibT("refused"), ibDataValue::Array(refused));

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		// ANSWERED WITH THE WHOLE ORDER, because that is what was changed - a caller adding three
		// columns sees the left-to-right result without asking again.
		std::vector<ibDataValue> shown;
		for (const ibSelectedFieldDescription& field : selected)
			shown.push_back(ibDataValue::String(field.IsAuto() ? ibString(ibT("(auto)")) : field.m_path));

		result.AddField(ibT("shows"), ibDataValue::Array(shown));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportSelect);

//---------------------------------------------------------------------------
// report_field
//---------------------------------------------------------------------------
//
// ⭐⭐ WHAT A FIELD OF THE REPORT IS — the title printed over its column, what it is in a balance and its
// appearance — said through the very doors the settings window's Fields page says them (ibFieldEntryForPath,
// SayTitle, SayRole, the appearance's Say), so a field described here and one described there are described
// the same way (Max, 2026-09-29: "you do in the settings what I do"). Its NAME is not said here: the fields are
// what the query's SELECT yields, and a new name is a new query (Max: "the query drives them"). It was
// report_title, which said one of them; the title lives on the FIELD (ibFieldDescription), and a payroll report
// built through these verbs printed "Month norm days" over a column meant as the month's norm in days until one
// could be set (2026-09-17). Written in several languages, it is read in the reader's, as a synonym is.
class ibMcpToolReportField : public ibMcpTool {

	static const ibArg& ArgTitle() {
		static const ibArg a(ibT("title"), ibArg::Kind::Text,
			ibMcpText("What stands over the column. Several languages in the synonym form - "
				"`en = 'Norm, days'; ru = '...'` - are read in the reader's. Empty gives back the generated title: "
				"what the source calls the field, else its name read out loud. Omit to leave it."));
		return a;
	}
	static const ibArg& ArgRole() {
		static const ibArg a(ibT("role"), ibArg::Kind::Text,
			ibMcpText("What the field is in a balance: `period` (the time a balance is taken at), `dimension` (what "
				"keeps one balance apart from another - an item, a warehouse, an account), `opening` / `closing` (the "
				"balance before / after a row: a SUM of it is taken at each key's first / last period instead of added "
				"up), `none` (an ordinary value), or `source` to give the field back to what its source says. A "
				"register's virtual table says it for its own fields already. Omit to leave it."),
			false, { ibT("period"), ibT("dimension"), ibT("opening"), ibT("closing"), ibT("none"), ibT("source") });
		return a;
	}
	static const ibArg& ArgPeriodNumber() {
		static const ibArg a(ibT("period_number"), ibArg::Kind::Whole,
			ibMcpText("For a field whose role is `period`: its seniority when several periods are read (a period, "
				"a recorder, a line number) - 1 is compared first; numbered periods come before the ones nobody "
				"numbered, which keep their source's order. 0 = in the order it stands. Omit to leave it."));
		return a;
	}
	// ⭐ THE APPEARANCE'S PARAMETERS — the platform's list (ibAppearanceParameters), one argument each; Format is
	// the whole list today.
	static const ibArg& ArgFormat() {
		static const ibArg a(ibT("format"), ibArg::Kind::Text,
			ibMcpText("How the field's values are written - a format string (`ND=15; NFD=2`, `DF=dd.MM.yyyy`; "
				"format_string reads one out and builds one), several languages in the synonym form - `en = 'NFD=2'; ru = '...'`. "
				"Applies to the field's column and to every figure summed over it. Empty takes it away. Omit to leave it."));
		return a;
	}

public:

	ibString GetName() const override { return ibT("report_field"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("describing a field of '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Say what a field of the report IS - the same things the composer window's Fields page "
			"says: its TITLE (printed over its column), its ROLE in a balance (period / dimension / opening / "
			"closing) and its APPEARANCE (for now the FORMAT its values are written in). Until somebody says "
			"otherwise a field is what its source says it is. A report's roles are said HERE, not with `ROLE` in its "
			"query - the composer drops that. The fields themselves are what the query's SELECT yields: a field is "
			"added, removed or renamed by changing the query. The field is named by `path` as report_fields lists "
			"it; a resource may be titled by the name it answers to.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgOnePath(), ArgTitle(), ArgRole(),
			ArgPeriodNumber(), ArgFormat() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		const ibString path = ArgOnePath().Text(params);
		if (path.IsEmpty()) {
			refusal = ibMcpText("Name the field to describe - `path`, as report_fields lists it.");
			return false;
		}
		const bool saysRole = ArgRole().Given(params) || ArgPeriodNumber().Given(params);
		if (!ArgTitle().Given(params) && !saysRole && !ArgFormat().Given(params)) {
			refusal = ibMcpText("Say something about the field - a `title`, a `role` or a `format`.");
			return false;
		}
		// 🛑 A FIELD FIRST, then a resource. `SUM(Opening) AS Opening` is the ordinary way to total a field, and the
		// name then belongs to both: taken as the resource, its role and its format were refused and the report's
		// every figure lost its title with them (2026-09-29, the first report built through this verb). Only a name
		// the query does not offer is a resource alone — one over an expression, which has no field to describe.
		const bool isResourceName = std::any_of(composition.m_resources.begin(), composition.m_resources.end(),
			[&path](const ibResourceDescription& r) { return r.AnswersTo().IsSameAs(path, false); });
		ibString fault;
		const bool isField = PathIsOffered(composition, path, fault);
		if (!isField && !isResourceName) {
			refusal = fault;
			return false;
		}
		if (!isField && (saysRole || ArgFormat().Given(params))) {
			refusal = ibString::Format(ibMcpText("'%s' is a resource over an expression, not a field of the query: a "
				"role and a format are a field's - give them to the fields it folds. Its title is said here."), path);
			return false;
		}

		// WHAT THE SOURCE SAYS OF IT — the generated title and the role a person's word is measured against.
		const std::vector<ibQueryConstructorField> fields = FieldsOf(composition, fault);
		const auto source = std::find_if(fields.begin(), fields.end(),
			[&path](const ibQueryConstructorField& f) { return f.m_name.IsSameAs(path, false); });
		const ibString      caption    = source != fields.end() ? source->m_caption : ibString();
		const ibBalanceRole sourceRole = source != fields.end() ? source->m_balanceRole : ibBalanceRole::None;

		// THE FIELD'S ENTRY — the one door every writer of a field goes through (the Fields page is the other).
		ibFieldDescription* field = ibFieldEntryForPath(composition.m_selects, path);
		if (field == nullptr) {
			refusal = ibString::Format(ibMcpText("'%s' does not say which select of the query it is - qualify it by the select's name."), path);
			return false;
		}
		if (ArgTitle().Given(params))
			field->SayTitle(ArgTitle().Text(params));
		if (saysRole) {
			ibBalanceRole role = field->RoleInForce(sourceRole);
			if (ArgRole().Given(params)) {
				const ibString word = ArgRole().Text(params);
				role = sourceRole;   // `source` — the field follows its source again
				for (const ibBalanceRole known : { ibBalanceRole::None, ibBalanceRole::Moment, ibBalanceRole::Dimension,
				                                   ibBalanceRole::Opening, ibBalanceRole::Closing })
					if (word.IsSameAs(ibBalanceRoleWord(known), false))
						role = known;
			}
			const std::int64_t number = ArgPeriodNumber().Given(params) ? ArgPeriodNumber().Whole(params) : 0;
			if (number != 0 && role != ibBalanceRole::Moment) {
				refusal = ibMcpText("A number orders PERIODS: give `period_number` to a field whose role is `period`.");
				return false;
			}
			if (number < 0 || number >= ibSourcePeriodRank(0)) {
				refusal = ibString::Format(ibMcpText("A period's number is 1 to %d, or 0 for the order it stands in."),
					ibSourcePeriodRank(0) - 1);
				return false;
			}
			field->SayRole(role, sourceRole, static_cast<int>(number));
		}
		if (ArgFormat().Given(params)) {
			const ibString format = ArgFormat().Text(params);
			field->m_appearance.Say(ibAppearanceParameter::Format, !format.IsEmpty(),
				format.IsEmpty() ? ibDataNode() : ibStoreValue(ibValue(format)));
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		result.SetValue(ibT("path"), path);
		result.SetValue(ibT("title"), field->TitleInForce(caption));
		result.SetValue(ibT("role"), ibString(ibBalanceRoleWord(field->RoleInForce(sourceRole))).Lower());
		if (field->m_useRole && field->m_periodRank > 0)
			result.SetValue(ibT("period_number"), static_cast<std::int32_t>(field->m_periodRank));
		result.SetValue(ibT("format"),
			ibString(ibStoredValue(field->m_appearance.ValueInForce(ibAppearanceParameter::Format), composer->GetMetaData()).GetString()));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportField);

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

	ibString GetName() const override { return ibT("report_variant"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ArgAdd().Flag(params)
			? ibString::Format(ibMcpText("adding the variant '%s'"), ArgName().Text(params))
			: ibString::Format(ibMcpText("naming a variant of the composer '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		const ibString name = ArgName().Text(params);
		const ibString synonym = ArgSynonym().Text(params);

		// A variant's synonym is a caption like any other — every language the configuration declares
		// (ibMcpCaptionInEveryLanguage).
		if (params.Has(ArgSynonym().Name())
		    && !ibMcpCaptionInEveryLanguage(composer->GetMetaData(), synonym,
		                                    ibMcpText("The variant's synonym"), refusal))
			return false;

		if (composition.m_variants.empty())
			composition.m_variants.emplace_back();

		const auto named = [&composition](const ibString& wanted) {
			return std::find_if(composition.m_variants.begin(), composition.m_variants.end(),
				[&wanted](const ibVariantDescription& v) { return v.m_name.IsSameAs(wanted, false); });
		};

		// TAKEN OUT — by name, and never the last one: a composer with no variant has nowhere to
		// keep the settings it composes on.
		if (ArgRemove().Flag(params)) {

			const auto found = named(name);
			if (found == composition.m_variants.end()) {
				refusal = ibString::Format(ibMcpText("This composer has no variant called '%s'."), name);
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
				refusal = ibString::Format(
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
					refusal = ibString::Format(
						ibMcpText("Another variant is already called '%s'."), name);
					return false;
				}
				variant->m_name = name;
			}

			if (params.Has(ArgSynonym().Name()))
				variant->m_synonym = synonym;
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		// ANSWERED WITH THE WHOLE LIST, so a caller sees what the picker will show without asking
		// again - and sees at once whether anything is still nameless.
		std::vector<ibDataValue> variants;

		for (const ibVariantDescription& variant : composition.m_variants) {
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(ibT("name"), variant.m_name);
			if (!variant.m_synonym.IsEmpty())
				entry->SetValue(ibT("synonym"), variant.m_synonym);
			variants.push_back(ibDataValue::Child(entry));
		}

		result.AddField(ibT("variants"), ibDataValue::Array(variants));
		ibMcpSayComposerComplaints(composition, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportVariant);

class ibMcpToolReportOutput : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("report_output"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("adding an output to the composer '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		const ibString name = ArgName().Text(params);

		// TAKEN OUT, when that is what was asked — see ArgRemove.
		if (ArgRemove().Flag(params)) {

			auto& structure = variant->m_settings.m_structure;

			const auto found = std::find_if(structure.begin(), structure.end(),
				[&name](const ibOutputDescription& output) { return output.m_name.IsSameAs(name, false); });

			if (found == structure.end()) {
				refusal = ibString::Format(
					ibMcpText("This variant has no output called '%s'."), name);
				return false;
			}

			structure.erase(found);

			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);

			result.AddField(ibT("removed"), ibDataValue::Bool(true));
			result.SetValue(ibT("output"), name);
			return true;
		}

		for (const ibOutputDescription& existing : variant->m_settings.m_structure) {
			if (existing.m_name.IsSameAs(name, false)) {
				refusal = ibString::Format(
					ibMcpText("This variant already has an output called '%s'."), name);
				return false;
			}
		}

		ibOutputDescription output;
		output.m_name = name;
		variant->m_settings.m_structure.push_back(output);

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		result.AddField(ibT("added"), ibDataValue::Bool(true));
		result.SetValue(ibT("output"), name);
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

	ibString GetName() const override { return ibT("report_level"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("grouping '%s' by %s"),
			ibMcpNameOf(params), ArgGroupBy().Text(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Add a grouping level to an output - a report grouped by month, by department - "
			"down the ROWS by default, across the "
			"COLUMNS when asked, which is what makes a cross table. The path must be one the "
			"query projects: report_fields lists them, and a path that is not there is refused.\n"
			"`groupByAll` puts SEVERAL fields in ONE level, which is what an object's own facts "
			"want: [\"FixedAsset\", \"InventoryNumber\", \"Method\"] is one line per asset carrying "
			"all three, where a level each nests them and repeats the row once per level. Reach for "
			"a level each only when every one of them genuinely subdivides the one above it.\n"
			"`periods` groups a date by months, days, quarters - one heading per period, with the empty ones "
			"between `periodsFrom` and `periodsTo` shown too.\n"
			"No field at all is a DETAIL level: the records themselves, showing what report_select selects. That is "
			"where a register read by `Recorder` / `Record` puts its documents and line numbers - the periodicity "
			"already made each row one document's record, so they are selected onto the detail level, not grouped "
			"by.\n"
			"`remove` takes a level out again, named by the field it groups by (or with neither "
			"field given, the DETAIL level).");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgOutput(), ArgGroupBy(), ArgGroupByMany(), ArgPeriods(), ArgPeriodsFrom(), ArgPeriodsTo(),
			  ArgColumns(), ArgVariant(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		ibVariantDescription* variant =
			VariantOf(composition, ArgVariant().Text(params), refusal);
		if (variant == nullptr)
			return false;

		const ibString outputName = ArgOutput().Text(params);

		ibOutputDescription* output = nullptr;
		ibString available;
		for (ibOutputDescription& candidate : variant->m_settings.m_structure) {
			available << (available.IsEmpty() ? ibT("") : ibT(", ")) << candidate.m_name;
			if (candidate.m_name.IsSameAs(outputName, false))
				output = &candidate;
		}

		if (output == nullptr) {
			refusal = available.IsEmpty()
				? ibMcpText("This variant has no output yet - add one with report_output.")
				: ibString::Format(ibMcpText("There is no output called '%s'. It has: %s."),
					outputName, available);
			return false;
		}

		// ONE FIELD OR SEVERAL, read into one list so everything below knows only the list — the
		// same shape report_select uses for `path` / `paths`.
		std::vector<ibString> wanted;

		if (auto many = params.FindField(ArgGroupByMany().Name())) {
			if (many->Kind() == ibDataKind::Array)
				for (const ibDataValue& one : many->AsArray())
					if (one.Kind() == ibDataKind::String && !one.AsString().IsEmpty())
						wanted.push_back(one.AsString());
		}

		if (const ibString one = ArgGroupBy().Text(params); !one.IsEmpty())
			wanted.push_back(one);

		const bool columns = ArgColumns().Flag(params);
		std::vector<ibLevelDescription>& levels = columns ? output->m_columnGroups : output->m_rowGroups;

		// ⭐⭐ A LEVEL CAN BE TAKEN OUT AGAIN, and until now it could not — a grouping added to see
		// what it looked like stayed for good, and the only way back was to rewrite the whole
		// composition with report_set. Named by the field it groups by, because that is what the
		// caller knows about it; with no field named it is the DETAIL level, which has no field by
		// construction.
		if (ArgRemove().Flag(params)) {

			const auto matches = [&wanted](const ibLevelDescription& level) {
				if (wanted.empty())
					return level.m_kind == ibCompositionLevelKind::Details;
				if (level.m_kind != ibCompositionLevelKind::Grouping)
					return false;
				for (const ibGroupLineDescription& field : level.m_settings.m_group.m_lines)
					for (const ibString& name : wanted)
						if (field.m_path.IsSameAs(name, false))
							return true;
				return false;
			};

			const size_t before = levels.size();
			levels.erase(std::remove_if(levels.begin(), levels.end(), matches), levels.end());

			if (levels.size() == before) {
				// SAID RATHER THAN REPORTED AS DONE. "Removed" about a level that is still there is
				// the answer that costs the next hour.
				ibString has;
				for (const ibLevelDescription& level : levels)
					for (const ibGroupLineDescription& field : level.m_settings.m_group.m_lines)
						has << (has.IsEmpty() ? ibT("") : ibT(", ")) << field.m_path;
				refusal = has.IsEmpty()
					? ibMcpText("This output has no such level. Nothing was removed.")
					: ibString::Format(ibMcpText("This output has no level grouped by that. It groups by: %s. "
						"Nothing was removed."), has);
				return false;
			}

			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);

			result.AddField(ibT("removed"), ibDataValue::Int((std::int64_t)(before - levels.size())));
			result.SetValue(ibT("output"), outputName);
			ibMcpSayComposerComplaints(composition, result);
			return true;
		}

		ibLevelDescription level;

		if (wanted.empty()) {
			// A DETAIL LEVEL groups by nothing on purpose: it is the rows. Said by
			// its KIND rather than by an empty grouping, so "nothing yet" and
			// "nothing, deliberately" stay different things.
			level.m_kind = ibCompositionLevelKind::Details;
		}
		else {
			for (const ibString& one : wanted)
				if (!PathIsOffered(composition, one, refusal))
					return false;

			// ⭐ ALL OF THEM INTO ONE LEVEL. The grouping is a list and always was; putting each
			// field in a level of its own is what turned an asset's three facts into three nested
			// headings repeating the same row.
			level.m_kind = ibCompositionLevelKind::Grouping;
			for (const ibString& one : wanted)
				level.m_settings.m_group.Append(one, ibQueryDimUnfold::Elements);
		}

		// …BY PERIODS, the unit in the platform's own spelling (ibPeriodUnits) — the word the query and the
		// settings window write.
		const ibString unit = ArgPeriods().Text(params);
		ibGroupPeriodsDescription periods;
		if (!unit.IsEmpty()) {
			if (wanted.size() != 1) {
				refusal = ibMcpText("Periods cut ONE date field: name it with `groupBy`.");
				return false;
			}
			ibString known;
			for (const auto& one : ibPeriodUnits()) {
				if (one.second.IsSameAs(unit, false))
					periods.m_unit = one.second;
				known << (known.IsEmpty() ? ibT("") : ibT(", ")) << one.second;
			}
			if (!periods.IsOk()) {
				refusal = ibString::Format(ibMcpText("'%s' is not a length of period. It takes: %s."), unit, known);
				return false;
			}
			periods.m_from = ArgPeriodsFrom().Text(params);
			periods.m_to   = ArgPeriodsTo().Text(params);
			level.m_settings.m_group.m_lines.back().m_periods = periods;
		}
		else if (ArgPeriodsFrom().Given(params) || ArgPeriodsTo().Given(params)) {
			refusal = ibMcpText("`periodsFrom` and `periodsTo` bound a level BY PERIODS: say `periods` too.");
			return false;
		}

		levels.push_back(level);

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		result.AddField(ibT("added"), ibDataValue::Bool(true));
		result.SetValue(ibT("output"), outputName);
		result.SetValue(ibT("where"), ibString(columns ? ibT("columns") : ibT("rows")));
		ibMcpSayComposerComplaints(composition, result);

		// WHAT THE LEVEL ACTUALLY GROUPS BY — all of it, in order, because a level of several is
		// now an ordinary answer and reporting only the first would describe a different report.
		if (wanted.empty()) {
			result.AddField(ibT("detail"), ibDataValue::Bool(true));
		}
		else {
			std::vector<ibDataValue> by;
			for (const ibString& one : wanted)
				by.push_back(ibDataValue::String(one));
			result.AddField(ibT("groupBy"), ibDataValue::Array(by));
		}
		if (periods.IsOk())
			result.SetValue(ibT("periods"), periods.m_unit);

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportLevel);

//---------------------------------------------------------------------------
// report_resource
//---------------------------------------------------------------------------
class ibMcpToolReportResource : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("report_resource"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("totalling %s in '%s'"),
			ArgPath().Text(params), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Declare a resource - what the levels FOLD. A function over a field "
			"(SUM over Quantity), one aggregate call over an expression, `SUM(Quantity * Price)`, or "
			"arithmetic BETWEEN calls, `SUM(Accrued) - SUM(Deducted)` or `100 * SUM(Margin) / SUM(Revenue)` "
			"- worked out at every grouping from the calls folded there, which a sum of per-row ratios "
			"is not. Arithmetic between calls folds no one field, so it needs `name`: the field of the "
			"query it answers to - a grouping shows the figure, a detail row the field's own value. An "
			"area (`over`) is the whole figure's. The resource has no caption of its own: it is built on a field, and the field holds "
			"the title. A resource is known by its name: declaring one under a name already taken "
			"REPLACES it, so running the same build twice leaves one. Pass remove:true with the "
			"same path to take one out again.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments =
			{ ArgId(), ArgFunction(), ArgPath(), ArgName(), ArgOver(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		ibCompositionDescription composition = composer->GetCompositionDesc();

		const ibString function = ArgFunction().Text(params);
		const ibString path = ArgPath().Text(params);

		// TAKEN OUT, when that is what was asked — the inverse this verb was missing. Matched on
		// the PATH, which is what a resource is about; the alias is optional and the function is
		// how it folds, so neither identifies one.
		if (ArgRemove().Flag(params)) {

			auto& resources = composition.m_resources;

			const auto found = std::find_if(resources.begin(), resources.end(),
				[&path](const ibResourceDescription& resource) { return resource.m_path.IsSameAs(path, false); });

			if (found == resources.end()) {
				refusal = ibString::Format(ibMcpText("This composer has no resource over '%s'."), path);
				return false;
			}

			resources.erase(found);

			composer->SetCompositionDesc(composition);
			activeMetaData->Modify(true);

			result.AddField(ibT("removed"), ibDataValue::Bool(true));
			result.SetValue(ibT("path"), path);
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

		// ⭐ ONE FIGURE PER NAME — stating a resource again REPLACES it, the way stating a parameter
		// again does. The name is what the figure is read back under (the query's `AS`, `res["Qty"]`),
		// so two resources answering to one name cannot both be read. This verb appended, and a build
		// script run twice declared every resource twice: the reports then failed where the query was
		// lowered, and came back empty (the payroll demo, 2026-09-10).
		auto same = std::find_if(composition.m_resources.begin(), composition.m_resources.end(),
			[&](const ibResourceDescription& existing) { return existing.AnswersTo().IsSameAs(resource.AnswersTo(), false); });
		const bool replaced = same != composition.m_resources.end();
		if (replaced)
			*same = resource;
		else
			composition.m_resources.push_back(resource);

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		result.AddField(replaced ? ibT("replaced") : ibT("added"), ibDataValue::Bool(true));
		if (function.IsEmpty()) {
			result.SetValue(ibT("expression"), path);
			result.SetValue(ibT("note"),
				ibMcpText("An expression is not checked here: the query it goes into is built and parsed "
				  "by compose_run, and a mistake in it comes back from there."));
		}
		else {
			result.SetValue(ibT("function"), function);
			result.SetValue(ibT("path"), path);
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

	ibString GetName() const override { return ibT("report_query"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("setting a composer's query");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("The query a composer composes OVER - what it reads before anything is grouped or "
			"totalled. Refused, and nothing stored, when the text does not parse or names "
			"something this configuration does not have: query_sources and query_fields say what "
			"it does have, and report_fields then lists what the stored query offers to group and "
			"total by. `tableParameters` lists each WHERE condition a virtual table's own parameters "
			"would take - move it inside the brackets, so the table selects before it folds.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObjectComposer* composer = FindComposer(params, refusal);
		if (composer == nullptr)
			return false;

		// The KIND is checked, the way module_write checks it: an argument that is not a string
		// reads as empty and would silently clear the query instead of setting it.
		auto incoming = params.FindField(ArgText().Name());

		if (!incoming || incoming->Kind() != ibDataKind::String) {
			refusal = ibMcpText("'text' must be a string holding the query. Nothing was written.");
			return false;
		}

		const ibString text = incoming->AsString();

		// AN EMPTY QUERY IS A LEGITIMATE ASK — it is how a composer is cleared — and there is
		// nothing in it to judge.
		if (!text.IsEmpty()) {

			try {
				ibQueryParser parser;
				// The engine's door: the core parser's refusal is raised as the engine's, so the syntax handler below reads its span.
				ibQueryPackage package;
				try { package = parser.ParsePackage(text); }
				catch (const ibQueryRefusal& refusal) { ibBackendQuerySyntaxException::Raise(refusal); }

				// Against THIS configuration, named explicitly: a tool is not standing inside one.
				const ibSourceMetaDataScope resolveAgainst(activeMetaData);
				ibQueryLowering::CheckNames(package, std::map<ibString, ibValue>());

				// Stored as written — and told, in the same answer, which WHERE condition reads the whole
				// register first while the table's own parameter would select before the fold.
				std::vector<ibDataValue> advice;
				for (const ibString& sentence : ibQueryLowering::FiltersAroundVirtualTables(package))
					advice.push_back(ibDataValue::String(sentence));
				if (!advice.empty())
					result.AddField(ibT("tableParameters"), ibDataValue::Array(advice));

				// 🛑⭐⭐ A JOIN IS NOT REFUSED HERE, and the attempt to refuse it is worth keeping as
				// a warning to whoever reads this next. A composer's query with a JOIN used to fail
				// at RUN time — the composer reads its author's query as a nested source, and that
				// road would not carry joins — so this tool briefly turned that into a refusal at
				// the write, on the reasoning that catching it early beats catching it in front of
				// the person.
				//
				// The reasoning was right and the target was wrong: the query was LEGITIMATE (Max,
				// 2026-09-09: *"your query is completely legitimate, it is the composer that
				// handled it wrongly — you must not cut your own hands off"*). Refusing it made the
				// tool enforce a defect instead of reporting one, and it would have gone on
				// refusing correct queries long after the defect was gone.
				//
				// The defect is fixed where it lived: WrapSelectAsQueryable now builds its source
				// tree with BuildSourceTree, the same builder the statement road and the CTE road
				// use, so a nested source carries joins like any other query.
				//
				// THE RULE THIS LEAVES: a door may refuse what is WRONG. What is merely unsupported
				// today is the engine's business to finish, not this tool's to forbid.
			}
			// THE TWO VARIETIES, CAUGHT BY TYPE — a name that does not exist, and a text that does not
			// parse. This used to read the POSITION to tell them apart (0:0 meant a name), which
			// stopped being true the moment an unresolved source learnt to point at its own FROM.
			catch (const ibBackendQueryNameException& e) {
				refusal = ibString::Format(ibMcpText("The query names something this configuration does "
					"not have: %s. Nothing was written."), e.GetErrorDescription());
				return false;
			}
			catch (const ibBackendQuerySourceException& e) {
				refusal = ibString::Format(ibMcpText("The query does not parse at %i:%i - %s. Nothing was "
					"written."), (int)e.GetLine(), (int)e.GetColumn(), e.GetErrorDescription());
				return false;
			}
			catch (const ibCoreException& e) {
				refusal = ibString::Format(
					ibMcpText("The query was refused: %s. Nothing was written."), e.GetErrorDescription());
				return false;
			}
		}

		ibCompositionDescription composition = composer->GetCompositionDesc();
		composition.m_query = text;
		composer->SetCompositionDesc(composition);

		activeMetaData->Modify(true);

		result.SetValue(ibT("composer"), composer->GetName());
		result.AddField(ibT("characters"), ibDataValue::Int((std::int64_t)text.Length()));

		// ⭐ WHAT IT NOW OFFERS, in the same breath. The next question after "the query is set" is
		// always "so what can I group by", and answering it here saves the round trip — and shows
		// immediately whether the text projects what the caller thought it did.
		ibString fault;
		const std::vector<ibQueryConstructorField> fields =
			ibQueryFieldsOfText(text, activeMetaData, &fault);

		std::vector<ibDataValue> names;
		for (const ibQueryConstructorField& field : fields)
			names.push_back(ibDataValue::String(field.m_name));   // the name the AST carries

		result.AddField(ibT("fields"), ibDataValue::Array(names));

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

	ibString GetName() const override { return ibT("report_set"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("setting the composer '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectComposer* composer =
			object->ConvertToType<ibValueMetaObjectComposer>();
		if (composer == nullptr) {
			refusal = ibString::Format(
				ibMcpText("'%s' is not a composer. A report declares one under itself."),
				object->GetName());
			return false;
		}

		auto given = params.FindChild(ArgComposition().Name());
		if (!given) {
			refusal = ibMcpText("No composition given. report_get answers with the shape this takes.");
			return false;
		}

		// ⭐ FILLED INTO A FRESH ONE, THEN PLACED. Reading straight into the live description would
		// leave a half-read composition standing in the configuration if the read failed partway —
		// and a partly-read report is the kind of wrong that looks like an edit.
		ibCompositionDescription composition;

		if (!ibCompositionDescriptionMemory::ReadNode(*given, composition)) {
			refusal = ibMcpText("That is not a composition this platform can read. Send back the shape "
				"report_get gives, with your changes in it.");
			return false;
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		// ANSWERED WITH WHAT IT NOW HOLDS, read back off the composer rather than echoed from the
		// argument: what was asked for and what was taken are different facts.
		result.SetValue(ibT("composer"), composer->GetName());

		if (!ibCompositionDescriptionMemory::WriteNode(result, composer->GetCompositionDesc())) {
			refusal = ibMcpText("The composition was placed, but could not be read back to confirm it.");
			return false;
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportSet);
