#ifndef __QUERY_LOWERING_INTERNAL_H__
#define __QUERY_LOWERING_INTERNAL_H__

////////////////////////////////////////////////////////////////////////////
//	The lowering's OWN vocabulary — what its files say to each other.
////////////////////////////////////////////////////////////////////////////
//
// queryLowering.h is the lowering's face to every caller; this is how its own files — the translation
// (queryLowering.cpp), the name check (queryNameCheck.cpp), the package (queryPackage.cpp) and its lock
// (queryLock.cpp) — speak to one another, and nothing here reaches the face. The same shape as dataComposerInternal.h. The names were
// file-local and stay as they were, so they live in a namespace of their own: another file of the engine
// says ibNoCaseLess too.

#include "backend/query/queryLowering.h"
#include "backend/query/queryRamTable.h" // ibQueryRamTable — what DrainIntoSnapshot pours a selection into

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace ibLowering {

// ⭐ NAMES IN THIS LANGUAGE ARE MATCHED WITHOUT REGARD TO CASE — every comparison in the lowering says
// so (`IsSameAs(x, false)`, `CmpNoCase`), and a map keyed by the default `wxString` ordering would
// say the opposite in the one place a name is LOOKED UP rather than compared. `SELECT Qty * Price AS
// Total … TOTALS SUM(total)` then refuses a field the query plainly carries.
struct ibNoCaseLess
{
	bool operator()(const ibString& a, const ibString& b) const { return a.CmpNoCase(b) < 0; }
};

// The output-column descriptor the runtime reads back — used unqualified throughout this namespace.
using ibQueryOutputColumn = ibQueryLowering::ibQueryOutputColumn;

// All lowering errors carry the AST source span. Always throws (callers that return
// a value follow with an unreachable dummy return — the codebase's Error();return idiom).
//
// ⭐ THE TIER RAISES ITS OWN VARIETY. A query that does not hold up is not "an error with no
// subsystem" — it is L3 refusing, and the exception TYPE is what says so (docs/private/exceptions.md §3).
// Typed as Core it could only be caught by catching everything, which is the same as not being able
// to catch it at all: a script's Try/Except around a query, a tool that wants to show the author
// where the query is wrong, and a caller that must let real faults through were all indistinguishable.
void ThrowQueryException(unsigned int line, unsigned int col, const ibString& msg);

// …and the same with the message's OWN arguments, because most refusals have to name the thing they
// are refusing. Formatting belongs here, once, and not at every callsite: the span, the wording and
// the assembly are one decision, and spreading `wxString::Format` through the lowering is the same
// decision made again in twenty places.
template <typename... Args>
void ThrowQueryException(unsigned int line, unsigned int col, const ibString& fmt, Args&&... args)
{
	ThrowQueryException(line, col, ibString::Format(fmt, std::forward<Args>(args)...));
}

// One resolved source in a query: its alias + queryable. Known before the source resolver, because a
// condition the source CONSUMES is lowered while the source is still being built — see ResolveSource.
// (Its full role is described where the resolver's column lookup uses it, queryLowering.cpp.)
struct ibSourceBinding
{
	ibString                                  m_alias;
	// ⭐ HELD, whatever it is: a metaobject's table, a virtual table's reading (the query's own,
	// ibQueryableSourceDescriptor::CreateQueryable), or an alias twin this lowering MINTED for a repeated
	// reading of one table — which the NEXT reading of that table takes as its origin (queryLowering.cpp), so it
	// has to outlive the expression that made it.
	std::shared_ptr<const ibBackendQueryable> m_q;
};

// A synthetic scalar column the totals lowering builds: it reads a value the door projected under a
// distinct cursor alias (a computed measure `1 AS test`; a dot-walk dimension's leaf `Parent.Code`),
// STRAIGHT off the cursor by that alias, under a UNIQUE synthetic model id. This is what lets the
// metaID-keyed totals fold read it as a normal column — AND keeps a self-referential dimension
// (Parent.Code, whose leaf shares a metaID with the row's own attribute) from clashing with the main
// table. The column stamps its own kind onto the ordinal it is handed, which is what makes the id
// negative and distinct from every other minted one (ibBackendQueryColumn::SyntheticId).
class ibSyntheticScalarColumn : public ibBackendColumnRawDB
{
public:
	// Takes the ordinary running number and stamps its own kind on it inside, like every synthetic
	// column here — the caller hands a plain ordinal and knows nothing about the layout.
	ibSyntheticScalarColumn(const ibString& alias, ibMetaID id, RawType type = RawType::Number)
		: ibBackendColumnRawDB(alias, type), m_id(SyntheticId(SyntheticKind::Output, id)) {}
	ibMetaID GetColumnId() const override { return m_id; }
private:
	ibMetaID m_id;
};

// The translation's (queryLowering.cpp), asked by the check and the package:
ibValue EvalValue(const ibQueryAstExpr& e, const std::map<ibString, ibValue>& params);
std::map<ibString, ibValue> WithEveryParameter(const ibString& text, const std::map<ibString, ibValue>& params);
std::shared_ptr<const ibBackendQueryable> ResolveSource(const ibQuerySource& src, const std::map<ibString, ibValue>& params,
                                                        std::vector<ibQueryAstExprPtr>* conditionsOut = nullptr,
                                                        const ibQuerySelect* readBy = nullptr);
std::vector<std::shared_ptr<const ibBackendQueryColumn>> ResolvePath(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e);
ibTypeDescription CastTargetType(const ibQueryAstExpr& cast);
bool IsComputedExprAst(const ibQueryAstExpr& e);
const ibQueryAstExprPtr& SortTargetOf(const ibQuerySelect& ast, const ibQueryAstExprPtr& written);
ibString OutputNameFor(const ibQuerySelect& select, const ibQueryProjection& p, int idx);

// The name check's (queryNameCheck.cpp), asked by the translation and the package:
void CollectColumns(const ibQueryAstExprPtr& e, std::vector<ibQueryAstExprPtr>& out);
void RefuseUngrouped(const ibQuerySelect& ast, const std::vector<ibSourceBinding>& sources);
void CheckUnlimitedStringsAreCast(const ibQuerySelect& ast, const std::vector<ibSourceBinding>& sources);
void CheckTotalsNameSelectedFields(const ibQuerySelect& ast);
void CheckIndexNamesOutputs(const ibQuerySelect& ast);

// The package's (queryPackage.cpp), asked by the translation — a finished selection drained into a snapshot.
ibQueryRamTable DrainIntoSnapshot(ibDataQueryResult& result, const std::vector<ibQueryOutputColumn>& schema);

// The package's LOCK (queryLock.cpp) — the facade, whole: the keys of the tables it names, each held by its table's
// provider, kept in `temps` under the LOCK's name, and held by `manager` alone when the package was handed one, else
// by the transaction. Answers how many keys it holds.
long ExecuteLock(const ibQueryAstLock& lock, const std::map<ibString, ibValue>& params, ibQueryTempDataStore& temps,
                 const class ibQueryTempDataStore* manager);

} // namespace ibLowering

#endif // __QUERY_LOWERING_INTERNAL_H__
