////////////////////////////////////////////////////////////////////////////
//	Description : L4-1 — the living set of temp tables (queryTempStore.h)
////////////////////////////////////////////////////////////////////////////

#include "queryTempStore.h"

#include "queryable.h"        // ibBackendQueryable + ibComputedProviderInstance
#include "queryColumn.h"      // ibBackendQueryColumn

#include <map>
#include <unordered_map>   // per-column index — keyed by the value, see ibValueHash (value.h)

namespace {

// A column of a stored temp table — name + type + the id its cells are stored at. NO attribute
// behind it, which is exactly why a temp table can exist without a metaobject.
class ibTempStoreColumn : public ibBackendQueryColumn
{
public:
	ibTempStoreColumn(const wxString& name, const ibTypeDescription& type, ibMetaID id)
		: m_name(name), m_type(type), m_id(id) {}

	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }

private:
	wxString                  m_name;
	mutable ibTypeDescription m_type;   // mutable: GetTypeDesc() is const but the interface returns a non-const ref
	ibMetaID                  m_id;
};

// A snapshot standing in the source registry as an ordinary queryable, so the statements after it
// read it by name exactly like a catalog. The RAM twin of ibTempTableQueryable (which wraps a
// RUNTIME value table): same shape, but it owns an L3 snapshot directly, because that is what a
// select produced.
class ibTempStoreQueryable : public ibBackendQueryable
{
public:
	ibTempStoreQueryable(ibQueryRamTable&& rows, const std::vector<wxString>& indexedColumns)
		: m_rows(std::move(rows))
	{
		for (const ibQueryRamColumn& c : m_rows.Columns())
			m_columns.push_back(std::make_unique<ibTempStoreColumn>(c.m_name, c.m_type, c.m_id));

		// THE INDEX, built once when the table is stored. A map from the value in an indexed column
		// to the rows carrying it — which is the whole of what an index is, and the reason a read
		// filtering that column does not have to walk the table.
		//
		// ⚠ KEYED BY THE VALUE'S IDENTITY, NEVER BY ITS PRESENTATION. `GetString()` of a
		// REFERENCE is its description — two different references sharing one would share a
		// bucket, and, far worse, a lookup whose presentation differs by so much as a renamed
		// item would miss its bucket entirely and the matching rows would SILENTLY VANISH. The
		// filter applied downstream can drop extra rows; it cannot bring back rows the index
		// never handed over.
		//
		// ibValueHash / ibValueEqual (value.h) key by the value itself — a reference compares by
		// guid there, which is the property this needs. It used to render GetHashKey() into a
		// string per row and index THAT; same identity, one text conversion per row cheaper.
		for (const wxString& name : indexedColumns) {
			for (const ibQueryRamColumn& c : m_rows.Columns()) {
				if (!c.m_name.IsSameAs(name, false))
					continue;
				auto& byValue = m_indexes[c.m_id];
				for (long r = 0; r < m_rows.RowCount(); ++r)
					byValue[m_rows.GetCell(r, c.m_id)].push_back(r);
				break;
			}
		}
	}

	const ibBackendQueryColumn* Column(const wxString& name) const
	{
		for (const auto& c : m_columns)
			if (c->GetName().IsSameAs(name, false)) return c.get();
		return nullptr;
	}

	bool OwnsColumn(const ibBackendQueryColumn* col) const override
	{
		for (const auto& c : m_columns)
			if (c.get() == col) return true;
		return false;
	}

	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override { return Column(name); }

	std::vector<const ibBackendQueryColumn*> GetColumns() const override
	{
		std::vector<const ibBackendQueryColumn*> out;
		out.reserve(m_columns.size());
		for (const auto& c : m_columns) out.push_back(c.get());
		return out;
	}

	bool IsComputedInRam() const override { return true; }

	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }

	// Handed out BY COPY per read: ibQueryRamTable is move-only (a copy of a result is never
	// accidental), and a temp table is read more than once by design — that is the point of it.
	//
	// AND THIS IS WHERE AN INDEX PAYS. When the read carries an equality on an indexed column, the
	// rows come from the map; otherwise every row is copied, as before. The filter is still applied
	// downstream either way — the index changes how many rows are looked at, never which ones match.
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override
	{
		ibQueryRamTable t;
		for (const ibQueryRamColumn& c : m_rows.Columns())
			t.AddColumn(c.m_id, c.m_name, c.m_type);

		const std::vector<long>* rows = nullptr;
		for (const ibQueryCondition& condition : extra) {
			if (condition.m_op != ibQueryFilterOp::Equal || condition.m_col == nullptr)
				continue;
			if (!condition.m_path.empty())
				continue;   // a dot-walk leaf is not this table's own column
			const auto index = m_indexes.find(condition.m_col->GetColumnId());
			if (index == m_indexes.end())
				continue;
			// The SAME key on the way in and on the way out — see the ctor. Asking with a
			// presentation for a table keyed by identity is how an index answers "no rows" to a
			// question that has rows.
			const auto found = index->second.find(condition.m_value);
			rows = found != index->second.end() ? &found->second : &s_noRows;
			break;   // one index is enough; the rest of the conditions still filter downstream
		}

		auto copyRow = [this, &t](long source) {
			const long dst = t.AppendRow();
			for (const ibQueryRamColumn& c : m_rows.Columns())
				t.SetCell(dst, c.m_id, m_rows.GetCell(source, c.m_id));
		};

		if (rows != nullptr) {
			for (const long r : *rows)
				copyRow(r);
		}
		else {
			for (long r = 0; r < m_rows.RowCount(); ++r)
				copyRow(r);
		}
		return t;
	}

	wxString GetQueryTableName() const override { return wxEmptyString; }
	ibGuid   GetQueryTableGuid() const override { return wxNullGuid; }
	ibMetaID GetQueryTableId()   const override { return 0; }
	std::vector<ibQuerySortItem> GetIdentitySort() const override { return {}; }

private:
	ibQueryRamTable                                  m_rows;
	std::vector<std::unique_ptr<ibTempStoreColumn>>  m_columns;
	// INDEXED BY: column id -> the value each row carries -> the rows carrying it.
	std::map<ibMetaID, std::unordered_map<ibValue, std::vector<long>, ibValueHash, ibValueEqual>> m_indexes;
	static const std::vector<long> s_noRows;   // an indexed value nothing has — no rows, not all rows
};

const std::vector<long> ibTempStoreQueryable::s_noRows;

} // namespace

ibQueryTempTableStore::~ibQueryTempTableStore()
{
	Close();
}

bool ibQueryTempTableStore::Has(const wxString& name) const
{
	return m_sources.find(name) != m_sources.end();
}

void ibQueryTempTableStore::Put(const wxString& name, ibQueryRamTable&& rows,
                                const std::vector<wxString>& indexedColumns)
{
	auto table = std::make_unique<ibTempStoreQueryable>(std::move(rows), indexedColumns);
	m_sources[name] = table.get();
	m_owned.push_back(std::move(table));
}

bool ibQueryTempTableStore::Drop(const wxString& name)
{
	const auto it = m_sources.find(name);
	if (it == m_sources.end())
		return false;

	// The NAME goes first, and the rows with it — a dropped table that stayed alive behind a
	// removed name would be memory nobody can reach and nobody can free.
	const ibBackendQueryable* dropped = it->second;
	m_sources.erase(it);
	for (auto owned = m_owned.begin(); owned != m_owned.end(); ++owned) {
		if (owned->get() != dropped) continue;
		m_owned.erase(owned);
		break;
	}
	return true;
}

void ibQueryTempTableStore::Close()
{
	m_sources.clear();
	m_owned.clear();
}
