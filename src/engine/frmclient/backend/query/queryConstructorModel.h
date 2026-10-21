#ifndef _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__
#define _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__

// THE QUERY CONSTRUCTOR'S MODEL — the engine's (backend/query/queryConstructorModel.h): what may go into a query, its
// sources and their fields, asked of the configuration — the server's. What the client's windows take of it is the
// answer: the fields a query text offers (ibQueryConstructorField, the core's), as the server described them with the
// window's question.

#include "frmclient/backend/backend_core.h"   // FRMCLIENT_API
#include "core/query/queryConstructorField.h"   // ibQueryConstructorField — one field, as every window reads it
#include "core/query/queryAST.h"                 // ibQuerySource / ibQueryPackage — what the model is asked about
#include "core/description/typeDescription.h"    // ibTypeDescription — what a field or a parameter holds
#include "queryable.h"                            // ibSourceMetaDataScope, ibPeriodUnits — the desktop's header brings them (queryableFactory.h)
#include "frmclient/backend/compiler/value.h"     // ibValue — likewise

#include <memory>
#include <vector>

class ibMetaData;

// ⭐⭐ WHAT FIELDS A QUERY TEXT OFFERS — on the client, as the server said: parsing the text and asking the configuration
// are its, so it described the fields with the window's question (Fields — a field's name, its source's caption, its
// type, whether the options leave it). A configuration that is a tab's (a composer's panel, ibMetaData::GetTabId) asks
// that tab for `text` — the server parses it; `error` is why it offers none. Otherwise `text` is not read: the fields
// are the question's.
FRMCLIENT_API std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& text,
	const ibMetaData* metaData, wxString* error = nullptr);

// …AND WHAT A TAB'S VIEW ALREADY SAID OF A TEXT — a composer's tab draws its query's Fields and References with the view
// (and Error): kept here by the tab, so the panel's read of that same text asks nothing; forgotten with the tab.
class ibProtocolNode;
FRMCLIENT_API void ibQueryFieldsKnown(long long tabId, const wxString& text, const ibProtocolNode& answer);
FRMCLIENT_API void ibQueryFieldsForget(long long tabId);

// A DYNAMIC LIST'S FIELDS on a tab — those of its main table (wxNOT_FOUND: none) and what its query adds, as the desktop's
// list settings window reads them; asked of the tab (fetch {Tab, Request: {Kind: Source, Id, Text}}) once per main table
// and text. Off a tab: none.
FRMCLIENT_API std::vector<struct ibSettingsPlainField> ibListSourceFields(ibMetaID mainTable, const wxString& text,
	const ibMetaData* metaData, wxString* error = nullptr);

// ⭐⭐ THE DESKTOP'S MODEL, UNDER ITS OWN NAME — the windows of win/dlgs/queryConstructor are the desktop's, ported whole,
// and they ask this class what they ask there. What only the configuration answers is asked of the tab the window was
// opened from (ibProtocol::ibView::ibQueryConstructor): the sources, a source's fields, a virtual table's parameters —
// and a reference's fields one level down, the composer's Expand. Everything the desktop's model DERIVES from those
// (qualified fields, the walk of a path, the fields of a select) is the desktop's code, run here over the answers.
// What the package itself answers (its temporary tables, the roles it says) is read here, as the desktop reads it.
// ONE QUESTION PER SOURCE: the answers are kept for the model's life — the window's.

// WHAT ARGUMENTS A VIRTUAL TABLE TAKES — the desktop's (backend/query/queryableFactory.h), as the server describes it.
struct ibQuerySourceParameter
{
	wxString          m_name;                 // Period / Begin / End / Periodicity / Condition
	bool              m_condition = false;    // false = value expression, true = predicate
	ibTypeDescription m_type;                 // VALUE only: what it holds — and therefore how it is edited
	bool              m_required = false;

	// ⭐ A CLOSED SET OF VALUES, WHEN THE ARGUMENT HAS ONE — the periodicity a turnover rolls up to,
	// the way a balance-and-turnovers table is filled. Declared BY THE SOURCE, because the source is
	// the only one that knows what it accepts; a window that carried the list would be holding a
	// second copy of the source's own vocabulary, and the two would part company on the first change.
	//
	// Empty means "any expression the language can write" — a moment, a parameter, a computation.
	// Non-empty makes the argument a CHOICE, and the editor shows exactly these and nothing else.
	std::vector<wxString> m_choices;

	// ⭐ WHAT THIS ARGUMENT DECIDES, in one line, when the name alone is not enough — declared by
	// the source for the same reason the choices are: it is the only one that knows.
	//
	// 🛑 `Periodicity` is why this exists. The word names two different things one floor apart: the
	// register's STORED grain (a metadata property, whose words are "Non periodic / Within second /
	// Within day") and the granularity of a READING (this, whose words are Period / Recorder /
	// Day / Month / …). A newcomer reads one list and writes it into the other, and both refusals
	// are correct and neither says the two are different questions (measured over MCP, 2026-09-03).
	// Empty for an argument whose name says it all.
	wxString m_description;

	// ⭐⭐ THE SOURCE CONSUMES THIS CONDITION ITSELF — it is not folded into the WHERE around it.
	//
	// An ordinary condition slot is sugar: the predicate written there is ANDed into the query's own
	// WHERE, and the source never learns of it. That is right when the condition only SELECTS rows.
	// It is wrong when the source has to ACT on it — an accounting register asked for accounts «in
	// hierarchy» reports the subordinate accounts UNDER the one that was named, and a filter applied
	// around the reading cannot fold anything: it can only remove rows the reading already produced.
	//
	// Set, the lowering resolves the predicate against `GetConditionScope()` and hands it to
	// `CreateQueryable` by slot position instead of into the WHERE. Nothing else changes: the source
	// is then the only place that condition exists, which is what makes the fold possible at all.
	bool m_consumedBySource = false;

	// AND WHAT THE SOURCE USES WHEN THE ARGUMENT IS LEFT OUT. Shown to the author rather than left to
	// be guessed: an empty box that quietly means "Auto" teaches nothing, and the first surprise
	// arrives when somebody sets it explicitly and the result changes.
	wxString              m_default;

};

// One entry of the constructor's left-hand tree.
struct ibQueryConstructorSource
{
	// The dotted path a query names it by: {"Catalog","Products"} or {"AccumulationRegister","Goods","Balance"}
	// for a virtual table, and a SINGLE segment for a temp table the package made (it has no metaclass —
	// it is what a previous statement left).
	std::vector<wxString> m_path;
	wxString              m_presentation;   // what a person reads (the synonym where there is one)
	bool                  m_temp = false;   // declared by the package being edited, not by the metadata
	// ⭐ …AND WHICH OF THE TWO IT IS. A package declares two kinds of source of its own: a TEMPORARY
	// TABLE (`INTO`, rows put away for later statements) and a NAMED RESULT (`ONTO`, a result a
	// later statement may read — a query result link). They behave the same in a FROM, so they share
	// m_temp; they are different things to a person, so the catalogue shows them apart.
	bool                  m_namedResult = false;

	// INLINE, not exported: the struct itself carries no BACKEND_API (it is a plain value the
	// frontend copies), so a definition in the .cpp would be invisible across the DLL — which is
	// exactly what the test link found.
	wxString Text() const {                 // the path joined with dots — what goes into the AST
		wxString out;
		for (size_t i = 0; i < m_path.size(); ++i) {
			if (i > 0) out += wxT(".");
			out += m_path[i];
		}
		return out;
	}
};

class FRMCLIENT_API ibQueryConstructorModel
{
public:
	// `metaData` — the config the query runs on behalf of (its factory resolves the sources).
	// Null falls back to the global factory, which is the right answer with no config open.
	explicit ibQueryConstructorModel(const ibMetaData* metaData = nullptr);

	// EVERY source a query may name, grouped by nothing — the caller groups by m_path[0], which is
	// the metaclass kind, so the tree's top level is whatever the factory actually holds.
	std::vector<ibQueryConstructorSource> GetSources() const;

	// A kind's class picture, as the server named it (the Sources answer's Kinds); empty: none.
	wxString KindPicture(const wxString& kind) const;

	// The desktop's ibQueryLowering::CheckNames and UngroupedProjections, answered by the server (request Check).
	void CheckNames(const ibQueryPackage& package) const;
	std::vector<ibQueryAstExprPtr> UngroupedProjections(const ibQueryPackage& package, size_t statement,
	                                                    int unionBranch) const;

	// WHAT ARGUMENTS THIS SOURCE TAKES, in order — empty for an ordinary table, non-empty for a
	// virtual one (a register's balance / turnovers, an information register's slice). The shell
	// shows a parameter window only when this answers something, so a table that takes none never
	// grows a menu item about parameters it does not have.
	std::vector<ibQuerySourceParameter> GetSourceParameters(const ibQuerySource& source) const;

	// THE FIELDS A CONDITION PARAMETER MAY NAME — asked of the source, not decided here. A balance
	// answers with its dimensions only (a resource it folded cannot be filtered before folding); a
	// slice answers with everything it carries. Same shape as GetFields, so the expression editor
	// takes it without knowing which source it came from.
	// Fields admissible in a condition slot. `slot` empty = the general `Condition`; a named slot may
	// narrow it (an accounting register's account conditions admit ACCOUNTS only — the source consumes
	// them and folds by them, so a leaf about anything else cannot be applied at all).
	std::vector<ibQueryConstructorField> GetConditionFields(const ibQuerySource& source,
	                                                       const wxString& slot = wxEmptyString) const;

	// The temp tables `package` has DECLARED so far, in the order they are made. These belong to the
	// package being edited, not to the metadata — which is why they are a separate question with the
	// package as its argument, and why the tree is fed from two places (docs §5b).
	// `beforeStatement` — only the statements BEFORE this index count: statement 2 cannot select from
	// a table statement 5 will make.
	static std::vector<ibQueryConstructorSource> GetTempSources(const ibQueryPackage& package,
	                                                            size_t beforeStatement);

	// THE FIELDS OF A SOURCE — one question, and the source decides how it is answered:
	// a nested table (m_subquery set) answers with its own projections, a real source through its
	// descriptor's FillSourceExplorer, a temp table with the projections of the statement that made it.
	// `package` + `beforeStatement` let a temp-table source find its maker; pass an empty package when
	// there is none.
	std::vector<ibQueryConstructorField> GetFields(const ibQuerySource& source,
	                                               const ibQueryPackage& package,
	                                               size_t beforeStatement) const;

	// The fields a chosen source EXPOSES under an alias — the same list as above, but each name
	// prefixed with the alias the query gave the source (`p.Code`), which is what a projection,
	// a condition or an ordering actually writes when more than one table is in play.
	std::vector<ibQueryConstructorField> GetQualifiedFields(const ibQuerySource& source,
	                                                        const ibQueryPackage& package,
	                                                        size_t beforeStatement) const;

	// ONE LEVEL DOWN A REFERENCE — the fields of what `clsid` points AT. This is the model half of
	// the dot walk (`Supplier.Region.Country`): the language has always been able to write it and
	// the lowering has always been able to run it (ExpandDotWalkJoins / TotalByDotWalk), but the
	// constructor had no way to OFFER it, so a reference was a dead leaf in every field tree.
	//
	// No lookup table and no new registry: a reference's clsid is CONSTRUCTIVE — its body IS the
	// metaID of what it refers to (docs, clsid.h §dynamic) — so the referenced source is the one
	// the factory already has under that id. A type added tomorrow unfolds the day it registers.
	//
	// Empty for anything that is not a reference at all.
	//
	// ⭐⭐ A COMPOSITE REFERENCE IS WALKED TOO, and this used to say the opposite: that the lowering
	// refuses a composite mid-segment, so offering the walk would be offering something the engine
	// then rejects. That stopped being true and the note outlived the code — ResolveReferenceTargets
	// yields one queryable per alternative, and the lowering resolves a representative chain while
	// the provider BRANCHES per type, one join sub-tree each, COALESCING the leaf
	// (queryLowering.cpp). Measured 2026-09-07: `SELECT R.Recorder.Date FROM
	// AccumulationRegister.GoodsInWarehouses AS R` returns the dates, while this model was still
	// calling Recorder a leaf and query_fields was refusing the path.
	//
	// So the overload below takes the WHOLE type and answers with the UNION of its alternatives,
	// SAME NAMES COLLAPSED — which is what the query itself does with them: `Recorder.Date` is one
	// column whichever document the row points at.
	//
	// `sourceLabel` — WHICH TABLE THE WALK STARTED FROM, carried down every level. A field five hops
	// inside a reference still belongs to the table it hangs off, and the trees group by that; a
	// walked field with no source landed in a group of its own, away from its own table. The caller
	// knows it (it is walking a node of that table), so it hands it in rather than being asked to
	// re-derive it from a dotted path, which is exactly what a dot-walk makes ambiguous.
	std::vector<ibQueryConstructorField> GetReferenceFields(ibClassID clsid,
	                                                        const wxString& sourceLabel = wxEmptyString) const;

	// The same walk asked of a whole TYPE rather than of one target: every reference alternative is
	// unfolded and their fields merged by name, so a composite answers with what all of it offers.
	// A field carried by several alternatives keeps the union of their types, which is what lets the
	// next hop through it stay walkable.
	std::vector<ibQueryConstructorField> GetReferenceFields(const ibTypeDescription& typeDesc,
	                                                        const wxString& sourceLabel = wxEmptyString) const;

	// ⭐ THE ALTERNATIVES THEMSELVES, when a field points at SEVERAL types — one entry per type,
	// named the way a query names it. A tree draws these as a level of their own and unfolds each
	// into its own fields, because that is the only way to say WHICH type offers what: two
	// documents both have a `Date`, and merging them would hide that they are two.
	//
	// The merged overload above is the other question — what the WALK offers — and it collapses the
	// same names, because that is what the query does when it runs. Discovery separates; execution
	// merges.
	//
	// One alternative (an ordinary reference) answers with one entry, and a tree showing branches
	// only for several will just unfold it: a plain [+], no type level in between.
	std::vector<ibQueryConstructorField> GetReferenceBranches(const ibTypeDescription& typeDesc) const;

	// ⭐ A TYPE WRITTEN AS A CAST, BOTH WAYS — how a temporary table's fields are described in the text itself
	// (`CAST(T.Qty AS Number(15, 2)) AS Qty … INTO Goods FROM &Goods AS T`), so a window can write them and every
	// reader of the text — this model, the lowering — reads them back alike.
	//   · CastTo — `value` wrapped in the CAST that brings it to `type`: a primitive with its qualifiers
	//     (ibQueryMakeCast), one reference as the table it points at, named as GetReferenceBranches names it.
	//     Anything no CAST can say (a composite, a type no table stands behind) leaves `value` as it is.
	//   · TypeOfCast — the type such a CAST brings its value to; empty for anything else.
	ibQueryAstExprPtr CastTo(ibQueryAstExprPtr value, const ibTypeDescription& type) const;
	ibTypeDescription TypeOfCast(const ibQueryAstExpr& cast) const;
	// …AND WHAT A SELECT SAYS OF A TABLE THAT HAS NO ROWS YET — one handed in (`&Goods`), or a temporary table the
	// manager brings: every `Goods.X` it takes, as it is or CAST to a type, is a field of that table, typed by the
	// CAST. The one reading of such a table, for the model's field lists and for the window that describes it.
	std::vector<ibQueryConstructorField> FieldsTakenFrom(const ibQuerySelect& reader, const ibQuerySource& source) const;

	// THE FIELD A PATH ENDS IN — the walk the trees make when somebody clicks [+], asked all at once
	// instead of one level at a time: find the source the first segment belongs to, then hop through
	// the references. Done ONCE, and both questions below read its answer.
	//
	// An unresolvable path yields an EMPTY field: silence, never a guess.
	ibQueryConstructorField FieldOfPath(const ibQuerySelect& select, const std::vector<wxString>& path,
	                                    const ibQueryPackage& package, size_t beforeStatement) const;
	// …AND THE HOPS OF IT, from a field already in hand — each segment found among the fields of the one
	// before, a reference walked by its TYPE (GetReferenceFields: the target's descriptor, no value made).
	// The leaf answers with its type and with whether EVERY hop is available (functional options).
	// FieldOfPath runs it once it has found where a path starts; a host that holds the start itself (a
	// setting over a known source) runs it directly. A hop nothing answers yields an EMPTY field that keeps
	// only what the walk knew up to there — silence, never a guess.
	ibQueryConstructorField WalkFrom(const ibQueryConstructorField& from, const std::vector<wxString>& hops) const;
	// …AND WHERE A WRITTEN PATH STARTS among the fields a holder already has: the LONGEST prefix one of them is
	// named — a linked package names its fields with the selection in front (`Sales.Qty`), so the first
	// segment alone is not always one — and the rest walked from it (WalkFrom). One rule for every holder of
	// fields: the query's own sources (FieldOfPath), a composer's source, a settings window's list. A path
	// none of them starts yields an EMPTY field — silence, and no verdict on it.
	ibQueryConstructorField WalkPath(const std::vector<ibQueryConstructorField>& fields,
	                                 const std::vector<wxString>& path) const;
	// WHAT IT REFERS TO — 0 when the leaf is not a single-target reference.
	ibClassID ReferenceOfPath(const ibQuerySelect& select, const std::vector<wxString>& path,
	                          const ibQueryPackage& package, size_t beforeStatement) const;
	// WHAT IT HOLDS — empty when the path does not resolve, which is "unknown", never "no type".
	// A host reads it to ask the engine which aggregates fit (ibQueryAggregatesFor).
	ibTypeDescription TypeOfPath(const ibQuerySelect& select, const std::vector<wxString>& path,
	                             const ibQueryPackage& package, size_t beforeStatement) const;

	// WHAT A SELECT PRODUCES, as fields — how a NESTED table and a TEMP table both answer, and
	// the same question a host asks about the whole query: "which fields may I write an
	// expression over". An unaliased projection is named by its own expression, which is what
	// the renderer writes and therefore what an outer query can refer to.
	//
	// ⚠ IT RESOLVES THE TYPE, not just the name. A temp table made by `SELECT Owner.Ref INTO Tmp`
	// vends a column that IS a reference, and the next statement must be able to walk into it —
	// `Tmp.OwnerRef.Description` parses, runs, and has to be OFFERABLE. Reading only the projection
	// NAMES left every temp-table field a dead leaf with no [+], which is the design-time half of
	// "the temp table does not inherit the type" (the runtime half was OutputColumn::m_type).
	//
	// `package` / `beforeStatement` — because resolving a projection means resolving it against the
	// making select's OWN sources, and one of those may itself be an earlier temp table.
	//
	// (Public since 2026-08-19: the data composition's settings window builds its expression
	// editor over exactly this answer. A window assembling the list itself would part company
	// with this one at the first reference field — this one unfolds, a hand-made one does not.)
	std::vector<ibQueryConstructorField> FieldsOfSelect(const ibQuerySelect& select,
	                                                    const ibQueryPackage& package,
	                                                    size_t beforeStatement) const;

private:
	const ibMetaData* m_metaData = nullptr;   // which configuration the question is for — the server answers it
	std::shared_ptr<struct ibQueryModelAnswers> m_known;   // what the tab already answered — kept for the window's life
};

// ⭐⭐ …AND NO ROLE THE TEXT SAYS. What a field is in a balance is a composition's word on its Fields page
// (ibFieldDescription::m_role), and a `ROLE` in the query under it would be a second place to say one thing
// (Max, 2026-09-29). So a composition reads its query without them — the fields above offer the SOURCE's
// roles only, and the composer stands on this text (ibDataDBComposer::FromText). Every select the text holds
// forgets them, the ones it reads FROM included, since a role travels up with its column. A text that says no
// role — or does not parse yet — comes back as it was, byte for byte.
FRMCLIENT_API wxString ibQueryTextWithoutRoles(const wxString& text);
// …the same over a parsed package (the query constructor's, opened by a host that keeps roles itself —
// ibQueryExclude_Roles). True when one was said.
FRMCLIENT_API bool ibQueryDropRoles(ibQueryPackage& package);

#endif
