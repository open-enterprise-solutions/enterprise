#ifndef __VALUE_DATA_COMPOSITION_H__
#define __VALUE_DATA_COMPOSITION_H__

#include "backend/spreadsheetModel.h"             // ibValueSpreadsheetModel — the base a composition IS
#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject
#include "backend/composition/dataComposer.h"        // L5 — ibDataDBComposer
#include "backend/composition/listFilter.h"          // ibValueListSettings
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
//   * ibValueListSettings (the ONE settings object — filter TREE, sort, grouping),
//     held by the base model and edited by the SAME settings dialog;
//   * the L5 composer (GetModelComposer) and the base fetch (RunComposerPage) —
//     fetch lives ONLY in the parent;
//   * the source descriptor, so a row still knows how to open itself.
//
// What it does NOT carry (a report is not a picker): choice mode, folder-select,
// the hierarchy-column display. A tree here comes from GROUPING, never from a
// parent column.
//
// ⚠ NEXT STEP, held in mind while shaping this: the grouping is a FLAT ordered
// list today (ibValueGroupList — the same one the list uses). It becomes the
// output TREE — a node is a table, a table carries rows and, once cross-tables
// land, columns as well. Nothing here should assume "one flat grouping": read it
// through GetListSettings()->GetGroup(), never cache the order.
// ---------------------------------------------------------------------------
// ⭐⭐ A COMPOSER IS A SPREADSHEET MODEL, NOT A TABLE MODEL (Max, 2026-08-20: "ibValueModelCursor is
// rubbish here — I asked for the spreadsheet document's model; it got there by being copied from the
// dynamic list").
//
// The cursor model is a LIST: it pages, it has an anchor and a direction, it hands out rows and view
// nodes and column collections. None of that is what a composition is. A composition READS ONCE and
// writes a SHEET — which is exactly what ibValueSpreadsheetModel owns (its own fetch, the L5 store,
// the Compose verb), and what a spreadsheet document is the other half of.
class BACKEND_API ibValueDataComposition : public ibValueSpreadsheetModel, public ibSourceDataObject, public ibPropertyObject {
public:

	// The source may be passed here — null means "set it later" (SetSource).
	ibValueDataComposition(const ibBackendQueryable* queryable = nullptr);
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
	wxString GetQueryText() const { return m_propertyQuery->GetValueAsString(); }
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
	// being two reports. Parameters join the snapshot when the composition grows them.
	//
	// 🛑 THERE IS ALWAYS AT LEAST ONE. A composition with no variant would have no settings at all,
	// so the list starts with one and the last one cannot be removed — the invariant is held HERE,
	// in the store, not by whichever window happens to be open.
	//
	// The ACTIVE variant is the one the composer (the store the fetch reads) currently holds. That
	// is what makes activation cheap and makes the settings window need no knowledge of variants
	// beyond "capture what I edited, then switch".
	size_t   VariantCount() const { return m_variants.size(); }
	wxString GetVariantName(size_t idx) const;
	bool     SetVariantName(size_t idx, const wxString& name);
	size_t   GetActiveVariant() const { return m_activeVariant; }
	// Make `idx` the settings the composer holds. The previous active variant is NOT captured here:
	// a caller that has been editing says so first (CaptureActiveVariant), because only it knows
	// whether the edits are meant to be kept.
	bool     SetActiveVariant(size_t idx);
	// Re-read the ACTIVE variant's snapshot FROM the composer — the settings window edits the
	// composer, so this is how an edit reaches the variant it belongs to.
	void     CaptureActiveVariant();
	// ADD: empty, or a COPY of an existing variant (copyFrom = its index) — "you can add one, you
	// can copy an existing one; it copies the groupings, filters, sorts and so on". Returns the new
	// variant's index; it is NOT activated (the caller decides).
	size_t   AddVariant(const wxString& name, int copyFrom = wxNOT_FOUND);
	// REMOVE — refuses the last one. When the active variant goes, the one before it becomes active.
	bool     RemoveVariant(size_t idx);

	// The variants as a NODE — the same door serialisation uses, so a window can snapshot the whole
	// set on open and put it back on Cancel without knowing what is inside.
	void WriteVariants(ibDataNode& node) const;
	// The parameters travel with the settings — value, expression, declared type, who fills it.
	void WriteParameters(ibDataNode& node) const;
	void ReadParameters(const ibDataNode& node);
	bool ReadVariants(const ibDataNode& node);
	// THE RESOURCES — written nowhere at all before 2026-08-20, so a saved report came back without
	// its numbers. Composition-level, beside the parameters: today one store holds them for every
	// variant, and the file says exactly that rather than claiming a snapshot nobody takes.
	void WriteTotals(ibDataNode& node) const;
	void ReadTotals(const ibDataNode& node);

	// --- parameters: what the query asks for, and who fills it in --------------
	//
	// ⭐ TWO WAYS IN, ONE LIST (Max, 2026-08-19): a parameter is either written into the query text
	// (`&Period` — the lexer finds it) or added BY HAND. Both live in the same list; the difference
	// is only that an auto one disappears when the text stops mentioning it, and a hand-made one
	// stays until it is removed. Nobody has to keep two lists in step.
	//
	// ⭐ AND TWO ORTHOGONAL QUESTIONS ABOUT EACH — kept apart from the first day, because folding
	// them into one field is exactly how 1C ends up with "value / available to user / include in
	// settings" and nobody can say which overrides which:
	//   * WHAT FILLS IT — a fixed value, or an EXPRESSION evaluated before the query runs.
	//   * WHO FILLS IT — the author fixes it, or it is handed to the user (quick settings).
	//
	// An expression is legitimate here in a way a scripted FIELD is not: it is evaluated ONCE, before
	// the read, and what reaches the query is a plain value — so `CurrentDate()` or a call into a
	// common module costs nothing per row and breaks no server-side paging. (The page cache signs
	// itself with the EVALUATED value, or "today" would be one signature all day.)
	size_t   ParameterCount() const { return m_parameters.size(); }
	wxString GetParameterName(size_t idx) const;
	ibValue  GetParameterValue(size_t idx) const;
	wxString GetParameterExpression(size_t idx) const;
	bool     IsParameterUserSettable(size_t idx) const;
	bool     IsParameterFromQuery(size_t idx) const;

	bool SetParameterValue(size_t idx, const ibValue& value);
	bool SetParameterExpression(size_t idx, const wxString& expression);
	bool SetParameterUserSettable(size_t idx, bool settable);
	// The DECLARED type — what the value is adjusted to. Empty = the expression decides.
	const ibTypeDescription& GetParameterType(size_t idx) const;
	bool SetParameterType(size_t idx, const ibTypeDescription& type);
	// A HAND-MADE parameter — the query may not mention it yet (it is being written), or it may be
	// EVERY PARAMETER AND WHAT IT CURRENTLY HOLDS — the map the engine takes.
	//
	// ⭐ ONE DOOR for both uses: DESCRIBING the query (which resolves its names and would otherwise
	// report an unfilled `&Ref` as "parameter is not set" while the query is still being written) and
	// RUNNING it. An unfilled parameter arrives as an EMPTY value rather than being left out: empty
	// is an answer, and the SHAPE of the result never depends on it.
	std::map<wxString, ibValue> ParameterValues() const;

	// EVALUATE THE EXPRESSIONS — once, before the read. `Evaluate` is the language.s own door, so a
	// parameter may hold a call into a common module; the RESULT decides the parameter.s type.
	void EvaluateParameters() const;

	// ⭐ SETTLE THE PARAMETERS AT THE MOMENT THE QUERY RUNS (Max, 2026-08-19: "at the moment you
	// execute the query, if there is an expression there, you evaluate it through our script — quite
	// elegant"). Not when the settings are edited: `CurrentDate()` has to mean the day the report is
	// built, not the day somebody configured it. And not inside Compose alone: a composition is a
	// MODEL as well, so a fetch reads it too, and a parameter filled only on the report path would
	// look like "works in the report, empty in the list".
	//
	// Const because a fetch is const — what changes is the parameter's cached value and the
	// composer's parameter map, which are the state a run is entitled to settle.
	void PrepareParametersForRun() const;
	// there for a common module to read. Returns its index; an existing name is not duplicated.
	size_t AddParameter(const wxString& name);
	// Removes a HAND-MADE one. An auto parameter refuses: it is in the query text, and removing it
	// here would put the list back the moment the text is re-read — a button that undoes itself.
	bool RemoveParameter(size_t idx);

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

	// …AND INTO A DOCUMENT SOMEBODY ELSE OWNS — the script's `Compose(Document)`. The same read and
	// the same layout; only where the result lands differs, so there is ONE composing routine and no
	// copy of a finished sheet afterwards.
	bool Compose(class ibBackendSpreadsheetObject* target);

	// --- the live settings, and the store behind them -------------------------
	//
	// ⭐ THE SAME TYPE A LIST CARRIES, over this composition's OWN store: Filter and Sort written
	// STRAIGHT into the composer, so a script's `Report.Composer1.Filter.Add(…)`, the settings window
	// and the read all speak to one place. That pair is ALL the two worlds share — the fold is the
	// composition's own and lives in its structure (Max, 2026-08-20).
	ibValueListSettings* GetListSettings() const { return m_listSettings; }

	// THE STORE. Public because the settings window edits it and the facade writes through it — the
	// shell is what a person holds, this is what it holds inside.
	ibDataDBComposer& GetModelComposer() const { return m_composer; }

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


	// Add a FILTER / SORT / GROUPING programmatically — the same doors the list has,
	// so a generated composer (a standard report form) can be set up from the backend.
	void AddFilter(const wxString& path, const wxString& op, const ibValue& value);
	void AddSort(const wxString& path, bool ascending = true);
	void AddGroup(const wxString& path, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements);
	// A RESOURCE — the aggregate the levels fold (`SUM` over an amount, `COUNT` over a key). A
	// grouped composition with no resource has the shape of a report and none of its numbers.
	void AddTotal(const wxString& func, const wxString& path);
	// …AND THE OTHER TWO ACTS ON ONE. They existed only on the STORE, so every window that changed a
	// resource reached past the composition into it — which is precisely how a change went
	// unannounced: the composition never learned it happened. Same door, same rule as AddTotal.
	bool SetTotal(size_t idx, const wxString& func, const wxString& path);
	bool RemoveTotal(size_t idx);

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

	// --- script member surface (Filter/Order/Group/Settings + Refresh) -------
	void FillMembers(ibMemberTable& helper) const;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

private:

	// Rebuild the columns + composer from the current source and query text.
	void RebuildSource();

	// DROP the filter / sort / grouping lines whose field the composition no longer
	// has — by RESOLUTION after every rebuild, never by chasing the change.
	void PruneUnresolvedSettings();

	// ⭐ THE ENGINE'S COMPOSER, INSIDE THIS ONE. Two things wear the name and they nest: the value a
	// person holds is this SHELL — settings, variants, parameters — and inside it sits the L5 store
	// that builds the query for the driver (Max, 2026-08-20). It is NOT on the model base: filling a
	// sheet is not the same as reading one, and a hand-filled document has no query at all.
	//
	// Mutable: the store is asked from const readers (a settings window reading back what it holds).
	mutable ibDataDBComposer m_composer;

	// The LIVE settings over that store — a facade, not a second copy. (The variants below hold
	// BUFFER-mode ones: those are snapshots, which is the opposite arrangement on purpose.)
	ibValuePtr<ibValueListSettings> m_listSettings;

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

	// THE VARIANTS THEMSELVES. Each holds a BUFFER-mode settings object — own storage, not a facade
	// over the composer — because a snapshot that wrote through to the composer would not be a
	// snapshot. Never empty: the constructor puts the first one in.
	// WHAT A PARAMETER IS WORTH IN THIS VARIANT — its value and the expression that produces one.
	// Only those two: the parameter's TYPE, whether the user may set it and whether the query asked
	// for it are facts about the SCHEMA, the same in every variant, and a variant that carried them
	// would be a second place stating them.
	struct ibVariantParameter {
		wxString m_name;
		ibValue  m_value;
		wxString m_expression;
	};

	struct ibCompositionVariant {
		wxString                        m_name;
		ibValuePtr<ibValueListSettings> m_settings;
		// ⭐ AND ITS STRUCTURE — the outputs, their levels and the fields inside them. A variant is
		// a whole way of showing the data, and the flat settings above cannot describe a level made
		// of several fields, a second output, or an axis of columns. Captured when the variant is
		// left, applied when it is entered, written to the file with the rest of it.
		std::vector<ibDataComposer::Output> m_structure;
		// ⭐ …AND WHAT ITS PARAMETERS WERE SET TO. "Sales for last month" and "sales for the year"
		// are one structure and two periods, so a variant that did not carry them was not a whole
		// way of showing the data — switching one changed the groupings and left the period behind.
		//
		// Held BY NAME and applied as a merge, never as a replacement of the list: the list itself
		// is derived from the query text (SyncParametersWithQuery adds what it mentions and drops
		// the auto ones it stopped mentioning), so a variant restoring it wholesale would resurrect
		// parameters the query no longer asks for. A name that is no longer there is not matched.
		std::vector<ibVariantParameter> m_parameters;
	};
	std::vector<ibCompositionVariant> m_variants;
	size_t                            m_activeVariant = 0;

	// Snapshot <-> composer, both directions, in one place — activation and capture are the same
	// two doors the settings window's OK already goes through.
	void ApplyActiveVariant();
	// EVERY LEVEL'S CONDITION, REBUILT FROM ITS TREE. What a level SAVES is the tree a person wrote;
	// what the engine reads is an expression derived from it. Run wherever levels arrive from
	// outside (a file, a variant switch) — a level edited in the window commits both at once.
	void RebuildLevelFilters();
	// The first variant, made on construction so the invariant "there is always one" holds from the
	// first moment rather than from the first window.
	void EnsureVariant();
	// WHAT THE LIVE PARAMETERS ARE WORTH, said in a variant's terms. One spelling, because three
	// places ask the same question: the capture, a copy taken from the active variant, and the file.
	std::vector<ibVariantParameter> LiveParameterSnapshot() const;

	// A VARIANT'S PARAMETER RECORDS, to and from its node. Members rather than the file-local
	// helpers their neighbours (WriteStructure / ReadStructure) are, for one reason: what they carry
	// is private to this class, and a free function cannot name it.
	static void WriteVariantParameters(ibDataNode& node, const std::vector<ibVariantParameter>& parameters);
	static void ReadVariantParameters(const ibDataNode& node, std::vector<ibVariantParameter>& parameters);

	// THE PARAMETERS THEMSELVES. Kept in the order the query mentions them (that is the order a
	// person reads them in), with hand-made ones appended after.
	struct ibCompositionParameter {
		wxString m_name;
		ibValue  m_value;          // what it holds when no expression is given
		// ⭐ THE DECLARED TYPE (Max, 2026-08-19: "add a separate Type column — look at how the type
		// description works"). Empty means "whatever the expression produces": a parameter has no type
		// of its own until somebody says otherwise, and then the value is adjusted to it.
		ibTypeDescription m_type;
		wxString m_expression;     // non-empty: evaluated before the read, and the RESULT is the value
		bool     m_userSettable = false;   // handed to the user rather than fixed by the author
		bool     m_fromQuery = false;      // found in the text (auto) rather than added by hand
	};
	mutable std::vector<ibCompositionParameter> m_parameters;   // a RUN settles the evaluated values

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
	// The query text — ALWAYS present once a source is picked (no on/off flag: a
	// composer without a query has nothing to compose).
	ibPropertyString*        m_propertyQuery    = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryComposer, wxT("Query"), _("Query text"), wxEmptyString);
};

#endif // __VALUE_DATA_COMPOSITION_H__
