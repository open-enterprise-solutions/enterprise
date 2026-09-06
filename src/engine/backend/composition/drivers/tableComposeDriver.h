#ifndef __TABLE_COMPOSE_DRIVER_H__
#define __TABLE_COMPOSE_DRIVER_H__

// L5-1 — a composition read as DATA rather than drawn. TWO drivers here, because there are two
// shapes and they are not one thing with a flag:
//
//   ibGroupingComposeDriver — rows under headings, columns FIXED by the schema.
//   ibCrossComposeDriver    — rows under headings, columns MADE BY VALUES, resources at the crossing.
//
// ⭐⭐ THREE MECHANISMS, NOT ONE — and only the last two are these drivers' business
// (Max, 2026-09-06):
//
//   * a LIST is a PAGED output with a logic of its own — an envelope in, a page out
//     (ibListFetchDriver). ⚠ NOT A ROAD THESE SERVE, and deliberately: a schema is run HERE as a
//     report, whole, whatever it was read from. Paging is what a window needs to draw a screenful;
//     a reader comparing figures wants the figures. (A list also has no cross shape at all — its
//     columns are laid out by whoever designed the form, so nothing data-derived can appear across
//     it, which is why it leaves OnColumn at the base's do-nothing.)
//   * a REPORT is a DIRECT output, and it will be ASYNCHRONOUS: the output is started, it goes to a
//     broker, the broker gathers, and only then is the result handed over. So nothing here may
//     assume the data has arrived when Run() returns — these are accumulators and say nothing about
//     when they are read.
//   * a TABLE is its own thing — a schema in, values out, worked with directly. That is what these
//     two are for.
//
// ⭐ AND THE COMPOSER ALREADY EXPECTS THE SPLIT. A driver is attached PER OUTPUT
// (`ibDataComposer::Output::m_driver`), not per composition — the field is per output precisely so
// a composition declaring a grouping AND a cross-table can hand each of them the reader it needs.
// The spreadsheet driver takes both because it draws them onto one page; a reader comparing figures
// wants them apart, and one class answering "look at the kind to know what my values line up
// against" is one structure serving two questions.
//
// Neither driver computes anything. The composer has already folded, filtered and ordered; a driver
// is the passive sink of its walk, and one doing arithmetic of its own would be a second road
// obliged to agree with the first (docs/query-language-arc.md § 31.4 is the day that cost).
//
//   ibGroupingComposeDriver g; ibCrossComposeDriver x;
//   for (ibDataComposer::Output& o : composer.Outputs())
//       o.m_driver = <this output is a cross-table> ? (ibCompositionDriver*)&x : &g;
//   composer.Run();

#include "backend/composition/drivers/compositionDriver.h"   // a DRIVER needs the contract, not the composer

#include <vector>

// ==========================================================================
// What both keep about a node: its place in the fold, its figures, and its ADDRESS.
// ==========================================================================
struct ibComposedNode
{
	int                m_level  = 0;   // which rung of the fold this stands on
	int                m_indent = 0;   // …and how deep inside that rung's own tree
	ibSelectorNodeKind m_kind   = ibSelectorNodeKind::Group;
	// The fold's fact and the output's promise — two questions, kept apart for the reason the base
	// spends a paragraph on (ibCompositionDriver::OnGroupBegin).
	bool m_hasChildren      = false;
	bool m_showsWhatIsUnder = false;
	bool m_isHeading        = false;   // came from OnGroupBegin rather than OnRow

	std::vector<ibValue> m_values;     // the figures this node carries

	// ⭐⭐ WHERE IT STOOD — the headings open above it, outermost first, each as the values it was
	// opened with. Without this a figure is a number with no address: the walk emits ONE node at a
	// time and carries the position in a cursor of its own (the spreadsheet driver keeps one and
	// draws into it), so a driver that only appended would keep every figure and lose what every
	// figure is ABOUT.
	std::vector<std::vector<ibValue>> m_path;
};

// A column the schema declares. Three different things, and not interchangeable: the TITLE is what a
// person reads, the NAME is what a script says, the ID is what a value is keyed by — and only some
// have the last one, since an aggregate or a dot-walk is read back by its ALIAS with no source
// column behind it.
struct ibComposedColumn
{
	wxString m_title;
	wxString m_name;
	wxString m_alias;
	ibMetaID m_id    = 0;
	bool     m_hasId = false;   // …because 0 is a value
};

// The shared beginning: an output opens, its schema is recorded, open headings are tracked.
class BACKEND_API ibComposedDriverBase : public ibCompositionDriver
{
public:
	// ⭐ THE GRAND TOTAL IS WANTED. A reader checking whether a report adds up is asking about
	// exactly that row, and a driver that declines it gets a tree with no root.
	bool WantsGrandTotal() const override { return true; }

	const wxString&                      Name()    const { return m_name; }
	const std::vector<ibComposedColumn>& Columns() const { return m_columns; }
	bool                                 Totals()  const { return m_totals; }

	void OnOutputBegin(const ibCompositionOutputInfo& info) override
	{
		m_name = info.m_name;
		m_columns.clear();
		m_columns.reserve(info.m_schema.size());
		for (size_t i = 0; i < info.m_schema.size(); ++i) {
			const ibQueryLowering::OutputColumn& oc = info.m_schema[i];
			ibComposedColumn c;
			c.m_title = info.TitleOf(i);   // the output's own wording, falling back to the name
			c.m_name  = oc.m_name;
			c.m_alias = oc.m_alias;
			if (oc.m_col != nullptr) { c.m_id = oc.m_col->GetColumnId(); c.m_hasId = true; }
			m_columns.push_back(std::move(c));
		}
		m_open.clear();
	}

	void OnOutputEnd(bool totals) override { m_totals = totals; m_open.clear(); }

protected:

	ibComposedNode Node(const ibCompositionLine& line, const std::vector<ibValue>& values,
	                    bool heading) const
	{
		ibComposedNode n;
		n.m_level            = line.m_level;
		n.m_indent           = line.m_indent;
		n.m_kind             = line.m_kind;
		n.m_hasChildren      = line.m_hasChildren;
		n.m_showsWhatIsUnder = line.m_showsWhatIsUnder;
		n.m_isHeading        = heading;
		n.m_values           = values;
		n.m_path             = m_open;   // the address, snapshotted as it stood
		return n;
	}

	void Push(const std::vector<ibValue>& values) { m_open.push_back(values); }
	// Guarded: a walk closing more than it opened would take the stack negative and every address
	// after it would be wrong — silently, which is the failure worth spending a branch on.
	void Pop() { if (!m_open.empty()) m_open.pop_back(); }

private:
	wxString                          m_name;
	std::vector<ibComposedColumn>     m_columns;
	bool                              m_totals = false;
	std::vector<std::vector<ibValue>> m_open;   // the walk's cursor, kept because the walk hands none
};

// ==========================================================================
// A GROUPING output — the row dimension makes the groups, and the schema's columns ARE the columns:
// a line's values line up with Columns() positionally. Nothing reads across, so OnColumn stays at
// the base's do-nothing; a grouping has no column axis to lose.
// ==========================================================================
class BACKEND_API ibGroupingComposeDriver : public ibComposedDriverBase
{
public:
	const std::vector<ibComposedNode>& Lines() const { return m_lines; }

	void OnOutputBegin(const ibCompositionOutputInfo& info) override
	{
		ibComposedDriverBase::OnOutputBegin(info);
		m_lines.clear();
	}
	void OnGroupBegin(const ibCompositionLine& line, const std::vector<ibValue>& values) override
	{
		m_lines.push_back(Node(line, values, /*heading*/ true));
		Push(values);
	}
	void OnRow(const ibCompositionLine& line, const std::vector<ibValue>& values) override
	{
		m_lines.push_back(Node(line, values, /*heading*/ false));
	}
	void OnGroupEnd(const ibCompositionLine& /*line*/, const std::vector<ibValue>& /*values*/) override
	{
		Pop();
	}

private:
	std::vector<ibComposedNode> m_lines;
};

// ==========================================================================
// A CROSS-TABLE — the row dimension makes the groups, the COLUMN dimension makes NEW COLUMNS, one
// per value it meets, and the resources sit at the crossing. So the column set is declared nowhere:
// it GROWS with the data, which is the whole difference from the grouping above. `Columns()` (the
// schema's) still names the resources; the AXIS is what a reader lays the table out by.
//
// ⭐⭐ THE COLUMN AXIS IS SETTLED BEFORE THE FIRST ROW. The cells of the ROOT heading arrive first,
// so by the time any row is written every column it can land in already exists, IN THIS ORDER —
// which is why a printed cross-table carries its total column leftmost, ahead of the values. Kept in
// arrival order and never sorted: the order IS the axis.
// ==========================================================================
class BACKEND_API ibCrossComposeDriver : public ibComposedDriverBase
{
public:
	const std::vector<ibComposedNode>& Rows() const { return m_rows; }
	const std::vector<ibComposedNode>& Axis() const { return m_axis; }

	void OnOutputBegin(const ibCompositionOutputInfo& info) override
	{
		ibComposedDriverBase::OnOutputBegin(info);
		m_rows.clear();
		m_axis.clear();
	}
	void OnGroupBegin(const ibCompositionLine& line, const std::vector<ibValue>& values) override
	{
		m_rows.push_back(Node(line, values, /*heading*/ true));
		Push(values);
	}
	void OnRow(const ibCompositionLine& line, const std::vector<ibValue>& values) override
	{
		m_rows.push_back(Node(line, values, /*heading*/ false));
	}
	void OnGroupEnd(const ibCompositionLine& /*line*/, const std::vector<ibValue>& /*values*/) override
	{
		Pop();
	}

	// ⭐ A COLUMN HEADING AND A COLUMN'S CELL COME THROUGH THE SAME DOOR, and only the kind tells them
	// apart — the spreadsheet driver branches on exactly this (`Detail` -> a cell, otherwise a
	// heading). Recorded rather than resolved: which of the two a reader wants depends on the
	// question, and deciding here would throw one of them away.
	void OnColumn(const ibCompositionLine& line, const std::vector<ibValue>& values) override
	{
		m_axis.push_back(Node(line, values, /*heading*/ line.m_kind != ibSelectorNodeKind::Detail));
	}

private:
	std::vector<ibComposedNode> m_rows;
	std::vector<ibComposedNode> m_axis;
};

#endif
