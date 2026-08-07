////////////////////////////////////////////////////////////////////////////
//	L4 — lowering: BOTH front-ends land here — L4-1 text (Execute / ExecuteTotals) and
//	L4-2 LINQ (LowerLambda*) -> ibDataQueryBuilder, executed (queryLowering.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLowering.h"

#include "queryRewrite.h"                 // ibQueryRewrite — optimizer pass (AST -> AST)
#include "queryRender.h"                  // ibQueryOutputName — the ONE answer to "what is this field called"
#include "queryRamTable.h"                // ibQueryRamTable — a package's temp table IS a snapshot
#include "queryTempStore.h"               // ibQueryTempTableStore — WHO keeps the temp tables alive
#include "tempTableQueryable.h"           // ibTempTableQueryable — a table handed in as a PARAMETER is a source
#include "queryable.h"                    // ibBackendQueryColumn / ibQueryFilterOp
#include "queryProvider.h"                // ibBackendQueryProvider — GetProvider().ResolveReferenceTarget (dot-walk resolution)
#include "queryableFactory.h"             // ibQueryableFactory — source-namespace resolution
#include "backend/appData.h"              // ibApplicationData::GetQueryableFactory
#include "backend/metaData.h"             // ibMetaData::GetSourceFactory — resolve through the query's OWN config
#include "backend/metaCollection/genericData.h"  // ibValueMetaObjectGenericData::ResolveQueryConstant (value(...) resolution)
#include "backend/model.h"            // ibComparisonType
#include "backend/backend_exception.h"    // ibBackendCoreException

// ⚠ NAMED, NOT INHERITED. std::find / std::remove_if arrived in this file with the grouping and
// prune passes; MSVC hands <algorithm> over transitively and GCC/Clang do not, so the Windows build
// stayed green while the other three CI jobs could not compile it. See docs/portability.md.
#include <algorithm>

// --- the AUXILIARY per-query temp-source registry (decl in queryable.h) ------------
// Thread-local so concurrent sessions don't see each other's transient sources; RAII so a
// query leaves no trace once it finishes. ResolveSource consults it before the metaobject
// factory, returning a registered RAM / temp queryable directly. (Max: L5 registers at L4.)
static thread_local const std::map<wxString, const ibBackendQueryable*>* t_tempSources = nullptr;

ibTempSourceScope::ibTempSourceScope(const std::map<wxString, const ibBackendQueryable*>& sources)
	: m_prev(t_tempSources)
{
	t_tempSources = &sources;
}

ibTempSourceScope::~ibTempSourceScope()
{
	t_tempSources = m_prev;
}

const ibBackendQueryable* ibTempSourceScope::Find(const wxString& name)
{
	if (t_tempSources == nullptr)
		return nullptr;
	const auto it = t_tempSources->find(name);
	return it != t_tempSources->end() ? it->second : nullptr;
}

// The config a query runs on behalf of, for one execution — the composer threads its metadata in through here (parallel
// to the temp-source scope), so ResolveSource resolves by-name metaobject sources against THIS config's factory.
static thread_local const ibMetaData* t_sourceMetaData = nullptr;

ibSourceMetaDataScope::ibSourceMetaDataScope(const ibMetaData* metaData)
	: m_prev(t_sourceMetaData)
{
	t_sourceMetaData = metaData;
}

ibSourceMetaDataScope::~ibSourceMetaDataScope()
{
	t_sourceMetaData = m_prev;
}

const ibMetaData* ibSourceMetaDataScope::Get()
{
	return t_sourceMetaData;
}

namespace {

// The output-column descriptor the runtime reads back — used unqualified throughout this namespace.
using OutputColumn = ibQueryLowering::OutputColumn;

// All lowering errors carry the AST source span. Always throws (callers that return
// a value follow with an unreachable dummy return — the codebase's Error();return idiom).
void Fail(unsigned int line, unsigned int col, const wxString& msg)
{
	ibBackendCoreException::Error(_("Query: %s (line %u, position %u)"), msg, line, col);
}

ibValue EvalValue(const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params);   // defined below

// Resolve the FROM / JOIN source namespace.name to a queryable through the source
// factory appData owns (built-in metaobject families + plugin / external sources).
const ibBackendQueryable* ResolveSource(const ibQuerySource& src, const std::map<wxString, ibValue>& params)
{
	// A TEMP TABLE IS A BARE NAME. `SELECT … INTO Sales` then `FROM Sales` — no <Kind>. prefix,
	// because a temp table has no metaclass to name: it is what the previous statement left.
	// Asked FIRST and asked of the auxiliary registry, so a package reads the way it was written.
	if (src.m_name.size() == 1) {
		if (const ibBackendQueryable* tmp = ibTempSourceScope::Find(src.m_name[0]))
			return tmp;

		// ⭐ …OR A TABLE HANDED IN AS A PARAMETER. `FROM Goods` where `Goods` is a bound value table:
		// the table is DECLARED OUTSIDE and passed in, which is the other kind of temp table and the
		// one a caller controls. `SELECT … INTO Sales` makes one; this one arrives already full.
		//
		// Nothing new was needed for it. A runtime table is already a first-class source
		// (ibTempTableQueryable — the same wrap `Data.From(table)` and the LINQ joins use), and the
		// parameters already reach the lowering; what was missing was that resolving a SOURCE never
		// looked at them, so a query could take a table as a VALUE and not as a TABLE.
		//
		// ⚠ Owned for the whole run: the wrap must outlive the door's terminal call, and a source
		// resolved once is read many times. The map is keyed by name so two mentions of the same
		// parameter are ONE source rather than two snapshots of it.
		const auto bound = params.find(src.m_name[0]);
		if (bound != params.end()) {
			static thread_local std::map<wxString, std::shared_ptr<ibTempTableQueryable>> s_boundTables;
			auto& wrap = s_boundTables[src.m_name[0]];
			if (!wrap)
				wrap = std::make_shared<ibTempTableQueryable>(bound->second);
			if (!wrap->GetColumns().empty())
				return wrap.get();
			// A parameter that is NOT a table vends no columns. Fall through and let the ordinary
			// "a source must be <Kind>.<Name>" verdict be given — naming the real mistake rather
			// than "this table has no columns", which would send the author looking at the wrong end.
			s_boundTables.erase(src.m_name[0]);
		}
	}

	if (src.m_name.size() < 2)
		Fail(0, 0, _("a source must be <Kind>.<Name>"));

	// ns = the first segment (the metaclass kind — English canonical, as the descriptors
	// register); the rest join into the object name, so a virtual table reads as
	// <Kind>.<Object>.<Table> -> name="Object.Table" (the register's balance / turnover / slice
	// descriptor registers under that composite name).
	const wxString& ns = src.m_name[0];
	wxString name = src.m_name[1];
	for (size_t i = 2; i < src.m_name.size(); ++i)
		name += wxT(".") + src.m_name[i];

	// The AUXILIARY per-query registry FIRST: a transient (RAM / temp) source the composer
	// registered under a unique local name. It IS a complete L3 queryable — return it directly,
	// bypassing the metaobject factory (which carries no descriptor for it). (temp-table feature)
	if (const ibBackendQueryable* tmp = ibTempSourceScope::Find(name))
		return tmp;

	// Resolve through the config the query runs ON BEHALF OF (threaded in by the composer) — metaobject sources
	// register per-config now, and that factory descends to the global one internally (future plugin sources). No
	// config in scope → knock on the global base factory directly (the common/plugin one).
	const ibMetaData* md = ibSourceMetaDataScope::Get();
	ibQueryableFactory* factory = md != nullptr ? md->GetSourceFactory() : nullptr;
	if (factory == nullptr)
		factory = ibApplicationData::GetQueryableFactory();
	if (factory == nullptr) {
		Fail(0, 0, _("the query engine is not available (no application data)"));
		return nullptr;
	}
	if (!factory->HasNamespace(ns)) {
		Fail(0, 0, wxString::Format(_("unknown metaobject kind '%s'"), ns));
		return nullptr;
	}

	// Source-call args (Balance(&Period, …)) -> the descriptor's CreateQueryable. The companion copies
	// them by value (e.g. ibBalanceQueryable stores m_period), so these locals may die after Resolve.
	std::vector<ibValue>  argVals;
	for (const ibQueryAstExprPtr& a : src.m_args)
		argVals.push_back(EvalValue(*a, params));
	std::vector<ibValue*> argPtrs;
	for (ibValue& v : argVals)
		argPtrs.push_back(&v);

	const ibBackendQueryable* q = factory->Resolve(ns, name,
		argPtrs.empty() ? nullptr : argPtrs.data(), static_cast<long>(argPtrs.size()));
	if (q == nullptr) {
		Fail(0, 0, wxString::Format(_("metaobject '%s.%s' not found or cannot be queried"), ns, name));
		return nullptr;
	}
	return q;
}

// One resolved source in a query: its alias + queryable. A single-source query is a list of one
// (alias may be empty); a JOIN adds one binding per joined source. Column resolution picks the
// source by alias prefix (`b.Field`), else searches all sources (the primary, sources[0], first).
struct ibSourceBinding
{
	wxString                    m_alias;
	const ibBackendQueryable*   m_q = nullptr;
};

// Find the source an alias names, or null if the first path segment is not a known alias.
const ibBackendQueryable* SourceForAlias(const std::vector<ibSourceBinding>& sources, const wxString& alias)
{
	if (!alias.empty())
		for (const ibSourceBinding& s : sources)
			if (!s.m_alias.empty() && s.m_alias.CmpNoCase(alias) == 0) return s.m_q;
	return nullptr;
}

// The single source that OWNS a bare (unqualified) column. Fails on AMBIGUITY — a bare column that
// exists in more than one joined source must be qualified with an alias (SQL "ambiguous column"). Returns
// null only when no source owns it (the caller reports "unknown attribute") or sources is empty.
const ibBackendQueryable* OwnerOfBareColumn(const std::vector<ibSourceBinding>& sources, const wxString& name,
                                            int line, int col)
{
	const ibBackendQueryable* owner = nullptr;
	for (const ibSourceBinding& s : sources)
		if (s.m_q->ResolveColumnByName(name) != nullptr) {
			if (owner != nullptr)
				Fail(line, col, wxString::Format(
					_("ambiguous attribute '%s': it is in more than one source: qualify it with an alias (e.g. a.%s)"),
					name, name));
			owner = s.m_q;
		}
	return owner;
}

// Reject a DUPLICATE source alias — the same alias on two FROM/JOIN sources makes every `alias.col`
// reference ambiguous (SQL "table name specified more than once"). Synthetic dot-walk / ref-join aliases
// are unique by construction, so only user-written aliases can collide here.
void RequireAliasFree(const std::vector<ibSourceBinding>& sources, const wxString& alias, int line, int col)
{
	if (!alias.empty() && SourceForAlias(sources, alias) != nullptr)
		Fail(line, col, wxString::Format(
			_("duplicate source alias '%s': each FROM / JOIN source needs a distinct alias"), alias));
}

// A SINGLE column (for WHERE / ORDER / aggregate arg / GROUP BY): `alias.col` -> the aliased
// source's column; bare `col` -> the one source that owns it (ambiguous across sources -> error). Dot-walk
// paths are rejected here (only the projection resolves them, via ResolvePath).
const ibBackendQueryColumn* ResolveColumnSingle(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	const std::vector<wxString>& path = e.m_path;

	if (path.size() == 2) {
		const ibBackendQueryable* q = SourceForAlias(sources, path[0]);
		if (q != nullptr) {
			const ibBackendQueryColumn* col = q->ResolveColumnByName(path[1]);
			if (col == nullptr)
				Fail(e.m_line, e.m_col, wxString::Format(_("unknown attribute '%s'"), path[1]));
			return col;
		}
		// path[0] is not an alias -> a dot-walk (Producer.Name), not allowed in this clause yet.
	}
	else if (path.size() == 1) {
		const ibBackendQueryable* q = OwnerOfBareColumn(sources, path[0], e.m_line, e.m_col);   // ambiguous -> Fail
		if (q == nullptr)
			Fail(e.m_line, e.m_col, wxString::Format(_("unknown attribute '%s'"), path[0]));
		return q->ResolveColumnByName(path[0]);
	}

	Fail(e.m_line, e.m_col, _("dot-walk columns are not supported in this clause yet"));
	return nullptr;
}

// A reference dot-walk path (Producer.Name | b.Producer.Name) -> the chain of columns SelectPath
// wants. An alias prefix selects the starting source; otherwise the walk starts on the source that
// owns the first segment (primary first).
std::vector<const ibBackendQueryColumn*> ResolvePath(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	const std::vector<wxString>& path = e.m_path;
	size_t i = 0;
	const ibBackendQueryable* cur = sources.empty() ? nullptr : sources[0].m_q;

	std::vector<const ibBackendQueryColumn*> cols;

	// ⭐ A WALK ROOTED ON A CAST. `CAST(Recorder AS Document.Order).Number` — the cast names WHICH of
	// a composite reference's types is meant, and the walk continues into that one.
	//
	// This is the whole of what CAST is for. A composite reference has no single set of fields behind
	// it, so the hop below refuses it ("not a single-target reference") — correctly, because there is
	// no one answer. Saying which type is meant supplies the answer, and from that point on this is
	// an ORDINARY dot-walk: the chain it builds is [the reference column, …, the leaf], exactly the
	// shape SelectPath and ExpandDotWalkJoins already consume. Nothing downstream learns a new trick.
	if (e.m_arg && e.m_arg->m_kind == ibQueryAstExprKind::Cast) {
		const ibQueryAstExpr& cast = *e.m_arg;
		if (!cast.m_arg || cast.m_arg->m_kind != ibQueryAstExprKind::Column)
			Fail(cast.m_line, cast.m_col, _("CAST expects a field to narrow"));

		// The reference column being narrowed — resolved the ordinary way, so `p.Recorder` works.
		const std::vector<const ibBackendQueryColumn*> root = ResolvePath(sources, *cast.m_arg);
		if (root.empty())
			return cols;
		cols = root;

		// ⚠ A PRIMITIVE TARGET IS REFUSED, and the message says why rather than "unknown table".
		//
		// `CAST(Code AS Number)` is a CONVERSION, and this is a NARROWING — two different things that
		// share a word. Narrowing needs no work at the door: the value already IS of that type, the
		// cast only says which. Converting needs an operation the door does not have (its expression
		// IR is Column / Const / Arith / Case / PeriodTrunc), and it would have to exist in BOTH the
		// SQL provider and the RAM one, in every dialect, with the rounding and the failure mode
		// spelled out. That is its own piece of work, and pretending otherwise here would mean a
		// query that parses and then answers something nobody chose.
		if (cast.m_path.size() == 1) {
			Fail(cast.m_line, cast.m_col, wxString::Format(
				_("CAST narrows a reference to one of its types (%s), it does not convert values: "
				  "'%s' is not a table"),
				wxT("Catalog.Products"), cast.m_path[0]));
			return cols;
		}

		// …AND THE TYPE IT IS NARROWED TO, resolved as a SOURCE — because that is what it is: the
		// table whose fields the walk continues into. Same resolver a FROM goes through, so a cast
		// cannot name something the language could not have named after FROM.
		ibQuerySource target;
		target.m_name = cast.m_path;
		cur = ResolveSource(target, std::map<wxString, ibValue>());
		if (cur == nullptr)
			return cols;   // ResolveSource has already raised, in its own words
		i = 0;   // the whole trailing path is walked on the target
		for (size_t k = 0; k < path.size(); ++k) {
			const ibBackendQueryColumn* col = cur->ResolveColumnByName(path[k]);
			if (col == nullptr) {
				Fail(e.m_line, e.m_col, wxString::Format(_("unknown attribute '%s'"), path[k]));
				return cols;
			}
			cols.push_back(col);
			if (k + 1 < path.size()) {
				const ibBackendQueryable* next = cur->GetProvider().ResolveReferenceTarget(cur, col);
				if (next == nullptr) {
					Fail(e.m_line, e.m_col, wxString::Format(
						_("'%s' is not a single-target reference (cannot walk)"), path[k]));
					return cols;
				}
				cur = next;
			}
		}
		return cols;
	}

	if (path.size() >= 2) {
		const ibBackendQueryable* aliased = SourceForAlias(sources, path[0]);
		if (aliased != nullptr) { cur = aliased; i = 1; }
	}
	if (i == 0)   // unqualified — start on the source that owns the first segment
		if (const ibBackendQueryable* q = OwnerOfBareColumn(sources, path[0], e.m_line, e.m_col)) cur = q;   // ambiguous -> Fail

	for (size_t k = i; k < path.size(); ++k) {
		const ibBackendQueryColumn* col = cur->ResolveColumnByName(path[k]);
		if (col == nullptr) {
			Fail(e.m_line, e.m_col, wxString::Format(_("unknown attribute '%s'"), path[k]));
			return {};
		}
		cols.push_back(col);
		if (k + 1 < path.size()) {
			const ibBackendQueryable* next = cur->GetProvider().ResolveReferenceTarget(cur, col);
			if (next == nullptr) {
				// A COMPOSITE (multi-type) reference at ANY segment: resolve the REPRESENTATIVE chain through
				// the FIRST target (attribute names are the same across types); the provider (BuildPageIR)
				// re-resolves + BRANCHES per type — one JOIN sub-tree per target, COALESCE the leaf. A
				// composite mid-segment forks the path; the provider's recursive walk handles the tree.
				const std::vector<const ibBackendQueryable*> targets = cur->GetProvider().ResolveReferenceTargets(cur, col);
				if (targets.empty()) {
					Fail(e.m_line, e.m_col,
						wxString::Format(_("'%s' is not a single-target reference (cannot walk)"), path[k]));
					return {};
				}
				next = targets.front();
			}
			cur = next;
		}
	}
	return cols;
}

// The queryable that owns the FIRST segment of a path — the dot-walk root. Mirrors ResolvePath's
// start: a qualified `alias.col…` starts on the aliased source, an unqualified one on the source
// that owns the first segment. (Used to expand a dot-walk over a multi-source builder.)
const ibBackendQueryable* RootForPath(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	const std::vector<wxString>& path = e.m_path;
	if (path.size() >= 2) {
		const ibBackendQueryable* aliased = SourceForAlias(sources, path[0]);
		if (aliased != nullptr) return aliased;
	}
	if (const ibBackendQueryable* q = OwnerOfBareColumn(sources, path[0], e.m_line, e.m_col)) return q;   // ambiguous -> Fail
	return sources.empty() ? nullptr : sources[0].m_q;
}

// Expand a reference dot-walk path (size > 1) over a MULTI-SOURCE builder. `ibRefJoinChain` builds
// SQL joins for the single-source door; the RAM stitch has none, so here each NON-leaf segment's
// reference target becomes an explicit LEFT-join leaf, keyed EXACTLY on (segment ref column, target
// self-reference = GetPrimaryKeyColumns().front()) — no `ReferenceColumnTo` ambiguity when a leaf has
// two references to the same target. Joins are deduped by path prefix (`joined`: prefix-key -> target),
// so `c.Owner.Region` and `c.Owner.Code` share ONE Owner join. Returns the leaf column (pathCols.back(),
// owned by the final target) to group / project by — a plain qualified column in the composed snapshot.
const ibBackendQueryColumn* ExpandDotWalkJoins(
	ibDataQueryBuilder& b, const ibBackendQueryable* rootQ,
	const std::vector<const ibBackendQueryColumn*>& pathCols,
	std::map<wxString, const ibBackendQueryable*>& joined, int& aliasSeq, const ibQueryAstExpr& e)
{
	const ibBackendQueryable* curQ = rootQ;
	wxString prefixKey;
	for (size_t i = 0; i + 1 < pathCols.size(); ++i) {
		const ibBackendQueryColumn* refCol = pathCols[i];
		const ibBackendQueryable* tgtQ = (curQ != nullptr) ? curQ->GetProvider().ResolveReferenceTarget(curQ, refCol) : nullptr;
		const std::vector<const ibBackendQueryColumn*> tgtKeys = (tgtQ != nullptr) ? tgtQ->GetPrimaryKeyColumns()
		                                                                           : std::vector<const ibBackendQueryColumn*>{};
		if (tgtQ == nullptr || tgtKeys.empty())
			Fail(e.m_line, e.m_col,
				_("a dot-walk segment over a JOIN/UNION must be a single-target catalog/document reference"));
		prefixKey += wxString::Format(wxT("%p|"), (const void*)refCol);
		if (joined.find(prefixKey) == joined.end()) {
			const wxString alias = wxString::Format(wxT("_dw%d"), aliasSeq++);
			b.Join(tgtQ, refCol, tgtKeys.front(), ibQueryJoinKind::Left, alias);   // explicit keys — no ambiguity
			joined[prefixKey] = tgtQ;
		}
		curQ = tgtQ;
	}
	return pathCols.back();   // owned by the final target — a plain qualified column in the snapshot
}

// Map the L4 AST join kind to the L3 join kind.
ibQueryJoinKind MapJoinKind(ibQueryJoinKindAst k)
{
	switch (k) {
		case ibQueryJoinKindAst::Left:  return ibQueryJoinKind::Left;
		case ibQueryJoinKindAst::Right: return ibQueryJoinKind::Right;
		case ibQueryJoinKindAst::Full:  return ibQueryJoinKind::Full;
		default:                        return ibQueryJoinKind::Inner;
	}
}

// Declare a NAMED ref-join: `JOIN root.refA[.refB…] AS alias` auto-joins the reference chain off `root` and
// binds the FINAL target to `alias` in `sources`, so a later `alias.field` resolves as a clean qualified
// column. Every segment is a single-target reference; intermediate targets get synthetic aliases, the last
// gets the user's alias. Reuses ExpandDotWalkJoins' key derivation (segment ref col, target self-reference).
void ExpandRefJoinAlias(ibDataQueryBuilder& b, std::vector<ibSourceBinding>& sources,
	const ibBackendQueryable* rootQ, const std::vector<wxString>& segs,
	const wxString& finalAlias, ibQueryJoinKind kind, int& aliasSeq)
{
	const ibBackendQueryable* curQ = rootQ;
	for (size_t i = 0; i < segs.size(); ++i) {
		const ibBackendQueryColumn* refCol = (curQ != nullptr) ? curQ->ResolveColumnByName(segs[i]) : nullptr;
		const ibBackendQueryable*   tgtQ   = (curQ != nullptr && refCol != nullptr) ? curQ->GetProvider().ResolveReferenceTarget(curQ, refCol) : nullptr;
		const std::vector<const ibBackendQueryColumn*> tgtKeys = (tgtQ != nullptr) ? tgtQ->GetPrimaryKeyColumns()
		                                                                           : std::vector<const ibBackendQueryColumn*>{};
		if (refCol == nullptr || tgtQ == nullptr || tgtKeys.empty())
			Fail(0, 0, wxString::Format(_("'%s' is not a single-target reference for a named ref-join (AS)"), segs[i]));
		const bool last = (i + 1 == segs.size());
		const wxString alias = last ? finalAlias : wxString::Format(wxT("_rj%d"), aliasSeq++);
		b.Join(tgtQ, refCol, tgtKeys.front(), last ? kind : ibQueryJoinKind::Left, alias);
		sources.push_back({ alias, tgtQ });
		curQ = tgtQ;
	}
}

// Map the L4 AST comparison op to the L3 join op (the join node carries the L3 enum so L3 stays L4-agnostic).
ibJoinCompareOp MapJoinOp(ibQueryCompareOp op)
{
	switch (op) {
		case ibQueryCompareOp::Ne: return ibJoinCompareOp::Ne;
		case ibQueryCompareOp::Lt: return ibJoinCompareOp::Lt;
		case ibQueryCompareOp::Le: return ibJoinCompareOp::Le;
		case ibQueryCompareOp::Gt: return ibJoinCompareOp::Gt;
		case ibQueryCompareOp::Ge: return ibJoinCompareOp::Ge;
		default:                   return ibJoinCompareOp::Eq;
	}
}

const ibValue* FindParam(const std::map<wxString, ibValue>& params, const wxString& name)
{
	for (const auto& kv : params)
		if (kv.first.CmpNoCase(name) == 0) return &kv.second;
	return nullptr;
}

ibValue EvalValue(const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	if (e.m_kind == ibQueryAstExprKind::Literal) return e.m_literal;
	if (e.m_kind == ibQueryAstExprKind::Param) {
		const ibValue* v = FindParam(params, e.m_paramName);
		if (v == nullptr) {
			Fail(e.m_line, e.m_col, wxString::Format(_("parameter '&%s' is not set"), e.m_paramName));
			return ibValue();
		}
		return *v;
	}
	if (e.m_kind == ibQueryAstExprKind::Value) {
		// value(<Kind>.<Name>.<Member>) — a literal reference constant. Resolve the metaobject through the SAME
		// factory a FROM source uses, then read the member's value straight off the queryable's metaobject (Max:
		// "you just get the runtime value off the queryable by name"). The metaobject try-resolves (bool + out); the
		// engine raises the exception HERE so it carries the query source span (Max).
		if (e.m_path.size() < 3)
			Fail(e.m_line, e.m_col, _("value(...) needs <Kind>.<Name>.<Member>"));
		const wxString& ns     = e.m_path.front();
		const wxString& member = e.m_path.back();
		wxString name = e.m_path[1];
		for (size_t i = 2; i + 1 < e.m_path.size(); ++i) name += wxT(".") + e.m_path[i];

		const ibMetaData* md = ibSourceMetaDataScope::Get();
		ibQueryableFactory* factory = md != nullptr ? md->GetSourceFactory() : ibApplicationData::GetQueryableFactory();
		if (factory == nullptr)
			Fail(e.m_line, e.m_col, _("the query engine is not available (no application data)"));
		const ibBackendQueryable* q = factory->Resolve(ns, name);
		if (q == nullptr)
			Fail(e.m_line, e.m_col, wxString::Format(_("value(): metaobject '%s.%s' not found"), ns, name));
		const ibValueMetaObjectGenericData* meta = q->GetSourceMetaObject();
		ibValue out;
		if (meta == nullptr || !meta->ResolveQueryConstant(member, out))
			Fail(e.m_line, e.m_col, wxString::Format(_("value(): '%s.%s' has no '%s' (an empty reference or a predefined item)"), ns, name, member));
		return out;
	}
	Fail(e.m_line, e.m_col, _("expected a literal or a parameter as the comparison value"));
	return ibValue();
}

ibAggregateFn AggFn(ibQueryKeyword kw)
{
	switch (kw) {
	case ibQueryKeyword::Sum:   return ibAggregateFn::Sum;
	case ibQueryKeyword::Count: return ibAggregateFn::Count;
	case ibQueryKeyword::Min:   return ibAggregateFn::Min;
	case ibQueryKeyword::Max:   return ibAggregateFn::Max;
	case ibQueryKeyword::Avg:   return ibAggregateFn::Avg;
	default:                    return ibAggregateFn::Count;
	}
}

// Forward decls used by IN (subquery): build a sub-SELECT into a queryable (owned by `owner`).
using ibSubqueryOwner = std::vector<std::unique_ptr<ibSubqueryQueryable>>;
const ibBackendQueryable* WrapSelectAsQueryable(const ibQuerySelect& sel,
                                                const std::map<wxString, ibValue>& params, ibSubqueryOwner& owner);

// --- WHERE-leaf condition builders. `path` (size > 1) = a reference dot-walk; the provider joins it
// and qualifies the LEAF (== path.back()) by the join alias. A plain column passes path = {col}. ------
ibQueryCondition CondEq(const std::vector<const ibBackendQueryColumn*>& path, const ibValue& v, bool notEqual = false)
{
	ibQueryCondition c;
	c.m_col   = path.back();
	c.m_value = v;
	c.m_op    = notEqual ? ibQueryFilterOp::NotEqual : ibQueryFilterOp::Equal;
	if (path.size() > 1) c.m_path = path;
	return c;
}
ibQueryCondition CondOp(const std::vector<const ibBackendQueryColumn*>& path, ibQueryFilterOp op, const ibValue& v)
{
	ibQueryCondition c;
	c.m_col = path.back(); c.m_value = v; c.m_op = op;
	if (path.size() > 1) c.m_path = path;
	return c;
}

std::vector<const ibBackendQueryColumn*> ResolveWhereTarget(const std::vector<ibSourceBinding>& sources,
                                                            const ibQueryAstExpr& e, bool allowDotWalk);   // defined below

ibQueryColumnExprPtr BuildColumnExprFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params);   // defined below

// Is this AST expression a COMPUTED WHERE / aggregate-input lhs (arithmetic or CASE)?
bool IsComputedExprAst(const ibQueryAstExpr& e)
{
	return e.m_kind == ibQueryAstExprKind::Arith || e.m_kind == ibQueryAstExprKind::Case;
}

// Gate for a computed (arithmetic / CASE) condition lhs / aggregate input / projection: a single DB source
// lowers it server-side; a COMPUTED source (register slice / subquery) evaluates it in RAM per row
// (EvalColumnExprRow — SELECT / WHERE / SUM alike). Only a JOIN across leaves is still unsupported here
// (the RAM stitch has no cross-leaf expression evaluator yet).
void GateComputedExpr(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	if (sources.size() > 1)
		Fail(e.m_line, e.m_col, _("an arithmetic / CASE expression here is not yet supported over a JOIN"));
}

// Build the full boolean WHERE as an L3 predicate TREE (ibQueryPredicate). The door lowers it to
// the L2 IR (OR/NOT/IS NULL all expressible there). IN expands to Or(Eq …), BETWEEN to And(>=, <=),
// NOT IN / NOT BETWEEN / NOT LIKE wrap the positive form in Not — so the tree needs no dedicated node.
// Compare / LIKE / BETWEEN leaves carry a reference dot-walk PATH (the leaf condition's m_path) when
// allowDotWalk; the provider joins it (single-source non-aggregate read). IN / IS NULL stay plain-column
// (no path leaf on those nodes yet). Used for single-source queries + co-located JOIN booleans.
ibQueryPredicatePtr BuildWherePredicate(const std::vector<ibSourceBinding>& sources,
                                        const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params,
                                        bool allowDotWalk)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Logical:
		return ibQueryPredicate::Compose(
			e.m_isOr ? ibQueryPredicateKind::Or : ibQueryPredicateKind::And,
			BuildWherePredicate(sources, *e.m_lhs, params, allowDotWalk),
			BuildWherePredicate(sources, *e.m_rhs, params, allowDotWalk));

	case ibQueryAstExprKind::Not:
		return ibQueryPredicate::Not(BuildWherePredicate(sources, *e.m_lhs, params, allowDotWalk));

	case ibQueryAstExprKind::Compare: {
		// COMPUTED lhs — `Qty * Price > value`, a CASE: the leaf carries the lowered expression
		// (m_expr); the provider compares BuildColumnExpr(lhs) to the value. Gated single-source DB.
		if (IsComputedExprAst(*e.m_lhs)) {
			GateComputedExpr(sources, *e.m_lhs);
			ibQueryCondition c;
			c.m_value = EvalValue(*e.m_rhs, params);
			c.m_expr  = BuildColumnExprFromAst(sources, *e.m_lhs, params);
			switch (e.m_cmp) {
			case ibQueryCompareOp::Eq:                                       break;   // m_op defaults to Equal
			case ibQueryCompareOp::Ne: c.m_op = ibQueryFilterOp::NotEqual;     break;
			case ibQueryCompareOp::Lt: c.m_op = ibQueryFilterOp::Less;         break;
			case ibQueryCompareOp::Le: c.m_op = ibQueryFilterOp::LessEqual;    break;
			case ibQueryCompareOp::Gt: c.m_op = ibQueryFilterOp::Greater;      break;
			case ibQueryCompareOp::Ge: c.m_op = ibQueryFilterOp::GreaterEqual; break;
			}
			return ibQueryPredicate::Leaf(c);
		}
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue val = EvalValue(*e.m_rhs, params);
		switch (e.m_cmp) {
		case ibQueryCompareOp::Eq: return ibQueryPredicate::Leaf(CondEq(cols, val));
		case ibQueryCompareOp::Ne: return ibQueryPredicate::Leaf(CondEq(cols, val, /*notEqual*/true));
		case ibQueryCompareOp::Lt: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::Less,         val));
		case ibQueryCompareOp::Le: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::LessEqual,    val));
		case ibQueryCompareOp::Gt: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::Greater,      val));
		case ibQueryCompareOp::Ge: return ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::GreaterEqual, val));
		}
		return nullptr;
	}

	case ibQueryAstExprKind::Like: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		ibQueryPredicatePtr like = ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::Like, EvalValue(*e.m_rhs, params)));
		return e.m_negated ? ibQueryPredicate::Not(like) : like;
	}

	case ibQueryAstExprKind::Between: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		ibQueryPredicatePtr lo = ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::GreaterEqual, EvalValue(*e.m_low,  params)));
		ibQueryPredicatePtr hi = ibQueryPredicate::Leaf(CondOp(cols, ibQueryFilterOp::LessEqual,    EvalValue(*e.m_high, params)));
		ibQueryPredicatePtr between = ibQueryPredicate::Compose(ibQueryPredicateKind::And, lo, hi);
		return e.m_negated ? ibQueryPredicate::Not(between) : between;
	}

	case ibQueryAstExprKind::In: {
		// col IN (a, b, …)  ->  Or(col=a, col=b, …); NOT IN -> Not of that. Empty list = a vacuous
		// FALSE (Or of nothing); the door tree treats a null child as no-constraint, so guard it.
		// The leaf may be a reference dot-walk (every Eq shares the path -> one join, prefix deduped).
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);

		// Collect the IN values: either the literal list, or — for IN (subquery) — the (uncorrelated)
		// inner SELECT's single output column, materialised eagerly into a value list.
		std::vector<ibValue> values;
		if (e.m_subquery) {
			ibSubqueryOwner localOwner;   // the inner queryable lives only for this materialisation
			const ibBackendQueryable* subq = WrapSelectAsQueryable(*e.m_subquery, params, localOwner);
			const std::vector<const ibBackendQueryColumn*> outCols = subq->GetColumns();
			if (outCols.size() != 1 || outCols.front() == nullptr)
				Fail(e.m_line, e.m_col, _("IN (subquery) must SELECT exactly one column"));
			ibDataQueryBuilder sq;
			sq.From(subq);
			sq.Select(outCols.front(), wxT("v"));
			ibDataQueryResult r = sq.Execute(ibReadPageRequest{});
			while (r.Next())
				values.push_back(r.GetColumn(wxT("v")));
		}
		else {
			// Each list item contributes its value; a COLLECTION item — `col IN arrayVar` with a
			// captured array / value table / computed set — expands into its elements, so a runtime
			// array works as an IN set. A scalar / literal goes in as-is (CreateIterator == null).
			for (const ibQueryAstExprPtr& item : e.m_list) {
				ibValue v = EvalValue(*item, params);
				if (std::shared_ptr<ibValueIteratorState> it = v.CreateIterator()) {
					ibValue elem;
					while (it->MoveNext(elem))
						values.push_back(elem);
				}
				else {
					values.push_back(v);
				}
			}
		}

		ibQueryPredicatePtr acc;
		for (const ibValue& v : values) {
			ibQueryPredicatePtr eq = ibQueryPredicate::Leaf(CondEq(cols, v));
			acc = acc ? ibQueryPredicate::Compose(ibQueryPredicateKind::Or, acc, eq) : eq;
		}
		if (!acc) {
			// Empty IN ( ) — matches NOTHING. Encode as a contradiction (col IS NULL AND col IS NOT NULL);
			// NOT IN of an empty set then matches everything (the outer Not below).
			acc = ibQueryPredicate::Compose(ibQueryPredicateKind::And,
			                                ibQueryPredicate::Null(cols.back(), /*negated*/false, cols),
			                                ibQueryPredicate::Null(cols.back(), /*negated*/true,  cols));
		}
		return e.m_negated ? ibQueryPredicate::Not(acc) : acc;
	}

	case ibQueryAstExprKind::IsNull: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		return ibQueryPredicate::Null(cols.back(), e.m_negated, cols);   // m_negated = IS NOT NULL; path = dot-walk
	}

	case ibQueryAstExprKind::Column: {
		// A BARE column / dot-walk used as a predicate is a TRUTHY test on a Boolean column:
		// `WHERE Field.Seller`  ==  `WHERE Field.Seller = TRUE`. (`= TRUE` lowers as a plain Compare above.)
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, e, allowDotWalk);
		return ibQueryPredicate::Leaf(CondEq(cols, ibValue(true)));
	}

	default:
		Fail(e.m_line, e.m_col, _("unsupported WHERE expression"));
		return nullptr;
	}
}

// Resolve a WHERE / ORDER target to a column PATH: size 1 = a plain column, >1 = a reference dot-walk
// (Producer.Region). Dot-walk is only realizable in a single-source, non-aggregate read (BuildPageIR
// builds the join + qualifies the leaf); reject it elsewhere rather than let the aggregate / stitch
// paths silently drop the filter. allowDotWalk = (single source AND not aggregate).
std::vector<const ibBackendQueryColumn*> ResolveWhereTarget(const std::vector<ibSourceBinding>& sources,
                                                            const ibQueryAstExpr& e, bool allowDotWalk)
{
	if (e.m_kind != ibQueryAstExprKind::Column || e.m_path.empty())
		Fail(e.m_line, e.m_col, _("expected a column (or a reference dot-walk path) here"));
	std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, e);
	if (cols.empty())
		Fail(e.m_line, e.m_col, _("could not resolve the column"));
	if (cols.size() > 1 && !allowDotWalk)
		Fail(e.m_line, e.m_col, _("a reference dot-walk here needs a single, non-aggregate source"));
	return cols;
}

// Flat AND-tree WHERE -> the door's verb conditions. Plain columns AND reference dot-walks (the leaf
// of a path, joined by the provider). Used for a flat single-source WHERE (dot-walk allowed) and a
// flat JOIN WHERE (dot-walk rejected — the composer has no per-leaf dot-walk join yet). OR / NOT / IN
// / IS NULL never reach here (IsFlatAndWhere routes them to the predicate tree).
void LowerFlatWhere(ibDataQueryBuilder& b, const std::vector<ibSourceBinding>& sources,
                    const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params, bool allowDotWalk)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Logical:
		if (e.m_isOr)
			Fail(e.m_line, e.m_col, _("OR in this WHERE is not lowered to flat conditions"));
		LowerFlatWhere(b, sources, *e.m_lhs, params, allowDotWalk);
		LowerFlatWhere(b, sources, *e.m_rhs, params, allowDotWalk);
		return;

	case ibQueryAstExprKind::Compare: {
		// COMPUTED lhs — route to the door's expression verbs (single DB source; gated).
		if (IsComputedExprAst(*e.m_lhs)) {
			GateComputedExpr(sources, *e.m_lhs);
			const ibQueryColumnExprPtr lhs = BuildColumnExprFromAst(sources, *e.m_lhs, params);
			const ibValue val = EvalValue(*e.m_rhs, params);
			switch (e.m_cmp) {
			case ibQueryCompareOp::Eq: b.WhereExpr(lhs, ibQueryFilterOp::Equal,    val); break;
			case ibQueryCompareOp::Ne: b.WhereExpr(lhs, ibQueryFilterOp::NotEqual, val); break;
			case ibQueryCompareOp::Lt: b.WhereExprCompare(lhs, ibQueryFilterOp::Less,         val); break;
			case ibQueryCompareOp::Le: b.WhereExprCompare(lhs, ibQueryFilterOp::LessEqual,    val); break;
			case ibQueryCompareOp::Gt: b.WhereExprCompare(lhs, ibQueryFilterOp::Greater,      val); break;
			case ibQueryCompareOp::Ge: b.WhereExprCompare(lhs, ibQueryFilterOp::GreaterEqual, val); break;
			}
			return;
		}
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue val = EvalValue(*e.m_rhs, params);
		const bool walk = cols.size() > 1;
		switch (e.m_cmp) {
		case ibQueryCompareOp::Eq: walk ? b.Where(cols, ibQueryFilterOp::Equal,    val)
		                                : b.Where(cols[0], ibQueryFilterOp::Equal,    val); break;
		case ibQueryCompareOp::Ne: walk ? b.Where(cols, ibQueryFilterOp::NotEqual, val)
		                                : b.Where(cols[0], ibQueryFilterOp::NotEqual, val); break;
		case ibQueryCompareOp::Lt: walk ? b.WhereCompare(cols, ibQueryFilterOp::Less,         val) : b.WhereCompare(cols[0], ibQueryFilterOp::Less,         val); break;
		case ibQueryCompareOp::Le: walk ? b.WhereCompare(cols, ibQueryFilterOp::LessEqual,    val) : b.WhereCompare(cols[0], ibQueryFilterOp::LessEqual,    val); break;
		case ibQueryCompareOp::Gt: walk ? b.WhereCompare(cols, ibQueryFilterOp::Greater,      val) : b.WhereCompare(cols[0], ibQueryFilterOp::Greater,      val); break;
		case ibQueryCompareOp::Ge: walk ? b.WhereCompare(cols, ibQueryFilterOp::GreaterEqual, val) : b.WhereCompare(cols[0], ibQueryFilterOp::GreaterEqual, val); break;
		}
		return;
	}

	case ibQueryAstExprKind::Like: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue val = EvalValue(*e.m_rhs, params);
		if (cols.size() > 1) b.WhereCompare(cols, ibQueryFilterOp::Like, val);
		else                 b.WhereLike(cols[0], val);
		return;
	}

	case ibQueryAstExprKind::Between: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		const ibValue lo = EvalValue(*e.m_low, params), hi = EvalValue(*e.m_high, params);
		if (cols.size() > 1) {
			b.WhereCompare(cols, ibQueryFilterOp::GreaterEqual, lo);
			b.WhereCompare(cols, ibQueryFilterOp::LessEqual,    hi);
		} else {
			b.WhereCompare(cols[0], ibQueryFilterOp::GreaterEqual, lo);
			b.WhereCompare(cols[0], ibQueryFilterOp::LessEqual,    hi);
		}
		return;
	}

	default:
		Fail(e.m_line, e.m_col, _("this WHERE expression is not a flat condition"));
		return;
	}
}

// A WHERE is "flat" if it is a pure AND-tree of simple comparisons / LIKE / BETWEEN — no OR / NOT /
// IN / IS NULL. A flat JOIN WHERE rides the door's per-leaf verb conditions (work co-located AND in
// the RAM stitch); a boolean one goes through the predicate tree, which only the co-located join path
// lowers (per-leaf qualified) — the stitch path errors clearly rather than under-filter.
bool IsFlatAndWhere(const ibQueryAstExpr& e)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Logical: return !e.m_isOr && IsFlatAndWhere(*e.m_lhs) && IsFlatAndWhere(*e.m_rhs);
	case ibQueryAstExprKind::Compare: return true;
	case ibQueryAstExprKind::Like:    return !e.m_negated;
	case ibQueryAstExprKind::Between: return !e.m_negated;
	default:                       return false;   // Not / In / IsNull
	}
}

// Build an L3 computed-column expression (ibQueryColumnExpr) from an AST expression — arithmetic, CASE,
// a plain column, a literal, or a &parameter. The provider lowers it to the L2 IR and projects it AS an
// alias. Plain columns only (no dot-walk inside a computed expression yet). Single source.
ibQueryColumnExprPtr BuildColumnExprFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Column: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, e);
		if (cols.size() != 1)
			Fail(e.m_line, e.m_col, _("a computed expression takes plain columns (no dot-walk)"));
		return ibQueryColumnExpr::Col(cols[0]);
	}
	case ibQueryAstExprKind::Literal:
		return ibQueryColumnExpr::Const(e.m_literal);
	case ibQueryAstExprKind::Param:
	case ibQueryAstExprKind::Value:   // value(...) resolves to a constant, exactly like a &parameter
		return ibQueryColumnExpr::Const(EvalValue(e, params));

	case ibQueryAstExprKind::Arith: {
		const ibQueryColumnArithOp op =
			e.m_arith == ibQueryArithOp::Add ? ibQueryColumnArithOp::Add
			: e.m_arith == ibQueryArithOp::Sub ? ibQueryColumnArithOp::Sub
			: e.m_arith == ibQueryArithOp::Mul ? ibQueryColumnArithOp::Mul
			: e.m_arith == ibQueryArithOp::Div ? ibQueryColumnArithOp::Div
			                                   : ibQueryColumnArithOp::Mod;
		return ibQueryColumnExpr::Arith(op, BuildColumnExprFromAst(sources, *e.m_lhs, params),
		                                    BuildColumnExprFromAst(sources, *e.m_rhs, params));
	}

	case ibQueryAstExprKind::Case: {
		auto c = std::make_shared<ibQueryColumnExpr>();
		c->m_kind = ibQueryColumnExprKind::Case;
		for (const auto& wt : e.m_cases)
			c->m_cases.emplace_back(BuildWherePredicate(sources, *wt.first, params, /*allowDotWalk*/false),
			                        BuildColumnExprFromAst(sources, *wt.second, params));
		if (e.m_else)
			c->m_else = BuildColumnExprFromAst(sources, *e.m_else, params);
		return c;
	}

	default:
		Fail(e.m_line, e.m_col, _("unsupported expression in a computed column"));
		return nullptr;
	}
}

// A synthetic scalar column the totals lowering builds: it reads a value the door projected under a
// distinct cursor alias (a computed measure `1 AS test`; a dot-walk dimension's leaf `Parent.Code`),
// STRAIGHT off the cursor by that alias, under a UNIQUE synthetic model id. This is what lets the
// metaID-keyed totals fold read it as a normal column — AND keeps a self-referential dimension
// (Parent.Code, whose leaf shares a metaID with the row's own attribute) from clashing with the main
// table. The id sits in its own high range, clear of real metaIDs AND the COUNT(*) synthetic receivers
// (kAggSyntheticBase = 0x40000000).
const ibMetaID kSyntheticColumnBase = 0x50000000u;

class ibSyntheticScalarColumn : public ibRawDBColumn
{
public:
	ibSyntheticScalarColumn(const wxString& alias, ibMetaID id, RawType type = RawType::Number)
		: ibRawDBColumn(alias, type), m_id(id) {}
	ibMetaID GetColumnId() const override { return m_id; }
private:
	ibMetaID m_id;
};

// ⭐ AN OUTPUT COLUMN THAT IS A COLUMN. Name, TYPE (whole, reference and all) and an id of its own.
//
// A query's output used to be a column only when a real source column stood behind it. Everything
// read BY ALIAS — a dot-walk, an aggregate, a computed expression, any projection of a JOIN — had a
// null m_col, and a null column is nobody:
//
//   * the list's fetch keys each row's values BY COLUMN ID (listFetchDriver) and skipped them, so
//     the values never arrived;
//   * a form could see the field in the source explorer and not bind to it, because there was no id
//     to bind (Max: "I cannot use such elements on a form");
//   * where a foreign column WAS borrowed for the id, the binding pointed at another table's
//     attribute — right-looking and wrong;
//   * and a temp table built from such a schema had columns with no type (the hop had nothing to
//     walk into).
//
// So every output column gets one, minted here, owned by the schema (OutputColumn::m_ownedCol) so it
// outlives the door that made it — the schema travels with the selection. Unlike the scalar synthetic
// above it carries a full ibTypeDescription, because an output can perfectly well BE a reference.
class ibSyntheticOutputColumn : public ibBackendQueryColumn
{
public:
	ibSyntheticOutputColumn(const wxString& name, const ibTypeDescription& type, ibMetaID id)
		: m_name(name), m_type(type), m_id(id) {}

	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }   // the interface hands back a non-const ref
	ibMetaID           GetColumnId()     const override { return m_id; }

private:
	wxString                  m_name;
	mutable ibTypeDescription m_type;   // mutable: GetTypeDesc() is const and returns a non-const ref
	ibMetaID                  m_id;
};

// The raw read-type of a PLAIN SCALAR column (string / number / date / bool, single CLSID). Returns false
// for a reference / enum / composite leaf — those are not single-field scalars and cannot ride a synthetic
// raw column (a multi-type totals dimension is a separate feature).
bool ScalarRawType(const ibBackendQueryColumn* col, ibRawDBColumn::RawType& out)
{
	const ibTypeDescription& td = col->GetTypeDesc();
	if (td.GetClsidCount() != 1) return false;
	if      (td.ContainType(ibValueTypes::TYPE_STRING))  out = ibRawDBColumn::RawType::String;
	else if (td.ContainType(ibValueTypes::TYPE_NUMBER))  out = ibRawDBColumn::RawType::Number;
	else if (td.ContainType(ibValueTypes::TYPE_DATE))    out = ibRawDBColumn::RawType::Date;
	else if (td.ContainType(ibValueTypes::TYPE_BOOLEAN)) out = ibRawDBColumn::RawType::Boolean;
	else return false;
	return true;
}

// WHAT SHALL WE CALL IT — a different question from "what is it called". The name it HAS comes from
// ibQueryOutputName (one answer, one place, shared with the constructor); only where there is
// none does this invent one, which is the part that belongs to execution alone.
// WHAT TO CALL AN OUTPUT COLUMN AT EXECUTION. The proposal first — the SAME answer every host uses
// when it adds a field, so a query assembled in the constructor and one typed by hand produce the
// same column names — and only where there is no proposal does this invent one.
wxString OutputNameFor(const ibQuerySelect& select, const ibQueryProjection& p, int idx)
{
	const wxString named = ibQueryProposedName(select, p);
	if (!named.IsEmpty()) return named;

	const ibQueryAstExpr& e = *p.m_expr;
	if (e.m_kind == ibQueryAstExprKind::Func) {
		const wxString f = ibQueryKeywordText(e.m_func);
		if (e.m_star) return f + wxT("_all");
		return f + wxT("_") + (e.m_arg && !e.m_arg->m_path.empty() ? e.m_arg->m_path.back() : wxString());
	}
	return wxString::Format(wxT("col%d"), idx);
}

// ibSubqueryOwner owns the ibSubqueryQueryable instances built for a single Execute — they must outlive
// the door's terminal call (declared above for IN-subquery). RAM-materialised on Execute, so a local
// list living to the return statement is enough (no cross-call lifetime). (docs §22 / §23)
bool PopulateBuilder(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                     const std::vector<ibSourceBinding>& sources, ibDataQueryBuilder& b,
                     std::vector<OutputColumn>& outSchema, bool asSubquery);

// Resolve a FROM / source to a queryable: a plain metaobject source via the factory, or a nested
// SELECT wrapped in ibSubqueryQueryable (built recursively, its own FROM resolved the same way).
const ibBackendQueryable* ResolveFrom(const ibQuerySource& src,
                                      const std::map<wxString, ibValue>& params,
                                      ibSubqueryOwner& owner)
{
	if (!src.m_subquery)
		return ResolveSource(src, params);
	return WrapSelectAsQueryable(*src.m_subquery, params, owner);   // FROM (SELECT …) AS alias
}

// Populate the door from a single SELECT's clauses (projections / GROUP BY / HAVING / WHERE / ORDER /
// DISTINCT). Shared by the top-level execute, nested subqueries, and JOIN queries. The source set
// (1 = single source, >1 = JOIN) drives column resolution. explicitProjection (a subquery's inner
// query OR any multi-source query) PROJECTS plain columns via Select(col, alias) so the output schema
// is explicit; a plain single-source query instead records them in outSchema and reads the result
// column directly. Returns whether the query is in aggregate mode (GROUP BY / aggregate projection).
bool PopulateBuilder(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                     const std::vector<ibSourceBinding>& sources, ibDataQueryBuilder& b,
                     std::vector<OutputColumn>& outSchema, bool asSubquery)
{
	const bool multiSource      = sources.size() > 1;
	const bool explicitProjection = asSubquery || multiSource;
	// A COMPUTED primary (subquery / virtual table) materialises in RAM — reference dot-walk
	// joins and dot-walk aggregate inputs have no DB join to ride there; reject rather than
	// push a path leaf as a plain column (silently wrong rows).
	const bool computedPrimary  = sources.size() == 1 && sources[0].m_q != nullptr
	                              && sources[0].m_q->IsComputedInRam();
	std::map<wxString, const ibBackendQueryable*> dwJoined; int dwAliasSeq = 0;   // dot-walk join dedup (multi-source projection)

	// The id pool for the output columns this query has to mint — its own high range, clear of real
	// metaIDs (see kSyntheticColumnBase).
	ibMetaID nextOutputId = kSyntheticColumnBase;

	// EVERY OUTPUT IS A COLUMN. Where a branch below found a real one (a plain read, or a non-scalar
	// dot-walk leaf reassembled by prefix) it stands; where the value is read BY ALIAS and nothing
	// backs it, one is minted — so the output has an identity and a type like any other column.
	auto giveIdentity = [&nextOutputId](OutputColumn& oc) {
		if (oc.m_col != nullptr)
			return;
		auto column = std::make_shared<ibSyntheticOutputColumn>(oc.m_name, oc.m_type, nextOutputId++);
		oc.m_col      = column.get();
		oc.m_ownedCol = column;
	};

	bool aggregate = !ast.m_groupBy.empty();
	for (const ibQueryProjection& p : ast.m_projections)
		if (p.m_expr && p.m_expr->m_kind == ibQueryAstExprKind::Func) aggregate = true;

	// projections -> output schema (+ door select for dot-walk / aggregates / explicit projection)
	outSchema.clear();
	if (ast.m_selectAll) {
		// SELECT * — every column of every source (a JOIN flattens all sides; a single source = its own).
		for (const ibSourceBinding& s : sources)
			for (const ibBackendQueryColumn* c : s.m_q->GetColumns()) {
				OutputColumn oc;
				oc.m_name = c->GetName();
				oc.m_type = c->GetTypeDesc();   // the column IS the output: its type travels whichever way it is read
				if (explicitProjection) { b.Select(c, c->GetName()); oc.m_alias = c->GetName(); oc.m_byAlias = true; }
				else                    { oc.m_col = c; }
				giveIdentity(oc);
				outSchema.push_back(oc);
			}
	}
	else {
		int idx = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			const ibQueryAstExpr& e = *p.m_expr;
			const wxString alias = OutputNameFor(ast, p, idx++);
			OutputColumn oc;
			oc.m_name = alias;

			if (e.m_kind == ibQueryAstExprKind::Func) {
				// Aggregate input: a plain column, a reference dot-walk leaf (SUM(Producer.Weight)), or a
				// COMPUTED expression (SUM(Qty * Price) — the provider lowers it; single DB source, gated).
				if (e.m_star) {
					b.Aggregate(AggFn(e.m_func), (const ibBackendQueryColumn*)nullptr, alias, e.m_distinctArg);
				}
				else if (e.m_arg && IsComputedExprAst(*e.m_arg)) {
					GateComputedExpr(sources, *e.m_arg);
					b.Aggregate(AggFn(e.m_func), BuildColumnExprFromAst(sources, *e.m_arg, params), alias, e.m_distinctArg);
				}
				else {
					const std::vector<const ibBackendQueryColumn*> argCols = ResolvePath(sources, *e.m_arg);
					// A dot-walk aggregate input over a COMPUTED source is resolved in RAM (the provider LEFT-joins
					// the reference leaf via ResolveComputedDotWalks, then aggregates it); a JOIN expands SQL leaves.
					if (argCols.size() > 1 && multiSource) {
						// dot-walk aggregate input over a JOIN — expand the ref path into LEFT-join leaves and
						// aggregate the qualified leaf (same mechanism as the TOTALS dimension / projection).
						const ibBackendQueryColumn* dwLeaf =
							ExpandDotWalkJoins(b, RootForPath(sources, *e.m_arg), argCols, dwJoined, dwAliasSeq, *e.m_arg);
						b.Aggregate(AggFn(e.m_func), dwLeaf, alias, e.m_distinctArg);
					}
					else
						b.Aggregate(AggFn(e.m_func), argCols, alias, e.m_distinctArg);
				}
				oc.m_alias = alias;
				oc.m_byAlias = true;
			}
			else if (e.m_kind == ibQueryAstExprKind::Column) {
				const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, e);
				if (aggregate) {
					// AGGREGATE mode: a projected column is a GROUP BY key (a non-aggregate output must be
					// grouped). The provider GROUPS BY + projects it (plain or dot-walk leaf) and the result is
					// read back by the leaf column — NOT via SelectPath (the read-path machinery).
					// A dot-walk GROUP BY key over a COMPUTED source is RAM-joined by the provider; over a JOIN it
					// expands SQL join leaves; either way the key IS the leaf column (read back by GetColumnId).
					oc.m_col = (pathCols.size() > 1 && multiSource)
						? ExpandDotWalkJoins(b, RootForPath(sources, e), pathCols, dwJoined, dwAliasSeq, e)   // JOIN -> expand ref path
						: pathCols.back();
				}
				else if (pathCols.size() == 1 && !explicitProjection) {
					oc.m_col  = pathCols[0];
					oc.m_type = pathCols[0]->GetTypeDesc();
				}
				else if (pathCols.size() == 1) {
					b.Select(pathCols[0], alias);   // explicit: project the plain column under its alias
					oc.m_alias = alias;
					oc.m_byAlias = true;
					oc.m_type = pathCols[0]->GetTypeDesc();   // read by alias, but it is still THAT column
				}
				else if (multiSource) {
					// MULTI-SOURCE dot-walk projection — `SelectPath` is the single-source door's join; the RAM
					// stitch has none. Expand the path into explicit LEFT-join leaves (ExpandDotWalkJoins) and
					// project the qualified leaf column, read back by alias (a scalar value or a whole reference
					// cell). Paths sharing a prefix reuse one join via dwJoined.
					const ibBackendQueryColumn* dwLeaf =
						ExpandDotWalkJoins(b, RootForPath(sources, e), pathCols, dwJoined, dwAliasSeq, e);
					b.Select(dwLeaf, alias);
					oc.m_alias = alias;
					oc.m_byAlias = true;
					oc.m_type = dwLeaf->GetTypeDesc();   // the LEAF of the walk is what the output holds
				}
				else {
					b.SelectPath(pathCols, alias);   // records m_dotWalks — the provider resolves it (SQL join / computed-source RAM join)
					if (computedPrimary) {
						// A COMPUTED source resolves the dot-walk in RAM (ibComputedProvider::ResolveComputedDotWalks
						// materialises the reference targets + LEFT-joins the leaf in, keyed by GetColumnId), so the
						// leaf — scalar OR a whole reassembled reference/enum cell — is read DIRECTLY by its column, not
						// by an SQL alias. (The physical path below aliases it in SQL and reads by alias.)
						oc.m_col  = pathCols.back();
						oc.m_type = pathCols.back()->GetTypeDesc();
					}
					else {
						oc.m_alias = alias;
						oc.m_byAlias = true;
						oc.m_type   = pathCols.back()->GetTypeDesc();
						// A NON-scalar leaf (reference / enum / composite) is read by reassembling its full field
						// spread (the provider projects it under the alias prefix). A plain single-primitive scalar
						// keeps the by-alias single-field read. Same type test as the provider's scalar/object split.
						const ibTypeDescription& ltd = pathCols.back()->GetTypeDesc();
						const bool plainScalar = ltd.GetClsidCount() == 1
							&& (ltd.ContainType(ibValueTypes::TYPE_NUMBER) || ltd.ContainType(ibValueTypes::TYPE_STRING)
								|| ltd.ContainType(ibValueTypes::TYPE_DATE) || ltd.ContainType(ibValueTypes::TYPE_BOOLEAN));
						if (!plainScalar) {
							oc.m_objectPrefix = alias;
							oc.m_col          = pathCols.back();
						}
					}
				}
			}
			else if (e.m_kind == ibQueryAstExprKind::Arith || e.m_kind == ibQueryAstExprKind::Case) {
				// COMPUTED column (a * b, CASE …). The provider lowers the L3 expression tree + projects it
				// AS the alias. NON-AGGREGATE only: a single DB source projects it server-side, a JOIN /
				// computed source evaluates it per row in the composer (EvalColumnExprRow).
				if (aggregate)   // single source -> SQL; JOIN -> composer RAM-eval; OVER aggregates only is unsupported
					Fail(e.m_line, e.m_col, _("a computed column (arithmetic / CASE) over aggregates is not supported"));
				// A computed column over a COMPUTED source evaluates in RAM (ibComputedProvider::ExecuteRead —
				// EvalColumnExprRow per row, projected under the alias). Over aggregates it stays unsupported (above).
				b.SelectExpr(BuildColumnExprFromAst(sources, e, params), alias);
				oc.m_alias = alias;
				oc.m_byAlias = true;
			}
			else if (e.m_kind == ibQueryAstExprKind::Value || e.m_kind == ibQueryAstExprKind::Param) {
				// SELECT value(<Kind>.<Name>.<Member>) [AS x] / SELECT &param [AS x] — project a CONSTANT column
				// (an empty ref / a predefined item / a bound &parameter value), resolved now. Common to tag a
				// UNION branch or seed a constant column.
				b.SelectExpr(ibQueryColumnExpr::Const(EvalValue(e, params)), alias);
				oc.m_alias = alias;
				oc.m_byAlias = true;
			}
			else {
				Fail(e.m_line, e.m_col, _("unsupported projection expression"));
			}
			giveIdentity(oc);
			outSchema.push_back(oc);
		}
	}

	// GROUP BY — plain column OR a reference dot-walk leaf (GROUP BY Producer.Region). The path overload
	// routes size-1 to a plain key; the provider joins a longer path (single source only — JOIN is A1).
	for (const ibQueryAstExprPtr& g : ast.m_groupBy) {
		const std::vector<const ibBackendQueryColumn*> gcols = ResolvePath(sources, *g);
		// A dot-walk GROUP BY over a COMPUTED source is RAM-joined by the provider (ExecuteAggregate resolves
		// m_groupPaths); a JOIN expands SQL join leaves; a single physical source auto-joins the ref chain.
		if (gcols.size() > 1 && multiSource)
			b.GroupBy(ExpandDotWalkJoins(b, RootForPath(sources, *g), gcols, dwJoined, dwAliasSeq, *g));   // JOIN -> expand ref path
		else
			b.GroupBy(gcols);
	}

	// HAVING — aggregate <op> value (ordered ops only; the door's filter-op set has no = / <>)
	// Over a COMPUTED source the aggregate folds in RAM, which does not apply HAVING — reject
	// rather than silently return unfiltered groups.
	if (ast.m_having && sources.size() == 1 && sources[0].m_q != nullptr && sources[0].m_q->IsComputedInRam())
		Fail(ast.m_having->m_line, ast.m_having->m_col,
			_("HAVING over a computed source (subquery / virtual table) is not yet supported"));
	if (ast.m_having) {
		// ⚠ ONE HAVING PER AND-TERM. `Having()` is AND-folded by the builder (dataQueryBuilder.h), so
		// several group filters compose exactly as they read — and they arrive together routinely
		// now that a condition over an aggregate is MOVED here from WHERE wherever it was written
		// (queryRewrite's rule). Reading only the whole expression meant two group filters written
		// side by side were refused as "not a comparison", which they each plainly were.
		std::vector<ibQueryAstExprPtr> terms;
		ibQueryFlattenAnd(ast.m_having, terms);

		for (const ibQueryAstExprPtr& term : terms) {
			const ibQueryAstExpr& h = *term;
			if (h.m_kind != ibQueryAstExprKind::Compare || !h.m_lhs
			    || h.m_lhs->m_kind != ibQueryAstExprKind::Func)
				Fail(h.m_line, h.m_col, _("HAVING must compare an aggregate function to a value"));
			const ibQueryAstExpr& f = *h.m_lhs;
			const ibBackendQueryColumn* col = f.m_star ? nullptr : ResolveColumnSingle(sources, *f.m_arg);
			const ibValue val = EvalValue(*h.m_rhs, params);
			// ⚠ EQUALITY TOO. `HavingItem` carries a full ibQueryFilterOp and always did — the four
			// ordered ops were this switch's limit, not the mechanism's, and `HAVING COUNT(x) = 1`
			// (exactly one) is as ordinary a thing to ask as "more than one". Refusing it sent the
			// author looking for a workaround for a comparison the builder could already carry.
			ibQueryFilterOp op = ibQueryFilterOp::Greater;
			switch (h.m_cmp) {
			case ibQueryCompareOp::Eq: op = ibQueryFilterOp::Equal;        break;
			case ibQueryCompareOp::Ne: op = ibQueryFilterOp::NotEqual;     break;
			case ibQueryCompareOp::Lt: op = ibQueryFilterOp::Less;         break;
			case ibQueryCompareOp::Le: op = ibQueryFilterOp::LessEqual;    break;
			case ibQueryCompareOp::Gt: op = ibQueryFilterOp::Greater;      break;
			case ibQueryCompareOp::Ge: op = ibQueryFilterOp::GreaterEqual; break;
			default: Fail(h.m_line, h.m_col, _("HAVING compares an aggregate with =, <>, <, <=, > or >=")); break;
			}
			b.Having(AggFn(f.m_func), col, op, val);
		}
	}

	// Dot-walk in WHERE / ORDER is realizable only on a single-source, non-aggregate, PHYSICAL READ
	// (the provider's BuildPageIR builds the reference join + qualifies the leaf). An aggregate /
	// JOIN / computed-source query rejects it (a computed source has no DB join to ride).
	const bool allowDotWalk = !aggregate && !multiSource && !computedPrimary;

	// WHERE — a FLAT AND-of-simple WHERE rides the door's verb conditions (plain columns + dot-walk
	// leaves); a BOOLEAN WHERE (OR / NOT / IN / IS NULL) goes through the predicate tree. The tree
	// supports the full boolean for a single source — INCLUDING dot-walk leaves (Compare/LIKE/BETWEEN)
	// when allowDotWalk; the provider joins them. For a JOIN the tree lowers only in the co-located
	// path (the stitch path errors), and a dot-walk leaf there is rejected (allowDotWalk is false).
	if (ast.m_where) {
		if (IsFlatAndWhere(*ast.m_where))
			// A COMPUTED source resolves a flat dot-walk WHERE (Ref.Field = X) in RAM: the provider joins the
			// reference leaf and filters by it (the register cannot). The boolean predicate path stays gated.
			LowerFlatWhere(b, sources, *ast.m_where, params, allowDotWalk || computedPrimary);
		else
			// A COMPUTED source resolves a boolean dot-walk WHERE (Ref.A = X OR Ref.B = Y) in RAM too: the
			// provider joins the leaves (predicate-tree gather) and FilterRows evaluates the tree by the leaf.
			b.Where(BuildWherePredicate(sources, *ast.m_where, params, allowDotWalk || computedPrimary));
	}

	// ORDER BY — plain column or reference dot-walk leaf. A COMPUTED source resolves the dot-walk leaf in
	// RAM (ibComputedProvider::ResolveComputedDotWalks joins it, keyed by GetColumnId; the RAM sort keys on
	// s.m_col == the leaf), so ORDER BY a reference field is allowed there too — the physical path SQL-joins
	// it instead, but either way the sort keys on the leaf column.
	const bool allowOrderDotWalk = allowDotWalk || computedPrimary;
	for (const ibQueryOrderItem& o : ast.m_orderBy) {
		const ibQueryAstExpr& oe = *o.m_expr;
		// ORDER BY <expression> — a CASE / arithmetic ("sort by a condition") or a bare constant (value(...) /
		// &parameter): lower it to an EXPRESSION sort. Single DB source only (like the computed WHERE side); a
		// computed sort is no keyset key, so the text-query full read is the user. A plain column / dot-walk keeps
		// the column sort below (it CAN keyset).
		if (IsComputedExprAst(oe)
		    || oe.m_kind == ibQueryAstExprKind::Param
		    || oe.m_kind == ibQueryAstExprKind::Value
		    || oe.m_kind == ibQueryAstExprKind::Literal) {
			if (computedPrimary)
				Fail(oe.m_line, oe.m_col, _("ORDER BY an expression over a computed source is not yet supported: sort by a column"));
			if (IsComputedExprAst(oe))
				GateComputedExpr(sources, oe);
			b.OrderByExpr(BuildColumnExprFromAst(sources, oe, params), o.m_ascending);
			continue;
		}
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, oe, allowOrderDotWalk);
		if (cols.size() > 1) b.OrderBy(cols, o.m_ascending);
		else                 b.OrderBy(cols[0], o.m_ascending);
	}

	if (ast.m_distinct)
		b.Distinct();

	return aggregate;
}

// Build a SELECT's CORE (projections / FROM / WHERE / GROUP — NOT order/totals/unions) into an inner
// door and wrap it in ibSubqueryQueryable (owned in `owner`). Used for a subquery source AND for each
// branch of a UNION (the branch is itself a sub-SELECT). The wrapper exposes the branch's output
// columns (by their Select alias) — so the outer query / the UNION matches columns by name.
const ibBackendQueryable* WrapSelectAsQueryable(const ibQuerySelect& sel,
                                                const std::map<wxString, ibValue>& params,
                                                ibSubqueryOwner& owner)
{
	if (!sel.m_joins.empty() || sel.m_hasTotals)
		Fail(0, 0, _("a subquery / UNION branch may not use JOIN or TOTALS yet"));

	const ibBackendQueryable* qi = ResolveFrom(sel.m_from, params, owner);   // recurse — nested subqueries
	ibDataQueryBuilder inner;
	inner.From(qi, sel.m_from.m_alias);

	std::vector<OutputColumn> innerSchema;
	const std::vector<ibSourceBinding> innerSources{ { sel.m_from.m_alias, qi } };
	// An AGGREGATE inner (GROUP BY / aggregate projections) is fine: the wrapper detects it from the
	// builder, exposes the group keys + a synthetic column per aggregate alias, and ComputeRows runs
	// SelectAggregate. The outer's pushed-down conditions post-filter the materialised rows.
	PopulateBuilder(sel, params, innerSources, inner, innerSchema, /*asSubquery*/true);

	// ibSubqueryQueryable copies the inner door (shares its owned raw columns via shared_ptr), so the
	// local 'inner' may die here — the copy is self-sufficient. The wrapper itself lives in 'owner'.
	// sel.m_top (SELECT TOP n in the branch / subquery) limits the materialised rows.
	auto wrapped = std::unique_ptr<ibSubqueryQueryable>(new ibSubqueryQueryable(inner, sel.m_top));
	const ibBackendQueryable* result = wrapped.get();
	owner.push_back(std::move(wrapped));
	return result;
}

// UNION — every branch (the first SELECT's core + each m_unions branch) is wrapped as a queryable and
// stacked vertically; the trailing ORDER BY applies to the whole. The composer realizes the stack
// (RAM union today, co-located where possible). Columns match BY NAME across branches.
ibDataQueryResult LowerUnion(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                            std::vector<OutputColumn>& outSchema, ibSubqueryOwner& owner)
{
	// First branch = ast's CORE (strip the whole-union ORDER / TOTALS / the union list itself).
	// Its TOP is stripped too: on the first core it means the WHOLE-union limit (like the trailing
	// ORDER BY) and applies at the final Execute; a later branch's TOP limits that branch only.
	// FOR UPDATE cannot mean anything here and must not pretend to. A union is composed — its rows
	// are stacked out of branch results, so there is nothing left for the driver to hold. Saying so
	// is the honest answer; carrying the flag into a read that cannot honour it would leave an
	// author believing the rows were locked when they were not.
	if (ast.m_forUpdate)
		Fail(0, 0, _("FOR UPDATE cannot be used with UNION: a composed result holds no rows to lock"));

	ibQuerySelect core0 = ast;
	core0.m_orderBy.clear();
	core0.m_unions.clear();
	core0.m_totalsBy.clear();
	core0.m_totalsAggregates.clear();
	core0.m_totalsOverall = false;
	core0.m_hasTotals = false;
	core0.m_top = 0;

	const ibBackendQueryable* b0 = WrapSelectAsQueryable(core0, params, owner);

	ibDataQueryBuilder b;
	b.From(b0);
	b.Allowed(ast.m_allowed);   // the flag reaches every read of this statement, the stack included

	// The union's output = the first branch's columns (read back by name); each is the output schema.
	outSchema.clear();
	for (const ibBackendQueryColumn* c : b0->GetColumns()) {
		if (c == nullptr) continue;
		b.Select(c, c->GetName());
		OutputColumn oc; oc.m_name = c->GetName(); oc.m_alias = c->GetName(); oc.m_byAlias = true;
		outSchema.push_back(oc);
	}

	// Each branch carries its UNION-vs-ALL flag: plain UNION dedupes the accumulated rows at its
	// operator (SQL left-assoc semantics), UNION ALL keeps duplicates.
	for (const std::shared_ptr<ibQuerySelect>& u : ast.m_unions)
		b.Union(WrapSelectAsQueryable(*u, params, owner), wxEmptyString, /*keepDuplicates*/ u->m_unionAll);

	// ORDER BY on the whole union — resolve against the first branch's columns (by name).
	const std::vector<ibSourceBinding> usrc{ { wxEmptyString, b0 } };
	for (const ibQueryOrderItem& o : ast.m_orderBy)
		b.OrderBy(ResolveColumnSingle(usrc, *o.m_expr), o.m_ascending);

	ibReadPageRequest page;
	page.m_count = ast.m_top;   // TOP on the first core = the whole-union row limit (0 = all)
	return b.Execute(page);
}

// Build the FROM + JOIN source tree into `b` and the `sources` bindings: the primary source, then each JOIN —
// a named ref-join (`JOIN a.ref AS x`), a CROSS (`ON TRUE`), a comparison ON (`a.x <op> b.y` — plain columns
// hash/theta, a computed side -> RAM theta), or an auto-join by reference (no ON). Shared by the read
// (ExecuteImpl) and the totals (ExecuteTotals) single-/JOIN-source paths, so every JOIN feature lives in ONE
// place. The caller has already taken the UNION path (its own vertical stack) before calling this.
void BuildSourceTree(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                     ibSubqueryOwner& owner, std::vector<ibSourceBinding>& sources, ibDataQueryBuilder& b)
{
	const ibBackendQueryable* q0 = ResolveFrom(ast.m_from, params, owner);
	sources.push_back({ ast.m_from.m_alias, q0 });
	b.From(q0, ast.m_from.m_alias);

	int refJoinSeq = 0;   // synthetic aliases for the intermediate segments of a named ref-join
	for (const ibQueryAstJoin& j : ast.m_joins) {
		const ibQueryJoinKind kind = MapJoinKind(j.m_kind);

		// Named ref-join: `JOIN rootAlias.refA[.refB…] AS alias` — a reference PATH off an existing source
		// (not a metaobject), no ON. Auto-join the chain and bind the FINAL target to `alias`, so a later
		// `alias.field AS x` is a clean qualified column. The first segment must be a live source alias.
		const ibBackendQueryable* refRoot = nullptr;
		if (!j.m_source.m_subquery && !j.m_on && j.m_source.m_name.size() >= 2
		    && (refRoot = SourceForAlias(sources, j.m_source.m_name[0])) != nullptr) {
			if (j.m_source.m_alias.empty())
				Fail(0, 0, _("a reference-path JOIN (alias.field) needs an explicit alias (AS)"));
			ExpandRefJoinAlias(b, sources, refRoot,
				std::vector<wxString>(j.m_source.m_name.begin() + 1, j.m_source.m_name.end()),
				j.m_source.m_alias, kind, refJoinSeq);
			continue;
		}

		const ibBackendQueryable* qi = ResolveFrom(j.m_source, params, owner);
		const wxString alias = j.m_source.m_alias;
		RequireAliasFree(sources, alias, 0, 0);   // duplicate alias -> Fail
		sources.push_back({ alias, qi });
		if (j.m_on && j.m_on->m_kind == ibQueryAstExprKind::Literal && j.m_on->m_literal.GetBoolean()) {
			b.CrossJoin(qi, kind, alias);   // ON TRUE -> cross join (cartesian)
		}
		else if (j.m_on) {
			if (j.m_on->m_kind != ibQueryAstExprKind::Compare)
				Fail(j.m_on->m_line, j.m_on->m_col, _("a JOIN ON clause must be a single comparison (a.x <op> b.y) or TRUE (cross join)"));
			// A CONSTANT side (&parameter / value(...) / literal) makes the ON a FILTER, not a join key —
			// `JOIN b ON b.x <op> &p` is, by SQL, a cross join filtered on b.x (Max: params/values usable in a JOIN
			// ON like anywhere a value goes). Emit it as cross-join + WHERE on the column side. Filter-in-ON differs
			// from filter-in-WHERE only for OUTER joins (it pre-filters the null-padded side), so gate to INNER —
			// an outer join wanting this writes the condition in WHERE.
			const auto isValueExpr = [](const ibQueryAstExpr& x) {
				return x.m_kind == ibQueryAstExprKind::Literal
					|| x.m_kind == ibQueryAstExprKind::Param
					|| x.m_kind == ibQueryAstExprKind::Value;
			};
			const bool lhsVal = isValueExpr(*j.m_on->m_lhs);
			const bool rhsVal = isValueExpr(*j.m_on->m_rhs);
			if (lhsVal != rhsVal) {
				if (kind != ibQueryJoinKind::Inner)
					Fail(j.m_on->m_line, j.m_on->m_col, _("a constant JOIN ON (column <op> value/&param) is only supported for an INNER join: put the condition in WHERE for an outer join"));
				const ibQueryAstExpr& colE = rhsVal ? *j.m_on->m_lhs : *j.m_on->m_rhs;
				const ibQueryAstExpr& valE = rhsVal ? *j.m_on->m_rhs : *j.m_on->m_lhs;
				// When the value is on the LEFT the comparison direction flips for `col <op> value`.
				ibQueryCompareOp cmp = j.m_on->m_cmp;
				if (lhsVal) switch (cmp) {
					case ibQueryCompareOp::Lt: cmp = ibQueryCompareOp::Gt; break;
					case ibQueryCompareOp::Le: cmp = ibQueryCompareOp::Ge; break;
					case ibQueryCompareOp::Gt: cmp = ibQueryCompareOp::Lt; break;
					case ibQueryCompareOp::Ge: cmp = ibQueryCompareOp::Le; break;
					default: break;   // Eq / Ne are symmetric
				}
				const ibQueryFilterOp fop =
					cmp == ibQueryCompareOp::Eq ? ibQueryFilterOp::Equal
					: cmp == ibQueryCompareOp::Ne ? ibQueryFilterOp::NotEqual
					: cmp == ibQueryCompareOp::Lt ? ibQueryFilterOp::Less
					: cmp == ibQueryCompareOp::Le ? ibQueryFilterOp::LessEqual
					: cmp == ibQueryCompareOp::Gt ? ibQueryFilterOp::Greater
					                              : ibQueryFilterOp::GreaterEqual;
				const ibBackendQueryColumn* col = ResolveColumnSingle(sources, colE);
				b.CrossJoin(qi, kind, alias);
				b.Where(col, fop, EvalValue(valE, params));
			}
			else if (IsComputedExprAst(*j.m_on->m_lhs) || IsComputedExprAst(*j.m_on->m_rhs)) {
				// Computed ON (a.x+1 <op> b.y) — both sides become column exprs, evaluated per pair in the RAM
				// theta loop (lhs over left, rhs over right). No dot-walk inside the expression.
				b.Join(qi, BuildColumnExprFromAst(sources, *j.m_on->m_lhs, params),
				           BuildColumnExprFromAst(sources, *j.m_on->m_rhs, params),
				           MapJoinOp(j.m_on->m_cmp), kind, alias);
			}
			else {
				const ibBackendQueryColumn* lc = ResolveColumnSingle(sources, *j.m_on->m_lhs);
				const ibBackendQueryColumn* rc = ResolveColumnSingle(sources, *j.m_on->m_rhs);
				b.Join(qi, lc, rc, MapJoinOp(j.m_on->m_cmp), kind, alias);   // = -> hash; <,<=,>,>=,<> -> theta (server-side when co-located)
			}
		}
		else {
			b.Join(qi, kind, alias);   // auto-join by reference
		}
	}
}

//////////////////////////////////////////////////////////////////////
// NAME CHECK — resolve, run nothing (ibQueryLowering::CheckNames)
//////////////////////////////////////////////////////////////////////

// Every COLUMN expression under a node, in reading order. The check walks these and asks the same
// resolver the execution asks; it does NOT descend into a nested SELECT, which is checked on its
// own terms (with its OWN sources — a column inside a subquery belongs to that subquery's tables).
void CollectColumns(const ibQueryAstExprPtr& e, std::vector<const ibQueryAstExpr*>& out)
{
	if (!e)
		return;
	// A COLUMN ROOTED ON A CAST is collected WHOLE — ResolvePath knows that shape and resolves both
	// halves (the narrowed field here, the trailing walk on the target). Descending into it would
	// hand the checker a path with no source to start on.
	if (e->m_kind == ibQueryAstExprKind::Column) { out.push_back(e.get()); return; }

	CollectColumns(e->m_lhs, out);
	CollectColumns(e->m_rhs, out);
	CollectColumns(e->m_arg, out);
	CollectColumns(e->m_low, out);
	CollectColumns(e->m_high, out);
	CollectColumns(e->m_else, out);
	for (const ibQueryAstExprPtr& item : e->m_list)
		CollectColumns(item, out);
	for (const auto& branch : e->m_cases) {
		CollectColumns(branch.first, out);
		CollectColumns(branch.second, out);
	}
}

void CheckSelectNames(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params);

// The sources of one select, resolved for CHECKING. Anything that cannot be resolved without
// RUNNING — a package's temp table, a nested query — makes this select unverifiable and the check
// falls silent on it rather than inventing an error. The nested query is still checked itself.
bool BuildCheckSources(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                       std::vector<ibSourceBinding>& out)
{
	std::vector<const ibQuerySource*> sources;
	sources.push_back(&ast.m_from);
	for (const ibQueryAstJoin& join : ast.m_joins)
		sources.push_back(&join.m_source);

	for (const ibQuerySource* source : sources) {
		if (source == nullptr)
			return false;

		if (source->m_subquery) {
			CheckSelectNames(*source->m_subquery, params);   // on its own terms
			return false;                                    // its columns are not ours to verify
		}
		if (source->m_name.empty())
			return false;   // a query with no table yet — being written, not wrong

		// ResolveSource raises for a name nothing answers to (a temp table outside its package is
		// exactly that case), and being unable to resolve is NOT the same as the query being wrong.
		const ibBackendQueryable* queryable = nullptr;
		try { queryable = ResolveSource(*source, params); }
		catch (const ibBackendException&) { return false; }
		if (queryable == nullptr)
			return false;

		ibSourceBinding binding;
		binding.m_alias = source->m_alias;
		binding.m_q     = queryable;
		out.push_back(binding);
	}
	return !out.empty();
}

// WHICH PROJECTIONS ARE NEITHER A GROUP KEY NOR INSIDE AN AGGREGATE — the rule itself, over sources
// that are already resolved. Its two readings (a refusal, and the work still to do) are the public
// entries; this is what both of them ask. See ibQueryLowering::UngroupedProjections.
// ⭐ ONE WALK, AND IT ANSWERS BOTH HALVES: which columns a projection FOLDS, and which it reads
// FREE. A column under an aggregate is accounted for; a column outside every aggregate has to be a
// group key. That is the whole grouping rule, and it is one question about one tree.
//
// ⚠ IT IS NOT A QUESTION ABOUT THE TOP OF THE PROJECTION. Both halves used to look only at what a
// projection IS — a bare Column, or a Func — which is right for `Qty` and for `SUM(Qty)` and wrong
// for everything in between. `SUM(Price * Qty) / COUNT(*) * 1.2` is neither: it belongs in no list
// the constructor shows, and both collectors skipped it whole. So the columns inside it were held to
// no rule at all — a free `Price` beside a folded `Qty` in one expression, and the check silent on
// both. An expression is a TREE; the rule reads it as one.
void CollectFoldedAndFree(const ibQueryAstExprPtr& e, bool insideAggregate,
                          std::vector<const ibQueryAstExpr*>& folded,
                          std::vector<const ibQueryAstExpr*>& free)
{
	if (!e)
		return;
	if (e->m_kind == ibQueryAstExprKind::Column) {
		(insideAggregate ? folded : free).push_back(e.get());
		return;
	}
	// AN AGGREGATE FOLDS EVERYTHING BENEATH IT, however deep. Nested calls change nothing — once
	// inside, inside stays.
	const bool fold = insideAggregate
		|| (e->m_kind == ibQueryAstExprKind::Func && ibIsAggregateKeyword(e->m_func));

	for (const ibQueryAstExprPtr& child : { e->m_lhs, e->m_rhs, e->m_arg, e->m_low, e->m_high, e->m_else })
		CollectFoldedAndFree(child, fold, folded, free);
	for (const ibQueryAstExprPtr& item : e->m_list)
		CollectFoldedAndFree(item, fold, folded, free);
	for (const auto& branch : e->m_cases) {
		CollectFoldedAndFree(branch.first, fold, folded, free);
		CollectFoldedAndFree(branch.second, fold, folded, free);
	}
}

// THE OTHER HALF OF THE SAME RULE: the columns that live INSIDE an aggregate.
//
// A column being folded is already accounted for — it is not "missing from GROUP BY", and it must
// not be ADDED to it either. `SELECT SUM(Parent) ... GROUP BY Parent` folds each group over a single
// row, so the sum is the value itself: legal to write, and never what anybody meant. The window let
// exactly that happen — the same field standing in the grouping list and in the aggregates list at
// once — because nothing anywhere said the two lists are disjoint.
//
// One collector, two readings, like CollectUngrouped beside it: the check reads it as a refusal, the
// constructor reads it as "do not offer this one".
// A COLUMN NODE STANDING ON ITS OWN — the caller gets a path it can resolve and report without
// holding the projection alive.
ibQueryAstExprPtr ColumnCopy(const ibQueryAstExpr& column)
{
	ibQueryAstExprPtr copy = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
	copy->m_path = column.m_path;
	copy->m_line = column.m_line;
	copy->m_col  = column.m_col;
	return copy;
}

std::vector<ibQueryAstExprPtr> CollectAggregated(const ibQuerySelect& ast)
{
	std::vector<ibQueryAstExprPtr> out;
	for (const ibQueryProjection& projection : ast.m_projections) {
		std::vector<const ibQueryAstExpr*> folded, free;
		CollectFoldedAndFree(projection.m_expr, false, folded, free);
		for (const ibQueryAstExpr* column : folded)
			if (column != nullptr && !column->m_path.empty())
				out.push_back(ColumnCopy(*column));
	}
	return out;
}

std::vector<ibQueryAstExprPtr> CollectUngrouped(const ibQuerySelect& ast,
                                                const std::vector<ibSourceBinding>& sources)
{
	std::vector<ibQueryAstExprPtr> out;

	// DOES IT GROUP AT ALL? A GROUP BY says so outright; so does an aggregate ANYWHERE in the
	// projection list, because one aggregate makes the whole SELECT a fold. Anywhere, not at the
	// top: `SUM(Qty) / COUNT(*)` is as much a fold as `SUM(Qty)` is.
	// ⚠ WALKED ONCE. Each projection's tree is read a single time and both answers kept — the sweep
	// for "does it group at all" used to walk every projection, and the loop below walked them all
	// again. Two identical passes over the same trees, on a check that runs on every keystroke.
	//
	// THE COLUMNS A PROJECTION READS FREE are the ones outside every aggregate. For `Qty` that is Qty
	// itself; for `SUM(Qty)` there are none; for `SUM(Qty) / Price` it is Price alone — exactly the
	// field that has to be a group key, and the one the old shape never looked at.
	bool grouping = !ast.m_groupBy.empty();
	std::vector<std::vector<const ibQueryAstExpr*>> freePerProjection;
	freePerProjection.reserve(ast.m_projections.size());
	for (const ibQueryProjection& projection : ast.m_projections) {
		std::vector<const ibQueryAstExpr*> folded, free;
		CollectFoldedAndFree(projection.m_expr, false, folded, free);
		if (!folded.empty())
			grouping = true;
		freePerProjection.push_back(std::move(free));
	}
	if (!grouping || ast.m_selectAll)
		return out;   // not a grouping query, or one whose columns are not written out to judge

	// The group keys resolve ONCE, not once per projected column.
	std::vector<std::vector<const ibBackendQueryColumn*>> keyCols;
	keyCols.reserve(ast.m_groupBy.size());
	for (const ibQueryAstExprPtr& key : ast.m_groupBy)
		keyCols.push_back(key && key->m_kind == ibQueryAstExprKind::Column && !key->m_path.empty()
			? ResolvePath(sources, *key) : std::vector<const ibBackendQueryColumn*>());

	for (const std::vector<const ibQueryAstExpr*>& free : freePerProjection) {
		for (const ibQueryAstExpr* column : free) {
			if (column == nullptr || column->m_path.empty())
				continue;
			const std::vector<const ibBackendQueryColumn*> projCols = ResolvePath(sources, *column);
			bool grouped = false;
			// SAME COLUMN, whichever way it is written: `Products.Code` and `Code` are one column
			// when one table owns it, so the comparison is on the resolved LEAF, not on the text.
			for (const std::vector<const ibBackendQueryColumn*>& key : keyCols)
				if (!key.empty() && key == projCols) { grouped = true; break; }
			if (!grouped)
				out.push_back(ColumnCopy(*column));
		}
	}
	return out;
}

void CheckSelectNames(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params)
{
	// A nested SELECT anywhere in an expression is checked with ITS own sources.
	auto checkNested = [&params](const ibQueryAstExprPtr& e) {
		std::vector<const ibQueryAstExpr*> unusedColumns;
		if (!e) return;
		if (e->m_subquery) CheckSelectNames(*e->m_subquery, params);
	};

	for (const ibQuerySelectPtr& branch : ast.m_unions)
		if (branch) CheckSelectNames(*branch, params);

	std::vector<ibSourceBinding> sources;
	if (!BuildCheckSources(ast, params, sources))
		return;   // unverifiable — silence, never a false error

	// TWO LISTS, because one clause may legitimately name something the tables do not have.
	//
	// ⚠ THE EXEMPTION BELONGS TO `ORDER BY` ALONE. `ORDER BY Total` over `SUM(x) AS Total` names an
	// OUTPUT, and the executing path resolves it against the projection list. Applying that leniency
	// to every column was a hole big enough to drive the whole check through: a projection column IS
	// its own output name, so `SELECT ReferenceReference` declared itself known and walked straight
	// past the resolver. The exemption has to be as narrow as the rule that earns it.
	std::vector<const ibQueryAstExpr*> strict;    // must name a real column of a real source
	std::vector<const ibQueryAstExpr*> ordering;  // may instead name an output of this select

	for (const ibQueryProjection& projection : ast.m_projections) {
		CollectColumns(projection.m_expr, strict);
		checkNested(projection.m_expr);
	}
	CollectColumns(ast.m_where, strict);
	checkNested(ast.m_where);
	CollectColumns(ast.m_having, strict);
	for (const ibQueryAstExprPtr& key : ast.m_groupBy)
		CollectColumns(key, strict);
	for (const ibQueryAstExprPtr& aggregate : ast.m_totalsAggregates)
		CollectColumns(aggregate, strict);
	for (const ibQueryTotalDim& dim : ast.m_totalsBy)
		CollectColumns(dim.m_expr, strict);
	for (const ibQueryAstJoin& join : ast.m_joins)
		CollectColumns(join.m_on, strict);
	for (const ibQueryOrderItem& item : ast.m_orderBy)
		CollectColumns(item.m_expr, ordering);

	// THE SAME RESOLVER THE EXECUTION USES. Its words are the ones the user sees — there is no
	// second definition here of what a known field is.
	for (const ibQueryAstExpr* column : strict) {
		if (column == nullptr || column->m_path.empty())
			continue;
		ResolvePath(sources, *column);   // raises "unknown attribute" / "ambiguous attribute"
	}

	// ⚠ ASKED, NOT RESPELLED. This is the gate that refuses duplicate output names, so the name it
	// compares MUST be the name everything else uses — a second spelling here would refuse queries
	// the constructor generates, or pass ones it should refuse, and only when the two drifted.
	std::vector<wxString> outputs;
	for (const ibQueryProjection& projection : ast.m_projections) {
		const wxString name = ibQueryOutputName(projection);
		if (!name.IsEmpty())
			outputs.push_back(name);
	}

	// ⚠ TWO OUTPUT FIELDS CANNOT SHARE A NAME, and in THIS engine that is not a style rule. The
	// output is read back BY NAME: a union lines its branches up by it, a temp table's columns are
	// these names and its index is built over them, and a totals level answers to one. Two columns
	// called the same thing make every one of those ambiguous, and the ambiguity is silent — the
	// query runs and one of them wins.
	//
	// Checked HERE, in the engine, so the rule holds for hand-written text as much as for what the
	// constructor produces — and so the constructor does not have to own a rule of its own.
	for (size_t i = 0; i < outputs.size(); ++i)
		for (size_t k = i + 1; k < outputs.size(); ++k)
			if (!outputs[i].IsEmpty() && outputs[i].IsSameAs(outputs[k], false))
				Fail(0, 0, wxString::Format(
					_("two output fields are called '%s': give one of them a different alias"), outputs[i]));

	for (const ibQueryAstExpr* column : ordering) {
		if (column == nullptr || column->m_path.empty())
			continue;
		bool namesAnOutput = false;
		if (column->m_path.size() == 1)
			for (const wxString& name : outputs)
				if (name.IsSameAs(column->m_path[0], false)) { namesAnOutput = true; break; }
		if (!namesAnOutput)
			ResolvePath(sources, *column);
	}

	// ⚠ AN AGGREGATE OVER A TYPE IT CANNOT FOLD IS REFUSED — `SUM(Description)`, `AVG(Supplier)`.
	//
	// Asked here rather than at execution because the answer never depends on the DATA: it follows
	// from the column's declared TYPE, which is known the moment the query is written. Left to the
	// run, it surfaces as whatever the driver makes of adding strings together — a dialect-specific
	// error at best, zeroes at worst, and either way in front of a user rather than an author.
	//
	// Only what the fold genuinely means is allowed:
	//   SUM / AVG  — NUMBER alone. There is no sum of dates and no average of references.
	//   MIN / MAX  — anything ORDERED: number, date, string. A reference has no order of its own
	//                (it keys by guid), so folding one would rank rows by an internal identity.
	//   COUNT      — anything at all. Counting asks nothing of the type.
	//
	// A COMPOSITE column (several clsids) is not refused: which type a row holds is a fact about the
	// row, and refusing on "it might be a string" would be this check inventing an answer. That is
	// the same promise the rest of the check makes — silence where it cannot verify.
	for (const ibQueryProjection& projection : ast.m_projections) {
		const ibQueryAstExprPtr& expr = projection.m_expr;
		if (!expr || expr->m_kind != ibQueryAstExprKind::Func || expr->m_star || !expr->m_arg)
			continue;
		if (expr->m_func == ibQueryKeyword::Count)
			continue;   // counting asks nothing of the type
		if (expr->m_arg->m_kind != ibQueryAstExprKind::Column || expr->m_arg->m_path.empty())
			continue;   // a computed argument is checked by what it is made of, not here

		const std::vector<const ibBackendQueryColumn*> argCols = ResolvePath(sources, *expr->m_arg);
		if (argCols.empty() || argCols.back() == nullptr)
			continue;
		// ⚠ THE SAME LIST THE CONSTRUCTOR OFFERS. Read here as a refusal, read there as what to put
		// in the dropdown — one answer, so a person is never shown a choice their engine rejects.
		const std::vector<ibQueryKeyword> allowed =
			ibQueryLowering::AggregatesFor(argCols.back()->GetTypeDesc());
		if (std::find(allowed.begin(), allowed.end(), expr->m_func) == allowed.end()) {
			const bool sumLike = expr->m_func == ibQueryKeyword::Sum || expr->m_func == ibQueryKeyword::Avg;
			Fail(expr->m_line, expr->m_col, wxString::Format(
				_("%s cannot be taken over '%s': the field is not %s"),
				ibQueryKeywordText(expr->m_func), expr->m_arg->m_path.back(),
				sumLike ? _("a number") : _("an ordered value")));
		}
	}

	// ⚠⚠ A FOLDED COLUMN IS NOT ALSO A GROUP KEY. Grouping by the very column an aggregate folds
	// leaves one row per group, so the fold returns the value itself — writable, and never meant.
	// It is refused here rather than left to arrive as a puzzling result set.
	// ⚠ EACH SIDE RESOLVED ONCE. Resolving is a dot-walk through the sources, not a comparison, so
	// doing it inside the pairing loop cost (folded × keys) walks of the same two paths — and this
	// runs on every check, which is every keystroke in the constructor.
	{
		const std::vector<ibQueryAstExprPtr> foldedList = CollectAggregated(ast);
		std::vector<std::vector<const ibBackendQueryColumn*>> keyCols;
		keyCols.reserve(ast.m_groupBy.size());
		for (const ibQueryAstExprPtr& key : ast.m_groupBy)
			keyCols.push_back(key && key->m_kind == ibQueryAstExprKind::Column && !key->m_path.empty()
				? ResolvePath(sources, *key) : std::vector<const ibBackendQueryColumn*>());

		std::vector<std::vector<const ibBackendQueryColumn*>> plainCols;
		for (const ibQueryProjection& projection : ast.m_projections)
			if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column
			    && !projection.m_expr->m_path.empty())
				plainCols.push_back(ResolvePath(sources, *projection.m_expr));

		for (const ibQueryAstExprPtr& folded : foldedList) {
			const std::vector<const ibBackendQueryColumn*> foldedCols = ResolvePath(sources, *folded);

			// ⚠ UNLESS THE QUERY ALSO SELECTS IT PLAINLY. `SELECT Qty, SUM(Qty) … GROUP BY Qty` reads
			// oddly but means something, and the completeness rule right below DEMANDS that key — so
			// refusing it here would be the engine contradicting itself, one paragraph apart. What is
			// refused is the narrow case: a column folded and NOWHERE selected on its own, standing in
			// GROUP BY for no reason but a window that put it there.
			if (std::find(plainCols.begin(), plainCols.end(), foldedCols) != plainCols.end())
				continue;

			for (size_t i = 0; i < ast.m_groupBy.size(); ++i) {
				// SAME COLUMN, whichever way it is written — the resolved leaf, as everywhere else.
				if (keyCols[i].empty() || keyCols[i] != foldedCols)
					continue;
				Fail(ast.m_groupBy[i]->m_line, ast.m_groupBy[i]->m_col, wxString::Format(
					_("'%s' is already aggregated, so it cannot also be a grouping field"),
					ast.m_groupBy[i]->m_path.back()));
			}
		}
	}

	// ⚠⚠ AN INCOMPLETE GROUPING IS REFUSED — asked of the SAME door a host asks to complete one, so
	// what the check calls wrong and what the constructor fixes are one answer (see the header).
	const std::vector<ibQueryAstExprPtr> ungrouped = CollectUngrouped(ast, sources);
	if (!ungrouped.empty()) {
		const ibQueryAstExprPtr& first = ungrouped.front();
		Fail(first->m_line, first->m_col, wxString::Format(
			_("'%s' is neither grouped nor aggregated: add it to GROUP BY, or wrap it in an aggregate"),
			first->m_path.back()));
	}
}


// Does this expression still resolve against these sources? Asked of the SAME resolver the
// execution uses, so "still there" means what it means everywhere else.
bool StillResolves(const std::vector<ibSourceBinding>& sources, const ibQueryAstExprPtr& e)
{
	std::vector<const ibQueryAstExpr*> columns;
	CollectColumns(e, columns);
	for (const ibQueryAstExpr* column : columns) {
		if (column == nullptr || column->m_path.empty())
			continue;
		try { ResolvePath(sources, *column); }
		catch (const ibBackendException&) { return false; }
	}
	return true;
}

int PruneSelect(ibQuerySelect& ast, const std::map<wxString, ibValue>& params)
{
	int dropped = 0;
	for (const ibQuerySelectPtr& branch : ast.m_unions)
		if (branch) dropped += PruneSelect(*branch, params);

	std::vector<ibSourceBinding> sources;
	if (!BuildCheckSources(ast, params, sources))
		return dropped;   // unverifiable — leave it whole

	auto gone = [&](bool resolves) { if (!resolves) ++dropped; return !resolves; };

	ast.m_projections.erase(std::remove_if(ast.m_projections.begin(), ast.m_projections.end(),
		[&](const ibQueryProjection& p) { return gone(StillResolves(sources, p.m_expr)); }),
		ast.m_projections.end());

	ast.m_groupBy.erase(std::remove_if(ast.m_groupBy.begin(), ast.m_groupBy.end(),
		[&](const ibQueryAstExprPtr& e) { return gone(StillResolves(sources, e)); }), ast.m_groupBy.end());

	ast.m_orderBy.erase(std::remove_if(ast.m_orderBy.begin(), ast.m_orderBy.end(),
		[&](const ibQueryOrderItem& o) { return gone(StillResolves(sources, o.m_expr)); }), ast.m_orderBy.end());

	ast.m_indexBy.erase(std::remove_if(ast.m_indexBy.begin(), ast.m_indexBy.end(),
		[&](const ibQueryAstExprPtr& e) { return gone(StillResolves(sources, e)); }), ast.m_indexBy.end());

	ast.m_totalsAggregates.erase(std::remove_if(ast.m_totalsAggregates.begin(), ast.m_totalsAggregates.end(),
		[&](const ibQueryAstExprPtr& e) { return gone(StillResolves(sources, e)); }), ast.m_totalsAggregates.end());

	ast.m_totalsBy.erase(std::remove_if(ast.m_totalsBy.begin(), ast.m_totalsBy.end(),
		[&](const ibQueryTotalDim& d) { return gone(StillResolves(sources, d.m_expr)); }), ast.m_totalsBy.end());

	if (ast.m_totalsAggregates.empty() && ast.m_totalsBy.empty() && !ast.m_totalsOverall)
		ast.m_hasTotals = false;   // TOTALS with nothing in it is not a TOTALS

	// A JOIN's condition goes, not the join: the join IS a source, and an empty ON means "join by
	// the reference between the tables" — a definition, not a hole.
	for (ibQueryAstJoin& join : ast.m_joins)
		if (join.m_on && !StillResolves(sources, join.m_on)) { join.m_on = nullptr; ++dropped; }

	// THE WHERE IS A CHAIN, so only the links that broke go — not the whole filter.
	if (ast.m_where) {
		std::vector<ibQueryAstExprPtr> rows;
		ibQueryFlattenAnd(ast.m_where, rows);
		const size_t before = rows.size();
		rows.erase(std::remove_if(rows.begin(), rows.end(),
			[&](const ibQueryAstExprPtr& e) { return !StillResolves(sources, e); }), rows.end());
		dropped += static_cast<int>(before - rows.size());
		ast.m_where = ibQueryFoldAnd(rows);
	}

	if (ast.m_projections.empty())
		ast.m_selectAll = true;   // a query with no named fields reads every one of them

	return dropped;
}

} // namespace

int ibQueryLowering::PruneUnresolved(ibQueryPackage& package, const std::map<wxString, ibValue>& params)
{
	int dropped = 0;
	for (ibQueryAstStatement& statement : package.m_statements)
		if (statement.m_select)
			dropped += PruneSelect(*statement.m_select, params);
	return dropped;
}

void ibQueryLowering::CheckNames(const ibQueryPackage& package, const std::map<wxString, ibValue>& params)
{
	for (const ibQueryAstStatement& statement : package.m_statements)
		if (statement.m_select)
			CheckSelectNames(*statement.m_select, params);
}

std::vector<ibQueryKeyword> ibQueryLowering::AggregatesFor(const ibTypeDescription& type)
{
	// COUNT is always in — it asks nothing of the type, and a list with nothing in it would be a
	// cell a person cannot fill.
	std::vector<ibQueryKeyword> out{ ibQueryKeyword::Count };

	// UNKNOWN OR COMPOSITE: offer everything. Which type a row holds is the row's business, and
	// narrowing here on a guess would take away a choice the query can perfectly well make.
	if (type.GetClsidCount() != 1) {
		out.push_back(ibQueryKeyword::Sum);
		out.push_back(ibQueryKeyword::Min);
		out.push_back(ibQueryKeyword::Max);
		out.push_back(ibQueryKeyword::Avg);
		return out;
	}

	const bool numeric = type.ContainType(ibValueTypes::TYPE_NUMBER);
	const bool ordered = numeric || type.ContainType(ibValueTypes::TYPE_DATE)
	                             || type.ContainType(ibValueTypes::TYPE_STRING);

	if (numeric) out.push_back(ibQueryKeyword::Sum);
	if (ordered) { out.push_back(ibQueryKeyword::Min); out.push_back(ibQueryKeyword::Max); }
	if (numeric) out.push_back(ibQueryKeyword::Avg);
	return out;
}

std::vector<ibQueryAstExprPtr> ibQueryLowering::AggregatedColumns(const ibQuerySelect& ast)
{
	return CollectAggregated(ast);
}

std::vector<ibQueryAstExprPtr> ibQueryLowering::UngroupedProjections(
	const ibQuerySelect& ast, const std::map<wxString, ibValue>& params)
{
	// UNVERIFIABLE IS EMPTY, not a guess — the same promise CheckNames and PruneUnresolved make. A
	// host reading this as "the work still to do" would otherwise add group keys to a query nobody
	// could resolve.
	std::vector<ibSourceBinding> sources;
	if (!BuildCheckSources(ast, params, sources))
		return {};

	try {
		return CollectUngrouped(ast, sources);
	}
	catch (const ibBackendException&) {
		// A name that does not resolve is CheckNames' verdict to give, with its own words. Here it
		// only means "nothing can be said about grouping yet".
		return {};
	}
}

//////////////////////////////////////////////////////////////////////
// L4-2 — recorded-lambda lowering (the Queryable fold reuses the same
// file-local builders the text language lowers through). Bail = empty,
// never a thrown user error: untranslatable folds fall back to RAM.
//////////////////////////////////////////////////////////////////////

ibQueryPredicatePtr ibQueryLowering::LowerLambdaPredicate(const ibBackendQueryable* source,
                                                          const ibQueryAstExpr& expr,
                                                          const std::map<wxString, ibValue>& captured)
{
	if (source == nullptr)
		return nullptr;
	const std::vector<ibSourceBinding> sources{ { wxEmptyString, source } };
	try {
		// Dot-walk leaves ride only on a physical single source (same gate as text).
		return BuildWherePredicate(sources, expr, captured, /*allowDotWalk*/ !source->IsComputedInRam());
	}
	catch (...) {
		return nullptr;   // resolution / subset failure -> the fold bails to RAM
	}
}

std::vector<const ibBackendQueryColumn*> ibQueryLowering::LowerLambdaColumnPath(
	const ibBackendQueryable* source, const ibQueryAstExpr& expr)
{
	if (source == nullptr || expr.m_kind != ibQueryAstExprKind::Column)
		return {};
	const std::vector<ibSourceBinding> sources{ { wxEmptyString, source } };
	try {
		std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, expr);
		if (cols.size() > 1 && source->IsComputedInRam())
			return {};   // dot-walk needs a physical source
		return cols;
	}
	catch (...) {
		return {};
	}
}

ibQueryColumnExprPtr ibQueryLowering::LowerLambdaColumnExpr(
	const ibBackendQueryable* source, const ibQueryAstExpr& expr,
	const std::map<wxString, ibValue>& captured)
{
	if (source == nullptr || !IsComputedExprAst(expr))
		return nullptr;   // only arithmetic / CASE; a plain column / dot-walk / structure is handled elsewhere
	const std::vector<ibSourceBinding> sources{ { wxEmptyString, source } };
	try {
		GateComputedExpr(sources, expr);   // single physical source only (no JOIN, no computed / RAM source)
		return BuildColumnExprFromAst(sources, expr, captured);
	}
	catch (...) {
		return nullptr;   // resolution failure -> caller bails to RAM
	}
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::Execute
//////////////////////////////////////////////////////////////////////

ibDataQueryResult ibQueryLowering::Execute(const ibQuerySelect& astIn,
                                           const std::map<wxString, ibValue>& params,
                                           std::vector<OutputColumn>& outSchema)
{
	return ExecuteImpl(astIn, params, outSchema, ibReadPageRequest{}, nullptr, wxEmptyString);
}

ibDataQueryResult ibQueryLowering::Execute(const ibQuerySelect& astIn,
                                           const std::map<wxString, ibValue>& params,
                                           std::vector<OutputColumn>& outSchema,
                                           const ibReadPageRequest& page)
{
	return ExecuteImpl(astIn, params, outSchema, page, nullptr, wxEmptyString);
}

ibDataQueryResult ibQueryLowering::Execute(const ibQuerySelect& astIn,
                                           const std::map<wxString, ibValue>& params,
                                           std::vector<OutputColumn>& outSchema,
                                           const ibReadPageRequest& page,
                                           ibRenderedPageCache& cache, const wxString& signature)
{
	return ExecuteImpl(astIn, params, outSchema, page, &cache, signature);
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::ExecutePackage — several statements as one trip
//////////////////////////////////////////////////////////////////////

namespace {

// Drain a finished selection into a snapshot, one snapshot column per OUTPUT column.
//
// The ids are minted here, sequentially, rather than borrowed from the source columns: a
// projection may name the same source column twice (under two aliases), and an id that is not
// unique would make the second one overwrite the first. The NAME is what later statements
// select by, so it is the name that must survive — not the provenance of the id.
ibQueryRamTable DrainIntoSnapshot(ibDataQueryResult& result,
                                  const std::vector<OutputColumn>& schema)
{
	ibQueryRamTable table;
	std::vector<ibMetaID> ids;
	ids.reserve(schema.size());

	for (size_t i = 0; i < schema.size(); ++i) {
		const ibMetaID id = static_cast<ibMetaID>(i + 1);
		ids.push_back(id);
		static const ibTypeDescription s_anyType;
		table.AddColumn(id, schema[i].m_name,
			schema[i].m_col != nullptr ? schema[i].m_col->GetTypeDesc() : s_anyType);
	}

	while (result.Next()) {
		const long row = table.AppendRow();
		for (size_t i = 0; i < schema.size(); ++i) {
			const OutputColumn& oc = schema[i];
			ibValue v;
			if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
				v = result.GetColumnObject(oc.m_objectPrefix, oc.m_col);
			else if (oc.m_byAlias)
				v = result.GetColumn(oc.m_alias);
			else
				v = result.GetValue(oc.m_col);
			table.SetCell(row, ids[i], v);
		}
	}
	return table;
}

} // namespace

std::vector<ibQueryLowering::PackageResult> ibQueryLowering::ExecutePackage(
	const ibQueryPackage& package, const std::map<wxString, ibValue>& params,
	ibQueryTempTableStore* store)
{
	std::vector<PackageResult> results;

	// WHO KEEPS THE TABLES ALIVE. Without a store the package owns one for its own run: a single
	// query's temp scope is RAII-bound to ONE execution, so statement 3 would never see what
	// statement 2 left. With a store handed in (a script's TempTablesManager), the tables outlive
	// this call and several separate queries share them — the same mechanism, a different holder.
	//
	// The scope holds a POINTER to the map either way, so entries added mid-package are visible to
	// every statement after them, which IS the batch contract.
	ibQueryTempTableStore ownStore;
	ibQueryTempTableStore& temps = store != nullptr ? *store : ownStore;
	ibTempSourceScope packageScope(temps.Sources());

	for (const ibQueryAstStatement& statement : package.m_statements) {

		if (statement.IsDrop()) {
			// Releasing EARLY — said out loud instead of left to scope. Dropping a name that was
			// never made is an error, not a shrug: it is either a typo or a statement that was
			// expected to run and did not.
			if (!temps.Drop(statement.m_dropTemp))
				Fail(0, 0, wxString::Format(_("temporary table '%s' does not exist"), statement.m_dropTemp));
			PackageResult r;
			r.m_dropTemp = statement.m_dropTemp;
			results.push_back(std::move(r));
			continue;
		}

		if (!statement.m_select)
			continue;

		const ibQuerySelect& ast = *statement.m_select;

		PackageResult r;
		r.m_hasTotals = ast.m_hasTotals;
		std::vector<OutputColumn> schema;
		ibDataQueryResult read = ast.m_hasTotals
			? ExecuteTotals(ast, params, schema)
			: Execute(ast, params, schema);

		if (ast.m_intoTemp.IsEmpty()) {
			r.m_result = std::make_unique<ibDataQueryResult>(std::move(read));
			r.m_schema = std::move(schema);
			results.push_back(std::move(r));
			continue;
		}

		// INTO — the rows go into the store under that name, and what comes back is their COUNT.
		// A name declared twice is a mistake worth naming: the second statement would otherwise
		// silently shadow the first and the reader could not tell which one was read afterwards.
		// (With a shared store the clash can also be against a table an EARLIER query left, which
		// is the same mistake seen from further away.)
		if (temps.Has(ast.m_intoTemp))
			Fail(0, 0, wxString::Format(_("temporary table '%s' already exists"), ast.m_intoTemp));

		ibQueryRamTable snapshot = DrainIntoSnapshot(read, schema);
		r.m_rowCount = snapshot.RowCount();
		r.m_intoTemp = ast.m_intoTemp;

		// INDEX BY — the columns the store builds a lookup over, named as the OUTPUT names, because
		// that is what the later statements select by. A name the projection did not produce simply
		// is not there to index.
		std::vector<wxString> indexed;
		for (const ibQueryAstExprPtr& column : ast.m_indexBy)
			if (column && !column->m_path.empty())
				indexed.push_back(column->m_path.back());

		temps.Put(ast.m_intoTemp, std::move(snapshot), indexed);

		// WHAT A CREATE-TEMP STATEMENT HANDS BACK: a RESULT of one column and one row, holding the
		// number of records placed in the table — not a bare number.
		//
		// The difference is the caller's. A package's results come back as an array addressed by
		// position, and if one element were a number while the rest are results, every consumer
		// would need a branch to walk it: `Results[i].Select()` has to work for every i. So the
		// count is a one-by-one result, and the array is uniform in KIND. (A drop yields the
		// UNDEFINED value for the same reason seen from the other side: it is a position with no
		// result, and saying so with an empty value is what "no result" IS.)
		{
			ibQueryRamTable counted;
			auto column = std::make_shared<ibSyntheticScalarColumn>(
				wxT("Count"), kSyntheticColumnBase, ibRawDBColumn::RawType::Number);
			counted.AddColumn(column->GetColumnId(), wxT("Count"), ibTypeDescription());
			const long row = counted.AppendRow();
			counted.SetCell(row, column->GetColumnId(), ibValue(static_cast<signed int>(r.m_rowCount)));

			OutputColumn oc;
			oc.m_name     = wxT("Count");
			oc.m_col      = column.get();
			oc.m_ownedCol = column;   // the schema travels with the selection — the column must outlive both

			r.m_schema.push_back(std::move(oc));
			r.m_result = std::make_unique<ibDataQueryResult>(std::move(counted), nullptr);
		}

		results.push_back(std::move(r));
	}

	return results;
}

// A QUERY WITH NO TABLE. `SELECT 1` returns one row holding 1 — the way a constant, a parameter or
// a computed expression is asked for without reading anything. The row is built here, from the
// projections themselves; the door is never opened, because there is no source to open it on.
//
// The columns are SYNTHETIC (the same ibSyntheticScalarColumn the totals measures use), so the
// result is read back exactly like any other — by name, through the normal schema.
ibDataQueryResult ibQueryLowering::ExecuteSourceless(const ibQuerySelect& ast,
                                                    const std::map<wxString, ibValue>& params,
                                                    std::vector<OutputColumn>& outSchema)
{
	if (ast.m_selectAll || ast.m_projections.empty())
		Fail(0, 0, _("SELECT * needs a table to read: name one after FROM"));

	ibQueryRamTable table;
	std::vector<ibValue> values;
	ibMetaID nextId = kSyntheticColumnBase;

	int index = 0;
	for (const ibQueryProjection& projection : ast.m_projections) {
		if (!projection.m_expr)
			continue;
		if (projection.m_expr->m_kind == ibQueryAstExprKind::Column)
			Fail(projection.m_expr->m_line, projection.m_expr->m_col, wxString::Format(
				_("'%s' is a field, and this query reads no table: name one after FROM"),
				projection.m_expr->m_path.empty() ? wxString() : projection.m_expr->m_path.back()));

		// EvalValue answers a literal, a parameter and the arithmetic / CASE built out of them —
		// which is exactly the set a query with no rows behind it can mean.
		const ibValue value = EvalValue(*projection.m_expr, params);
		const wxString name = OutputNameFor(ast, projection, index++);

		auto column = std::make_shared<ibSyntheticScalarColumn>(name, nextId++, ibRawDBColumn::RawType::String);
		table.AddColumn(column->GetColumnId(), name, ibTypeDescription());
		values.push_back(value);

		OutputColumn oc;
		oc.m_name     = name;
		oc.m_col      = column.get();
		oc.m_ownedCol = column;
		outSchema.push_back(std::move(oc));
	}

	const long row = table.AppendRow();
	for (size_t i = 0; i < outSchema.size() && i < values.size(); ++i)
		table.SetCell(row, outSchema[i].m_col->GetColumnId(), values[i]);

	return ibDataQueryResult(std::move(table), nullptr);
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::DescribeOutput — the schema WITHOUT the read
//////////////////////////////////////////////////////////////////////

void ibQueryLowering::DescribeOutput(const ibQuerySelect& astIn,
                                     const std::map<wxString, ibValue>& params,
                                     std::vector<OutputColumn>& outSchema)
{
	outSchema.clear();

	// TOTALS describes its DETAIL. A totals query answers with a TREE, and the levels of that tree are
	// made at the fold — but what a host asks this question for is "which fields does this query put
	// in front of me", and those are the detail columns: they are what a filter, a sort and a grouping
	// can name. So the clause is set aside and the select underneath is described.
	ibQuerySelectPtr detail;
	if (astIn.m_hasTotals) {
		detail = std::make_shared<ibQuerySelect>(astIn);
		detail->m_totalsBy.clear();
		detail->m_totalsAggregates.clear();
		detail->m_totalsOverall = false;
		detail->m_hasTotals = false;
	}

	const ibQuerySelectPtr astOpt = ibQueryRewrite::Rewrite(detail ? *detail : astIn);
	const ibQuerySelect& ast = *astOpt;

	// NO SOURCE — `SELECT 1`. The columns are synthetic and named by the projections alone, so the
	// answer is built here rather than through a door there is nothing to open.
	if (ast.m_from.m_name.empty() && !ast.m_from.m_subquery && ast.m_joins.empty()) {
		if (ast.m_selectAll || ast.m_projections.empty())
			Fail(0, 0, _("SELECT * needs a table to read: name one after FROM"));

		ibMetaID nextId = kSyntheticColumnBase;
		int index = 0;
		for (const ibQueryProjection& projection : ast.m_projections) {
			if (!projection.m_expr)
				continue;
			if (projection.m_expr->m_kind == ibQueryAstExprKind::Column)
				Fail(projection.m_expr->m_line, projection.m_expr->m_col, wxString::Format(
					_("'%s' is a field, and this query reads no table: name one after FROM"),
					projection.m_expr->m_path.empty() ? wxString() : projection.m_expr->m_path.back()));

			const wxString name = OutputNameFor(ast, projection, index++);
			auto column = std::make_shared<ibSyntheticScalarColumn>(name, nextId++, ibRawDBColumn::RawType::String);

			OutputColumn oc;
			oc.m_name     = name;
			oc.m_col      = column.get();
			oc.m_ownedCol = column;
			outSchema.push_back(std::move(oc));
		}
		return;
	}

	// The sources this description opened. They must outlive PopulateBuilder (which resolves columns
	// through them) and no longer — nothing is read, so they are released the moment we return.
	ibSubqueryOwner subOwners;

	std::vector<ibSourceBinding> sources;
	ibDataQueryBuilder b;
	BuildSourceTree(ast, params, subOwners, sources, b);

	// AND HERE IT STOPS. PopulateBuilder is where names become columns and the output schema is
	// decided; the terminal below it (Execute / SelectAggregate) is where rows are read. Describing
	// is the first half without the second.
	PopulateBuilder(ast, params, sources, b, outSchema, /*asSubquery*/false);
}

ibDataQueryResult ibQueryLowering::ExecuteImpl(const ibQuerySelect& astIn,
                                               const std::map<wxString, ibValue>& params,
                                               std::vector<OutputColumn>& outSchema,
                                               const ibReadPageRequest& pageIn,
                                               ibRenderedPageCache* cache, const wxString& signature)
{
	// Optimizer pass — negation normalization + FROM-subquery flattening. Works on a
	// deep clone; the Query value object's cached parse is never mutated. (queryRewrite.h)
	const ibQuerySelectPtr astOpt = ibQueryRewrite::Rewrite(astIn);
	const ibQuerySelect& ast = *astOpt;

	if (ast.m_hasTotals)
		Fail(0, 0, _("hierarchical TOTALS execution goes through ExecuteTotals, not Execute"));

	// NO SOURCE — `SELECT 1`, `SELECT &Param`, `SELECT CASE WHEN … END`. A legitimate query that
	// touches no table: it yields ONE row carrying the projected values. There is no door to open
	// and nothing to read, so it is answered right here, from a one-row RAM table.
	//
	// A COLUMN in such a query is refused, and the message says why rather than "unknown attribute":
	// the name is not unknown, there is simply nowhere to read it FROM.
	if (ast.m_from.m_name.empty() && !ast.m_from.m_subquery && ast.m_joins.empty()
	    && ast.m_unions.empty())
		return ExecuteSourceless(ast, params, outSchema);

	// Sources built for this run (subqueries / UNION branches) — must live until the door's terminal call.
	ibSubqueryOwner subOwners;

	// UNION — stack the branches vertically (the composer realizes it). The branch queryables live in
	// subOwners through the materialising terminal inside LowerUnion.
	if (!ast.m_unions.empty())
		return LowerUnion(ast, params, outSchema, subOwners);

	std::vector<ibSourceBinding> sources;
	ibDataQueryBuilder b;
	BuildSourceTree(ast, params, subOwners, sources, b);

	// SELECT ALLOWED — carried to the door, which owns what a refusal turns into.
	b.Allowed(ast.m_allowed);

	const bool aggregate = PopulateBuilder(ast, params, sources, b, outSchema, /*asSubquery*/false);

	if (aggregate) {
		// SELECT TOP n + GROUP BY — the door's aggregate-terminal row limit: the DB / co-located
		// paths render the dialect LIMIT, the RAM fold truncates after grouping.
		if (ast.m_top > 0)
			b.Top(ast.m_top);
		return b.SelectAggregate();
	}

	// The external envelope drives the cursor; a `TOP n` in the text still caps the
	// page — the smaller positive count wins (0 = unbounded on either side). With a
	// caller-owned page cache the door reuses the rendered SQL, rebinding the anchor.
	ibReadPageRequest page = pageIn;
	if (ast.m_top > 0 && (page.m_count <= 0 || ast.m_top < page.m_count))
		page.m_count = ast.m_top;
	// FOR UPDATE rides the page request — the dialect appends its own row-lock clause
	// (FOR UPDATE / WITH LOCK) from there. Nothing new below L2: the driver half was built.
	if (ast.m_forUpdate)
		page.m_lockForUpdate = true;
	return cache != nullptr ? b.Execute(page, *cache, signature) : b.Execute(page);
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::ExecuteTotals — hierarchical subtotals (TOTALS … BY …)
//////////////////////////////////////////////////////////////////////

ibDataQueryResult ibQueryLowering::ExecuteTotals(const ibQuerySelect& astIn,
                                                 const std::map<wxString, ibValue>& params,
                                                 std::vector<OutputColumn>& outSchema,
                                                 const ibReadPageRequest& page,
                                                 bool* outServerGroupedLevel)
{
	// Same optimizer pass as Execute — the totals path benefits from a flattened FROM
	// and a normalized WHERE the same way. (queryRewrite.h)
	const ibQuerySelectPtr astOpt = ibQueryRewrite::Rewrite(astIn);
	const ibQuerySelect& ast = *astOpt;

	// OVERALL ON ITS OWN IS A WHOLE TOTALS QUERY — one row folding everything, no dimensions. So
	// what is refused is a TOTALS asking for no level at all, not a TOTALS with no dimension.
	if (ast.m_totalsBy.empty() && !ast.m_totalsOverall)
		Fail(0, 0, _("TOTALS needs at least one BY dimension, or BY OVERALL"));
	// TOP + TOTALS — the limit caps the DETAIL rows the fold runs over (the first n by ORDER BY), NOT the
	// subtotal tree. Applied as the page count on the detail read at the terminal below.

	ibSubqueryOwner owner;

	// FROM — single source, a JOIN chain, or a UNION stack. In every case the flat read
	// (b.Execute -> ExecuteRead) realizes the source (server-side or RAM-composed), the TotalBy config is
	// stamped on the result, and the runtime folds the ONE snapshot — no separate totals terminal. The
	// dimension / aggregate resolution below reads through `sources`. (docs/query-language-arc.md §22.1b)
	std::vector<ibSourceBinding> sources;
	ibDataQueryBuilder b;

	if (!ast.m_unions.empty()) {
		// UNION — stack the branches vertically (mirrors LowerUnion). The whole-union output = the FIRST
		// branch's columns (by name); dimensions / aggregates resolve against that branch, like the plain
		// union's trailing ORDER BY. The composer realizes the stack into one RAM snapshot the fold reads.
		ibQuerySelect core0 = ast;
		core0.m_orderBy.clear();
		core0.m_unions.clear();
		core0.m_totalsBy.clear();
		core0.m_totalsAggregates.clear();
		core0.m_totalsOverall = false;
		core0.m_hasTotals = false;
		core0.m_top = 0;

		const ibBackendQueryable* b0 = WrapSelectAsQueryable(core0, params, owner);
		b.From(b0);
		for (const ibBackendQueryColumn* c : b0->GetColumns())   // carry every union-output column into the snapshot
			if (c != nullptr) b.Select(c, c->GetName());
		for (const std::shared_ptr<ibQuerySelect>& u : ast.m_unions)
			b.Union(WrapSelectAsQueryable(*u, params, owner), wxEmptyString, /*keepDuplicates*/ u->m_unionAll);

		sources.push_back({ wxEmptyString, b0 });
	}
	else {
		// FROM + JOINs / single source -- shared with the non-totals read path (BuildSourceTree).
		BuildSourceTree(ast, params, owner, sources, b);
	}

	// SELECT ALLOWED reaches the totals read the same way — a report over a composite type is
	// exactly where the quiet form is the honest one.
	b.Allowed(ast.m_allowed);

	// ⭐ SINGLE-LEVEL GROUP KEYSET PAGE (docs: group-level paging). A drill fetches ONE grouping level; when it
	// is a single PLAIN scalar dim over a SINGLE source with NO measures (the nomenclature-hierarchy tree) AND
	// the fetch carries a real page, run the level's groups server-side -- GROUP BY dim ORDER BY dim [keyset]
	// LIMIT count (SelectAggregatePage -> CanPageGroupLevel) -- instead of reading EVERY detail row and folding
	// all groups in RAM. The caller emits the flat groups at level 1 (outServerGroupedLevel), skipping the fold.
	// Reports (measures), multi-level, dot-walk and multi-source keep the detail-read + fold below.
	const bool multiSourceTotals = !ast.m_joins.empty() || !ast.m_unions.empty();
	if (outServerGroupedLevel != nullptr && page.m_count > 0 && !multiSourceTotals
	    && ast.m_totalsBy.size() == 1 && ast.m_totalsAggregates.empty()
	    && ast.m_totalsBy[0].m_unfold == ibQueryDimUnfold::Elements) {   // flat grouping only (Hierarchy = recursive tree -> fold)
		const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, *ast.m_totalsBy[0].m_expr);
		if (pathCols.size() == 1) {   // plain scalar dim (no dot-walk expansion -> single-column keyset ORDER BY)
			const ibBackendQueryColumn* leaf = pathCols.back();
			b.GroupBy(leaf);
			OutputColumn oc; oc.m_name = leaf->GetName(); oc.m_col = leaf;
			outSchema.clear(); outSchema.push_back(oc);
			// WHERE = the drill SCOPE filter + the user filter (same lowering the fold path uses below).
			if (ast.m_where) {
				if (IsFlatAndWhere(*ast.m_where))
					LowerFlatWhere(b, sources, *ast.m_where, params, /*allowDotWalk*/false);
				else
					b.Where(BuildWherePredicate(sources, *ast.m_where, params, /*allowDotWalk*/false));
			}
			*outServerGroupedLevel = true;
			return b.SelectAggregatePage(page);   // server-side GROUP BY + keyset + LIMIT -> the page's groups
		}
	}

	outSchema.clear();
	ibMetaID nextSynthId = kSyntheticColumnBase;   // shared id pool for synthetic dimension + measure columns
	const bool multiSource = !ast.m_joins.empty() || !ast.m_unions.empty();
	std::map<wxString, const ibBackendQueryable*> dwJoined; int dwAliasSeq = 0;   // dot-walk join dedup (multi-source)

	// BY OVERALL — the level above them all. Nothing to resolve and nothing to group by: the fold's
	// root already holds the whole-result aggregates, so this only says to walk it as a row.
	b.TotalsOverall(ast.m_totalsOverall);

	// The dimension levels, IN ORDER (each yields a subtotal node; the root is the grand total). They
	// are the leading output columns (their group key at each node — read by GetValue(col)).
	for (const ibQueryTotalDim& d : ast.m_totalsBy) {
		const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, *d.m_expr);   // plain col OR dot-walk path
		const ibBackendQueryColumn* leaf = pathCols.back();
		const ibDimensionKind dim =
			d.m_unfold == ibQueryDimUnfold::Hierarchy       ? ibDimensionKind::Hierarchy
			: d.m_unfold == ibQueryDimUnfold::HierarchyOnly ? ibDimensionKind::HierarchyOnly
			                                                : ibDimensionKind::Elements;

		// THE LEVEL'S NAME. Its own when it was given one — that is what makes two levels over the
		// same column (Date by month, Date by day) two readable columns instead of one name answered
		// by whichever came last.
		OutputColumn oc; oc.m_name = d.m_alias.IsEmpty() ? leaf->GetName() : d.m_alias;
		if (pathCols.size() == 1) {
			b.TotalBy(leaf, dim);            // plain dimension — group by the column's own metaID
			oc.m_col = leaf;
		}
		else {
			// DOT-WALK dimension — two strategies by source shape / leaf kind:
			//  - single-source SCALAR leaf (Parent.Code): SQL ROLLUP via a synthetic scalar projection
			//    (TotalByDotWalk) — the DBMS folds, efficient. The synthetic's DISTINCT id avoids a
			//    self-reference metaID clash with the main table's same-named field.
			//  - multi-source OR a NON-scalar leaf (reference / composite): expand the ref path into explicit
			//    LEFT-join leaves (ExpandDotWalkJoins) and group by the leaf in the RAM fold (by the leaf's
			//    VALUE — scalar OR reference). A non-scalar single-source leaf rides this too: adding the
			//    ref-join makes it multi-source / RAM-folded, grouping by the reference value the scalar
			//    synthetic could not carry. (A composite MID-segment still fails inside the expand — that path
			//    is not a single-target reference; same edge as projection.)
			ibRawDBColumn::RawType rt;
			const bool scalarLeaf = ScalarRawType(leaf, rt);
			if (multiSource || !scalarLeaf) {
				const ibBackendQueryColumn* dwLeaf =
					ExpandDotWalkJoins(b, RootForPath(sources, *d.m_expr), pathCols, dwJoined, dwAliasSeq, *d.m_expr);
				b.TotalBy(dwLeaf, dim);
				oc.m_col = dwLeaf;
			}
			else {
				const wxString alias = wxString::Format(wxT("dim%u"), static_cast<unsigned>(nextSynthId - kSyntheticColumnBase));
				auto synth = std::make_shared<ibSyntheticScalarColumn>(alias, nextSynthId++, rt);
				b.TotalByDotWalk(pathCols, synth.get(), alias, dim);   // provider joins path, projects leaf scalar AS alias
				oc.m_col = synth.get(); oc.m_ownedCol = synth;
			}
		}
		outSchema.push_back(oc);
	}

	// SELECT output-name map, so a TOTALS aggregate may name a SELECTed field (the resource pattern:
	// SELECT Price … TOTALS SUM(Price); SELECT 1 AS test … TOTALS SUM(test)). A real column aggregates by
	// its metaID; a COMPUTED / constant field is projected by the door and aggregated through a SYNTHETIC
	// measure column (below) — both readable by the metaID-keyed totals fold.
	std::map<wxString, const ibQueryProjection*> selectByName;
	{
		int idx = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star || !p.m_expr) continue;
			selectByName[OutputNameFor(ast, p, idx++)] = &p;
		}
	}

	// The TOTALS aggregate set is COMMON across all dimension levels (each level rolls them IN-PLACE, so
	// the aggregate reads back off its own column — GetValue(col), same as a dimension).
	b.Totals();
	std::map<wxString, const ibBackendQueryColumn*> measureCol;   // computed alias -> its synthetic measure (projected once)

	for (const ibQueryAstExprPtr& agg : ast.m_totalsAggregates) {
		if (!agg || agg->m_kind != ibQueryAstExprKind::Func)
			Fail(0, 0, _("TOTALS expects aggregate functions (SUM/COUNT/MIN/MAX/AVG)"));

		// Output name read back via res[name]: COUNT(*) -> the function name; a field -> its identifier.
		const wxString outName = agg->m_star
			? ibQueryKeywordText(agg->m_func)
			: (agg->m_arg && !agg->m_arg->m_path.empty() ? agg->m_arg->m_path.back() : ibQueryKeywordText(agg->m_func));

		const ibBackendQueryColumn*           col   = nullptr;   // the column the fold aggregates by metaID
		std::shared_ptr<ibBackendQueryColumn> owned;             // set only for a synthetic computed measure
		if (!agg->m_star) {
			// A bare identifier may name a SELECTed field (alias) before a metadata attribute.
			const bool bareName = agg->m_arg->m_kind == ibQueryAstExprKind::Column && agg->m_arg->m_path.size() == 1;
			auto pit = bareName ? selectByName.find(agg->m_arg->m_path.back()) : selectByName.end();

			if (pit != selectByName.end() && pit->second->m_expr->m_kind != ibQueryAstExprKind::Column) {
				// COMPUTED / constant SELECT field — project the expression once, aggregate it through a
				// synthetic measure column that reads the projected field (unique id for the metaID fold).
				const wxString alias = pit->first;
				auto mit = measureCol.find(alias);
				if (mit != measureCol.end())
					col = mit->second;   // already projected for an earlier aggregate over the same field
				else {
					b.SelectExpr(BuildColumnExprFromAst(sources, *pit->second->m_expr, params), alias);
					owned = std::make_shared<ibSyntheticScalarColumn>(alias, nextSynthId++);   // RawType::Number measure
					col   = owned.get();
					measureCol[alias] = col;
				}
			}
			else if (pit != selectByName.end()) {
				col = ResolvePath(sources, *pit->second->m_expr).back();   // a SELECTed real column, named by alias
			}
			else col = ResolveColumnSingle(sources, *agg->m_arg);          // a metadata attribute
		}
		b.Aggregate(AggFn(agg->m_func), col, outName, agg->m_distinctArg);   // in-place — rolls into its own column (named for read-back)

		OutputColumn oc; oc.m_name = outName;
		if (col != nullptr) { oc.m_col = col; oc.m_ownedCol = owned; }   // real OR synthetic column — keyed by metaID
		else { oc.m_alias = outName; oc.m_byAlias = true; }              // COUNT(*) — synthetic receiver, read by name
		outSchema.push_back(oc);
	}

	// WHERE (flat verbs or the boolean tree) — dot-walk rejected here (the totals fold is its own path).
	if (ast.m_where) {
		if (IsFlatAndWhere(*ast.m_where))
			LowerFlatWhere(b, sources, *ast.m_where, params, /*allowDotWalk*/false);
		else
			b.Where(BuildWherePredicate(sources, *ast.m_where, params, /*allowDotWalk*/false));
	}

	// One read → one snapshot, with the TotalBy config STAMPED on the result; the runtime's
	// QueryResult.Select() folds it (ByGroupsHierarchy) — no second query, so detail and subtotal
	// cannot skew. The synthetic measure columns live in outSchema (m_ownedCol), which travels with the
	// selection — so the result's stamped raw pointers into them stay valid through Select(). (docs §22.1b)
	//
	// TOP n caps the DETAIL rows the fold runs over — the first n (0 = all); the subtotals then roll over
	// exactly those rows. The tree itself is not row-limited (a subtotal is not a detail row). (`topPage`, not the
	// fetch `page` parameter — the fold reads its own detail slice; the fetch page only drives the group-level fast path above.)
	ibReadPageRequest topPage;
	if (ast.m_top > 0) topPage.m_count = ast.m_top;
	// FOR UPDATE reaches the DETAIL read — the one that actually touches rows. A subtotal holds
	// nothing of its own, so locking the detail is what the word can honestly mean here.
	if (ast.m_forUpdate) topPage.m_lockForUpdate = true;
	return b.Execute(topPage);
}
