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
		_("The composer's NodeId - a report declares one, metadata_get on the report "
			  "lists it among its children."), /*required*/ true);
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		_("What to call it - how a reader will pick it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgVariant()
{
	static const ibArg s_a(wxT("variant"), ibArg::Kind::Text,
		_("Which variant it belongs to. Omit for the author's."));
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
		_("Take it OUT instead of putting it in. Off by default."));
	return s_a;
}

const ibArg& ArgOutput()
{
	static const ibArg s_a(wxT("output"), ibArg::Kind::Text,
		_("Which output."), /*required*/ true);
	return s_a;
}

const ibArg& ArgGroupBy()
{
	static const ibArg s_a(wxT("groupBy"), ibArg::Kind::Text,
		_("The field to group by. Omit for a DETAIL level - the rows themselves."));
	return s_a;
}

const ibArg& ArgColumns()
{
	static const ibArg s_a(wxT("columns"), ibArg::Kind::Flag,
		_("Put it across the columns instead of down the rows."));
	return s_a;
}

const ibArg& ArgFunction()
{
	static const ibArg s_a(wxT("function"), ibArg::Kind::Text,
		_("SUM, MIN, MAX, COUNT... Omit when `path` is a whole expression."));
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(wxT("path"), ibArg::Kind::Text,
		_("The field to fold, or the expression when no function is given."), /*required*/ true);
	return s_a;
}

const ibArg& ArgOver()
{
	static const ibArg s_a(wxT("over"), ibArg::Kind::Text,
		_("The grouping it is computed over. Omit for the ladder - one figure per heading, "
			  "following whatever the reader regrouped."));
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		_("The query. An empty one is allowed and clears it."), /*required*/ true);
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
		_("The whole composition, in the shape report_get answers with. Read it, change what you "
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
		return wxString::Format(_("reading the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return _("A composer as the tree it is: the query it reads, the fields and resources it "
			"declares, and its outputs - each with the levels it groups by, down the rows and "
			"across the columns. This is what a report IS; the query below it is what the "
			"composer renders into.");
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
				_("'%s' is not a composer. A report declares one under itself."),
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
				_("'%s' could not describe itself. The composition is there; reading it out failed."),
				object->GetName());
			return false;
		}

		if (composition.m_variants.empty()
			|| composition.m_variants.front().m_settings.m_structure.empty())
			result.SetValue(wxT("note"),
				_("The composer declares no output yet - nothing would be produced."));

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
			_("'%s' is not a composer. A report declares one under itself."), object->GetName());
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
			_("The composer's query cannot be read, so nothing can be checked against it: %s"),
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
		? _("The composer's query projects no fields yet - give it a query first.")
		: wxString::Format(_("'%s' is not a field this query offers. It has: %s."),
			path, available);
	return false;
}

// Every write lands in a VARIANT, because that is where settings live — see the
// note in report_get. Named, or the first one, which is the author's.
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
			<< (variant.m_name.IsEmpty() ? _("(the author's)") : variant.m_name);
	}

	refusal = wxString::Format(_("This composer has no variant called '%s'. It has: %s."),
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
void ibMcpWarnNothingSelected(const ibCompositionDescription& composition, ibDataNode& result)
{
	if (!composition.m_selected.empty())
		return;

	result.SetValue(wxT("warning"),
		_("Nothing is SELECTED for output yet, so this report will compose its groupings with no "
		  "columns - every heading in place and every figure missing. Say which fields to show, "
		  "at least one per resource: a resource says what to FOLD, a selected field says what to "
		  "SHOW, and they are two separate statements."));
}

} // namespace

//---------------------------------------------------------------------------
// report_fields
//---------------------------------------------------------------------------
class ibMcpToolReportFields : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_fields"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("looking at what the composer '%s' can group by"),
			ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return _("What a composer's query projects - the fields anything in its settings may "
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
				_("The query projects nothing yet - set the composer's query first."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportFields);

//---------------------------------------------------------------------------
// report_output
//---------------------------------------------------------------------------
class ibMcpToolReportOutput : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("report_output"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("adding an output to the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return _("Add an output to a composer - one report shape. A composer with no output "
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
					_("This variant has no output called '%s'."), name);
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
					_("This variant already has an output called '%s'."), name);
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
		return wxString::Format(_("grouping '%s' by %s"),
			ibMcpNameOf(params), ArgGroupBy().Text(params));
	}

	wxString GetDescription() const override
	{
		return _("Add a grouping level to an output - down the ROWS by default, across the "
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
				? _("This variant has no output yet - add one with report_output.")
				: wxString::Format(_("There is no output called '%s'. It has: %s."),
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
		return wxString::Format(_("totalling %s in '%s'"),
			ArgPath().Text(params), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return _("Declare a resource - what the levels FOLD. Either a function over a field "
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
				refusal = wxString::Format(_("This composer has no resource over '%s'."), path);
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
				_("An expression is not checked here - script_check compiles it, which is what "
				  "the settings window does before it closes."));
		}
		else {
			result.SetValue(wxT("function"), function);
			result.SetValue(wxT("path"), path);
		}

		ibMcpWarnNothingSelected(composition, result);
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
		return _("setting a composer's query");
	}

	wxString GetDescription() const override
	{
		return _("The query a composer composes OVER - what it reads before anything is grouped or "
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
			refusal = _("'text' must be a string holding the query. Nothing was written.");
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
			catch (const ibBackendQuerySourceException& e) {
				// The position is what tells a misplaced comma from a name that does not exist:
				// the latter arrives at 0:0, because there is nothing in the text to point at.
				const s32 line = (s32)e.GetLine();
				const s32 column = (s32)e.GetColumn();

				refusal = (line == 0 && column == 0)
					? wxString::Format(_("The query names something this configuration does not "
						"have: %s. Nothing was written."), e.GetErrorDescription())
					: wxString::Format(_("The query does not parse at %i:%i - %s. Nothing was "
						"written."), (int)line, (int)column, e.GetErrorDescription());
				return false;
			}
			catch (const ibBackendException& e) {
				refusal = wxString::Format(
					_("The query was refused: %s. Nothing was written."), e.GetErrorDescription());
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
		return wxString::Format(_("setting the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return _("Replace a composer's WHOLE composition with one you were given by report_get - "
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
				_("'%s' is not a composer. A report declares one under itself."),
				object->GetName());
			return false;
		}

		const ibDataNode* given = params.FindChild(ArgComposition().Name());
		if (given == nullptr) {
			refusal = _("No composition given. report_get answers with the shape this takes.");
			return false;
		}

		// ⭐ FILLED INTO A FRESH ONE, THEN PLACED. Reading straight into the live description would
		// leave a half-read composition standing in the configuration if the read failed partway —
		// and a partly-read report is the kind of wrong that looks like an edit.
		ibCompositionDescription composition;

		if (!ibCompositionDescriptionMemory::ReadNode(*given, composition, activeMetaData)) {
			refusal = _("That is not a composition this platform can read. Send back the shape "
				"report_get gives, with your changes in it.");
			return false;
		}

		composer->SetCompositionDesc(composition);
		activeMetaData->Modify(true);

		// ANSWERED WITH WHAT IT NOW HOLDS, read back off the composer rather than echoed from the
		// argument: what was asked for and what was taken are different facts.
		result.SetValue(wxT("composer"), composer->GetName());

		if (!ibCompositionDescriptionMemory::WriteNode(result, composer->GetCompositionDesc())) {
			refusal = _("The composition was placed, but could not be read back to confirm it.");
			return false;
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolReportSet);
