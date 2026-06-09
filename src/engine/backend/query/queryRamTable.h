#ifndef __QUERY_RAM_TABLE_H__
#define __QUERY_RAM_TABLE_H__

// ibQueryRamTable — the ONE UNIFIED L3 result table, DECOUPLED from the runtime/UI type.
// It is canonical: every L3 read / join / union / ИТОГИ produces THIS, and it is
// optimised for BOTH shapes at once — a FLAT list (the root's direct children, one level)
// and a TREE (nested subtotal nodes).
//
// L3 NAMES NO RUNTIME TYPE BUT ibValue: cells are ibValue, columns are id/name/type, the
// tree is plain nodes. There is NO ToModelTable here — converting this raw tree into a
// runtime model (ibValueModelTable, or a runtime tree model) is the RUNTIME's job: the
// runtime includes this header, walks Root()/Node, and builds whatever it renders. L3
// hands back the raw tree and stops; ibValueModelTable never appears in L3.
//
// The hierarchical-totals result (TOTALS <fields> BY <sums>) is a real TREE: the root is the
// grand total, each grouping level a subtotal node carrying its key path + the folded sums, the
// deepest level the detail. L3 returns this RAW — flat-vs-hierarchical rendering is the
// RUNTIME's decision, NOT L3's. (docs/query-language-arc.md §22.1b)

#include "queryColumn.h"                 // ibBackendQueryColumn / ibTypeDescription / ibMetaID
#include "backend/compiler/value.h"      // ibValue

#include <map>
#include <vector>
#include <memory>

// One column of the L3 table — keyed by its model id (the same GetModelID the rows use).
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

	// A totals node: its values (group-key path + aggregates), its grouping level
	// (0 = grand-total root), and its child nodes (next level).
	struct Node
	{
		Row                                m_values;
		int                                m_level = 0;
		std::vector<std::unique_ptr<Node>> m_children;

		Node* AddChild(int level) { m_children.push_back(std::make_unique<Node>()); m_children.back()->m_level = level; return m_children.back().get(); }
	};

	// Move-only (the totals tree owns its nodes via unique_ptr). Declared explicitly so the
	// BACKEND_API dllexport does NOT instantiate the implicit copy ops (ill-formed through
	// unique_ptr<Node>) — it only exports the moves it actually uses.
	ibQueryRamTable() = default;
	ibQueryRamTable(ibQueryRamTable&&) = default;
	ibQueryRamTable& operator=(ibQueryRamTable&&) = default;
	ibQueryRamTable(const ibQueryRamTable&) = delete;
	ibQueryRamTable& operator=(const ibQueryRamTable&) = delete;

	void AddColumn(ibMetaID id, const wxString& name, const ibTypeDescription& type) { m_columns.push_back({ id, name, type }); }
	const std::vector<ibQueryRamColumn>& Columns() const { return m_columns; }

	// --- flat-list face (composition scratch + flat results) ----------------
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

	// --- tree face (ИТОГИ) --------------------------------------------------
	Node&       Root()       { return m_root; }
	const Node& Root() const { return m_root; }

	// No conversion to a runtime model lives here — that is the runtime's job (walk
	// the flat rows or Root()/Node). L3 names no runtime type but ibValue.

private:
	std::vector<ibQueryRamColumn> m_columns;
	std::vector<Row>              m_rows;      // flat rows
	Node                          m_root;      // grand-total root (ИТОГИ tree)
};

#endif // __QUERY_RAM_TABLE_H__
