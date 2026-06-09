#ifndef __QUERY_RAM_TABLE_H__
#define __QUERY_RAM_TABLE_H__

// ibQueryRamTable — the flat raw RESULT of an L3 read (ibDataQueryBuilder::Run): a pure array of
// rows, built for FAST extraction (by row index + column model id). It is the SNAPSHOT, and that is
// ALL it is — it carries NO tree. Splitting a snapshot into folders / subtotal levels is the
// Selector's job (ibSelector over this snapshot), and the product is a SEPARATE ibSelectorTree
// (querySelectorTree.h). L3 names no runtime type but ibValue: cells are ibValue, columns are
// id/name/type. Turning the snapshot (or the folded tree) into a runtime model is the RUNTIME's
// job. (docs/query-language-arc.md §22.1, §22.1b)

#include "queryColumn.h"                 // ibBackendQueryColumn / ibTypeDescription / ibMetaID
#include "backend/compiler/value.h"      // ibValue

#include <map>
#include <vector>

// One column of the snapshot — keyed by its model id (the same GetModelID the rows use).
struct ibQueryRamColumn
{
	ibMetaID          m_id;
	wxString          m_name;
	ibTypeDescription m_type;
};

class BACKEND_API ibQueryRamTable
{
public:
	using Row = std::map<ibMetaID, ibValue>;   // a row keyed by column model id

	// Move-only (symmetry with ibSelectorTree; a snapshot is moved through the composer, never copied
	// wholesale — copy is deep over the ibValue rows, so it is deleted to catch accidental copies).
	ibQueryRamTable() = default;
	ibQueryRamTable(ibQueryRamTable&&) = default;
	ibQueryRamTable& operator=(ibQueryRamTable&&) = default;
	ibQueryRamTable(const ibQueryRamTable&) = delete;
	ibQueryRamTable& operator=(const ibQueryRamTable&) = delete;

	void AddColumn(ibMetaID id, const wxString& name, const ibTypeDescription& type) { m_columns.push_back({ id, name, type }); }
	const std::vector<ibQueryRamColumn>& Columns() const { return m_columns; }

	long    AppendRow()                                       { m_rows.emplace_back(); return static_cast<long>(m_rows.size()) - 1; }
	void    SetCell(long row, ibMetaID id, const ibValue& v)  { if (row >= 0 && row < RowCount()) m_rows[static_cast<size_t>(row)][id] = v; }
	// Set by column NAME — resolves to that column's id. For derived columns read by alias
	// (register balance / turnover X_Balance / X_Turnover…). No-op if the name is unknown.
	void    SetByName(long row, const wxString& name, const ibValue& v) {
	            for (const ibQueryRamColumn& c : m_columns) if (c.m_name == name) { SetCell(row, c.m_id, v); return; } }
	ibValue GetCell(long row, ibMetaID id) const              { if (row < 0 || row >= RowCount()) return ibValue();
	                                                             const Row& r = m_rows[static_cast<size_t>(row)];
	                                                             const auto it = r.find(id); return it != r.end() ? it->second : ibValue(); }
	long    RowCount() const                                  { return static_cast<long>(m_rows.size()); }

private:
	std::vector<ibQueryRamColumn> m_columns;
	std::vector<Row>              m_rows;      // flat rows — the whole snapshot
};

#endif // __QUERY_RAM_TABLE_H__
