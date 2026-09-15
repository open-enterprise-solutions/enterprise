#ifndef __DATAVIEW_EDIT_ON_ACTIVATE_H__
#define __DATAVIEW_EDIT_ON_ACTIVATE_H__

////////////////////////////////////////////////////////////////////////////
// A DOUBLE-CLICK OPENS THE CELL — for every grid of the constructors, one way.
////////////////////////////////////////////////////////////////////////////
//
// The grid answers a double-click with ACTIVATE and nothing else, so an editable cell could only be
// opened with F2 or by clicking a selected row a second time — which is why a grid "needed some number
// of clicks". This is the answer the query constructor arrived at after four attempts; it lived in its
// internal header, and the LINQ constructor needed exactly the same one, so it lives by the control now.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/ctrls/dataview/dataview.h"

inline void ibDataViewEditOnActivate(ibDataViewCtrl* grid)
{
	// ⚠⚠ AND THE HANDLER MUST NOT Skip(). This took four attempts, so the reason is written down.
	//
	// The control raises ITEM_ACTIVATED on a double-click and passes the CLICKED COLUMN with it
	// (datavgen.cpp, `le(wxEVT_DATAVIEW_ITEM_ACTIVATED, this, col, item)`) — so the column was never
	// the problem, and binding a mouse handler here was worse than useless: mouse events go to the
	// control's inner window and never reach this one at all.
	//
	// What killed it is the line right after:  `if (ProcessWindowEvent(le)) return;`
	// Skipping means "not handled", so the control carried on and treated the double-click as a
	// plain click — selecting the row and, in doing so, closing the editor we had just opened. From
	// outside that is exactly "the cell will not open".
	//
	// So: open the cell and OWN the event.
	grid->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [grid](ibDataViewEvent& event) {
		ibDataViewColumn* column = event.GetDataViewColumn();
		if (column == nullptr)
			column = grid->GetCurrentColumn();
		if (column == nullptr || column->GetRenderer() == nullptr)
			return;
		if (column->GetRenderer()->GetMode() != wxDATAVIEW_CELL_EDITABLE) {
			event.Skip();   // an inert cell (a path, a derived name): let whoever else wants it have it
			return;
		}

		// ⚠⚠ AND IT IS OPENED ON THE NEXT TURN OF THE EVENT LOOP, not here. This is the part four
		// earlier attempts missed: the control is still INSIDE its own click handling when it
		// raises this event, and what follows that handling — the click it simulates, the rename
		// timer it starts with `m_currentCol` — tears down an editor opened underneath it. The
		// editor appears and vanishes in the same breath, which from outside is "the cell will not
		// open at all".
		//
		// CallAfter puts the edit after all of that, when the control is idle and nothing is left
		// to undo it.
		const ibDataViewItem item = event.GetItem();
		grid->Select(item);
		grid->CallAfter([grid, item, column] { grid->EditItem(item, column); });
	});
}

#endif
