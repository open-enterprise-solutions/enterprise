////////////////////////////////////////////////////////////////////////////
//	Description : RUN A COMPOSITION SCHEMA - see composeRunSchema.h for why
//	              this runs on the application side and not in the designer,
//	              and for what crosses the wire to get here.
////////////////////////////////////////////////////////////////////////////

#include "backend/composition/composeRunSchema.h"

#include "backend/compositionDescription.h"
#include "backend/metadataConfiguration.h"
#include "backend/composition/dataComposer.h"                 // ibDataDBComposer - the composer this builds
#include "backend/composition/drivers/tableComposeDriver.h"   // ...and the two readers of its walk
#include "backend/appData.h"                                  // GetJobManager - who lends the thread
#include "backend/job/jobManager.h"                           // ...and the rented read itself
#include "backend/session/session.h"                          // the root module a parameter evaluates against
#include "backend/moduleManager/moduleManager.h"              // ...which is where its ProcUnit lives
#include "backend/compiler/procUnit.h"                        // ...and Evaluate itself
#include "backend/compiler/procUnitState.h"                   // GetCurrentRunContext - the caller's own frame
#include "backend/compiler/procContext.h"                     // ibRunContext - the frame it is given
#include "backend/system/value/valueType.h"                   // AdjustValue - the declared type wins
#include "backend/backend_exception.h"
#include "backend/stringUtils.h"                              // CompareString - how this tree compares a NAME

#include <algorithm>
#include <memory>

namespace {

// The refusals here are read by an ASSISTANT rather than shown in somebody's window, so they are not
// run through the translation macro - the same choice every MCP description makes, for the same
// reason: one wording, and it is the wording the caller was written against.
#define ibComposeText(text) wxString(wxT(text))

// ⭐⭐ A FIGURE TRAVELS AS MORE THAN ITS SPELLING. Everything in this family writes values as text,
// which is right when a person reads them and wrong here: this exists to COMPARE numbers, and a
// number rendered to a string has already lost its exactness and its type. So a value carries what
// it IS - the number, the date, the flag - and its presentation beside them, for reading.
//
// A reference carries neither: what identifies it is not text at all. Its presentation is what a
// person sees, so that is what travels, and anything needing the identity asks the schema for the
// column instead (docs/reference-key-metaid.md - the name carries the name, the id carries the
// object).
ibDataValue ValueEntry(const ibValue& value)
{
	std::shared_ptr<ibDataNode> out = std::make_shared<ibDataNode>();
	out->SetValue(wxT("text"), value.GetString());
	switch (value.GetType()) {
	case ibValueTypes::TYPE_NUMBER:  out->AddField(wxT("number"), ibDataValue::Number(value.GetNumber())); break;
	case ibValueTypes::TYPE_BOOLEAN: out->AddField(wxT("bool"),   ibDataValue::Bool(value.GetBoolean())); break;
	case ibValueTypes::TYPE_DATE:    out->SetValue(wxT("date"),   value.GetDateTime()); break;
	case ibValueTypes::TYPE_NULL:    out->SetValue(wxT("null"),   true); break;
	default: break;   // a reference, an enum, a composite - the presentation is what there is
	}
	return ibDataValue::Child(out);
}

// ...and back, for a parameter somebody states.
//
// 🛑⭐⭐ THIS IS AN IDENTITY, NOT A CONVERSION TABLE, and the difference is the whole design (Max,
// 2026-09-06: *"make it so they only have to be CONNECTED afterwards, not written for each case"*).
// What is here is the four kinds JSON HAS, each becoming the value it already is — a closed set that
// will not grow, because JSON will not gain a kind.
//
// ⛔ WHAT MUST NOT BE ADDED HERE is a case that CONVERTS: string-to-date, number-to-date,
// bool-to-string. That is the table which grows without end, and every pair somebody has not written
// yet fails by producing an empty value — silently, and looking exactly like an answer. One such
// branch existed for an hour and made a report say 0 at every date.
//
// A value that has to survive as a PARTICULAR type does not come this way at all: it comes PACKED,
// and `ibStoredValue` unpacks it. That is the platform's own serialiser, it already knows every
// value there is, and a new type reaches it by being connected there rather than by being written
// again here.
ibValue ValueFrom(const ibDataValue& given)
{
	switch (given.Kind()) {
	case ibDataKind::String: return ibValue(given.AsString());
	case ibDataKind::Number: return ibValue(given.AsNumber());
	case ibDataKind::Bool:   return ibValue(given.AsBool());
	case ibDataKind::Date:   return ibValue(wxDateTime(static_cast<wxLongLong>(given.AsDate())));
	default: return ibValue();
	}
}

// ⭐⭐ EVALUATE AGAINST THE SESSION'S ROOT MODULE — the one thing a caller cannot do for itself
// before sending, and the reason a computed parameter has to be settled HERE.
//
// A caller can state any literal it knows: a number, a date, a flag, a string. What it cannot state
// is anything that has to be WORKED OUT where the data is — today's date as this session reckons it,
// a rate looked up in a register, a catalogue item found by its code. Those are not values a sender
// has; they are values a runtime produces. So the third road exists for exactly them, and in a
// designer it would have nothing to evaluate against.
//
// NO FRAME OF OUR OWN — the root module's is borrowed. A run context is a frame descriptor: it
// carries the ProcUnit whose bytecode the expression is compiled against, which is what "attached to
// the root" means.
//
// 🛑 A SECOND COPY of ibEvaluateInRoot (valueDataComposition.cpp), which is file-local there. Marked
// rather than hidden: the two are obliged to agree, and converging them means giving that helper a
// home of its own — a change to the composition value's file, not made from here.
bool EvaluateInRoot(const wxString& expression, ibValue& produced)
{
	produced = ibValue();
	if (expression.IsEmpty())
		return true;   // nothing to evaluate is not a failure

	// Inside running code the caller's own frame is the truthful one — the same rule the built-in
	// Evaluate follows.
	if (ibProcUnitState* const state = ibSession::GetPUState()) {
		if (ibRunContext* const current = state->GetCurrentRunContext())
			return ibProcUnit::Evaluate(expression, current, produced, false);
	}

	ibSession* const session = ibSession::Current();
	ibValueModuleManagerRuntimeConfiguration* const root =
		session != nullptr ? session->GetManagerModule() : nullptr;
	std::shared_ptr<ibProcUnit> rootUnit = root != nullptr ? root->GetProcUnit() : nullptr;

	// ⚠ NO RUNTIME, NO PRETENDING. Answering "true, produced nothing" here is indistinguishable from
	// an expression that legitimately evaluated to empty — which is how a computed parameter can be
	// silently ignored and the report merely look wrong.
	if (!rootUnit) {
		produced = ibValue(ibComposeText("there is no runtime in this process to evaluate against"));
		return false;
	}

	ibRunContext rootFrame;
	rootFrame.SetProcUnit(rootUnit.get());
	return ibProcUnit::Evaluate(expression, &rootFrame, produced, false);
}

// --- what a driver accumulated, as a node --------------------------------------------------------

ibDataValue NodeEntry(const ibComposedNode& node)
{
	std::shared_ptr<ibDataNode> out = std::make_shared<ibDataNode>();
	out->SetValue(wxT("level"), (s32)node.m_level);
	if (node.m_indent != 0)
		out->SetValue(wxT("indent"), (s32)node.m_indent);
	out->SetValue(wxT("heading"), node.m_isHeading);
	// The fold's fact and the output's promise - two questions, and a reader checking whether a
	// report adds up wants the first; one deciding whether to offer an expander wants the second.
	if (node.m_hasChildren)
		out->SetValue(wxT("hasChildren"), node.m_hasChildren);
	if (node.m_showsWhatIsUnder)
		out->SetValue(wxT("showsWhatIsUnder"), node.m_showsWhatIsUnder);

	std::vector<ibDataValue> values;
	values.reserve(node.m_values.size());
	for (const ibValue& v : node.m_values)
		values.push_back(ValueEntry(v));
	out->AddField(wxT("values"), ibDataValue::Array(values));

	// ⭐⭐ WHERE IT STOOD. Without this a figure is a number nobody can place: the walk emits one node
	// at a time and carries the position in a cursor of its own, so the address has to travel WITH
	// the figure or it is gone. In a cross-table it is half the answer - a cell is the row's
	// headings AND the column's value.
	if (!node.m_path.empty()) {
		std::vector<ibDataValue> path;
		path.reserve(node.m_path.size());
		for (const std::vector<ibValue>& step : node.m_path) {
			std::vector<ibDataValue> stepValues;
			stepValues.reserve(step.size());
			for (const ibValue& v : step)
				stepValues.push_back(ValueEntry(v));
			path.push_back(ibDataValue::Array(stepValues));
		}
		out->AddField(wxT("path"), ibDataValue::Array(path));
	}
	return ibDataValue::Child(out);
}

ibDataValue ColumnEntry(const ibComposedColumn& column)
{
	std::shared_ptr<ibDataNode> out = std::make_shared<ibDataNode>();
	// Three different things, and not interchangeable: the title is what a person reads, the name is
	// what a script says, the id is what a value is keyed by - and an aggregate or a dot-walk has no
	// source column at all, so it answers with its alias and no id.
	out->SetValue(wxT("title"), column.m_title);
	if (!column.m_name.IsEmpty() && column.m_name != column.m_title)
		out->SetValue(wxT("name"), column.m_name);
	if (!column.m_alias.IsEmpty())
		out->SetValue(wxT("alias"), column.m_alias);
	if (column.m_hasId)
		out->SetValue(wxT("id"), (s32)column.m_id);
	return ibDataValue::Child(out);
}

void WriteColumns(ibDataNode& into, const ibComposedDriverBase& driver)
{
	std::vector<ibDataValue> columns;
	columns.reserve(driver.Columns().size());
	for (const ibComposedColumn& c : driver.Columns())
		columns.push_back(ColumnEntry(c));
	into.AddField(wxT("columns"), ibDataValue::Array(columns));
}

void WriteNodes(ibDataNode& into, const wxString& field, const std::vector<ibComposedNode>& nodes)
{
	std::vector<ibDataValue> out;
	out.reserve(nodes.size());
	for (const ibComposedNode& n : nodes)
		out.push_back(NodeEntry(n));
	into.AddField(field, ibDataValue::Array(out));
}

}   // namespace

bool ibComposeRunSchema::Run(const ibDataNode& request, ibDataNode& result, wxString& refusal)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = ibComposeText("This application has no configuration open, so there is nothing to "
			"compose against.");
		return false;
	}

	// ⭐ THE SCHEMA ARRIVED; NOTHING IS LOOKED UP. What the designer resolved - which composer, which
	// variant, whose saved setting - was resolved where the configuration and the settings live. This
	// end is handed the answer, which is why there is no metaobject and no settings table in sight.
	//
	// `activeMetaData` is still needed, and only for the setting's own VALUES: a filter's right-hand
	// side may be a reference or an enum member, and those live in a configuration's registry rather
	// than in the node.
	const ibDataNode* schemaNode = request.FindChild(kComposeSchema);
	if (schemaNode == nullptr) {
		refusal = ibComposeText("No schema came with the request. Nothing was composed.");
		return false;
	}

	ibCompositionDescription description;
	if (!ibCompositionDescriptionMemory::ReadNode(*schemaNode, description, activeMetaData)) {
		refusal = ibComposeText("The schema that arrived could not be read as a composition.");
		return false;
	}

	// ⚠ A COMPOSER WITH NO SOURCE ANSWERS NOTHING, and saying so beats running an empty walk and
	// reporting no rows: those read identically and mean opposite things.
	if (!description.IsOk()) {
		refusal = ibComposeText("The schema has no source to read - neither a query nor a main table.");
		return false;
	}

	ibDataDBComposer composer;
	composer.SetMetaData(activeMetaData);
	if (description.HasQuery())
		composer.FromText(description.m_query);

	// ⚠ THE ORDER IS NOT ARBITRARY, and the authority for it is the runtime's own setup: resources
	// and selects first, then the settings, and the STRUCTURE of the outputs LAST - the settings
	// rebuild the grouping ladder from a flat list of lines, and a flat list cannot say "one level of
	// two fields", nor "a second output", nor "an axis of columns".
	composer.ClearResources();
	for (const ibResourceDescription& resource : description.m_resources)
		composer.Resource(resource.m_func, resource.m_path);
	composer.Selects()        = description.m_selects;
	composer.CommonSelected() = description.m_selected;
	composer.LoadVariants(description.m_variants);

	// ⭐ THE SETTINGS SECTION AS IT WAS CHOSEN. The designer folded the author's variant and whoever's
	// saved setting into one section before sending it, so there is one thing to apply and no order
	// to get wrong on this side.
	if (const ibDataNode* settingsNode = request.FindChild(kComposeSettings)) {
		ibSettingsDescription settings;
		if (!ibSettingsDescriptionMemory::ReadNode(*settingsNode, settings, activeMetaData)) {
			refusal = ibComposeText("The settings that arrived could not be read.");
			return false;
		}
		composer.SetUserSettingsDesc(settings);
	}

	// ⭐⭐ THE PARAMETERS ARE SETTLED HERE, AND THAT IS WHY THIS RUNS WHERE IT DOES. A parameter is one
	// of three things and only the last one needs anything: a value the caller stated, a value the
	// schema stored, or an EXPRESSION — and an expression needs a runtime to evaluate against. In a
	// designer there is none, which is exactly the case that used to answer "true, produced nothing"
	// and look like a parameter that legitimately evaluated to empty.
	//
	// ⭐ AND A CALLER MAY SEND AN EXPRESSION OF ITS OWN (Max, 2026-09-06: *"it should still be able to
	// construct computed parameters — you want the current date in a query, you just make a computed
	// parameter"*). So a field under `parameters` is read two ways: a scalar is a VALUE, and a child
	// carrying `expression` is computed here, where `CurrentDate()` means something.
	//
	// 🛑 THIS IS A SECOND ROAD, marked rather than hidden: ibValueDataComposition::
	// EvaluatedParameterValues folds parameters the same way for the screen's own compose. They are
	// obliged to agree and nothing enforces it. Converging them is a change to the composition
	// value's own setup and is not made from here.
	{
		const ibDataNode* given = request.FindChild(kComposeParameters);

		for (const ibParameterDescription& parameter : description.m_parameters) {
			if (parameter.m_name.IsEmpty())
				continue;

			// WHAT THE CALLER SAID ABOUT THIS ONE, asked BY NAME rather than section-wide: somebody
			// who stated a period said nothing about any other parameter.
			const ibDataValue* stated     = given != nullptr ? given->FindField(parameter.m_name) : nullptr;
			const ibDataNode*  statedNode = given != nullptr ? given->FindChild(parameter.m_name) : nullptr;

			wxString expression = parameter.m_expression;
			ibValue  value;
			bool     haveValue = false;

			if (statedNode != nullptr) {
				const ibDataValue* written = statedNode->FindField(wxT("expression"));
				if (written != nullptr && written->Kind() == ibDataKind::String) {
					expression = written->AsString();   // …the caller's, over whatever was declared
				}
				else {
					// ⭐⭐ A PACKED VALUE, THROUGH THE PLATFORM'S OWN DOOR. `ibStoredValue` is one of the
					// TWO places a store becomes a runtime value — the header says so, and says the
					// other direction needs nothing because *a value packs itself*. So a caller that
					// read a schema saw its parameters in exactly this form and can hand one straight
					// back, which is what makes a REFERENCE or an enum member expressible at all: those
					// are built by the metadata, and no scalar on a wire can carry one.
					value     = ibStoredValue(*statedNode, activeMetaData);
					haveValue = true;
					expression.Clear();
				}
			}
			else if (stated != nullptr) {
				value     = ValueFrom(*stated);
				haveValue = true;
				// ⚠⚠ AND THIS IS A DELIBERATE DIVERGENCE FROM THE ENGINE, named rather than left to be
				// discovered. In ibValueDataComposition::EvaluatedParameterValues an EXPRESSION always
				// wins: a parameter that declares one is computed and its stored value is ignored. A
				// caller here is not storing a value, it is OVERRIDING for this one run — and the whole
				// point of stating a period is to vary it. If the declared expression won, no report
				// whose period is computed could be re-run for a different one, which is most of them.
				expression.Clear();
			}

			// …then whatever the SETTINGS section carried for this name — a saved setting usually
			// brings a period of its own, and it stands above the schema's stored value and below the
			// caller's. Asked BY NAME: somebody who saved a period said nothing about anything else.
			if (!haveValue && expression.IsEmpty()) {
				const ibDataNode* stored = &parameter.m_value;
				for (const ibParameterDescription& theirs : composer.GetUserParameters())
					// ⚠ stringUtils::CompareString, which is how this tree compares a NAME — folding case
					// is not the same operation in every alphabet, and a configuration's identifiers are
					// written in the person's own. (valueDataComposition.cpp does this comparison with
					// wxString::IsSameAs; the two can disagree on a Cyrillic name, and that file is the
					// one to bring across, not this one to match.)
					if (stringUtils::CompareString(theirs.m_name, parameter.m_name)) {
						stored = &theirs.m_value;
						break;
					}

				// …and a packed node becomes a live value only NOW, with every metaobject in place —
				// the only moment a reference can be built truthfully.
				value     = ibStoredValue(*stored, activeMetaData);
				haveValue = true;
			}

			// ⭐⭐ A BROKEN EXPRESSION STOPS THE READ AND SAYS SO. Producing nothing and carrying on
			// makes a report composed on an unfilled parameter look exactly like one with no data.
			if (!expression.IsEmpty()) {
				ibValue produced;
				if (!EvaluateInRoot(expression, produced)) {
					refusal = wxString::Format(
						ibComposeText("Parameter '%s' could not be evaluated: %s"),
						parameter.m_name, produced.GetString());
					return false;
				}
				value = produced;
			}

			// 🛑⭐⭐ THE DECLARED TYPE WINS, AND IT HAS TO APPLY TO ALL THREE ROADS. This adjustment
			// used to sit inside the expression branch only — so a COMPUTED period was made into a
			// date and a STATED one was not, and JSON has no date type: `"2026-12-31"` arrives as a
			// STRING. A string handed to a Date parameter does not become the boundary of a Balance
			// virtual table; it is simply not applied, and the report answers with everything.
			//
			// ⚠ MEASURED, BECAUSE IT LOOKS RIGHT: the balance at 2026-12-31 and at 2001-01-01 came
			// back identical, in a base whose documents are all of 2026 (2026-09-06). Nothing failed,
			// nothing was refused, and every figure was wrong in the one way that cannot be seen —
			// the report was answering a question nobody asked.
			//
			// ⭐ WITH NO DECLARED TYPE the produced value decides: a parameter has no type of its own
			// until somebody gives it one, and forcing one onto nothing would invent it.
			{
				ibTypeDescription target = parameter.m_type;
				if (target.GetClsidCount() == 0 && !value.IsEmpty())
					target.SetDefaultMetaType(value.GetClassType());

				// 🛑⭐⭐ ASKED BEFORE THE CONVERSION, AND ABOUT THE TYPE — not after it, and not about
				// emptiness. A first version refused a value that came out EMPTY, reasoning that a
				// conversion which produced nothing had failed. It is not so, and the counter-example
				// is ordinary: FALSE is an empty Boolean, and so is 0 a number (Max, 2026-09-06:
				// *"sometimes empty is legitimate — False is empty for a boolean, and you would see it
				// as an error"*). Emptiness cannot tell a failed conversion from a legitimate value,
				// so nothing downstream of the conversion can.
				//
				// ⚠ WHAT CAN BE ASKED is whether the value the caller sent IS one of the types the
				// parameter declares. A string against a Date is not, and no amount of converting will
				// honestly make it one — that is the road that ends in a table of every pair.
				if (target.GetClsidCount() > 0 && haveValue && !value.IsEmpty() &&
				    !target.ContainType(value.GetClassType())) {
					refusal = wxString::Format(
						ibComposeText("Parameter '%s' was given a value of a type it does not declare, so "
							      "it cannot be applied as it stands - and applying it converted "
							      "would be guessing. Send it in its PACKED form instead (the shape "
							      "report_get shows under `Value`), which carries its own type, or "
							      "as an expression evaluated where the data is."),
						parameter.m_name);
					return false;
				}

				if (target.GetClsidCount() > 0)
					value = ibValueTypeDescription::AdjustValue(target, value, activeMetaData);
			}

			composer.Parameter(parameter.m_name, value);
		}

		// ⚠ AND A NAME THE SCHEMA DOES NOT DECLARE IS REFUSED, not swallowed. Setting one changes
		// nothing and says nothing, so `StartDate` on a report that asks for `From` runs with the
		// period unset — and the engine's complaint then names `&From`, a name the caller never used,
		// which reads as a defect in the report rather than a typo in the call.
		if (given != nullptr) {
			for (const std::pair<wxString, ibDataValue>& field : given->Fields()) {
				const bool declared = std::any_of(
					description.m_parameters.begin(), description.m_parameters.end(),
					// ⚠ CompareString, like every other name lookup here. `==` on wxString is
					// case-SENSITIVE, so `period` against a declared `Period` would have been refused
					// as an unknown name — a refusal naming the very parameter that is right there in
					// the list it prints.
					[&field](const ibParameterDescription& p) {
						return stringUtils::CompareString(p.m_name, field.first);
					});
				if (!declared) {
					wxString names;
					for (const ibParameterDescription& p : description.m_parameters)
						names += (names.IsEmpty() ? wxString() : wxT(", ")) + p.m_name;
					refusal = names.IsEmpty()
						? wxString::Format(
							ibComposeText("This schema asks for no parameters, so '%s' sets nothing."),
							field.first)
						: wxString::Format(
							ibComposeText("This schema has no parameter '%s'. It asks for: %s."),
							field.first, names);
					return false;
				}
			}
		}
	}

	// ⭐⭐ AND THE STRUCTURE INTO THE LIVE OUTPUTS, which is the step that makes a report a report. A
	// composer is born with ONE output and no levels; the groupings live in whichever setting is now
	// current, and until they are carried across, the walk has nothing to fold by. Missing it does
	// not fail - it ANSWERS, with the grand total and nothing under it, which reads as a report whose
	// groupings produced no rows rather than as a setup that never arrived.
	//
	// ⚠ ASKED OF THE COMPOSER, NOT OF THE DESCRIPTION. `GetCurrentStructure` answers with the setting
	// in force, so the outputs and the settings cannot come from two different places.
	//
	// 🛑 THIS IS A SECOND ROAD, on purpose for now: valueDataComposition.cpp does the same six lines
	// before its own compose. They are obliged to agree and nothing enforces it. Converging them is a
	// change to the composition value's own setup and is not made from here.
	{
		const std::vector<ibOutputDescription>& stored = composer.GetCurrentStructure();
		if (!stored.empty()) {
			std::vector<ibDataComposer::Output>& live = composer.Outputs();
			live.resize(stored.size());
			for (size_t i = 0; i < stored.size(); ++i)
				static_cast<ibOutputDescription&>(live[i]) = stored[i];
		}
	}

	// ⭐⭐ A DRIVER PER OUTPUT, AND THE RIGHT ONE FOR ITS SHAPE. A grouping's columns are the schema's;
	// a cross-table's are made by its column dimension's VALUES and are not known until they arrive.
	// The composer's own `Output::m_driver` is per output precisely so the two can differ. It comes
	// AFTER the structure: resizing the outputs would drop the pointers.
	std::vector<std::unique_ptr<ibComposedDriverBase>> drivers;
	std::vector<ibDataComposer::Output>& outputs = composer.Outputs();
	drivers.reserve(outputs.size());
	for (ibDataComposer::Output& output : outputs) {
		// The composer states it; nothing here infers it from the shape of what arrives.
		const bool cross = output.Kind() == ibCompositionOutputKind::Table;
		drivers.push_back(cross
			? std::unique_ptr<ibComposedDriverBase>(new ibCrossComposeDriver())
			: std::unique_ptr<ibComposedDriverBase>(new ibGroupingComposeDriver()));
		output.m_driver = drivers.back().get();
	}

	// ⭐⭐ READ IT ON A RENTED SESSION, so the person at this application keeps working. A report can
	// read for a long time and they did not ask for it - so what is borrowed is a thread and a
	// connection of its own, and their window stays alive throughout. Same road
	// ibValueDataComposition::SubmitFetchAsync takes for the screen's own compose.
	//
	// ⭐⭐ WHAT IS RENTED IS A CONNECTION PLUS A POLICY, not a place in anybody's session. The session
	// a tenant is minted on is UNLISTED: it takes no row in the registry, answers no lookup and
	// installs no identity of its own. That is what lets it read ALONGSIDE them rather than queueing
	// behind them - and here, unlike in a designer, the policy it borrows is the policy of the very
	// person whose report this is, so the rows are the rows they would see.
	//
	// ⚠ AND A REFUSAL TO RENT MEANS "READ IT HERE", never "no data". The wait for a free worker is
	// BOUNDED (five seconds) exactly so a caller gets an answer it can act on rather than patience it
	// did not ask for - so this catches, and reads on this thread instead.
	bool     composed = false;
	wxString failed;
	auto     work = [&composer, &composed]() { composed = composer.Run(); };

	std::shared_ptr<ibBackgroundRun> rented;
	if (ibJobManager* const jobs = ibApplicationData::GetJobManager()) {
		try {
			rented = jobs->StartBackground(
				[work](ibSession*) -> ibValue { work(); return ibValue(); },
				ibComposeText("reading a report for an assistant"),
				ibJobTenancy::Tenant);
		}
		catch (const ibBackendException&) {
			rented.reset();   // nothing to rent - fall through and read here
		}
	}
	if (rented) {
		rented->Wait();      // 0 = until it finishes; the waiting is off the window's thread
		failed = rented->Error();
	}
	else {
		// ⚠ CAUGHT HERE TOO, so the two paths answer alike. A rented run turns a throw into `Error()`;
		// reading on this thread would let the same failure travel as an exception, and then "the
		// period is not set" would reach the caller two different ways depending on whether a session
		// happened to be free.
		try                                      { work(); }
		catch (const ibBackendException& thrown) { failed = thrown.GetErrorDescription(); }
	}

	// The pointers do not outlive this call - the drivers are locals, and an output holding a
	// dangling one would be read on the next compose.
	for (ibDataComposer::Output& output : outputs)
		output.m_driver = nullptr;

	// ⭐ THE ENGINE'S OWN WORDS WHEN IT HAS ANY. "did not compose" says a report failed; "parameter
	// '&Period' is not set" says WHICH INPUT is missing, and only one of those can be acted on.
	if (!failed.IsEmpty()) {
		refusal = failed;
		return false;
	}
	if (!composed) {
		refusal = ibComposeText("It did not compose. The schema is there; running it failed.");
		return false;
	}

	std::vector<ibDataValue> tables;
	tables.reserve(drivers.size());
	for (const std::unique_ptr<ibComposedDriverBase>& driver : drivers) {
		std::shared_ptr<ibDataNode> table = std::make_shared<ibDataNode>();
		if (!driver->Name().IsEmpty())
			table->SetValue(wxT("name"), driver->Name());
		if (driver->Totals())
			table->SetValue(wxT("totals"), true);
		WriteColumns(*table, *driver);

		if (const ibCrossComposeDriver* cross = dynamic_cast<const ibCrossComposeDriver*>(driver.get())) {
			table->SetValue(wxT("shape"), wxString(wxT("cross")));
			WriteNodes(*table, wxT("rows"), cross->Rows());
			// ⭐ The axis, in ARRIVAL order - that order IS the axis. The root heading's cells come
			// first, which is why a printed cross-table carries its total column leftmost.
			WriteNodes(*table, wxT("axis"), cross->Axis());
		}
		else if (const ibGroupingComposeDriver* grouping =
				dynamic_cast<const ibGroupingComposeDriver*>(driver.get())) {
			table->SetValue(wxT("shape"), wxString(wxT("grouping")));
			WriteNodes(*table, wxT("rows"), grouping->Lines());
		}
		tables.push_back(ibDataValue::Child(table));
	}
	result.AddField(wxT("outputs"), ibDataValue::Array(tables));

	if (tables.empty())
		result.SetValue(wxT("note"),
			ibComposeText("The composer declares no output - nothing was produced. report_output is "
				      "where one is declared."));
	return true;
}
