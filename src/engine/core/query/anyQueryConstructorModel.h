#ifndef __ANY_QUERY_CONSTRUCTOR_MODEL_H__
#define __ANY_QUERY_CONSTRUCTOR_MODEL_H__

////////////////////////////////////////////////////////////////////////////
//	Description : THE QUERY CONSTRUCTOR'S MODEL, AS EITHER SIDE HOLDS IT - what it derives over a query
////////////////////////////////////////////////////////////////////////////
//
// The constructor's model has two halves. What a TABLE of the configuration offers — its fields, the reference type it
// is, the table a reference points at — is the configuration's, and each side answers it its own way: the engine's
// model (backend/query/queryConstructorModel.h) through the descriptors its factory holds, the client's
// (frmclient/backend/query/queryConstructorModel.h) by asking the tab the window was opened from. Everything the windows
// DERIVE from those answers — a temporary table's fields, a nested query's, the walk of a path, the type a CAST says —
// is a walk over the query, and it is written here once: the way ibAnyValue is what both sides' values are.
//
// See docs/private/query-constructor.md §4 / §5a / §5b.
//
////////////////////////////////////////////////////////////////////////////

#include "core/query/queryAST.h"
#include "core/query/queryConstructorField.h"   // ibQueryConstructorField — one field, as every window reads it

#include <vector>

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

	// INLINE, not exported: the struct itself carries no CORE_API (it is a plain value each side
	// copies), so a definition in the .cpp would be invisible across the DLL — which is exactly what
	// the test link found.
	wxString Text() const {                 // the path joined with dots — what goes into the AST
		wxString out;
		for (size_t i = 0; i < m_path.size(); ++i) {
			if (i > 0) out += wxT(".");
			out += m_path[i];
		}
		return out;
	}
};

class CORE_API ibAnyQueryConstructorModel
{
public:

	// Out of line (anyQueryConstructorModel.cpp): the table is the core's, one in the process — and each side's model,
	// exported from its own library, derives from an exported one.
	virtual ~ibAnyQueryConstructorModel();

	// The temp tables `package` has DECLARED so far, in the order they are made. These belong to the
	// package being edited, not to the metadata — which is why they are a separate question with the
	// package as its argument, and why the tree is fed from two places (docs §5b).
	// `beforeStatement` — only the statements BEFORE this index count: statement 2 cannot select from
	// a table statement 5 will make.
	static std::vector<ibQueryConstructorSource> GetTempSources(const ibQueryPackage& package,
	                                                            size_t beforeStatement);

	// THE FIELDS OF A SOURCE — one question, and the source decides how it is answered:
	// a nested table (m_subquery set) answers with its own projections, a table of the configuration
	// through the side (GetTableFields), a temp table with the projections of the statement that made it.
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
	// No lookup table and no new registry: the side names the table behind the class (ConvertToMetaIds)
	// and answers with its fields (GetTableFields). A type added tomorrow unfolds the day it registers.
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

	// ⭐ WHAT A FIELD'S TYPE REFERS TO — its m_reference and m_referenceClsid, from its m_type, asked of the side
	// (ConvertToMetaIds) and never read off the clsid here: only the side knows what a type is. A type with ANY table
	// behind it can be walked; ONE class with ONE table behind it refers to that table (a barrier — `AnyRef` — standing
	// for several refers to none). What makes fields of a type it has in hand runs it: a CAST's, a client's row.
	void ResolveReference(ibQueryConstructorField& field) const;

	// ⭐ THE ALTERNATIVES THEMSELVES, when a field points at SEVERAL types — one entry per type,
	// named the way a query names it (TableOfReference). A tree draws these as a level of their own and
	// unfolds each into its own fields, because that is the only way to say WHICH type offers what: two
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
	//   · TypeOfCast — the type such a CAST brings its value to (a table's: ReferenceOfTable); empty for anything else.
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
	// before, a reference walked by its TYPE (GetReferenceFields: the target's table, no value made).
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

	// ⭐ A TABLE'S OWN REFERENCE TYPE — what a CAST to it brings a value to (`CAST(x AS Catalog.Goods)`); 0 for a table
	// no reference points at (a register, a virtual table). The configuration's answer, the side's — and asked outright:
	// the server writes it into the catalogue it sends (request Sources).
	virtual ibClassID ReferenceOfTable(const std::vector<wxString>& path) const = 0;
	// …AND THE OTHER WAY: the table a reference type names, as a query names it (`Catalog.Goods`); empty for a type
	// no table stands behind (a report, a data processor).
	virtual wxString TableOfReference(ibClassID reference) const = 0;

protected:

	// THE FIELDS OF A TABLE OF THE CONFIGURATION, the side's answer — by the source as a FROM writes it, asked with its
	// arguments, which decide a virtual table's columns…
	virtual std::vector<ibQueryConstructorField> GetTableFields(const ibQuerySource& source) const = 0;
	// …and by its id: the table a reference points at (ConvertToMetaIds), one level down the dot walk.
	virtual std::vector<ibQueryConstructorField> GetTableFields(ibMetaID table) const = 0;
	// THE TABLES A TYPE REFERS TO — the ids of the tables behind its classes; none for a class no table stands behind.
	// The question every side already answers under this name (ibValueReferenceDataObject::ConvertToMetaIds): the
	// engine reads the class, the client the references the server described with what it sent.
	virtual std::vector<ibMetaID> ConvertToMetaIds(const std::vector<ibClassID>& clsids) const = 0;
};

// ⭐⭐ …AND NO ROLE THE TEXT SAYS. What a field is in a balance is a composition's word on its Fields page
// (ibFieldDescription::m_role), and a `ROLE` in the query under it would be a second place to say one thing
// (Max, 2026-09-29). So a composition reads its query without them — the fields above offer the SOURCE's
// roles only, and the composer stands on this text (ibDataDBComposer::FromText). Every select the text holds
// forgets them, the ones it reads FROM included, since a role travels up with its column. A text that says no
// role — or does not parse yet — comes back as it was, byte for byte.
CORE_API wxString ibQueryTextWithoutRoles(const wxString& text);
// …the same over a parsed package (the query constructor's, opened by a host that keeps roles itself —
// ibQueryExclude_Roles). True when one was said.
CORE_API bool ibQueryDropRoles(ibQueryPackage& package);

#endif // __ANY_QUERY_CONSTRUCTOR_MODEL_H__
