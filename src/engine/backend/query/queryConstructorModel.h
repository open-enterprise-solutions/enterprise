#ifndef __QUERY_CONSTRUCTOR_MODEL_H__
#define __QUERY_CONSTRUCTOR_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// L4-1 — the query constructor's MODEL half.
////////////////////////////////////////////////////////////////////////////
//
// The constructor is a frontend VIEW over a backend MODEL, and this is the model: what may go
// into a query (the sources, their fields, their types). It lives here because it is METADATA,
// and metadata is the backend's — the shell above it is wx code that knows nothing beyond what
// this hands it.
//
// TWO RULES, and both are the reason this file exists rather than a helper in the dialog:
//
//   1. THE CATALOGUE IS A WALK, NEVER A LIST. The sources come from ibQueryableFactory, which is
//      where a metaobject REGISTERS itself when it runs. A written-out list of metaclasses would
//      make the next metatype invisible in the constructor exactly as a written-out list made it
//      invisible in the role editor (2026-08-06) — the same defect, one layer up.
//
//   2. FIELDS ARE ASKED OF THE SOURCE. A real source answers through its descriptor
//      (FillSourceExplorer — the same call a dynamic list fills its columns with); a NESTED table
//      (a subquery standing where a table would) answers with its own projections. Both are
//      answers to one question, so the shell asks the SOURCE which it is rather than branching on
//      a type — and a source kind added later needs no new branch up in the dialog.
//
// See docs/query-constructor.md §4 / §5a / §5b.
//
////////////////////////////////////////////////////////////////////////////

#include "queryAst.h"
#include "queryableFactory.h"

#include <vector>

class ibMetaData;

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

// One field of a source: the name a query writes and the words a person reads.
struct ibQueryConstructorField
{
	wxString m_name;           // the technical name — this is what the AST carries
	wxString m_presentation;   // synonym, falling back to the name
	// WHICH SOURCE IT CAME OUT OF, by the name the query calls that source. Carried on the field so
	// every list of fields can GROUP by it — a flat merge of two tables' fields is unreadable the
	// moment both have a `Code`, and the reader cannot tell which is which without the prefix on
	// every single line (which is the other unreadable answer).
	wxString m_source;
	bool     m_reference = false;   // the field is a reference: it can be dot-walked further
	// WHAT it refers to, when it refers to exactly one thing. Non-zero is the answer to "can this
	// be unfolded", and it is also the key the fields behind it are asked for by — so the shell
	// never has to look a type up, and a COMPOSITE reference (which the engine cannot dot-walk
	// through) is simply left at zero and shown as a leaf.
	ibClassID m_referenceClsid = 0;
	// WHAT IT HOLDS. The explorer node already knows; carrying it here is what lets a host ask a
	// type question (which aggregates fit this field?) without walking the metadata itself.
	ibTypeDescription m_type;
	// HOW IT LOOKS, taken from the COLUMN (ibBackendSourceColumn::GetColumnIcon) rather than
	// decided by the shell: a dimension, a resource and a plain attribute each answer with their
	// own, and a shell that draws them holds no list of kinds. Null only for a field standing
	// behind no column at all — the drawer then uses its default picture.
	wxIcon m_icon;
};

class BACKEND_API ibQueryConstructorModel
{
public:
	// `metaData` — the config the query runs on behalf of (its factory resolves the sources).
	// Null falls back to the global factory, which is the right answer with no config open.
	explicit ibQueryConstructorModel(const ibMetaData* metaData = nullptr);

	// EVERY source a query may name, grouped by nothing — the caller groups by m_path[0], which is
	// the metaclass kind, so the tree's top level is whatever the factory actually holds.
	std::vector<ibQueryConstructorSource> GetSources() const;

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
	// Empty for anything that is not a single-target reference. A COMPOSITE reference (several
	// clsids) has no one answer, and the lowering refuses a composite mid-segment for the same
	// reason — offering a walk the engine would then reject is worse than not offering it.
	//
	// `sourceLabel` — WHICH TABLE THE WALK STARTED FROM, carried down every level. A field five hops
	// inside a reference still belongs to the table it hangs off, and the trees group by that; a
	// walked field with no source landed in a group of its own, away from its own table. The caller
	// knows it (it is walking a node of that table), so it hands it in rather than being asked to
	// re-derive it from a dotted path, which is exactly what a dot-walk makes ambiguous.
	std::vector<ibQueryConstructorField> GetReferenceFields(ibClassID clsid,
	                                                        const wxString& sourceLabel = wxEmptyString) const;

	// THE FIELD A PATH ENDS IN — the walk the trees make when somebody clicks [+], asked all at once
	// instead of one level at a time: find the source the first segment belongs to, then hop through
	// the references. Done ONCE, and both questions below read its answer.
	//
	// An unresolvable path yields an EMPTY field: silence, never a guess.
	ibQueryConstructorField FieldOfPath(const ibQuerySelect& select, const std::vector<wxString>& path,
	                                    const ibQueryPackage& package, size_t beforeStatement) const;
	// WHAT IT REFERS TO — 0 when the leaf is not a single-target reference.
	ibClassID ReferenceOfPath(const ibQuerySelect& select, const std::vector<wxString>& path,
	                          const ibQueryPackage& package, size_t beforeStatement) const;
	// WHAT IT HOLDS — empty when the path does not resolve, which is "unknown", never "no type".
	// A host reads it to ask the engine which aggregates fit (ibQueryLowering::AggregatesFor).
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
	ibQueryableFactory* Factory() const;

	const ibMetaData* m_metaData = nullptr;
};

#endif
