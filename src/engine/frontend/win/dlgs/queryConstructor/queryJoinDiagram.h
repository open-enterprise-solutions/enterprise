#ifndef __QUERY_JOIN_DIAGRAM_H__
#define __QUERY_JOIN_DIAGRAM_H__

////////////////////////////////////////////////////////////////////////////
// The join DIAGRAM — a second view over `m_joins`, not a second copy of them.
////////////////////////////////////////////////////////////////////////////
//
// The tables as boxes, the joins as lines between the fields they compare. It replaces nothing:
// the list beside it edits the same `ibQueryAstJoin` vector, and both are refilled from the AST
// after every change, so neither can drift from the other or from the text.
//
// WHY A PICTURE EARNS ITS PLACE HERE, when a list of joins is perfectly readable for two tables:
// a join is a relation BETWEEN two things, and a list shows one of them per row — the other is
// implied by position. With four tables and five joins, working out which table a row attaches to
// costs more than reading a line drawn between two boxes. The line IS the `on` predicate.
//
// The gesture is the one every diagram editor uses and the reason the diagram is worth having at
// all: DRAG A FIELD ONTO A FIELD and the two are compared. The condition it makes is composed as
// TEXT (`Orders.Ref = Products.Ref`) and read back by the engine's parser — the same door the
// condition rows go through, so the diagram has no private idea of what a join is either.
//
// See docs/query-constructor.md §5 step 4.
//
////////////////////////////////////////////////////////////////////////////

#include <wx/panel.h>
#include <wx/dc.h>

#include "backend/query/queryAst.h"

#include <functional>
#include <vector>

class ibQueryJoinDiagram : public wxPanel
{
public:
	explicit ibQueryJoinDiagram(wxWindow* parent);

	// One box: a source of the query, with the fields it offers. `sourceIndex` is the position the
	// constructor knows it by — 0 = FROM, 1..n = joins[i-1].
	struct Table
	{
		int                   m_sourceIndex = -1;
		wxString              m_title;
		std::vector<wxString> m_fields;      // technical names — what a condition writes
	};

	// Rebuild from the AST. `select` is BORROWED and edited in place; the diagram never owns it and
	// never keeps a copy, which is what makes "both views show the same thing" true by construction
	// rather than by a synchronisation step somebody has to remember.
	void SetContent(ibQuerySelect* select, std::vector<Table> tables);

	void SetReadOnly(bool readOnly) { m_readOnly = readOnly; }

	// Raised after the diagram has CHANGED the AST — the constructor refills everything and
	// re-renders the text. Raised with the join index for a double-click on a line, so the same
	// link editor the list uses opens.
	void SetOnChanged(std::function<void()> onChanged)        { m_onChanged = std::move(onChanged); }
	void SetOnEditJoin(std::function<void(size_t)> onEditJoin) { m_onEditJoin = std::move(onEditJoin); }

private:
	void OnPaint(wxPaintEvent&);
	void OnLeftDown(wxMouseEvent&);
	void OnLeftUp(wxMouseEvent&);
	void OnMotion(wxMouseEvent&);
	void OnDoubleClick(wxMouseEvent&);

	// Where each box and each of its field rows sits. Laid out left to right, wrapping — a layout
	// nobody has to arrange by hand, because the diagram is READ far more often than it is arranged.
	void LayoutBoxes();
	wxRect BoxRect(size_t table) const;
	wxRect FieldRect(size_t table, size_t field) const;

	// What is under a point: the table and the field row, or -1 / npos when nothing.
	bool HitField(const wxPoint& at, size_t& outTable, size_t& outField) const;
	// The join whose line passes near `at`, or npos.
	size_t HitJoin(const wxPoint& at) const;

	// The point a line leaves / enters a box at, for a join between two tables.
	wxPoint AnchorFor(size_t table, size_t field) const;

	// Make (or extend) the join that compares `from` with `to`. The condition is composed as TEXT
	// and parsed — the diagram states a comparison, the engine decides it is one.
	void ConnectFields(size_t fromTable, size_t fromField, size_t toTable, size_t toField);

	ibQuerySelect*     m_select = nullptr;   // borrowed
	std::vector<Table> m_tables;
	std::vector<wxRect> m_boxes;             // parallel to m_tables
	bool               m_readOnly = false;

	// The drag in flight: the field it started on, and where the pointer is now.
	bool     m_dragging   = false;
	size_t   m_dragTable  = 0;
	size_t   m_dragField  = 0;
	wxPoint  m_dragPoint;

	std::function<void()>       m_onChanged;
	std::function<void(size_t)> m_onEditJoin;
};

#endif // __QUERY_JOIN_DIAGRAM_H__
