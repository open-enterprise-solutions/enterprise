////////////////////////////////////////////////////////////////////////////
//	Description : the L3 SELECTION — ibDataQueryResult, a thin value handle over a
//	              polymorphic ibDataResultSource (resultSource.h): the backing that
//	              is a RAM table (ibRamTableResultSource), the rows handed to a fold
//	              as a cursor (Select). The DB cursor's backing and the page cache
//	              (ibDataQueryBuilder::NewPageCache) are dbTableProvider.cpp's; the roads that make a
//	              result are queryComposer.cpp's. See docs/private/query-language-arc.md §22.4d.
////////////////////////////////////////////////////////////////////////////

#include "queryProvider.h"            // ibDataQueryResult (+ dataQueryBuilder.h, queryable.h); ibBackendQueryProvider::ReadReferences
#include "queryRamTable.h"            // ibQueryRamTable — the backing that is a table
#include "queryRowCursor.h"           // ibQueryRowCursor — the rows Select hands to a fold
#include "querySelector.h"            // ibSelector — what Select answers
#include "querySelectorTree.h"        // ibSelectorTree — the push-down's ready tree
#include "resultSource.h"             // ibDataResultSource + EvalOverResultRow — a column computed over the row

#include <map>
#include <memory>
#include <stdexcept>             // GetTable() && on a result read row by row
#include <utility>

namespace {

// RAM table source — walks an ibQueryRamTable (L3's OWN table; NO runtime type). Value(col)
// reads by the column's model id (register-slice dimensions self-describe their metaID; temp
// columns their source id); Column(alias) by the output column name (aggregates / derived
// columns with no column object). EVERY RAM source rides on this — multi-source composition
// AND a computed virtual table (slice / balance / turnover / temp), so neither produces a
// runtime ibValueModelTable. Needs no queryable. (docs §22.1a, §22.6)
class ibRamTableResultSource : public ibDataResultSource {
public:
	ibRamTableResultSource(ibQueryRamTable&& table, const std::shared_ptr<const ibBackendQueryable>& /*queryable*/)
		: m_table(std::move(table)) {}

	bool Next() override { return ++m_row < m_table.RowCount(); }

	const ibQueryRamTable* Table() const & override {
		return m_row < 0 ? &m_table : nullptr;   // a walk has begun — what it already passed is not the caller's
	}
	ibQueryRamTable Table() && override {
		if (m_row >= 0)
			return std::move(*this).ibDataResultSource::Table();   // the walk's refusal, said once
		return std::move(m_table);
	}

	ibValue Value(const std::shared_ptr<const ibBackendQueryColumn>& col) const override {
		return (m_row >= 0 && col != nullptr) ? m_table.GetCell(m_row, col->GetColumnId()) : ibValue();
	}
	// A COMPOSED TABLE HOLDS VALUES, NOT FIELDS — whatever was spread on the way here was reassembled
	// before it was poured, so both shapes are one lookup by name and `col` has nothing to add. Said here
	// rather than inherited, because the same sentence is false of a cursor (dbTableProvider.cpp).
	ibValue Column(const ibString& name, const std::shared_ptr<const ibBackendQueryColumn>& /*col*/ = nullptr) const override {
		for (const ibQueryRamColumn& c : m_table.Columns())
			if (c.m_name == name) return m_table.GetCell(m_row, c.m_id);
		return ibValue();
	}

private:
	ibQueryRamTable m_table;     // owns the composed rows
	long            m_row = -1;
};

} // namespace

// ==========================================================================
// ibDataQueryResult — the L3 selection: a thin value handle over a polymorphic
// ibDataResultSource. Ready ibValue rows out; the backing is hidden even from this
// class. Special members out-of-line where the source types are complete.
// ==========================================================================

ibDataQueryResult::ibDataQueryResult(ibQueryRamTable&& ramTable, const std::shared_ptr<const ibBackendQueryable>& queryable)
{
	// Every RAM road ends here with its flat list whole — its references are told what they say by the
	// provider it was read from (ibBackendQueryProvider::ReadReferences), before anybody reads a row of it.
	if (queryable != nullptr)
		queryable->GetProvider().ReadReferences();
	m_source = std::make_shared<ibRamTableResultSource>(std::move(ramTable), queryable);
}

ibDataQueryResult::~ibDataQueryResult() = default;
ibDataQueryResult::ibDataQueryResult(ibDataQueryResult&&) noexcept = default;
ibDataQueryResult& ibDataQueryResult::operator=(ibDataQueryResult&&) noexcept = default;

bool     ibDataQueryResult::Next()                                                     { return m_source->Next(); }
ibValue  ibDataQueryResult::GetValue(const std::shared_ptr<const ibBackendQueryColumn>& col)            const { return m_source->Value(col); }
ibValue  ibDataQueryResult::GetColumn(const ibString& alias)                     const {
	// A column this result answers for ITSELF comes first: it is published under this name and the
	// backing has never heard of it. (SetComputedOverRow — the presentation of a value, an expression
	// standing over a fold.)
	for (const ibQueryColumnSelect& c : m_computedOverRow)
		if (c.m_alias.IsSameAs(alias, false) && c.m_expr)
			return EvalOverResultRow(c, *m_source);
	// …then an output that came back as a SPREAD: a computed value of a composite type is projected field
	// by field under a prefix, so it is reassembled by the reader that reassembles every other object
	// output. Asked by name here, it looks like any other column — which is the whole point.
	// …then an output that came back as a SPREAD: the result knows it, because the same question decided
	// how to project it. The backing is only told WHICH shape to read — one door, and no caller anywhere
	// has to know that an output was ever spread.
	for (const ibComputedSpread& s : m_computedSpreads)
		if (s.m_alias.IsSameAs(alias, false) && s.m_col != nullptr)
			return m_source->Column(s.m_prefix, s.m_col);
	return m_source->Column(alias);
}
ibValue  ibDataQueryResult::GetColumn(const ibString& prefix, const std::shared_ptr<const ibBackendQueryColumn>& col) const { return m_source->Column(prefix, col); }
// ⚠ NOT WHERE THIS RESULT ANSWERS A NAME ITSELF: GetColumn asks the computed-over-row columns first,
// and the table does not hold those.
const ibQueryRamTable* ibDataQueryResult::GetTable() const &
{
	return m_source != nullptr && m_computedOverRow.empty() ? m_source->Table() : nullptr;
}

// Taken only where the look answered a table — the backing refuses anything else.
ibQueryRamTable ibDataQueryResult::GetTable() &&
{
	if (GetTable() == nullptr)
		throw std::logic_error("ibDataQueryResult::GetTable: this result is read row by row - there is no table to take");
	return std::move(*m_source).Table();
}

void ibDataQueryResult::SetMaterialiseColumns(std::vector<std::shared_ptr<const ibBackendQueryColumn>> cols)
{
	m_matColumns = std::move(cols);
}

void ibDataQueryResult::SetComputedOverRow(std::vector<ibQueryColumnSelect> columns)
{
	m_computedOverRow = std::move(columns);
}

void ibDataQueryResult::SetComputedSpreads(std::vector<ibComputedSpread> spreads)
{
	m_computedSpreads = std::move(spreads);
}

void ibDataQueryResult::SetTotals(std::vector<ibTotalLevel> levels, std::vector<ibAggregateItem> aggregates,
	bool overall, std::vector<ibDerivedAggregate> derived)
{
	m_totalLevels     = std::move(levels);
	m_totalAggregates = std::move(aggregates);
	m_totalDerived    = std::move(derived);
	m_totalsOverall   = overall;
}

void ibDataQueryResult::SetReadyTree(ibSelectorTree tree)
{
	// The push-down's answer, carried to whoever walks this result. Held by shared pointer because a
	// result is moved around and the tree is not a thing to copy.
	m_readyTree = std::make_shared<ibSelectorTree>(std::move(tree));
}

void ibDataQueryResult::SetObjectRead(ibMetaID dimColumnId, const ibString& prefix, const std::shared_ptr<const ibBackendQueryColumn>& leaf)
{
	if (prefix.IsEmpty() || leaf == nullptr)
		return;
	m_objectReads[dimColumnId] = ibObjectRead{ prefix, leaf };
}

void ibDataQueryResult::SetSource(ibDatabaseConnectionHolder* holder, const std::shared_ptr<const ibBackendQueryable>& queryable,
	std::vector<std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>> selectCols,
	std::vector<ibQueryCondition> conditions)
{
	m_srcHolder     = holder;
	m_srcQueryable  = queryable;
	m_srcSelectCols = std::move(selectCols);
	m_srcConditions = std::move(conditions);
}

namespace {

// ⭐⭐ THE RESULT'S ROWS, HANDED OVER AS A CURSOR — and this is where "the fold takes a cursor"
// stops being an abstraction and becomes one fewer copy of the detail.
//
// Select() used to DRAIN the backing into an ibQueryRamTable right here: every row of the read,
// materialised, before the fold had seen any of it. A report over a register with a million
// movements held a million rows to print forty headings.
//
// The plan the door stamped travels with it — WHICH columns (m_matColumns) and HOW each is read
// (m_objectReads: a dot-walked dimension is a reference / enum / composite projected as a whole
// SPREAD under an alias prefix, so asking for the column itself would find nothing and the level
// would fold every row into one empty group).
class ibResultRowCursor : public ibQueryRowCursor
{
public:
	ibResultRowCursor(std::shared_ptr<ibDataResultSource> source,
	                  const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& cols,
	                  std::map<ibMetaID, std::pair<ibString, std::shared_ptr<const ibBackendQueryColumn>>> objectReads)
		: m_source(std::move(source)), m_objectReads(std::move(objectReads))
	{
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : cols)
			if (c != nullptr) {
				m_columns.push_back(ibQueryRamColumn{ c->GetColumnId(), c->GetName(), c->GetTypeDesc(), c->GetSynonym() });
				m_cols.push_back(c);
			}
	}

	bool Next() override { return m_source != nullptr && m_source->Next(); }

	ibValue Get(ibMetaID id) const override
	{
		if (m_source == nullptr)
			return ibValue();
		// (Asked only where there is anything to find: a fold reads a few cells of every row through
		// here, and a lookup in an empty map is still a lookup — a checked build locks for it.)
		if (!m_objectReads.empty()) {
			const auto objectRead = m_objectReads.find(id);
			if (objectRead != m_objectReads.end())
				return m_source->Column(objectRead->second.first, objectRead->second.second);
		}
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_cols)
			if (c->GetColumnId() == id)
				return m_source->Value(c);
		return ibValue();
	}

	const std::vector<ibQueryRamColumn>& Columns() const override { return m_columns; }

private:
	std::shared_ptr<ibDataResultSource>      m_source;    // shared with the result it came from — ONE scan
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_cols;      // the columns as the backing reads them
	std::vector<ibQueryRamColumn>            m_columns;   // …and as the tree declares them
	std::map<ibMetaID, std::pair<ibString, std::shared_ptr<const ibBackendQueryColumn>>> m_objectReads;
};

} // namespace

// selection = result.Select(mode). The rows go to the ibSelector AS A CURSOR — it folds them per the
// mode (streamed for the group folds, drained into a table for the ones that need every row
// addressable), and the caller sees one interface, never the backing or the form.
ibSelector ibDataQueryResult::Select(ibSelectKind mode)
{
	// ⭐ THE DBMS MAY HAVE FOLDED IT ALREADY. When the totals were pushed down (GROUP BY ROLLUP), the
	// tree is complete and there is no cursor of detail rows to read — reading one here would fetch
	// exactly the rows the push-down exists to leave in the database.
	if (m_readyTree) {
		ibSelector s(ibQueryRamTable(), mode);
		s.WithTotals(m_totalLevels, m_totalAggregates, m_totalsOverall, m_totalDerived);
		s.WithReadyTree(m_readyTree);
		return s;
	}

	std::map<ibMetaID, std::pair<ibString, std::shared_ptr<const ibBackendQueryColumn>>> objectReads;
	for (const auto& kv : m_objectReads)
		objectReads.emplace(kv.first, std::make_pair(kv.second.m_prefix, kv.second.m_leaf));

	ibSelector s(std::make_unique<ibResultRowCursor>(m_source, m_matColumns, std::move(objectReads)), mode);
	// ⚠ …AND A DIRECT WALK IS NOT TOLD OF THE OVERALL ROW, because it folds nothing: its tree is the RECORDS, one flat
	// layer under a root that holds no figure. Told of it, the walk visited that root and nothing else — a totals
	// query read directly (`Select()`, `Unload()`) came back as ONE row with every column blank, over a register as
	// over a value table (2026-09-29). The totals are the grouped walk's.
	s.WithTotals(m_totalLevels, m_totalAggregates,
	             m_totalsOverall && mode != ibSelectKind::ibSelectKind_Direct,    // fold by the door's TotalBy config
	             m_totalDerived);
	s.WithSource(m_srcHolder, m_srcQueryable, m_srcSelectCols, m_srcConditions);  // enable lazy sub-selections
	// ⚠ AND IT IS NOT FOLDED HERE. A selection is configured AFTER it is made — MakeChild adds
	// ByParentRef / ByGroups / Aggregating to what this hands back — so folding at this point would
	// fold by a configuration that is not complete yet, and the settings arriving next would have
	// nothing left to change. Whoever finishes configuring is the one who says ReadRows().
	return s;
}

// ONE BRANCH OF THE SAME FOLD. The read, the fold and the tree are the ones above — only the WALK
// is narrowed, which is why this is a line and not a second Select: a branch is not another result.
ibSelector ibDataQueryResult::Select(ibSelectKind mode, const ibString& branch)
{
	// ⭐⭐ FOLDED ONCE, WALKED MANY TIMES. The branches share the fold as well as the read: the rows
	// come as a cursor, so the SECOND branch to ask would scan a cursor the first one already drank
	// dry — live, that surfaced as "Error retrieving Next record" from the driver the moment a
	// composition's second output started (2026-08-27).
	//
	// The fold happens on a selection of its own, thrown away straight after, because a selection
	// builds its tree and its VISITS together (EnsureWalk) and the visits depend on things the
	// caller states afterwards — the branch, WalkOverall, the level's OrderBy. Folding on the
	// branch's own selection would freeze a walk configured only half way.
	if (m_readyTree == nullptr && m_branchTree == nullptr) {
		ibSelector fold = Select(mode);
		fold.ReadRows();                  // one pass over the cursor, and it is released here
		m_branchTree = fold.FoldedTree();
	}

	ibSelector s = Select(mode);
	s.WalkBranch(branch);
	if (m_branchTree != nullptr)
		s.WithReadyTree(m_branchTree);    // hands over a Clone(), so one branch's walk cannot disturb another's
	return s;
}
