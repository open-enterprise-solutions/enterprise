////////////////////////////////////////////////////////////////////////////
//	L4 — lowering: BOTH front-ends land here — L4-1 text (Execute / ExecuteTotals) and
//	L4-2 LINQ (LowerLambda*) -> ibDataQueryBuilder, executed (queryLowering.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLowering.h"

#include "queryException.h"                // ibBackendQuerySourceException — L3 refuses in its own variety
#include "queryRewrite.h"                 // ibQueryRewrite — optimizer pass (AST -> AST)
#include "queryRender.h"                  // ibQueryOutputName — the ONE answer to "what is this field called"
#include "backend/diagnostics/journal.h"  // ibJournal — the technology journal
#include "queryRamTable.h"                // ibQueryRamTable — a package's temp table IS a snapshot
#include "queryTempStore.h"               // ibQueryTempTableStore — WHO keeps the temp tables alive
#include "tempTableQueryable.h"           // ibTempTableQueryable — a table handed in as a PARAMETER is a source
#include "queryable.h"                    // ibBackendQueryColumn / ibQueryFilterOp
#include "columnLayout.h"                 // ColumnFieldNames — a declared query publishes only what it can write
#include "queryHierarchy.h"               // ibQueryHierarchyScope / ibQueryHierarchyNamedValues — «IN HIERARCHY»
#include "queryableFactory.h"             // ibQueryableSourceDescriptor — the source that consumes its own condition
#include "queryProvider.h"                // ibBackendQueryProvider — GetProvider().ResolveReferenceTarget (dot-walk resolution)
#include "dbTableProvider.h"              // ibDbTableProvider::CanDeclareAsNamedQuery — would this door render whole?
#include "queryableFactory.h"             // ibQueryableFactory — source-namespace resolution
#include "backend/appData.h"              // ibApplicationData::GetQueryableFactory
#include "backend/metaData.h"             // ibMetaData::GetSourceFactory — resolve through the query's OWN config
#include "backend/metaCollection/genericData.h"  // ibValueMetaObjectGenericData::ResolveQueryConstant (value(...) resolution)
#include "backend/tabularModel.h"     // ibComparisonType
#include <unordered_set>                  // the link step matches rows by a key tuple
#include "backend/backend_exception.h"    // ibBackendCoreException

// ⚠ NAMED, NOT INHERITED. std::find / std::remove_if arrived in this file with the grouping and
// prune passes; MSVC hands <algorithm> over transitively and GCC/Clang do not, so the Windows build
// stayed green while the other three CI jobs could not compile it. See docs/portability.md.
#include <algorithm>
#include <wx/tokenzr.h>   // the OVER area may name several groupings, comma-separated
#include <set>          // the aggregate inputs already claimed in one TOTALS clause

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

// --- the results a PACKAGE has named, for the statements that read them (decl in queryable.h) ---
// Same shape and the same lifetime as the temp sources above, and separate from them for a reason:
// a temp table HAS rows, a named result is a QUERY — and which of the two roads a reader takes
// (declare it to the server, or take its rows) is decided by the reader's own lowering.
static thread_local const std::map<wxString, const ibQuerySelect*>* t_namedResults = nullptr;

ibNamedResultScope::ibNamedResultScope(const std::map<wxString, const ibQuerySelect*>& results)
	: m_prev(t_namedResults)
{
	t_namedResults = &results;
}

ibNamedResultScope::~ibNamedResultScope()
{
	t_namedResults = m_prev;
}

const ibQuerySelect* ibNamedResultScope::Find(const wxString& name)
{
	if (t_namedResults == nullptr || name.IsEmpty())
		return nullptr;
	// NAMES IN THIS LANGUAGE MATCH WITHOUT REGARD TO CASE, and the package stores them lower-cased —
	// asked here so every caller says the name the author wrote.
	const auto it = t_namedResults->find(name.Lower());
	return it != t_namedResults->end() ? it->second : nullptr;
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

// WHOSE sources this query sees: the config in scope, else the global base factory (common / plugin
// sources only — launcher, codeRunner before it opens anything). Nothing process-wide is consulted in
// between ON PURPOSE: "which configuration" is a question the CALLER can always answer, and every
// caller now does — the composer and the dynamic list from their own metadata, the constructor from
// the open one, a script query from the session it runs in. One place asks, so a new caller that
// forgets fails the same way everywhere instead of quietly resolving against someone else's config.
ibQueryableFactory* ibSourceMetaDataScope::GetFactory()
{
	if (const ibMetaData* md = t_sourceMetaData)
		if (ibQueryableFactory* factory = md->GetSourceFactory())
			return factory;

	return ibApplicationData::GetQueryableFactory();
}

namespace {

// ⭐ NAMES IN THIS LANGUAGE ARE MATCHED WITHOUT REGARD TO CASE — every comparison in this file says
// so (`IsSameAs(x, false)`, `CmpNoCase`), and a map keyed by the default `wxString` ordering would
// say the opposite in the one place a name is LOOKED UP rather than compared. `SELECT Qty * Price AS
// Total … TOTALS SUM(total)` then refuses a field the query plainly carries.
struct ibNoCaseLess
{
	bool operator()(const wxString& a, const wxString& b) const { return a.CmpNoCase(b) < 0; }
};

// The output-column descriptor the runtime reads back — used unqualified throughout this namespace.
using OutputColumn = ibQueryLowering::OutputColumn;

// All lowering errors carry the AST source span. Always throws (callers that return
// a value follow with an unreachable dummy return — the codebase's Error();return idiom).
//
// ⭐ THE TIER RAISES ITS OWN VARIETY. A query that does not hold up is not "an error with no
// subsystem" — it is L3 refusing, and the exception TYPE is what says so (docs/exceptions.md §3).
// Typed as Core it could only be caught by catching everything, which is the same as not being able
// to catch it at all: a script's Try/Except around a query, a tool that wants to show the author
// where the query is wrong, and a caller that must let real faults through were all indistinguishable.
void ThrowQueryException(unsigned int line, unsigned int col, const wxString& msg)
{
	// ⚠ NO PLACE IS SAID AS NO PLACE. A refusal raised without a span used to print "(line 0,
	// position 0)", which reads as a place — the first line of the query, where the mistake
	// usually is not. Nothing is printed instead, and the reader is not sent to a line that
	// answers nothing.
	//
	// And the number a person reads is 1-BASED, as it already is on the syntax path (queryParser):
	// the raw offset travels as DATA to ErrorAt, where a caret wants it, and only the SENTENCE is
	// shifted. Printing both spellings of the same number was one of the two ways a position here
	// could mislead.
	// THE NAME VARIETY, not the syntax one: everything raised through here is the LOWERING refusing —
	// the text read perfectly and asked for something that is not there. The lexer and the parser
	// raise the other (queryException.h).
	if (line == 0)
		ibBackendQueryNameException::Error(_("Query: %s"), msg);
	else
		ibBackendQueryNameException::ErrorAt(line, col,
			_("Query: %s (line %u, position %u)"), msg, line, col + 1);
}

// …and the same with the message's OWN arguments, because most refusals have to name the thing they
// are refusing. Formatting belongs here, once, and not at every callsite: the span, the wording and
// the assembly are one decision, and spreading `wxString::Format` through the file is the same
// decision made again in twenty places.
template <typename... Args>
void ThrowQueryException(unsigned int line, unsigned int col, const wxString& fmt, Args&&... args)
{
	ThrowQueryException(line, col, wxString::Format(fmt, std::forward<Args>(args)...));
}

ibValue EvalValue(const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params);   // defined below

// One resolved source in a query: its alias + queryable. Defined HERE, above the source resolver,
// because a condition the source CONSUMES is lowered while the source is still being built — see
// ResolveSource. (Its full role is described where the resolver's column lookup uses it, below.)
struct ibSourceBinding
{
	wxString                    m_alias;
	const ibBackendQueryable*   m_q = nullptr;
	// ⭐ THE SHARE, when this binding is one this lowering MINTED — an alias twin for a repeated
	// reading of one table. Held because the NEXT reading of that table takes THIS one as its origin
	// (see below), so the wrapper has to outlive the expression that made it.
	std::shared_ptr<const ibBackendQueryable> m_hold;
};

// ⭐ `keepUnfold` — do NOT resolve `IN HIERARCHY` into the subtree it stands for; leave the values as
// NAMED and put the word on the leaf. True only for a condition handed to a SOURCE, which folds by it
// (queryable.h, ibQueryCondition::m_unfold). Everywhere else the subtree is expanded here, because a
// provider renders `IN`, not a hierarchy.
ibQueryPredicatePtr BuildWherePredicate(const std::vector<ibSourceBinding>& sources,
                                        const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params,
                                        bool allowDotWalk, bool keepUnfold = false);   // defined below

// Resolve the FROM / JOIN source namespace.name to a queryable through the source
// factory appData owns (built-in metaobject families + plugin / external sources).
//
// `conditionsOut` collects the CONDITION arguments of a virtual table — the ones this function
// must not evaluate (see below). Null when the caller has nowhere to put them: name checking, for
// instance, only wants to know whether the source resolves at all.
const ibBackendQueryable* ResolveSource(const ibQuerySource& src, const std::map<wxString, ibValue>& params,
                                        std::vector<ibQueryAstExprPtr>* conditionsOut = nullptr)
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
		ThrowQueryException(0, 0, _("a source must be <Kind>.<Name>"));

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

	// Resolve through the config the query runs ON BEHALF OF — see ibSourceMetaDataScope::GetFactory for the order.
	ibQueryableFactory* factory = ibSourceMetaDataScope::GetFactory();
	if (factory == nullptr) {
		ThrowQueryException(0, 0, _("the query engine is not available (no application data)"));
		return nullptr;
	}
	if (!factory->HasNamespace(ns)) {
		// ⭐ A KIND NOBODY REGISTERED LOOKS EXACTLY LIKE A KIND THAT DOES NOT EXIST. A metatype reaches
		// the factory only if its own KIND registered a source descriptor (each does so in its
		// OnAfterRun — commonObject.cpp), so a missed registration reads to the author as "there is no
		// such thing", with nothing to distinguish the two. The journal says which it was.
		// …and WHICH source was asked for, because the kind alone cannot tell the two cases apart.
		// A kind is registered by its metatype's own OnAfterRun, so a read that happens WHILE the
		// configuration is still loading finds it missing and the same read succeeds seconds later
		// (measured 2026-08-23: `ChartOfCharacteristicTypes` unregistered at 16:42:25, resolving at
		// 16:42:28 in the same session). Naming the object says whose load order it is.
		ibJournalInfo(wxT("query.road"), wxT("source '%s.%s': kind '%s' is not registered with this configuration"),
		              ns, name, ns);
		ThrowQueryException(0, 0, wxString::Format(_("unknown metaobject kind '%s'"), ns));
		return nullptr;
	}

	// Source-call args (Balance(&Period, …)) -> the descriptor's CreateQueryable. The companion copies
	// them by value (e.g. ibBalanceQueryable stores m_period), so these locals may die after Resolve.
	//
	// ⚠ NOT EVERY ARGUMENT IS A VALUE. A source declares what its arguments MEAN
	// (DescribeParameters), and a CONDITION one is a predicate over the table's own columns —
	// evaluating it here would ask for the value of `Warehouse = &Store` with no row to read it
	// from. So the condition is NOT evaluated: it is handed back to the caller, which qualifies it
	// with this source's alias and ANDs it into the WHERE. From there the ordinary machinery does
	// the rest — plain conditions are pushed INTO ComputeRows, so a balance filters by its
	// dimension before folding rather than after.
	//
	// Its slot still travels, as an empty value: the descriptor reads its arguments by position,
	// and an empty filter is what "no filter" has always looked like to it.
	std::vector<ibQuerySourceParameter> declared;
	if (ibQueryableSourceDescriptor* descriptor = factory->FindDescriptor(ns, name))
		descriptor->DescribeParameters(declared);

	// ⚠ A CONDITION SLOT TAKES EITHER SHAPE, and the shape decides the road — not the declaration.
	// `Balance(&P, Warehouse = &W)` is a predicate and becomes a condition; `Balance(&P, &Filter)`
	// hands the source a VALUE (the filter structure it has always understood) and must keep doing
	// so. Sending a bare `&Filter` into the WHERE produced `WHERE &Filter`, which is not a condition
	// at all — the engine said "unsupported WHERE expression" about text nobody wrote that way.
	const auto isPredicate = [](const ibQueryAstExpr& e) {
		switch (e.m_kind) {
		case ibQueryAstExprKind::Compare:
		case ibQueryAstExprKind::Logical:
		case ibQueryAstExprKind::Not:
		case ibQueryAstExprKind::In:
		case ibQueryAstExprKind::Like:
		case ibQueryAstExprKind::Between:
		case ibQueryAstExprKind::IsNull:
			return true;
		default:
			return false;   // a parameter, a literal, a computation — a VALUE for the source
		}
	};

	// ⭐⭐ A CONDITION THE SOURCE CONSUMES ITSELF NEVER REACHES THE WHERE.
	//
	// The ordinary condition slot is sugar for "AND this into the query around the reading", and that
	// is right while the condition only SELECTS rows. It is wrong when the source has to ACT on it: an
	// accounting register asked for accounts «in hierarchy» reports the subordinates UNDER the account
	// that was named, and a filter applied AROUND the reading cannot fold — it can only remove rows the
	// reading already produced, which for `HIERARCHYONLY` removes exactly the rows the fold made.
	//
	// So a slot declared `m_consumedBySource` is lowered HERE and handed to the source by position.
	// Resolved against the source's CONDITION SCOPE, because the companion this call builds does not
	// exist yet — the scope is its stable side (a register's movements), where those columns live
	// anyway.
	// ⭐⭐ A CONDITION THE SOURCE CONSUMES ITSELF NEVER REACHES THE WHERE.
	//
	// The ordinary condition slot is sugar for "AND this into the query around the reading", which is
	// right while the condition only SELECTS rows. It is wrong when the source has to ACT on it: an
	// accounting register asked for accounts «in hierarchy» reports the subordinates UNDER the account
	// that was named, and a filter applied AROUND the reading cannot fold — it can only remove rows the
	// reading already produced, which under `HIERARCHYONLY` removes exactly the rows the fold made.
	//
	// Resolved against the source's CONDITION SCOPE, because the companion this call builds does not
	// exist yet — the scope is its stable side (a register's movements), where those columns live
	// anyway. And `keepUnfold`: the word travels with it, unexpanded, for the source to fold by.
	ibQueryableSourceDescriptor* descriptor = factory->FindDescriptor(ns, name);
	const ibBackendQueryable* conditionScope = descriptor != nullptr ? descriptor->GetConditionScope() : nullptr;
	std::vector<ibQueryPredicatePtr> consumed(declared.size());
	bool anyConsumed = false;

	std::vector<ibValue>  argVals;
	for (size_t i = 0; i < src.m_args.size(); ++i) {
		const bool isCondition = i < declared.size() && declared[i].m_condition
			&& src.m_args[i] && isPredicate(*src.m_args[i]);
		if (isCondition) {
			if (i < declared.size() && declared[i].m_consumedBySource && conditionScope != nullptr) {
				const std::vector<ibSourceBinding> scope{ ibSourceBinding{ wxString(), conditionScope } };
				consumed[i] = BuildWherePredicate(scope, *src.m_args[i], params,
					/*allowDotWalk*/ true, /*keepUnfold*/ true);
				anyConsumed = anyConsumed || consumed[i] != nullptr;
				argVals.push_back(ibValue());
				continue;
			}
			if (conditionsOut != nullptr)
				conditionsOut->push_back(src.m_args[i]);
			argVals.push_back(ibValue());
			continue;
		}
		if (!src.m_args[i]) {
			argVals.push_back(ibValue());
			continue;
		}

		// ⭐ A CLOSED SET IS NAMED, NOT QUOTED. `Turnovers(, , Hour, )` — the periodicity is a MEMBER
		// of the set the source declared, and a member is written the way every name in this language
		// is written: bare. The parser has no way to know that slot is special, so it reads `Hour` as
		// a one-segment column path; here, where the DECLARATION is at hand, it is read back as the
		// word it is.
		//
		// (It was written quoted for exactly one build. A quoted word is a string, and the query text
		// usually lives inside a script string literal where quotes double — so it reached the module
		// as `""Hour""` and read as a mistake, which it was.)
		if (i < declared.size() && !declared[i].m_condition && !declared[i].m_choices.empty()
		    && src.m_args[i]->m_kind == ibQueryAstExprKind::Column
		    && src.m_args[i]->m_path.size() == 1) {
			ibValue word;
			word.SetString(src.m_args[i]->m_path.front());
			argVals.push_back(word);
			continue;
		}

		// ⚠ CHECKING NAMES IS NOT RUNNING. `conditionsOut == nullptr` is the checking path (nobody
		// asked for the conditions because nobody is going to execute them), and there a parameter
		// with no value is NORMAL: the constructor is where a query is WRITTEN, and its parameters
		// are set by the code that later runs it. Raising there painted the verdict line red over a
		// query that is perfectly well-formed — telling the author to fix something that is not
		// theirs to fix here.
		//
		// At execution the same missing value still raises, in EvalValue's own words: that is where
		// it genuinely is a mistake.
		if (conditionsOut == nullptr) {
			try { argVals.push_back(EvalValue(*src.m_args[i], params)); }
			catch (const ibBackendException&) { argVals.push_back(ibValue()); }
		}
		else {
			argVals.push_back(EvalValue(*src.m_args[i], params));
		}
	}
	std::vector<ibValue*> argPtrs;
	for (ibValue& v : argVals)
		argPtrs.push_back(&v);

	// The consumed conditions ride WITH the call, by slot. With none, this is the resolve it always
	// was — the second entrance exists only for the sources that asked for it.
	const ibBackendQueryable* q = anyConsumed
		? descriptor->CreateQueryable(argPtrs.empty() ? nullptr : argPtrs.data(),
		                              static_cast<long>(argPtrs.size()), consumed)
		: factory->Resolve(ns, name,
			argPtrs.empty() ? nullptr : argPtrs.data(), static_cast<long>(argPtrs.size()));
	if (q == nullptr) {
		ThrowQueryException(src.m_line, src.m_col,
			wxString::Format(_("metaobject '%s.%s' not found or cannot be queried"), ns, name));
		return nullptr;
	}
	return q;
}

// One resolved source in a query: its alias + queryable. A single-source query is a list of one
// (alias may be empty); a JOIN adds one binding per joined source. Column resolution picks the
// source by alias prefix (`b.Field`), else searches all sources (the primary, sources[0], first).
// (ibSourceBinding itself is defined near the top of this file — the source resolver needs it, and a
//  resolver that runs BEFORE the sources are bound is exactly what a consumed condition is lowered
//  in.)

// ⭐⭐ IS THIS A SOURCE THIS RESOLVER CANNOT SEE INTO? A nested select, a temp table made by a
// statement outside this check — the query READS it, its columns are simply not knowable here.
//
// It has to be asked apart from "is there a source called that", because a binding with no queryable
// answers null to both and the two mean opposite things: one is a table that is GONE (say so, loudly)
// and the other is a table that is THERE (say nothing). Conflating them is how a perfectly good field
// of a nested query got reported as left over from a table the query does not read.
bool SourceIsOpaque(const std::vector<ibSourceBinding>& sources, const wxString& name)
{
	if (name.IsEmpty())
		return false;
	for (const ibSourceBinding& binding : sources)
		if (binding.m_q == nullptr && !binding.m_alias.IsEmpty() && binding.m_alias.IsSameAs(name, false))
			return true;
	return false;
}

// Find the source an alias names, or null if the first path segment is not a known alias.
const ibBackendQueryable* SourceForAlias(const std::vector<ibSourceBinding>& sources, const wxString& alias)
{
	if (!alias.empty())
		for (const ibSourceBinding& s : sources)
			if (!s.m_alias.empty() && s.m_alias.CmpNoCase(alias) == 0) return s.m_q;
	return nullptr;
}

// ⭐ IS THIS TABLE ALREADY IN THE FROM? — asked of the ORIGIN on both sides, so a third reading of
// one table sees the first two as the same table and not as two different sources. What comes back
// is the signal to give the new reading columns of its own (ibAliasQueryable).
//
// ⭐⭐ AND IT ANSWERS WITH THE LATEST READING, NOT WITH "YES". A twin's column ids are its origin's
// ids with the Alias kind stamped on, so a third reading built from the SAME origin would be given
// the SAME ids as the second — two sources indistinguishable again, which is the very thing the twin
// exists to prevent. Built from the second, it stamps the kind onto an already-stamped value and
// lands somewhere new (queryColumn.h, SyntheticId). Hence "the last one", not "any one".
const ibSourceBinding* LatestReadingOf(const std::vector<ibSourceBinding>& sources,
                                       const ibBackendQueryable* q)
{
	const ibBackendQueryable* const origin = ibOriginQueryable(q);
	if (origin == nullptr)
		return nullptr;
	const ibSourceBinding* latest = nullptr;
	for (const ibSourceBinding& s : sources)
		if (s.m_q != nullptr && ibOriginQueryable(s.m_q) == origin)
			latest = &s;
	return latest;
}

// The single source that OWNS a bare (unqualified) column. Fails on AMBIGUITY — a bare column that
// exists in more than one joined source must be qualified with an alias (SQL "ambiguous column"). Returns
// null only when no source owns it (the caller reports "unknown attribute") or sources is empty.
const ibBackendQueryable* OwnerOfBareColumn(const std::vector<ibSourceBinding>& sources, const wxString& name,
                                            int line, int col)
{
	const ibBackendQueryable* owner = nullptr;
	for (const ibSourceBinding& s : sources)
		// A binding with no queryable behind it is skipped, not dereferenced — same reason as the
		// guard in ResolvePath: this runs over queries that are still being written.
		if (s.m_q != nullptr && s.m_q->ResolveColumnByName(name) != nullptr) {
			if (owner != nullptr)
				ThrowQueryException(line, col, wxString::Format(
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
		ThrowQueryException(line, col, wxString::Format(
			_("duplicate source alias '%s': each FROM / JOIN source needs a distinct alias"), alias));
}

// ⭐⭐ THE NAME THE AUTHOR CALLS IT BY — for a refusal, which is read by whoever wrote the query and
// nobody else. `GetQueryName()` answers with the name the SCHEMA gave it: a register's balance
// surface says `AccumulationRegister1041_Balance`, a string that appears nowhere in the query and
// that the reader has no way to connect to what they typed ("what is 1041?"). The binding already
// holds the spoken name — the alias when one was written, the last segment of the path otherwise
// (ibQuerySourceName) — so the refusal points at the FROM the reader can see (2026-09-02).
//
// The storage name is not lost: it is what the SQL and the journal carry, where it belongs.
wxString SpokenSourceName(const std::vector<ibSourceBinding>& sources, const ibBackendQueryable* q)
{
	for (const ibSourceBinding& s : sources)
		if (s.m_q == q && !s.m_alias.empty())
			return s.m_alias;
	return q != nullptr ? q->GetQueryName() : wxString();
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
			// ⭐ AND THE SOURCE IS NAMED. "unknown attribute 'PointInTime'" is true and unactionable:
			// the author knows what they wrote, and what they need is WHICH table was asked — a
			// document has a moment, a register or a wrapped subquery does not, and the answer to
			// that decides whether the field or the table is the thing to change (2026-08-23).
			if (col == nullptr)
				ThrowQueryException(e.m_line, e.m_col, wxString::Format(
					_("unknown attribute '%s' on source '%s'"), path[1], SpokenSourceName(sources, q)));
			return col;
		}
		// path[0] is not an alias -> a dot-walk (Producer.Name), not allowed in this clause yet.
	}
	else if (path.size() == 1) {
		const ibBackendQueryable* q = OwnerOfBareColumn(sources, path[0], e.m_line, e.m_col);   // ambiguous -> Fail
		if (q == nullptr) {
			// A BARE name belongs to NO source here, so the thing to say is which ones were asked —
			// the reader can then see whether the table they meant is even in the query.
			wxString asked;
			for (const ibSourceBinding& s : sources)
				if (s.m_q != nullptr)
					asked += (asked.IsEmpty() ? wxT("") : wxT(", ")) + SpokenSourceName(sources, s.m_q);
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(
				_("unknown attribute '%s': no source of this query has it (asked: %s)"), path[0], asked));
		}
		return q->ResolveColumnByName(path[0]);
	}

	// ⭐ AND THE REFUSAL SAYS WHICH OF THE TWO THIS IS. Reaching here means the path is neither
	// `<source>.<field>` nor a bare field — and that happens for two different reasons a reader has
	// to tell apart: a walk of three or more segments (`Producer.Region.Name`, genuinely not carried
	// in this clause), or a two-segment path whose FIRST word names no source of this query at all —
	// a misspelled alias, or one that was never written. The old text announced a dot-walk for both,
	// so an author whose alias was simply wrong went looking for a missing feature (2026-09-04).
	wxString written;
	for (const wxString& segment : path)
		written += (written.IsEmpty() ? wxString() : wxT(".")) + segment;

	if (path.size() == 2) {
		wxString asked;
		for (const ibSourceBinding& s : sources)
			if (s.m_q != nullptr)
				asked += (asked.IsEmpty() ? wxString() : wxT(", ")) + SpokenSourceName(sources, s.m_q);
		ThrowQueryException(e.m_line, e.m_col, wxString::Format(
			_("'%s': this query has no source called '%s' (it reads: %s). If '%s' is a field to walk "
			  "THROUGH, this clause does not carry a walk yet."),
			written, path[0], asked, path[0]));
	}

	ThrowQueryException(e.m_line, e.m_col, wxString::Format(
		_("'%s' walks through a reference, which this clause does not carry yet: name the field on a "
		  "source of the query, or join the table it walks to"), written));
	return nullptr;
}

// A reference dot-walk path (Producer.Name | b.Producer.Name) -> the chain of columns SelectPath
// wants. An alias prefix selects the starting source; otherwise the walk starts on the source that
// owns the first segment (primary first).
std::vector<const ibBackendQueryColumn*> ResolvePath(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	// ⚠ A PATH IS A NAME, AND SOME EXPRESSIONS HAVE NONE. A call, a literal, an arithmetic node carry
	// an EMPTY path, and every line below indexes into it — which is a debug break in a std::vector,
	// not a refusal (Max, 2026-09-05: "vector subscript out of range", with the whole lowering on the
	// stack). Whoever asked for a path out of something that is not a name asked the wrong question;
	// this says so in the query's own words instead of dying.
	if (e.m_path.empty() && e.m_kind != ibQueryAstExprKind::Column)
		ThrowQueryException(e.m_line, e.m_col, _("expected a field here, not a computed expression"));

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
			ThrowQueryException(cast.m_line, cast.m_col, _("CAST expects a field to narrow"));

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
			ThrowQueryException(cast.m_line, cast.m_col, wxString::Format(
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
				ThrowQueryException(e.m_line, e.m_col, wxString::Format(
					_("unknown attribute '%s' on source '%s'"), path[k], SpokenSourceName(sources, cur)));
				return cols;
			}
			cols.push_back(col);
			if (k + 1 < path.size()) {
				const ibBackendQueryable* next = cur->GetProvider().ResolveReferenceTarget(cur, col);
				if (next == nullptr) {
					ThrowQueryException(e.m_line, e.m_col, wxString::Format(
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
	if (i == 0) {   // unqualified — start on the source that owns the first segment
		if (const ibBackendQueryable* q = OwnerOfBareColumn(sources, path[0], e.m_line, e.m_col)) {
			cur = q;   // ambiguous -> Fail, inside
		}
		else if (path.size() >= 2 && !SourceIsOpaque(sources, path[0])) {
			// ⭐ A QUALIFIED NAME WHOSE QUALIFIER IS NOTHING — `SliceLast.Dimension2` in a query that
			// no longer reads SliceLast. The first segment is neither a source alias nor a column of
			// any source, so this is not a dot-walk that went wrong somewhere in the middle: it is a
			// FIELD LEFT OVER from a table that is gone.
			//
			// ⚠ …AND NOT ONE THIS RESOLVER MERELY CANNOT SEE INTO. `SDFGH.Dimension1` over a nested
			// query named SDFGH is the table standing right there in the FROM; accusing it of being
			// gone painted the verdict line red over a query with nothing wrong with it.
			//
			// Said as itself, and with what to do about it. "unknown attribute 'SliceLast'" named the
			// segment the resolver happened to stop on and left the reader to work out that the thing
			// to delete is the whole field — which is the one thing the message could have said.
			wxString field;
			for (size_t k = 0; k < path.size(); ++k)
				field += (k > 0 ? wxT(".") : wxT("")) + path[k];
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(
				_("'%s' is left over from a table this query does not read: remove the field, or add "
				  "'%s' back to the tables"), field, path[0]));
		}
	}

	// ⚠ THERE MAY BE NO SOURCE TO WALK FROM, and that is a QUERY the author is in the middle of
	// writing — not a state this function may assume away. `cur` starts as the first source, and a
	// select with none (or one whose source did not resolve) leaves it null; the loop below then
	// dereferenced it and the process went down. It is exactly the everyday path: the constructor
	// re-asks the engine after EVERY edit, so adding a table or a link to a query that is not yet
	// whole ran straight through here.
	//
	// An error is the honest answer and the window already knows how to show one — the verdict line
	// under the tabs, in the engine's own words.
	if (cur == nullptr) {
		// Same distinction one more time: with an unreadable source in the query, a name this resolver
		// cannot place may perfectly well be that table's, and silence is the only honest answer.
		for (const ibSourceBinding& binding : sources)
			if (binding.m_q == nullptr)
				return cols;
		ThrowQueryException(e.m_line, e.m_col, wxString::Format(_("'%s' belongs to no table of this query"),
			path.empty() ? wxString() : path[0]));
		return {};
	}

	for (size_t k = i; k < path.size(); ++k) {
		const ibBackendQueryColumn* col = cur->ResolveColumnByName(path[k]);
		if (col == nullptr) {
			// The source is named for the same reason as above — and here it is the one that matters
			// most, because a WALK moves from table to table: the name that failed may belong to the
			// third hop, and without saying where it was asked the message points at the query rather
			// than at the step.
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(
				_("unknown attribute '%s' on source '%s'"), path[k], SpokenSourceName(sources, cur)));
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
					ThrowQueryException(e.m_line, e.m_col,
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

// The queryable that owns the LAST segment of a path — the dot-walk LEAF's own source. RootForPath
// answers where the walk STARTS; each non-leaf segment is then ONE PROVIDER HOP, the same hop
// ExpandDotWalkJoins makes to build the join, so there is no second way of walking a path here.
// Null when a segment is not a single-target reference — the honest answer, because then there is no
// one table the leaf lives in.
const ibBackendQueryable* OwnerOfPathLeaf(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e,
                                          const std::vector<const ibBackendQueryColumn*>& cols)
{
	const ibBackendQueryable* cur = RootForPath(sources, e);
	for (size_t i = 0; cur != nullptr && i + 1 < cols.size(); ++i)
		cur = cur->GetProvider().ResolveReferenceTarget(cur, cols[i]);
	return cur;
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
			ThrowQueryException(e.m_line, e.m_col,
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

// ⭐⭐ THE SAME EXPANSION, REACHABLE FROM WHERE AND ORDER BY.
//
// A dot-walk over several sources is turned into a column by joining the reference chain and taking
// its leaf (ExpandDotWalkJoins, above) — and the SELECT list has always done exactly that. WHERE and
// ORDER BY could not: they resolve a target through ResolveWhereTarget, which is handed a BOOL saying
// whether a dot-walk is realizable and, for a join, told no. So `SELECT Rate.Currency.Description …
// JOIN …` answered, and `ORDER BY Rate.Currency.Description` on the very same query was refused
// (measured 2026-09-06) — one query, two verdicts about one path.
//
// The thing they lacked was not permission but the BUILDER: the expansion adds joins, and a resolver
// that takes only (sources, expression) has nothing to add them to. So it is threaded the way this
// file already threads the other two facts a lowering needs everywhere — a thread-local set for the
// duration, restored on the way out. The joins it adds share the projection's own dedup map, so a
// prefix already walked for the SELECT is not walked again for the sort.
//
// Absent (the default) = no expansion available, and the refusal stands — which is right for every
// road that has no builder in hand (name checking, a nested lowering that only resolves).
struct ibDotWalkExpansion {
	ibDataQueryBuilder*                            m_builder = nullptr;
	std::map<wxString, const ibBackendQueryable*>* m_joined  = nullptr;
	int*                                           m_seq     = nullptr;
};
static thread_local const ibDotWalkExpansion* t_dotWalkExpansion = nullptr;

struct ibDotWalkExpansionScope {
	explicit ibDotWalkExpansionScope(const ibDotWalkExpansion* now)
		: m_prev(t_dotWalkExpansion) { t_dotWalkExpansion = now; }
	~ibDotWalkExpansionScope() { t_dotWalkExpansion = m_prev; }
	const ibDotWalkExpansion* m_prev;
};

// ⭐⭐ WHERE AN AGGREGATE INSIDE AN EXPRESSION IS REGISTERED — so `ISNULL(MIN(x), &Till)` is one
// expression and not a shape the builder needs a second grammar for.
//
// The fold itself is declared exactly as it would be standing alone (`b.Aggregate(...)`), under a
// name of its own; what replaces it in the tree is a reference to that name (OutputRef), read after
// the fold has run. So NOTHING about aggregation changes — not the item, not the slot, not either
// road's way of publishing the figure — and the expression around it is evaluated over the group row.
//
// Armed around ONE call to the expression builder, so a nested lowering cannot inherit it and file
// its aggregates against somebody else's query. Null everywhere else, which is what keeps the
// ordinary refusal ("an expression over an aggregate's result") standing where there is nothing to
// register against.
struct ibAggregateSink {
	ibDataQueryBuilder* m_builder = nullptr;
	int*                m_seq     = nullptr;
};
static thread_local const ibAggregateSink* t_aggregateSink = nullptr;

struct ibAggregateSinkScope {
	explicit ibAggregateSinkScope(const ibAggregateSink* now)
		: m_prev(t_aggregateSink) { t_aggregateSink = now; }
	~ibAggregateSinkScope() { t_aggregateSink = m_prev; }
	const ibAggregateSink* m_prev;
};

// ⭐⭐ CAN AN ENGINE BE ASKED FOR THIS AT ALL? — one question, asked of the finished expression.
//
// Two kinds cannot be written into SQL, and for the same reason rather than two: they are not
// readings of a stored field. `ValueAsk` puts a question to the VALUE (what it shows, what type it
// is — answered by the metatype, off the loaded object); `OutputRef` reads a figure that does not
// exist until a fold has run. Both are answered over the row that came back, and an expression
// containing either goes to the result rather than to the door (SetComputedOverRow).
//
// Asked ONCE, of the whole tree, because it is a property of the tree: `ISNULL(PRESENTATION(x), "-")`
// is our side even though ISNULL is not.
bool ExprIsAnsweredHere(const ibQueryColumnExpr* e)
{
	if (e == nullptr) return false;
	if (e->m_kind == ibQueryColumnExprKind::ValueAsk || e->m_kind == ibQueryColumnExprKind::OutputRef)
		return true;
	if (ExprIsAnsweredHere(e->m_lhs.get()) || ExprIsAnsweredHere(e->m_rhs.get())
	 || ExprIsAnsweredHere(e->m_else.get())) return true;
	for (const ibQueryColumnExprPtr& a : e->m_args) if (ExprIsAnsweredHere(a.get())) return true;
	for (const auto& wt : e->m_cases)               if (ExprIsAnsweredHere(wt.second.get())) return true;
	return false;
}

// Every SOURCE column such an expression reads — they have to be projected, or the question is put
// to a value that never came back. (The fold's own outputs are read by name and need nothing here.)
void GatherExprSourceColumns(const ibQueryColumnExpr* e, std::vector<const ibBackendQueryColumn*>& into)
{
	if (e == nullptr) return;
	if (e->m_kind == ibQueryColumnExprKind::Column && e->m_col != nullptr) {
		if (std::find(into.begin(), into.end(), e->m_col) == into.end()) into.push_back(e->m_col);
		return;
	}
	GatherExprSourceColumns(e->m_lhs.get(), into);
	GatherExprSourceColumns(e->m_rhs.get(), into);
	GatherExprSourceColumns(e->m_else.get(), into);
	for (const ibQueryColumnExprPtr& a : e->m_args) GatherExprSourceColumns(a.get(), into);
	for (const auto& wt : e->m_cases)               GatherExprSourceColumns(wt.second.get(), into);
}

// ⭐⭐ THE ONE DOOR TO THE EXPANSION — for the SELECT list as much as for WHERE and ORDER BY.
//
// The projection used to call ExpandDotWalkJoins itself while the other clauses reached it sideways,
// through the scope above. One mechanism with two ways in is a mechanism that will be taught twice:
// the day the expansion learns something (a hierarchy hop, a cast in the middle of a path), whoever
// teaches it will find one caller and miss the other. So every clause asks HERE, and the scope stops
// being a second route — it becomes how this door gets hold of the builder.
//
// Null when there is nothing to expand with, which the caller reads as "refuse" in its own words.
const ibBackendQueryColumn* ExpandDotWalkHere(const std::vector<ibSourceBinding>& sources,
                                              const std::vector<const ibBackendQueryColumn*>& cols,
                                              const ibQueryAstExpr& e)
{
	const ibDotWalkExpansion* const x = t_dotWalkExpansion;
	if (x == nullptr || x->m_builder == nullptr || x->m_joined == nullptr || x->m_seq == nullptr)
		return nullptr;
	return ExpandDotWalkJoins(*x->m_builder, RootForPath(sources, e), cols, *x->m_joined, *x->m_seq, e);
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
			ThrowQueryException(0, 0, wxString::Format(_("'%s' is not a single-target reference for a named ref-join (AS)"), segs[i]));
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
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(_("parameter '&%s' is not set"), e.m_paramName));
			return ibValue();
		}
		return *v;
	}
	// ⭐ `DATETIME(2026, 1, 1)` IS A LITERAL that happens to be spelled as a call — every argument is a
	// constant by construction, so it is settled here with the other constants rather than travelling
	// as an expression the server would rebuild per row. Reached by a comparison value, a virtual
	// table's argument and a projected constant alike, because all three ask this one function.
	if (e.m_kind == ibQueryAstExprKind::ScalarCall && e.m_scalar == ibQueryScalarFn::DateTime) {
		int parts[6] = { 0, 1, 1, 0, 0, 0 };
		for (size_t i = 0; i < e.m_args.size() && i < 6; ++i) {
			const ibValue v = e.m_args[i] ? EvalValue(*e.m_args[i], params) : ibValue();
			parts[i] = static_cast<int>(v.GetNumber().ToInt());
		}
		wxDateTime moment(static_cast<wxDateTime::wxDateTime_t>(parts[2] > 0 ? parts[2] : 1),
		                  static_cast<wxDateTime::Month>((parts[1] > 0 ? parts[1] : 1) - 1),
		                  parts[0],
		                  static_cast<wxDateTime::wxDateTime_t>(parts[3]),
		                  static_cast<wxDateTime::wxDateTime_t>(parts[4]),
		                  static_cast<wxDateTime::wxDateTime_t>(parts[5]));
		if (!moment.IsValid())
			ThrowQueryException(e.m_line, e.m_col, _("DATETIME was given a date that does not exist"));
		return ibValue(moment);
	}
	// ⭐⭐ `TYPE(Number)` IS A CONSTANT TOO — a TYPE is an ordinary value here (ibValueType, registered
	// as `Type`), so the word denotes one exactly as `DATETIME(…)` denotes a date, and it is settled
	// with the other constants rather than being a shape only the type test knows about.
	//
	// This is what makes the two spellings one: a script may hand a type in through a parameter, and a
	// query may write it out — both arrive here as a value carrying a clsid, and whoever compares them
	// asks that value, not the syntax it came from. A REFERENCE type keeps its own road (the metaobject
	// is resolved by name, so `TYPE(Catalog.Goods)` is folded into REFS before reaching this function);
	// what lands here is a type NAMED IN ONE WORD — a primitive.
	if (e.m_kind == ibQueryAstExprKind::ScalarCall && e.m_scalar == ibQueryScalarFn::Type) {
		wxString named;
		if (!e.m_args.empty() && e.m_args.front()->m_kind == ibQueryAstExprKind::Column)
			for (const wxString& segment : e.m_args.front()->m_path)
				named += (named.IsEmpty() ? wxString() : wxT(".")) + segment;
		if (named.IsEmpty())
			ThrowQueryException(e.m_line, e.m_col, _("TYPE takes a type name"));
		// Made through the door in typeDescription.h — this tier names no runtime value class.
		const ibValue type = ibTypeValueByName(named);
		if (type.IsEmpty())
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(_("TYPE: '%s' is not a type"), named));
		return type;
	}
	if (e.m_kind == ibQueryAstExprKind::Value) {
		// value(<Kind>.<Name>.<Member>) — a literal reference constant. Resolve the metaobject through the SAME
		// factory a FROM source uses, then read the member's value straight off the queryable's metaobject (Max:
		// "you just get the runtime value off the queryable by name"). The metaobject try-resolves (bool + out); the
		// engine raises the exception HERE so it carries the query source span (Max).
		if (e.m_path.size() < 3)
			ThrowQueryException(e.m_line, e.m_col, _("value(...) needs <Kind>.<Name>.<Member>"));
		const wxString& ns     = e.m_path.front();
		const wxString& member = e.m_path.back();
		wxString name = e.m_path[1];
		for (size_t i = 2; i + 1 < e.m_path.size(); ++i) name += wxT(".") + e.m_path[i];

		ibQueryableFactory* factory = ibSourceMetaDataScope::GetFactory();
		if (factory == nullptr)
			ThrowQueryException(e.m_line, e.m_col, _("the query engine is not available (no application data)"));
		const ibBackendQueryable* q = factory->Resolve(ns, name);
		if (q == nullptr)
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(_("value(): metaobject '%s.%s' not found"), ns, name));
		const ibValueMetaObjectGenericData* meta = q->GetSourceMetaObject();
		ibValue out;
		if (meta == nullptr || !meta->ResolveQueryConstant(member, out))
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(_("value(): '%s.%s' has no '%s' (an empty reference or a predefined item)"), ns, name, member));
		return out;
	}
	ThrowQueryException(e.m_line, e.m_col, _("expected a literal or a parameter as the comparison value"));
	return ibValue();
}

// 🛑 THE GRAMMAR READS A WINDOW; THIS TIER DOES NOT YET LOWER ONE — so it says so, naming the call
// it choked on.
//
// The parser accepts `SUM(x) OVER (…)` and ROW_NUMBER() / RANK() / DENSE_RANK() (queryParser.cpp),
// and L2-1 has carried an OVER since 2026-08-20 (ibQueryExpr::m_over). What is missing is the road
// BETWEEN them: an aggregate travels L4→L3 as an ibAggregateItem, which has no window field yet, and
// five places in dbTableProvider build the call out of it.
//
// Until that road exists the only honest answer is a refusal. Dropping the OVER would leave a query
// that RUNS — reporting a plain total under the name of a running one, which reconciles to nothing
// and reads as bad data rather than as a missing feature.
void RefuseUnloweredWindow(const ibQueryAstExpr& e)
{
	if (!e.m_over && !ibIsRankingKeyword(e.m_func))
		return;

	ThrowQueryException(e.m_line, e.m_col,
		_("window functions are not executed yet: the language reads OVER (...), the query engine does not run it"));
}

// The language's word -> the tier's concept. Twin of AggFn, with the three ranking calls the folds
// do not have; the SQL spelling of either is the provider's business.
ibQueryWindowFn WindowFnOf(ibQueryKeyword kw)
{
	switch (kw) {
	case ibQueryKeyword::Sum:       return ibQueryWindowFn::Sum;
	case ibQueryKeyword::Count:     return ibQueryWindowFn::Count;
	case ibQueryKeyword::Min:       return ibQueryWindowFn::Min;
	case ibQueryKeyword::Max:       return ibQueryWindowFn::Max;
	case ibQueryKeyword::Avg:       return ibQueryWindowFn::Avg;
	case ibQueryKeyword::RowNumber: return ibQueryWindowFn::RowNumber;
	case ibQueryKeyword::Rank:      return ibQueryWindowFn::Rank;
	case ibQueryKeyword::DenseRank: return ibQueryWindowFn::DenseRank;
	default:                        return ibQueryWindowFn::Count;
	}
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

// Forward decls used by IN (subquery): build a sub-SELECT into a queryable.
//
// ⭐ SHARED, NOT UNIQUE. A wrapper owns the columns it publishes, and the things that read those
// columns — the builder, the result it stamps, the output schema — outlive this list. So ownership
// is SHARED from the start: `owner` keeps the run's wrappers together while the lowering runs, each
// consumer takes a share of the ones it names, and a wrapper dies when the last of them lets go.
// The sources built FOR one execution — a nested subquery wrapper, a named query declared as a CTE.
// Held as the BASE type: both mint columns of their own and answer ShareColumn, and the run keeps
// them alive by holding the source, not by knowing which class it is.
using ibSubqueryOwner = std::vector<std::shared_ptr<const ibBackendQueryable>>;
std::shared_ptr<ibSubqueryQueryable> WrapSelectAsQueryable(const ibQuerySelect& sel,
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
ibQueryColumnExprPtr BuildWindowExprFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params);   // defined below

// Is this AST expression a COMPUTED WHERE / aggregate-input lhs (arithmetic or CASE)?
bool IsComputedExprAst(const ibQueryAstExpr& e)
{
	// A SCALAR CALL COUNTS — `YEAR(Date)`, `DATEDIFF(a, b, Day)`, `SUBSTRING(s, 1, 3)` are computed
	// per row exactly as arithmetic and CASE are, so every place that asks "is this computed" (a
	// WHERE lhs, an aggregate's input, a sort key) gets the same answer about all three.
	return e.m_kind == ibQueryAstExprKind::Arith || e.m_kind == ibQueryAstExprKind::Case
	    || e.m_kind == ibQueryAstExprKind::ScalarCall;
}

// Gate for a computed (arithmetic / CASE) condition lhs / aggregate input / projection: a single DB source
// lowers it server-side; a COMPUTED source (register slice / subquery) evaluates it in RAM per row
// (EvalColumnExprRow — SELECT / WHERE / SUM alike). Only a JOIN across leaves is still unsupported here
// (the RAM stitch has no cross-leaf expression evaluator yet).
void GateComputedExpr(const std::vector<ibSourceBinding>& sources, const ibQueryAstExpr& e)
{
	if (sources.size() > 1)
		ThrowQueryException(e.m_line, e.m_col, _("an arithmetic / CASE expression here is not yet supported over a JOIN"));
}

// Build the full boolean WHERE as an L3 predicate TREE (ibQueryPredicate). The door lowers it to
// the L2 IR (OR/NOT/IS NULL all expressible there). IN expands to Or(Eq …), BETWEEN to And(>=, <=),
// NOT IN / NOT BETWEEN / NOT LIKE wrap the positive form in Not — so the tree needs no dedicated node.
// Compare / LIKE / BETWEEN leaves carry a reference dot-walk PATH (the leaf condition's m_path) when
// allowDotWalk; the provider joins it (single-source non-aggregate read). IN / IS NULL stay plain-column
// (no path leaf on those nodes yet). Used for single-source queries + co-located JOIN booleans.
ibQueryPredicatePtr BuildWherePredicate(const std::vector<ibSourceBinding>& sources,
                                        const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params,
                                        bool allowDotWalk, bool keepUnfold)
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
		// ⭐ `VALUETYPE(x) = TYPE(Catalog.Counterparties)` IS `x REFS Catalog.Counterparties`, and it
		// is folded into that here rather than given a machinery of its own. Two spellings of one
		// question — a person writes whichever their trade taught them — and one thing underneath, so
		// there is one place where the answer can be right or wrong.
		//
		// `<>` folds to the negated form for the same reason. Any OTHER shape (a type compared with a
		// number, VALUETYPE against VALUETYPE) is left to fall through and be refused by the ordinary
		// path, which knows how to say what it could not read.
		// ⭐⭐ THE OTHER SIDE IS WHATEVER *DENOTES A TYPE* — not necessarily the word `TYPE`.
		//
		// A type is a VALUE in this system (ibValueType, registered as `Type`), so a script can put one
		// in a parameter and a query can compare against it: `VALUETYPE(Src.Source) = &Kind`. Read as
		// "the other side must be a TYPE(...) call", that sentence was refused — and the mechanism it
		// needed was already standing: the parameter arrives evaluated, and the type it denotes is the
		// clsid it carries. (Max, 2026-09-06.)
		//
		// ⚠ AND A TYPE IS NOT ONLY A REFERENCE. `VALUETYPE(x) = TYPE(Number)` asks a composite attribute
		// whether it is holding a number today — an everyday question, and the same question, so it takes
		// the same road. It is NOT folded into REFS: REFS resolves a METAOBJECT and answers about
		// `_RTRef`, while a primitive is answered by the stored `_TYPE` tag. The predicate carries the
		// clsid; the renderer asks IsReference to know which field says so.
		if (e.m_lhs && e.m_rhs && (e.m_cmp == ibQueryCompareOp::Eq || e.m_cmp == ibQueryCompareOp::Ne)) {
			auto isValueTypeCall = [](const ibQueryAstExpr& a) {
				return a.m_kind == ibQueryAstExprKind::ScalarCall && a.m_scalar == ibQueryScalarFn::ValueType;
			};
			const ibQueryAstExpr* valueSide = nullptr;
			const ibQueryAstExpr* typeSide  = nullptr;
			for (int pass = 0; pass < 2; ++pass) {
				const ibQueryAstExpr& a = pass == 0 ? *e.m_lhs : *e.m_rhs;
				const ibQueryAstExpr& b = pass == 0 ? *e.m_rhs : *e.m_lhs;
				if (isValueTypeCall(a) && !isValueTypeCall(b)) { valueSide = &a; typeSide = &b; break; }
			}
			if (valueSide != nullptr && typeSide != nullptr) {
				if (valueSide->m_args.empty())
					ThrowQueryException(e.m_line, e.m_col, _("VALUETYPE takes the value whose type is asked"));
				const bool negated = (e.m_cmp == ibQueryCompareOp::Ne);

				// SPELLED AS A NAME — `TYPE(<Kind>.<Name>)`. Written as the REFS it means and lowered by
				// the one case that knows how, so the two spellings cannot drift apart.
				if (typeSide->m_kind == ibQueryAstExprKind::ScalarCall && typeSide->m_scalar == ibQueryScalarFn::Type
				    && !typeSide->m_args.empty()
				    && typeSide->m_args.front()->m_kind == ibQueryAstExprKind::Column
				    && typeSide->m_args.front()->m_path.size() >= 2) {
					ibQueryAstExpr refs;
					refs.m_kind    = ibQueryAstExprKind::Refs;
					refs.m_lhs     = valueSide->m_args.front();
					refs.m_path    = typeSide->m_args.front()->m_path;
					refs.m_negated = negated;
					refs.m_line    = e.m_line;
					refs.m_col     = e.m_col;
					return BuildWherePredicate(sources, refs, params, allowDotWalk);
				}

				// …OR CARRIED AS A VALUE — a &parameter, a value(...) constant, `TYPE(Number)`. The type
				// it denotes is read off it and the predicate is built directly: there is no metaobject
				// PATH to write a REFS with, and inventing one would be a second answer to "which type".
				ibClassID target = 0;
				if (!ibTypeValueClsid(EvalValue(*typeSide, params), target))   // the door — see typeDescription.h
					ThrowQueryException(typeSide->m_line, typeSide->m_col,
						_("the other side of VALUETYPE has to BE a type: TYPE(<Kind>.<Name>), or a parameter holding one"));

				const std::vector<const ibBackendQueryColumn*> cols =
					ResolveWhereTarget(sources, *valueSide->m_args.front(), allowDotWalk);
				return ibQueryPredicate::RefType(cols.back(), target, negated, cols);
			}
		}

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
		// ⭐⭐ HANDED TO A SOURCE, THE WORD SURVIVES INSTEAD OF BEING RESOLVED. The source folds by it —
		// the subordinates report UNDER the account that was named — and a fold cannot be reconstructed
		// from twenty expanded values, because nothing in them says which one they roll into.
		if (keepUnfold && e.m_unfold != ibQueryDimUnfold::Elements) {
			ibQueryCondition named;
			named.m_col    = cols.back();
			named.m_path   = cols.size() > 1 ? cols : std::vector<const ibBackendQueryColumn*>{};
			named.m_op     = ibQueryFilterOp::In;
			named.m_unfold = e.m_unfold;
			named.m_values = ibQueryHierarchyNamedValues(
				e.m_list.empty() ? ibValue() : EvalValue(*e.m_list.front(), params));
			ibQueryPredicatePtr leaf = ibQueryPredicate::Leaf(named);
			return e.m_negated ? ibQueryPredicate::Not(leaf) : leaf;
		}

		if (e.m_unfold != ibQueryDimUnfold::Elements) {
			// ⭐⭐ «IN HIERARCHY» IS RESOLVED HERE AND NOWHERE BELOW. The named values are walked down
			// to what is subordinate to them, and what leaves this function is the ordinary IN of the
			// line above — so the door, the RAM evaluator and all four drivers keep the ONE set-valued
			// operator they already render, and nothing under L4 learns a word it would have to expand
			// with a read of its own. The operand is a single &parameter (the parser admits nothing
			// else here), which may hold one value or a list of them.
			//
			// ⚠ THE SUBTREE IS READ THROUGH THE COLUMN, so the column's own source has to be known —
			// and being unable to name it is an ERROR, not a quieter filter. Degrading to «in» here
			// would answer a question nobody asked with a number that looks entirely right: the same
			// rows, minus every subordinate. (A FLAT source is a different case and stays silent: the
			// target is known, it simply records no parent, and «in hierarchy» over a flat list IS
			// the list.)
			const ibBackendQueryable* owner = OwnerOfPathLeaf(sources, *e.m_lhs, cols);
			if (owner == nullptr)
				ThrowQueryException(e.m_line, e.m_col, _("IN HIERARCHY needs a field whose own source is known - a reference column of a source this query reads"));
			const ibQueryHierarchyScope scope(owner, cols.back(),
				ibQueryHierarchyNamedValues(e.m_list.empty() ? ibValue() : EvalValue(*e.m_list.front(), params)),
				e.m_unfold);
			values = scope.Accepted();
		}
		else if (e.m_subquery) {
			ibSubqueryOwner localOwner;   // the inner queryable lives only for this materialisation
			const std::shared_ptr<ibSubqueryQueryable> subq = WrapSelectAsQueryable(*e.m_subquery, params, localOwner);
			const std::vector<const ibBackendQueryColumn*> outCols = subq->GetColumns();
			if (outCols.size() != 1 || outCols.front() == nullptr)
				ThrowQueryException(e.m_line, e.m_col, _("IN (subquery) must SELECT exactly one column"));
			ibDataQueryBuilder sq;
			sq.From(subq);   // owning — the values are drained below, but the builder holds it regardless

			// 🛑⭐⭐ READ THE COLUMN, NOT AN ALIAS — and this is the same trap the projection above
			// carries a note about, sprung a second time in a quieter place.
			//
			// Only a RAW column is one projected field a NAME can fetch. A reference is a SPREAD of
			// physical fields (`_TYPE`, `_RTRef`, `_RRRef`), which the provider projects under its
			// own names — so `Select(col, "v")` + `GetColumn("v")` found nothing and handed back the
			// type's DEFAULT: an EMPTY reference, once per row. The IN set then held N empty refs,
			// every comparison failed, and the query came back with NO ROWS AND NO COMPLAINT.
			//
			// ⚠ That silence is the whole cost of it: `WHERE G.Ref IN (SELECT M.Goods FROM …)` — a
			// plain "the items something moved" — answered EMPTY on data that plainly matched, and a
			// report built on it looks finished (measured 2026-09-03: the subquery alone returned its
			// row, the same subquery inside IN returned none).
			//
			// A single source needs no select-list at all — the door's own contract says consumers
			// loop attributes via GetValue, which recovers the attribute behind the column and
			// materialises a whole reference. Scalars come back through the same call.
			ibDataQueryResult r = sq.Execute(ibReadPageRequest{});
			while (r.Next())
				values.push_back(r.GetValue(outCols.front()));
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
		// ⭐ WHAT IS ASKED ABOUT MAY BE A FOLD, not a column. `ISNULL(MIN(x), y)` unfolds to a CASE
		// whose WHEN is `MIN(x) IS NULL`, and a fold has no column to point at — it is read back by the
		// name it was registered under. Only where a sink is armed, which is exactly the projection
		// that will attach the finished expression to the result.
		if (t_aggregateSink != nullptr && e.m_lhs && ibQueryMentionsAggregate(e.m_lhs))
			return ibQueryPredicate::NullExpr(BuildColumnExprFromAst(sources, *e.m_lhs, params), e.m_negated);
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);
		return ibQueryPredicate::Null(cols.back(), e.m_negated, cols);   // m_negated = IS NOT NULL; path = dot-walk
	}

	// ⭐ `<expr> [NOT] REFS <Kind>.<Name>` — WHICH OF ITS TYPES IS IN THIS ROW.
	//
	// The type travels as the CLSID a reference to that table carries, and it is obtained the way
	// everything else obtains a reference constant here: by asking the metaobject for its EMPTY
	// reference (`ResolveQueryConstant`) and reading the class off it. One door for "a reference to
	// this table", so `value(Catalog.Goods.EmptyRef)` and `REFS Catalog.Goods` cannot come to
	// disagree about what that type is.
	case ibQueryAstExprKind::Refs: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, *e.m_lhs, allowDotWalk);

		ibQuerySource target;
		target.m_name = e.m_path;
		const ibBackendQueryable* q = ResolveSource(target, std::map<wxString, ibValue>());
		if (q == nullptr)
			return nullptr;   // ResolveSource has already raised, in its own words

		const ibValueMetaObjectGenericData* meta = q->GetSourceMetaObject();
		ibValue emptyRef;
		if (meta == nullptr || !meta->ResolveQueryConstant(ibRefMember::EmptyRef, emptyRef) || emptyRef.GetClassType() == 0) {
			wxString named;
			for (const wxString& segment : e.m_path)
				named += (named.IsEmpty() ? wxString() : wxT(".")) + segment;
			ThrowQueryException(e.m_line, e.m_col, wxString::Format(
				_("REFS takes a table a reference can point at; '%s' is not one"), named));
		}

		return ibQueryPredicate::RefType(cols.back(), emptyRef.GetClassType(), e.m_negated, cols);
	}

	case ibQueryAstExprKind::Column: {
		// A BARE column / dot-walk used as a predicate is a TRUTHY test on a Boolean column:
		// `WHERE Field.Seller`  ==  `WHERE Field.Seller = TRUE`. (`= TRUE` lowers as a plain Compare above.)
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, e, allowDotWalk);
		return ibQueryPredicate::Leaf(CondEq(cols, ibValue(true)));
	}

	default:
		ThrowQueryException(e.m_line, e.m_col, _("unsupported WHERE expression"));
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
		ThrowQueryException(e.m_line, e.m_col, _("expected a column (or a reference dot-walk path) here"));
	std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, e);
	if (cols.empty())
		ThrowQueryException(e.m_line, e.m_col, _("could not resolve the column"));
	if (cols.size() > 1 && !allowDotWalk) {
		// ⭐ …OR THE READ CAN JOIN IT — which, over several sources, is what the SELECT list already
		// does with the same path. The expansion adds the reference chain as LEFT joins and hands back
		// the leaf as an ORDINARY column, so everything after this line goes on reading a plain column
		// and no clause needs a dot-walk case of its own. (See ibDotWalkExpansion.)
		if (const ibBackendQueryColumn* leaf = ExpandDotWalkHere(sources, cols, e))
			return { leaf };
		ThrowQueryException(e.m_line, e.m_col, _("a reference dot-walk here needs a single, non-aggregate source"));
	}
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
			ThrowQueryException(e.m_line, e.m_col, _("OR in this WHERE is not lowered to flat conditions"));
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
		ThrowQueryException(e.m_line, e.m_col, _("this WHERE expression is not a flat condition"));
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
	// ⚠ A COMPARISON IS FLAT ONLY IF IT IS ONE. `VALUETYPE(x) = TYPE(Catalog.Goods)` wears the shape of
	// a comparison and is a TYPE TEST — the predicate road folds it into the REFS it means, and the flat
	// road has no idea it exists. Sent down the flat road it lost the fold and was refused for the
	// reason the flat road understood: "expected a column here", pointing at TYPE.
	//
	// One question, asked where the road forks: anything the predicate road knows how to fold belongs to
	// the predicate road. (Adding the fold to the flat road too would be the second spelling this whole
	// arc exists to avoid.)
	case ibQueryAstExprKind::Compare: {
		// ⭐⭐ ASKED ABOUT *VALUETYPE*, AND ABOUT NOTHING ELSE. What makes this comparison a type test is
		// the call on ONE side; what stands on the other is the predicate road's business, and listing
		// the shapes it accepts here would be that list written twice.
		//
		// 🛑 It WAS written twice — "VALUETYPE on one side AND a TYPE(...) call on the other" — and the
		// day the fold learned to take a type from a &parameter, this half went on routing that
		// sentence down the flat road, which has never heard of the fold: `VALUETYPE(x) = &Kind` was
		// refused with "VALUETYPE cannot be used as a computed value here", about a query the tier one
		// call away had just been taught to answer (2026-09-06).
		auto isValueTypeCall = [](const ibQueryAstExprPtr& side) {
			return side && side->m_kind == ibQueryAstExprKind::ScalarCall
			            && side->m_scalar == ibQueryScalarFn::ValueType;
		};
		return !(isValueTypeCall(e.m_lhs) || isValueTypeCall(e.m_rhs));
	}
	case ibQueryAstExprKind::Like:    return !e.m_negated;
	case ibQueryAstExprKind::Between: return !e.m_negated;
	// `x REFS Document.Order` is a predicate of its own — the flat road carries conditions, not kinds,
	// so it goes with Not / In / IsNull down the road that has one.
	default:                       return false;   // Not / In / IsNull / Refs
	}
}

// Build an L3 computed-column expression (ibQueryColumnExpr) from an AST expression — arithmetic, CASE,
// a plain column, a literal, or a &parameter. The provider lowers it to the L2 IR and projects it AS an
// alias. Plain columns only (no dot-walk inside a computed expression yet). Single source.
// ⭐⭐ `SUM(x) OVER (PARTITION BY … ORDER BY … ROWS)` — the AST's windowed call, as an L3 expression
// the provider spells through the one clause writer every driver shares.
//
// The FUNCTION IS ITS NAME and one name serves both families: the language writes `SUM` / `COUNT` /
// `MIN` / `MAX` / `AVG` and `ROW_NUMBER` / `RANK` / `DENSE_RANK` exactly as SQL does, so nothing is
// translated here. A ranking call takes no argument, and that is carried by the absence of one.
//
// ⚠ THE FRAME IS TWO WORDS AND NO MORE, and the mapping is a decision rather than a detail: `ROWS`
// counts rows one by one through this one, `RANGE` lets every row sharing this one's sort key
// contribute (three movements stamped with the same period are one period's worth of stock, in any
// order).
//
// ⭐⭐ AND UNSTATED IS ANSWERED BY THE ORDER, because that is SQL's own rule and the server obeys it
// whatever we decide here. With no ORDER the frame is the partition WHOLE — the denominator of a
// share, and there is no "through this row" without a row order to be through. With an ORDER it is
// RANGE through this row's peers, which is what `OVER (PARTITION BY c ORDER BY p)` means to every
// server and is therefore what the SQL road has been answering all along.
//
// 🛑 Deciding WHOLE for both was one query giving two numbers by the road it took: a running total
// over a joined source read 90 on every row (the partition entire) while the same window over a
// single table read 19, 40, 63, 90 — because the single table went to the server, which applied its
// own default, and the join was folded here, which applied this one (measured 2026-09-06). The
// obligation the RAM twins carry is stated all over this file; this is it, honoured in the one place
// the frame is settled, so neither road has to be bent to meet the other.
ibQueryColumnExprPtr BuildWindowExprFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	std::vector<ibQueryColumnExprPtr> partition;
	std::vector<std::pair<ibQueryColumnExprPtr, bool>> order;
	ibQueryWindowFrame frame = ibQueryWindowFrame::Whole;
	if (e.m_over) {
		for (const ibQueryAstExprPtr& key : e.m_over->m_partitionBy)
			if (key) partition.push_back(BuildColumnExprFromAst(sources, *key, params));
		for (const ibQueryOrderItem& key : e.m_over->m_orderBy)
			if (key.m_expr) order.emplace_back(BuildColumnExprFromAst(sources, *key.m_expr, params), key.m_ascending);
		frame = e.m_over->m_frame == ibQueryAstFrame::Rows  ? ibQueryWindowFrame::Rows
		      : e.m_over->m_frame == ibQueryAstFrame::Range ? ibQueryWindowFrame::Range
		      : order.empty()                               ? ibQueryWindowFrame::Whole
		                                                    : ibQueryWindowFrame::Range;   // see above
	}
	// The input, where there is one. `COUNT(*)` and the ranking calls have none.
	ibQueryColumnExprPtr arg;
	if (!e.m_star && e.m_arg && !ibIsRankingKeyword(e.m_func))
		arg = BuildColumnExprFromAst(sources, *e.m_arg, params);

	return ibQueryColumnExpr::WindowAgg(WindowFnOf(e.m_func), std::move(arg), std::move(partition),
	                                    std::move(order), frame);
}

// The calendar WORD an argument names — `Month`, `Day`, `Quarter`. It arrives as an ordinary Column
// node holding one segment, because that is what it looks like to a parser that was deliberately not
// taught these words (queryKeywords.h says why). Read here, against the one vocabulary the language
// has for calendar units, so `Turnovers(&A, &B, Month)` and `BEGINOFPERIOD(x, Month)` cannot come to
// mean different things by the same word.
ibTotalsPeriod ReadUnitArgument(const ibQueryAstExpr& call, size_t index, const wxString& what)
{
	if (index >= call.m_args.size() || !call.m_args[index])
		ThrowQueryException(call.m_line, call.m_col, wxString::Format(
			_("%s needs a period: Second, Minute, Hour, Day, Week, Month, Quarter, HalfYear or Year"), what));

	const ibQueryAstExpr& arg = *call.m_args[index];
	ibTotalsPeriod unit = ibTotalsPeriod::Month;
	if (arg.m_kind != ibQueryAstExprKind::Column || arg.m_path.size() != 1 || !ibReadPeriodUnit(arg.m_path.front(), unit))
		ThrowQueryException(arg.m_line, arg.m_col, wxString::Format(
			_("'%s' is not a period. %s takes one of: Second, Minute, Hour, Day, Week, Month, Quarter, HalfYear, Year"),
			arg.m_path.empty() ? wxString() : arg.m_path.front(), what));
	// ⚠ TENDAYS IS A BUCKET, NOT A LENGTH — its third one runs to the end of the month, so "one
	// ten-day later" names nothing. Refused HERE, once, rather than left to each dialect to be
	// missing from: a unit the engine silently substituted would be a wrong date that still runs.
	if (unit == ibTotalsPeriod::TenDays)
		ThrowQueryException(arg.m_line, arg.m_col, wxString::Format(
			_("TenDays cannot be moved by or counted in: its last bucket is not ten days long. %s takes a unit of fixed meaning"), what));
	return unit;
}

// The same word read as a PART of a date — `YEAR(x)` and its nine companions. A separate mapping
// from the unit above for the reason the two enums are separate: truncating to a month answers with
// a date, taking the month answers with 9.
bool ReadDatePartOf(ibQueryScalarFn fn, ibDatePart& part)
{
	switch (fn) {
	case ibQueryScalarFn::Year:      part = ibDatePart::Year;      return true;
	case ibQueryScalarFn::Quarter:   part = ibDatePart::Quarter;   return true;
	case ibQueryScalarFn::Month:     part = ibDatePart::Month;     return true;
	case ibQueryScalarFn::DayOfYear: part = ibDatePart::DayOfYear; return true;
	case ibQueryScalarFn::Day:       part = ibDatePart::Day;       return true;
	case ibQueryScalarFn::Week:      part = ibDatePart::Week;      return true;
	case ibQueryScalarFn::WeekDay:   part = ibDatePart::WeekDay;   return true;
	case ibQueryScalarFn::Hour:      part = ibDatePart::Hour;      return true;
	case ibQueryScalarFn::Minute:    part = ibDatePart::Minute;    return true;
	case ibQueryScalarFn::Second:    part = ibDatePart::Second;    return true;
	default:                                                       return false;
	}
}

ibQueryColumnExprPtr BuildScalarCallFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	const wxString word = ibQueryScalarFnText(e.m_scalar);
	auto arg = [&](size_t i) {
		return BuildColumnExprFromAst(sources, *e.m_args[i], params);
	};

	ibDatePart part = ibDatePart::Year;
	if (ReadDatePartOf(e.m_scalar, part))
		return ibQueryColumnExpr::DatePart(arg(0), part);

	switch (e.m_scalar) {
	case ibQueryScalarFn::BeginOfPeriod:
		return ibQueryColumnExpr::PeriodTrunc(arg(0), ReadUnitArgument(e, 1, word));
	case ibQueryScalarFn::EndOfPeriod:
		return ibQueryColumnExpr::PeriodEnd(arg(0), ReadUnitArgument(e, 1, word));
	case ibQueryScalarFn::DateAdd:
		// DATEADD(<date>, <unit>, <count>) — the unit is named SECOND, as a person says it: "add to
		// this date, in months, three".
		return ibQueryColumnExpr::DateAdd(arg(0), ReadUnitArgument(e, 1, word), arg(2));
	case ibQueryScalarFn::DateDiff:
		// DATEDIFF(<from>, <to>, <unit>) — the unit is LAST here, and that difference is exactly why
		// the position is asked of the table rather than assumed.
		return ibQueryColumnExpr::DateDiff(arg(0), arg(1), ReadUnitArgument(e, 2, word));
	case ibQueryScalarFn::Substring:
		return ibQueryColumnExpr::Substring(arg(0), arg(1), arg(2));

	// ⭐⭐ THE THREE QUESTIONS PUT TO THE VALUE — one node, three askings.
	//
	// `PRESENTATION(x)` is what the METATYPE says the value shows, and it says it off the LOADED
	// object (GenerateDataDesc). There is no set of "presentation fields" in the metadata to project
	// and no SQL that could assemble one — which is why this is not a gap in the dialects but a
	// question that belongs on our side of the wire. `VALUETYPE(x)` is the same shape: the type is
	// carried by the value, not stored beside it.
	//
	// So the input column is projected as usual and the question is put to the value that came back
	// (ibDataQueryResult::SetComputedOverRow). `REFPRESENTATION` is the reference-only spelling of
	// the first — one answer, because a value that is not a reference shows itself either way.
	case ibQueryScalarFn::Presentation:
		return ibQueryColumnExpr::ValueAsk(arg(0), ibQueryValueAsk::Presentation);
	case ibQueryScalarFn::RefPresentation:
		return ibQueryColumnExpr::ValueAsk(arg(0), ibQueryValueAsk::RefPresentation);
	case ibQueryScalarFn::ValueType:
		return ibQueryColumnExpr::ValueAsk(arg(0), ibQueryValueAsk::ValueType);

	// ⭐ DATETIME(y, m, d[, h, mi, s]) IS A LITERAL, not a call: every argument is a constant by
	// construction, so it is folded HERE and travels as the date it denotes. An engine asked to build
	// a date out of six numbers per row would be doing work for a value that never varies.
	case ibQueryScalarFn::DateTime: {
		int parts[6] = { 0, 1, 1, 0, 0, 0 };
		for (size_t i = 0; i < e.m_args.size() && i < 6; ++i) {
			const ibQueryAstExpr& a = *e.m_args[i];
			const ibValue v = (a.m_kind == ibQueryAstExprKind::Literal || a.m_kind == ibQueryAstExprKind::Param
			                || a.m_kind == ibQueryAstExprKind::Value)
				? EvalValue(a, params) : ibValue();
			if (v.IsEmpty())
				ThrowQueryException(a.m_line, a.m_col,
					_("DATETIME builds a date out of CONSTANTS: give it numbers or parameters, not fields"));
			parts[i] = static_cast<int>(v.GetNumber().ToInt());
		}
		wxDateTime moment(static_cast<wxDateTime::wxDateTime_t>(parts[2] > 0 ? parts[2] : 1),
		                  static_cast<wxDateTime::Month>((parts[1] > 0 ? parts[1] : 1) - 1),
		                  parts[0],
		                  static_cast<wxDateTime::wxDateTime_t>(parts[3]),
		                  static_cast<wxDateTime::wxDateTime_t>(parts[4]),
		                  static_cast<wxDateTime::wxDateTime_t>(parts[5]));
		if (!moment.IsValid())
			ThrowQueryException(e.m_line, e.m_col, _("DATETIME was given a date that does not exist"));
		return ibQueryColumnExpr::Const(ibValue(moment));
	}

	default:
		// The type tests, PRESENTATION and GROUPING are answered elsewhere (or not yet at all) — say
		// which call it was rather than "unsupported expression", so the reader knows what to look up.
		ThrowQueryException(e.m_line, e.m_col, wxString::Format(
			_("%s cannot be used as a computed value here"), word));
		return nullptr;
	}
}

ibQueryColumnExprPtr BuildColumnExprFromAst(const std::vector<ibSourceBinding>& sources,
                                            const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	switch (e.m_kind) {
	case ibQueryAstExprKind::Column: {
		const std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, e);
		if (cols.size() != 1)
			ThrowQueryException(e.m_line, e.m_col, _("a computed expression takes plain columns (no dot-walk)"));
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

	// ⭐ THE SCALAR CALLS — read here rather than in the parser, because THIS is where a word can be
	// checked against the configuration and refused with the position it was written at. The unit of
	// a period call arrives as an ordinary name (`Month`), and it is looked up in the SAME vocabulary
	// the virtual tables read their periodicity from: one list of calendar words for the language,
	// not one per place that needed one.
	case ibQueryAstExprKind::ScalarCall:
		return BuildScalarCallFromAst(sources, e, params);

	// ⭐⭐ A FOLD INSIDE AN EXPRESSION — declared as the aggregate it is, and referred to by name.
	//
	// `ISNULL(MIN(B.Period), &Till)`, `DATEDIFF(a, MIN(b), Day)`, `MAX(x) * 2`: the call folds rows,
	// so it cannot be evaluated per row like everything else in this builder. It is REGISTERED
	// instead — the same `Aggregate` verb a bare `MIN(x) AS m` uses — and what stands in the tree is
	// a reference to the name it will be published under. The expression is then evaluated after the
	// fold, over the group row. Nothing about folding changes; the expression simply reads its result.
	//
	// Only where a sink is armed (a projection that has somewhere to attach the finished expression).
	// Elsewhere this falls through and is refused in the words it always was.
	case ibQueryAstExprKind::Func:
		if (t_aggregateSink != nullptr && t_aggregateSink->m_builder != nullptr
		    && ibIsAggregateKeyword(e.m_func) && !e.m_over) {
			const wxString aggAlias = wxString::Format(wxT("_agg%d"), (*t_aggregateSink->m_seq)++);
			ibDataQueryBuilder& ab = *t_aggregateSink->m_builder;
			if (e.m_star)
				ab.Aggregate(AggFn(e.m_func), (const ibBackendQueryColumn*)nullptr, aggAlias, e.m_distinctArg);
			else if (e.m_arg && IsComputedExprAst(*e.m_arg))
				ab.Aggregate(AggFn(e.m_func), BuildColumnExprFromAst(sources, *e.m_arg, params), aggAlias, e.m_distinctArg);
			else if (e.m_arg && e.m_arg->m_kind != ibQueryAstExprKind::Column)
				ab.Aggregate(AggFn(e.m_func), BuildColumnExprFromAst(sources, *e.m_arg, params), aggAlias, e.m_distinctArg);
			else {
				const std::vector<const ibBackendQueryColumn*> argCols = ResolvePath(sources, *e.m_arg);
				if (argCols.empty())
					ThrowQueryException(e.m_line, e.m_col,
						_("%s() has nothing to fold - that argument resolves to no column of the "
						  "tables this query reads."), e.m_func);
				ab.Aggregate(AggFn(e.m_func), argCols, aggAlias, e.m_distinctArg);
			}
			return ibQueryColumnExpr::OutputRef(aggAlias);
		}
		ThrowQueryException(e.m_line, e.m_col, _("unsupported expression in a computed column"));
		return nullptr;

	default:
		ThrowQueryException(e.m_line, e.m_col, _("unsupported expression in a computed column"));
		return nullptr;
	}
}

// A synthetic scalar column the totals lowering builds: it reads a value the door projected under a
// distinct cursor alias (a computed measure `1 AS test`; a dot-walk dimension's leaf `Parent.Code`),
// STRAIGHT off the cursor by that alias, under a UNIQUE synthetic model id. This is what lets the
// metaID-keyed totals fold read it as a normal column — AND keeps a self-referential dimension
// (Parent.Code, whose leaf shares a metaID with the row's own attribute) from clashing with the main
// table. The column stamps its own kind onto the ordinal it is handed, which is what makes the id
// negative and distinct from every other minted one (ibBackendQueryColumn::SyntheticId).
// How far apart two NAMED QUERIES' column blocks stand (DeclareNamedResultAsCte). Wide enough that
// a named query's whole output fits inside its own block; the blocks are numbered from the RUN's
// source count, so they restart with every execution instead of climbing forever.
const ibMetaID kCteColumnStride = 4096;

class ibSyntheticScalarColumn : public ibBackendColumnRawDB
{
public:
	// Takes the ordinary running number and stamps its own kind on it inside, like every synthetic
	// column here — the caller hands a plain ordinal and knows nothing about the layout.
	ibSyntheticScalarColumn(const wxString& alias, ibMetaID id, RawType type = RawType::Number)
		: ibBackendColumnRawDB(alias, type), m_id(SyntheticId(SyntheticKind::Output, id)) {}
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
	// Takes the ORDINARY id the door minted and stamps its own kind on it inside — exactly the way a
	// clsid separates a reference from an object from a manager (clsid.h). The caller hands a plain
	// number and knows nothing about the layout; the class is what knows which kind it is.
	ibSyntheticOutputColumn(const wxString& name, const ibTypeDescription& type, ibMetaID id)
		: m_name(name), m_type(type), m_id(SyntheticId(SyntheticKind::Output, id)) {}

	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }   // the interface hands back a non-const ref
	ibMetaID           GetColumnId()     const override { return m_id; }
	// COMPUTED: it exists in the result and nowhere else — minted for an output that has no column
	// behind it, and read back by its alias.
	Kind               GetColumnKind()   const override { return Kind::Computed; }

private:
	wxString                  m_name;
	mutable ibTypeDescription m_type;   // mutable: GetTypeDesc() is const and returns a non-const ref
	ibMetaID                  m_id;
};

// ⭐ WHAT A COMPUTED OUTPUT HOLDS — the type an expression ANSWERS WITH.
//
// A column brings its type with it; an expression has to be asked. Nobody was asking, so every
// computed output went out untyped, and untyped is not a small gap: the CTE publishes this type to
// whoever selects from it, and the value codec has nothing else to read the field back BY — a
// computed output is one field with no `_TYPE` beside it, so an untyped one degrades to an empty
// cell (Max, 2026-08-24: a constant is a legitimate grouping key, and so is a CASE).
//
// The rule is one line long: ANSWER ONLY WHERE THE ANSWER IS CERTAIN. A type that is merely
// plausible is worse than none — the empty description keeps the old road, a wrong one is read as
// a value nobody vouched for. So mixed arithmetic, disagreeing CASE arms and everything not listed
// return {} on purpose.
static ibTypeDescription TypeOfExpr(const std::vector<ibSourceBinding>& sources,
	const ibQueryAstExpr& e, const std::map<wxString, ibValue>& params)
{
	const ibTypeDescription number(g_valueNumberCLSID);

	switch (e.m_kind) {
	case ibQueryAstExprKind::Literal:
	case ibQueryAstExprKind::Value:
	case ibQueryAstExprKind::Param: {
		// Resolved right here — the value knows what it is (a number, a string, an empty reference),
		// because the lexer typed it on the way in (SetNumber / SetString / SetDate, queryLexer.cpp).
		// UNDEFINED is not a type, though: it is what a value holds when nobody put anything in it, so
		// it answers "no single type" rather than naming one — otherwise the codec would confidently
		// read a field as the empty tag.
		const ibValue value = EvalValue(e, params);
		return value.GetType() == ibValueTypes::TYPE_EMPTY
		     ? ibTypeDescription() : ibTypeDescription(value.GetClassType());
	}

	case ibQueryAstExprKind::Column: {
		// The LEAF of the path is what the output holds — the same answer the projection branches give.
		const std::vector<const ibBackendQueryColumn*> cols = ResolvePath(sources, e);
		return cols.empty() ? ibTypeDescription() : cols.back()->GetTypeDesc();
	}

	case ibQueryAstExprKind::Arith: {
		if (!e.m_lhs || !e.m_rhs)
			return ibTypeDescription();
		const ibTypeDescription l = TypeOfExpr(sources, *e.m_lhs, params);
		const ibTypeDescription r = TypeOfExpr(sources, *e.m_rhs, params);
		const bool lNum = l.GetClsidCount() == 1 && l.ContainType(ibValueTypes::TYPE_NUMBER);
		const bool rNum = r.GetClsidCount() == 1 && r.ContainType(ibValueTypes::TYPE_NUMBER);
		if (lNum && rNum)
			return number;
		// A date SHIFTED by a number is still a date; a date TIMES anything is not a date, and two
		// dates subtracted are not one either — neither is claimed here.
		const bool shift = e.m_arith == ibQueryArithOp::Add || e.m_arith == ibQueryArithOp::Sub;
		if (shift && l.GetClsidCount() == 1 && l.ContainType(ibValueTypes::TYPE_DATE) && rNum)
			return l;
		return ibTypeDescription();
	}

	case ibQueryAstExprKind::Case: {
		// The arms ARE the output. They agree or there is no single answer — a CASE that yields a
		// number on one branch and a string on another is a column of two types, which is a
		// composite, which this is not.
		ibTypeDescription agreed;
		bool first = true;
		auto fold = [&](const ibQueryAstExprPtr& arm) {
			if (!arm)
				return;
			const ibTypeDescription t = TypeOfExpr(sources, *arm, params);
			if (first) { agreed = t; first = false; }
			else if (agreed.GetClsidList() != t.GetClsidList()) { agreed = ibTypeDescription(); }
		};
		for (const std::pair<ibQueryAstExprPtr, ibQueryAstExprPtr>& c : e.m_cases)
			fold(c.second);
		fold(e.m_else);
		return agreed;
	}

	case ibQueryAstExprKind::Func:
		// COUNT is a count; SUM / AVG fold numbers into a number. MIN / MAX yield one of the values
		// they compared, so they answer with the argument's own type.
		switch (e.m_func) {
		case ibQueryKeyword::Count:
		case ibQueryKeyword::Sum:
		case ibQueryKeyword::Avg:
			return number;
		case ibQueryKeyword::Min:
		case ibQueryKeyword::Max:
			return e.m_arg ? TypeOfExpr(sources, *e.m_arg, params) : ibTypeDescription();
		default:
			return ibTypeDescription();
		}

	default:
		return ibTypeDescription();
	}
}

// ⭐⭐ THE SCHEMA OUTLIVES THE DOOR, SO IT MAY NOT POINT INTO IT.
//
// A subquery wrapper is built for ONE run and owns the columns it publishes; `subOwners` releases
// them the moment the lowering returns. The OUTPUT SCHEMA, by contrast, travels out with the
// selection and is read long afterwards — a script reading a field, a watch window in the debugger
// expanding the selection minutes later.
//
// It survived only while a nested table published its SOURCE's columns, which the metadata owns and
// which outlive everything. The day it began publishing its own output honestly — an alias, a
// dot-walk, a computed expression, all allocated by the wrapper — every such schema entry became a
// pointer into memory the allocator had already handed to somebody else. That is not a rare race:
// the first read after the query returns is already too late.
//
// Nothing the schema does needs the door. An entry needs an ID to find its cell, a NAME, and a TYPE
// — so it takes a snapshot of exactly those three, owned by the schema itself, through the same
// `m_ownedCol` a synthetic totals measure already uses. Where the column belongs to the METADATA
// nothing is copied: it outlives the schema by construction.
void DetachSchemaFromRunSources(std::vector<OutputColumn>& schema, const ibSubqueryOwner& owner)
{
	if (owner.empty())
		return;
	for (OutputColumn& oc : schema) {
		if (oc.m_col == nullptr || oc.m_ownedCol != nullptr)
			continue;
		// ⭐ SHARE THE STORAGE, DO NOT COPY THE COLUMN (Max, 2026-08-19: "the two of them own it —
		// the query and the result; the query dies, the result still holds the skeleton"). The
		// wrapper already keeps its published columns in shared_ptr, so keeping one alive is a
		// refcount, not a snapshot: the schema then names the SAME column the run named — same
		// identity, same id, same type object — and nothing can drift between the two.
		//
		// (It used to mint an ibSyntheticOutputColumn copy. That kept the schema readable, but a
		// copy answers only the three questions it copied: anything the real column knows, and
		// anything asked through pointer identity, quietly stopped matching.)
		std::shared_ptr<ibBackendQueryColumn> shared;
		for (const std::shared_ptr<const ibBackendQueryable>& source : owner) {
			if (source == nullptr)
				continue;
			shared = source->ShareColumn(oc.m_col);   // asked of the SOURCE — a metadata one answers nothing
			if (shared)
				break;
		}
		if (!shared)
			continue;   // metadata-owned — outlives everything, nothing to keep
		oc.m_ownedCol = shared;
	}
}

// (The RESULT needs no pass of its own: a source built for the query reaches the builder as an
// owning handle — From / Join / Union take a shared_ptr — and the builder hands that ownership to
// the result when it stamps it. What the result names, the result keeps alive.)

// The raw read-type of a PLAIN SCALAR column (string / number / date / bool, single CLSID). Returns false
// for a reference / enum / composite leaf — those are not single-field scalars and cannot ride a synthetic
// raw column (a multi-type totals dimension is a separate feature).
bool ScalarRawType(const ibBackendQueryColumn* col, ibBackendColumnRawDB::RawType& out)
{
	const ibTypeDescription& td = col->GetTypeDesc();
	if (td.GetClsidCount() != 1) return false;
	if      (td.ContainType(ibValueTypes::TYPE_STRING))  out = ibBackendColumnRawDB::RawType::String;
	else if (td.ContainType(ibValueTypes::TYPE_NUMBER))  out = ibBackendColumnRawDB::RawType::Number;
	else if (td.ContainType(ibValueTypes::TYPE_DATE))    out = ibBackendColumnRawDB::RawType::Date;
	else if (td.ContainType(ibValueTypes::TYPE_BOOLEAN)) out = ibBackendColumnRawDB::RawType::Boolean;
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
                     std::vector<OutputColumn>& outSchema, bool asSubquery,
                     const std::vector<ibQueryAstExprPtr>& sourceConditions = std::vector<ibQueryAstExprPtr>(),
                     // ⭐ …AND THE OUTPUTS THIS QUERY CANNOT ASK AN ENGINE FOR — an expression standing
                     // OVER a fold. Filled here, attached to the RESULT by the caller
                     // (ibDataQueryResult::SetComputedOverRow). Null = the caller has no result to
                     // attach them to, and such a projection is refused as before.
                     std::vector<ibQueryColumnSelect>* outComputedOverRow = nullptr);

// Resolve a FROM / source to a queryable: a plain metaobject source via the factory, or a nested
// SELECT wrapped in ibSubqueryQueryable (built recursively, its own FROM resolved the same way).
// QUALIFY A CONDITION WRITTEN INSIDE A SOURCE CALL. Its identifiers name THAT source's own columns
// — `Balance(&P, Warehouse = &W)` means the balance's Warehouse — but once the condition joins the
// query's WHERE it stands among every other source, so each path gets the source's name in front.
// Works on a CLONE: the author's AST keeps what they wrote, and the text goes on rendering it inside
// the brackets where they put it.
ibQueryAstExprPtr QualifyToSource(const ibQueryAstExprPtr& expr, const wxString& sourceName)
{
	if (!expr)
		return nullptr;

	// COPIES AS IT QUALIFIES — one pass, not a clone followed by a rewrite. The author's AST is left
	// exactly as written (the text goes on rendering the condition inside the brackets), and nothing
	// here duplicates the optimizer's cloner: this builds the expression it needs.
	ibQueryAstExprPtr copy = std::make_shared<ibQueryAstExpr>(*expr);

	// Only a Column's path names a source; a Cast's path is a TYPE and a Value's a meta-path, so
	// both keep theirs untouched and are followed through their argument instead.
	if (copy->m_kind == ibQueryAstExprKind::Column && !copy->m_path.empty()
	    && !sourceName.IsEmpty() && !copy->m_path.front().IsSameAs(sourceName, false))
		copy->m_path.insert(copy->m_path.begin(), sourceName);

	copy->m_arg  = QualifyToSource(expr->m_arg,  sourceName);
	copy->m_lhs  = QualifyToSource(expr->m_lhs,  sourceName);
	copy->m_rhs  = QualifyToSource(expr->m_rhs,  sourceName);
	copy->m_low  = QualifyToSource(expr->m_low,  sourceName);
	copy->m_high = QualifyToSource(expr->m_high, sourceName);
	copy->m_else = QualifyToSource(expr->m_else, sourceName);

	copy->m_list.clear();
	for (const ibQueryAstExprPtr& item : expr->m_list)
		copy->m_list.push_back(QualifyToSource(item, sourceName));

	copy->m_cases.clear();
	for (const auto& branch : expr->m_cases)
		copy->m_cases.emplace_back(QualifyToSource(branch.first, sourceName),
		                           QualifyToSource(branch.second, sourceName));
	return copy;
}

// The source as an OWNING handle. A metaobject source is owned by the metadata and outlives every
// query, so it comes back as a non-owning share (aliasing ctor: a pointer with no control block) —
// the caller passes both kinds to From / Join the same way, and only the ones that CAN die carry a
// refcount. (Max, 2026-08-19: "safer to run everything through shared_ptr and not keep track of
// which raw pointer is in what state".)
// ⭐⭐ A NAMED RESULT AS A CTE — declared on the reader's own door and read by NAME.
//
// The inner query is built exactly as a nested source is (the same PopulateBuilder over the same
// kind of door); what differs is where it ends up: `.With(name, inner)` writes it into the reader's
// statement as `WITH <name> AS (…)`, and the source becomes an ibCteQueryable — a NAME the server
// resolves. The rows never come back to us, and the join is the DBMS's.
//
// Returns null when this select cannot travel that way, and the caller then takes the old road (a
// nested source, materialised in RAM). Nothing here refuses a query: it either has a server-side
// form or it does not.
std::shared_ptr<const ibBackendQueryable> DeclareNamedResultAsCte(ibDataQueryBuilder& outer,
	const wxString& name, const ibQuerySelect& sel,
	const std::map<wxString, ibValue>& params, ibSubqueryOwner& owner);

// FROM + every JOIN into one door and one set of bindings — defined below, beside the read that
// first needed it. A DECLARED query opens its sources exactly the same way (see
// DeclareNamedResultAsCte): "which tables does this select read" has one answer, wherever the
// select is going to be written.
void BuildSourceTree(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                     ibSubqueryOwner& owner, std::vector<ibSourceBinding>& sources, ibDataQueryBuilder& b,
                     std::vector<ibQueryAstExprPtr>* sourceConditions);

std::shared_ptr<const ibBackendQueryable> ResolveFrom(const ibQuerySource& src,
                                      const std::map<wxString, ibValue>& params,
                                      ibSubqueryOwner& owner,
                                      std::vector<ibQueryAstExprPtr>* conditionsOut = nullptr,
                                      ibDataQueryBuilder* declareOn = nullptr)
{
	// ⭐ A BARE NAME MAY BE A RESULT AN EARLIER STATEMENT NAMED — a query result link. Two roads lead
	// from here and the choice is made ONCE, here: declare it to the server (`WITH`) when the engine
	// can read a named query and the reader has a door to declare it on, otherwise take its rows as
	// a nested source, which is what this has always done.
	if (!src.m_subquery && !src.m_parameter && src.m_name.size() == 1) {
		if (const ibQuerySelect* named = ibNamedResultScope::Find(src.m_name.front())) {
			if (declareOn != nullptr) {
				std::shared_ptr<const ibBackendQueryable> cte =
					DeclareNamedResultAsCte(*declareOn, src.m_name.front(), *named, params, owner);
				if (cte)
					return cte;
			}
			return WrapSelectAsQueryable(*named, params, owner);   // its ROWS, read here — the old road
		}
	}

	if (!src.m_subquery) {
		// A virtual table's CONDITION argument comes back here rather than being evaluated: it is a
		// predicate over the table's own columns, and it is applied by ANDing it into the WHERE —
		// from where the ordinary machinery pushes plain conditions INTO ComputeRows, so a balance
		// filters before folding rather than after.
		std::vector<ibQueryAstExprPtr> own;
		const ibBackendQueryable* q = ResolveSource(src, params, &own);
		if (conditionsOut != nullptr)
			for (const ibQueryAstExprPtr& condition : own)
				if (ibQueryAstExprPtr qualified = QualifyToSource(condition, ibQuerySourceName(src)))
					conditionsOut->push_back(qualified);
		// Non-owning share: the metadata keeps this source, nobody else may release it.
		return std::shared_ptr<const ibBackendQueryable>(std::shared_ptr<void>(), q);
	}
	// ⭐⭐ AND AN ANONYMOUS NESTED SOURCE TAKES THE VERY SAME ROAD — `FROM (SELECT …) AS s` declared to
	// the server as `WITH <name> AS (…)` and read by that name.
	//
	// It is the SAME question the named result answered above ("can the server read this itself?"),
	// asked about a query that simply never got a name — so it is given one. That matters far beyond
	// a hand-written subquery: a COMPOSITION always renders its source as a nested query
	// (`SELECT … FROM (<the author's query>) AS AuthorQuery`, dataComposer.cpp), and a nested source
	// is computed in RAM BY CONSTRUCTION (ibSubqueryQueryable::IsComputedInRam) — which is why no
	// report could fold its totals server-side at any setting, on any engine, whatever its shape.
	// Not a refusal anybody wrote: the road simply ended before the gate was ever reached.
	//
	// The name is SYNTHETIC rather than the source's alias: the reader still writes its own
	// (`FROM q_sub0 AS AuthorQuery`), so nothing about how the outer query names its columns moves.
	// Numbered off `owner`, which counts THIS run's sources and resets with it — both roads out of
	// here push into it, so no two subqueries of one run can be handed the same name.
	if (declareOn != nullptr) {
		const wxString name = wxString::Format(wxT("q_sub%d"), static_cast<int>(owner.size()));
		if (std::shared_ptr<const ibBackendQueryable> cte =
		        DeclareNamedResultAsCte(*declareOn, name, *src.m_subquery, params, owner))
			return cte;
	}
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
                     std::vector<OutputColumn>& outSchema, bool asSubquery,
                     const std::vector<ibQueryAstExprPtr>& sourceConditions,
                     std::vector<ibQueryColumnSelect>* outComputedOverRow)
{
	const bool multiSource      = sources.size() > 1;
	const bool explicitProjection = asSubquery || multiSource;
	// A COMPUTED primary (subquery / virtual table) materialises in RAM — reference dot-walk
	// joins and dot-walk aggregate inputs have no DB join to ride there; reject rather than
	// push a path leaf as a plain column (silently wrong rows).
	const bool computedPrimary  = sources.size() == 1 && sources[0].m_q != nullptr
	                              && sources[0].m_q->IsComputedInRam();
	std::map<wxString, const ibBackendQueryable*> dwJoined; int dwAliasSeq = 0;   // dot-walk join dedup (multi-source projection)

	// ⭐⭐ THE EXPANSION, ARMED FOR THE WHOLE LOWERING — one door, every clause (ExpandDotWalkHere).
	//
	// It sits here rather than beside the WHERE because the SELECT list, the GROUP BY and the sort all
	// walk the same paths and share one dedup map: a prefix joined for the projection must not be
	// joined again for the filter. Armed for a multi-source read of REAL sources; a computed primary
	// resolves its own walks in RAM, and arming it there would offer a second way to do what that road
	// already does.
	const ibDotWalkExpansion dotWalkExpansion{ &b, &dwJoined, &dwAliasSeq };
	const ibDotWalkExpansionScope dotWalkScope(
		(multiSource && !computedPrimary) ? &dotWalkExpansion : nullptr);

	int aggExprSeq = 0;   // names the folds registered from inside an expression (_agg0, _agg1, …)

	// ⭐ THE ONLY PLACE THAT HAS TO COUNT — so it counts for itself.
	//
	// Every other minted column takes its number from something it already has: a twin from the
	// column it stands for, a named query from this run's count of sources, the stitch and the folds
	// from the position they are already walking. Only an output with nothing behind it has no such
	// number, and one running ordinal over THIS query's outputs is the whole of what it needs — the
	// synthetic column stamps its own kind onto it, which is what makes it negative and distinct.
	ibMetaID outputOrdinal = 0;
	auto nextOutputId = [&outputOrdinal]() { return ++outputOrdinal; };

	// EVERY OUTPUT IS A COLUMN. Where a branch below found a real one (a plain read, or a non-scalar
	// dot-walk leaf reassembled by prefix) it stands; where the value is read BY ALIAS and nothing
	// backs it, one is minted — so the output has an identity and a type like any other column.
	auto giveIdentity = [&nextOutputId](OutputColumn& oc) {
		if (oc.m_col != nullptr)
			return;
		auto column = std::make_shared<ibSyntheticOutputColumn>(oc.m_name, oc.m_type, nextOutputId());
		oc.m_col      = column.get();
		oc.m_ownedCol = column;
	};

	// ⭐⭐ IS THIS A FOLDING QUERY — asked as "does anything fold rows", not as "is there a call".
	//
	// A WINDOWED call is a call and folds NOTHING: `SUM(x) OVER (…)` returns a value on every row,
	// which is the whole point of it. Read as "there is a Func", the question answered yes and sent
	// the statement to the aggregate terminal, where a window is not projected at all — so
	// `SELECT Period, SUM(Rate) OVER (ORDER BY Period)` came back with the right number of rows, the
	// plain column filled and the computed one BLANK, and no error anywhere (measured 2026-09-06).
	// A ranking call — `ROW_NUMBER()`, `RANK()` — is the same shape and was swept up the same way.
	//
	// The right question already exists and is documented as exactly this one
	// (ibQueryMentionsAggregate, "does this expression fold rows"). Asking it here is what keeps the
	// two from drifting — and it reads the whole TREE, so a fold buried inside an expression
	// (`ISNULL(SUM(x), 0)`) is seen, which the kind test on the root node could never be.
	bool aggregate = !ast.m_groupBy.empty();
	for (const ibQueryProjection& p : ast.m_projections)
		if (ibQueryMentionsAggregate(p.m_expr)) aggregate = true;

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

			// ⭐⭐ A WINDOWED CALL IS A PROJECTED EXPRESSION, NOT AN AGGREGATE. `SUM(x) OVER (…)` folds
			// nothing away — it returns a value on every row — so it goes into the selection as a
			// computed column, and the engine that computes it is the SERVER.
			//
			// This is where the refusal used to stand. It was honest while there was no road: an
			// aggregate travelled to the door as an ibAggregateItem, which had no window on it, and
			// dropping the OVER would have reported a plain total under the name of a running one.
			// The road exists now (ibQueryColumnExprKind::WindowAgg → ibRenderOverClause), so the
			// refusal narrows to the one case that still cannot be honoured: an engine without windows.
			if (e.m_kind == ibQueryAstExprKind::Func && (e.m_over || ibIsRankingKeyword(e.m_func))) {
				if (!b.CanPushWindow())
					ThrowQueryException(e.m_line, e.m_col,
						_("this database does not compute window functions - OVER (...) cannot be run here"));
				b.SelectExpr(BuildWindowExprFromAst(sources, e, params), alias);
				oc.m_alias = alias; oc.m_byAlias = true;   // read back by name — a computed column
				outSchema.push_back(oc);
				continue;
			}

			if (e.m_kind == ibQueryAstExprKind::Func) {
				RefuseUnloweredWindow(e);
				// Aggregate input: a plain column, a reference dot-walk leaf (SUM(Producer.Weight)), or a
				// COMPUTED expression (SUM(Qty * Price) — the provider lowers it; single DB source, gated).
				if (e.m_star) {
					b.Aggregate(AggFn(e.m_func), (const ibBackendQueryColumn*)nullptr, alias, e.m_distinctArg);
				}
				else if (e.m_arg && IsComputedExprAst(*e.m_arg)) {
					GateComputedExpr(sources, *e.m_arg);
					b.Aggregate(AggFn(e.m_func), BuildColumnExprFromAst(sources, *e.m_arg, params), alias, e.m_distinctArg);
				}
				// ⭐⭐ AND AN AGGREGATE OVER A CONSTANT IS ORDINARY. `SUM(1)`, `MAX(&Limit)`,
				// `MIN(VALUE(…))` — the argument is a value, not a column, and folding it is perfectly
				// legitimate: SUM(1) counts the rows of each group, MAX of a constant is that constant.
				// The expression builder already speaks literals, parameters, VALUE(), arithmetic and
				// CASE, so this is one more way in rather than a new mechanism - and no source gate,
				// because a constant reads from nothing.
				//
				// 🛑 IT USED TO REACH THE COLUMN PATH AND RESOLVE TO NO COLUMNS AT ALL, and the empty
				// list travelled into the builder, which indexes it: a vector subscript out of range -
				// an assert in Debug, undefined behaviour in Release. A hand-written query CRASHED THE
				// RUNNING APPLICATION (2026-09-03, `SELECT G.Description, SUM(1) FROM Catalog.Goods`),
				// and query_check passed it beforehand, because a name that is not there is not the
				// problem: nothing was checking the SHAPE.
				else if (e.m_arg && e.m_arg->m_kind != ibQueryAstExprKind::Column) {
					b.Aggregate(AggFn(e.m_func), BuildColumnExprFromAst(sources, *e.m_arg, params), alias, e.m_distinctArg);
				}
				else {
					const std::vector<const ibBackendQueryColumn*> argCols = ResolvePath(sources, *e.m_arg);

					// ⚠ AND A COLUMN THAT RESOLVED TO NOTHING IS STILL REFUSED, not passed on: the
					// builder indexes what it is given, so an empty list is a crash rather than an
					// answer. The constant case is handled above; anything else that gets here is a
					// path the sources do not offer.
					if (argCols.empty())
						ThrowQueryException(e.m_line, e.m_col,
							_("%s() has nothing to fold - that argument resolves to no column of the "
							  "tables this query reads."), e.m_func);
					// A dot-walk aggregate input over a COMPUTED source is resolved in RAM (the provider LEFT-joins
					// the reference leaf via ResolveComputedDotWalks, then aggregates it); a JOIN expands SQL leaves.
					if (argCols.size() > 1 && multiSource) {
						// dot-walk aggregate input over a JOIN — expand the ref path into LEFT-join leaves and
						// aggregate the qualified leaf (same mechanism as the TOTALS dimension / projection).
						const ibBackendQueryColumn* dwLeaf =
							ExpandDotWalkHere(sources, argCols, *e.m_arg);
						b.Aggregate(AggFn(e.m_func), dwLeaf, alias, e.m_distinctArg);
					}
					else
						b.Aggregate(AggFn(e.m_func), argCols, alias, e.m_distinctArg);
				}
				oc.m_type = TypeOfExpr(sources, e, params);   // a fold answers too: COUNT/SUM/AVG a number, MIN/MAX the argument's own
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
						? ExpandDotWalkHere(sources, pathCols, e)   // JOIN -> expand the ref path, through the one door
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

					// 🛑 …BUT AN ALIAS IS NOT HOW A METADATA COLUMN IS READ. Only a RAW column is one
					// projected field that a name can fetch; everything else is a spread of physical
					// fields, and the provider projects it under ITS OWN names — so a by-alias read
					// finds nothing and hands back the type's default (an empty date, False, an empty
					// string), which is indistinguishable from a table full of blank rows. That is
					// exactly how a report over `(SELECT Document1.Ref, …)` came out empty.
					//
					// ⭐ THE TEST IS THE COLUMN'S OWN (Max: "there are not just those four types —
					// there's a unique identifier, there can be anything"): ask IsRawColumn, the same
					// question the co-located join asks when it plans a projection, instead of listing
					// type names that would be wrong the day a type is added.
					//
					// A single source needs no alias to tell columns apart, so the column IS the read —
					// the very path the non-subquery branch above takes, and the one that works.
					if (!pathCols[0]->IsRawColumn()) {
						if (multiSource) {
							// Several sources: the alias is what tells two same-named columns apart, so the
							// spread is read back under it (the dot-walk branch below does the same).
							oc.m_objectPrefix = alias;
							oc.m_col          = pathCols[0];
						}
						else {
							// One source: nothing to disambiguate, so the COLUMN is the read — the same
							// path the non-subquery branch above takes, and the one that works.
							oc.m_col     = pathCols[0];
							oc.m_alias   = wxString();
							oc.m_byAlias = false;
						}
					}
				}
				else if (multiSource) {
					// MULTI-SOURCE dot-walk projection — `SelectPath` is the single-source door's join; the RAM
					// stitch has none. Expand the path into explicit LEFT-join leaves (ExpandDotWalkJoins) and
					// project the qualified leaf column, read back by alias (a scalar value or a whole reference
					// cell). Paths sharing a prefix reuse one join via dwJoined.
					const ibBackendQueryColumn* dwLeaf =
						ExpandDotWalkHere(sources, pathCols, e);
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
			// A SCALAR CALL IS A COMPUTED COLUMN, on exactly the terms arithmetic and CASE are: it answers
			// per row, it has no physical name to be read back by, and it carries its alias. Listed beside
			// them rather than given a branch of its own — one rule for "an expression in the selection",
			// so `YEAR(Date)` groups in an aggregate query the same way `a * b` does.
			else if (e.m_kind == ibQueryAstExprKind::Arith || e.m_kind == ibQueryAstExprKind::Case
			      || e.m_kind == ibQueryAstExprKind::ScalarCall) {
				// ⭐⭐ …UNLESS NO ENGINE CAN BE ASKED FOR IT. `PRESENTATION(x)` and `VALUETYPE(x)` are
				// questions put to the VALUE, not readings of a field, so there is nothing to render:
				// the INPUT is projected and the question is answered over the row that came back.
				//
				// Built first and then ASKED (ExprIsAnsweredHere), rather than switched on the written
				// word: `ISNULL(PRESENTATION(x), "-")` is our side too, and a test on the outer call
				// would have said otherwise. In an aggregate query the input is grouped by, exactly as a
				// plain projected column would be — the presentation of a key cannot vary inside its own
				// group, so this decides nothing that grouping by the column itself did not.
				// ⭐⭐ ONE BUILD, ONE ARMING — and the second of each is what was wrong here.
				//
				// A fold inside an expression is not evaluated in place: it is REGISTERED as the
				// aggregate it is and replaced by a reference to the name it will be published under
				// (ibAggregateSink, and the Func case in BuildColumnExprFromAst). That needs the sink
				// armed AT THE BUILD — and this expression was built twice: once here to ask it what
				// it is, once further down to hand it to the builder, with the sink armed only on the
				// second. So `MAX(x) * 2` met the builder unarmed on the FIRST build and was refused
				// as "unsupported", while the road that could have taken it was never reached.
				//
				// Built once, armed once, and every road below reads the SAME expression.
				const ibAggregateSink sink{ &b, &aggExprSeq };
				const ibAggregateSinkScope armed(&sink);
				const ibQueryColumnExprPtr built = BuildColumnExprFromAst(sources, e, params);
				if (built && ExprIsAnsweredHere(built.get())) {
					if (outComputedOverRow == nullptr)
						ThrowQueryException(e.m_line, e.m_col,
							_("this expression is answered over the finished row and cannot be read back here"));
					std::vector<const ibBackendQueryColumn*> inputs;
					GatherExprSourceColumns(built.get(), inputs);
					ibQueryColumnSelect ours;
					ours.m_expr  = built;
					ours.m_alias = alias;
					for (const ibBackendQueryColumn* in : inputs) {
						// ⭐⭐ IN A FOLD THE INPUT IS A GROUP KEY, and a key comes back under its OWN
						// identity — the column id it always had. So it is grouped by and remembered
						// AS ITSELF; giving it a name of ours here would be inventing a second name
						// for a thing that already has one, and the result has never heard of it.
						//
						// 🛑 That is exactly how it failed, and silently: `DATEDIFF(A.Period,
						// MIN(B.Period), Day)` was projected as `_vinN`, a fold's result carries no
						// such column, so A.Period read as an empty date — and the answer was 739635,
						// the distance from the date zero. A plausible figure and the wrong one
						// (measured 2026-09-06). The reader has always been able to read an input by
						// its column (ibResultRow::Get — "an input with no prefix"); this side was
						// the one naming it something the reader could not find.
						if (aggregate) {
							b.GroupBy(in);
							ours.m_inputs.push_back({ in, wxString(), false });
							continue;
						}
						// Projected so the value reaches us; NOT published — the schema below carries
						// only what the author asked to see. And REMEMBERED under the name it was
						// projected as, because that is how the result will hand it back.
						const wxString prefix = wxString::Format(wxT("_vin%d"), -nextOutputId());
						b.Select(in, prefix);
						// A REFERENCE / enum / composite comes back as a whole reassembled value from
						// its spread; a primitive as one field. Same split the ordinary projection makes.
						const ibTypeDescription& itd = in->GetTypeDesc();
						const bool plainScalar = itd.GetClsidCount() == 1
							&& (itd.ContainType(ibValueTypes::TYPE_NUMBER) || itd.ContainType(ibValueTypes::TYPE_STRING)
								|| itd.ContainType(ibValueTypes::TYPE_DATE) || itd.ContainType(ibValueTypes::TYPE_BOOLEAN));
						ours.m_inputs.push_back({ in, ibSqlAliasOf(prefix), !plainScalar });
					}
					outComputedOverRow->push_back(std::move(ours));
					oc.m_alias   = alias;
					oc.m_byAlias = true;
					// A PRESENTATION is a string and says so — the schema is read by whoever displays
					// this column, and an output that will not name its type reads as an empty cell.
					// The rest answer with what the expression answers (a type value, a fold's figure)
					// and say nothing they cannot vouch for.
					oc.m_type    = (built->m_kind == ibQueryColumnExprKind::ValueAsk
					                && built->m_valueAsk != ibQueryValueAsk::ValueType)
						? ibTypeDescription(g_valueStringCLSID)
						: TypeOfExpr(sources, e, params);
					giveIdentity(oc);
					outSchema.push_back(oc);
					continue;
				}
				// COMPUTED column (a * b, CASE …). The provider lowers the L3 expression tree + projects it
				// AS the alias: a single DB source projects it server-side, a JOIN / computed source
				// evaluates it per row in the composer (EvalColumnExprRow).
				if (aggregate) {
					// ⭐ IN AN AGGREGATE QUERY A COMPUTED PROJECTION IS A GROUP KEY — the very answer the plain
					// column above gets, reached through GroupByExpr because an expression has no physical name
					// to be read back by and must carry its alias. Both floors already honour a computed key:
					// the SQL path GROUPs BY the lowered tree and projects it (dbTableProvider), the RAM fold
					// evaluates it per row (RamAggregate) — this branch was the only one not using them.
					//
					// AND GROUPING BY IT DECIDES NOTHING. The expression is built from group keys, so inside a
					// group it cannot vary: adding it is "a spelling the server requires", exactly as the
					// dot-walk rule below says of `Producer.Region` beside `Producer`. One rule, now stated for
					// expressions too, instead of a refusal where the rule already had the answer.
					//
					// 🛑 `ISNULL(Balance, 0) AS Rest` beside `SUM(Qty) AS Needed` — ordinary SQL, written by a
					// stock control that has to read a missing row as zero — was refused outright, and the
					// posting that asked it failed with a sentence about the platform instead of an answer
					// (Max, 2026-09-03: "ISNULL is normal, the platform must support it").
					// ⭐⭐ AN EXPRESSION THAT MENTIONS A FOLD IS NOT A GROUP KEY — it is an output computed
					// AFTER the fold, and that is the whole difference. It does not need a branch of its
					// own: the fold was replaced at the build above by a reference to the name it is
					// published under, and `ExprIsAnsweredHere` says yes to exactly that — so it left on
					// the road above, alongside PRESENTATION, with its plain columns projected and
					// grouped by. Two roads for one question, and the second one was the poorer of the
					// two: it attached the expression with no inputs at all, so `DATEDIFF(A.Period,
					// MIN(B.Period), Day)` had nowhere to read A.Period from.
					//
					// 🛑 The shape used to be refused outright: `ISNULL(MIN(Period), &Till)` — a stock
					// statement's ordinary way of saying "and if there is no next one, use this" — came
					// back as a sentence about the platform (Max, 2026-09-05, the rate-duration scenario).
					b.GroupByExpr(built, alias);
				}
				else {
					// A computed column over a COMPUTED source evaluates in RAM (ibComputedProvider::ExecuteRead —
					// EvalColumnExprRow per row, projected under the alias).
					b.SelectExpr(built, alias);
				}
				oc.m_type = TypeOfExpr(sources, e, params);   // what the expression answers with — see TypeOfExpr
				oc.m_alias = alias;
				oc.m_byAlias = true;
			}
			else if (e.m_kind == ibQueryAstExprKind::Literal || e.m_kind == ibQueryAstExprKind::Value
			      || e.m_kind == ibQueryAstExprKind::Param) {
				// SELECT 2 AS x / SELECT value(<Kind>.<Name>.<Member>) [AS x] / SELECT &param [AS x] — project a
				// CONSTANT column (a plain literal / an empty ref / a predefined item / a bound &parameter value),
				// resolved now. Common to tag a UNION branch or seed a constant column.
				//
				// ⭐ A PLAIN LITERAL BELONGS HERE and was the odd one out: `2 AS iuytfds` is ordinary SQL, the
				// parser reads it, EvalValue has always answered it — and the projection alone refused, with
				// "unsupported projection expression" on a query that is not wrong (Max, 2026-08-24: "an error
				// that is not an error"). One kind was missing from a list of three that do the same thing.
				//
				// ⭐ AND IT CARRIES ITS TYPE. The value is resolved right here, so the output has no
				// excuse to be untyped: `15 AS x` IS a number and says so. Everything downstream reads
				// the schema — the CTE publishes this type to whoever selects from it, and the value
				// codec needs it to read the field back (a computed output has ONE field and no `_TYPE`
				// beside it, so the tag can only come from what the output IS). Left empty, the read
				// asks for a discriminator that was never projected and degrades to an empty cell —
				// silently, 496 times per report (Max, 2026-08-24).
				b.SelectExpr(ibQueryColumnExpr::Const(EvalValue(e, params)), alias);
				oc.m_type = TypeOfExpr(sources, e, params);
				oc.m_alias = alias;
				oc.m_byAlias = true;
			}
			else {
				ThrowQueryException(e.m_line, e.m_col, _("unsupported projection expression"));
			}
			giveIdentity(oc);
			outSchema.push_back(oc);
		}
	}

	// GROUP BY — plain column OR a reference dot-walk leaf (GROUP BY Producer.Region). The path overload
	// routes size-1 to a plain key; the provider joins a longer path (single source only — JOIN is A1).
	std::vector<std::vector<const ibBackendQueryColumn*>> groupKeys;   // resolved, for the walk rule below
	for (const ibQueryAstExprPtr& g : ast.m_groupBy) {
		// ⭐ A COMPUTED KEY IS GROUPED BY THE EXPRESSION, not by a column it does not have.
		// `GROUP BY MONTH(Period)` names no field — arithmetic, a CASE and a scalar call all answer
		// per row and have to be grouped as the expression they are (GroupByExpr, the door the
		// projection already uses for the same shape). Sent to ResolvePath instead, a call arrived
		// with an EMPTY path and the resolver indexed into it (Max, 2026-09-05: a debug break in
		// `ResolvePath`, `vector subscript out of range`) — so the guard is here, where the kind is
		// known, and ResolvePath below also refuses an empty path in words rather than dying on it.
		if (g && IsComputedExprAst(*g)) {
			// …and grouped ONCE. A projection that computes the same expression has already grouped by
			// it (the aggregate branch above sends a computed projection through GroupByExpr), so the
			// key is added here only when the author grouped by something they did not also select.
			// The two are compared as TEXT, through the renderer that writes this language — the same
			// answer the person sees, and cheaper than an equality over trees.
			const wxString written = ibRenderQueryExpr(*g);
			bool alreadyProjected = false;
			int keyIndex = 0;
			for (const ibQueryProjection& p : ast.m_projections) {
				++keyIndex;
				if (p.m_expr && IsComputedExprAst(*p.m_expr) && ibRenderQueryExpr(*p.m_expr) == written) {
					alreadyProjected = true;
					break;
				}
			}
			if (!alreadyProjected)
				b.GroupByExpr(BuildColumnExprFromAst(sources, *g, params),
					wxString::Format(wxT("gk%d"), keyIndex));
			groupKeys.push_back({});
			continue;
		}
		const std::vector<const ibBackendQueryColumn*> gcols = ResolvePath(sources, *g);
		// A dot-walk GROUP BY over a COMPUTED source is RAM-joined by the provider (ExecuteAggregate resolves
		// m_groupPaths); a JOIN expands SQL join leaves; a single physical source auto-joins the ref chain.
		if (gcols.size() > 1 && multiSource)
			b.GroupBy(ExpandDotWalkHere(sources, gcols, *g));   // JOIN -> expand the ref path, through the one door
		else
			b.GroupBy(gcols);
		groupKeys.push_back(gcols);
	}

	// ⭐ A WALK IS FIXED BY WHAT IT WALKS FROM. Group by `Producer` and `Producer.Region` comes with it:
	// one reference value per group, so one region — the field cannot vary inside the group, and making
	// the author list every child of a key he already grouped by is asking him to repeat what he said.
	//
	// SQL does not know that, and would refuse the projection ("must appear in the GROUP BY"). So the
	// key it needs is added HERE, where the fact is known. Grouping additionally by a value that cannot
	// vary within the group changes no result — it is a spelling the server requires, not a decision.
	if (aggregate && !ast.m_selectAll) {
		for (const ibQueryProjection& projection : ast.m_projections) {
			const ibQueryAstExpr* e = projection.m_expr.get();
			if (e == nullptr || e->m_kind != ibQueryAstExprKind::Column || e->m_path.size() < 2)
				continue;
			const std::vector<const ibBackendQueryColumn*> pcols = ResolvePath(sources, *e);
			bool extendsAKey = false;
			for (const std::vector<const ibBackendQueryColumn*>& key : groupKeys) {
				if (key.empty() || key.size() >= pcols.size())
					continue;   // an EXACT key is already grouped; a longer one is not this key's child
				bool prefix = true;
				for (size_t k = 0; k < key.size() && prefix; ++k)
					prefix = (key[k] == pcols[k]);
				if (prefix) { extendsAKey = true; break; }
			}
			if (!extendsAKey)
				continue;
			if (pcols.size() > 1 && multiSource)
				b.GroupBy(ExpandDotWalkHere(sources, pcols, *e));
			else
				b.GroupBy(pcols);
			groupKeys.push_back(pcols);   // a second projection walking the same leaf must not add it twice
		}
	}

	// HAVING — aggregate <op> value (ordered ops only; the door's filter-op set has no = / <>)
	// Over a COMPUTED source the aggregate folds in RAM, which does not apply HAVING — reject
	// rather than silently return unfiltered groups.
	if (ast.m_having && sources.size() == 1 && sources[0].m_q != nullptr && sources[0].m_q->IsComputedInRam())
		ThrowQueryException(ast.m_having->m_line, ast.m_having->m_col,
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
				ThrowQueryException(h.m_line, h.m_col, _("HAVING must compare an aggregate function to a value"));
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
			default: ThrowQueryException(h.m_line, h.m_col, _("HAVING compares an aggregate with =, <>, <, <=, > or >=")); break;
			}
			b.Having(AggFn(f.m_func), col, op, val);
		}
	}

	// Dot-walk in WHERE / ORDER is realizable only on a single-source, non-aggregate, PHYSICAL READ
	// (the provider's BuildPageIR builds the reference join + qualifies the leaf). An aggregate /
	// JOIN / computed-source query rejects it (a computed source has no DB join to ride).
	const bool allowDotWalk = !aggregate && !multiSource && !computedPrimary;

	// (The dot-walk expansion is armed at the top of this function — one door for every clause.)

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

	// A VIRTUAL TABLE'S CONDITION ARGUMENT lands here, alongside the written WHERE and by exactly the
	// same road. `Balance(&P, Warehouse = &W)` says "compute the balance over this slice", and the
	// slice IS a condition — so once it is qualified with its source's name (QualifyToSource) there
	// is nothing left to distinguish it from a condition the author typed in WHERE.
	//
	// That is the point: from here the ordinary machinery carries it, and plain conditions are pushed
	// INTO ComputeRows. The balance therefore filters BEFORE folding, which is what an argument of the
	// table means and what a WHERE over the finished result could not do.
	for (const ibQueryAstExprPtr& condition : sourceConditions) {
		if (!condition)
			continue;
		if (IsFlatAndWhere(*condition))
			LowerFlatWhere(b, sources, *condition, params, allowDotWalk || computedPrimary);
		else
			b.Where(BuildWherePredicate(sources, *condition, params, allowDotWalk || computedPrimary));
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
				ThrowQueryException(oe.m_line, oe.m_col, _("ORDER BY an expression over a computed source is not yet supported: sort by a column"));
			if (IsComputedExprAst(oe))
				GateComputedExpr(sources, oe);
			b.OrderByExpr(BuildColumnExprFromAst(sources, oe, params), o.m_ascending);
			continue;
		}
		// ⭐⭐ A PATH THAT WAS GROUPED BY CAN BE SORTED BY. The gate above forbids a reference walk in
		// an aggregate query because the join it needs is built only on the single-source read path
		// — true in general, and false for exactly this case: GROUP BY has ALREADY built that join
		// for this very path, so the leaf is there to sort on and the SQL is valid (the key is in
		// the GROUP BY, which is what the server asks of a sorted column).
		//
		// 🛑 `SELECT Ref.Number, COUNT(*) … GROUP BY Ref.Number ORDER BY Ref.Number` is how anybody
		// writes this, and it was refused while the same query without the ORDER worked (measured
		// 2026-09-02). The rule is not "no walking under an aggregate"; it is "no walking to a place
		// nothing grouped by".
		bool ridesAGroupKey = false;

		if (aggregate && !groupKeys.empty()
			&& oe.m_kind == ibQueryAstExprKind::Column && oe.m_path.size() > 1) {

			const std::vector<const ibBackendQueryColumn*> ocols = ResolvePath(sources, oe);

			for (const std::vector<const ibBackendQueryColumn*>& key : groupKeys) {

				if (key.empty() || key.size() > ocols.size())
					continue;

				// The same prefix rule the projection uses above: a key fixes everything it walks
				// INTO, so `Producer` as a key makes `Producer.Region` a sortable leaf as well.
				bool prefix = true;
				for (size_t k = 0; k < key.size() && prefix; ++k)
					prefix = (key[k] == ocols[k]);

				if (prefix) { ridesAGroupKey = true; break; }
			}
		}

		// …AND THE REFUSAL SAYS THE RULE THAT ACTUALLY APPLIES. The shared guard below answers "needs
		// a single, non-aggregate source", which is the truth about a WHERE and only half of it
		// here: an aggregate query CAN sort by a walked path, provided it grouped by that path.
		// Told at the callsite, because this is the only place that knows what was grouped.
		if (aggregate && !allowOrderDotWalk && !ridesAGroupKey
			&& oe.m_kind == ibQueryAstExprKind::Column && oe.m_path.size() > 1) {

			wxString path;
			for (const wxString& segment : oe.m_path)
				path += (path.IsEmpty() ? wxString() : wxT(".")) + segment;

			ThrowQueryException(oe.m_line, oe.m_col, wxString::Format(
				_("ORDER BY '%s' needs that path among the GROUP BY keys - a grouped query can only "
				  "sort by what it grouped by. Add it to GROUP BY, or sort by one of the keys."),
				path));
		}

		// ⭐⭐ SORTING BY A FOLD — `ORDER BY SUM(Qty) DESC`, or by the NAME that fold was given
		// (`SUM(Qty) AS Total … ORDER BY Total`). Both are the same request and both name something
		// that exists only after the grouping, so neither can be resolved as a column of a source:
		// the first came back "expected a column", the second "unknown attribute 'Total' on source
		// 'M'" — about the alias the query itself had just written (2026-09-04). A TOP-N report is
		// made of this sentence, so the language has to be able to say it.
		//
		// Matched against the OUTPUT, which is the only place an aggregate has a name: by the alias
		// when the author sorted by one, and by the rendered call when they repeated it.
		if (aggregate) {
			wxString folded;
			if (oe.m_kind == ibQueryAstExprKind::Column && oe.m_path.size() == 1) {
				// ⚠ ASKED OF THE PROJECTION, NOT OF THE SCHEMA. Every output carries a column by now
				// (an aggregate gets a synthetic one), so "has no source column" no longer tells a fold
				// from a field — what does is whether the projection this name belongs to FOLDS.
				for (const ibQueryProjection& p : ast.m_projections) {
					if (!p.m_expr || !ibQueryMentionsAggregate(p.m_expr))
						continue;
					const wxString name = p.m_alias.IsEmpty() ? ibQueryOutputName(p) : p.m_alias;
					if (name.IsSameAs(oe.m_path.front(), false)) { folded = name; break; }
				}
			}
			else if (ibQueryMentionsAggregate(o.m_expr)) {
				const wxString written = ibRenderQueryExpr(oe);
				for (const ibQueryProjection& p : ast.m_projections)
					if (p.m_expr && ibRenderQueryExpr(*p.m_expr).IsSameAs(written, false)) {
						folded = p.m_alias.IsEmpty() ? ibQueryOutputName(p) : p.m_alias;
						break;
					}
				if (folded.IsEmpty())
					ThrowQueryException(oe.m_line, oe.m_col,
						_("ORDER BY an aggregate sorts by one this query SELECTS - add it to the selection (SUM(x) AS Total) and sort by that name"));
			}
			if (!folded.IsEmpty()) {
				b.OrderByOutput(folded, o.m_ascending);
				continue;
			}
		}

		const std::vector<const ibBackendQueryColumn*> cols =
			ResolveWhereTarget(sources, oe, allowOrderDotWalk || ridesAGroupKey);

		if (cols.size() > 1) b.OrderBy(cols, o.m_ascending);
		else                 b.OrderBy(cols[0], o.m_ascending);
	}

	if (ast.m_distinct) {
		// ⭐ WITH THE COLUMNS IT DISTINGUISHES BY — the OUTPUT is what "duplicate" is a question
		// about, and this is the only tier that knows what the output is. A single-source read
		// projects nothing (the result reads columns off the source), so without this the provider
		// could only compare whole rows — key included, and a key never repeats.
		//
		// A computed / constant output has no column to name; those stay out of the list, and a
		// query made ONLY of them dedupes the way it always did (the whole projected row).
		std::vector<const ibBackendQueryColumn*> by;
		by.reserve(outSchema.size());
		for (const OutputColumn& oc : outSchema)
			if (oc.m_col != nullptr
			 && std::find(by.begin(), by.end(), oc.m_col) == by.end())
				by.push_back(oc.m_col);
		b.Distinct(std::move(by));
	}

	return aggregate;
}

// Build a SELECT's CORE (projections / FROM / WHERE / GROUP — NOT order/totals/unions) into an inner
// door and wrap it in ibSubqueryQueryable (owned in `owner`). Used for a subquery source AND for each
// branch of a UNION (the branch is itself a sub-SELECT). The wrapper exposes the branch's output
// columns (by their Select alias) — so the outer query / the UNION matches columns by name.
std::shared_ptr<ibSubqueryQueryable> WrapSelectAsQueryable(const ibQuerySelect& sel,
                                                const std::map<wxString, ibValue>& params,
                                                ibSubqueryOwner& owner)
{
	if (!sel.m_joins.empty() || sel.m_hasTotals)
		ThrowQueryException(0, 0, _("a subquery / UNION branch may not use JOIN or TOTALS yet"));

	const std::shared_ptr<const ibBackendQueryable> qi = ResolveFrom(sel.m_from, params, owner);   // recurse — nested subqueries
	ibDataQueryBuilder inner;
	inner.From(qi, sel.m_from.m_alias);   // owning handle — a nested wrapper stays alive through this builder

	std::vector<OutputColumn> innerSchema;
	// ⚠ BOUND BY THE ONE NAME A SOURCE HAS — the alias if written, the last segment of its name if
	// not (ibQuerySourceName). Binding by `m_alias` alone leaves an UNALIASED source bound to the
	// empty string, and then its own qualified fields (`Sales.Partner`) resolve against nothing:
	// "left over from a table this query does not read", about the table it is reading. The
	// statement road already learnt this (BuildSourceTree); the two inner roads had not.
	const std::vector<ibSourceBinding> innerSources{ { ibQuerySourceName(sel.m_from), qi.get() } };
	// A FOLDING inner (GROUP BY, with or without aggregate projections) is fine: the wrapper reads the
	// fold off the builder and ComputeRows runs SelectAggregate — the unpaged, full-spread read a
	// grouped query needs. The outer's pushed-down conditions post-filter the materialised rows.
	PopulateBuilder(sel, params, innerSources, inner, innerSchema, /*asSubquery*/true);

	// ibSubqueryQueryable copies the inner door (shares its owned raw columns via shared_ptr), so the
	// local 'inner' may die here — the copy is self-sufficient. The wrapper itself lives in 'owner'.
	// sel.m_top (SELECT TOP n in the branch / subquery) limits the materialised rows.
	// ⭐ THE WRAPPER IS TOLD ITS OUTPUT. `innerSchema` is exactly what this select produces — the name
	// of each field and how it is read — and it was being thrown away, leaving the wrapper to work the
	// same thing out from the door's internals. It could not: a dot-walk has no column of its own, a
	// GROUP BY key never enters the select list, a computed expression has neither. Each of those came
	// back as "unknown attribute" about a field the inner query plainly names.
	std::vector<ibSubqueryOutput> published;
	published.reserve(innerSchema.size());
	for (const OutputColumn& oc : innerSchema) {
		ibSubqueryOutput out;
		out.m_name         = oc.m_name;
		out.m_col          = oc.m_col;
		out.m_alias        = oc.m_byAlias ? oc.m_alias : wxString();
		out.m_objectPrefix = oc.m_objectPrefix;
		out.m_type         = oc.m_type;
		out.m_owned        = oc.m_ownedCol;   // …and its storage, when the schema minted the column
		published.push_back(out);
	}

	// SHARED FROM BIRTH — the wrapper owns the columns it publishes, and everything downstream that
	// names one of them (the builder, the result it stamps, the output schema) takes a share rather
	// than a bare pointer. `owner` is simply the run's own share.
	std::shared_ptr<ibSubqueryQueryable> wrapped =
		std::make_shared<ibSubqueryQueryable>(inner, sel.m_top, published);
	owner.push_back(wrapped);
	return wrapped;
}

// ⭐ WHY A QUERY WENT HOME INSTEAD OF TO THE SERVER — said where the decision is made, and named.
// Every exit below is "the rows road takes it", which is correct and invisible: the report is right
// and the whole source came into memory to make it so. In Debug the reason is one line in the
// journal beside the RAM read it causes; in Release the macro is the bare `return nullptr` it was.
// Format AND arguments, and the prefix GLUED to the format rather than printed through it: adjacent
// literals are one literal, so there is one format string and one pass. `name` is the declaration's
// own subject, so it is filled in here and the callsite writes only its reason.
#define CteDecline(fmt, ...) \
	do { ibJournalInfo(wxT("query.road"), wxT("nested source '%s' not declared to the server: ") fmt, \
	                   name, ##__VA_ARGS__); return nullptr; } while (false)

// A NAMED RESULT, DECLARED ON THE READER'S DOOR (the decision is in ResolveFrom — see there).
std::shared_ptr<const ibBackendQueryable> DeclareNamedResultAsCte(ibDataQueryBuilder& outer,
	const wxString& name, const ibQuerySelect& sel,
	const std::map<wxString, ibValue>& params, ibSubqueryOwner& owner)
{
	// THE ENGINE MUST BE ABLE TO READ ONE. Asked of the connected driver through L2's own question,
	// never of its dictionary — and a driver that cannot simply sends this back to the rows road.
	if (!ibQueryComposer::CanDeclareNamedQuery(outer.GetHolder()))
		CteDecline(wxT("this engine has no WITH"));

	// WHAT THE SERVER CANNOT READ FOR US goes back the other way rather than being half-translated:
	// a named query whose own source is computed in RAM (a register slice, a nested subquery, a temp
	// table we filled) has no SQL to declare, and its TOTALS is a tree, which no CTE can carry.
	//
	// ⚠ ASKED FIRST, BEFORE ANY WORK. A refusal costs nothing when it is the first thing said, and
	// the rows road resolves the same sources over again after one.
	if (sel.m_hasTotals)          CteDecline(wxT("it has TOTALS, which is a tree and not a table"));
	if (!sel.m_unions.empty())    CteDecline(wxT("it has a UNION (the inner door is built as one relation)"));

	// 🛑⭐⭐ AND A FOLD, BECAUSE THIS ROAD DOES NOT WRITE ONE. The body of a declaration is rendered by
	// BuildPageIR — an ordinary paged READ, `SELECT * FROM <table> ORDER BY <key>` — and an aggregate
	// lives on the other builder entirely (BuildAggregateQuery). Nothing here noticed the difference,
	// so a folded select was declared as a flat one: the GROUP BY and the SUM simply were not written.
	//
	// ⚠ AND THE READER STILL NAMED WHAT THE FOLD WOULD HAVE PRODUCED, which is how it surfaced —
	// `FROM (SELECT Goods, SUM(Qty) AS Total … GROUP BY Goods) AS t WHERE t.Total > 3` reached the
	// person as the DRIVER's own words, `Column unknown OUT_TOTAL`, about a column this road decided
	// not to write (measured 2026-09-03). A raw SQL error is the worst possible form for it: the
	// query is ordinary, and nothing in that sentence points at the query.
	//
	// So it takes the ROWS road, which folds it — the same fallback every other line here uses, and
	// the answer is correct on it. What is LOST is only where the folding happens: in RAM rather than
	// on the server. Teaching this road to render a fold (BuildAggregateQuery + its own projection,
	// instead of BuildPageIR) is the work that would win it back — worth doing, and not to be done
	// blind: composition renders EVERY report's source as a nested query, so this road carries them.
	if (!sel.m_groupBy.empty())   CteDecline(wxT("it has a GROUP BY, and a declaration's body is written as a plain read"));
	if (sel.m_having)             CteDecline(wxT("it has a HAVING, which only a folded body could apply"));
	for (const ibQueryProjection& p : sel.m_projections)
		if (ibQueryMentionsAggregate(p.m_expr))
			CteDecline(wxT("it projects an aggregate, and a declaration's body is written as a plain read"));

	// ⭐ A JOIN IS NO LONGER REFUSED HERE, because the refusal was about the door and not about the
	// query: this road built its inner door single-source, so a joined select had nowhere to put the
	// second table. It builds the whole source TREE now (BuildSourceTree, the one the statement road
	// uses), and whether that tree renders is a question for the tier that writes the SQL — asked
	// below, once the door exists, by CanDeclareAsNamedQuery.

	// …AND THE WORDS A DECLARATION WOULD DROP ON THE FLOOR. Each of these is carried by the rows road
	// and by nothing here, so taking this road with one of them written is the SILENT kind of wrong —
	// the query still runs and answers differently:
	//   FOR UPDATE — a declaration holds nothing, so the rows an author believes locked are not;
	//   INTO       — it materialises a temp table, which is the opposite of declaring a query.
	// (The same ones the FROM-flattening rule refuses, for the same reason — queryRewrite.cpp.)
	//
	// ⭐ TOP USED TO STAND HERE and no longer does. The reasoning was sound and the premise was not:
	// "a CTE built without the limit publishes EVERY row" was true of the BUILDER, which rendered the
	// body from an empty page request and dropped `m_topCount` on the floor — not of `WITH`, which
	// takes a limited body in every engine that has it. The body now carries its own limit
	// (AttachNamedQueries, both roads), so the declaration says what the author wrote.
	if (sel.m_forUpdate)          CteDecline(wxT("it has FOR UPDATE, and a declaration holds nothing"));
	if (!sel.m_intoTemp.IsEmpty()) CteDecline(wxT("it has INTO, which materialises rather than declares"));

	ibDataQueryBuilder inner;
	// SELECT ALLOWED travels DOWN, to the door that reads the restricted source. It is the quiet form
	// of a policy refusal — read what you may rather than raise — and the reader above cannot carry it
	// for this source: a declared query is a NAME to it, with no policy of its own left to soften.
	inner.Allowed(sel.m_allowed);

	std::vector<ibSourceBinding> innerSources;
	std::vector<ibQueryAstExprPtr> innerSourceConditions;   // conditions written INSIDE a virtual table call
	std::vector<OutputColumn> innerSchema;
	// ⭐ THE WHOLE SOURCE TREE, BY THE ONE BUILDER THAT BUILDS ONE. FROM plus every JOIN — a ref-path
	// join, a cross, an ON, the alias rules, the source-name binding — is the statement road's own
	// BuildSourceTree, so a declared query reads its sources exactly as an ordinary one does. This
	// road used to open the FROM by hand and had no place to put a second table, which is what the
	// blanket JOIN refusal above was really about.
	//
	// asSubquery — every output field is projected under an explicit alias, which is exactly what a
	// CTE publishes and what the reader then names (`Sales.Partner`).
	//
	// ⚠ AND ITS REFUSAL IS A FALLBACK, NOT A FAILURE. Anything this select does that the CTE road
	// cannot build has to leave by returning null so the rows road takes it — which is why BOTH the
	// source tree and PopulateBuilder run inside the catch: an unresolvable source is not an error
	// here, the rows road resolves it the same way and reports it there if it is one.
	try {
		BuildSourceTree(sel, params, owner, innerSources, inner, &innerSourceConditions);
		PopulateBuilder(sel, params, innerSources, inner, innerSchema, /*asSubquery*/true, innerSourceConditions);
		// ⭐ AND THE AUTHOR'S LIMIT IS PUT ON THE DOOR, because a declaration has no terminal to put it
		// on. An ordinary read carries TOP in its PAGE REQUEST — the limit is asked for at the moment
		// the rows are fetched — and a declared query is never fetched: it is written into a `WITH` and
		// read by somebody else. Stated here, it reaches the declaration's body as a real limit
		// (AttachNamedQueries renders it); left off, the body would publish every row while the author
		// wrote ten, which is the silent wrong answer this used to be refused to avoid.
		if (sel.m_top > 0)
			inner.Top(sel.m_top);
	}
	catch (const ibBackendException& err) {
		// ⚠ THE DESCRIPTION IS DATA, never the format — a message carrying a stray `%` would be read
		// as a conversion and print whatever happened to be next (CLAUDE.md, the same rule wxLogError
		// follows).
		CteDecline(wxT("%s"), err.GetErrorDescription());
	}
	if (innerSources.empty() || innerSources.front().m_q == nullptr)
		CteDecline(wxT("its own source did not resolve"));
	for (const ibSourceBinding& src : innerSources)
		if (src.m_q != nullptr && src.m_q->IsComputedInRam())
			CteDecline(wxT("its source '%s' is computed in RAM"), src.m_q->GetQueryName());

	// …AND WOULD IT RENDER? Asked of the tier that writes the SQL, because that is where the answer
	// lives: one source always renders, a join renders when its tree co-locates. Asked HERE, while
	// there is still a road back — after `outer.With(...)` the statement would name a table nothing
	// wrote.
	if (!ibDbTableProvider::CanDeclareAsNamedQuery(inner))
		CteDecline(wxT("its source tree has no single server-side form (a join that does not co-locate)"));

	// ⭐ WHAT THE DECLARATION PUBLISHES IS WHAT ITS STATEMENT CAN WRITE. A column the layout gives no
	// fields for has nothing to project, so publishing it would name something the CTE's own SELECT
	// never wrote — which is precisely the `-206 Column unknown` this road first hit (2026-08-23).
	//
	// Dropped on BOTH sides by the same test (AttachNamedQueries drops it from the projection), because
	// a published set and a select list that disagree is the shape of that error.
	std::vector<ibCteQueryable::Field> fields;
	fields.reserve(innerSchema.size());
	// Every physical field this declaration actually writes, so the synthetic columns below can be
	// asked the only question that decides them: are the fields you are made of in here?
	std::vector<wxString> writtenFields;
	std::vector<const OutputColumn*> synthetic;
	for (const OutputColumn& oc : innerSchema) {
		// Nothing to write into a SELECT means nothing to publish.
		if (oc.m_col != nullptr && !oc.m_col->IsSyntheticColumn() && ColumnFieldNames(oc.m_col).empty())
			continue;
		// ⭐ A SYNTHETIC COLUMN IS NOT WRITTEN, BUT IT IS PUBLISHED — the two are different questions,
		// and answering the second with the first is what made the MOMENT disappear. It has no field
		// of its own (its layout names the date's and the reference's), so the projection leaves it
		// out — AttachNamedQueries does that, and must go on doing it. The declared TABLE, though, is
		// read by name, and the name is exactly what a report groups or sorts by. Held back to a
		// second pass, because "are its parts written" cannot be answered until they all are.
		if (oc.m_col != nullptr && oc.m_col->IsSyntheticColumn()) {
			synthetic.push_back(&oc);
			continue;
		}
		// ⚠ …AND ONE COLUMN IS PUBLISHED ONCE. Two outputs may stand over the SAME column — the same
		// field asked for twice, under two names — and both would publish its physical `fld<metaID>`
		// spread, which is one name twice in the statement (`-104 … specified multiple times`,
		// 2026-08-24). The projection drops the second by the same rule (AttachNamedQueries), so the
		// published set and the select list keep saying the same thing.
		// ⭐ …AND "ONCE" IS COUNTED BY THE NAME IT IS READ BY. Two outputs over one column under two
		// aliases are two things — the outer query may fold by either — and only the same NAME twice
		// is one thing said twice. Deduped by the column alone, the second alias vanished from the
		// published set while the outer went on naming it (`unknown attribute 'Attribute21'`).
		if (std::any_of(fields.begin(), fields.end(), [&](const ibCteQueryable::Field& f) {
				return f.m_name.IsSameAs(oc.m_name, false); }))
			continue;
		// A REPEATED COLUMN gets its spelling from the OUTPUT NAME: `fld<metaID>` is already taken by
		// the first projection of it, and one alias written twice is the `-104` this rule guards.
		// AttachNamedQueries writes exactly this, so the declaration and the statement agree.
		const bool repeated = oc.m_col != nullptr &&
			std::any_of(fields.begin(), fields.end(), [&](const ibCteQueryable::Field& f) {
				return f.m_physical.IsSameAs(oc.m_col->GetPhysicalName(), false); });
		// …and the PHYSICAL name comes from the source column: `fld<metaID>`, unique per metatype, so
		// the fields two sources publish cannot collide even when both are called `Ref` or
		// `PointInTime`. Falls back to the output name where there is no column behind it (an
		// aggregate, a computed projection) — those are already unique by their own alias.
		// …AND THE KIND IS ASKED OF THE COLUMN, NOT INFERRED FROM HAVING ONE.
		//
		// 🛑 It read `m_col != nullptr ? Composite : Computed` — which was true only while a computed
		// output had no column at all. Every output is given one now (giveIdentity mints a synthetic
		// for the ones nothing backs), so the test answered "Composite" for all of them, and the
		// declared field spread into role fields the statement never wrote: `-206 Column unknown
		// YTFDS_N` the moment a person sorted by a computed field (Max, 2026-08-24).
		//
		// The column already answers it — `ibSyntheticOutputColumn` says Computed. And the question is
		// asked NARROWLY, as "does this output spread?": the declared field is an `ibTempColumn`, so
		// carrying a source column's kind across verbatim would have it claim to be a RAW DB column
		// (which a reader casts to one) or the MOMENT (whose fields are other columns'). Everything
		// that spreads is Composite here, exactly as before; only the computed output is not.
		//
		// A repeated alias is still the column it came from and spreads exactly as the first
		// projection does.
		const bool computed = oc.m_col == nullptr
			|| oc.m_col->GetColumnKind() == ibBackendQueryColumn::Kind::Computed;

		// ⭐⭐ AN OUTPUT READ BY ALIAS IS SPELLED BY ITS ALIAS — physical name included.
		//
		// A DOT-WALK output (`Product.Parent AS ProductGroup`) has a column behind it, but that column
		// belongs to the JOINED table, not to this query's source: the statement writes it under the
		// alias (`ProductGroup_RRRef`), because that is the only name the walk has out here. Publishing
		// it as `fld<metaID>` said the leaf's own name instead, and then the two halves disagreed:
		// the declaration promised FLD1012_RRREF, the SELECT wrote ProductGroup_RRRef, and an outer
		// ORDER BY over it answered `-206 Column unknown FLD1012_RRREF` (2026-08-31).
		//
		// ⚠ AND THE ALIAS IS THE UNIQUE ONE. Two walks that end on the SAME leaf — `Ref.Date` and
		// `Recorder.Date` — carry one physical name between them and would be written twice
		// (`-104 … specified multiple times`); their aliases are different by construction, because an
		// author cannot name two outputs alike.
		const bool spelledByAlias = oc.m_byAlias && !oc.m_alias.IsEmpty();

		fields.push_back({ oc.m_name,
			spelledByAlias ? ibSqlAliasOf(oc.m_alias)   // …in the STATEMENT's spelling, which is what was written
			               : ((oc.m_col != nullptr && !repeated) ? oc.m_col->GetPhysicalName() : oc.m_name),
			oc.m_type,
			computed ? ibBackendQueryColumn::Kind::Computed
			         : ibBackendQueryColumn::Kind::Composite });
		// What this output really puts in the statement — the spread of the column behind it. A
		// repeated alias writes the SAME fields under its own name, so it adds nothing here.
		if (oc.m_col != nullptr && !repeated && !spelledByAlias)
			for (const wxString& f : ColumnFieldNames(oc.m_col))
				writtenFields.push_back(f);
	}

	// …AND NOW THE SYNTHETIC ONES, each published only if every field it reads itself out of is in
	// the statement. A moment over a declaration that projected the date but not the reference would
	// resolve by name and then read half of itself — the silent kind of wrong this road has already
	// paid for once.
	for (const OutputColumn* oc : synthetic) {
		// ⚠ UNDER ITS OWN NAME ONLY. The column IS the published one, so what a reader may call it is
		// what the column calls itself; an output that renamed it (`PointInTime AS Moment`) would be
		// published under a name it does not answer to, which is worse than not publishing it.
		if (oc->m_col == nullptr || !oc->m_name.IsSameAs(oc->m_col->GetName(), false))
			continue;
		const std::vector<wxString> parts = ColumnFieldNames(oc->m_col);
		if (parts.empty())
			continue;
		const bool allWritten = std::all_of(parts.begin(), parts.end(), [&](const wxString& p) {
			return std::any_of(writtenFields.begin(), writtenFields.end(),
				[&](const wxString& w) { return w.IsSameAs(p, false); });
		});
		if (!allWritten)
			continue;
		if (std::any_of(fields.begin(), fields.end(), [&](const ibCteQueryable::Field& f) {
				return f.m_name.IsSameAs(oc->m_name, false); }))
			continue;
		ibCteQueryable::Field borrowed;
		borrowed.m_name     = oc->m_name;
		borrowed.m_borrowed = oc->m_col;
		fields.push_back(borrowed);
	}

	if (fields.empty())
		CteDecline(wxT("it publishes no fields to be read by name"));

	// ⚠ AND EVERY PUBLISHED NAME MUST BE ITS OWN. A declaration is read BY NAME, so two outputs called
	// the same thing are not a preference — the engine refuses the statement outright ("column X was
	// specified multiple times"), and a reader could not have told them apart anyway.
	//
	// Two sources are what makes this reachable: `SELECT A.Ref, B.Ref` publishes `Ref` twice, and a
	// column that spreads (a reference, a document's MOMENT) publishes several fields under each of
	// those names. Today a JOIN or a UNION already sends this road back, so it cannot happen; this
	// stands so that lifting THAT refusal cannot quietly produce an unparseable statement instead.
	for (size_t i = 0; i < fields.size(); ++i)
		for (size_t j = i + 1; j < fields.size(); ++j)
			if (fields[i].m_name.IsSameAs(fields[j].m_name, false))
				CteDecline(wxT("two outputs publish the same name '%s'"), fields[i].m_name);

	ibJournalInfo(wxT("query.road"), wxT("SERVER: nested source declared as WITH %s (%u fields)"),
	              name, static_cast<unsigned>(fields.size()));
	outer.With(name, inner);   // …and the declaration lands on the READER's statement

	// ⚠ THE IDS ARE NUMBERED FROM THE RUN, not from a counter that keeps growing. `ibMetaID` is a
	// 32-bit signed int, so a thread_local that only ever climbs eventually overflows and, long
	// before that, walks into the range real metaIDs live in. What these ids have to be is UNIQUE
	// AMONG THIS RUN'S SOURCES — and `owner` counts exactly those, resetting with every execution.
	// The KIND is what keeps these clear of every other minted id, so nothing has to be offset away
	// from anybody: the ordinal is simply this run's count of sources, times the block a named query
	// needs for its own columns. (The `+ 100000` that used to sit here was pure band-thinking — a
	// distance from ranges that no longer exist.)
	const ibMetaID base = ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Subquery,
		static_cast<ibMetaID>(owner.size()) * kCteColumnStride);
	std::shared_ptr<ibCteQueryable> source =
		std::make_shared<ibCteQueryable>(name, fields, base, ibSourceMetaDataScope::Get());
	owner.push_back(source);
	return source;
}

#undef CteDecline   // one function's word — it ends with the function

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
		ThrowQueryException(0, 0, _("FOR UPDATE cannot be used with UNION: a composed result holds no rows to lock"));

	ibQuerySelect core0 = ast;
	core0.m_orderBy.clear();
	core0.m_unions.clear();
	core0.m_totalsBy.clear();
	core0.m_totalsAggregates.clear();
	core0.m_totalsOverall = false;
	core0.m_hasTotals = false;
	core0.m_top = 0;

	const std::shared_ptr<ibSubqueryQueryable> b0 = WrapSelectAsQueryable(core0, params, owner);

	ibDataQueryBuilder b;
	b.From(b0);   // the branch is owned by the builder too, and travels on to the result it stamps
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
	//
	// ⭐⭐ EVERY BRANCH SELECTS THE SAME NUMBER OF COLUMNS, and this is where that is checked.
	//
	// Unchecked, a mismatch reached the SERVER, which answered in its own words about its own
	// generated column names — a message that names nothing the author wrote and points at no line
	// of their query. Worse, some engines do not refuse at all: they line the columns up by position
	// and hand back a result where one branch's value sits under another branch's heading, which is
	// not an error anywhere and is wrong everywhere.
	//
	// So the count is compared here, against the FIRST branch (whose columns are the union's own
	// output), and the complaint names both numbers and the branch that differs.
	int branchNumber = 1;
	for (const std::shared_ptr<ibQuerySelect>& u : ast.m_unions) {
		const std::shared_ptr<ibSubqueryQueryable> bn = WrapSelectAsQueryable(*u, params, owner);
		branchNumber++;

		size_t width = 0;
		for (const ibBackendQueryColumn* c : bn->GetColumns())
			if (c != nullptr) width++;

		if (width != outSchema.size())
			ThrowQueryException(0, 0, wxString::Format(
				_("UNION branch %d selects %u column(s) while the first selects %u - every branch of a "
				  "union must select the same columns, in the same order"),
				branchNumber, static_cast<unsigned int>(width), static_cast<unsigned int>(outSchema.size())));

		b.Union(bn, wxEmptyString, /*keepDuplicates*/ u->m_unionAll);
	}

	// ORDER BY on the whole union — resolve against the first branch's columns (by name).
	const std::vector<ibSourceBinding> usrc{ { wxEmptyString, b0.get() } };
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
                     ibSubqueryOwner& owner, std::vector<ibSourceBinding>& sources, ibDataQueryBuilder& b,
                     std::vector<ibQueryAstExprPtr>* sourceConditions)
{
	// ⭐⭐ A DECLARATION IS ONLY WORTH MAKING IF THE WHOLE QUERY GOES TO THE SERVER AS ONE STATEMENT.
	//
	// `WITH q_sub0 AS (…)` is written into the statement that READS it. When some other source of the
	// same query cannot be rendered — a register's virtual table computes its rows in memory — the
	// composer stops writing one statement and reads each source on its own instead. The declaration
	// then belongs to a statement nobody runs, and the read of the declared name goes out bare.
	//
	// 🛑 Which is exactly what the person saw: `FROM (SELECT … FROM …Balance(&Early)) AS a LEFT JOIN
	// …Balance(&P) AS b2` came back as the DRIVER's words — `-204 Table unknown Q_SUB0`, about
	// `SELECT * FROM q_sub0`. The journal shows both halves of the split one line apart: "SERVER:
	// nested source declared as WITH q_sub0", then a statement with no WITH in it (2026-09-04).
	//
	// A virtual table is named in THREE segments (`Register.Stock.Balance`) — that is what makes it
	// one, and it is knowable here, before any source is resolved. With one of those present the
	// nested source takes the rows road (ibSubqueryQueryable) as it always did: slower, correct, and
	// the road every other computed source in this query is on anyway.
	const bool anySourceComputesInRam = [&ast]() {
		// A select that FOLDS or UNIONS cannot be a declaration's body — this road writes the body as a
		// plain read (see DeclareNamedResultAsCte). Asked of the AST, so it holds for a subquery written
		// here AND for a named result the package declared with ONTO, which is the same select seen by
		// name. Without the second case a LINK over one folded selection and one plain one declared the
		// PLAIN one, sent the join to RAM, and the declaration was left for nobody to write:
		// `-204 Table unknown ITEMS` about `SELECT * FROM Items` (2026-09-04).
		auto foldsOrUnions = [](const ibQuerySelect& s) {
			if (!s.m_groupBy.empty() || s.m_having || s.m_hasTotals || !s.m_unions.empty())
				return true;
			for (const ibQueryProjection& p : s.m_projections)
				if (ibQueryMentionsAggregate(p.m_expr))
					return true;
			return false;
		};
		auto readsInRam = [&foldsOrUnions](const ibQuerySource& src) {
			if (src.m_name.size() >= 3)
				return true;                       // a register's virtual table — computed in memory
			if (src.m_subquery)
				return foldsOrUnions(*src.m_subquery);
			if (src.m_name.size() == 1)
				if (const ibQuerySelect* named = ibNamedResultScope::Find(src.m_name.front()))
					return foldsOrUnions(*named);   // a selection this package named (ONTO)
			return false;
		};
		if (readsInRam(ast.m_from))
			return true;
		for (const ibQueryAstJoin& j : ast.m_joins)
			if (readsInRam(j.m_source))
				return true;
		return false;
	}();
	ibDataQueryBuilder* const declareOn = anySourceComputesInRam ? nullptr : &b;

	// The door goes in as well: a source that turns out to be a NAMED RESULT is declared ON it
	// (`WITH …`) instead of being read into RAM — see ResolveFrom.
	const std::shared_ptr<const ibBackendQueryable> q0 = ResolveFrom(ast.m_from, params, owner, sourceConditions, declareOn);
	// ⭐⭐ ONE NAME FOR A SOURCE, HERE TOO. `ibQuerySourceName` — the alias if written, the last
	// segment of the name if not — is what the renderer writes and what the constructor matches on,
	// so it is the name the AUTHOR sees. Binding by `m_alias` alone made `BalanceAndTurnovers.Period`
	// resolvable only when somebody had written `AS`, and unresolvable in the very text this product
	// generates.
	sources.push_back({ ibQuerySourceName(ast.m_from), q0.get() });
	b.From(q0, ast.m_from.m_alias);   // owning handle — see ResolveFrom

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
				ThrowQueryException(0, 0, _("a reference-path JOIN (alias.field) needs an explicit alias (AS)"));
			ExpandRefJoinAlias(b, sources, refRoot,
				std::vector<wxString>(j.m_source.m_name.begin() + 1, j.m_source.m_name.end()),
				j.m_source.m_alias, kind, refJoinSeq);
			continue;
		}

		const std::shared_ptr<const ibBackendQueryable> resolved = ResolveFrom(j.m_source, params, owner, sourceConditions, declareOn);
		const wxString alias = ibQuerySourceName(j.m_source);   // same one name — see the FROM above
		// ⚠ ASKED OF THE WRITTEN ALIAS, not of the name it falls back to. This rule is about an author
		// writing one `AS` twice; two unaliased reads of the same table are a different (and older)
		// shape, and refusing them here would be this change picking up a quarrel that is not its own.
		RequireAliasFree(sources, j.m_source.m_alias, 0, 0);   // duplicate alias -> Fail
		// ⭐⭐ A TABLE JOINED TO ITSELF IS TWO SOURCES, and the second one is given its own columns.
		//
		// `FROM T AS A JOIN T AS B` resolves to the SAME queryable both times, and everything below
		// routes a column to its source by asking the sources which of them owns it — so with one set
		// of columns the answer is "A" for both sides and `B.x` reads `A.x`. The wrapper is minted
		// HERE, where the repetition is first visible, and nothing downstream needs to know why the
		// two sides differ: they simply do. (queryable.h, ibAliasQueryable)
		// ⭐⭐ AND EACH FURTHER READING WRAPS THE ONE BEFORE IT — the third takes the second, not the
		// original. Their ids are what has to differ, and an id is the origin's number with the kind
		// stamped on: stamping the SAME origin twice gives the same number twice. Chaining the wrap
		// stamps a number that already carries a kind, and the two twins land apart.
		const ibSourceBinding* const prior = LatestReadingOf(sources, resolved.get());
		std::shared_ptr<const ibBackendQueryable> qi = resolved;
		if (prior != nullptr)
			qi = std::make_shared<const ibAliasQueryable>(prior->m_hold ? prior->m_hold : resolved, alias);
		sources.push_back({ alias, qi.get(), qi });
		if (j.m_on && j.m_on->m_kind == ibQueryAstExprKind::Literal && j.m_on->m_literal.GetBoolean()) {
			b.CrossJoin(qi, kind, alias);   // ON TRUE -> cross join (cartesian)
		}
		else if (j.m_on) {
			// AN ARBITRARY LINK CONDITION — `ON a.x = b.y AND a.z = b.w`, which is what the constructor's
			// "custom" link writes. The builder carries ONE join key, so the condition is split: the
			// first comparison IS the key, the rest are filters. They are equivalent for an INNER join
			// and NOT for an outer one, where a condition in ON pre-filters the null-padded side and the
			// same condition in WHERE removes the padded rows afterwards — so the extra terms are only
			// accepted on an INNER join, and an outer one is told to move them itself rather than being
			// quietly given a different result.
			std::vector<ibQueryAstExprPtr> onTerms;
			ibQueryFlattenAnd(j.m_on, onTerms);
			if (onTerms.empty())
				onTerms.push_back(j.m_on);

			// ⭐⭐ A KEY OF SEVERAL FIELDS IS ONE KEY. `ON T.Goods = B.Goods AND T.Warehouse =
			// B.Warehouse` is how two readings of one register meet — a stock statement matches on
			// the ITEM and the WAREHOUSE, and matching on one of them is not a weaker answer, it is
			// the wrong one. Every term that is a plain COLUMN = COLUMN equality joins the key
			// (ibJoinOn::m_alsoOn); the door then hashes the TUPLE.
			//
			// 🛑 An outer join used to be refused here and told to "write the other conditions in
			// WHERE" — advice that silently changes the answer: a condition on the null-producing
			// side, moved to WHERE, removes the padded rows and turns the outer join into an inner
			// one. What is refused now is only what still cannot be carried: a term that is not a
			// column-to-column equality (a constant filter, a computed side) on an OUTER join.
			// ⭐⭐ WHAT MAKES A TERM PART OF THE KEY IS THAT IT COMPARES THE TWO SIDES — not that it
			// compares them with `=`. `ON B.Currency = A.Currency AND B.Period > A.Period` is one
			// sentence: the equality says WHICH series, the inequality says WHERE IN IT. Refusing the
			// second on an outer join left that question unanswerable — a query language with no
			// windows and no scalar subquery has only the join to say it with.
			//
			// What still cannot ride is a term that filters ONE SIDE (a constant, a computed side):
			// on an outer join that is not a key at all, and moving it to WHERE changes the answer.
			std::vector<ibQueryAstExprPtr> keyTerms, otherTerms;
			for (const ibQueryAstExprPtr& term : onTerms) {
				const bool comparesBothSides = term && term->m_kind == ibQueryAstExprKind::Compare
					&& term->m_lhs && term->m_rhs
					&& term->m_lhs->m_kind == ibQueryAstExprKind::Column
					&& term->m_rhs->m_kind == ibQueryAstExprKind::Column;
				(comparesBothSides ? keyTerms : otherTerms).push_back(term);
			}
			if (!otherTerms.empty() && (keyTerms.size() + otherTerms.size()) > 1 && kind != ibQueryJoinKind::Inner)
				ThrowQueryException(j.m_on->m_line, j.m_on->m_col,
					_("an outer JOIN ON takes a key of column comparisons (a.x = b.y AND a.z = b.w); "
					  "anything else belongs in WHERE"));

			// The key terms lead, so the first of them becomes the join key proper and the rest ride
			// with it; a non-key term keeps the old road (an INNER join's extra filter).
			if (keyTerms.size() > 1) {
				onTerms = keyTerms;
				onTerms.insert(onTerms.end(), otherTerms.begin(), otherTerms.end());
			}

			const ibQueryAstExprPtr on = onTerms.front();
			if (on->m_kind != ibQueryAstExprKind::Compare)
				ThrowQueryException(on->m_line, on->m_col, _("a JOIN ON clause must compare two sides (a.x <op> b.y), or be TRUE for a cross join"));
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
			const bool lhsVal = isValueExpr(*on->m_lhs);
			const bool rhsVal = isValueExpr(*on->m_rhs);
			if (lhsVal != rhsVal) {
				if (kind != ibQueryJoinKind::Inner)
					ThrowQueryException(on->m_line, on->m_col, _("a constant JOIN ON (column <op> value/&param) is only supported for an INNER join: put the condition in WHERE for an outer join"));
				const ibQueryAstExpr& colE = rhsVal ? *on->m_lhs : *on->m_rhs;
				const ibQueryAstExpr& valE = rhsVal ? *on->m_rhs : *on->m_lhs;
				// When the value is on the LEFT the comparison direction flips for `col <op> value`.
				ibQueryCompareOp cmp = on->m_cmp;
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
			else if (IsComputedExprAst(*on->m_lhs) || IsComputedExprAst(*on->m_rhs)) {
				// Computed ON (a.x+1 <op> b.y) — both sides become column exprs, evaluated per pair in the RAM
				// theta loop (lhs over left, rhs over right). No dot-walk inside the expression.
				b.Join(qi, BuildColumnExprFromAst(sources, *on->m_lhs, params),
				           BuildColumnExprFromAst(sources, *on->m_rhs, params),
				           MapJoinOp(on->m_cmp), kind, alias);
			}
			else {
				const ibBackendQueryColumn* lc = ResolveColumnSingle(sources, *on->m_lhs);
				const ibBackendQueryColumn* rc = ResolveColumnSingle(sources, *on->m_rhs);

				// …AND THE REST OF THE KEY, where the author wrote a composite one. Only plain
				// equalities join it (the split above put them first); an INNER join's remaining
				// terms stay filters, exactly as before.
				// …AND THE REST OF THE KEY, each part with the comparison the author wrote. The parts are
				// no longer required to be equalities: the door hashes the equal ones and checks the
				// rest per candidate pair, which is what lets one ON hold both halves of "the next
				// record of the same series".
				std::vector<ibJoinOn::ibJoinKeyPart> alsoOn;
				for (size_t k = 1; k < keyTerms.size(); ++k) {
					const ibQueryAstExprPtr& term = keyTerms[k];
					ibJoinOn::ibJoinKeyPart part;
					part.m_colL = ResolveColumnSingle(sources, *term->m_lhs);
					part.m_colR = ResolveColumnSingle(sources, *term->m_rhs);
					part.m_op   = MapJoinOp(term->m_cmp);
					alsoOn.push_back(part);
				}

				b.Join(qi, lc, rc, MapJoinOp(on->m_cmp), kind, alias, alsoOn);   // = -> hash; <,<=,>,>=,<> -> theta (server-side when co-located)
			}
		}
		else {
			// ⭐⭐ NO LINK MEANS THE PRODUCT. A link either exists or it does not; where it does not,
			// every row of one table stands against every row of the other — add Products and Features
			// and you asked for count(Products) × count(Features). It is an ordinary thing to write,
			// and what narrows it is a condition.
			//
			// It used to auto-join by reference here — the engine looked for a reference between the
			// tables and quietly joined on it. That is a link nobody wrote: invisible in the text,
			// absent from the constructor's list of links, and impossible to remove because there was
			// nothing to remove. Worse, the same query meant different things depending on whether
			// some column happened to refer to the other table.
			//
			// (The reference walk is not lost. It is what a dot-walk IS — `Order.Client.Name` — and
			// the named ref-path join above, both of which SAY so. What is gone is the guess.)
			//
			b.CrossJoin(qi, kind, alias);
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
//
// ⚠ ONE DOOR, TWO READINGS — and only ONE of them is allowed to accuse. CheckNames is the VERDICT:
// a qualified name the factory does not know is a table that is GONE and it says so, loudly. Its
// two siblings (PruneUnresolved, UngroupedProjections) are asking what WORK IS LEFT, and their
// published promise is that the unverifiable answers empty. Raising at them turns "I cannot check
// this" into an exception the caller never asked for — with no application data at all (a headless
// host, a bare test binary) the very first source raises and a plain `SELECT Code, Name` — which
// owes no grouping whatsoever — dies before the rule is even read.
//
// So the accusation is the CALLER's to ask for. `reportMissing` says which reading this is; the
// silent one still returns false, which every caller already handles as "nothing can be said".

// A source this check cannot know the columns of — held in the list so positions stay true, with
// nothing behind it. Every reader here already skips a binding with no queryable.
ibSourceBinding Opaque(const ibQuerySource& source)
{
	ibSourceBinding binding;
	binding.m_alias = ibQuerySourceName(source);
	return binding;   // m_q stays null — that is what makes it opaque
}
// ⭐⭐ AND ONE UNVERIFIABLE SOURCE DOES NOT MAKE A QUERY UNVERIFIABLE.
//
// This answered "cannot verify" for the WHOLE query the moment ONE source was a nested select — and
// a nested select is an ordinary thing to write: `FROM (SELECT …) AS T, AccumulationRegister.R.
// BalanceAndTurnovers(…)`. Everything standing beside it went unchecked with it, so a field the
// register plainly does not produce sailed through and the verdict line said the engine reads the
// query. The one sentence this window must never say untruthfully, said because of the table NEXT to
// the one at fault.
//
// `tolerateOpaque` keeps the unverifiable source IN THE LIST instead, with no queryable behind it:
// its position is preserved (the join rules read sources by position), its columns are left alone,
// and every other source is checked exactly as strictly as before.
bool BuildCheckSources(const ibQuerySelect& ast, const std::map<wxString, ibValue>& params,
                       std::vector<ibSourceBinding>& out, bool reportMissing = true,
                       bool tolerateOpaque = false)
{
	std::vector<const ibQuerySource*> sources;
	sources.push_back(&ast.m_from);
	for (const ibQueryAstJoin& join : ast.m_joins)
		sources.push_back(&join.m_source);

	for (const ibQuerySource* source : sources) {
		if (source == nullptr)
			return false;

		if (source->m_subquery) {
			// Same split: the nested select is VERDICT work. A caller that only asked what is left
			// to do must not be handed the nested query's refusal either.
			if (reportMissing)
				CheckSelectNames(*source->m_subquery, params);   // on its own terms
			// Its COLUMNS are not ours to verify — the sources beside it still are.
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}
		if (source->m_name.empty()) {
			// A source still being written: unverifiable, not wrong. Same treatment.
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}

		// ⚠ TWO DIFFERENT SILENCES, and telling them apart is the whole point.
		//
		// A ONE-SEGMENT name is a temp table. It may legitimately be made by a statement this check
		// cannot see (another package, a caller's manager), so failing to resolve it means "cannot
		// verify" — and inventing an error there would be worse than saying nothing.
		//
		// A QUALIFIED name (Kind.Name) is a metaobject, resolved through the config's own factory.
		// If the factory does not know it, the object is GONE — deleted or renamed — and that is not
		// "cannot verify", that is the query naming something that does not exist. Swallowing it is
		// how a query goes on looking healthy after the table under it was deleted: the fields are
		// still listed, the verdict line still says the engine reads the query, and the first sign
		// of trouble arrives when somebody runs it.
		const ibBackendQueryable* queryable = nullptr;
		try { queryable = ResolveSource(*source, params); }
		catch (const ibBackendException&) {
			if (reportMissing && source->m_name.size() > 1)
				throw;      // the engine's own words, its own position — carried up verbatim
			// A temp table this check cannot see — or a reading that does not accuse.
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}
		if (queryable == nullptr) {
			// A name nothing answers to is the QUERY being wrong, not the engine — L4's variety even
			// though L3 is the one that finds out, because that is who the message is for.
			if (reportMissing && source->m_name.size() > 1)
				ibBackendQueryNameException::ErrorAt(source->m_line, source->m_col,
					_("Table '%s' does not exist"), ibQuerySourceName(*source));
			if (!tolerateOpaque)
				return false;
			out.push_back(Opaque(*source));
			continue;
		}

		// ⭐⭐ THE NAME A SOURCE ANSWERS TO IS ibQuerySourceName — the alias if one is written, the
		// last segment of the name if not. That is what the renderer writes, what the constructor
		// matches on, and what the author therefore SEES; binding by `m_alias` alone meant this check
		// could not attribute `BalanceAndTurnovers.Period` to the table one line above it unless the
		// author had happened to write `AS`. Two readers of one sentence, and only one of them right.
		ibSourceBinding binding;
		binding.m_alias = ibQuerySourceName(*source);
		binding.m_q     = queryable;
		out.push_back(binding);
	}
	// At least one source this check can actually stand on. All-opaque is the old silence, unchanged.
	for (const ibSourceBinding& binding : out)
		if (binding.m_q != nullptr)
			return true;
	return false;
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

	ibQueryForEachChild(*e, [&](const ibQueryAstExprPtr& child) {
		CollectFoldedAndFree(child, fold, folded, free);
	});
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
			//
			// ⭐ …AND A KEY COVERS WHAT IT WALKS INTO. `GROUP BY Producer` fixes `Producer.Region` for
			// every row of the group, so a key that is a PREFIX of the projected path is enough. The
			// lowering adds the leaf to the server's GROUP BY itself (a spelling SQL requires); asking
			// the author for it here would be demanding he repeat what he has already said.
			for (const std::vector<const ibBackendQueryColumn*>& key : keyCols) {
				if (key.empty() || key.size() > projCols.size())
					continue;
				bool prefix = true;
				for (size_t k = 0; k < key.size() && prefix; ++k)
					prefix = (key[k] == projCols[k]);
				if (prefix) { grouped = true; break; }
			}
			if (!grouped)
				out.push_back(ColumnCopy(*column));
		}
	}
	return out;
}

// ⭐⭐ THE LINKS MUST NOT CONTRADICT ONE ANOTHER, and it is the ENGINE that says so.
//
// A join condition is a sentence about TWO tables. Two ways of writing one are not links at all, and
// both are easy to write by hand and impossible to see afterwards:
//
//   * a table joined to ITSELF — every column of the condition roots in the same source. Whatever it
//     filters, it says nothing about how the two tables meet, and the join is left unconstrained.
//   * a condition that never MENTIONS the table it is written on. That is a filter wearing a join's
//     clothes: for an INNER join it happens to behave like one in WHERE, for an OUTER join it does
//     NOT (it pre-filters the null-padded side), so the same text means two different things
//     depending on a box ticked elsewhere. The language has a place for a filter, and it is WHERE.
//
// Said here rather than in the constructor's grid because it is a fact about the QUERY, not about
// the window: the same text typed into the query editor deserves the same answer.
void CheckJoinsAreConsistent(const ibQuerySelect& ast, const std::vector<ibSourceBinding>& sources)
{
	// WHICH source a column path stands on, BY POSITION — because position is what one of the rules
	// below is about. -1 when nothing answers: that is an UNKNOWN COLUMN, which the ordinary name
	// check reports in its own words, so this one stays quiet about it.
	const auto rootOf = [&sources](const ibQueryAstExpr& e) -> int {
		if (e.m_path.empty())
			return -1;
		if (e.m_path.size() >= 2)
			for (size_t i = 0; i < sources.size(); ++i)
				if (!sources[i].m_alias.IsEmpty() && sources[i].m_alias.IsSameAs(e.m_path[0], false))
					return static_cast<int>(i);
		for (size_t i = 0; i < sources.size(); ++i)
			if (sources[i].m_q != nullptr && sources[i].m_q->ResolveColumnByName(e.m_path[0]) != nullptr)
				return static_cast<int>(i);
		return -1;
	};

	for (size_t i = 0; i < ast.m_joins.size(); ++i) {
		const ibQueryAstJoin& join = ast.m_joins[i];
		// ⚠ NO LINK IS NOT A MISTAKE, AND NOTHING HERE IS CHECKED ABOUT IT. A link either exists or it
		// does not; where it does not, the two tables are multiplied, and that is a complete, ordinary
		// query. This check is about links that CONTRADICT one another — where there is none, there is
		// nothing to contradict.
		//
		// (It refused one for a while, on the reading that a bare JOIN looks unfinished. It does not:
		// adding two tables and linking neither is the first thing anybody does, and the red line
		// under the tabs said the query was broken while it was merely young.)
		//
		// ⚠ AND THERE IS NO EXCEPTION FOR THE KIND EITHER. An outer join with no condition was refused
		// here for one build, on the reading that "all rows of X" is a sentence about a link. It is —
		// but a KIND left behind on a table whose link was deleted is not the author saying anything,
		// it is a leftover, and the red line then appeared over a Links tab with no rows in it at all.
		// The leftover is fixed where it is made (the kind goes back to inner with the link); nothing
		// is checked about a condition nobody wrote.
		if (!join.m_on)
			continue;
		// `ON TRUE` is the product said out loud — the same thing, so the same silence.
		if (join.m_on->m_kind == ibQueryAstExprKind::Literal)
			continue;

		// The table this link is written ON: sources[0] is the FROM, joins[k] is sources[k + 1].
		const int bound = static_cast<int>(i) + 1;
		if (bound >= static_cast<int>(sources.size()))
			continue;

		std::vector<const ibQueryAstExpr*> columns;
		CollectColumns(join.m_on, columns);

		bool mentionsJoined = false;
		bool mentionsOther  = false;
		for (const ibQueryAstExpr* column : columns) {
			const int root = column != nullptr ? rootOf(*column) : -1;
			if (root < 0)
				continue;   // unknown column — the name check owns that complaint

			// ⭐⭐ AND IT MAY NAME MORE THAN THE TWO SIDES — but only tables the query has ALREADY
			// read. A link over three tables is an ordinary thing to write (`a.x = b.y AND a.z =
			// c.w`): the sources are joined left to right, so everything before this one is standing
			// there to be compared against.
			//
			// What cannot work is naming a table that comes AFTER: at the moment this join is made
			// that table has not been read, and there is nothing to compare with. The reader cannot
			// see the difference — both look like a table of the same query — so the ORDER is what
			// makes two tables compatible here, and the engine is the one that knows it.
			if (root > bound)
				ThrowQueryException(join.m_on->m_line, join.m_on->m_col, wxString::Format(
					_("the link on '%s' refers to '%s', which this query reads AFTER it: a link can "
					  "only relate tables that are already read — move the table earlier, or write "
					  "this as a condition"),
					ibQuerySourceName(join.m_source),
					ibQuerySourceName(ast.m_joins[static_cast<size_t>(root) - 1].m_source)));

			if (root == bound) mentionsJoined = true;
			else               mentionsOther  = true;
		}

		if (!mentionsJoined && !mentionsOther)
			continue;   // nothing resolved — say nothing rather than guess

		if (!mentionsJoined)
			ThrowQueryException(join.m_on->m_line, join.m_on->m_col, wxString::Format(
				_("this link says nothing about '%s': a condition that does not relate the joined "
				  "table is a filter, and belongs in the conditions"),
				ibQuerySourceName(join.m_source)));

		if (!mentionsOther)
			ThrowQueryException(join.m_on->m_line, join.m_on->m_col, wxString::Format(
				_("'%s' is linked to itself: a link relates TWO tables, so each side has to stand "
				  "on a different one"),
				ibQuerySourceName(join.m_source)));
	}
}

// ⭐⭐ AN AGGREGATE NAMES A FIELD OF THE SELECTION — the rule, in ONE place, because TWO doors ask it.
// `TOTALS COUNT(Number)` over `SELECT Posted` would put a column called `Number` into a result whose
// author never asked for one (Max, 2026-08-22, seeing it appear: "wrong, I have no Number in the
// selection"). A LEVEL may group by anything: it adds depth and no column. An aggregate WRITES a
// column, and a column of the result has to have been declared by the SELECT that declares it.
//
// Living here rather than inside the lowering is the whole point: the constructor's verdict line
// resolves names through CheckNames and the run goes through ExecuteTotals, and a rule written into
// only one of them is a rule the other says nothing about — which is exactly what happened, the
// dialog cheerfully reporting "the query engine reads this query" about a query it does not.
//
// A selected field has TWO spellings and both are it: the alias (`Parent`) and the way the SELECT
// wrote it (`Catalog1.Parent`), which is what the old constructor put in these lines. So the test is
// "does some projection claim this path", by output name or by prefix. A dot-walk CONTINUES from a
// claimed field: `Parent.Description` is claimed by `SELECT Parent`, the walk starting inside the
// result. COUNT(*) names nothing and is always in.
// ⭐⭐ `OVER <name>` NAMES A GROUPING OF THIS VERY QUERY — checked HERE, where the window verifies a
// query, and not only when it runs. A misspelt area is the easiest mistake to make in the totals
// grid (a name typed by hand, or one that survived a grouping being renamed), and finding it at
// composition time means finding it in front of a report that shows nothing.
//
// The names are the ones the lowering resolves by: a level's alias, else its head field's name,
// qualified by the separator for the levels that sit on one.
void CheckTotalsScopeNames(const ibQuerySelect& ast)
{
	std::vector<wxString> known;
	const auto nameOf = [](const ibQueryTotalDim& level) -> wxString {
		if (!level.m_alias.IsEmpty())
			return level.m_alias;
		const ibQueryTotalField* head = level.Head();
		return head != nullptr && head->m_expr != nullptr && !head->m_expr->m_path.empty()
			? head->m_expr->m_path.back() : wxString();
	};
	for (const ibQueryTotalDim& level : ast.m_totalsBy)
		if (const wxString name = nameOf(level); !name.IsEmpty())
			known.push_back(name);
	for (const ibQueryTotalSplit& node : ast.m_totalsSplits)
		for (const ibQueryTotalDim& level : node.m_levels) {
			const wxString name = nameOf(level);
			if (name.IsEmpty())
				continue;
			known.push_back(name);
			if (!node.m_name.IsEmpty())
				known.push_back(node.m_name + wxT(".") + name);
		}

	for (const ibQueryTotalAggregate& resource : ast.m_totalsAggregates) {
		if (resource.m_scope.IsEmpty())
			continue;
		wxStringTokenizer names(resource.m_scope, wxT(","));
		while (names.HasMoreTokens()) {
			wxString one = names.GetNextToken();
			one.Trim(true).Trim(false);
			if (one.IsEmpty())
				continue;
			bool found = false;
			for (const wxString& had : known)
				if (had.IsSameAs(one, false)) { found = true; break; }
			if (!found)
				ThrowQueryException(0, 0,
					_("OVER \"%s\": this query groups by nothing of that name: name one of its own groupings, qualified by the separator (Separator.Grouping) where it sits on one"),
					one);
		}
	}
}

void CheckTotalsNameSelectedFields(const ibQuerySelect& ast)
{
	if (ast.m_totalsAggregates.empty() || ast.m_selectAll)
		return;

	std::map<wxString, const ibQueryProjection*, ibNoCaseLess> byOutputName;
	{
		int idx = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star || !p.m_expr) continue;
			byOutputName[OutputNameFor(ast, p, idx++)] = &p;
		}
	}

	auto claimed = [&ast, &byOutputName](const ibQueryAstExprPtr& expr) -> bool {
		if (!expr)
			return false;
		if (expr->m_kind != ibQueryAstExprKind::Column)
			return true;                                    // an expression stands for itself, not a field
		if (expr->m_path.empty())
			return false;
		if (byOutputName.find(expr->m_path.front()) != byOutputName.end())
			return true;                                    // by the name the result calls it
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star)
				return true;
			if (!p.m_expr || p.m_expr->m_kind != ibQueryAstExprKind::Column || p.m_expr->m_path.empty())
				continue;
			if (p.m_expr->m_path.size() > expr->m_path.size())
				continue;
			if (std::equal(p.m_expr->m_path.begin(), p.m_expr->m_path.end(), expr->m_path.begin(),
			               [](const wxString& a, const wxString& b) { return a.IsSameAs(b, false); }))
				return true;                                // spelled the way the SELECT spelled it
		}
		return false;
	};

	for (const ibQueryTotalAggregate& resource : ast.m_totalsAggregates) {
		const ibQueryAstExprPtr& agg = resource.m_expr;
		if (!agg || agg->m_kind != ibQueryAstExprKind::Func || agg->m_star || !agg->m_arg)
			continue;
		if (!claimed(agg->m_arg))
			ThrowQueryException(agg->m_arg->m_line, agg->m_arg->m_col,
				_("TOTALS aggregates \"%s\", which the selection does not carry: add it to SELECT and total over it there"),
				ibRenderQueryExpr(*agg->m_arg));
	}
}

void CheckSelectNames(const ibQuerySelect& astAsWritten, const std::map<wxString, ibValue>& params)
{
	// ⭐ JUDGE WHAT WILL RUN, NOT WHAT WAS TYPED. The execution reorders a run of INNER joins so a
	// link may name a table added after it (ibQueryRewrite::ReorderJoins — an OUTER join never moves,
	// a cycle is left alone). Checking the text in its written order would report a forward reference
	// the engine never hits, which is the worst kind of complaint: true about the sentence, false
	// about the query, and impossible for the author to act on.
	//
	// Only that one rule is applied. The rest of the optimizer rephrases the query, and a check must
	// not judge a rephrasing the author cannot see.
	const ibQuerySelectPtr ordered = astAsWritten.m_joins.size() > 1
		? ibQueryRewrite::ReorderJoins(astAsWritten) : nullptr;
	const ibQuerySelect& ast = ordered ? *ordered : astAsWritten;

	// A nested SELECT anywhere in an expression is checked with ITS own sources.
	auto checkNested = [&params](const ibQueryAstExprPtr& e) {
		std::vector<const ibQueryAstExpr*> unusedColumns;
		if (!e) return;
		if (e->m_subquery) CheckSelectNames(*e->m_subquery, params);
	};

	for (const ibQuerySelectPtr& branch : ast.m_unions)
		if (branch) CheckSelectNames(*branch, params);

	std::vector<ibSourceBinding> sources;
	if (!BuildCheckSources(ast, params, sources, /*reportMissing=*/true, /*tolerateOpaque=*/true)) {
		// ⚠ SILENCE IS FOR "CANNOT VERIFY", NOT FOR "NOTHING TO VERIFY AGAINST".
		//
		// A one-segment name may be a temp table made by a statement this check cannot see, so a
		// query whose sources did not resolve is left alone rather than accused. But a query with NO
		// TABLE AT ALL that still selects `SliceLast.Dimension2` is not unverifiable — it is plainly
		// wrong, and every one of its qualified fields is a leftover from a table that is gone.
		//
		// It passed silently, and the verdict line said the engine reads it. That is the one sentence
		// this window must never say untruthfully.
		const bool noTables = ast.m_joins.empty()
			&& ast.m_from.m_name.empty() && !ast.m_from.m_subquery;
		if (noTables) {
			std::vector<const ibQueryAstExpr*> columns;
			for (const ibQueryProjection& projection : ast.m_projections)
				CollectColumns(projection.m_expr, columns);
			for (const ibQueryAstExpr* column : columns)
				if (column != nullptr && column->m_path.size() >= 2) {
					wxString field;
					for (size_t k = 0; k < column->m_path.size(); ++k)
						field += (k > 0 ? wxT(".") : wxT("")) + column->m_path[k];
					ThrowQueryException(column->m_line, column->m_col, wxString::Format(
						_("this query reads no table, yet '%s' is selected from one: remove the field, "
						  "or add '%s' to the tables"), field, column->m_path[0]));
				}
		}
		return;   // unverifiable — silence, never a false error
	}

	CheckJoinsAreConsistent(ast, sources);

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
	for (const ibQueryTotalAggregate& aggregate : ast.m_totalsAggregates)
		CollectColumns(aggregate.m_expr, strict);
	// …and the aggregates answer one question the resolver cannot: whether the RESULT has a column
	// for the figure. Asked here so the dialog's verdict and the run agree.
	//
	// ⚠ ON THE FULLY REWRITTEN FORM, and this one rule is the exception to the paragraph at the top
	// of this function. Everywhere else a check judges what was WRITTEN, because complaining about a
	// rephrasing the author cannot see is useless to them. Here the rephrasing is what makes the
	// query legal: flattening a nested SELECT substitutes its aliases, so
	// `SELECT Q.a AS x FROM (SELECT b AS a FROM …) AS Q TOTALS SUM(a) BY x` names nothing the outer
	// text carries and everything the flattened query does. Judged as typed, the dialog refuses a
	// query the engine runs — the same false complaint, arrived at from the other side.
	const ibQuerySelectPtr asRun = ibQueryRewrite::Rewrite(astAsWritten);
	CheckTotalsNameSelectedFields(asRun ? *asRun : ast);
	CheckTotalsScopeNames(ast);   // …and that every OVER names a grouping this query declares
	for (const ibQueryTotalDim& dim : ast.m_totalsBy)
		for (const ibQueryTotalField& field : dim.m_fields)
			CollectColumns(field.m_expr, strict);
	for (const ibQueryAstJoin& join : ast.m_joins)
		CollectColumns(join.m_on, strict);
	for (const ibQueryOrderItem& item : ast.m_orderBy)
		CollectColumns(item.m_expr, ordering);

	// THE SAME RESOLVER THE EXECUTION USES. Its words are the ones the user sees — there is no
	// second definition here of what a known field is.
	// ⚠ A COLUMN THIS CHECK CANNOT ATTRIBUTE TO A VERIFIABLE SOURCE IS LEFT ALONE. With an opaque
	// source in the query (a nested select), a name it does not recognise may perfectly well be one of
	// THAT table's columns — inventing an error about it would be the old blanket silence's mistake
	// made in the other direction. Everything that DOES land on a source this check knows is checked.
	bool anyOpaque = false;
	for (const ibSourceBinding& binding : sources)
		if (binding.m_q == nullptr)
			anyOpaque = true;

	const auto checkable = [&sources, anyOpaque](const ibQueryAstExpr& column) {
		if (!anyOpaque)
			return true;   // nothing to be uncertain about
		if (column.m_path.empty())
			return false;
		// Qualified: the source it names decides — known table, checked; opaque one, silent.
		for (const ibSourceBinding& binding : sources)
			if (!binding.m_alias.IsEmpty() && binding.m_alias.IsSameAs(column.m_path[0], false))
				return binding.m_q != nullptr;
		// Unqualified: checked only if a source this check KNOWS owns it. Otherwise it may be the
		// opaque table's, and nothing can be said.
		for (const ibSourceBinding& binding : sources)
			if (binding.m_q != nullptr && binding.m_q->ResolveColumnByName(column.m_path[0]) != nullptr)
				return true;
		return false;
	};

	for (const ibQueryAstExpr* column : strict) {
		if (column == nullptr || column->m_path.empty())
			continue;
		if (!checkable(*column))
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
				ThrowQueryException(0, 0, wxString::Format(
					_("two output fields are called '%s': give one of them a different alias"), outputs[i]));

	for (const ibQueryAstExpr* column : ordering) {
		if (column == nullptr || column->m_path.empty())
			continue;
		bool namesAnOutput = false;
		if (column->m_path.size() == 1)
			for (const wxString& name : outputs)
				if (name.IsSameAs(column->m_path[0], false)) { namesAnOutput = true; break; }
		if (!namesAnOutput && checkable(*column))
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
			ThrowQueryException(expr->m_line, expr->m_col, wxString::Format(
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
				ThrowQueryException(ast.m_groupBy[i]->m_line, ast.m_groupBy[i]->m_col, wxString::Format(
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
		ThrowQueryException(first->m_line, first->m_col, wxString::Format(
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
	if (!BuildCheckSources(ast, params, sources, /*reportMissing=*/false))
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
		[&](const ibQueryTotalAggregate& r) { return gone(StillResolves(sources, r.m_expr)); }), ast.m_totalsAggregates.end());

	// A LEVEL LOSES THE FIELDS THAT NO LONGER RESOLVE, and only then, having none left, loses itself.
	// Dropping the whole level because one of its fields died would take the grouping away over a
	// field the author never touched.
	for (ibQueryTotalDim& d : ast.m_totalsBy)
		d.m_fields.erase(std::remove_if(d.m_fields.begin(), d.m_fields.end(),
			[&](const ibQueryTotalField& f) { return gone(StillResolves(sources, f.m_expr)); }), d.m_fields.end());

	ast.m_totalsBy.erase(std::remove_if(ast.m_totalsBy.begin(), ast.m_totalsBy.end(),
		[](const ibQueryTotalDim& d) { return d.m_fields.empty(); }), ast.m_totalsBy.end());

	// ⚠ THE NODES ARE PRUNED THE SAME WAY — a level whose field stopped resolving goes, and a node
	// left with nothing goes with it. Skipped here, a removed attribute would live on inside a
	// `SPLIT` and take the whole query down on the next run, which is exactly what this pass exists
	// to prevent.
	for (ibQueryTotalSplit& node : ast.m_totalsSplits) {
		for (ibQueryTotalDim& d : node.m_levels)
			d.m_fields.erase(std::remove_if(d.m_fields.begin(), d.m_fields.end(),
				[&](const ibQueryTotalField& f) { return gone(StillResolves(sources, f.m_expr)); }), d.m_fields.end());
		node.m_levels.erase(std::remove_if(node.m_levels.begin(), node.m_levels.end(),
			[](const ibQueryTotalDim& d) { return d.m_fields.empty(); }), node.m_levels.end());
	}

	bool nodesHoldLevels = false;
	for (const ibQueryTotalSplit& node : ast.m_totalsSplits)
		if (!node.m_levels.empty()) { nodesHoldLevels = true; break; }

	if (ast.m_totalsAggregates.empty() && ast.m_totalsBy.empty() && !nodesHoldLevels && !ast.m_totalsOverall)
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

std::vector<wxString> ibQueryLowering::AggregateCallsFor(const ibTypeDescription& type, const wxString& field)
{
	std::vector<wxString> calls;
	if (field.IsEmpty())
		return calls;

	for (const ibQueryKeyword keyword : AggregatesFor(type)) {
		calls.push_back(ibQueryKeywordText(keyword) + wxT("(") + field + wxT(")"));

		// …AND ITS DISTINCT FORM where that asks a DIFFERENT question — `COUNT(DISTINCT x)` counts how
		// many different values there are, `COUNT(x)` how many rows have one. WHICH functions get the
		// twin is the keyword table's answer (ibDistinctMattersFor), so MIN and MAX do not: their
		// result is the same value however often it occurs, and a padded list is one people stop
		// reading. The same rule the expression editor already follows.
		if (ibDistinctMattersFor(keyword))
			calls.push_back(ibQueryKeywordText(keyword) + wxT("(")
				+ ibQueryKeywordText(ibQueryKeyword::Distinct) + wxT(" ") + field + wxT(")"));
	}
	return calls;
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
	if (!BuildCheckSources(ast, params, sources, /*reportMissing=*/false))
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

// ⭐⭐ WHAT PREPARES, RUN — see the header. The COMPOSER's road: it lowers ONE statement of its own
// and needs the tables the statements before it MAKE to be standing when it does.
//
// ⚠ `INTO` AND `ONTO` ARE NOT TWO SPELLINGS OF ONE THING (Max, 2026-08-27: *"INTO is not in the
// link, and it should not be"*). A named result is a QUERY — the lowering declares it to the server
// and nothing runs early; a temp table is ROWS, and rows exist only once somebody has drained them.
// That is why a link relates names and never tables, and why this function executes one of the two
// and passes the other by.
void ibQueryLowering::PreparePackage(const ibQueryPackage& package,
	const std::map<wxString, ibValue>& params,
	ibQueryTempTableStore& store,
	std::map<wxString, const ibBackendQueryable*>& sources)
{
	// The names declared SO FAR — a preparing statement may read a selection named above it, which
	// is an ordinary thing to write and costs nothing here (the lowering resolves it).
	std::map<wxString, const ibQuerySelect*> named;
	ibNamedResultScope namedScope(named);

	for (const ibQueryAstStatement& statement : package.m_statements) {

		if (statement.IsDrop()) {
			// Releasing EARLY — said out loud instead of left to scope, exactly as a package does.
			if (!store.Drop(statement.m_dropTemp))
				ThrowQueryException(0, 0, wxString::Format(
					_("temporary table '%s' does not exist"), statement.m_dropTemp));
			sources.erase(statement.m_dropTemp);   // …and out of the registry with it, not left dangling
			continue;
		}

		if (!statement.m_select)
			continue;
		const ibQuerySelect& ast = *statement.m_select;

		if (ast.m_intoTemp.IsEmpty()) {
			// NOT RUN. A named result is read where it is used; an unnamed statement prepares
			// nothing and is nobody's source — running either here would be work for no reader.
			//
			// ⚠ Registered AFTER it is passed, for the reason ExecutePackage states at length: a
			// name visible while its own statement is lowered resolves to itself, without end.
			if (!ast.m_ontoName.IsEmpty() && !named.emplace(ast.m_ontoName.Lower(), statement.m_select.get()).second)
				ThrowQueryException(0, 0, wxString::Format(
					_("two statements of this package name their result '%s'"), ast.m_ontoName));
			continue;
		}

		if (store.Has(ast.m_intoTemp))
			ThrowQueryException(0, 0, wxString::Format(
				_("temporary table '%s' already exists"), ast.m_intoTemp));

		std::vector<OutputColumn> schema;
		ibDataQueryResult read = ast.m_hasTotals
			? ExecuteTotals(ast, params, schema, ibReadPageRequest{}, nullptr, /*withDetails*/true)
			: Execute(ast, params, schema);

		ibQueryRamTable snapshot = DrainIntoSnapshot(read, schema);
		const long rows = snapshot.RowCount();

		// INDEX BY — the columns the store builds a lookup over, named as the OUTPUT names, because
		// that is what the statements after it select by.
		std::vector<wxString> indexed;
		for (const ibQueryAstExprPtr& column : ast.m_indexBy)
			if (column && !column->m_path.empty())
				indexed.push_back(column->m_path.back());

		store.Put(ast.m_intoTemp, std::move(snapshot), indexed);

		// ⭐ AND INTO THE REGISTRY THE SCOPE IS OPEN OVER, so the next statement — and the query this
		// was all prepared for — resolve the name straight to the table.
		const auto made = store.Sources().find(ast.m_intoTemp);
		if (made != store.Sources().end())
			sources[made->first] = made->second;

		ibJournalInfo(wxT("query"), wxT("prepared '%s': %ld rows"), ast.m_intoTemp, rows);
	}
}

// ⭐⭐ THE PLACEMENT — see the header. `LINK` declares relations as a SET; a query needs a TREE, and
// working the tree out is this function's whole job. It answers for BOTH readers of it: the package
// running here, and the COMPOSER writing its settings over a linked package as text.
ibQueryLowering::FromTree ibQueryLowering::PlacePackageLinks(
	const std::vector<ibQueryPackageLink>& links, const std::vector<wxString>& declared)
{
	FromTree tree;

	auto isDeclared = [&declared](const wxString& name) {
		for (const wxString& had : declared)
			if (had.IsSameAs(name, false))
				return true;
		return false;
	};

	// A HALF-FILLED ROW SAYS NOTHING YET — the author opened it and has not written it, which is
	// an ordinary state of the window and not a mistake.
	//
	// ⚠ A NAME THIS PACKAGE DOES NOT DECLARE IS A MISTAKE, and it is said out loud: skipping it
	// quietly runs a DIFFERENT query — one link fewer — and hands back numbers nobody can explain.
	// Everywhere else in this lowering an unknown name raises; a link is no exception.
	std::vector<const ibQueryPackageLink*> pending;
	for (const ibQueryPackageLink& link : links) {
		if (link.m_left.IsEmpty() || link.m_right.IsEmpty() || !link.m_on)
			continue;
		for (const wxString& side : { link.m_left, link.m_right })
			if (!isDeclared(side))
				ThrowQueryException(0, 0, wxString::Format(
					_("the link names '%s', which is not a result this package names"), side));
		pending.push_back(&link);
	}
	if (pending.empty())
		return tree;

	std::vector<wxString> present;
	auto reads = [&present](const wxString& name) {
		for (const wxString& had : present)
			if (had.IsSameAs(name, false))
				return true;
		return false;
	};

	// THE LINKS ARE A GRAPH, NOT A LIST, so they are placed until nothing else can be placed —
	// not in one pass down the vector. Written in any order, `[A-B, C-D, B-C]` is one chain; a
	// single pass drops the middle link (neither side placed yet when it is read) and D never
	// enters the query at all, silently.
	while (!pending.empty()) {
		bool placed = false;
		for (size_t i = 0; i < pending.size(); ) {
			const ibQueryPackageLink& link = *pending[i];

			if (present.empty()) {
				tree.m_head = link.m_left;
				present.push_back(link.m_left);
			}
			const bool hasLeft  = reads(link.m_left);
			const bool hasRight = reads(link.m_right);

			if (!hasLeft && !hasRight) { ++i; continue; }   // nothing to hang it off YET

			// ⭐ BOTH SIDES ALREADY IN — this is a SECOND condition between two selections that
			// are already joined, and it must not be dropped: dropping it widens the result by
			// exactly the rows the author wrote it to exclude. It is ANDed into the step that
			// brought the later of them in.
			if (hasLeft && hasRight) {
				for (JoinStep& step : tree.m_steps) {
					if (!step.m_name.IsSameAs(link.m_left, false) && !step.m_name.IsSameAs(link.m_right, false))
						continue;
					if (step.m_on) {
						ibQueryAstExprPtr both = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
						both->m_isOr = false;
						both->m_lhs  = step.m_on;
						both->m_rhs  = link.m_on;
						step.m_on    = both;
					}
					else {
						step.m_on = link.m_on;
					}
					break;
				}
				pending.erase(pending.begin() + i);
				placed = true;
				continue;
			}

			JoinStep step;
			step.m_name = hasLeft ? link.m_right : link.m_left;
			step.m_kind = link.m_kind;
			// SHARED, not copied: whoever lowers this rewrites a deep clone of what it is given, so
			// nothing downstream can reach back into the author's link.
			step.m_on   = link.m_on;
			present.push_back(step.m_name);
			tree.m_steps.push_back(std::move(step));
			pending.erase(pending.begin() + i);
			placed = true;
		}

		// ⚠ NOTHING MOVED AND SOMETHING IS LEFT: what remains relates selections this query does
		// not reach — a second, disconnected group. Joining it in would be a cross product
		// nobody asked for, so the package says so instead.
		if (!placed) {
			ThrowQueryException(0, 0, wxString::Format(
				_("the link between '%s' and '%s' relates selections this package does not join to the rest"),
				pending.front()->m_left, pending.front()->m_right));
		}
	}
	return tree;
}

namespace {

// ⭐⭐ THE LINK, AS A STEP: RECONCILE TWO NAMED SELECTIONS THAT ALREADY RAN.
//
// Max, 2026-09-04: *"without a link the two selections are as they are; with a link even the TOTALS
// change, since we selected them by one another"* — and *"the LINK operator processes the named
// selection and itself returns the number of rows it changed"*.
//
// So it does not build a third table. It takes the rows each side already produced and drops those
// that found no partner on the other; what remains on both sides is the same set of facts, seen
// twice. The totals follow for free: a TOTALS result is folded from its DETAIL rows when it is
// walked, so removing details is all that "the totals change" needs.
//
// ⚠ WHICH SIDE LOSES ROWS is the join kind, read the way anyone reads a join: INNER reconciles BOTH
// sides, LEFT keeps every left row and cleans the right, RIGHT the mirror, FULL removes nothing (it
// asks for everything on both sides, which is what the selections already are).
struct LinkedSide
{
	ibQueryLowering::PackageResult* m_result = nullptr;
	ibQueryRamTable                 m_rows;
	std::vector<wxString>           m_keyColumns;   // in step with the other side's list
};

// The columns each side is matched on, read off the ON condition: an AND-chain of `A.x = B.y`.
// Anything else is refused out loud — a link nobody can evaluate must not pass as a link that
// removed nothing.
bool ReadLinkKeys(const ibQueryPackageLink& link, std::vector<wxString>& leftCols,
                  std::vector<wxString>& rightCols, wxString& refusal)
{
	if (!link.m_on) {
		refusal = _("a link needs its ON condition before it can reconcile anything");
		return false;
	}
	std::vector<ibQueryAstExprPtr> terms;
	ibQueryFlattenAnd(link.m_on, terms);

	for (const ibQueryAstExprPtr& term : terms) {
		if (!term || term->m_kind != ibQueryAstExprKind::Compare || term->m_cmp != ibQueryCompareOp::Eq
		 || !term->m_lhs || !term->m_rhs
		 || term->m_lhs->m_kind != ibQueryAstExprKind::Column || term->m_rhs->m_kind != ibQueryAstExprKind::Column
		 || term->m_lhs->m_path.size() != 2 || term->m_rhs->m_path.size() != 2) {
			refusal = _("a link is matched on equalities between the two selections' fields "
			            "(Sales.Item = Plan.Item), joined by AND");
			return false;
		}
		const wxString& lq = term->m_lhs->m_path[0];
		const wxString& rq = term->m_rhs->m_path[0];
		if (lq.IsSameAs(link.m_left, false) && rq.IsSameAs(link.m_right, false)) {
			leftCols.push_back(term->m_lhs->m_path[1]);
			rightCols.push_back(term->m_rhs->m_path[1]);
		}
		else if (lq.IsSameAs(link.m_right, false) && rq.IsSameAs(link.m_left, false)) {
			leftCols.push_back(term->m_rhs->m_path[1]);
			rightCols.push_back(term->m_lhs->m_path[1]);
		}
		else {
			refusal = wxString::Format(
				_("this link relates '%s' and '%s', and its condition names something else"),
				link.m_left, link.m_right);
			return false;
		}
	}
	return !leftCols.empty();
}

// One side's cell, by the OUTPUT name the selection published.
ibValue LinkCell(const ibQueryRamTable& rows, long row, const wxString& column)
{
	for (const ibQueryRamColumn& c : rows.Columns())
		if (c.m_name.IsSameAs(column, false))
			return rows.GetCell(row, c.m_id);
	return ibValue();
}

// …and the whole key of a row: the tuple its side is matched by. Compared as VALUES (ibValueSeqHash),
// never as rendered text — two references that display alike are still two references.
std::vector<ibValue> LinkKey(const ibQueryRamTable& rows, long row, const std::vector<wxString>& columns)
{
	std::vector<ibValue> key;
	key.reserve(columns.size());
	for (const wxString& c : columns)
		key.push_back(LinkCell(rows, row, c));
	return key;
}

// Keep the rows whose key is in `wanted`; returns how many were dropped. The schema is rewritten to
// read BY NAME, because what is handed back is now this table and not the source the rows came from.
long KeepMatched(ibQueryLowering::PackageResult& side, const ibQueryRamTable& rows,
                 const std::vector<wxString>& keyColumns,
                 const std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual>& wanted)
{
	ibQueryRamTable kept;
	for (const ibQueryRamColumn& c : rows.Columns())
		kept.AddColumn(c.m_id, c.m_name, c.m_type);

	long dropped = 0;
	for (long r = 0; r < rows.RowCount(); ++r) {
		if (wanted.find(LinkKey(rows, r, keyColumns)) == wanted.end()) { ++dropped; continue; }
		const long into = kept.AppendRow();
		for (const ibQueryRamColumn& c : rows.Columns())
			kept.SetCell(into, c.m_id, rows.GetCell(r, c.m_id));
	}

	side.m_result = std::make_unique<ibDataQueryResult>(std::move(kept), nullptr);
	for (OutputColumn& oc : side.m_schema) {
		oc.m_alias   = oc.m_name;   // the snapshot is named by the OUTPUT names — read it by them
		oc.m_byAlias = true;
		oc.m_col     = nullptr;
		oc.m_objectPrefix.clear();
	}
	return dropped;
}

} // namespace

std::vector<ibQueryLowering::PackageResult> ibQueryLowering::ExecutePackage(
	const ibQueryPackage& package, const std::map<wxString, ibValue>& params,
	ibQueryTempTableStore* store)
{
	std::vector<PackageResult> results;

	// ⭐⭐ EVERY QUERY THE PLATFORM RUNS PASSES HERE, and this is where the technology journal sees
	// it. Not at the script's `Query.Execute()` — that door only knows the ones a person typed. A
	// report builds its query, a dynamic list rebuilds one per page, a LINQ block records one from
	// a lambda, a register reads its totals: none of those was ever a string, and all of them arrive
	// at this function.
	//
	// So the AST is RENDERED BACK to text rather than the source being echoed. What is journalled is
	// then what the engine is about to run — after rewriting, after the settings were folded in —
	// which is the query a person actually needs to see when the answer looks wrong.
	ibJournalInfo(wxT("query"), wxT("run:\n%s"), ibRenderQueryPackage(package));

	// WHO KEEPS THE TABLES ALIVE. Without a store the package owns one for its own run: a single
	// query's temp scope is RAII-bound to ONE execution, so statement 3 would never see what
	// statement 2 left. With a store handed in (a script's TempTablesManager), the tables outlive
	// this call and several separate queries share them — the same mechanism, a different holder.
	//
	// The scope holds a POINTER to the map either way, so entries added mid-package are visible to
	// every statement after them, which IS the batch contract.
	// ⚠ SHARED, NOT A LOCAL. The tables die when the last READER lets go, not when this function
	// returns — the results handed back point at this store's columns and are read afterwards. See
	// PackageResult::m_temps for what a stack local cost here.
	const std::shared_ptr<ibQueryTempTableStore> ownStore =
		store != nullptr ? nullptr : std::make_shared<ibQueryTempTableStore>();
	ibQueryTempTableStore& temps = store != nullptr ? *store : *ownStore;
	ibTempSourceScope packageScope(temps.Sources());

	// THE RESULTS THIS PACKAGE HAS NAMED so far, by their ONTO name (lower-cased: names in this
	// language are matched without regard to case). A later statement may read one of them, and
	// reading it is what a RESULT LINK is.
	std::map<wxString, const ibQuerySelect*> named;

	for (const ibQueryAstStatement& statement : package.m_statements) {

		if (statement.IsDrop()) {
			// Releasing EARLY — said out loud instead of left to scope. Dropping a name that was
			// never made is an error, not a shrug: it is either a typo or a statement that was
			// expected to run and did not.
			if (!temps.Drop(statement.m_dropTemp))
				ThrowQueryException(0, 0, wxString::Format(_("temporary table '%s' does not exist"), statement.m_dropTemp));
			PackageResult r;
			r.m_dropTemp = statement.m_dropTemp;
			results.push_back(std::move(r));
			continue;
		}

		// ⭐⭐ A LINK RUNS WHERE IT STANDS. It reconciles two selections this package has already
		// named, in place — see ApplyPackageLink's note — and answers from its own position with the
		// number of rows it removed, exactly as an INTO answers with the number it wrote.
		if (statement.IsLink()) {
			PackageResult r;

			// ⭐⭐ THE WHOLE SECTION RUNS HERE, not one relation of it. A chain is written with its
			// head said once (`LINK Sales LEFT JOIN Plan ON … JOIN Stock ON …`) and therefore stands
			// in ONE place in the package's sequence — so the statement covers every relation
			// sharing that head, and answers with the total it removed.
			//
			// 🛑 One statement per RELATION was the first shape, and it made the number of
			// statements depend on how many JOINs a section happened to carry — invisible in the
			// text, and it left the second relation of a chain with nobody to execute it once the
			// statement became the section's (2026-09-04).
			const std::size_t firstLink = static_cast<std::size_t>(statement.m_linkIndex);
			const wxString sectionHead = package.m_links[firstLink].m_left;

			long removedInSection = 0;

			for (std::size_t li = firstLink; li < package.m_links.size(); ++li) {

			const ibQueryPackageLink& link = package.m_links[li];
			if (!link.m_left.IsSameAs(sectionHead, false))
				break;

			auto findNamed = [&results](const wxString& name) -> PackageResult* {
				for (PackageResult& done : results)
					if (!done.m_name.IsEmpty() && done.m_name.IsSameAs(name, false) && done.m_result)
						return &done;
				return nullptr;
			};
			PackageResult* left  = findNamed(link.m_left);
			PackageResult* right = findNamed(link.m_right);

			// ⚠ SAID, NOT SHRUGGED: a link naming a selection that has not run (or was never named)
			// is a mistake in the text, and answering "removed nothing" would hide it.
			if (left == nullptr || right == nullptr)
				ThrowQueryException(0, 0, wxString::Format(
					_("the link names '%s' and '%s'; a link reconciles selections this package NAMED with "
					  "ONTO, and they must be written before it"), link.m_left, link.m_right));

			std::vector<wxString> leftCols, rightCols;
			wxString refusal;
			if (!ReadLinkKeys(link, leftCols, rightCols, refusal))
				ThrowQueryException(0, 0, refusal);

			ibQueryRamTable leftRows  = DrainIntoSnapshot(*left->m_result,  left->m_schema);
			ibQueryRamTable rightRows = DrainIntoSnapshot(*right->m_result, right->m_schema);

			std::unordered_set<std::vector<ibValue>, ibValueSeqHash, ibValueSeqEqual> leftKeys, rightKeys;
			for (long r0 = 0; r0 < leftRows.RowCount(); ++r0)
				leftKeys.insert(LinkKey(leftRows, r0, leftCols));
			for (long r0 = 0; r0 < rightRows.RowCount(); ++r0)
				rightKeys.insert(LinkKey(rightRows, r0, rightCols));

			// Which side the kind cleans — read as any join is read.
			const bool cleanLeft  = link.m_kind == ibQueryJoinKindAst::Inner || link.m_kind == ibQueryJoinKindAst::Right;
			const bool cleanRight = link.m_kind == ibQueryJoinKindAst::Inner || link.m_kind == ibQueryJoinKindAst::Left;

			long removed = 0;
			removed += cleanLeft  ? KeepMatched(*left,  leftRows,  leftCols,  rightKeys)
			                      : KeepMatched(*left,  leftRows,  leftCols,  leftKeys);
			removed += cleanRight ? KeepMatched(*right, rightRows, rightCols, leftKeys)
			                      : KeepMatched(*right, rightRows, rightCols, rightKeys);

			removedInSection += removed;

			ibJournalInfo(wxT("query"), wxT("link %s <-> %s: %ld row(s) removed"),
			              link.m_left, link.m_right, removed);
			}

			// ⭐ THE SECTION'S OWN ANSWER: how many rows this link removed, in total, from the
			// selections it reconciled (Max, 2026-09-04: *"a link returns either Undefined, or how
			// many rows it filtered out"*).
			ibQueryRamTable counted;
			counted.AddColumn(1, wxT("Count"), ibTypeDescription());
			counted.SetCell(counted.AppendRow(), 1, ibValue(static_cast<int>(removedInSection)));
			r.m_result = std::make_unique<ibDataQueryResult>(std::move(counted), nullptr);

			OutputColumn oc;
			oc.m_name = wxT("Count"); oc.m_alias = wxT("Count"); oc.m_byAlias = true;
			r.m_schema.push_back(oc);

			results.push_back(std::move(r));
			continue;
		}

		if (!statement.m_select)
			continue;

		// ⭐⭐ QUERY RESULT LINKS (Max, 2026-08-21).
		//
		// A statement may READ a result an earlier statement named with ONTO: `FROM Sales AS S INNER
		// JOIN Plan AS P ON …`. The name is resolved HERE, by putting that statement's own select in
		// as a SUBQUERY — so the link becomes an ordinary join and the whole thing goes to the DBMS
		// as ONE query. Nothing is materialised and nothing comes back to us in between.
		//
		// This is what replaces the "data sets + links between sets" machinery other composition
		// systems grow: there are statements, their results have names, and a link is a join.
		//
		// ⚠ NOT the same as INTO. INTO makes a table (and hands back a row count); a named result
		// stays a result.
		//
		// ⭐⭐ AND THE NAME IS RESOLVED BY THE LOWERING, NOT SUBSTITUTED HERE. It used to be
		// substituted here — the named statement's AST cloned into the reader's FROM as a nested
		// source — and that road has ONE execution model: a nested source is computed in RAM
		// (ibSubqueryQueryable::IsComputedInRam), so the rows came back to us and the join happened
		// here. Which road to take depends on what the ENGINE can do (`WITH`) and on what the named
		// query IS, and both are things only the lowering knows. So the names travel to it in a
		// scope, exactly as transient sources do, and it decides per source (ResolveFrom):
		// declare it to the server, or take its rows.
		ibNamedResultScope namedScope(named);

		// ⚠ A STATEMENT IS LEFT EXACTLY AS IT WAS WRITTEN. A package statement PREPARES a selection;
		// grafting another statement's source into it — which one build of this did — mixes two
		// prepared selections into one query nobody wrote (Max, 2026-08-21, on seeing it). What the
		// links mean is assembled AFTER the statements, as the package's FINAL query (below).
		const ibQuerySelect& ast = *statement.m_select;

		PackageResult r;
		r.m_hasTotals = ast.m_hasTotals;
		// ONTO — the name this statement gave its result. Carried out with the result so a caller
		// asks by name; an INTO statement never has one (the parser refuses the pair).
		r.m_name = ast.m_ontoName;
		std::vector<OutputColumn> schema;
		// ⭐⭐ WITH THE ROWS UNDER THE HEADINGS — always, on this road. A TOTALS result is levels ABOVE
		// data, and the data is the bottom of it: three `BY` levels over a catalogue give four rows
		// per item, the last one being the item itself (Max, 2026-08-22, on a measured result: 284
		// items x 4 = 1136 rows). Without this the deepest heading had nothing under it and a walk
		// that descended to the bottom found an empty selection.
		//
		// The COMPOSER asks per output (it knows whether that output prints rows); a hand-written
		// query has nobody to ask, and its author, having named the levels, means the rows they stand
		// over. Defaulting the other way made the same query text mean two different trees depending
		// on which door it came through.
		//
		// ⚠ THE PRICE, PAID KNOWINGLY: details and the server-side fold are exclusive — `GROUP BY
		// ROLLUP` returns aggregated rows and no detail to hang — so a scripted TOTALS folds in RAM.
		// That is the same road it takes today anyway (Firebird has no ROLLUP), and the big reports
		// go through the composer, which still chooses.
		ibDataQueryResult read = ast.m_hasTotals
			? ExecuteTotals(ast, params, schema, ibReadPageRequest{}, nullptr, /*withDetails*/true)
			: Execute(ast, params, schema);

		// ⚠⚠ REGISTERED AFTER ITS OWN READ, and that is not a detail. The name travels to the
		// lowering in a scope now (ibNamedResultScope), and the lowering RESOLVES a bare source
		// against it — so a name visible while its own statement is being lowered means
		// `SELECT … FROM Sales … ONTO Sales` resolves to itself, and the resolution recurses without
		// end. A statement cannot read the result it is producing; registering afterwards is what
		// says so, and the statements below still see it — they are lowered later.
		//
		// (The temp-table road has always carried the same guard, in its own words: a temp source is
		// resolved as of its MAKER, which is what stops one resolving through itself.)
		// ⚠⚠ AND A NAME IS DECLARED ONCE. Overwriting silently is not a tidier answer: with two
		// statements naming the same result, a reader gets whichever came last, and a package can
		// name a CYCLE (`… ONTO A; … FROM A ONTO B; … FROM B ONTO A`) whose resolution calls itself
		// until the stack ends — a crash, not an error message. The window checks uniqueness as a
		// name is typed; text written by hand and packages built by script come in through here.
		if (!ast.m_ontoName.IsEmpty() && !named.emplace(ast.m_ontoName.Lower(), statement.m_select.get()).second)
			ThrowQueryException(0, 0, wxString::Format(
				_("two statements of this package name their result '%s'"), ast.m_ontoName));

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
			ThrowQueryException(0, 0, wxString::Format(_("temporary table '%s' already exists"), ast.m_intoTemp));

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
				wxT("Count"), 0, ibBackendColumnRawDB::RawType::Number);
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

	// ⭐⭐ THE FINAL QUERY, ASSEMBLED FROM THE LINKS — and it is the ONLY place two selections meet.
	//
	// Max, 2026-08-21: the statements PREPARE the selections and know nothing about each other; at
	// the end you say how they join, two ready tables are related, and the value goes on. So nothing
	// is grafted into a statement and nothing is materialised: the package's last act is a query
	// whose sources ARE the named selections, joined exactly as the links say.
	//
	// The names resolve through the scope below — declared to the server as `WITH` where the engine
	// reads a named query, read as rows where it does not (ResolveFrom decides). Either way no
	// temporary table is made for a selection, named or otherwise.
	if (!package.m_links.empty() && !named.empty()) {
		ibNamedResultScope namedScope(named);

		// WHERE EACH SELECTION STANDS — worked out by the placer, which is also what the COMPOSER
		// asks when it writes its settings over a linked package. One answer to one question.
		std::vector<wxString> declared;
		for (const auto& entry : named)
			declared.push_back(entry.first);
		const FromTree tree = PlacePackageLinks(package.m_links, declared);

		auto asSource = [](const wxString& name) {
			ibQuerySource source;
			source.m_name.push_back(name);
			// NAMED BY ITS OWN NAME, because that is what a link's condition qualifies its fields
			// with (`T1.Ref = T2.Ref`); any other alias would leave the condition naming a source
			// this query does not have.
			source.m_alias = name;
			return source;
		};

		ibQuerySelect final;
		std::vector<wxString> present;

		if (!tree.m_head.IsEmpty()) {
			final.m_from = asSource(tree.m_head);
			present.push_back(tree.m_head);
			for (const JoinStep& step : tree.m_steps) {
				ibQueryAstJoin join;
				join.m_source = asSource(step.m_name);
				join.m_kind   = step.m_kind;
				// SHARED, not copied: the lowering rewrites a deep clone of whatever it is given, so
				// nothing downstream can reach back into the author's link.
				join.m_on     = step.m_on;
				final.m_joins.push_back(std::move(join));
				present.push_back(step.m_name);
			}
		}

		// ⭐ WHAT THE FINAL QUERY SELECTS — every selection's fields, each under ITS OWN NAME.
		//
		// `SELECT *` over two joined selections publishes both sides' columns under their bare names,
		// so two `Partner` columns answer to one name and a read by name lands on whichever came
		// first — the right-hand key silently reading the left-hand value. Qualifying by the
		// selection is what the author already writes in the link's condition (`Sales.Partner`), so
		// it is the same vocabulary and not a new one.
		// The names come from the SELECTS THEMSELVES — the statements that produced them are right
		// here, and what a select publishes is what its projections are called. No constructor model
		// is asked: that is the window's view of a query, and this is the engine running one.
		bool everyNameKnown = true;
		for (const wxString& name : present) {
			const auto found = named.find(name.Lower());
			const ibQuerySelect* source = found != named.end() ? found->second : nullptr;
			if (source == nullptr || source->m_selectAll || source->m_projections.empty()) {
				everyNameKnown = false;   // `SELECT *` inside — its fields are not named here
				break;
			}
			int index = 0;
			for (const ibQueryProjection& projection : source->m_projections) {
				if (projection.m_star || !projection.m_expr)
					continue;
				const wxString field = OutputNameFor(*source, projection, index++);
				if (field.IsEmpty())
					continue;
				ibQueryProjection qualified;
				qualified.m_expr  = ibQueryColumnFromPath(name + wxT(".") + field);
				qualified.m_alias = name + wxT("_") + field;
				final.m_projections.push_back(std::move(qualified));
			}
		}
		// ⚠ ALL OR NOTHING. A half-qualified list would publish one side by name and the other not at
		// all; the star is the honest fallback when a selection does not name its own fields, and the
		// duplicate-name hazard is then the author's own `SELECT *`, visible in their text.
		if (!everyNameKnown) {
			final.m_projections.clear();
			final.m_selectAll = true;
		}

		// ⛔ AND NO THIRD TABLE IS PRODUCED HERE ANY MORE.
		//
		// This road used to append a FINAL result — the query assembled from the links — and that was
		// the wrong shape twice over. It answered a position no statement was written at, in an array
		// whose whole contract is "one entry per statement"; and it left the named selections exactly
		// as they were, so the author saw their own two results unchanged beside a third they had not
		// asked for (Max, 2026-09-04: *"it is more logical to work with the ones that exist than to
		// pile on something new"* — and the link "returns the number of rows it changed").
		//
		// A LINK is a STEP now (see the statement branch above): it reconciles the two selections in
		// place and answers with what it removed. The tree built here is still worth building — the
		// composer and the constructor ask for exactly this placement (PlacePackageLinks) — so the
		// walk above stays and only the extra result is gone.
	}

	// ⭐ AND EVERY RESULT TAKES A SHARE OF THE TABLES IT READS FROM — stated ONCE, here, rather than
	// at each of the four places a result is built: a share that has to be remembered per branch is
	// a share the next branch forgets. Null store = a caller's TempTablesManager owns them already.
	if (ownStore)
		for (PackageResult& r : results)
			r.m_temps = ownStore;

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
		ThrowQueryException(0, 0, _("SELECT * needs a table to read: name one after FROM"));

	ibQueryRamTable table;
	std::vector<ibValue> values;
	ibMetaID nextId = 0;

	int index = 0;
	for (const ibQueryProjection& projection : ast.m_projections) {
		if (!projection.m_expr)
			continue;
		if (projection.m_expr->m_kind == ibQueryAstExprKind::Column)
			ThrowQueryException(projection.m_expr->m_line, projection.m_expr->m_col, wxString::Format(
				_("'%s' is a field, and this query reads no table: name one after FROM"),
				projection.m_expr->m_path.empty() ? wxString() : projection.m_expr->m_path.back()));

		// EvalValue answers a literal, a parameter and the arithmetic / CASE built out of them —
		// which is exactly the set a query with no rows behind it can mean.
		const ibValue value = EvalValue(*projection.m_expr, params);
		const wxString name = OutputNameFor(ast, projection, index++);

		auto column = std::make_shared<ibSyntheticScalarColumn>(name, nextId++, ibBackendColumnRawDB::RawType::String);
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
			ThrowQueryException(0, 0, _("SELECT * needs a table to read: name one after FROM"));

		ibMetaID nextId = 0;
		int index = 0;
		for (const ibQueryProjection& projection : ast.m_projections) {
			if (!projection.m_expr)
				continue;
			if (projection.m_expr->m_kind == ibQueryAstExprKind::Column)
				ThrowQueryException(projection.m_expr->m_line, projection.m_expr->m_col, wxString::Format(
					_("'%s' is a field, and this query reads no table: name one after FROM"),
					projection.m_expr->m_path.empty() ? wxString() : projection.m_expr->m_path.back()));

			const wxString name = OutputNameFor(ast, projection, index++);
			auto column = std::make_shared<ibSyntheticScalarColumn>(name, nextId++, ibBackendColumnRawDB::RawType::String);

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
	std::vector<ibQueryAstExprPtr> sourceConditions;   // conditions written INSIDE a virtual table call
	BuildSourceTree(ast, params, subOwners, sources, b, &sourceConditions);

	// AND HERE IT STOPS. PopulateBuilder is where names become columns and the output schema is
	// decided; the terminal below it (Execute / SelectAggregate) is where rows are read. Describing
	// is the first half without the second.
	PopulateBuilder(ast, params, sources, b, outSchema, /*asSubquery*/false, sourceConditions);
	DetachSchemaFromRunSources(outSchema, subOwners);   // the description leaves; the sources do not
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
		ThrowQueryException(0, 0, _("hierarchical TOTALS execution goes through ExecuteTotals, not Execute"));

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
	if (!ast.m_unions.empty()) {
		ibDataQueryResult stacked = LowerUnion(ast, params, outSchema, subOwners);
		DetachSchemaFromRunSources(outSchema, subOwners);   // the schema leaves, sharing what it names
		return stacked;
	}

	std::vector<ibSourceBinding> sources;
	ibDataQueryBuilder b;
	std::vector<ibQueryAstExprPtr> sourceConditions;
	BuildSourceTree(ast, params, subOwners, sources, b, &sourceConditions);

	// SELECT ALLOWED — carried to the door, which owns what a refusal turns into.
	b.Allowed(ast.m_allowed);

	// …and the outputs no engine can be asked for — an expression standing OVER a fold. They are
	// collected while the projections are read and handed to the RESULT, which is what evaluates them.
	std::vector<ibQueryColumnSelect> computedOverRow;
	const bool aggregate = PopulateBuilder(ast, params, sources, b, outSchema, /*asSubquery*/false,
	                                       sourceConditions, &computedOverRow);

	if (aggregate) {
		// SELECT TOP n + GROUP BY — the door's aggregate-terminal row limit: the DB / co-located
		// paths render the dialect LIMIT, the RAM fold truncates after grouping.
		if (ast.m_top > 0)
			b.Top(ast.m_top);
		ibDataQueryResult aggregated = b.SelectAggregate();
		if (!computedOverRow.empty())
			aggregated.SetComputedOverRow(std::move(computedOverRow));
		DetachSchemaFromRunSources(outSchema, subOwners);   // the schema leaves, sharing what it names
		return aggregated;
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
	ibDataQueryResult rows = cache != nullptr ? b.Execute(page, *cache, signature) : b.Execute(page);
	if (!computedOverRow.empty())
		rows.SetComputedOverRow(std::move(computedOverRow));
	DetachSchemaFromRunSources(outSchema, subOwners);   // the schema leaves, sharing what it names
	return rows;
}

//////////////////////////////////////////////////////////////////////
// ibQueryLowering::ExecuteTotals — hierarchical subtotals (TOTALS … BY …)
//////////////////////////////////////////////////////////////////////

ibDataQueryResult ibQueryLowering::ExecuteTotals(const ibQuerySelect& astIn,
                                                 const std::map<wxString, ibValue>& params,
                                                 std::vector<OutputColumn>& outSchema,
                                                 const ibReadPageRequest& page,
                                                 bool* outServerGroupedLevel,
                                                 bool withDetails,
                                                 const ibTotalsLayout& layout)
{
	// Same optimizer pass as Execute — the totals path benefits from a flattened FROM
	// and a normalized WHERE the same way. (queryRewrite.h)
	const ibQuerySelectPtr astOpt = ibQueryRewrite::Rewrite(astIn);
	const ibQuerySelect& ast = *astOpt;

	// OVERALL ON ITS OWN IS A WHOLE TOTALS QUERY — one row folding everything, no dimensions. So
	// what is refused is a TOTALS asking for no level at all, not a TOTALS with no dimension.
	// ⚠ AND THE NODES COUNT. A query may put every level on `SPLIT` nodes and leave the common ladder
	// empty (`TOTALS SUM(x) BY SPLIT A BY …`), which is a perfectly ordinary report with two tables —
	// asked only of `m_totalsBy` this refused it outright, because the levels were somewhere the
	// question did not look (live, 2026-08-27).
	bool anyNodeLevels = false;
	for (const ibQueryTotalSplit& node : ast.m_totalsSplits)
		if (!node.m_levels.empty()) { anyNodeLevels = true; break; }

	if (ast.m_totalsBy.empty() && !anyNodeLevels && !ast.m_totalsOverall)
		ThrowQueryException(0, 0, _("TOTALS needs at least one BY dimension, or BY OVERALL"));
	// TOP + TOTALS — the limit caps the DETAIL rows the fold runs over (the first n by ORDER BY), NOT the
	// subtotal tree. Applied as the page count on the detail read at the terminal below.

	ibSubqueryOwner owner;

	// FROM — single source, a JOIN chain, or a UNION stack. In every case the flat read
	// (b.Execute -> ExecuteRead) realizes the source (server-side or RAM-composed), the TotalBy config is
	// stamped on the result, and the runtime folds the ONE snapshot — no separate totals terminal. The
	// dimension / aggregate resolution below reads through `sources`. (docs/query-language-arc.md §22.1b)
	std::vector<ibSourceBinding> sources;
	ibDataQueryBuilder b;
	// Conditions written INSIDE a virtual table's call — collected here so the totals read applies
	// them exactly as the flat read does (see PopulateBuilder).
	std::vector<ibQueryAstExprPtr> totalsSourceConditions;

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

		const std::shared_ptr<ibSubqueryQueryable> b0 = WrapSelectAsQueryable(core0, params, owner);
		b.From(b0);   // owning handle — the branch outlives this lowering, inside the result
		for (const ibBackendQueryColumn* c : b0->GetColumns())   // carry every union-output column into the snapshot
			if (c != nullptr) b.Select(c, c->GetName());
		for (const std::shared_ptr<ibQuerySelect>& u : ast.m_unions)
			b.Union(WrapSelectAsQueryable(*u, params, owner), wxEmptyString, /*keepDuplicates*/ u->m_unionAll);

		sources.push_back({ wxEmptyString, b0.get() });
	}
	else {
		// FROM + JOINs / single source -- shared with the non-totals read path (BuildSourceTree).
		BuildSourceTree(ast, params, owner, sources, b, &totalsSourceConditions);
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
	    && !withDetails                                                 // the groups alone, and the rows were asked for
	    && ast.m_totalsBy.size() == 1
	    && ast.m_totalsBy[0].IsSingleField()                            // one FIELD too: a tuple key pages differently
	    && ast.m_totalsBy[0].Head()->m_unfold == ibQueryDimUnfold::Elements) {   // flat grouping only (Hierarchy = recursive tree -> fold)
		const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, *ast.m_totalsBy[0].Head()->m_expr);

		// ⭐ THE MEASURES COME WITH IT — when each of them is a PLAIN one.
		//
		// This gate used to require `m_totalsAggregates.empty()`, i.e. a level with no figures at all,
		// and the effect was that the very lists that cost the most read the most: a level with sums
		// fell to the fold below, which reads EVERY detail row of the source to show twenty groups. The
		// provider was never the obstacle — ExecuteGroupLevelPage has projected `m_aggregates` alongside
		// the dimension all along; nothing upstream ever handed it any.
		//
		// PLAIN means: COUNT(*), or an aggregate over a column that resolves to ONE real column. A
		// measure over a COMPUTED select field needs the fold's synthetic receiver (a projected
		// expression aggregated through a made-up column id), so a bare name that a projection already
		// claims sends the whole level back to the fold rather than risk aggregating a different thing
		// under the same name.
		struct PagedMeasure {
			ibAggregateFn               m_fn;
			const ibBackendQueryColumn* m_col = nullptr;   // null = COUNT(*)
			wxString                    m_name;
			bool                        m_distinct = false;
		};
		std::vector<PagedMeasure> pagedMeasures;
		bool measuresArePlain = true;

		// A vector, not a set: <set> is not included here and this file has already been bitten once by
		// a container MSVC hands over transitively and GCC/Clang do not (see <algorithm> at the top).
		// The list is a handful of names and it is walked once per measure.
		std::vector<wxString> projectionNames;
		{
			int idx = 0;
			for (const ibQueryProjection& p : ast.m_projections)
				if (!p.m_star && p.m_expr)
					projectionNames.push_back(OutputNameFor(ast, p, idx++));
		}

		for (const ibQueryTotalAggregate& resource : ast.m_totalsAggregates) {
			const ibQueryAstExprPtr& agg = resource.m_expr;
			if (!agg || agg->m_kind != ibQueryAstExprKind::Func) { measuresArePlain = false; break; }
			// A WINDOWED measure is not "not plain" — it is not executable at all yet, and the fold path
			// below is where that is said out loud. Leave it to the fold rather than refusing here, so
			// there is ONE sentence about it in the engine instead of two.
			if (agg->m_over || ibIsRankingKeyword(agg->m_func)) { measuresArePlain = false; break; }

			PagedMeasure m;
			m.m_fn       = AggFn(agg->m_func);
			m.m_distinct = agg->m_distinctArg;
			// THE NAME THE AUTHOR GAVE IT, and only failing that the one derived from the argument.
			m.m_name     = !resource.m_alias.IsEmpty() ? resource.m_alias
				: agg->m_star
				? ibQueryKeywordText(agg->m_func)
				: (agg->m_arg && !agg->m_arg->m_path.empty() ? agg->m_arg->m_path.back() : ibQueryKeywordText(agg->m_func));

			if (!agg->m_star) {
				if (!agg->m_arg) { measuresArePlain = false; break; }
				// A bare name a projection claims may be a computed field — the fold's business.
				if (agg->m_arg->m_kind == ibQueryAstExprKind::Column && agg->m_arg->m_path.size() == 1
				    && std::find(projectionNames.begin(), projectionNames.end(),
				                 agg->m_arg->m_path.back()) != projectionNames.end()) { measuresArePlain = false; break; }
				try {
					const std::vector<const ibBackendQueryColumn*> argCols = ResolvePath(sources, *agg->m_arg);
					if (argCols.size() != 1 || argCols.back() == nullptr) { measuresArePlain = false; break; }
					m.m_col = argCols.back();
				}
				catch (const ibBackendException&) { measuresArePlain = false; break; }
			}
			pagedMeasures.push_back(m);
		}

		// ⭐⭐ …AND THE AUTHOR'S SORT MUST BE THE DIMENSION, or this path may not be taken (Max,
		// 2026-08-29, watching a list grouped by Ref and sorted by Number come back in neither order).
		//
		// 🛑 THIS BRANCH NEVER LOOKED AT `m_orderBy` AT ALL. It emits GROUP BY + the keyset over the
		// DIMENSION and nothing else, so a sort on any other field was dropped on the floor — silently,
		// and in the one place where silence is indistinguishable from "the database decided". The
		// journal shows it plainly: `ORDER BY Number TOTALS BY Ref` rendered, and the SQL that came out
		// carried `GROUP BY` with NO `ORDER BY` whatever. A setting a person had made did nothing.
		//
		// It cannot simply be emitted either: under a server fold a column that is neither a grouping
		// key nor an aggregate does not exist to order by. Ordering the GROUPS by it means aggregating
		// it (`MIN` ascending, `MAX` descending) — a real feature, and not this branch's.
		//
		// So the branch declines, and the FOLD below does it: it reads the detail rows and orders THEM
		// by what the author asked for, which puts each group where its first row lands — the mechanism
		// stated at "THE AUTHOR'S SORT GOES FIRST" further down. The cost is the one this whole branch
		// exists to avoid, a detail read to show a page of groups; paying it is strictly better than
		// answering in an order nobody asked for.
		bool sortIsTheDimension = true;
		if (pathCols.size() == 1 && pathCols.back() != nullptr) {
			for (const ibQueryOrderItem& o : ast.m_orderBy) {
				if (!o.m_expr || IsComputedExprAst(*o.m_expr)) { sortIsTheDimension = false; break; }
				std::vector<const ibBackendQueryColumn*> orderCols;
				try { orderCols = ResolveWhereTarget(sources, *o.m_expr, /*allowDotWalk*/true); }
				catch (const ibBackendException&) { sortIsTheDimension = false; break; }
				if (orderCols.size() != 1 || orderCols.front() != pathCols.back()) { sortIsTheDimension = false; break; }
			}
		}

		if (pathCols.size() == 1 && measuresArePlain && sortIsTheDimension) {   // plain scalar dim (no dot-walk expansion -> single-column keyset ORDER BY)
			const ibBackendQueryColumn* leaf = pathCols.back();
			b.GroupBy(leaf);
			OutputColumn oc; oc.m_name = leaf->GetName(); oc.m_col = leaf;
			oc.m_role = ibQueryLowering::ibColumnRole::Dimension;   // a server-paged grouping level IS a dimension
			outSchema.clear(); outSchema.push_back(oc);

			// The figures, in the order they were written — read back exactly as the fold's are: by the
			// column for a real one, by alias for COUNT(*), which has no column to be keyed on.
			for (const PagedMeasure& m : pagedMeasures) {
				b.Aggregate(m.m_fn, m.m_col, m.m_name, m.m_distinct);

				OutputColumn mc; mc.m_name = m.m_name;
				mc.m_role = ibQueryLowering::ibColumnRole::Measure;
				if (m.m_col != nullptr) mc.m_col = m.m_col;
				else { mc.m_alias = m.m_name; mc.m_byAlias = true; }
				outSchema.push_back(mc);
			}
			// WHERE = the drill SCOPE filter + the user filter (same lowering the fold path uses below).
			if (ast.m_where) {
				if (IsFlatAndWhere(*ast.m_where))
					LowerFlatWhere(b, sources, *ast.m_where, params, /*allowDotWalk*/false);
				else
					b.Where(BuildWherePredicate(sources, *ast.m_where, params, /*allowDotWalk*/false));
			}
			*outServerGroupedLevel = true;
			ibDataQueryResult groups = b.SelectAggregatePage(page);   // server-side GROUP BY + keyset + LIMIT
			DetachSchemaFromRunSources(outSchema, owner);             // the schema leaves, sharing what it names
			return groups;
		}
	}

	outSchema.clear();
	// Plain ordinals: the column class stamps its own kind on them (ibSyntheticOutputColumn), and the
	// names below read the same ordinal.
	ibMetaID nextSynthId = 0;
	const bool multiSource = !ast.m_joins.empty() || !ast.m_unions.empty();
	std::map<wxString, const ibBackendQueryable*> dwJoined; int dwAliasSeq = 0;   // dot-walk join dedup (multi-source)

	// ⭐⭐ THE OUTPUT NAMES — WHAT THE RESULT CALLS ITS FIELDS, built BEFORE anything in TOTALS is
	// resolved, because TOTALS are taken OVER THE RESULT and name its fields, not the tables'.
	//
	// This is the alias table the Unions tab shows: with a union it is the COMMON name every branch
	// lines up under, and with a single source it is still the name `SELECT` gave the field. A total
	// says `BY PredefinedName`, not `BY Catalog1.PredefinedName` — the second is a path into a table
	// that a two-branch union may not even have (Max, 2026-08-22, pointing at the Unions tab: "these
	// names are what the totals take").
	//
	// It used to be built AFTER the dimension loop and served the aggregates alone, so a dimension
	// could only ever be a source path — and the constructor compensated by writing source paths into
	// the Totals tab, which is a shim standing in for a missing resolution.
	std::map<wxString, const ibQueryProjection*, ibNoCaseLess> selectByName;
	{
		int idx = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star || !p.m_expr) continue;
			selectByName[OutputNameFor(ast, p, idx++)] = &p;
		}
	}

	// A NAME IS THE RESULT'S FIELD FIRST. The LEADING segment — `BY Posted`, `BY Parent.Description` —
	// is looked up among the output names and replaced by the path that field was SELECTed by, so the
	// walk continues from there: `Parent` standing for `Catalog1.Parent` makes the second one
	// `Catalog1.Parent.Description`, which is what the sources can answer.
	//
	// Left alone: a name no projection claims (it is a source field, resolved as always), and a field
	// SELECTed as anything but a plain column — an expression has no path to walk INTO, so
	// `BY <computed>` stays exactly the expression it was and a dot after it is a real error rather
	// than a silently different reading.
	// The aggregates must name fields the SELECT declared — the same rule the constructor's verdict
	// asks, asked once, above (CheckTotalsNameSelectedFields). Here it runs before anything is built,
	// so a query that will not hold up says so before it reads a row.
	CheckTotalsNameSelectedFields(ast);

	auto throughAlias = [&selectByName](const ibQueryAstExprPtr& expr) -> ibQueryAstExprPtr {
		if (!expr || expr->m_kind != ibQueryAstExprKind::Column || expr->m_path.empty())
			return expr;
		const auto it = selectByName.find(expr->m_path.front());
		if (it == selectByName.end() || !it->second->m_expr)
			return expr;
		const ibQueryAstExprPtr& stands = it->second->m_expr;
		if (expr->m_path.size() == 1)
			return stands;                                          // the field itself
		if (stands->m_kind != ibQueryAstExprKind::Column)
			return expr;                                            // nothing to walk into
		ibQueryAstExprPtr walked = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		walked->m_path = stands->m_path;
		walked->m_path.insert(walked->m_path.end(), expr->m_path.begin() + 1, expr->m_path.end());
		walked->m_line = expr->m_line; walked->m_col = expr->m_col;   // the diagnostics point at what was WRITTEN
		return walked;
	};

	// BY OVERALL — the level above them all. Nothing to resolve and nothing to group by: the fold's
	// root already holds the whole-result aggregates, so this only says to walk it as a row.
	b.TotalsOverall(ast.m_totalsOverall);

	// The columns the levels group by, IN LEVEL ORDER — the sort the detail read is given below, so
	// that the rows of one group arrive together and the groups arrive in a repeatable order.
	// ⚠ THE PATH, NOT ONLY THE LEAF. A dot-walked level's column is a SYNTHETIC one — it exists as a
	// projection alias off a joined table (`… AS dim0`) and belongs to no table by that name. Sorting
	// by the column POINTER renders `MainTable."dim0"`, a field the main table does not have, and the
	// whole query dies. The path form routes the sort through the same join the projection uses.
	struct LevelSort {
		std::vector<const ibBackendQueryColumn*> m_path;   // dot-walk — sort through the join
		const ibBackendQueryColumn*              m_col = nullptr;   // plain column

		// ⭐⭐ …OR THE OUTPUT'S NAME, when the level groups by something READ BY ALIAS.
		//
		// A dot-walk output (`Product.Parent AS ProductGroup`) resolves to the walk's LEAF, and that
		// column belongs to the joined table — sorting by the pointer names it as `fld<metaID>` on a
		// source that has no such field (`-206 Column unknown FLD1012_RRREF`, 2026-08-31). Out here
		// the value exists under the OUTPUT's own name, so the name is what to sort by; it is
		// resolved against the query's source at the moment the sort is spent, which is the first
		// point where that source is known.
		wxString                                 m_name;
	};
	std::vector<LevelSort> levelOrder;
	// The columns the LEVELS group by — collected as they resolve, spent where the aggregates are
	// built (a level's key and an aggregate cannot share one slot).
	std::set<const ibBackendQueryColumn*> levelCols;

	// The dimension levels, IN ORDER (each yields a subtotal node; the root is the grand total). They
	// are the leading output columns (their group key at each node — read by GetValue(col)).
	size_t levelIndex = 0;
	// ⭐ WHERE A LEVEL CAN BE ADDRESSED FROM — filled as the levels are lowered, read by the
	// aggregates below (`OVER <level>`). See the note at the registration site.
	// ⭐ The address, AND the columns it stands for. Both are derived from the levels at the same
	// moment and neither is guessed later: the address is what the FOLD needs (which node carries the
	// figure), the prefix is what the SERVER needs (`PARTITION BY <these>`). Deriving the prefix later
	// would mean re-walking the ladder with the branch rules in hand — a second reading of one thing.
	struct ibScopeAddress {
		wxString                                 m_name;
		std::shared_ptr<ibTotalBranch>           m_branch;
		int                                      m_depth = 0;
		std::vector<const ibBackendQueryColumn*> m_prefix;   // the levels from the root down to it
	};
	std::vector<ibScopeAddress> scopeByName;   // a handful of levels — a list, matched case-insensitively
	// The ladder being walked, as columns: the common part, then whatever branch is open. Cut back to
	// the common part at every branch boundary, exactly as the depth counter is.
	std::vector<const ibBackendQueryColumn*> prefixCols;
	size_t commonPrefixCols = 0;
	// ⭐⭐ …AND A BRANCH CONTINUES THE COMMON LADDER RATHER THAN THE LIST. A level's number is its
	// DEPTH — which is what the fold builds (a fork spends no level, so two branches stand at the
	// same depth however many of them there are) and what every reader asks by: the composer names a
	// column's title by looking up `LevelAt(output, m_level + 1)` IN ITS OWN OUTPUT.
	//
	// 🛑 Counted straight through all the nodes, the second branch is off by however many levels the
	// first one had: its first grouping asked for level 2 and got the second one's field, and its
	// last asked for a level that is not there (Max, live, 2026-08-27, on a report whose second table
	// printed `Posted` twice and lost `Ref`: the first part overwrites the second).
	size_t commonLevels = 0;
	// ⭐⭐ WHERE THE TEXT SAYS "STARTS A BRANCH", THE FOLD NEEDS "BELONGS TO ONE" — and this is where
	// the one becomes the other. A query is written forwards, so `SPLIT` marks the level that opens
	// a branch; a fold asks each level which ladder it is on. One object per branch, shared by its
	// levels, so belonging is IDENTITY and no index has to be kept in step with the list.
	// ⭐⭐ EVERY NODE'S LEVELS, IN ORDER — the hidden node first (`m_totalsBy`, which is what a report
	// without SPLIT has), then each visible one. Flattened into ONE list here because that is what a
	// fold reads: each level says which node it belongs to, and levels of one node stand together.
	std::vector<std::pair<const ibQueryTotalDim*, std::shared_ptr<ibTotalBranch>>> ordered;
	for (const ibQueryTotalDim& d : ast.m_totalsBy)
		ordered.emplace_back(&d, nullptr);   // null = the hidden node
	for (const ibQueryTotalSplit& split : ast.m_totalsSplits) {
		if (split.m_levels.empty())
			continue;   // added, nothing hung on it yet — it says nothing and folds nothing
		auto branch = std::make_shared<ibTotalBranch>();
		branch->m_name = split.m_name;
		for (const ibQueryTotalDim& d : split.m_levels)
			ordered.emplace_back(&d, branch);
	}

	std::shared_ptr<ibTotalBranch> branch;   // the node whose levels are being lowered
	for (const auto& entry : ordered) {
		const ibQueryTotalDim& d = *entry.first;
		if (entry.second != branch) {
			// ⭐ THE LADDER THAT ENDS HERE TAKES ITS OWN RECORDS FIRST — while it is still the last one
			// declared, because the fold cuts the level list into ladders by neighbourhood.
			//
			// ⚠ THE HIDDEN NODE IS EXEMPT, and that is the whole of the rule: records close a ladder
			// that ENDS, and the hidden one does not end where a SPLIT begins — it continues, as every
			// visible node. Given records there they would hang beside the nodes, under no heading a
			// reader asked for.
			if (withDetails && branch != nullptr)
				b.TotalsDetails(layout.m_detailsAxis, branch);
			// THE COMMON LADDER ENDED HERE — its length is what every branch starts from (see above).
			if (branch == nullptr) {
				commonLevels     = levelIndex;
				commonPrefixCols = prefixCols.size();
			}
			branch     = entry.second;
			levelIndex = commonLevels;
			prefixCols.resize(commonPrefixCols);   // …and so does the prefix a branch's levels extend
		}

		// ONE LEVEL, ONE OR MORE FIELDS — grouped by the TUPLE of them. Each field resolves exactly as
		// a lone dimension always did (plain column or dot-walk); what changed is that they are
		// collected into one level instead of each becoming a level of its own.
		ibTotalLevel level;
		level.m_branch = branch;

		// ⭐⭐ THE LEVEL'S ADDRESS, REGISTERED UNDER ITS NAME — so `TOTALS SUM(x) OVER Item` can be
		// resolved below, where the aggregates are built. An ADDRESS (branch + depth) rather than the
		// fields themselves, deliberately: the fields are a PROJECTION of it and can be derived at any
		// moment, while the address cannot be recovered from a list of fields. That is what keeps the
		// push-down road open — the same address spells `PARTITION BY <the prefix's fields>`, which
		// every one of our engines runs (windows are on in Firebird, PostgreSQL and SQLite alike,
		// unlike ROLLUP).
		//
		// Registered under BOTH spellings — see where it happens, below, once this level's columns are
		// resolved: the address and the prefix are written together or not at all.

		// …AND WHICH WAY IT READS. `rowLevels` is the caller's own layout — a cross-table's row axis
		// — and everything past it stands across the page. Stamped ON the level, so the fold and
		// anything else downstream ask the level rather than re-deriving a seam from a count that
		// travelled separately (see ibTotalsAxis).
		// ⚠ ASKED OF THE LAYOUT, NOT OF A COUNT. `rowLevels == 0` is a legitimate table — one whose
		// rows axis is empty, so every level reads across — and it is also every ordinary report.
		// `m_hasColumns` is what tells them apart; deriving it from the count folded the first case
		// as the second and produced a table with no columns and no records at all.
		if (layout.m_hasColumns && levelIndex >= layout.m_rowLevels) {
			level.m_axis = ibTotalsAxis::Columns;

			// ⚠ AND A COLUMN AXIS DOES NOT UNFOLD A HIERARCHY. A hierarchy nests values INSIDE one
			// level — folders standing over items — and a table's columns are a flat run of keys,
			// one block of cells each: there is nowhere for a folder to nest ACROSS the page. Said
			// here, where the shape is decided, rather than folded into something plausible and
			// printed as a table whose columns do not line up.
			for (const ibQueryTotalField& field : d.m_fields)
				if (field.m_unfold != ibQueryDimUnfold::Elements)
					ThrowQueryException(0, 0,
						_("a level that reads ACROSS the page cannot unfold a hierarchy: a table's columns are a flat run of keys"));
		}
		++levelIndex;

		// A HIERARCHY UNFOLD walks one parent chain, and a key made of several fields has no single
		// chain to walk. Refused HERE, where the query says it, rather than folded into something
		// plausible downstream — the reading it would silently become (plain grouping) is not what
		// the word asks for.
		if (d.m_fields.size() > 1)
			for (const ibQueryTotalField& field : d.m_fields)
				if (field.m_unfold != ibQueryDimUnfold::Elements)
					ThrowQueryException(0, 0, _("a TOTALS level of several fields cannot unfold one of them through a hierarchy: give the hierarchy a level of its own"));

		// …AND NEITHER CAN IT BE READ BY PERIODS. A period is a SCALE, and the padding that makes the
		// word worth having fills the gaps ALONG it; a key made of several fields has no scale, and
		// "every month of every warehouse" is a different report — one the author writes as two
		// levels. Refused where it is written rather than quietly dropping the word later.
		if (d.m_fields.size() > 1)
			for (const ibQueryTotalField& field : d.m_fields)
				if (field.m_periods)
					ThrowQueryException(0, 0, _("a TOTALS level of several fields cannot be read BY PERIODS: give the period a level of its own"));

		for (const ibQueryTotalField& dimField : d.m_fields) {
			// The word that was WRITTEN — kept for the level's name — and the expression it stands
			// for, which is the SELECTed one when the word is an output field's alias.
			const wxString                 dimWritten = (dimField.m_expr && dimField.m_expr->m_kind == ibQueryAstExprKind::Column
			                                            && dimField.m_expr->m_path.size() == 1)
			                                            ? dimField.m_expr->m_path.back() : wxString();
			// ⭐ A LEVEL MAY GROUP BY A FIELD NOBODY SELECTED, and that is not a loophole — it is what
			// levels are for. The skeleton is INVISIBLE: an unselected dimension adds DEPTH and no
			// column, so the tree gains a tier that shows the first column again and looks, to a
			// reader, like the same row repeated (Max, 2026-08-22, on a three-level TOTALS whose
			// SELECT named only the first of the three fields: "physically I am not selecting all of
			// it, and it still shapes the totals").
			//
			// I had a refusal here for a while. It was wrong: the selection says what is READ BACK,
			// the levels say how it is CUT, and a cut may follow a line the reader never sees.
			const ibQueryAstExprPtr        dimExpr    = throughAlias(dimField.m_expr);
			const bool                     viaAlias   = dimExpr != dimField.m_expr;
			const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, *dimExpr);   // plain col OR dot-walk path
			const ibBackendQueryColumn* leaf = pathCols.back();
			const ibDimensionKind dim =
				dimField.m_unfold == ibQueryDimUnfold::Hierarchy       ? ibDimensionKind::Hierarchy
				: dimField.m_unfold == ibQueryDimUnfold::HierarchyOnly ? ibDimensionKind::HierarchyOnly
				                                                       : ibDimensionKind::Elements;
	
			// THE LEVEL'S NAME. Its own when it was given one — that is what makes two levels over the
			// same column (Date by month, Date by day) two readable columns instead of one name answered
			// by whichever came last. With several fields the given name belongs to the HEAD; the rest
			// answer to their own column names, since one name cannot stand for a tuple.
			// …and when the level was written as an OUTPUT FIELD'S name, that name wins over the
			// leaf's: `SELECT Posted AS Done … BY Done` reads back as `Done`, which is the word the
			// query used — the leaf is called something else and nobody asked for it.
			const bool headField = (&dimField == &d.m_fields.front());
			OutputColumn oc; oc.m_name = (headField && !d.m_alias.IsEmpty()) ? d.m_alias
			                           : (viaAlias && !dimWritten.IsEmpty()) ? dimWritten
			                                                                 : leaf->GetName();
			oc.m_role = ibQueryLowering::ibColumnRole::Dimension;   // a TOTALS BY level
			// WHICH level it belongs to — several fields of one level all carry the same number, so
			// a printer can put them side by side instead of counting columns as if they were levels.
			// 🛑 IT USED TO BE POINTER ARITHMETIC — `&d - &ast.m_totalsBy.front()`. That held only while
			// every level lived in that one vector: with `SPLIT` the levels are walked from a list
			// built out of the common ladder AND the nodes, so subtracting inside a different vector
			// is meaningless, and an empty common ladder (a report whose groupings all sit on nodes)
			// asserts outright — `front() called on empty vector`, live 2026-08-27.
			//
			// The counter this loop already keeps says the same thing without asking where the level
			// is stored, which is the point: a level's NUMBER is its position in the walk.
			//
			// ⚠ MINUS ONE, because `levelIndex` has already been advanced past this level by the axis
			// decision above — the number wanted here is the one this level was given, not the one
			// the next will get.
			oc.m_level = static_cast<int>(levelIndex) - 1;
			// ⭐ PERIODS(<unit>[, <from>, <to>]) — read HERE, because this tier owns the vocabulary
			// (ibReadPeriodUnit, the same one a register's Turnovers argument is read by) and the
			// bounds are ordinary expressions that have to be evaluated to values before the fold
			// can pad anything with them. A hierarchy unfold and a period are two different readings
			// of one field, so asking for both is refused where it is written.
			std::shared_ptr<ibTotalPeriods> periods;
			if (dimField.m_periods) {
				if (dim != ibDimensionKind::Elements)
					ThrowQueryException(0, 0, _("a TOTALS level field cannot be read both through a hierarchy and by periods"));
				periods = std::make_shared<ibTotalPeriods>();
				if (!ibReadPeriodUnit(dimField.m_periods->m_unit, periods->m_unit))
					ThrowQueryException(0, 0,
						_("'%s' is not a period unit: Second, Minute, Hour, Day, Week, TenDays, Month, Quarter, HalfYear, Year"),
						dimField.m_periods->m_unit);
				// A bound left out stays EMPTY, and empty is its own answer: pad between the first
				// and the last period the data holds. Written, it is an ordinary expression —
				// a literal or a parameter — read by the same evaluator every other bound uses.
				if (dimField.m_periods->m_from) periods->m_from = EvalValue(*dimField.m_periods->m_from, params);
				if (dimField.m_periods->m_to)   periods->m_to   = EvalValue(*dimField.m_periods->m_to,   params);
			}

			if (pathCols.size() == 1) {
				level.m_fields.push_back(ibTotalField{ leaf, dim, {}, periods });   // plain dimension — group by the column's own metaID
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
				// ⭐ A REFERENCE LEAF TAKES THE SAME ROAD AS A SCALAR ONE, over a SINGLE source: the path is
				// joined once and the leaf projected under the dimension's own alias — as a SPREAD, since a
				// reference is not one field — and the fold groups by a synthetic column with an id of its
				// own. Without that id the level was indistinguishable from the row's own attribute (a
				// self-reference walks back to the SAME metaID), so two levels folded as one and the value
				// came back empty (2026-08-20: three groupings produced one blank row).
				ibBackendColumnRawDB::RawType rt;
				const bool scalarLeaf = ScalarRawType(leaf, rt);
				if (!multiSource && !scalarLeaf) {
					const wxString alias = wxString::Format(wxT("dim%u"), static_cast<unsigned>(nextSynthId));
					auto synth = std::make_shared<ibSyntheticOutputColumn>(alias, leaf->GetTypeDesc(), nextSynthId++);
					level.m_fields.push_back(b.DeclareDimDotWalk(pathCols, synth.get(), alias, dim));
					oc.m_col = synth.get(); oc.m_ownedCol = synth;
				}
				else if (multiSource) {
					const ibBackendQueryColumn* dwLeaf =
						ExpandDotWalkJoins(b, RootForPath(sources, *dimExpr), pathCols, dwJoined, dwAliasSeq, *dimExpr);
					level.m_fields.push_back(ibTotalField{ dwLeaf, dim });
					oc.m_col = dwLeaf;
				}
				else {
					const wxString alias = wxString::Format(wxT("dim%u"), static_cast<unsigned>(nextSynthId));
					auto synth = std::make_shared<ibSyntheticScalarColumn>(alias, nextSynthId++, rt);
					// provider joins path, projects leaf scalar AS alias
					level.m_fields.push_back(b.DeclareDimDotWalk(pathCols, synth.get(), alias, dim));
					oc.m_col = synth.get(); oc.m_ownedCol = synth;
				}
			}
			// This level's key joins the detail read's sort — by its PATH when it was reached through
			// one, so the sort goes down the same join the projection did.
			if (pathCols.size() > 1)
				levelOrder.push_back(LevelSort{ pathCols, nullptr, wxString() });
			else if (oc.m_byAlias && !oc.m_name.IsEmpty())
				levelOrder.push_back(LevelSort{ {}, nullptr, oc.m_name });   // read by alias — sort by the NAME
			else if (oc.m_col != nullptr)
				levelOrder.push_back(LevelSort{ {}, oc.m_col, wxString() });
			outSchema.push_back(oc);
		}

		// ⭐ WHICH SLOTS THE LEVELS HAVE CLAIMED. The fold writes a level's KEY into the slot of the
		// column it groups by, so that slot is answered for before any aggregate runs — see where
		// this set is used, a few lines down, together with the columns already aggregated.
		for (const ibTotalField& field : level.m_fields)
			if (field.m_col != nullptr)
				levelCols.insert(field.m_col);

		// ⭐⭐ THE LEVEL CAN NOW BE ADDRESSED — its columns are resolved, so the address and the prefix
		// it stands for are written in one place. Under BOTH spellings: the bare name, and qualified
		// by the branch, so a level stays addressable when two branches carry one of the same name.
		//
		// The PREFIX is the ladder down to and including this level — exactly the `PARTITION BY` that
		// computes this figure on the server, which is why it is captured here rather than rebuilt
		// later from the address.
		for (const ibTotalField& field : level.m_fields)
			if (field.m_col != nullptr)
				prefixCols.push_back(field.m_col);
		{
			wxString levelName = d.m_alias;
			if (levelName.IsEmpty() && d.Head() != nullptr && d.Head()->m_expr != nullptr
			    && !d.Head()->m_expr->m_path.empty())
				levelName = d.Head()->m_expr->m_path.back();
			if (!levelName.IsEmpty()) {
				const int depth = static_cast<int>(levelIndex);   // already advanced past this level
				bool taken = false;
				for (const ibScopeAddress& had : scopeByName)
					if (had.m_name.IsSameAs(levelName, false)) { taken = true; break; }
				if (!taken)                                       // first wins; a duplicate needs qualifying
					scopeByName.push_back(ibScopeAddress{ levelName, branch, depth, prefixCols });
				if (branch != nullptr && !branch->m_name.IsEmpty())
					scopeByName.push_back(ibScopeAddress{ branch->m_name + wxT(".") + levelName,
					                                     branch, depth, prefixCols });
			}
		}

		b.TotalByLevel(std::move(level));   // the level goes in WHOLE, after all its fields resolved
	}

	// …AND THE ROWS UNDER THEM, when the reader asked for them: one more level, with no fields. The
	// fold reads that as "no group here — the rows themselves" and hangs a detail node per row under
	// the deepest heading. Nothing else in this function changes: the same read, the same schema,
	// one extra level in the config.
	// …AND THE LAST LADDER, which the loop above could not see the end of. Without branches `branch`
	// is null and this is the single call it has always been — the records of the one ladder there is.
	if (withDetails)
		b.TotalsDetails(layout.m_detailsAxis, branch);

	// (`selectByName` — the output-name map the aggregates read below — is built ABOVE, before the
	// dimensions, because both clauses name the RESULT'S fields: SELECT Price … TOTALS SUM(Price),
	// SELECT 1 AS test … TOTALS SUM(test). A real column aggregates by its metaID; a COMPUTED /
	// constant field is projected by the door and aggregated through a SYNTHETIC measure column
	// (below) — both readable by the metaID-keyed totals fold.)

	// The TOTALS aggregate set is COMMON across all dimension levels (each level rolls them IN-PLACE, so
	// the aggregate reads back off its own column — GetValue(col), same as a dimension).
	b.Totals();
	std::map<wxString, const ibBackendQueryColumn*> measureCol;   // computed alias -> its synthetic measure (projected once)

	// ⭐⭐ A NAME STANDS FOR ONE COLUMN. A resource is named after its ARGUMENT — `SUM(Amount)` reads
	// back as `Amount` — so a report that GROUPS BY Amount and also sums it produced two columns
	// both answering to "Amount": two identical captions on the sheet, and `res["Amount"]` returning
	// whichever came first (seen live, 2026-08-22).
	//
	// The later one is qualified by its FUNCTION (`SUMAmount`), which is the name a person would
	// have written themselves, and a counter settles the rest. The FIRST claimant keeps the plain
	// name, so nothing that reads a report by its resource's name changes.
	// ⚠ A SELECTED FIELD AND A TOTAL OVER IT ARE ONE COLUMN, not two — deliberately. `SELECT Posted,
	// Number … TOTALS COUNT(Number)` gives `Number` ONE slot: the fold writes the figure into it at
	// every heading, and the detail row underneath holds the row's own value (Max, 2026-08-22: "they
	// have the same name — the aggregate field writes its node's result, right down to the detail
	// record"). That is what makes a total READ like the field it totals. So the SELECT names are NOT
	// counted as taken here; only what is already in outSchema is, which is the DIMENSIONS — and a
	// level and a measure over the same column genuinely are two different figures.
	auto uniqueOutputName = [&outSchema](const wxString& wanted, const wxString& funcName) {
		auto taken = [&outSchema](const wxString& name) {
			for (const OutputColumn& used : outSchema)
				if (used.m_name.IsSameAs(name, false))
					return true;
			return false;
		};
		if (!taken(wanted))
			return wanted;
		// ⭐ THE QUALIFIER IS A WORD, NOT A KEYWORD. `ibQueryKeywordText` shouts — `COUNT` — because
		// that is how the LANGUAGE spells it, and gluing it on produced `COUNTDate`, which is what a
		// person then reads in the report header and writes in `res["COUNTDate"]` (Max, 2026-08-25:
		// "the name is stupid"). An output name is read by people, so the qualifier is written the
		// way a name is written: `CountDate`, `SumAmount`.
		const wxString qualified =
			funcName.Left(1).Upper() + funcName.Mid(1).Lower() + wanted;
		if (!taken(qualified))
			return qualified;
		for (int seq = 2; ; ++seq) {
			const wxString candidate = qualified + wxString::Format(wxT("%d"), seq);
			if (!taken(candidate))
				return candidate;
		}
	};

	// WHICH COLUMNS ARE ALREADY BEING AGGREGATED in this clause. The fold rolls an aggregate INTO
	// its own column, so a second aggregate over the SAME column would write into the slot the first
	// one wrote — `TOTALS SUM(Amount), COUNT(Amount)` printed one figure twice. A repeat is
	// projected a second time under a synthetic alias, which gives it a column, and therefore a
	// slot, of its own.
	std::set<const ibBackendQueryColumn*> aggregatedCols;

	// ⭐⭐ …AND A COLUMN THE REPORT GROUPS BY IS SPOKEN FOR TOO — by the LEVEL, which wrote its key
	// into that slot before any aggregate ran. Same collision, same cure, and it is the same set
	// because it is the same question: "is this column's slot already somebody's answer?"
	//
	// 🛑 IT COST A WHOLE CROSS-TABLE TO FIND. `TOTALS COUNT(Number) BY Number` folded correctly and
	// then the count landed on top of every key, so all the column headings came back as "1" and the
	// table collapsed to one column (Max, 2026-08-25, in one word: nonsense). The key is the older answer — it says
	// what the group IS — so the aggregate is the one that moves, exactly as the second aggregate
	// over one column already moves.
	aggregatedCols.insert(levelCols.begin(), levelCols.end());

	for (const ibQueryTotalAggregate& resource : ast.m_totalsAggregates) {
		const ibQueryAstExprPtr& agg = resource.m_expr;
		if (!agg || agg->m_kind != ibQueryAstExprKind::Func)
			ThrowQueryException(0, 0, _("TOTALS expects aggregate functions (SUM/COUNT/MIN/MAX/AVG)"));
		RefuseUnloweredWindow(*agg);

		// Output name read back via res[name]: COUNT(*) -> the function name; a field -> its identifier.
		// Qualified when that name is already spoken for — see uniqueOutputName.
		const wxString wantedName = agg->m_star
			? ibQueryKeywordText(agg->m_func)
			: (agg->m_arg && !agg->m_arg->m_path.empty() ? agg->m_arg->m_path.back() : ibQueryKeywordText(agg->m_func));

		// ⭐⭐ A NAME THE AUTHOR WROTE IS NOT A CANDIDATE — IT IS THE ANSWER. The qualifier below
		// exists to settle a name the ENGINE derived; applying it to `AS Qty` would rename the very
		// thing the author named, and `res["Qty"]` would then find nothing (the report reads the
		// figure back by the name it was given).
		//
		// So a collision with an alias is REFUSED, out loud, instead of being papered over: two
		// columns answering to one name is a query that cannot be read back, and the author is the
		// only one who can decide which name to change.
		wxString outName;
		if (!resource.m_alias.IsEmpty()) {
			for (const OutputColumn& used : outSchema)
				if (used.m_name.IsSameAs(resource.m_alias, false))
					ThrowQueryException(agg->m_line, agg->m_col,
						_("TOTALS names the resource \"%s\", and a grouping level already answers to that name: give one of them another alias"),
						resource.m_alias);
			outName = resource.m_alias;
		}
		else {
			outName = uniqueOutputName(wantedName, ibQueryKeywordText(agg->m_func));
		}

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
			// A selected field spelled its SOURCE way (`Catalog1.Parent`) — resolved against the
			// sources. That it IS selected was settled above, before anything was built.
			else col = ResolveColumnSingle(sources, *agg->m_arg);

			// ⭐ A SECOND AGGREGATE OVER THE SAME COLUMN NEEDS A COLUMN OF ITS OWN. The fold rolls
			// each one IN PLACE — into the slot keyed by its input column — so `SUM(Amount)` and
			// `COUNT(Amount)` both wrote into Amount's slot and the report printed one figure under
			// two headings, the second having quietly overwritten the first.
			//
			// The repeat is PROJECTED AGAIN under a synthetic alias, through the same road a computed
			// resource already takes: a projection of its own gives it a column of its own, and a
			// column of its own is a slot of its own. The first claimant is untouched.
			// ⚠ AND ONLY A SCALAR CAN TAKE THIS ROAD. The second projection is ONE column, so a
			// reference — which is a spread of several fields — cannot be re-projected this way; such
			// an aggregate keeps rolling in place, exactly as it did before there was a second road
			// at all. (Better a shared slot than a column the reader cannot read.)
			ibBackendColumnRawDB::RawType aggRaw = ibBackendColumnRawDB::RawType::Number;
			if (col != nullptr && ScalarRawType(col, aggRaw) && !aggregatedCols.insert(col).second) {
				const wxString alias = wxString::Format(wxT("agg%u"),
					static_cast<unsigned>(nextSynthId));
				b.SelectExpr(BuildColumnExprFromAst(sources, *agg->m_arg, params), alias);
				// ⭐ CARRYING THE INPUT'S TYPE. The synthetic column defaults to Number, and a default
				// is not a reading: `COUNT(Date)` projected `fld1025_D` and then read it as a number,
				// which the driver answers with "Invalid field type" — the whole report, for a column
				// nobody looked at (Max, 2026-08-25, on `TOTALS COUNT(Number), COUNT(Date) BY (Ref, Date)`).
				owned = std::make_shared<ibSyntheticScalarColumn>(alias, nextSynthId++, aggRaw);
				col   = owned.get();
			}
		}
		b.Aggregate(AggFn(agg->m_func), col, outName, agg->m_distinctArg);   // in-place — rolls into its own column (named for read-back)

		// ⭐⭐ …AND OVER WHAT, when the author said so. The name is resolved against the levels that
		// were just lowered, so it can only mean a grouping this very query declares.
		//
		// 🛑 AN UNKNOWN NAME IS REFUSED, LOUDLY. Folding by the ladder instead would answer a
		// different question in silence — a share against the wrong denominator reconciles to
		// nothing and reads as bad data, not as a missing feature. (The same reasoning as the
		// window refusal a few lines up.)
		if (!resource.m_scope.IsEmpty()) {
			// ⭐ ONE NAME OR SEVERAL — the constructor ticks groupings, and several ticks are ONE area.
			// The DEEPEST of them decides where the figure stands (a node of that level carries it),
			// and their columns together are what the server partitions by.
			const ibScopeAddress* found = nullptr;
			std::vector<const ibBackendQueryColumn*> scopeCols;
			wxStringTokenizer names(resource.m_scope, wxT(","));
			while (names.HasMoreTokens()) {
				wxString one = names.GetNextToken();
				one.Trim(true).Trim(false);
				if (one.IsEmpty())
					continue;
				const ibScopeAddress* at = nullptr;
				for (const ibScopeAddress& address : scopeByName)
					if (address.m_name.IsSameAs(one, false)) { at = &address; break; }
				if (at == nullptr)
					ThrowQueryException(0, 0,
						_("OVER \"%s\": this TOTALS declares no such grouping: name a level of its own BY, qualifying it with the branch (Branch.Level) where two branches carry one name"),
						one);
				if (found == nullptr || at->m_depth > found->m_depth)
					found = at;                     // the deepest ticked level is where the figure lives
				for (const ibBackendQueryColumn* c : at->m_prefix)
					if (std::find(scopeCols.begin(), scopeCols.end(), c) == scopeCols.end())
						scopeCols.push_back(c);
			}
			if (found == nullptr)
				ThrowQueryException(0, 0, _("OVER: no grouping was named"));
			b.AggregateOver(outName, found->m_branch, found->m_depth);

			// ⭐⭐ …AND THE SERVER COMPUTES IT, where the engine has windows. `SUM(x) OVER (PARTITION BY
			// <the prefix>)` is an ordinary output column — a value per ROW — so this needs neither
			// ROLLUP nor GROUPING SETS, and it therefore works on Firebird too, where the ladder's own
			// fold must stay in memory for good.
			//
			// The figure then reaches the node through MIN rather than through its own function, and
			// that is exact rather than approximate: inside its area the value is CONSTANT, so the
			// minimum of it IS it. Above the area the pass over the tree erases the column anyway
			// (ApplyScopedAggregates), so no wrong number can survive there either.
			// ⚠ AND WHETHER IT GOT ONE DECIDES HOW IT IS READ BACK. Without a receiver the figure is
			// folded here, and it CANNOT land in its source column — that column is the ladder
			// aggregate's (`SUM(Cost)` and `SUM(Cost) OVER Item` name the same one), and the area's
			// value carried down would overwrite every subtotal beneath it. So the fold gives it a
			// slot of its own (AggNeedsOwnSlot) and it is published BY ALIAS, exactly as COUNT(*) is.
			bool serverComputes = false;
			ibBackendColumnRawDB::RawType overRaw = ibBackendColumnRawDB::RawType::Number;
			if (col != nullptr && ScalarRawType(col, overRaw) && b.CanPushWindow()) {
				serverComputes = true;
				std::vector<ibQueryColumnExprPtr> partition;
				for (const ibBackendQueryColumn* key : scopeCols)
					partition.push_back(ibQueryColumnExpr::Col(key));
				const wxString windowAlias = wxString::Format(wxT("over%u"),
					static_cast<unsigned>(nextSynthId));
				b.SelectExpr(ibQueryColumnExpr::WindowAgg(WindowFnOf(agg->m_func),
				                                          ibQueryColumnExpr::Col(col), std::move(partition)),
				             windowAlias);
				// The receiver the fold reads it back through — a column of its own, so the figure does
				// not land on top of the one it was computed from.
				b.AggregateReceiver(outName,
					std::make_shared<ibSyntheticScalarColumn>(windowAlias, nextSynthId++, overRaw));
			}
			if (!serverComputes)
				col = nullptr;   // folded here, into a slot of its own — so it is read by NAME
		}

		OutputColumn oc; oc.m_name = outName;
		oc.m_role = ibQueryLowering::ibColumnRole::Measure;   // a TOTALS aggregate — the report's resource
		if (col != nullptr) { oc.m_col = col; oc.m_ownedCol = owned; }   // real OR synthetic column — keyed by metaID
		else { oc.m_alias = outName; oc.m_byAlias = true; }              // COUNT(*), or an area folded here — read by name
		outSchema.push_back(oc);
	}

	// ⭐⭐ AND THE SELECTed FIELDS — ALWAYS, whether or not the rows were asked for.
	//
	// A column is DECLARED by the query and FILLED by the row that has one: a heading holds no single
	// value for a field the group does not fold by, so it reads empty there, and a detail row unfolds
	// it (Max, 2026-08-22: "the column is there; if the group has no value for it, it comes back
	// empty, and the detail records unfold it").
	//
	// It used to be added only under `withDetails`, and that made the RESULT'S SCHEMA depend on the
	// traversal — so a script walking `SELECT Posted … TOTALS COUNT(Number) BY Posted` could not name
	// a SELECTed field at all: `selection["…"]` has nothing to look up, and "how do I read the value
	// then" has no answer. A schema that appears and disappears is not a schema.
	//
	// Costs nothing extra to READ: the query already projects these fields — the door was told to
	// select them — only the schema omitted them.
	//
	// ⚠ On a SERVER-side fold there are no detail rows at all, so these columns stay empty at every
	// level. That is honest — empty because no row carries them, not because the column vanished —
	// and it is one more argument for the phantom level, which brings the rows back there too.
	{
		int projectionIndex = 0;
		for (const ibQueryProjection& p : ast.m_projections) {
			if (p.m_star || !p.m_expr)
				continue;
			const wxString name = OutputNameFor(ast, p, projectionIndex++);

			// Already in the schema — a dimension or a resource of that name prints it, and a
			// second column under one name would be the same field twice.
			bool already = false;
			for (const OutputColumn& taken : outSchema)
				if (taken.m_name.IsSameAs(name, false)) { already = true; break; }
			if (already)
				continue;

			OutputColumn oc;
			oc.m_name = name;
			oc.m_role = ibQueryLowering::ibColumnRole::Detail;

			if (p.m_expr->m_kind == ibQueryAstExprKind::Column) {
				const std::vector<const ibBackendQueryColumn*> pathCols = ResolvePath(sources, *p.m_expr);
				const ibBackendQueryColumn* leaf = pathCols.back();
				if (pathCols.size() > 1) {
					// A DOT-WALK, by the road this query is already on: several sources expand the
					// reference chain into explicit joins, a single source lets the door resolve the
					// path. Either way the LEAF is what the column holds.
					if (multiSource) {
						const ibBackendQueryColumn* dwLeaf =
							ExpandDotWalkJoins(b, RootForPath(sources, *p.m_expr), pathCols, dwJoined, dwAliasSeq, *p.m_expr);
						b.Select(dwLeaf, name);
						oc.m_type = dwLeaf->GetTypeDesc();
					}
					else {
						b.SelectPath(pathCols, name);
						oc.m_type = leaf->GetTypeDesc();
					}
					oc.m_alias = name;
					oc.m_byAlias = true;
				}
				else {
					b.Select(leaf, name);
					oc.m_type = leaf->GetTypeDesc();
					// ⚠ AN ALIAS ONLY FETCHES A RAW COLUMN. Anything else is a SPREAD of physical
					// fields the provider projects under its own names, so a by-alias read finds
					// nothing and hands back the type's default — indistinguishable from a table of
					// blank rows. The flat read documents the same trap at its own projection.
					if (leaf->IsRawColumn()) { oc.m_alias = name; oc.m_byAlias = true; }
					else                     { oc.m_col = leaf; }
				}
			}
			else {
				// A COMPUTED field — projected once, read back under its own name.
				b.SelectExpr(BuildColumnExprFromAst(sources, *p.m_expr, params), name);
				oc.m_alias = name;
				oc.m_byAlias = true;
			}
			outSchema.push_back(oc);
		}
	}

	// WHERE (flat verbs or the boolean tree) — dot-walk rejected here (the totals fold is its own path).
	if (ast.m_where) {
		if (IsFlatAndWhere(*ast.m_where))
			LowerFlatWhere(b, sources, *ast.m_where, params, /*allowDotWalk*/false);
		else
			b.Where(BuildWherePredicate(sources, *ast.m_where, params, /*allowDotWalk*/false));
	}

	// AND THE VIRTUAL TABLE'S OWN CONDITION, by the same road as the flat read (PopulateBuilder).
	// A totals query reads its rows through this builder like any other, so an argument written
	// inside `Balance(&P, Warehouse = &W)` has to reach it here too — otherwise the same query
	// would filter when read plainly and not filter when read with TOTALS, which is the kind of
	// difference nobody would think to look for.
	for (const ibQueryAstExprPtr& condition : totalsSourceConditions) {
		if (!condition)
			continue;
		if (IsFlatAndWhere(*condition))
			LowerFlatWhere(b, sources, *condition, params, /*allowDotWalk*/false);
		else
			b.Where(BuildWherePredicate(sources, *condition, params, /*allowDotWalk*/false));
	}

	// One read → one snapshot, with the TotalBy config STAMPED on the result; the runtime's
	// QueryResult.Select() folds it (ByGroupsHierarchy) — no second query, so detail and subtotal
	// cannot skew. The synthetic measure columns live in outSchema (m_ownedCol), which travels with the
	// selection — so the result's stamped raw pointers into them stay valid through Select(). (docs §22.1b)
	//
	// TOP n caps the DETAIL rows the fold runs over — the first n (0 = all); the subtotals then roll over
	// exactly those rows. The tree itself is not row-limited (a subtotal is not a detail row). (`topPage`, not the
	// fetch `page` parameter — the fold reads its own detail slice; the fetch page only drives the group-level fast path above.)
	// ⭐ ORDER BY REACHES THE DETAIL READ, and through it the whole report. The fold keeps groups in
	// FIRST-SEEN order, and the rows inside a group in the order they arrived — so the order the
	// detail is read in IS the order the report comes out in, at every level. Without this a sort set
	// in the settings changed the query text and nothing else: the report came back in whatever order
	// the table happened to yield (Max, 2026-08-20: "I set a filter and a sort, compose the report,
	// and it does not react").
	//
	// ⭐⭐ AND THE LEVELS SORT IT FIRST. A grouping is not only something PRINTED above the rows — it
	// shapes the whole selection: rows of one group have to arrive together, and the groups have to
	// arrive in the same order twice (Max, 2026-08-22: "the totals may not even be output, but they
	// affect your whole selection"). The fold keeps groups in FIRST-SEEN order, so without this the
	// order of the headings was whatever order the table happened to yield the rows in — stable
	// enough to look right in a test and not stable at all.
	//
	// The levels go in BEFORE the query's own ORDER BY, which is what makes the two agree instead of
	// fight: the grouping decides the sequence of the groups, and `ORDER BY` decides the sequence
	// INSIDE the deepest one.
	//
	// ⚠ …UNLESS THE AUTHOR NAMED THE LEVEL THEMSELVES. `TOTALS … BY Period ORDER BY Period DESC` asks
	// for descending groups, and a forced ASC in front of it wins forever — the DESC key can never
	// fire, so the request is silently discarded. So a level whose column the query already sorts by
	// takes the AUTHOR'S direction, and that entry is struck from the list below rather than emitted
	// twice.
	std::vector<bool> orderConsumed(ast.m_orderBy.size(), false);
	std::vector<bool> levelAscending(levelOrder.size(), true);
	for (size_t li = 0; li < levelOrder.size(); ++li) {
		const LevelSort& ls = levelOrder[li];
		if (ls.m_col == nullptr)
			continue;
		for (size_t oi = 0; oi < ast.m_orderBy.size(); ++oi) {
			const ibQueryOrderItem& o = ast.m_orderBy[oi];
			if (orderConsumed[oi] || !o.m_expr || IsComputedExprAst(*o.m_expr))
				continue;
			std::vector<const ibBackendQueryColumn*> oc;
			try { oc = ResolveWhereTarget(sources, *o.m_expr, /*allowDotWalk*/true); }
			catch (const ibBackendException&) { continue; }
			if (oc.size() == 1 && oc.front() == ls.m_col) {
				levelAscending[li] = o.m_ascending;
				orderConsumed[oi]  = true;
				break;
			}
		}
	}

	// ⭐⭐ THE AUTHOR'S SORT GOES FIRST, AND THAT IS WHAT DECIDES THE ORDER OF THE GROUPS.
	//
	// 🛑 The levels used to be emitted ahead of it, on the reasoning that "a grouping decides the
	// sequence of the groups and ORDER BY decides the sequence inside the deepest one". That is true
	// of a grouping's KEY and false of everything else a person may sort by — and the difference is
	// the whole feature. Grouped by Ref and sorted by the moment, the reading came back ordered by
	// the reference's GUID: the level's key stood in front, and the moment could only ever break ties
	// INSIDE a group of one row. The setting did nothing, at any grain (Max, 2026-08-25, watching a
	// report of documents come out in identifier order: *"it does not react to this"*).
	//
	// The fold keeps groups in FIRST-SEEN order, so ordering the DETAIL by what the author asked for
	// is exactly what orders the groups by it: the group appears where its first row does. Sorting by
	// a document's moment therefore lists the documents by moment, which is what the words mean.
	//
	// The level keys still follow, and they still matter: they are the tie-break that keeps a group's
	// rows together when the author's key does not tell them apart, and they keep the order
	// REPEATABLE — two runs of the same query hand the groups over in the same sequence. A level
	// whose column the author already named is not emitted twice; it takes their direction (above)
	// and rides in its own place.
	for (size_t oi = 0; oi < ast.m_orderBy.size(); ++oi) {
		if (orderConsumed[oi])
			continue;                       // a level already sorts by it, in the direction asked for
		const ibQueryOrderItem& o = ast.m_orderBy[oi];
		if (!o.m_expr)
			continue;
		const ibQueryAstExpr& oe = *o.m_expr;
		if (IsComputedExprAst(oe)
		    || oe.m_kind == ibQueryAstExprKind::Param
		    || oe.m_kind == ibQueryAstExprKind::Value
		    || oe.m_kind == ibQueryAstExprKind::Literal) {
			b.OrderByExpr(BuildColumnExprFromAst(sources, oe, params), o.m_ascending);
			continue;
		}
		const std::vector<const ibBackendQueryColumn*> cols = ResolveWhereTarget(sources, oe, /*allowDotWalk*/true);
		if (cols.empty())
			continue;

		if (cols.size() > 1) b.OrderBy(cols, o.m_ascending);
		else                 b.OrderBy(cols[0], o.m_ascending);
	}

	// …AND THEN THE LEVELS. Behind the author's keys, where they are the tie-break rather than the
	// verdict: rows the sort cannot tell apart still arrive grouped, and the sequence stays the same
	// from one run to the next. With no sort written at all these are the whole order, exactly as
	// they were.
	for (size_t li = 0; li < levelOrder.size(); ++li) {
		const LevelSort& ls = levelOrder[li];

		if (!ls.m_path.empty()) {
			b.OrderBy(ls.m_path, levelAscending[li]);
			continue;
		}

		// ⭐ BY NAME — resolved against the source this read actually has, which is the declared query
		// (`q_sub0`) and not the table the walk ended on. See the note on LevelSort::m_name.
		if (!ls.m_name.IsEmpty()) {
			const ibBackendQueryable* src = b.GetPrimarySource();
			if (const ibBackendQueryColumn* named = src != nullptr ? src->ResolveColumnByName(ls.m_name) : nullptr) {
				b.OrderBy(named, levelAscending[li]);
				continue;
			}
			// Nothing of that name out here — say so rather than sort by something else. A level whose
			// key the source does not publish is a defect in the declaration, not a preference.
			ibJournalInfo(wxT("query.road"),
				wxT("level sort '%s' is not published by the source it reads - the level is left unsorted"),
				ls.m_name);
			continue;
		}

		b.OrderBy(ls.m_col, levelAscending[li]);
	}

	// ⭐ SAID IN THE JOURNAL, AS TWO NUMBERS — because "the sort does not react" is a complaint about
	// this line and nothing else, and the two roads it could have taken look identical from outside.
	// How many of the author's keys survived resolution, and how many level keys follow them: a sort
	// that resolved to nothing prints 0 and names itself, instead of leaving a person to compare
	// GUIDs by eye.
	{
		size_t authorKeys = 0;
		for (size_t oi = 0; oi < ast.m_orderBy.size(); ++oi)
			if (!orderConsumed[oi])
				++authorKeys;
		ibJournalInfo(wxT("query.order"),
			wxT("totals: %u author key(s) first, then %u level key(s)"),
			static_cast<unsigned>(authorKeys), static_cast<unsigned>(levelOrder.size()));
	}

	ibReadPageRequest topPage;
	if (ast.m_top > 0) topPage.m_count = ast.m_top;
	// FOR UPDATE reaches the DETAIL read — the one that actually touches rows. A subtotal holds
	// nothing of its own, so locking the detail is what the word can honestly mean here.
	if (ast.m_forUpdate) topPage.m_lockForUpdate = true;

	// ⭐⭐ LET THE DBMS FOLD IT. Until now every TOTALS came back as DETAIL ROWS that were folded
	// here, in memory — the composition's whole point (report over many rows) paying for exactly
	// what it should not. The push-down (GROUP BY ROLLUP) computes every subtotal level server-side
	// and returns only the aggregated rows; the tree arrives built, and the walk reads it.
	//
	// Refused where it would change the answer rather than the cost: TOP limits the DETAIL rows the
	// fold runs over, and FOR UPDATE has to touch the rows it locks — neither survives being folded
	// away, so both keep the detail read. Everything else asks the shape and the driver.
	// …and refused when the reader asked for the DETAIL ROWS: ROLLUP folds them away server-side, so
	// there would be nothing left to hang under the last heading. Details cost the rows by
	// definition — the choice is the reader's, and this is where it is paid.
	// ⭐⭐ WHICH ROAD, AND WHY — the one decision in this function nobody can see from outside, and the
	// one that decides whether a report answers in a second or in a minute. Four separate things can
	// send a fold into memory (rows were asked for, TOP, FOR UPDATE, or the shape the DBMS refused),
	// and every one of them is silent by construction: the answer is identical either way, only the
	// cost differs. So the journal says which road was taken and what stood in the way of the other.
	const bool eligible = (ast.m_top == 0 && !ast.m_forUpdate && !withDetails);
	if (eligible) {
		// The result has no default state — an empty one is made the way every empty result is.
		ibDataQueryResult folded = ibMakeEmptyQueryResult(nullptr);
		if (b.TryTotalsPushdown(folded)) {
			ibJournalInfo(wxT("query.totals"), wxT("%u level(s), %u resource(s): folded by the DBMS"),
				static_cast<unsigned>(ast.m_totalsBy.size()),
				static_cast<unsigned>(ast.m_totalsAggregates.size()));
			DetachSchemaFromRunSources(outSchema, owner);
			return folded;
		}
		ibJournalInfo(wxT("query.totals"), wxT("%u level(s), %u resource(s): folded in memory ")
			wxT("- the engine or the driver refused the shape"),
			static_cast<unsigned>(ast.m_totalsBy.size()),
			static_cast<unsigned>(ast.m_totalsAggregates.size()));
	}
	else {
		ibJournalInfo(wxT("query.totals"), wxT("%u level(s), %u resource(s): folded in memory - %s"),
			static_cast<unsigned>(ast.m_totalsBy.size()),
			static_cast<unsigned>(ast.m_totalsAggregates.size()),
			withDetails      ? wxT("the rows were asked for")
			: ast.m_forUpdate ? wxT("FOR UPDATE must touch the rows it locks")
			                  : wxT("TOP caps the detail rows the fold runs over"));
	}

	ibDataQueryResult detail = b.Execute(topPage);
	DetachSchemaFromRunSources(outSchema, owner);   // the schema leaves; the sources do not
	return detail;
}
