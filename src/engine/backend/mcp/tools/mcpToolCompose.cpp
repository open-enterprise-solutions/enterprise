////////////////////////////////////////////////////////////////////////////
//	Description : RUNNING a composition and reading what it answers -
//	              compose_run. The report_* family reads and writes a
//	              composer's SCHEMA; this is the one verb that produces its
//	              FIGURES.
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ WHY THIS EXISTS. Eleven verbs already state what a report IS — its query, fields, filters,
// order, parameters, outputs, levels, resources, variants. None of them runs it, so a report's
// NUMBERS have only ever been checkable by a person looking at the screen. That is what makes the
// report layer the one place a regression is noticed by eye, while the query engine underneath it
// is checked by suites.
//
// ⭐ IT REPRODUCES, IT DOES NOT INSPECT. Nothing here attaches to a live form or to a composer
// somebody has open. A schema plus a set of settings is enough to build the situation again — which
// is simpler AND safer, since reaching into another window from a tool call is a hazard avoided by
// not needing it. The same reason this file touches no existing one: a tool that diagnoses must not
// change what it diagnoses.
//
// THE THREE INPUTS, and they are three because they answer three different questions:
//
//   * the SCHEMA        — what is being run. From the composer metaobject, or handed over WHOLE.
//   * the SETTINGS      — what somebody chose: an author's VARIANT, or a reader's SAVED setting.
//   * the PARAMETERS    — what the schema asks for, the period first among them. Stated by hand and
//                         applied LAST, so "now show me half a year" changes one thing and nothing
//                         else. A saved setting usually carries a period of its own; an explicit
//                         parameter is meant to override it, which is the whole point of being able
//                         to state one.
//
// 🛑⭐⭐ AND THE FIGURES ARE NOT READ HERE. The designer does not work with data — a rule about RIGHTS,
// not about where a runtime happens to sit (Max, 2026-09-06: *"we do not work with data in the
// designer, we get it through the wire; and the wire is what the user sees, that they really gave
// access"*). This file resolves the schema and folds the settings, which are the CONFIGURATION and a
// person's own choices; then it sends them to the application, which runs them and answers with the
// tables. There is no local road, deliberately: a fallback that read here when nothing was attached
// would be the very thing this refuses to do.
//
// ⭐⭐ AND BECAUSE WHAT CROSSES IS A DESCRIPTION rather than the name of one, `schema` may be handed
// over whole — so a composition that exists NOWHERE runs exactly like a report that does. That was
// not built; it fell out of the split (Max: *"you can take an existing schema, or generate one
// yourself"*). See composition/composeRunSchema.h for the other half.
//
// ⚠ AND IT RUNS THE SCHEMA AS A REPORT, WHOLE. A dynamic list reads the same schemas but PAGES them
// — an envelope in, a screenful out. Paging is what a window needs in order to draw; a reader
// comparing figures wants the figures. So there is one road here and it returns the batch.

#include "backend/mcp/mcpTool.h"

#include "backend/compositionDescription.h"
#include "backend/metaCollection/metaComposerObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/composition/dataComposer.h"                    // ibDataDBComposer — folding the settings
#include "backend/composition/composeRunSchema.h"                     // …and the field names the far end reads
#include "backend/settings/settingsComposer.h"                   // saved settings, restored INTO a composer
#include "backend/userInfo.h"                                    // ibUserInfo - WHOSE setting
#include "backend/mcp/mcpDebugBridge.h"                          // the figures are asked for over the wire
#include "backend/debugger/debugClient.h"                        // …and there has to BE one to ask over

#include <memory>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// ⚠ REQUIRED FOR ONE VERB AND NOT THE OTHER, which is why it is asked for rather than fixed.
// compose_settings is ABOUT a report, so it must be named; compose_run takes either a report's id or
// a schema handed over whole, so neither is required on its own. The schema a caller reads is built
// from this list, so getting it wrong here means advertising a lie.
const ibArg& ArgId(bool required)
{
	static const ibArg s_optional(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The composer's NodeId - a report declares one, metadata_get on the report lists it "
			  "among its children. Give this OR `schema`, not both."), /*required*/ false);
	static const ibArg s_required(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The composer's NodeId - a report declares one, metadata_get on the report lists it "
			  "among its children."), /*required*/ true);
	return required ? s_required : s_optional;
}

const ibArg& ArgVariant()
{
	static const ibArg s_a(wxT("variant"), ibArg::Kind::Whole,
		ibMcpText("Which of the AUTHOR's variants to run, counting from 0. Default 0 - what the report "
			  "does when nobody has chosen. report_variant lists them."));
	return s_a;
}

const ibArg& ArgSettings()
{
	static const ibArg s_a(wxT("settings"), ibArg::Kind::Text,
		ibMcpText("The id of a setting somebody SAVED for themselves, to run under theirs instead of the "
			  "author's. This is what turns 'my numbers do not add up' into a question with an answer: "
			  "the person's own choices, reproduced. compose_settings lists what there is."));
	return s_a;
}

const ibArg& ArgUser()
{
	static const ibArg s_a(wxT("user"), ibArg::Kind::Text,
		ibMcpText("WHOSE saved setting. Omitted means the session's own. A setting belongs to a person as "
			  "much as to a report - the two together are its address."));
	return s_a;
}

const ibArg& ArgParameters()
{
	static const ibArg s_a(wxT("parameters"), ibArg::Kind::Node,
		ibMcpText("Values for the schema's parameters, by name - the PERIOD above all, since most reports "
			  "ask for one and answer nothing useful without it. Applied last, over whatever a saved "
			  "setting carried, so one of them can be varied and the rest left alone.\n"
			  "\n"
			  "THREE WAYS TO SAY WHAT A PARAMETER IS, and the first one is the LEAST capable:\n"
			  "\n"
			  "* a LITERAL - what a JSON scalar already is: a number, a flag, a string. NOTHING IS "
			  "CONVERTED for you: a scalar that cannot be the parameter's declared type is REFUSED, "
			  "not applied as empty. (An empty period is not 'no period' - it is the beginning of "
			  "time, and the report answers with a straight face.) A date is not a JSON scalar, so "
			  "this is not the road for one.\n"
			  "\n"
			  "* the PACKED form, as a sub-node - the shape a schema stores its own parameter values "
			  "in, and exactly what report_get shows under `Value`. THIS IS THE ONE TO USE for a "
			  "date, a reference, an enum member: it is the platform's own serialised value, so it "
			  "carries every type there is, and copying one out of a schema and back needs no "
			  "understanding of what is inside it. A REFERENCE can also be ASSEMBLED when its "
			  "identifier is known - a packed reference is its type and that identifier, nothing "
			  "more. Which is the only way to name one from here at all: a designer holds the "
			  "CONFIGURATION, so a PREDEFINED item is there to be named and an ordinary one is not - "
			  "it is data, and data lives in the application.\n"
			  "\n"
			  "* {\"expression\": \"...\"} - worked out in the APPLICATION, which is the only place "
			  "today's date, a rate, or an item found by its code means anything."));
	return s_a;
}


// ⭐⭐ A SCHEMA GIVEN WHOLE, instead of the name of one — and this is the argument that turns a
// diagnostic tool into a way of ASKING QUESTIONS OF THE DATA. Because what crosses the wire is a
// description rather than an id, a composition that exists nowhere runs exactly like a report that
// does: assemble one, send it, read the figures, throw it away.
//
// ⚠ Nothing was built for this. It is a second source for one field, and the far end cannot tell
// the two apart — which is the test of whether a road was added or merely used.
const ibArg& ArgSchema()
{
	static const ibArg s_a(wxT("schema"), ibArg::Kind::Node,
		ibMcpText("A composition to run, given WHOLE, instead of naming a report with `id`. The shape is "
			  "the one report_get answers with - a query, selected fields, an output with its "
			  "groupings, resources, parameters. This is how you ask a question the configuration "
			  "has no report for: build the schema, run it, read the figures. Nothing is stored and "
			  "no report is created.\n"
			  "\n"
			  "A parameter that must be worked out where the data is - today's date, a rate, an item "
			  "found by its code - is written as an EXPRESSION in the schema (or under `parameters`, "
			  "as {\"expression\": \"...\"}). It is evaluated in the application, which is the only "
			  "place those mean anything.\n"
			  "\n"
			  "A dynamic LIST's schema lives on a form; schema_read answers it, and what it gives "
			  "back goes straight in here."),
		/*required*/ false, std::vector<wxString>(),
		[](ibDataValue& shape) {
			return ibCompositionDescriptionMemory::WriteNode(shape, ibCompositionDescription());
		});
	return s_a;
}

// WHOSE SETTING. Empty means the session's own, which is what a person asking about their own
// report means and should not have to say. A name that resolves to nobody is a REFUSAL rather than a
// silent fall back to the caller's own settings: "show me Ivanov's" answered with mine is a wrong
// answer wearing the shape of a right one.
bool UserNamed(const ibDataNode& params, const ibArg& arg, ibUserInfo& into, wxString& refusal)
{
	const wxString name = arg.Text(params);
	if (name.IsEmpty())
		return true;                       // the session's own - `into` stays unset
	into = ibUserInfo::Read(name);
	if (!into.IsOk()) {
		refusal = wxString::Format(
			ibMcpText("no account '%s'. user_list says who there is."), name);
		return false;
	}
	return true;
}

// 🛑⭐⭐ THE ONE ROAD TO THE FIGURES, AND THERE IS DELIBERATELY NO OTHER. The designer does not read
// data — a rule about rights rather than about where a runtime sits. A fallback that read locally
// when nothing was attached would be precisely the thing being refused, so this returns null and
// says what to do instead.
ibMcpDebugBridge* ComposeBridge(wxString& refusal)
{
	if (debugClient == nullptr) {
		refusal = ibMcpText("This process has no debugger client, so there is no application to read the "
			"figures from.");
		return nullptr;
	}

	ibMcpDebugBridge* const bridge = ibMcpDebug();
	if (bridge == nullptr) {
		refusal = ibMcpText("Not attached to a running application, so there is nothing to read the "
			"figures from - and they are not read here. A designer holds the CONFIGURATION; the rows "
			"belong to the application somebody started, and starting it with the debugger attached "
			"(app_run with debug true) is the act that grants access to them. The schema, the "
			"settings and the variants are all readable from here without it.");
		return nullptr;
	}

	// 🛑 AND ASKED BEFORE ANYTHING IS SENT, because otherwise the two are told apart by nothing. The
	// bridge exists from the moment the assistant attaches; whether an APPLICATION is on the other
	// end of it is a separate fact, and Compose answers false for both "nobody there" and "nobody
	// answered in time". Without this, starting no application and starting a slow one produced the
	// same sentence — and only one of them means "it may still be running" (measured 2026-09-06).
	if (!bridge->GetStop().m_connected) {
		refusal = ibMcpText("No application is running, so there is nothing to read from. app_run with "
			"debug true starts one; debug_sessions says what is already there. Nothing was sent.");
		return nullptr;
	}

	return bridge;
}

//---------------------------------------------------------------------------
// compose_run
//---------------------------------------------------------------------------
class ibMcpToolComposeRun : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("compose_run"); }

	// 🛑 NOT ON THE MAIN THREAD — see ibMcpTool::NeedsMainThread, which says this in as many words.
	// The schema goes down the socket and the tables come back through the session's worker, which on
	// the desktop IS the main loop: waiting here would be waiting for a message only this thread can
	// dispatch. Measured, because it was not read: the designer hung with `ibMcpDebugBridge::Compose`
	// blocked at the bottom of `wxEventLoopManual::DoRunLoop` (2026-09-06).
	//
	// ⚠ A DEADLOCK BY CONSTRUCTION CANNOT BE FIXED BY A LONGER DEADLINE. Nothing in this verb touches
	// a window — it talks to a socket and a condition variable, and both are indifferent to where
	// they are called from. It is the WAITING that decides, not what is being waited for.
	bool NeedsMainThread() const override { return false; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("running the composer '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("RUN A REPORT AND READ ITS FIGURES - one table per output. report_get says "
			"what a report IS; this says what it produces. It is the verb for every question about "
			"NUMBERS: 'my numbers do not add up', 'why does this report show that', 'BUILD me a NEW "
			"REPORT', 'show me an EXAMPLE of how it would work', and TESTING or CHECKING a report "
			"you have just written against REAL FIGURES.\n"
			  "\n"
			"WHAT IT IS FOR, in one sentence: ARRIVING AT THE SAME NUMBER THE PERSON IS TALKING "
			"ABOUT. Running their report is the first move, not the job. The job is to build "
			"variants - a new one, a new one under their settings, a new one with one condition "
			"changed - and run them until your figures and theirs agree or the disagreement has a "
			"name. What makes that possible is that every answer carries its full STRUCTURE: "
			"groupings, the `path` above each row, the axis of a cross-table. Two runs can be "
			"compared row by row, not impression by impression.\n"
			  "\n"
			"'I AM SITTING IN THE BASE AND THE NUMBERS DO NOT ADD UP' - the road, in order, because "
			"every step of it narrows the question:\n"
			"  1. SEE IT if you can - screen_capture, area focus. What is wrong is often visible and "
			"never described precisely.\n"
			"  2. WHICH REPORT - metadata_list {kind: \"Report\"}, and report_get for what it is.\n"
			"  3. ITS SCHEMA - report_get; for a dynamic list on a form, schema_read.\n"
			"  4. THEIR SETTINGS - compose_settings, and mind the DEFAULT one: a default setting does "
			"not look like a setting from the inside, it looks like what the report does. A variant "
			"they are on by mistake is the other half of the same question.\n"
			"  5. RUN IT AS THEY SEE IT - this verb, with their `settings` or `variant` - and read "
			"the figures. Reproducing the wrong number is the point at which it stops being their "
			"word against the report's.\n"
			"  6. THEN TAKE THE SCHEMA APART. Pass it back as `schema` with one thing changed - a "
			"filter dropped, a grouping added, a period moved - and run it again. The DIFFERENCE "
			"between their answer and yours is the defect, localised; a version that adds up beside "
			"one that does not says more than either alone. Nothing is stored while you do this, so "
			"there is no cost to trying ten.\n"
			"  Then say what it is doing, in figures they can check.\n"
			  "\n"
			"WHILE WRITING A REPORT, this is how you find out whether it works at all: take the "
			"schema you are building and run it on its own, before there is a form, a variant or a "
			"place for it in the configuration. It answers with figures or with the reason there are "
			"none.\n"
			"  NOTE - THE LIMIT, stated so it is not discovered by trying: what a person has typed into a "
			"report's form and NOT SAVED cannot be reached from here - no form can be opened and "
			"nothing can be filled in on their screen. A SAVED setting can (compose_settings), and a "
			"variant can. If what they are looking at is neither, ask them to save it, or rebuild "
			"the same choices as a `schema` - the composer here reads one exactly the way theirs "
			"does.\n"
			  "\n"
			"AND A SCHEMA YOU WRITE IS AN ARBITRARY QUERY, which makes this the way to GO AND LOOK at "
			"the data itself: the specific row somebody says is wrong, what a register actually "
			"holds under that item, whether the document they named is posted at all. When a person "
			"says 'this value is wrong', the useful next thing is rarely another opinion about the "
			"report - it is the rows underneath it, pulled out and read. That does not need a "
			"breakpoint and does not block them.\n"
			  "\n"
			"WHAT FEEDS THIS, AND WHAT TO DO WITH WHAT COMES BACK - the verbs either side of it:\n"
			"  report_get -> a stored report's schema, ready to pass here as `schema`;\n"
			"  schema_read -> a dynamic LIST's schema, which lives on a form rather than in metadata. "
			"A LIST IS DIAGNOSED THE SAME WAY A REPORT IS: 'the list is not showing my rows' and "
			"'the report total is wrong' are one question - read the schema, run it here, look at "
			"what it actually selects and what it filters by;\n"
			"  compose_settings -> the saved settings and the author's variants, by id and index;\n"
			"  value_pack -> a PARAMETER a JSON scalar cannot state - a date, a reference, an enum "
			"member. This is the road for a period.\n"
			"  value_unpack -> what a packed value IS: its type, its presentation, its identifier. "
			"Use it on a schema's stored `Value` to read what a report is already filtering by, and "
			"on anything here you cannot account for.\n"
			  "\n"
			"THREE THINGS TO RUN, and they are three different questions:\n"
			"\n"
			"* one YOU COMPOSED, here and now - pass `schema`. It IS a report, built the same way a "
			"stored one is; the difference is only WHERE IT LIVES. A report made with the report_* "
			"verbs goes into the CONFIGURATION and stays there, to be read, changed and run next "
			"month. One passed here lives IN MEMORY ONLY: it runs, answers, and is gone, with "
			"nothing stored and nothing created.\n"
			"  Which is exactly what makes this the way to CHECK a report and to TRY one: a shape "
			"can be run, read, altered and run again as many times as it takes, and none of it "
			"touches anybody's configuration. Build it here until the figures are right; move it "
			"into the configuration with the report_* verbs when it is worth keeping. It is also "
			"how you ask a question the configuration has no report for at all.\n"
			"\n"
			"* an EXISTING REPORT - pass `id`. What it answers as its author wrote it.\n"
			"\n"
			"* an existing report UNDER A PARTICULAR SETTING - `id` with `settings` (somebody's saved "
			"one, and `user` says whose) or `variant` (one the author declared). This is the one that "
			"answers 'my numbers do not add up': their choices, reproduced, rather than yours.\n"
			  "\n"
			"READING WHAT COMES BACK, because the two shapes are not read alike and `shape` says "
			"which:\n"
			"\n"
			"* `grouping` - the row dimension makes the groups and the SCHEMA makes the columns. A "
			"row's `values` line up with `columns` positionally.\n"
			"\n"
			"* `cross` - the row dimension still makes the groups, but the COLUMN dimension makes new "
			"columns, one per value it meets, so the column set is declared nowhere and grows with "
			"the data. It arrives as `axis`, IN ARRIVAL ORDER - that order IS the axis, which is why "
			"a printed cross-table carries its total column leftmost. A row's `values` line up with "
			"the axis, NOT with `columns`.\n"
			"\n"
			"* every row and every axis entry carries `path`: the headings open above it, outermost "
			"first. Without it a figure is a number with no address - in a cross-table a cell is "
			"addressed by the row's headings AND the column's value.\n"
			"\n"
			"* a figure travels as what it IS - `number`, `date`, `bool` - with its `text` beside it "
			"for reading. Compare the first; show the second.\n"
			  "\n"
			"RUNNING IT AS SOMEBODY ELSE SEES IT: under an author's `variant`, or under a `settings` "
			"somebody saved (compose_settings lists them, and says which is their DEFAULT - a report "
			"opened under a default setting looks to its reader exactly like the report itself). "
			"State `parameters` - the period above all - to vary one thing and leave the rest.\n"
			  "\n"
			"OR RUN A SCHEMA THAT EXISTS NOWHERE. Pass `schema` instead of `id` and the composition "
			"you hand over is run and thrown away - nothing is stored, no report is created. That is "
			"how you ask a question the configuration has no report for.\n"
			  "\n"
			"WHERE IT RUNS: in the APPLICATION, not here. A designer holds the configuration; the "
			"rows belong to the application somebody started, and starting it with the debugger "
			"attached (app_run with debug true) is the act that grants access to them. So this needs "
			"one attached and refuses plainly without one.\n"
			  "\n"
			"IT DOES NOT NEED A BREAKPOINT, and this is worth saying because the opposite is the "
			"natural guess: debug_sandbox and debug_evaluate both need the runtime STOPPED, so a "
			"caller reasonably assumes everything on this wire does. This does not. It asks for a "
			"rented read - a connection of its own - and a running application can start one at any "
			"moment. Nobody has to be parked, nobody's window waits, and the person keeps working "
			"while the figures are read. Set no breakpoint for this; if one is already set, it "
			"changes nothing either way.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgId(/*required*/ false), ArgSchema(), ArgVariant(), ArgSettings(), ArgUser(),
			ArgParameters() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// ⭐⭐ THE SCHEMA COMES FROM A REPORT, OR IS GIVEN WHOLE, and that second road is the reason
		// this tool is worth more than it looks (Max, 2026-09-06: *"you can take an existing schema,
		// or generate one yourself — you know how to generate a schema; then you push it over the
		// wire, it runs, and you look at the result"*). Because what crosses is a DESCRIPTION rather
		// than the name of one, a composition that exists nowhere runs exactly like a report that
		// does. Nothing new was built for it: it is a second source for one field.
		ibCompositionDescription description;
		wxString                 subject;
		// The report this came from, when it came from one — kept because a SAVED SETTING is stored
		// against its key and would otherwise have to be looked up a second time further down.
		ibValueMetaObject*       object = nullptr;

		const ibDataNode* givenSchema = params.FindChild(ArgSchema().Name());
		const bool        namedReport = params.FindField(ArgId(false).Name()) != nullptr;

		if (givenSchema != nullptr && namedReport) {
			refusal = ibMcpText("Pass one or the other: `id` runs a report's own schema, `schema` runs the "
				"one you are handing over. Two of them is two answers to 'what is being run'.");
			return false;
		}

		if (givenSchema != nullptr) {
			if (!ibCompositionDescriptionMemory::ReadNode(*givenSchema, description, activeMetaData)) {
				refusal = ibMcpText("`schema` could not be read as a composition. report_get on any report "
					"shows the shape one has.");
				return false;
			}
			subject = ibMcpText("the schema given");
		}
		else {
			object = ibMcpObjectNamed(params, refusal);
			if (object == nullptr)
				return false;

			ibValueMetaObjectComposer* composerObject = object->ConvertToType<ibValueMetaObjectComposer>();
			if (composerObject == nullptr) {
				refusal = wxString::Format(
					ibMcpText("'%s' is not a composer. metadata_get on the report lists the composer among "
						  "its children."), object->GetName());
				return false;
			}
			description = composerObject->GetCompositionDesc();
			subject     = object->GetName();
		}

		// ⚠ A COMPOSITION WITH NO SOURCE ANSWERS NOTHING, and saying so beats running an empty walk
		// and reporting no rows: those read identically and mean opposite things.
		if (!description.IsOk()) {
			refusal = wxString::Format(
				ibMcpText("'%s' has no source to read - neither a query nor a main table. report_query is "
					  "where one is put."), subject);
			return false;
		}

		// ⭐⭐ THE SETTINGS ARE FOLDED HERE, WHERE THEY LIVE. The author's variant is the ground a
		// reader's saved setting stands on, and folding the two is what the settings window does when
		// somebody picks one - so it is done through the same doors, on this side, and what crosses
		// the wire is one settled section rather than three inputs and an order to get right.
		//
		// ⚠ NO DATA IS TOUCHED BY ANY OF THIS. Restoring a saved setting reads sys_settings, which is
		// this person's own choices about their own report; the ROWS are what the designer may not
		// read, and nothing here reads a row.
		ibDataDBComposer folding;
		folding.SetMetaData(activeMetaData);
		folding.LoadVariants(description.m_variants);

		// ⚠ A SCHEMA NEED NOT DECLARE A VARIANT AT ALL, which a report always does. One assembled by
		// hand is the composition itself and has nothing to choose between — so an absent variant is
		// not variant 0 of none, it is no question. Asking for one explicitly is still refused, and
		// with the count, because that is a caller expecting something that is not there.
		const bool askedVariant = params.FindField(ArgVariant().Name()) != nullptr;
		const long variant      = static_cast<long>(ArgVariant().Whole(params));

		if (!description.m_variants.empty()) {
			if (variant < 0 || static_cast<size_t>(variant) >= description.m_variants.size()) {
				refusal = wxString::Format(
					ibMcpText("'%s' declares %d variant(s); there is no number %d. report_variant lists them."),
					subject, (int)description.m_variants.size(), (int)variant);
				return false;
			}
			folding.SetUserSettingsDesc(description.m_variants[static_cast<size_t>(variant)].m_settings);
		}
		else if (askedVariant) {
			refusal = wxString::Format(
				ibMcpText("'%s' declares no variants, so there is no number %d to run. It composes as it "
					  "stands."), subject, (int)variant);
			return false;
		}

		// …and a reader's saved setting over it, when one was named. It rides in through the door that
		// already exists for exactly this, so what is reproduced is what the person had.
		ibUserInfo owner;
		if (!UserNamed(params, ArgUser(), owner, refusal))
			return false;

		const wxString settingsId = ArgSettings().Text(params);
		if (!settingsId.IsEmpty()) {
			if (object == nullptr) {
				refusal = ibMcpText("A saved setting belongs to a REPORT - it is stored against that "
					"object's key - so there is none to restore onto a schema you are handing over. "
					"Put what you want into the schema itself.");
				return false;
			}
			if (!ibRestoreComposerSettings(ibSettingsCategory::Composer, object->GetGuid(),
			                               ibGuid(settingsId), folding, object->GetMetaData(),
			                               owner.IsOk() ? &owner : nullptr)) {
				refusal = wxString::Format(
					ibMcpText("no saved setting '%s' for '%s'. compose_settings lists what there is, and "
						  "whose."), settingsId, subject);
				return false;
			}
		}

		// 🛑⭐⭐ AND NOW IT LEAVES. THE DESIGNER DOES NOT READ DATA - that is a rule about rights, not
		// about where a runtime happens to sit (Max, 2026-09-06: *"we do not work with data in the
		// designer, we get it through the wire; and the wire is what the user sees, that they really
		// gave access"*). Whoever started the application with the debugger attached performed the
		// visible act that grants it. There is deliberately NO local road: a fallback that read here
		// when nothing was attached would be the very thing this refuses to do.
		ibMcpDebugBridge* const bridge = ComposeBridge(refusal);
		if (bridge == nullptr)
			return false;

		// 🛑⭐⭐ WRITTEN WITH `Child`, WHICH IS WHAT `FindChild` READS. A node has TWO namespaces — its
		// FIELDS and its PROPERTIES — and a composite sub-node lives among the properties:
		// `ibDataNode::Child(name)` puts it there and `FindChild(name)` looks for it there. Built with
		// AddField instead, the schema went out perfectly well and arrived nowhere, and the far end
		// answered "no schema came with the request" about a request that carried one (measured
		// 2026-09-06, first round trip). The writer and the reader have to be a pair; being both
		// plausible is not the same as being the same door.
		ibDataNode request;
		{
			ibCompositionDescriptionMemory::WriteNode(request.Child(kComposeSchema), description);
			ibSettingsDescriptionMemory::WriteNode(request.Child(kComposeSettings),
			                                       folding.GetUserSettingsDesc());

			// The parameters ride as they were given - a value the caller knows, or a child carrying
			// an `expression` for anything that has to be worked out where the runtime is.
			if (const ibDataNode* given = params.FindChild(ArgParameters().Name()))
				request.Child(kComposeParameters) = *given;
		}

		ibDataNode answer;
		bool       accepted = false;
		if (!bridge->Compose(request, answer, accepted, refusal)) {
			refusal = ibMcpText("The application did not answer in time. A report that reads for minutes "
				"can outlast the wait - it is still running over there, and nothing was changed.");
			return false;
		}
		if (!accepted) {
			if (refusal.IsEmpty())
				refusal = wxString::Format(ibMcpText("'%s' did not compose, and no reason came back."), subject);
			return false;
		}

		// What came back IS the answer - the tables, built where the rows are. Nothing is
		// reinterpreted here; a second reading of the same bytes would be a second road.
		for (const std::pair<wxString, ibDataValue>& field : answer.Fields())
			result.AddField(field.first, field.second);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolComposeRun);

//---------------------------------------------------------------------------
// compose_settings
//---------------------------------------------------------------------------
// ⭐ WHAT SOMEBODY SAVED FOR THEMSELVES, so that "reproduce what Ivanov sees" can be asked at all.
// A saved setting's address is a PAIR — the report AND the person — so listing them takes both, and
// listing somebody else's is the ordinary case rather than the exotic one: the person with the
// problem is rarely the person holding the tool.
class ibMcpToolComposeSettings : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("compose_settings"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("listing saved settings of '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("EVERY WAY THIS REPORT GETS RUN, both sides of it, because a complaint names one or "
			"the other and the person cannot tell which:\n"
			"\n"
			"* `settings` - what somebody SAVED for themselves: id, what they called it, when it "
			"last changed, and `default: true` on the one that is APPLIED WITHOUT BEING CHOSEN. "
			"That flag is the first question of any 'my numbers look wrong' and the one they cannot "
			"answer: a default setting does not look like a setting from the inside, it looks like "
			"what the report does.\n"
			"\n"
			"* `variants` - what the AUTHOR declared, by index and by the wording a picker shows "
			"them under. 'Sales at cost do not add up' is almost never a broken report; it is the "
			"NAME OF A VARIANT, and the complaint means they are on the wrong one.\n"
			  "\n"
			"Whose: the session's own unless a `user` is named, and naming one is the ordinary case - "
			"the person whose numbers look wrong is rarely the person asking. Nothing saved is itself "
			"an answer: they run it as its author wrote it. Feed an id or an index to compose_run to "
			"reproduce exactly what they see.\n"
			  "\n"
			"NO APPLICATION IS NEEDED FOR THIS. What it reads is the configuration's variants and a "
			"person's own saved choices, which are the designer's business - only the FIGURES live "
			"in the application. So ask this first: it is free, and it says which of the two things "
			"to reproduce before anything is started.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(/*required*/ true), ArgUser() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectComposer* composerObject = object->ConvertToType<ibValueMetaObjectComposer>();
		if (composerObject == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a composer. metadata_get on the report lists the composer among "
					  "its children."), object->GetName());
			return false;
		}

		ibUserInfo owner;
		if (!UserNamed(params, ArgUser(), owner, refusal))
			return false;

		// ⚠ THE COMPOSER'S CATEGORY, AND ONLY IT. A dynamic list keeps its settings too, under
		// `ibSettingsCategory::List` and against the FORM that shows it, not against a composer
		// metaobject - so what is listed here is a report's, which is what this tool is handed. The
		// same schema read from a list has its own address, and finding it is a different question
		// than this one.
		const ibUserInfo* whose = owner.IsOk() ? &owner : nullptr;
		const std::vector<ibComposerSettingsEntry> saved = ibListComposerSettings(
			ibSettingsCategory::Composer, object->GetGuid(), whose);

		// ⭐⭐ WHICH ONE APPLIES WITHOUT BEING CHOSEN — the FIRST question of any "my numbers look
		// wrong", and the one the person cannot answer for themselves. A default setting is put on
		// automatically; from the inside it does not look like a setting at all, it looks like what
		// the report does. So somebody says "the report is broken" and means "I am running under
		// something I saved months ago and forgot", and nothing on their screen distinguishes the
		// two.
		const ibGuid preferred = ibGetDefaultComposerSettings(object->GetGuid(), whose);

		std::vector<ibDataValue> entries;
		entries.reserve(saved.size());
		for (const ibComposerSettingsEntry& entry : saved) {
			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			// The id is the ADDRESS - it is what compose_run takes. The name is what the person called
			// it, and two people may well use the same word for different things.
			node->SetValue(wxT("id"), wxString(entry.m_id));
			node->SetValue(wxT("name"), entry.m_name);
			if (entry.m_changed.IsValid())
				node->SetValue(wxT("changed"), entry.m_changed);
			if (preferred.isValid() && entry.m_id == preferred)
				node->SetValue(wxT("default"), true);   // …and this is the one running right now
			entries.push_back(ibDataValue::Child(node));
		}
		result.AddField(wxT("settings"), ibDataValue::Array(entries));

		// ⭐⭐ AND THE AUTHOR'S VARIANTS BESIDE THEM, because a person's words name one or the other and
		// they cannot tell which. "Sales at cost do not add up" is almost never a broken report - it is
		// the NAME OF A VARIANT, declared by whoever wrote the report, and the complaint means "I am on
		// the wrong one" or "this one is not what I expect". report_variant lists them too; they are
		// here because a diagnosis needs both sides in one look: what the report can be run AS, and
		// what this person saved for themselves. Two different ways to end up seeing something else.
		const ibCompositionDescription& description =
			composerObject != nullptr ? composerObject->GetCompositionDesc() : ibCompositionDescription();
		std::vector<ibDataValue> variants;
		variants.reserve(description.m_variants.size());
		for (size_t i = 0; i < description.m_variants.size(); ++i) {
			const ibVariantDescription& v = description.m_variants[i];
			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("index"), (s32)i);   // what compose_run takes
			node->SetValue(wxT("name"), v.m_name);
			// What a PICKER shows, when the author gave it different words from the name. That is the
			// wording a person will quote back, so it is the one to match their complaint against.
			if (!v.m_synonym.IsEmpty() && v.m_synonym != v.m_name)
				node->SetValue(wxT("title"), v.m_synonym);
			variants.push_back(ibDataValue::Child(node));
		}
		result.AddField(wxT("variants"), ibDataValue::Array(variants));

		// ⚠ NOTHING SAVED IS AN ANSWER, not an empty result to be puzzled over. It says the person runs
		// the report as the author wrote it, which is itself the answer to "why do we see different
		// numbers" more often than a setting is.
		if (entries.empty())
			result.SetValue(wxT("note"),
				ibMcpText("Nothing saved here - this report runs as its author wrote it, on the variant "
					  "chosen. compose_run with no settings reproduces exactly that."));
		else if (!preferred.isValid())
			result.SetValue(wxT("note"),
				ibMcpText("Saved settings exist but none is the default, so the report opens on the "
					  "author's variant and one of these is applied only when it is picked."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolComposeSettings);

} // namespace
