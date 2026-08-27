#ifndef __QUERY_GRID_MODEL_H__
#define __QUERY_GRID_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// ONE GRID MODEL for every plain list in the constructor.
////////////////////////////////////////////////////////////////////////////
//
// The links, the conditions and the unions each earned a model of their own: they hold a shape (a
// join has two sides and two "all" boxes; a union has one column per branch) that a general model
// would only obscure. Everything else in this window is the same thing N times — a list of rows,
// a few columns, cells read from the AST and some of them written back.
//
// Those were wxListBox and wxListCtrl, and that is what made the window read as several dialogs
// stitched together: three panes with grid lines and in-place editing, six without. The look is the
// smaller half of it. The bigger half is that a listbox cannot be edited in place, so every one of
// those panes needed a dialog to change one word — the alias, the direction, the totals kind.
//
// So: one model, told how to READ a cell and how to WRITE one. The AST stays the single copy of the
// truth (the callbacks reach into it directly), the dialog keeps its per-pane knowledge, and every
// list in the window is an ibDataViewCtrl with the same grid lines, the same row height and the
// same editing behaviour.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/ctrls/dataview/dataview.h"
#include "backend/tabularModelView.h"   // ibDataViewObject — what a TREE row is (the totals tree below)

#include <functional>
#include <memory>
#include <vector>

// Column 0 is reserved by the ibDataViewCtrl fork — the first column a pane can use is 1.
enum ibQueryGridColumn {
	kGridCol1 = 1,
	kGridCol2,
	kGridCol3,
};

class ibQueryGridModel : public ibDataViewVirtualListModel
{
public:
	// What a cell SAYS. Asked live, so a pane never carries a stale copy of the AST.
	using Reader = std::function<wxString(unsigned int row, unsigned int col)>;
	// What a cell DOES when it is typed into. Returning false refuses the edit (the old text stays)
	// — which is the honest answer for a name already taken or an expression the engine rejects.
	using Writer = std::function<bool(unsigned int row, unsigned int col, const wxString& text)>;

	ibQueryGridModel() = default;

	void SetReader(Reader reader) { m_reader = std::move(reader); }
	void SetWriter(Writer writer) { m_writer = std::move(writer); }
	// Run after an accepted write — the dialog refills, so the text pane and every other tab follow.
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }

	// How many rows there are now. The pane's Fill() calls this and nothing else.
	void SetRowCount(unsigned int count) { Reset(count); }

	// A COLUMN THAT CARRIES A PICTURE. The field grids show paths into the data, and a row without
	// the field picture reads as a different kind of thing from the same field in the tree beside
	// it — which is the whole of "the designer catches the eye". The icon is the model's because the
	// renderer asks the model for the cell's VALUE, picture included (ibDataViewIconText).
	void SetIconColumn(unsigned int column, const wxIcon& icon)
	{
		m_iconColumn = column;
		m_icon = icon;
	}

	// A PICTURE PER ROW, when the rows are not all the same kind of thing. The field grids list
	// dimensions, resources, plain attributes and free expressions side by side; one picture for
	// the whole column drew them as if they were interchangeable, while the tree three inches to
	// the left told them apart. Asked per row, and answered by whoever knows the row — the same
	// column that answered in the tree (ibBackendSourceColumn::GetColumnIcon).
	// Unset, or answering an invalid icon, falls back to the column-wide one.
	using IconReader = std::function<wxIcon(unsigned int row)>;
	void SetIconReader(IconReader reader) { m_iconReader = std::move(reader); }

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override
	{
		if (!m_reader)
			return;

		const wxString text = m_reader(row, col);
		if (col == m_iconColumn) {
			const wxIcon icon = m_iconReader ? m_iconReader(row) : wxNullIcon;
			if (icon.IsOk()) {
				variant << ibDataViewIconText(text, icon);
				return;
			}
			if (m_icon.IsOk()) {
				variant << ibDataViewIconText(text, m_icon);
				return;
			}
		}
		variant = text;
	}

	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override
	{
		if (!m_writer || !m_writer(row, col, variant.GetString()))
			return false;
		if (m_onChanged)
			m_onChanged();
		return true;
	}

private:
	Reader                m_reader;
	Writer                m_writer;
	std::function<void()> m_onChanged;
	unsigned int          m_iconColumn = 0;   // 0 is the fork's reserved column — never a real one
	wxIcon                m_icon;             // the column-wide fallback
	IconReader            m_iconReader;       // per row, when the rows differ in kind
};

////////////////////////////////////////////////////////////////////////////
// WHERE A ROW OF THE TOTALS TAB SITS — which NODE it belongs to.
////////////////////////////////////////////////////////////////////////////
//
// The levels have always hung on a node; there was simply only one of it and nobody had to name it.
// `SPLIT` adds VISIBLE nodes beside that hidden one, and groupings hang on them the same way (Max,
// 2026-08-27). So the tab still shows ONE FLAT LIST — a virtual list model addressed by row — and a
// row is either a node's header or a level of some node.
//
// ⚠ THE MAP IS REBUILT ON EVERY FILL and never stored in the AST: the AST holds the hidden node's
// levels and the visible nodes, and this is only how they are laid out on screen.
struct ibTotalsRow {
	int  m_node  = wxNOT_FOUND;   // wxNOT_FOUND = the hidden node; otherwise an index into m_totalsSplits
	int  m_level = wxNOT_FOUND;   // wxNOT_FOUND = this row IS the node's header (the SPLIT line)

	bool IsNodeHeader() const { return m_level == wxNOT_FOUND; }
	bool IsLevel()      const { return m_level != wxNOT_FOUND; }
	bool OnHiddenNode() const { return m_node == wxNOT_FOUND; }
	bool operator==(const ibTotalsRow& o) const { return m_node == o.m_node && m_level == o.m_level; }
};

// ONE ROW OF THE TOTALS TREE. Pooled by the model and never dropped while it lives: the view may
// still be painting a row when the ladder changes underneath it, and a row freed mid-paint is a
// crash whose stack blames the paint. (Same rule, same reason, as the composer's structure tree.)
class ibQueryTotalsNode : public ibDataViewObject {
public:
	ibQueryTotalsNode(const ibTotalsRow& at, ibQueryTotalsNode* parent) : m_at(at), m_parent(parent) {}

	const ibTotalsRow& At() const { return m_at; }

	void SetHasChild(bool hasChild) { m_hasChild = hasChild; }
	virtual bool IsContainer() const override { return m_hasChild; }

	virtual ibDataViewItem GetParentItem() const override {
		return m_parent != nullptr ? ibDataViewItem(m_parent) : ibDataViewItem();
	}

private:
	ibTotalsRow        m_at;
	ibQueryTotalsNode* m_parent = nullptr;   // owned by the model, outlives this row
	bool               m_hasChild = false;
};

////////////////////////////////////////////////////////////////////////////
// THE TOTALS AS A TREE — a separator is a NODE, its groupings are its CHILDREN.
////////////////////////////////////////////////////////////////////////////
//
// A node has always been there: the levels of `m_totalsBy` hang on the hidden one every report has,
// and nobody had to name it because there was only one. `SPLIT` adds VISIBLE nodes beside it, and
// what hangs on them has to LOOK like it hangs on them (Max, 2026-08-27: "a splitter is a NODE, and
// its children are the groupings") — an indent would only hint at that; an expander says it.
//
// ⚠ THE AST STAYS FLAT-ISH AND STAYS THE ONLY COPY: the hidden node's levels on the select, the
// visible nodes beside them. This is a READING of that, rebuilt whenever the tab refills.
class ibQueryTotalsTreeModel : public ibDataViewModel
{
public:
	// Column 0 belongs to the fork. The tree hangs off column 1, so the FIELD lives there: a
	// separator says SPLIT where a level says its field, and the two read as one list.
	enum { kColField = 1, kColKind, kColAlias };

	using Reader   = std::function<wxString(const ibTotalsRow& at, unsigned int col)>;
	using Writer   = std::function<bool(const ibTotalsRow& at, unsigned int col, const wxString& text)>;
	// How many levels hang on a node (wxNOT_FOUND = the hidden one), and how many nodes there are.
	using LevelsOf = std::function<int(int node)>;
	using NodeCount = std::function<int()>;

	void SetReader(Reader reader)        { m_reader = std::move(reader); }
	void SetWriter(Writer writer)        { m_writer = std::move(writer); }
	void SetLevelsOf(LevelsOf levels)    { m_levelsOf = std::move(levels); }
	void SetNodeCount(NodeCount count)   { m_nodeCount = std::move(count); }
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }

	// A PICTURE PER ROW — the same question the flat grids answer, asked the same way. The field
	// grids draw the path's own icon so a row reads as the kind of thing it is; a separator answers
	// with none, which is right: it is not a field.
	using IconReader = std::function<wxIcon(const ibTotalsRow& at)>;
	void SetIconColumn(unsigned int column, const wxIcon& icon) { m_iconColumn = column; m_icon = icon; }
	void SetIconReader(IconReader reader) { m_iconReader = std::move(reader); }

	// RE-READ THE LADDER. Rows are pooled by coordinate, so a selection survives a rebuild.
	void Rebuild() { Cleared(); }

	ibDataViewItem ItemFor(const ibTotalsRow& at, ibQueryTotalsNode* parent) const
	{
		for (const std::unique_ptr<ibQueryTotalsNode>& node : m_pool)
			if (node->At() == at)
				return ibDataViewItem(node.get());
		m_pool.push_back(std::make_unique<ibQueryTotalsNode>(at, parent));
		return ibDataViewItem(m_pool.back().get());
	}

	static ibTotalsRow AtOf(const ibDataViewItem& item)
	{
		const ibQueryTotalsNode* node = static_cast<ibQueryTotalsNode*>(item.GetID());
		return node != nullptr ? node->At() : ibTotalsRow();
	}

	// ⭐⭐ THE SHAPE. At the top: the hidden node's levels, then one row per separator. Under a
	// separator: its own levels. A level contains nothing — the ladder's nesting is its ORDER, and
	// only a separator is a container.
	// ⚠ THROUGH THE FETCH CONTRACT, not `GetChildren`: this fork routes every "give me this parent's
	// children" through GetFirstFetch, so a tree that answered the old verb would simply never be
	// asked. (The paged variants are for sources that stream; a ladder is a handful of rows.)
	unsigned int GetFirstFetch(const ibDataViewItem& parent, const ibDataViewItem& /*anchor*/,
		int /*count*/, ibDataViewItemArray& array) const override
	{
		const int nodes = m_nodeCount ? m_nodeCount() : 0;
		unsigned int added = 0;

		if (!parent.IsOk()) {
			const int hidden = m_levelsOf ? m_levelsOf(wxNOT_FOUND) : 0;
			for (int level = 0; level < hidden; ++level)
				array.Add(ItemFor(ibTotalsRow{ wxNOT_FOUND, level }, nullptr)), ++added;
			for (int node = 0; node < nodes; ++node) {
				const ibDataViewItem item = ItemFor(ibTotalsRow{ node, wxNOT_FOUND }, nullptr);
				// A SEPARATOR IS ALWAYS A CONTAINER, even with nothing on it yet: it was added to be
				// filled, and a node that stops being expandable the moment it empties would take its
				// own expander away while somebody is still using it.
				static_cast<ibQueryTotalsNode*>(item.GetID())->SetHasChild(true);
				array.Add(item), ++added;
			}
			return added;
		}

		const ibTotalsRow at = AtOf(parent);
		if (!at.IsNodeHeader())
			return 0;

		ibQueryTotalsNode* head = static_cast<ibQueryTotalsNode*>(parent.GetID());
		const int mine = m_levelsOf ? m_levelsOf(at.m_node) : 0;
		for (int level = 0; level < mine; ++level)
			array.Add(ItemFor(ibTotalsRow{ at.m_node, level }, head)), ++added;
		return added;
	}

	ibDataViewItem GetParent(const ibDataViewItem& item) const override
	{
		const ibQueryTotalsNode* node = static_cast<ibQueryTotalsNode*>(item.GetID());
		return node != nullptr ? node->GetParentItem() : ibDataViewItem();
	}

	bool IsContainer(const ibDataViewItem& item) const override
	{
		if (!item.IsOk())
			return true;                       // the invisible root
		return AtOf(item).IsNodeHeader();
	}

	// ⭐⭐ A CONTAINER HERE STILL HAS CELLS. The fork draws a container as a bare header by default —
	// it asks for no column values at all — which is right for a tree of names and wrong for this
	// one: a separator says `SPLIT` in the field cell and carries its NAME in the alias cell, and
	// without this it came up as an EMPTY ROW that expanded (seen live, 2026-08-27).
	bool HasContainerColumns(const ibDataViewItem& /*item*/) const override { return true; }

	void GetValue(wxVariant& variant, const ibDataViewItem& item, unsigned int col) const override
	{
		const ibQueryTotalsNode* node = static_cast<ibQueryTotalsNode*>(item.GetID());
		if (node == nullptr || !m_reader)
			return;

		const wxString text = m_reader(node->At(), col);
		if (col == m_iconColumn) {
			const wxIcon icon = m_iconReader ? m_iconReader(node->At()) : wxNullIcon;
			if (icon.IsOk())      { variant << ibDataViewIconText(text, icon); return; }
			if (m_icon.IsOk())    { variant << ibDataViewIconText(text, m_icon); return; }
		}
		variant = text;
	}

	// ⭐⭐ WHAT A SEPARATOR HAS NO ANSWER FOR IS NOT OFFERED. A node is not keyed by a value, so it
	// unfolds nothing — there is no Elements / Hierarchy to choose between, and a cell that opens for
	// editing and then refuses every value says "you may set this" and then goes back on it. Its
	// FIELD cell is what it is (`SPLIT`), and only its NAME is editable.
	//
	// Refusing in the writer is not enough on its own: the refusal comes after a person has typed.
	// Disabled, the cell never opens, which is the same answer given a moment earlier.
	bool IsEnabled(const ibDataViewItem& item, unsigned int col) const override
	{
		const ibQueryTotalsNode* node = static_cast<ibQueryTotalsNode*>(item.GetID());
		if (node == nullptr)
			return false;
		if (node->At().IsNodeHeader())
			return col == kColAlias;
		return true;
	}

	bool SetValue(const wxVariant& variant, const ibDataViewItem& item, unsigned int col) override
	{
		const ibQueryTotalsNode* node = static_cast<ibQueryTotalsNode*>(item.GetID());
		if (node == nullptr || !m_writer || !m_writer(node->At(), col, variant.GetString()))
			return false;
		if (m_onChanged)
			m_onChanged();
		return true;
	}

private:
	Reader    m_reader;
	Writer    m_writer;
	LevelsOf  m_levelsOf;
	NodeCount m_nodeCount;
	std::function<void()> m_onChanged;
	unsigned int m_iconColumn = 0;   // 0 is the fork's reserved column — never a real one
	wxIcon       m_icon;             // the column-wide fallback
	IconReader   m_iconReader;       // per row, because the rows differ in kind
	mutable std::vector<std::unique_ptr<ibQueryTotalsNode>> m_pool;
};

#endif // __QUERY_GRID_MODEL_H__
