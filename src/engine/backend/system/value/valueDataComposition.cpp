#include "backend/system/value/valueDataComposition.h"
#include "backend/compositionDescription.h"   // WHAT a saved composition consists of — the one file
#include "backend/appData.h"                         // appData / GetActiveMetaData
#include "backend/query/queryable.h"                 // ibBackendQueryable
#include "backend/query/queryableFactory.h"          // ibQueryableSourceDescriptor (source holder + its command surface)
#include "backend/query/queryColumn.h"               // ibBackendQueryColumn::GetColumnId
#include "backend/query/queryParser.h"               // the query is READ by the engine's own parser
#include "backend/query/queryLexer.h"                // ibQueryLexer::ParamNames — the parameters the TEXT asks for
#include "backend/system/systemManager.h"            // system functions
#include "backend/compiler/procUnit.h"                // ibProcUnit::Evaluate — a parameter expression is run here
#include "backend/compiler/procContext.h"             // ibRunContext — the frame an expression is evaluated in
#include "backend/moduleManager/moduleManager.h"       // the session root: common modules + the environment
#include "backend/session/session.h"                   // ibSession::Current / GetPUState — where the root lives
#include "backend/system/value/valueType.h"           // ibValueTypeDescription::AdjustValue — the result decides the type
#include "backend/query/queryConstructorModel.h"      // ibQueryConstructorModel — the fields a window offers
#include "backend/query/queryRender.h"               // ibRenderQuery / ibQueryColumnFromPath — the seed query, written out
#include "backend/backend_exception.h"               // the engine's verdict on a query that will not resolve
#include "backend/srcDataObject.h"                   // ibSourceExplorer
#include "backend/serialize/dataBuilder.h"           // ibDataNode (object-level save/load)
#include "backend/metadataConfiguration.h"           // ibMetaDataConfigurationBase (GetSourceMetaData)
#include "backend/composition/drivers/spreadsheetComposeDriver.h"   // the OUTPUT — composition → document
#include "backend/system/value/composition/valueComposerSettings.h"   // ibValueEnumComparisonKind — a condition's word
#include "backend/system/value/composition/valueComposerField.h"      // a declared value becomes runtime here
#include "backend/system/value/valueSpreadsheet.h"   // ibValueSpreadsheetDocument — the script-side document
#include "backend/value_cast.h"                      // CastValue — the script argument to its type
#include "backend/job/jobManager.h"                  // ibJobManager / ibBackgroundRun — the rented read
#include "backend/settings/settingsComposer.h"       // the reader's saved settings — and the one marked for open

namespace {

// Columns over the QUERY's output schema. A query's column is an ibBackendQueryColumn exactly as a
// queryable's is (the lowering hands one back for every output, synthesising it where the column is
// computed) — the same idea of a column, reached a different way.
// (GONE with the list surface — see the note below.)

// The same collection straight off the SOURCE — what the composer offers between picking a source and
// the query resolving (a query being edited, or one the engine refused).
// ⏳ THE DATAVIEW SURFACE IS GONE, and it was never a composition's. Two COLUMN COLLECTIONS (one over
// the query's output schema, one straight off the source) and a script RETURN LINE lived here so a
// composition could be shown in a table — copied, with the cursor model, from the dynamic list.
//
// A composition is not read row by row into a grid: it is read ONCE and written into a SHEET. What
// the query produces is kept as the output SCHEMA (m_querySchema) and laid out by the compose driver,
// which places it BY ROLE — dimensions down one column, measures each in their own (Max, 2026-08-20:
// "a spreadsheet document has none of that — no dataview, no nodes").
} // namespace

// ===========================================================================
//  ibValueDataComposition
// ===========================================================================

ibValueDataComposition::ibValueDataComposition(const ibBackendQueryable* queryable)
	: ibValueSpreadsheetModel(), ibSourceDataObject()
{
	// ⚠ THE SOURCE'S OWN CONFIGURATION FIRST, the ACTIVE one only if there is no source to ask. A
	// queryable lives in the configuration it was created in — the edited one in the designer, the
	// copy's own on a copy — and resolving against the active configuration there would answer with
	// somebody else's tables.
	//
	// ⚠⚠ A COMPOSITION BELONGING TO A CONFIGURATION MUST NEVER FALL BACK TO THE ACTIVE ONE — it would
	// see THE WRONG TABLES. Two configurations are open in the designer at once (the edited one and
	// the one being compared or copied from), and the active one is not necessarily this object's; a
	// name resolved there answers with somebody else's metadata and everything downstream is quietly
	// about the wrong report.
	//
	// The line below is INSURANCE, not the road: it catches the composition CREATED IN CODE
	// (`New DataComposition()` in a script, one a form builds), which has nowhere to take a
	// configuration from — no source was handed to it and it has no owner to ask — and where exactly
	// one configuration is running anyway. Anything that HAS an owner or a source is expected to have
	// been answered above.
	if (queryable != nullptr) {
		if (const ibValueMetaObjectGenericData* mo = queryable->GetSourceMetaObject())
			m_sourceMetaData = mo->GetMetaData();
	}
	if (m_sourceMetaData == nullptr)
		m_sourceMetaData = activeMetaData;

	// The sheet exists from the start — a control bound to a composition that has never been run
	// still has something to show, and it is the object it will stay subscribed to.
	// The script surface — Refresh / Compose. It used to arrive with the model base that is gone;
	// bound here, or CallAsProc dispatches on indices nothing ever registered.
	//
	// ⚠ FILTER / ORDER / GROUP / SETTINGS ARE NOT THERE ANY MORE. They handed out the runtime wrapper
	// that went under the knife on 2026-08-23; what a composition IS lives in its description, and a
	// script surface over it comes back once that structure has settled.
	m_members.Bind(this, &ibValueDataComposition::FillMembers);

	// THERE IS ALWAYS A VARIANT, from the first moment — a composition built from script or
	// generated by a report form has one too, so no window has to create it.
	EnsureVariant();
	if (queryable != nullptr)
		SetSourceQueryable(queryable);   // null → set later via SetSource
}

// THE RUNNING COMPOSITION OVER A DESCRIPTION SOMEBODY ELSE HOLDS — the two things it needs: the data,
// and the configuration to resolve names in.
//
// ⚠ NULL MEANS THE ACTIVE CONFIGURATION, decided here. A caller that has one passes it (a property
// passes its owner's — the edited configuration in the designer, the copy's own on a copy); a caller
// that has none passes null and says so. Leaving each of them to reach for the active one itself is
// the same decision written in as many places as there are callers.
ibValueDataComposition::ibValueDataComposition(ibCompositionDescription& desc, const ibMetaData* metaData)
	: ibValueDataComposition()
{
	m_sourceMetaData = metaData != nullptr ? metaData : activeMetaData;
	SetCompositionDesc(desc);
}

// 🛑 THE READ IS WAITED OUT BEFORE THE COMPOSITION GOES. The run holds this object's settings, its
// parameters and the sheet it is filling; letting it outlive the composition is a use-after-free with
// a delay on it. Cancel is cooperative and the walk polls per row, so this returns at the next one.
ibValueDataComposition::~ibValueDataComposition() { CancelFetch(); }

// --- source & query ---------------------------------------------------------

void ibValueDataComposition::SetSource(const wxString& ns, const wxString& name)
{
	const ibBackendQueryable* before = GetSourceQueryable();
	m_propertySource->SetSource(ns, name);   // resolves the queryable INTO the property variant
	if (GetSourceQueryable() != before) {    // only on a REAL change — re-picking the SAME source keeps the settings
		RebuildSource();
		OnChildChanged();
	}
}

void ibValueDataComposition::SetSourceQueryable(const ibBackendQueryable* queryable)
{
	if (queryable == GetSourceQueryable())   // same source — leave columns / composer / settings untouched
		return;
	m_propertySource->SetQueryable(queryable);   // the queryable lives in the property, not here
	RebuildSource();
	OnChildChanged();
}

void ibValueDataComposition::SetQueryText(const wxString& text)
{
	if (text == GetQueryText())
		return;
	GetCompositionDesc().m_query = text;

	// ⚠ NO SIGNAL HERE, deliberately — the CALLER raises it. This runs on every keystroke, and what
	// hears the signal is not always cheap: a composition held by a form attribute re-renders the
	// whole form editor when it does (control tree, object tree, attribute tree, drop targets), so a
	// per-character signal made typing in a query redraw a form per character. The caller already
	// coalesces the work onto the pause after typing, which is where saying "it changed" belongs
	// too — and to a person "when I stop typing for an instant" is still immediate.

	// ⭐⭐ AND IT DOES NOT RE-READ THE SOURCE. Storing what the text IS and working out what it MEANS
	// are two acts, and the second one is expensive — it re-describes the query, re-syncs the
	// parameters and prunes settings that no longer resolve. They were one call because the only
	// caller pressed a button; now the editor stores on every keystroke and asks for the meaning
	// when the typing stops, so the two moments are no longer the same moment.
	//
	// The verb for the second act already exists and is public: ApplySource(). Nothing new was
	// added here — one call was moved out to the caller that always made it anyway.
}

// RUNNING IS ASKING — see the header. Nothing to do when the composer already stands on this very
// text; a rebuild otherwise, which is the same act the settings window performs when it applies the
// query it just edited.
void ibValueDataComposition::EnsureSourceBuilt()
{
	if (m_sourceBuiltFor == GetQueryText() && GetModelComposer().HasSource())
		return;
	RebuildSource();
}

void ibValueDataComposition::RebuildSource()
{
	// WHAT THIS BUILD IS FOR — recorded before anything else, so an EnsureSourceBuilt after it knows
	// there is nothing left to do. (A seed below may replace an empty text; it re-stamps it there.)
	m_sourceBuiltFor = GetQueryText();

	// STORE the source config into the metadata VARIABLE, taken from the VALUE — the picked queryable's
	// metaobject knows its config (the edited one in the designer, the copy's on a copy). Held so
	// GetSourceMetaData stays TERMINAL: it never re-resolves the queryable through the attach owner,
	// which walks the form and would come back here.
	const ibBackendQueryable* queryable = GetSourceQueryable();
	if (queryable != nullptr) {
		if (const ibValueMetaObjectGenericData* mo = queryable->GetSourceMetaObject())
			m_sourceMetaData = mo->GetMetaData();
	}

	// Thread the stored config into the composer so the query text resolves its by-name FROM against
	// THIS config rather than the global factory.
	GetModelComposer().SetMetaData(GetSourceMetaData());

	// ⭐⭐ THE VARIANTS ARE PUT ON THE COMPOSER — the whole array, as a copy. Rebuilding the source
	// builds the composer from scratch, so what lived only in the description would silently vanish
	// with it.
	//
	// ⭐ AND `[0]` IS WHY A REPORT IS NEVER EMPTY. Max, 2026-08-24: *"a report may have no user
	// settings — the zeroth variant is used so the report is not blank, so a person can just open it
	// and run what was set up."* It is not "the author's settings" as a thing of its own; it is the
	// element that composes while nobody has saved a setting of their own.
	//
	// 🛑 NOT THE READER'S SLOT — putting it there stood for a day and did real damage: the reader
	// accepted the settings window, this line ran on the next rebuild, and their choice was
	// overwritten. They saw the old settings come back on reopening.
	GetModelComposer().LoadVariants(GetCompositionDesc().m_variants);

	// SEED ON FIRST SIGHT OF A SOURCE. A composer declares what to read; a source with no query text
	// reads nothing, and an empty text is not a query. So the moment a source exists and the text does
	// not, the source writes the query it would write for itself — something that already runs and can
	// be opened in the query constructor as it stands.
	if (queryable != nullptr && GetQueryText().IsEmpty()) {
		const wxString seed = SeedQuery();
		if (!seed.IsEmpty()) {
			GetCompositionDesc().m_query = seed;
			m_sourceBuiltFor = seed;   // what this build is for, now that the text is the seed
		}
	}

	// (⛔ THE DEFAULT IS NOT PUT ON HERE. It stood here for an hour and it was the wrong storey: a
	//  rebuild happens whenever the source or the query text changes, so it needed a "only once"
	//  flag to be safe — and a flag guarding a call is the shape of a call in the wrong place. The
	//  moment that happens ONCE is when a CONTROL is handed its model, which is also where the
	//  address comes from for a list. See ibValueGridBox / ibValueModelTableBox OnCreated.)

	m_querySchema.clear();
	m_queryError.clear();

	const wxString queryText = GetQueryText();

	// ⭐ THE PARAMETERS ARE READ FROM THE TEXT FIRST — before anything tries to describe the query.
	// A `&Ref` in the text IS a parameter of this composition, whether or not anybody filled it in,
	// so the page shows it the moment the text mentions it (and it cannot be removed there: the text
	// is what put it in).
	SyncParametersWithQuery();

	if (!queryText.IsEmpty()) {
		GetModelComposer().FromText(queryText);

		// The names resolve against the SOURCE's config, the same one the composer was just handed.
		const ibSourceMetaDataScope scope(GetSourceMetaData());
		try {
			ibQueryParser parser;
			const ibQuerySelectPtr ast = parser.Parse(queryText);
			// 🛑 A COMPOSITION DOES NOT TAKE `TOTALS` (Max, 2026-08-19). Its totals ARE the
			// resources, and its levels ARE the groupings in the settings — the composer renders
			// the TOTALS clause itself from those two. A TOTALS written into the text would be the
			// same setting stated twice, in a place no window can show or edit, and the two would
			// disagree the moment either side changed.
			//
			// Refused OUT LOUD, in the query error, rather than ignored: a clause that is silently
			// dropped looks like a clause that did not work.
			if (ast && (!ast->m_totalsBy.empty() || !ast->m_totalsAggregates.empty() || ast->m_totalsOverall))
				ibBackendCoreException::Error(
					_("A composition does not take TOTALS: its totals are the resources and its levels are the groupings"));
			// ⭐ …AND THE PARAMETERS GO WITH IT. Describing a query resolves its names, and a name it
			// cannot resolve is a parameter nobody has set — which is exactly what an unfilled `&Ref`
			// is while the query is still being WRITTEN (Max: "how is it supposed to be set here, this
			// is the constructor?"). So every parameter the composition knows about is handed over,
			// with whatever it holds — an empty value included. Empty is an ANSWER: the shape of the
			// result does not depend on it, and describing the shape is all this is for.
			if (ast)
				ibQueryLowering::DescribeOutput(*ast, ParameterValues(), m_querySchema);
		}
		catch (const ibBackendException& error) {
			// AT ONCE, AND IN THE ENGINE'S WORDS. A query that cannot be described is a query that
			// cannot be run, and learning that when the report is first composed — in front of a user
			// rather than its author — is the thing worth avoiding.
			m_queryError = error.GetErrorDescription();
			m_querySchema.clear();
		}
	}

	// (No column COLLECTION is built any more — what resolved is kept as the output SCHEMA above,
	//  and every field list in the settings window reads that. The collection existed only to feed
	//  a dataview.)

	if (queryText.IsEmpty() && queryable != nullptr)
		GetModelComposer().FromSource(queryable);   // no text yet — read the source plainly

	PruneUnresolvedSettings();   // the query decides which fields exist; the settings follow it
}

// THE QUERY THE SOURCE WOULD WRITE FOR ITSELF — a real query over the real source, rendered by the same
// renderer the constructor round-trips through. Every column spelled out rather than `SELECT *`: the
// point of a composer is to CHANGE what is read, and changing a list of fields you can see is an edit,
// while changing a star is a rewrite.
wxString ibValueDataComposition::SeedQuery() const
{
	const ibBackendQueryable* queryable = GetSourceQueryable();
	if (queryable == nullptr)
		return wxEmptyString;

	// THE NAME THE LANGUAGE KNOWS IT BY — `Catalog.Products`, which is what the lowering resolves a FROM
	// against. Not the physical table: a query written against one is a query the config cannot be
	// restructured under.
	const ibQueryableSourceDescriptor* descriptor = GetSourceDescriptor();
	if (descriptor == nullptr || descriptor->GetName().IsEmpty())
		return wxEmptyString;

	ibQuerySelect select;
	if (!descriptor->GetNamespace().IsEmpty())
		select.m_from.m_name.push_back(descriptor->GetNamespace());
	select.m_from.m_name.push_back(descriptor->GetName());

	// NAMED, AND EVERY FIELD QUALIFIED BY THAT NAME — a bare `Code` is unambiguous only until a SECOND
	// table joins, and at that moment every field written before it becomes ambiguous at once. The same
	// rule the query constructor holds to when it adds a table.
	select.m_from.m_alias = descriptor->GetName();
	const wxString prefix = select.m_from.m_alias + wxT(".");

	for (const ibBackendQueryColumn* column : queryable->GetColumns()) {
		if (column == nullptr || column->GetName().IsEmpty() || !column->IsAllowed())
			continue;
		ibQueryProjection projection;
		projection.m_expr = ibQueryColumnFromPath(prefix + column->GetName());
		select.m_projections.push_back(std::move(projection));
	}
	if (select.m_projections.empty())
		select.m_selectAll = true;   // a source that vends no columns: read it whole rather than read nothing

	return ibRenderQuery(select);
}

std::vector<ibQueryConstructorField> ibValueDataComposition::GetConstructorFields() const
{
	// ONE IMPLEMENTATION, TWO CALLERS. The body moved to ibQueryFieldsOfText (query/queryConstructorModel)
	// on 2026-08-24: it never needed a composition — the text and the configuration are the whole of
	// the question, and a settings window that had to hold a RUNNING composition to ask it was holding
	// a runtime object for a metadata answer.
	return ibQueryFieldsOfText(GetQueryText(), GetSourceMetaData());
}

// DROP THE SETTINGS WHOSE FIELD THE COMPOSITION NO LONGER HAS — by RESOLUTION after every rebuild,
// never by chasing the change. A field removed from the query leaves a filter, a sort or a grouping
// pointing at nothing; so does an attribute renamed in the configuration. The answer comes from the
// SOURCE EXPLORER, the same list the pickers are built from, so what a person can still choose and what
// the composer still keeps are one answer rather than two.
void ibValueDataComposition::PruneUnresolvedSettings()
{
	const ibSourceExplorer* explorer = GetSourceExplorer();
	if (explorer == nullptr || explorer->GetHelperCount() == 0)
		return;   // nothing to check against is not a verdict — leave the settings alone

	GetModelComposer().PruneUnresolvedSettings([explorer](const wxString& path) {
		// THE FIRST SEGMENT is what has to exist here: the rest is a reference walk, and a walk
		// resolves through the metadata of whatever the first segment turned out to be.
		const wxString head = path.BeforeFirst(wxT('.'));
		if (head.IsEmpty())
			return true;
		return explorer->FindByName(head) != nullptr;
	});
}

// ⭐⭐ WHAT THE COMPOSITION DECLARES, PUT BACK — the same act the list's method of this name performs,
// and now the same body. Two classes, one name, one meaning.
//
// 🛑 IT WAS EMPTY, and its comment explained why: *"the settings object is a live facade writing the
// composer directly, so by the time anybody asks, the store already says what the settings say."*
// That stopped being true in this arc — the settings window writes the DESCRIPTION now, and the
// composer is seeded from it. So an AUTHOR editing the settings reached the composer only if the
// SOURCE happened to be rebuilt too; nothing else re-stated the declared section, and Compose does
// not (EnsureSourceBuilt only builds what was never built). A naming collision on the surface, a
// hole underneath (2026-08-24).
//
// ⚠ AND STILL NOTHING TO REDRAW — a report is not a list. What is on screen is the sheet that was
// BUILT; it is rebuilt when somebody says Compose, not the moment a condition is edited. This
// re-states the SETTING, not the picture.
void ibValueDataComposition::RefreshComposerSettings()
{
	// ⭐ THE VARIANTS, never the reader's setting — see RebuildSource for what writing the author's
	// into the reader's slot cost. A reader who has saved one goes on composing on it.
	GetModelComposer().LoadVariants(GetCompositionDesc().m_variants);
}

// ===========================================================================
//  Variants — N snapshots of the settings, one of them active
// ===========================================================================
//
// ⭐ WHAT A VARIANT IS (Max, 2026-08-19): a SNAPSHOT of the settings — its own groupings, its own
// filter, its own sort, "as if every variant were a page of settings of its own". One report then
// answers "sales" and "sales plus turnover" by having the person pick a variant, instead of being
// two reports. Parameters will join the snapshot when the composition grows them.
//
// ⚠ THE COMPOSER HOLDS EXACTLY ONE SET OF SETTINGS, and that is deliberate: the fetch, the compose
// and every reader below it stay unaware that variants exist at all. A variant becomes real by
// becoming the description's own settings, and the composer is then told through the ONE door there
// is — ibDataComposer::SetUserSettingsDesc. It goes in one direction only: nobody reads them back
// out of the composer, because the description is what they are.

void ibValueDataComposition::EnsureVariant()
{
	// ⭐⭐ THERE IS ALWAYS ONE, AND FOR A LIST THAT ONE IS ALL THERE IS (Max, 2026-08-23): a variant
	// is part of the report — a KIND of it, "Sales" and "Sales with profitability" over one source —
	// so a report has many and a list has the degenerate single. Held here rather than in a window,
	// so a composition built from script or generated by a report form has it too.
	//
	// ⭐⭐ AND IT LIVES IN THE DESCRIPTION, WHOLE. A live vector stood beside desc.m_variants holding
	// the structure and the parameter values while the description held the name and the settings —
	// one variant, split across two stores, and only one of them ever reached the file. That is why
	// a structure edited in the designer came back the way it was (Max, 2026-08-23).
	ibCompositionDescription& desc = GetCompositionDesc();
	if (desc.m_variants.empty()) {
		ibVariantDescription first;
		first.m_name = _("Main");
		desc.m_variants.push_back(std::move(first));
	}

	// ⚠ AND NO OUTPUT IS MADE HERE. A variant with no structure is a LEGITIMATE state — "I add the
	// output myself, and the grouping I add may well be empty; then nothing is printed" (Max,
	// 2026-08-24). The composer keeps an output of its own because something has to be RUN; what a
	// report DECLARES starts empty, like everything else a person writes.
}



// ===========================================================================
//  Parameters — what the query asks for, and who fills it in
// ===========================================================================

// THE TEXT IS ONE OF THE TWO AUTHORS. `&Period` in the query means the composition has a parameter
// called Period, whether or not anybody added it — so the list follows the text: new names appear,
// names the text stopped mentioning disappear, and what was already FILLED IN survives an edit.
//
// Hand-made parameters are untouched: they were added deliberately (a common module reads one, or
// the text is still being written), and a re-parse is not a reason to lose them.
// ⭐⭐ THE LIST FOLLOWS THE TEXT — WRITTEN ONCE, over any list. `&Period` in a query means the
// composition has a parameter called Period, so the text is one of the two authors of this list and
// re-reading it has to leave the other author's work alone.
//
// A FREE FUNCTION over the vector, because TWO places ask it: this value, when its query is
// re-applied, and the settings window, over the copy of the description it is editing. It used to be
// a method reaching for the object's own list, so the window — which edits a copy — never saw the
// parameters the text declared, and wrote its own list back over them.
void ibSyncParametersWithQuery(std::vector<ibParameterDescription>& parameters, const wxString& queryText)
{
	ibSyncParametersWithTexts(parameters, { queryText });
}

// ⭐⭐ …AND THE QUERY IS NOT THE ONLY TEXT THE COMPOSITION WRITES. A resource is an EXPRESSION —
// `COUNT(Date) * &Rate` — and it goes into the query beside everything else, so a `&Rate` there
// declares a parameter exactly as one in the body does (Max, 2026-08-28: "or I add parameters in the
// query body itself — those must land there too"). One rule over ALL the texts rather than one rule
// per place, which is how the second place ends up behaving differently from the first.
void ibSyncParametersWithTexts(std::vector<ibParameterDescription>& parameters,
	const std::vector<wxString>& texts)
{
	std::vector<wxString> named;
	for (const wxString& text : texts)
		for (const wxString& name : ibQueryLexer::ParamNames(text))
			if (std::find_if(named.begin(), named.end(),
					[&name](const wxString& seen) { return seen.IsSameAs(name, false); }) == named.end())
				named.push_back(name);

	// 1) Drop the AUTO ones the text no longer asks for.
	parameters.erase(
		std::remove_if(parameters.begin(), parameters.end(),
			[&named](const ibParameterDescription& parameter) {
				if (!parameter.m_fromQuery)
					return false;
				return std::find_if(named.begin(), named.end(),
					[&parameter](const wxString& name) { return name.IsSameAs(parameter.m_name, false); }) == named.end();
			}),
		parameters.end());

	// 2) Add what is new. An existing entry — hand-made or already filled in — is left exactly as it
	//    is: re-reading the text must not clear a value somebody chose.
	for (const wxString& name : named) {
		const auto found = std::find_if(parameters.begin(), parameters.end(),
			[&name](const ibParameterDescription& parameter) { return parameter.m_name.IsSameAs(name, false); });
		if (found != parameters.end()) {
			found->m_fromQuery = true;   // a hand-made one the text now mentions IS an auto one
			continue;
		}
		ibParameterDescription parameter;
		parameter.m_name = name;
		parameter.m_fromQuery = true;
		parameters.push_back(std::move(parameter));
	}
}

// WHICH TEXTS COUNT — the query body, and every resource expression. A resource may carry a
// condition of its own ("in a resource I can add some condition too" — Max, 2026-08-28), and what it
// names with `&` is a parameter of this composition like any other.
void ibSyncParameters(ibCompositionDescription& composition)
{
	std::vector<wxString> texts;
	texts.push_back(composition.m_query);
	for (const ibResourceDescription& resource : composition.m_resources) {
		texts.push_back(resource.m_path);
		texts.push_back(resource.m_func);   // the function half can hold one just as well
	}
	ibSyncParametersWithTexts(composition.m_parameters, texts);
}

void ibValueDataComposition::SyncParametersWithQuery()
{
	ibSyncParameters(GetCompositionDesc());
}



// EVALUATE WHAT HAS AN EXPRESSION — once, before the read.
//
// ⭐ THE DOOR IS THE LANGUAGE'S OWN (Max, 2026-08-19): `Evaluate` — the same system function a script
// calls, so a parameter can hold `CurrentDate()`, a call into a common module, arithmetic over
// another value: anything the language can say. It is legitimate HERE and not in a computed field
// for one reason — it runs ONCE, before the query, and what reaches the query is a plain value; a
// scripted field would run per row and take the read into memory with it.
//
// ⭐ AND THE RESULT DECIDES THE TYPE. A parameter has no declared type of its own: what it IS, is
// what the expression produced — so the value is adjusted to the type of that result (Max: "then
// through AdjustValue you push it into the parameter; the type is determined automatically when it
// is evaluated"). Adjusting rather than assigning keeps one rule about how a value lands, the same
// one every typed cell in the product follows.
//
// A refusal is NOT swallowed: the expression is the author's, and an expression that cannot be
// evaluated has to say so where it was written.
// EVALUATE ONE EXPRESSION — through the SESSION'S ROOT, so the common modules and the whole
// environment answer (Max, 2026-08-19: "you have to attach to the root module, that is what makes
// all the common functions and common modules available").
//
// 🛑 WHY NOT PLAIN `Evaluate`: it runs against the CURRENT run context, and pressing "Generate" is
// not a script — nothing is executing, the context stack is empty, and the call returns false
// without a word (the same silence that made the syntax check pass everything). So when there is no
// current frame, the root module's own is used: its ProcUnit, its bytecode, its names.
// ⭐⭐ AND IT SAYS WHETHER IT WORKED. `ibProcUnit::Evaluate` is the DEBUGGER'S door: on any failure it
// swallows the exception, writes the reason INTO the result as `<error: …>` and returns false —
// which is right for a watch row and silent everywhere else. A report took the "result", adjusted it
// to the parameter's declared type (an error string adjusted to a date is an EMPTY date) and
// composed on nothing, saying nothing (Max, 2026-08-28: "an expression with an error — it did not
// even show there was an error").
//
// `produced` still carries the reason on failure, so the caller has something to show.
static bool ibEvaluateInRoot(const wxString& expression, ibValue& produced,
	const ibMetaData* metaData)
{
	produced = ibValue();
	if (expression.IsEmpty())
		return true;   // nothing to evaluate is not a failure

	auto* state = ibSession::GetPUState();
	ibRunContext* current = state != nullptr ? state->GetCurrentRunContext() : nullptr;

	if (current != nullptr)
		return ibProcUnit::Evaluate(expression, current, produced, false);

	// NO FRAME OF OUR OWN — borrow the root module's. The context is a frame descriptor: it carries
	// the ProcUnit whose bytecode the expression is compiled against, which is exactly what "attached
	// to the root" means.
	ibSession* session = ibSession::Current();
	ibValueModuleManagerRuntimeConfiguration* root = session != nullptr ? session->GetManagerModule() : nullptr;
	auto rootUnit = root != nullptr ? root->GetProcUnit() : nullptr;

	// ⭐⭐ AND IN THE DESIGNER THE ROOT IS THE EDIT MANAGER. There is no runtime root there, and this
	// used to answer "true, nothing produced" — success with an empty value, which the caller cannot
	// tell from an expression that legitimately evaluated to nothing. So a composer's parameter
	// expression was silently ignored for everyone building a report, which is precisely where it is
	// written (Max, 2026-09-01, pointing at it: *"look at how the expression in a report works —
	// there is exactly the same problem"*).
	//
	// ibSession::GetEditModuleManager is the seam for this: the manager whose context a module
	// compiled against THIS configuration parents to — the Designer's lightweight one, the session's
	// root at runtime. The same door script checking was just given.
	if (!rootUnit) {
		if (ibValueModuleManager* editManager = ibSession::EditModuleManagerFor(metaData))
			rootUnit = editManager->GetProcUnit();
	}

	if (!rootUnit)
		return true;   // no context at all — nothing to evaluate against, and nothing invented

	ibRunContext rootFrame;
	rootFrame.SetProcUnit(rootUnit.get());
	return ibProcUnit::Evaluate(expression, &rootFrame, produced, false);
}
// ⭐⭐ WHAT A RUN IS GIVEN — WORKED OUT HERE AND KEPT HERE. Nothing is written back into the
// description: a description is DATA, and an evaluated parameter is a runtime value — a reference
// with a session behind it, the result of an expression. Writing them into the description put
// runtime into a structure that is saved with the configuration and read back while the next load is
// still building its tree, which is where "Unknown value type '<id>'" came from (Max, 2026-08-29:
// "there is no runtime in a description, at all").
//
// So the run gets a map of its own, made fresh, and the description stays exactly what the author
// wrote.
std::map<wxString, ibValue> ibValueDataComposition::EvaluatedParameterValues() const
{
	std::map<wxString, ibValue> values;

	for (const ibCompositionParameter& parameter : Parameters()) {
		if (parameter.m_name.IsEmpty())
			continue;

		// ⭐⭐ THE READER'S VALUE WINS, PARAMETER BY PARAMETER. The author declares every parameter and
		// offers some of them ("For user"); what the person running the report put beside one of those
		// is what the query is given. Asked by NAME rather than section-wide, because a reader who
		// filled in a period said nothing about any other parameter (Max, 2026-08-29).
		const ibDataNode* stored = &parameter.m_value;
		for (const ibParameterDescription& theirs : GetModelComposer().GetUserParameters())
			if (theirs.m_name.IsSameAs(parameter.m_name, false)) { stored = &theirs.m_value; break; }

		// ⭐ AND HERE THE STORED NODE BECOMES A RUNTIME VALUE. The schema holds what was written; the
		// composer is running by now, with every metaobject in place, which is the moment a reference
		// can be built truthfully — and the only moment it should be built at all.
		ibValue value = ibStoredValue(*stored, GetSourceMetaData());

		if (parameter.m_expression.IsEmpty()) {
			values[parameter.m_name] = value;
			continue;
		}

		// ⭐⭐ A BROKEN EXPRESSION STOPS THE READ AND SAYS SO. It used to produce nothing and be
		// adjusted into an empty value, so a report composed on a parameter nobody had filled in and
		// looked exactly like one with no data. What the reader gets now is the engine's own words,
		// under the name of the parameter that could not be worked out.
		ibValue produced;
		if (!ibEvaluateInRoot(parameter.m_expression, produced, GetSourceMetaData())) {
			ibBackendCoreException::Error(_("Parameter '%s' could not be evaluated: %s"),
				parameter.m_name, produced.GetString());
		}

		// THE TYPE COMES FROM WHAT WAS PRODUCED. An empty result leaves the parameter empty — that is
		// an answer too, and forcing a type onto nothing would invent one.
		// THE DECLARED TYPE WINS when there is one — that is what declaring it is FOR. With none, the
		// result decides: a parameter has no type of its own until somebody gives it one.
		ibTypeDescription target = parameter.m_type;
		if (target.GetClsidCount() == 0 && !produced.IsEmpty())
			target.SetDefaultMetaType(produced.GetClassType());

		values[parameter.m_name] = target.GetClsidCount() > 0
			? ibValueTypeDescription::AdjustValue(target, produced, GetSourceMetaData())
			: produced;
	}

	return values;
}
std::map<wxString, ibValue> ibValueDataComposition::ParameterValues() const
{
	std::map<wxString, ibValue> values;
	for (const ibCompositionParameter& parameter : Parameters()) {
		if (parameter.m_name.IsEmpty())
			continue;
		// AN EXPRESSION IS NOT EVALUATED HERE (that happens at the run — see
		// EvaluatedParameterValues): for describing the query the value it will produce is
		// irrelevant, only that the name IS answered.
		//
		// ⭐ THE STORED NODE IS READ, because describing asks what TYPE stands behind the name and a
		// packed node answers nothing. The description is not touched by it — the value goes into
		// this map and no further.
		values[parameter.m_name] = ibStoredValue(parameter.m_value, GetSourceMetaData());
	}
	return values;
}

// THE ONE POINT WHERE A RUN SETTLES ITS PARAMETERS: evaluate what is an expression, then hand every
// value to the composer. Called by the one reader there is now — Compose — so "what the query was
// given" is settled in exactly one place.
void ibValueDataComposition::PrepareParametersForRun()
{
	for (const auto& parameter : EvaluatedParameterValues())
		GetModelComposer().Parameter(parameter.first, parameter.second);
}

// (RunComposerPage is gone: it was the LIST's fetch — a page, an anchor, a direction. A composition
//  has one read and one result, and it lives in Compose.)

// A RESOURCE IS AN AGGREGATE OVER A FIELD — what the levels actually fold. Without one a grouped
// composition produces the shape of a report and none of its numbers.
//
// ⭐⭐ THE FACADE WRITES THE DESCRIPTION, AND THAT IS ALL IT DOES. There is nothing to apply
// afterwards and nothing to keep in step: the description is held BY REFERENCE, so a script filling
// it through this value and a window editing the same description are looking at one object (Max,
// 2026-08-24). The composer is handed the list at a run.
void ibValueDataComposition::AddResource(const wxString& func, const wxString& path)
{
	if (path.IsEmpty())
		return;   // a resource with no expression is not a resource
	GetCompositionDesc().m_resources.push_back({ func, path });
	OnChildChanged();
}

bool ibValueDataComposition::SetResource(size_t idx, const wxString& func, const wxString& path)
{
	std::vector<ibResourceDescription>& resources = GetCompositionDesc().m_resources;
	if (idx >= resources.size() || path.IsEmpty())
		return false;
	resources[idx] = { func, path };
	OnChildChanged();
	return true;
}

bool ibValueDataComposition::RemoveResource(size_t idx)
{
	std::vector<ibResourceDescription>& resources = GetCompositionDesc().m_resources;
	if (idx >= resources.size())
		return false;
	resources.erase(resources.begin() + idx);
	OnChildChanged();
	return true;
}

bool ibValueDataComposition::GetResourceAt(size_t idx, wxString& func, wxString& path) const
{
	const std::vector<ibResourceDescription>& resources = GetCompositionDesc().m_resources;
	if (idx >= resources.size())
		return false;
	func = resources[idx].m_func;
	path = resources[idx].m_path;
	return true;
}

// COMPOSE INTO A DOCUMENT. The composer walks its rows into the spreadsheet driver, which owns the
// whole layout decision (header + freeze, one row per row, nesting as outline groups, a bound
// parameter on every cell that stands for something openable). Nothing here knows about a window:
// the document is the result, and a view is a subscriber to it.
// ⭐⭐ READ, LAY OUT, AND FILL THE SHEET THIS MODEL HOLDS. The control is already subscribed to that
// sheet, so filling it IS what puts the report on screen — nothing is installed afterwards and the
// control is never re-pointed (Max, 2026-08-20).
//
// 🛑 IT BUILDS INTO A DOCUMENT OF ITS OWN FIRST, then swaps. Writing into the shown one would fire
// its notifiers from a worker thread — that is GUI touched off the UI thread — and would show the
// report half-drawn while it is built. The swap is one move, on a handle: the backend document
// carries a refcount of its own (wxObjectDataPtr), parallel to the value refcount, so the old sheet
// lives exactly as long as somebody still holds it.
bool ibValueDataComposition::Compose()
{
	wxObjectDataPtr<ibBackendSpreadsheetObject> composed(new ibBackendSpreadsheetObject());
	if (!Compose(composed.get()))
		return false;

	// ⭐⭐ AND THE FINISHED SHEET BECOMES THE ONE THIS MODEL HOLDS — the whole document, not its
	// description: the drill-down parameters and the read-only mode live on the OBJECT, so publishing
	// a description alone leaves every cell bound to a name nothing answers to.
	m_spreadsheetDoc = composed;
	return true;
}

// …AND THE ONE ROUTINE BOTH ENTRANCES USE. The script hands over a document it owns
// (`Compose(Document)`); the model's own verb hands over a fresh one and installs it. Nothing here
// knows which of the two it is — and no finished sheet is ever copied afterwards.
bool ibValueDataComposition::Compose(ibBackendSpreadsheetObject* target)
{
	if (target == nullptr)
		return false;

	// ⭐ THE SOURCE IS BUILT HERE IF NOBODY HAS BUILT IT — running is the asking, and a description
	// read from a file has resolved nothing by design. Without this the FIRST Generate after opening
	// a report answered "no source is set" and the second worked, because opening the settings window
	// in between had applied the query (Max, 2026-08-24).
	EnsureSourceBuilt();

	// 🛑 A QUERY THAT COULD NOT BE DESCRIBED CANNOT BE COMPOSED — AND SAYS SO.
	//
	// RebuildSource already asked the engine to describe the query and kept its refusal verbatim
	// (m_queryError). Until now only the settings window read that field, so pressing Generate over
	// a query the engine had refused did nothing at all: no rows, no message, no clue — "I press
	// compose and it quietly dies" (Max, 2026-08-20). The engine's own words are the message.
	if (!m_queryError.IsEmpty())
		ibBackendCoreException::Error(m_queryError);

	// THE PARAMETERS ARE SETTLED FIRST — the same door the fetch uses (PrepareParametersForRun):
	// expressions evaluated once, values handed to the composer, and only then the read.
	PrepareParametersForRun();
	ibDataComposer& composer = GetModelComposer();

	// ⭐⭐ AND THE SETTINGS ARE DRIVEN IN HERE — at the moment the composer FIRES (Max, 2026-08-23:
	// "you need it at the moment your composer goes off at L5").
	//
	// Not on a refresh, not when a window closes, not when a filter line is typed: THIS is the run,
	// and what it runs on is stated once, in the breath before it. Everything up to here only
	// changed DATA — the description, or the copy a window was editing — and data that has not
	// reached a run has changed nothing about what the report says.
	//
	// NOTHING HANDED IN = RUN ON THE ACTIVE SETTING — the one somebody set, or, when nobody did, the
	// composition's own. That is not a fallback: a report nobody configured produces what it was
	// written to produce, and its own description is where that is written.
	// (NOTHING IS HANDED IN. The composer already carries what is in force — the author's, put there
	//  when the source was built, or the reader's, put there when they accepted the settings window.
	//  A run does not choose a setting; it uses the one that was chosen.)

	// ⭐ …AND THE STRUCTURE AFTER THEM, ALWAYS IN THAT ORDER. The settings rebuild the grouping
	// ladder out of a flat list of lines, and a flat list cannot say "one level made of two fields",
	// nor "a second output", nor "an axis of columns". Applying the active variant's own outputs
	// last is what makes those survive a run — and doing it HERE, at the run, is what keeps the two
	// in order however the settings arrived.
	{
		const ibCompositionDescription& desc = GetCompositionDesc();

		// ⭐ THE RESOURCES ARE THE COMPOSITION'S OWN — declared once and computed by every output over
		// its own rows. They come from the DESCRIPTION, which is where they are saved from: they used
		// to be edited straight into the composer, so closing the report and opening it again showed
		// none of them (Max, 2026-08-24).
		composer.ClearResources();
		for (const ibResourceDescription& resource : desc.m_resources)
			composer.Resource(resource.m_func, resource.m_path);

		// ⭐ …AND WHAT THE QUERY'S SELECTS SAY ABOUT THEIR FIELDS, from the same place and for the
		// same reason. A title belongs to a FIELD, a field belongs to a SELECT, and everything that
		// names one — a resource, a level, a printed column — reads it through the path
		// (ibTitleForPath). Empty is the ordinary state: only what somebody said is carried.
		composer.Selects() = desc.m_selects;

		// ⭐ AND WHAT THE WHOLE COMPOSITION SHOWS, from the same place and for the same reason — the
		// bottom of the pile every output and node adds to (SelectedFor).
		//
		// 🛑 IT WAS THE LAST SET STILL EDITED STRAIGHT INTO THE RUNNING COMPOSER. The settings window
		// read and wrote `CommonSelected()`, nothing ever carried the description's own into it, and
		// `ibDataComposer::Select` — the flat door that was the only other way to fill it — had no
		// callers at all. So what a person chose on the REPORT row lived until the report closed and
		// reached the file never, exactly as the resources above did before they moved.
		composer.CommonSelected() = desc.m_selected;

		// ⭐⭐ THE STRUCTURE OF WHAT COMPOSES — the reader's saved setting when there is one, variant
		// `[0]` when there is not. ONE question, asked of the composer, so the outputs and the
		// settings can never come from two different places.
		//
		// 🛑 IT READ THE DESCRIPTION'S FIRST VARIANT REGARDLESS, and that is what lost every per-node
		// edit: a reader set a grouping's own filter, the window handed it back, and this line put
		// the AUTHOR's structure over it on the next compose.
		{
			// Asked of the SECTION — the question is about the structure, so it is put to the accessor
			// that answers it.
			const std::vector<ibOutputDescription>& stored = composer.GetCurrentStructure();
			if (!stored.empty()) {
				std::vector<ibDataComposer::Output>& live = composer.Outputs();
				live.resize(stored.size());
				for (size_t i = 0; i < stored.size(); ++i)
					static_cast<ibOutputDescription&>(live[i]) = stored[i];   // the driver each output has stays
			}
		}
	}

	ibSpreadsheetComposeDriver driver(target);

	// THE HEADING SAYS WHAT WAS ASKED. A report without its conditions cannot be defended a week
	// later, and the conditions are not a second store — they are the filters the composition
	// already carries, read back through the same door the settings window reads.
	driver.SetTitle(GetSourceCaption());
	// 🛑 IT PRINTED THE SCOPE AND NOT THE FILTER. This walked the flat list — the engine's own
	// per-fetch conditions — while everything a PERSON asked for lives in the filter in force. So a
	// report run with conditions printed a heading that said there were none, which is the one thing
	// a heading exists to prevent (2026-08-24).
	for (const ibFilterNodeDescription& node : composer.GetCurrentFilterDesc().m_nodes) {
		if (!node.m_use || node.m_kind != ibFilterNodeKind_Condition)
			continue;   // a line switched off was not asked for; a group is not one condition
		// ⭐ THE LINE'S OWN LABEL WINS, AND THIS IS WHAT IT IS FOR. `m_presentation` is the wording a
		// person typed for the condition; it round-tripped through the file and the window and was
		// consulted by nothing at all (audit, 2026-08-24). A heading is exactly where somebody's own
		// phrasing of a condition belongs.
		if (!node.m_presentation.IsEmpty()) {
			driver.AddHeaderLine(node.m_presentation);
			continue;
		}
		// …and generated otherwise. A SIDE READS AS ITS OWN TEXT — a field as its presentation, a
		// value as its value: the same rule the settings window prints a line by
		// (ibFilterTreeModel::GetValueByRow), so a heading and the window a person set it in cannot
		// word one condition two ways.
		auto side = [](const ibFilterOperandDescription& operand) {
			return operand.IsField()
				? (operand.m_presentation.IsEmpty() ? operand.m_path : operand.m_presentation)
				: operand.m_value.GetString();
		};
		driver.AddHeaderLine(side(node.m_left) + wxT(" ")
			+ ibValue::CreateEnumObject<ibValueEnumComparisonKind>(node.m_comparison).GetString()
			+ wxT(" ") + side(node.m_right));
	}

	// ⭐ EVERY OUTPUT PRINTS, one after another, onto the SAME sheet (Max: "you load the outputs and
	// then run once, and it fills the drivers"). Running the first one only is what made a second
	// output look dead: it was declared, settings and all, and never read.
	//
	// One driver for all of them — the document is one, and an output's block follows the previous
	// one down the page. A driver of its own comes when an output needs a different KIND of drawing
	// (a chart), which is a driver question, not a composition one.
	for (ibDataComposer::Output& output : composer.Outputs())
		output.m_driver = &driver;
	const bool composed = composer.Run();

	// The pointers do not outlive this call — the driver is a stack object, and an output holding a
	// dangling one would be read on the next compose.
	for (ibDataComposer::Output& output : composer.Outputs())
		output.m_driver = nullptr;

	return composed;
}

// ⭐⭐ THE COMPOSITION'S OWN FETCH — one shot, not a page.
//
// The twin of ibValueModel::SubmitFetchAsync, and deliberately NOT it: a list rents a run and then
// serialises PORTIONS through the view door's lock, because a table is read a window at a time. A
// composition is not: one request produces one sheet, so there is nothing to serialise and no door to
// lock — what is borrowed is only the background thread and a connection of its own, so the window
// stays alive while the report is built.
//
// A refusal to rent (no job manager, nothing free, a headless test) means "read it here instead" —
// never "no data".
void ibValueDataComposition::SubmitFetchAsync(std::function<void()> work)
{
	if (!work)
		return;

	// ONE SHEET, ONE SLOT: a second Compose means the first read is no longer wanted.
	CancelFetch();

	if (ibJobManager* const jobs = ibApplicationData::GetJobManager()) {
		try {
			// The handle is KEPT: CancelFetch waits on it, so a read cannot outlive this composition.
			m_fetchRun = jobs->StartBackground(
				[work](ibSession*) -> ibValue { work(); return ibValue(); },
				_("composing the report"),
				ibJobTenancy::Tenant);
			return;
		}
		catch (const ibBackendException&) {
			// Nothing to rent — fall through and read on this thread.
		}
	}

	work();
}

// Cooperative: the flag is raised and the run is waited out — a query already in flight finishes.
void ibValueDataComposition::CancelFetch()
{
	if (!m_fetchRun)
		return;
	m_fetchRun->Cancel();
	m_fetchRun->Wait();
	m_fetchRun.reset();
}

// (GetFeatures / GetValueByRow / GetValueByMetaID / GetRowAt are GONE with the list surface — a
//  composition has no dataview rows to marshal a value out of. What it produces is a SHEET.)

ibUniqueKey ibValueDataComposition::GetGuid() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return q->GetQueryTableGuid();

	return wxNullGuid;
}

// (GetItemKey / ActivateItem are gone with the list surface: they keyed a DATAVIEW row, and a
//  composition has none. The drill-down never went through them — a composed cell carries its value
//  as a document parameter and the click ends in ibValue::ShowValue.)

// Forward the source's command SET — metadata-blind, whatever the descriptor lists is what shows. This
// is what puts a working command bar on a composition dropped onto any form, with no code written.
// ⭐⭐ THE COMPOSER NAMES ITS OWN VERBS. Compose and Settings are facts about a COMPOSITION — it
// reads, and it has settings to read by — not about whatever control happens to show it (Max,
// 2026-08-20: "those you do not need; they are determined by the composer itself"). A control lays
// out what it is given, so the same two appear wherever a composition is shown, and a spreadsheet
// document offers neither because it has neither.
//
// They land in the FORM's own toolbar when the composition is the form's main view — the control
// bound to it reports itself as such, and its bar is suppressed in favour of the form's.
void ibValueDataComposition::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	// COMPOSE changes what is SHOWN, not what is stored — live even on a view-only form.
	commands.push_back(ibCommandItem(ibSpreadsheetModelCommand_Compose, wxT("Compose"), _("Compose"),
		ibPictureDescription(g_picGenerateCLSID), true).SetModify(false));
	commands.push_back(ibCommandItem());   // separator — "show it" is not "arrange it"
	commands.push_back(ibCommandItem(ibSpreadsheetModelCommand_Settings, wxT("Settings"), _("Settings"),
		ibPictureDescription(g_picStructureCLSID), false).SetModify(false));
	// ⭐ AND THE VARIANTS — the settings the author NAMED, offered as a menu. A verb of the
	// composition like the two above: picking one sets the reader's setting and nothing is stored
	// as "the active variant" (ibDialogComposerSettings::ShowVariantPicker). Shows what is READ, so
	// it stays live on a view-only form.
	// …AND IT SHOWS ITS NAME, like Compose does: a tick-mark icon alone says nothing about what will
	// happen, and this is a verb a person looks for by word (Max, 2026-08-26).
	// …AND THE AUTHOR'S VARIANTS STAND APART FROM THE SETTINGS WINDOW TOO (Max, 2026-08-26). Four
	// groups, four questions: what the report SHOWS (compose) · how it is ARRANGED right now
	// (settings) · which of the AUTHOR's arrangements to take (variants) · what THIS person kept
	// (restore, save). A bar of seven equal buttons makes a reader find the one they want by reading
	// all seven.
	commands.push_back(ibCommandItem());   // separator — the author's named arrangements are their own group
	commands.push_back(ibCommandItem(ibSpreadsheetModelCommand_Variants, wxT("Variants"), _("Variants"),
		ibPictureDescription(g_picSelectCLSID), true).SetModify(false));

	commands.push_back(ibCommandItem());   // separator — …and the reader's own shelf is another

	// ⭐ …AND THE READER'S OWN, beside the author's. Same shape, different shelf: a variant ships
	// with the configuration, a saved setting is what THIS person arranged and kept. Both show what
	// is READ — nothing in the base's data changes — so they stay live on a view-only form.
	commands.push_back(ibCommandItem(ibSpreadsheetModelCommand_RestoreSettings, wxT("RestoreSettings"), _("Restore settings"),
		ibPictureDescription(g_picSelectCLSID), true).SetModify(false));
	commands.push_back(ibCommandItem(ibSpreadsheetModelCommand_SaveSettings, wxT("SaveSettings"), _("Save settings"),
		ibPictureDescription(g_picSaveCLSID), true).SetModify(false));

	// …then whatever the SOURCE offers, after a rule.
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor()) {
		const size_t before = commands.size();
		holder->GetCommandCollection(formType, commands);
		if (commands.size() > before)
			commands.insert(commands.begin() + before, ibCommandItem());   // separator
	}
}

// ⚠ NO ROW KEYS. The command surface is forwarded as it always was, but the anchor and the selection
// were DATAVIEW rows — a composition has none. A command that acts on "the current row" belongs to
// whatever shows the result (the sheet knows which value a cell carries), so an empty key is the
// honest argument here rather than one invented from a row that does not exist.
//
// The gridbox hands back an id it took from this very store, so it goes straight to the source.
// (A spreadsheet document has no store and never reaches here.)
void ibValueDataComposition::CallAsModelCommand(const ibActionID& id, ibBackendValueForm* srcForm)
{
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->CallAsCommand(id, ibUniqueKey(), ibUniqueKey(), srcForm);
}

const ibSourceExplorer* ibValueDataComposition::GetSourceExplorer() const
{
	// Columns come from the QUERYABLE, not a metaobject — a composition is queryable-based.
	// ⭐ A COMPOSITION IS NOT A TABLE (Max, 2026-08-19): "the composer is seen as a table, but a
	// composer is not a table — what it is, is where it OUTPUTS: a spreadsheet document".
	//
	// The root used to be flagged a tabular section, and that one flag is what made the whole
	// product treat it as a list: `IsTableSource` reads the flag, so dragging the attribute onto a
	// form built a TABLEBOX, and a tablebox's source picker offered the composition while the
	// GRIDBOX's picker — the control that actually shows a composed report — offered nothing.
	//
	// Unflagged, it is an attribute like any other value: the box that shows it is the gridbox, and
	// what appears there is the DOCUMENT the composition composed.
	m_sourceExplorer.Reset(GetObjectTypeName(), GetObjectTypeName(), wxNOT_FOUND, g_valueDataCompositionCLSID, /*tableSection*/false);
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->FillSourceExplorer(m_sourceExplorer);

	// …AND WHAT THE QUERY ADDS ON TOP. The source's fields are there because the rows are its rows; the
	// query's output columns are there because that is what the composition actually shows. Added, not
	// substituted — and a name the source already offers is not added twice.
	for (const ibQueryLowering::OutputColumn& column : m_querySchema) {
		if (column.m_name.IsEmpty())
			continue;
		if (m_sourceExplorer.FindByName(column.m_name) != nullptr)
			continue;

		// The column's own DESCRIPTOR when the query did not rename it — that is what carries the
		// synonym and the binding. A RENAMED one (`Owner.Code AS Supplier`) is appended by name and
		// type: the new name is the query's, and it belongs to no attribute to borrow a synonym from.
		if (column.m_col != nullptr && column.m_col->GetName().IsSameAs(column.m_name, false))
			m_sourceExplorer.AppendColumn(column.m_col);
		else
			m_sourceExplorer.AppendColumn(column.m_name,
				column.m_col != nullptr ? column.m_col->GetColumnId() : wxNOT_FOUND,
				column.m_col != nullptr ? column.m_col->GetTypeDesc() : ibTypeDescription());
	}
	return &m_sourceExplorer;
}

wxString ibValueDataComposition::GetSourceCaption() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return GetSourceMetaObject() ?
			stringUtils::GenerateSynonym(GetSourceMetaObject()->GetClassName()) + wxT(": ") + GetSourceMetaObject()->GetSynonym() : q->GetQueryName();

	return GetClassName();
}

const ibMetaData* ibValueDataComposition::GetMetaData() const
{
	// SELECTION — which config to PICK / resolve a source FROM: the owner FORM's config, via the attach
	// chain. Non-cyclic: the form falls back to its SOURCE (this composer) for metadata, and the
	// composer's GetSourceMetaData reads from the VALUE (terminal), so the walk ends.
	if (const ibPropertyObject* o = GetAttachOwner())
		return o->GetMetaData();

	return ibApplicationData::GetActiveMetaData();
}

const ibMetaData* ibValueDataComposition::GetSourceMetaData() const
{
	// READ — the STORED source config, captured in RebuildSource. Returned straight from the field: no
	// owner-walk, no queryable re-resolve, so a form's metadata may fall back here without looping.
	// Nothing stored yet → the ACTIVE config.
	return m_sourceMetaData != nullptr ? m_sourceMetaData : ibApplicationData::GetActiveMetaData();
}

// --- settings surface -------------------------------------------------------

// ⚠ NO Filter / Order / Group / Settings HERE FOR NOW — see ibValueDynamicList::FillMembers. The
// runtime wrapper they vended is gone; what a composition IS lives in its description, and the
// script surface over it is rebuilt over the settled structure.
void ibValueDataComposition::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProc(wxT("Refresh"), wxT("Refresh()"));
	helper.AppendProc(wxT("Compose"), wxT("Compose(Document)"));
}

bool ibValueDataComposition::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case 0:   // Refresh — for a report that means BUILD IT AGAIN, with whatever the settings now say.
		RefreshComposerSettings();
		Compose();
		return true;
	case 1: {  // Compose(Document)
		if (lSizeArray < 1 || paParams[0] == nullptr)
			return false;
		// The argument is the script's spreadsheet document; the C++ document under it is what
		// the driver writes into, and it is the same object the grid on the form is showing.
		ibValueSpreadsheetDocument* value = CastValue<ibValueSpreadsheetDocument>(paParams[0]);
		if (value == nullptr)
			return false;
		return Compose(value->GetSpreadsheetDocument().get());
	}
	}
	return false;
}

// --- ibPropertyObject -------------------------------------------------------


void ibValueDataComposition::OnPropertyRefresh()
{
	// See the header: the inspector shows "Settings..." and nothing else. Hidden rather than
	// removed — what they hold still serialises, and the settings window still edits the query.
	HideProperty(m_propertySource, true);
}
void ibValueDataComposition::OnPropertyChanged(ibProperty* property, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
	if (m_propertySource == property)
		RebuildSource();
	RefreshComposerSettings();
}

bool ibValueDataComposition::ReadProperty(const ibDataNode& node)
{
	// ⚠ THE SOURCE IS NOT READ HERE ANY MORE, and that is the point of the id being in the
	// description. ibPropertyDynamicSource reads by RESOLVING (`SetQueryable(factory->ResolveById(id))`)
	// and `SetQueryable(nullptr)` writes wxNOT_FOUND back — so a composition read before its metatype
	// registered did not merely fail to describe its query, it FORGOT WHICH TABLE IT WAS. The
	// description keeps the number whatever the world knows at the moment of reading; the property is
	// filled from it later, by Apply(), when resolving can succeed.

	// ⭐⭐ AND NOT A WORD ABOUT WHAT IT MEANS. Reading stores what the composition IS; working out what
	// its query means is the OTHER act (ApplySource — see SetQueryText, where the two were separated
	// for the editor), and it belongs to whoever ASKS for the composition. The cell it lives in makes
	// that the rule: ibVariantDataComposition refreshes on GetComposition() and never on a load.
	//
	// 🛑 A COMPOSER IS READ WITH THE TREE. Since it became a metaobject of its own, a composition is
	// read in the LOAD pass — while the tables its query names announce themselves in the RUN pass
	// afterwards — so describing the query here asked about metatypes that had not spoken yet and
	// recorded an error about a name that resolves perfectly a moment later. A composition on a FORM
	// never showed it: a form is read when it opens, with everything already up.
	//
	// ⚠ AND NOTHING IS SYNCHRONISED HERE EITHER. If getting the composition needed something to be
	// brought up to date on the side first, the split would not be a split at all — asking is the
	// whole of it (Max). What the TEXT asks for is added by the refresh, at the asking.
	// ⚠ AGAINST ITS OWN CONFIGURATION — the one this composition was made with. The values inside a
	// saved composition (a filter's right side, a parameter) are references and enum members, and
	// those are built by the metadata, not by the value factory.
	return ibCompositionDescriptionMemory::ReadNode(node, GetCompositionDesc(), GetSourceMetaData());
}

// …AND THE OTHER HALF OF THE TRANSLATION: a description becomes live objects. Nothing here reads a
// node — what a record LOOKS like was settled in compositionDescription.cpp — this only puts the
// pieces where a running composition keeps them.

bool ibValueDataComposition::WriteProperty(ibDataNode& node) const
{
	return ibCompositionDescriptionMemory::WriteNode(node, GetCompositionDesc());
}

// Register the type — runtime / designer know "DataComposition".
VALUE_TYPE_REGISTER(ibValueDataComposition, "DataComposition", g_valueDataCompositionCLSID);
// (The row type is gone with the dataview surface: there is no return LINE where there are no rows.)
