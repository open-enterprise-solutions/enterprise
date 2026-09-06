#ifndef __VALUE_DATA_COMPOSITION_H__
#define __VALUE_DATA_COMPOSITION_H__

#include "backend/spreadsheetModel.h"             // ibValueSpreadsheetModel — the base a composition IS
#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject
#include "backend/composition/dataComposer.h"        // L5 — ibDataDBComposer
#include "backend/propertyManager/propertyObject.h"  // ibPropertyObject — the composer IS a property object
#include "backend/propertyManager/property/propertyDynamicSource.h"  // ibPropertyDynamicSource — the "Source" property
#include "backend/propertyManager/property/propertyDataComposition.h" // ibPropertyDataComposition — the "Settings…" action, this type's own
#include "backend/propertyManager/property/propertyString.h"         // ibPropertyString — the query text
#include "backend/query/queryLowering.h"              // ibQueryLowering::OutputColumn — what the query produces
#include "backend/query/queryConstructorModel.h"      // ibQueryConstructorField — what a window offers to pick from

class ibBackendQueryable;
class ibBackendQueryColumn;
class ibQueryableSourceDescriptor;

constexpr ibClassID g_valueDataCompositionCLSID = value_to_clsid("VL_DCMPN");

// ---------------------------------------------------------------------------
// ibValueDataComposition — the REPORT's data attribute: a source + a query + the
// composition settings a user edits (filter / sort / grouping).
//
// It is the dynamic list's sibling, not its subtype, and the difference is ONE
// rule: THE QUERY IS ALWAYS THERE. A dynamic list can stand on its main table
// alone (an arbitrary query is the exception it switches on); a composer is a
// declaration of what to READ and how to FOLD it, so the query text exists from
// the moment a source is picked — seeded from the source, then edited.
//
// What it shares with the list, deliberately and by reuse, not by copy:
//   * ibCompositionDescription — the SAME stored shape (filter, sort, grouping,
//     query, main table), edited by the SAME settings dialog;
//   * the L5 composer (GetModelComposer) and the base fetch (RunComposerPage) —
//     fetch lives ONLY in the parent;
//   * the source descriptor, so a row still knows how to open itself.
//
// What it does NOT carry (a report is not a picker): choice mode, folder-select,
// the hierarchy-column display. A tree here comes from GROUPING, never from a
// parent column.
//
// ⚠ NEXT STEP, held in mind while shaping this: the grouping is a FLAT ordered
// list today (ibGroupDescription — the same one the list uses). It becomes the
// output TREE — a node is a table, a table carries rows and, once cross-tables
// land, columns as well. Nothing here should assume "one flat grouping": read it
// out of the description, never cache the order.
// ---------------------------------------------------------------------------
// ⭐⭐ A COMPOSER IS A SPREADSHEET MODEL, NOT A TABLE MODEL (Max, 2026-08-20: "ibValueModelCursor is
// rubbish here — I asked for the spreadsheet document's model; it got there by being copied from the
// dynamic list").
//
// The cursor model is a LIST: it pages, it has an anchor and a direction, it hands out rows and view
// nodes and column collections. None of that is what a composition is. A composition READS ONCE and
// writes a SHEET — which is exactly what ibValueSpreadsheetModel owns (its own fetch, the L5 store,
// the Compose verb), and what a spreadsheet document is the other half of.
// ⭐ THE PARAMETER LIST FOLLOWS THE QUERY TEXT — one spelling, over any list. `&Period` in the text
// means there is a parameter called Period: what the text stopped naming goes (if the text put it
// there), what it names is added, and everything a person wrote or filled in is left alone.
//
// FREE, because two places ask it and they hold different lists: the composition, over its own
// description, and the settings window, over the copy of the description it is editing.
BACKEND_API void ibSyncParametersWithQuery(std::vector<ibParameterDescription>& parameters,
                                           const wxString& queryText);

// …and the same rule over EVERY text the composition writes — the query body plus each resource
// expression. A `&Rate` in a resource declares a parameter as much as one in the body does, and a
// parameter the texts still name cannot be removed by hand (m_fromQuery).
BACKEND_API void ibSyncParametersWithTexts(std::vector<ibParameterDescription>& parameters,
                                           const std::vector<wxString>& texts);

// ⭐ THE WHOLE DESCRIPTION, ASKED ONCE. Both callers hold one — the composition its own, the window
// the copy it is editing — so which texts count is answered HERE and not twice at the two call
// sites, where the second one would sooner or later count something different.
BACKEND_API void ibSyncParameters(struct ibCompositionDescription& composition);

// ⭐ ibCompositionHolder LAST — ibValue stays the FIRST base (offset 0), which every runtime
// value's layout is read through. The composer VALUE is the third holder of a composition, beside
// the report metaobject and the dynamic list, and it already answers with these very signatures.
class BACKEND_API ibValueDataComposition : public ibValueSpreadsheetModel, public ibSourceDataObject,
                                          public ibPropertyObject, public ibCompositionHolder {
public:

	// The source may be passed here — null means "set it later" (SetSource).
	ibValueDataComposition(const ibBackendQueryable* queryable = nullptr);

	// ⭐⭐ THE RUNNING COMPOSITION OVER A DESCRIPTION SOMEBODY ELSE HOLDS — and whoever wants one makes
	// it, here, with the two things it needs: the data, and the configuration to resolve names in.
	// A metaobject does not keep one for you (it DECLARES; what it has is the description), and a
	// property hands none out (it STORES). The designer builds one over the current description; a
	// report builds one over its own copy of it.
	//
	// ⚠ A NULL CONFIGURATION MEANS THE ACTIVE ONE, and the substitution happens HERE, in the
	// constructor — by the time this object exists it always has one. A caller that had nothing to
	// pass (a value built from script, a headless tool) said so by passing null; making each of them
	// reach for the active configuration itself is the same decision written in as many places as
	// there are callers.
	ibValueDataComposition(ibCompositionDescription& desc, const class ibMetaData* metaData);
	virtual ~ibValueDataComposition();

	// --- source & query (L5) ------------------------------------------------
	// The composer starts EMPTY. Picking a source SEEDS the query text over it
	// (see SeedQuery) — a composer with a source and no query would read nothing,
	// and an empty text is not a query.
	void SetSource(const wxString& ns, const wxString& name);
	void SetSourceQueryable(const ibBackendQueryable* queryable);
	// Not an override any more: it was the TABLE model's hook, and a composition is not one. The
	// question is still this type's own — its property holds the queryable.
	const ibBackendQueryable* GetSourceQueryable() const { return m_propertySource->GetQueryable(); }

	// The query itself — what is READ. Always present once a source is picked; the
	// settings dialog's first tab and the query constructor both edit this text.
	// ⭐⭐ THE QUERY IS THE DESCRIPTION'S — one truth, and the file proves it: this object serialises
	// through ibCompositionDescriptionMemory alone, so a query kept anywhere else was simply not
	// SAVED (Max, 2026-08-23: "the value is not written"). It used to live in a hidden property
	// beside the description, which is two states for one fact and the second one never travelled.
	wxString GetQueryText() const { return GetCompositionDesc().m_query; }
	void     SetQueryText(const wxString& text);

	// THE QUERY THE SOURCE WOULD WRITE FOR ITSELF — `SELECT <fields> FROM <source>`.
	// The starting point when a source is picked, empty when there is none.
	wxString SeedQuery() const;

	// WHY THE QUERY PRODUCES NOTHING — the ENGINE's own words, empty when it resolved.
	const wxString& GetQueryError() const { return m_queryError; }

	// THE FIELDS THIS COMPOSITION OFFERS, in the shape the expression editor and the query
	// constructor already speak (`ibQueryConstructorField`: name, presentation, source, type,
	// icon, and whether it can be dot-walked further).
	//
	// Asked of the ENGINE — the text is parsed and handed to ibQueryConstructorModel, the same
	// model the constructor's own field trees are built from. A window assembling this list
	// itself would be a second answer to "what does this query offer", and the two would part
	// company at the first reference field: this one unfolds, a hand-made one does not.
	//
	// An unparseable text yields an EMPTY list rather than raising — a window asking what it
	// could offer is not where a syntax error is reported.
	std::vector<struct ibQueryConstructorField> GetConstructorFields() const;

	// Re-apply source + query onto the composer (the query constructor calls this
	// after editing the text — SetValue does not fire OnPropertyChanged).
	void ApplySource() { RebuildSource(); }

	// The source's DESCRIPTOR (holder) — the composer reaches the source's command
	// interface through it and stays metadata-blind itself.
	const ibQueryableSourceDescriptor* GetSourceDescriptor() const { return m_propertySource->GetDescriptor(); }

	// Commit Filter/Order/Group from the buffer ONTO the composer — on a settings
	// change, NOT per fetch.
	void RefreshComposerSettings();

	// --- variants: N SNAPSHOTS of the settings, one of them active -----------
	//
	// ⭐ A VARIANT IS A SNAPSHOT (Max, 2026-08-19): its own groupings, its own filter, its own
	// sort — "as if every variant were a page of settings of its own". A person picks one and
	// everything switches to it, so one report answers "sales" and "sales plus turnover" without
	// being two reports.
	//
	// ⛔ AND THE VERBS FOR THEM LIVED HERE AND WERE NEVER CALLED — `VariantCount`, `GetVariantName`,
	// `SetVariantName`, `GetActiveVariant`, `SetActiveVariant`, `CaptureActiveVariant`, `AddVariant`,
	// `RemoveVariant`, plus `ApplyActiveVariant` and `LiveParameterSnapshot` behind them. The
	// settings window was written against the DESCRIPTION instead, and says so in its own comment:
	// *"it used to be asked of the live composition (CaptureActiveVariant)"*. The first road was
	// left standing, ~250 lines of it (audit, 2026-08-24).
	//
	// The variants themselves are untouched — they live in the description, which is where the
	// window edits them and where they are saved from.

	// (THE PER-PART NODE METHODS ARE GONE — WriteVariants / ReadVariants / WriteParameters /
	//  ReadParameters / WriteTotals / ReadTotals. Each of them stated part of what a saved
	//  composition looks like, which is now stated once in composition/compositionDescription.h;
	//  Describe() and Apply() below are all that is left on this side. The one caller that wanted a
	//  snapshot — the settings window, for Cancel — takes a DESCRIPTION now, which is the whole
	//  composition rather than the variants alone.)

	// --- parameters: what the query asks for, and who fills it in --------------
	//
	// ⭐ TWO WAYS IN, ONE LIST (Max, 2026-08-19): a parameter is either written into the query text
	// (`&Period` — the lexer finds it) or added BY HAND. Both live in the same list; the difference
	// is only that an auto one disappears when the text stops mentioning it, and a hand-made one
	// stays until it is removed. Nobody has to keep two lists in step.
	//
	// ⭐ AND TWO ORTHOGONAL QUESTIONS ABOUT EACH — kept apart from the first day, because folding
	// them into one field is exactly how a settings panel ends up with "value / available to user /
	// include in settings" and nobody can say which overrides which:
	//   * WHAT FILLS IT — a fixed value, or an EXPRESSION evaluated before the query runs.
	//   * WHO FILLS IT — the author fixes it, or it is handed to the user (quick settings).
	//
	// An expression is legitimate here in a way a scripted FIELD is not: it is evaluated ONCE, before
	// the read, and what reaches the query is a plain value — so `CurrentDate()` or a call into a
	// common module costs nothing per row and breaks no server-side paging. (The page cache signs
	// itself with the EVALUATED value, or "today" would be one signature all day.)
	// ⛔ AND THE TWELVE ACCESSORS OVER THEM ARE GONE (2026-08-24) — `ParameterCount`,
	// `GetParameterName` / `Value` / `Expression` / `Type`, `IsParameterUserSettable`,
	// `IsParameterFromQuery`, `SetParameterValue` / `Expression` / `UserSettable` / `Type`,
	// `AddParameter`, `RemoveParameter`. Not one had a caller, and none was published to script
	// either (`FillMembers` offers `Refresh` and `Compose`).
	//
	// They are not replaced by anything: the settings window edits `Parameters()` — the description —
	// directly, the same way it edits the variants. A script surface comes back over the SETTLED
	// structure, which is what the note at the top of this file says about Filter / Order / Group.
	// A HAND-MADE parameter — the query may not mention it yet (it is being written), or it may be
	// EVERY PARAMETER AND WHAT IT CURRENTLY HOLDS — the map the engine takes.
	//
	// ⭐ ONE DOOR for both uses: DESCRIBING the query (which resolves its names and would otherwise
	// report an unfilled `&Ref` as "parameter is not set" while the query is still being written) and
	// RUNNING it. An unfilled parameter arrives as an EMPTY value rather than being left out: empty
	// is an answer, and the SHAPE of the result never depends on it.
	std::map<wxString, ibValue> ParameterValues() const;

	// ⭐⭐ WHAT THE RUN IS GIVEN — the same names, with the expressions EVALUATED and the declarations
	// built into live references. `Evaluate` is the language's own door, so a parameter may hold a
	// call into a common module; the RESULT decides the parameter's type.
	//
	// ⚠ IT RETURNS THEM. Nothing is written back into the description: a description is DATA, and an
	// evaluated parameter is runtime — a reference with a session behind it. Writing runtime into a
	// structure that is saved with the configuration is what made a later load fail on a value it had
	// written itself (Max, 2026-08-29: "there is no runtime in a description, at all").
	std::map<wxString, ibValue> EvaluatedParameterValues() const;

	// ⭐ SETTLE THE PARAMETERS AT THE MOMENT THE QUERY RUNS (Max, 2026-08-19: "at the moment you
	// execute the query, if there is an expression there, you evaluate it through our script — quite
	// elegant"). Not when the settings are edited: `CurrentDate()` has to mean the day the report is
	// built, not the day somebody configured it. And not inside Compose alone: a composition is a
	// MODEL as well, so a fetch reads it too, and a parameter filled only on the report path would
	// look like "works in the report, empty in the list".
	//
	// Const because a fetch is const — what changes is the parameter's cached value and the
	// composer's parameter map, which are the state a run is entitled to settle.
	void PrepareParametersForRun();

	// COMPOSE INTO A DOCUMENT — the report's whole act, and the reason this type exists
	// separately from the list. It runs the composition and writes the result into a
	// spreadsheet document (header, rows, outline groups, drill-down parameters).
	//
	// The document is BACKEND, so this works with no window at all: the desktop grid, the
	// web client and a test are three readers of one result rather than three outputs.
	// ⭐ PRODUCE THE DATA — read, lay the result out, and write it into the sheet this model holds
	// (ibValueSpreadsheetModel::m_spreadsheetDoc). The control is already SUBSCRIBED to that sheet,
	// so filling it is what puts the report on screen: nobody installs anything afterwards and
	// nobody re-points the control (Max, 2026-08-20: "the command updates that table, and since the
	// table is subscribed to the control's events, the control learns it has data now — the same way
	// an ordinary table notifies when it changes").
	virtual bool Compose() override;

	// --- the store the settings are driven into -------------------------------
	//
	// (NO LIVE SETTINGS OBJECT. There was one — the same type a list carried — and it went under the
	//  knife with composition/listFilter.h on 2026-08-23. What a composition's filter, sort and
	//  grouping ARE is its description (GetCompositionDesc); the composer is told, once, through
	//  ibDataComposer::SetSettings.)

	// (THE STORE IS THE BASE'S — every sheet model has one, and a drawn document's is simply empty.
	//  GetModelComposer and the settings pair over it are inherited unchanged; a copy here would be
	//  a second composer under one report.)

	// ⭐⭐ AND THE READ IS RENTED — HERE, not on the base (Max, 2026-08-20: "make the async fetch just
	// an event on the base model and let the composer override it itself"). Reading is what a
	// composition does and nothing else does: a hand-filled document rents nothing because it reads
	// nothing, so the job manager is named in exactly one place — this one.
	//
	// A composition can take seconds against a live connection, and a window frozen for those seconds
	// cannot be moved, scrolled or cancelled. What cannot be rented (no job manager, a saturated pool,
	// a headless test) runs INLINE — the caller hands over work and gets it done, never told which way
	// it went.
	//
	// A SECOND REQUEST WHILE ONE IS IN FLIGHT CANCELS THE FIRST. There is one sheet and one slot, and
	// pressing Compose again means "forget that read, do this one" — anything else either drops the
	// press silently or lets two runs race for the same sheet.
	virtual void SubmitFetchAsync(std::function<void()> work) override;
	// Cooperative: the flag is raised and the run is waited out, so a read cannot outlive the
	// composition that started it — nor a window that closed on it.
	virtual void CancelFetch() override;


	// 🛑 `AddFilter` / `AddSort` / `AddGroup` STOOD HERE AND WROTE THE WRONG STORE. They pushed into
	// the composer's FLAT declared lines, which the render throws away the moment the composition has
	// settings of its own — so a script that sorted a generated report was silently ignored. Nobody
	// called them, which is the only reason it never surfaced.
	//
	// The cure is the one `AddResource` below already had: write into the DESCRIPTION. They come back
	// that way, or not at all — a door that quietly does nothing is worse than no door.
	//
	// (And the composer's flat lines are gone since: `Filter` and `Sort` write the reader's section,
	//  so the road that silently swallowed them no longer exists.)
	// A RESOURCE — the aggregate the levels fold (`SUM` over an amount, `COUNT` over a key). A
	// grouped composition with no resource has the shape of a report and none of its numbers.
	//
	// ⭐⭐ THIS IS THE DOOR FOR CODE, and it writes into the DESCRIPTION (Max, 2026-08-24: "the
	// runtime is a wrapper — it works on that structure and lets it be filled FROM CODE; that is what
	// it is for, and the UI works on the structure directly"). So the two sides never meet in the
	// middle: a script fills the description through this value, a window fills its own copy of the
	// description, and the composer is handed the result at a run.
	//
	// 🛑 THEY USED TO WRITE INTO THE RUNNING COMPOSER, which is filled FROM the description — so a
	// resource lived until the report was closed and never reached the file.
	//
	// ⭐ AND THEY SAY **RESOURCE**, which is what they write. They were the Total family over a store
	// called `m_resources` — one concept in two vocabularies, crossing inside every one of these
	// bodies. *Total* is the QUERY's word (`TOTALS agg… BY dim…`) and now lives only where the query
	// text is rendered (audit, 2026-08-24).
	void AddResource(const wxString& func, const wxString& path);
	bool SetResource(size_t idx, const wxString& func, const wxString& path);
	bool RemoveResource(size_t idx);
	size_t ResourceCount() const { return GetCompositionDesc().m_resources.size(); }
	bool   GetResourceAt(size_t idx, wxString& func, wxString& path) const;

	// --- ibValueSpreadsheetModel --------------------------------------------
	//
	// ⏳ WHAT IS GONE, AND WHY IT WAS NEVER A COMPOSITION'S: rows by view item, a column collection,
	// editable cells, view features, `RunComposerPage` — the whole LIST surface. A composition is
	// read ONCE and written into a SHEET; it has no page, no anchor and no current row (Max,
	// 2026-08-20: "showing a composition as a table — the dataview, the nodes — a spreadsheet
	// document has none of that at all"). What replaced them is the pair above: the model's own
	// fetch and Compose.
	//
	// The old `IsTableValue() == false` said the same thing from the other side and is no longer
	// needed either: a composition is not a table model to begin with now. (Nor is there a
	// GetSpreadsheetValue: the sheet is asked of the MODEL — GetSpreadsheetDocument — and there is one
	// answer for both kinds.)

	// (ActivateItem is gone with the list surface: a row of a composition is not clicked in a
	//  dataview any more. THE DRILL-DOWN DID NOT GO WITH IT — it never came through here: a composed
	//  cell carries its value as a document PARAMETER and the click ends in ibValue::ShowValue.)

	// Command STORE — two verbs of its own (Compose, Settings), then the SOURCE's own set.
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	// …and ONE way to run one: the control hands back an id it took from this very store.
	//
	// (The TABULAR entry — CallAsCommand with a dataview row context — is gone with the table model
	//  it belonged to. It did the same thing as this one, minus the row keys it could not supply.)
	virtual void CallAsModelCommand(const ibActionID& id, class ibBackendValueForm* srcForm) override;

	// (No Get*Fetch override: the fetch is the spreadsheet model's own — one run, one sheet.)

	// --- ibSourceDataObject (the composer IS a form data source) -------------
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override {
		const ibBackendQueryable* q = GetSourceQueryable();
		return q != nullptr ? q->GetSourceMetaObject() : nullptr;
	}
	virtual ibClassID GetSourceClassType() const override { return g_valueDataCompositionCLSID; }
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }
	virtual bool IsEmpty() const override { return false; }
	virtual ibUniqueKey GetGuid() const override;

	virtual const ibSourceExplorer* GetSourceExplorer() const override;

	// (No GetValueBySourceHop override: there is nothing to hop INTO. It bridged two bases when this
	//  was a table — a hop stepped into a ROW. A composition has no rows to step into; what it holds
	//  is a sheet, and a cell there carries its value as a document parameter. The base's "not a field
	//  here" is the truthful answer.)

	virtual wxString GetSourceCaption() const override;

	// READ — the metadata OF THE CHOSEN SOURCE, taken straight from the VALUE.
	// TERMINAL (no owner-walk), so a form's metadata may fall back here without recursing.
	virtual const ibMetaData* GetSourceMetaData() const override;
	// SELECTION — which config to PICK / resolve a source FROM: the owner form's config.
	virtual const ibMetaData* GetMetaData() const override;

	// --- ibPropertyObject + serialization ------------------------------------
	virtual wxString GetClassName() const override { return ibValue::GetClassName(); }
	virtual wxString GetObjectTypeName() const override { return GetClassName(); }
	virtual bool IsEditable() const override { return true; }
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;
	// ⭐ ONLY THE SETTINGS SHOW IN THE INSPECTOR (Max, 2026-08-19: "a composer has nothing but its
	// settings"). The SOURCE and the QUERY TEXT stay as properties — they are serialised, and a
	// generated report form sets them — but they are not offered beside "Settings...", because a
	// composition's source IS its query and its query is edited inside the settings window, on the
	// tab that exists for it. Two doors to one text, one of them a bare string field, is how the two
	// drift. (A dynamic list keeps its Source: it stands on a main table, and its settings work over
	// that table — which is exactly why it is the list that shows it and the composition that does not.)
	virtual void OnPropertyRefresh() override;
	virtual bool ReadProperty(const ibDataNode& node) override;
	virtual bool WriteProperty(ibDataNode& node) const override;

	// ⭐ THE TWO HALVES OF THE TRANSLATION, live <-> description. What a saved composition CONSISTS OF
	// is stated once, in composition/compositionDescription.h, for a list and a report alike; these
	// two only carry the pieces across. Public because a composition is also SAVED TO A FILE: describe
	// it, hand the node to a provider (ibJsonProvider for JSON, ibBinaryProvider for bytes), and read
	// it back the same way.
	// ⚠ BY REFERENCE, always — the shape ibVariantDataAttribute::GetTypeDesc has. The description is
	// what this composition IS, not a copy made on request: a caller edits it in place and saves it,
	// and a returned copy would be a second answer going stale the moment it was handed over.
	// …AND IT LIVES IN THE "Settings" PROPERTY, exactly as a list's does in its own — the value is got
	// FROM there by reference and SET back there as an argument, the way a tabular section's value is
	// reached in a value table. No field beside the property; one place, and it is the one that saves.
	ibCompositionDescription& GetCompositionDesc() { return m_propertySettings->GetValueAsCompositionDesc(); }
	const ibCompositionDescription& GetCompositionDesc() const { return m_propertySettings->GetValueAsCompositionDesc(); }
	void SetCompositionDesc(const ibCompositionDescription& desc) { m_propertySettings->SetValue(desc); }

	// ⭐⭐ THE USER'S SETTING — THE SAME PAIR A LIST CARRIES, on the same composer, because they are
	// the same two calls (Max, 2026-08-23: "one pair, identical for reports and for lists").
	//
	// ⚠ WHAT DIFFERS IS WHEN IT SHOWS. A LIST re-reads at once — the caller refetches and the rows
	// come back narrowed. A REPORT does not: its sheet is the one that was BUILT, so the setting
	// takes effect the next time somebody composes, and then it decides the whole output — instead
	// of the two or three tables the composer used to produce, it produces what the user set up.
	// THE READER'S SECTION, PLAINLY — this forwards it and answers nothing else.
	//
	// ⭐ THE QUESTION "WHICH ONE IS IN FORCE" MOVED TO THE COMPOSER (SettingsInForce), because that is
	// where both sections live: the author's, loaded from the schema when the source is built, and
	// the reader's, only ever set. It used to be answered here — and then again at every window that
	// opened one, each reaching for the description on its own when this came back empty.
	const ibSettingsDescription& GetUserSettingsDesc() const { return m_composer.GetUserSettingsDesc(); }
	void SetUserSettingsDesc(const ibSettingsDescription& settings) { m_composer.SetUserSettingsDesc(settings); }

	// (⛔ NO SAVED-SETTINGS ADDRESS HERE. It stood here as `m_settingsOwner` and it
	//  was the wrong storey: what a person arranges belongs to the CONTROL they
	//  arranged it on, and the control is what knows its own guid, its own form and
	//  the moment it was opened. Max, 2026-08-26: *"you do not need to keep that
	//  form anywhere but in the controls"*. The composition just composes.)

	// (THERE IS NO "APPLY". Everyone reads the description THROUGH THE REFERENCE above, so a change
	//  made at runtime is already there. A COMMIT exists only on the interface side, and it is a copy
	//  that goes back into the property and replaces the one that was there.)

	// --- script member surface (Filter/Order/Group/Settings + Refresh) -------
	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

private:

	// ⭐⭐ THE COMPOSING BODY — PRIVATE, AND THAT IS THE WHOLE OF IT (Max, 2026-08-24: "this is now
	// just the composer's internal body, the composer decides in there"). Reading, laying out and
	// writing the sheet is one routine; where the result LANDS is the only thing that varies, so
	// the two public doors are both this call with a different target:
	//
	//   Compose()               — into the sheet this model holds, and publish it (the box's command)
	//   CallAsProc "Compose(Doc)" — into the document the script handed over
	//
	// Nothing outside decides HOW it composes. Not even the settings: they are already on the
	// composer by the time this runs, put there when the source was built or when somebody accepted
	// the settings window. A target is a place to write, not an instruction.
	bool Compose(class ibBackendSpreadsheetObject* target);

	// Rebuild the columns + composer from the current source and query text.
	void RebuildSource();

	// ⭐⭐ ASKING IS WHAT RESOLVES — and RUNNING is the asking. Reading a description stores what a
	// composition IS and works out nothing (§ "the refresh" in docs/description-serialization.md), so
	// a report opened and composed straight away had a composer that had never been shown the query:
	// "Composer: no source is set" on the FIRST Generate, and it worked on the second because opening
	// the settings window had applied the source in between (Max, 2026-08-24: "the source is known —
	// that is not true").
	//
	// The reading side of that rule was simply never written. Asked at both entrances — Compose and
	// the paged fetch — and it does nothing when the composer is already built for this very text,
	// which is what the marker below records.
	void EnsureSourceBuilt();
	wxString m_sourceBuiltFor;   // the query text RebuildSource last ran on; empty = never built

	// DROP the filter / sort / grouping lines whose field the composition no longer
	// has — by RESOLUTION after every rebuild, never by chasing the change.
	void PruneUnresolvedSettings();

	// (THE L5 STORE MOVED TO THE BASE. Two things wear the name and they nest: the value a person
	//  holds is this SHELL — settings, variants, parameters — and inside it sits the composer that
	//  builds the query for the driver. That composer is every sheet model's now, empty on the ones
	//  that never read, so this class holds nothing of its own here.)

	// The rented run, KEPT so a read cannot outlive the composition that started it — the destructor
	// waits it out. One slot: there is one sheet to fill.
	std::shared_ptr<class ibBackgroundRun> m_fetchRun;

	// (The sheet lives on the model base — ibValueSpreadsheetModel::m_spreadsheetDoc, a
	//  wxObjectDataPtr: the backend document carries a refcount of its OWN, parallel to the value
	//  refcount, and is passed around by that handle.)

	// WHAT THE QUERY PRODUCES, rebuilt on every RebuildSource — which is what makes
	// the field pickers follow the text.
	std::vector<ibQueryLowering::OutputColumn> m_querySchema;
	wxString                                   m_queryError;   // the ENGINE's words, verbatim

	// The SOURCE config, STORED (what GetSourceMetaData returns) — captured from the
	// picked queryable's metaobject in RebuildSource. Held so the READ never re-resolves
	// the queryable through the owner (which walks the form → recurses). Non-owning.

	const ibMetaData* m_sourceMetaData = nullptr;
	// (THE VARIANTS ARE NOT HERE. A live vector of them stood beside desc.m_variants, holding the
	//  structure and the parameter values while the description held the name and the settings — one
	//  variant split across two stores, and only one of them ever reached the file, which is why a
	//  structure edited in the designer came back the way it was. A VARIANT IS PART OF THE REPORT:
	//  it lives in the description, whole. See ibVariantDescription.)

	// The first variant, made on construction so the invariant "there is always one" holds from the
	// first moment rather than from the first window.
	void EnsureVariant();

	// (A VARIANT'S PARAMETER RECORDS used to be written here, to and from its node. They travel
	//  through ibParameterDescription now, like every other part — Describe() fills it, Apply()
	//  reads it back, and what the node looks like is not this class's business any more.)

	// THE PARAMETERS THEMSELVES. Kept in the order the query mentions them (that is the order a
	// person reads them in), with hand-made ones appended after.
	// ⭐⭐ A PARAMETER **IS** ITS DESCRIPTION. `struct ibCompositionParameter` stood here with the same
	// six fields — name, value, declared type, expression, user-settable, came-from-the-query — and a
	// live vector of them stood beside the description's. So a parameter existed twice and only the
	// copy in the description was ever written: closing the report and opening it again showed none
	// of them (Max, 2026-08-24: "the parameters have the same illness as the resources").
	//
	// (The DECLARED TYPE, Max 2026-08-19: "add a separate Type column — look at how the type
	//  description works". Empty means "whatever the expression produces": a parameter has no type of
	//  its own until somebody says otherwise, and then the value is adjusted to it.)
	using ibCompositionParameter = ibParameterDescription;

	// THE PARAMETERS THEMSELVES — in the description, where they are saved from. A RUN settles the
	// evaluated values, which is why this is writable on a const object: asked straight of the
	// PROPERTY, whose own accessor is const and hands back the description to work on. (That is the
	// same reason the live vector here used to be `mutable`.)
	std::vector<ibCompositionParameter>& Parameters() const {
		return m_propertySettings->GetValueAsCompositionDesc().m_parameters;
	}

	// Re-read the parameters the TEXT asks for: add what is new, drop the auto ones the text no
	// longer mentions, keep the hand-made ones and everything already filled in.
	void SyncParametersWithQuery();

	// --- property surface — these SURFACE onto the form attribute ------------
	ibPropertyCategory*      m_categoryComposer = ibPropertyObject::CreatePropertyCategory(wxT("DataComposition"), _("Data composer"));
	// Source = the picked queryable; read through the GetSourceQueryable() facade.
	ibPropertyDynamicSource* m_propertySource   = ibPropertyObject::CreateProperty<ibPropertyDynamicSource>(m_categoryComposer, wxT("Source"), _("Source"));
	// "Settings..." — the action property, and a TYPE of its own rather than the dynamic list's:
	// the frontend property is matched by this type, so opening the composition's window instead of
	// the list's needs no branch anywhere (Max, 2026-08-20). The frontend reaches this object
	// through the property's owner.
	ibPropertyDataComposition* m_propertySettings = ibPropertyObject::CreateProperty<ibPropertyDataComposition>(m_categoryComposer, wxT("Settings"), _("Settings"));

	// WHICH COMPOSER'S SETTINGS THE BASE HOLDS FOR THIS ONE. Empty on a composition
	// built in code or in the designer — those save nothing, which is right: there
	// is no reader whose settings they would be.
	//
	// NOT SERIALISED, deliberately. It is an identity, not a setting: it is stated
	// again every time the object is prepared, and a copy of the description carried
	// into another composer must not arrive claiming to be that composer.
};

#endif // __VALUE_DATA_COMPOSITION_H__
