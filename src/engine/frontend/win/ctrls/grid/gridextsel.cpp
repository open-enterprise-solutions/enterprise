///////////////////////////////////////////////////////////////////////////
// Name:        src/generic/gridsel.cpp
// Purpose:     ibGridSelection
// Author:      Stefan Neis
// Modified by:
// Created:     20/02/1999
// Copyright:   (c) Stefan Neis (Stefan.Neis@t-online.de)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#if wxUSE_GRID

#include "gridextsel.h"

#include <wx/dynarray.h>

namespace
{

	// The helper function to compare wxIntSortedArray elements.
	int CompareInts(int n1, int n2)
	{
		return n1 - n2;
	}

}

WX_DEFINE_SORTED_ARRAY_CMP_INT(int, CompareInts, wxIntSortedArray);


ibGridSelection::ibGridSelection(ibGrid* grid,
	ibGrid::ibGridSelectionModes sel)
{
	m_grid = grid;
	m_selectionMode = sel;
}

bool ibGridSelection::IsSelection()
{
	return !m_selection.empty();
}

void ibGridSelection::EndSelecting()
{
	// It's possible that nothing was selected finally, e.g. the mouse could
	// have been dragged around only to return to the starting cell, just don't
	// do anything in this case.
	if (!IsSelection())
		return;

	// Send RANGE_SELECTED event for the last modified block.
	const ibGridBlockCoords& block = m_selection.back();
	ibGridRangeSelectEvent gridEvt(m_grid->GetId(),
		wxEVT_GRID_RANGE_SELECTED,
		m_grid,
		block.GetTopLeft(),
		block.GetBottomRight(),
		true);

	m_grid->GetEventHandler()->ProcessEvent(gridEvt);
}

void ibGridSelection::CancelSelecting()
{
	// It's possible that nothing was selected finally, e.g. the mouse could
	// have been dragged around only to return to the starting cell, just don't
	// do anything in this case.
	if (!IsSelection())
		return;

	const ibGridBlockCoords& block = m_selection.back();
	m_grid->RefreshBlock(block.GetTopLeft(), block.GetBottomRight());
	m_selection.pop_back();
}


bool ibGridSelection::IsInSelection(int row, int col) const
{
	// Check whether the given cell is contained in one of the selected blocks.
	//
	// Note that this algorithm is O(N) in number of selected blocks, not in
	// number of cells in the grid, so it should be reasonably efficient even
	// for very large grids, as the user shouldn't be able to select too many
	// blocks. If we still run into problems with this, we should find a more
	// efficient way of storing the selection, e.g. using k-d trees.
	const size_t count = m_selection.size();
	for (size_t n = 0; n < count; n++)
	{
		if (m_selection[n].Contains(ibGridCellCoords(row, col)))
			return true;
	}

	return false;
}

// Change the selection mode
void ibGridSelection::SetSelectionMode(ibGrid::ibGridSelectionModes selmode)
{
	// if selection mode is unchanged return immediately
	if (selmode == m_selectionMode)
		return;

	if (selmode == ibGrid::ibGridSelectNone)
	{
		ClearSelection();
		m_selectionMode = selmode;
		return;
	}

	if (m_selectionMode != ibGrid::ibGridSelectCells)
	{
		// if changing form row to column selection
		// or vice versa, clear the selection.
		if (selmode != ibGrid::ibGridSelectCells)
			ClearSelection();

		m_selectionMode = selmode;
	}
	else
	{
		// Preserve only fully selected rows/columns when switching from cell
		// selection mode and discard the selected blocks that are invalid in
		// the new selection mode.
		const int lastCol = m_grid->GetNumberCols() - 1;
		const int lastRow = m_grid->GetNumberRows() - 1;
		for (size_t n = m_selection.size(); n > 0; )
		{
			n--;
			const ibGridBlockCoords& block = m_selection[n];
			const int topRow = block.GetTopRow();
			const int leftCol = block.GetLeftCol();
			const int bottomRow = block.GetBottomRow();
			const int rightCol = block.GetRightCol();

			bool valid = false;
			switch (selmode)
			{
			case ibGrid::ibGridSelectCells:
			case ibGrid::ibGridSelectNone:
				wxFAIL_MSG("unreachable");
				break;

			case ibGrid::ibGridSelectRows:
				valid = leftCol == 0 && rightCol == lastCol;
				break;

			case ibGrid::ibGridSelectColumns:
				valid = topRow == 0 && bottomRow == lastRow;
				break;

			case ibGrid::ibGridSelectRowsOrColumns:
				valid = (leftCol == 0 && rightCol == lastCol) ||
					(topRow == 0 && bottomRow == lastRow);
				break;
			}

			if (!valid)
			{
				if (!m_grid->GetBatchCount())
				{
					m_grid->RefreshBlock(block.GetTopLeft(), block.GetBottomRight());
				}
				m_selection.erase(m_selection.begin() + n);
			}
		}

		m_selectionMode = selmode;
	}
}

void ibGridSelection::SelectRow(int row, const wxKeyboardState& kbd)
{
	if (m_selectionMode == ibGrid::ibGridSelectColumns ||
		m_selectionMode == ibGrid::ibGridSelectNone)
		return;

	Select(ibGridBlockCoords(row, 0, row, m_grid->GetNumberCols() - 1),
		kbd, wxEVT_GRID_RANGE_SELECTED);
}

void ibGridSelection::SelectCol(int col, const wxKeyboardState& kbd)
{
	if (m_selectionMode == ibGrid::ibGridSelectRows ||
		m_selectionMode == ibGrid::ibGridSelectNone)
		return;

	Select(ibGridBlockCoords(0, col, m_grid->GetNumberRows() - 1, col),
		kbd, wxEVT_GRID_RANGE_SELECTED);
}

void ibGridSelection::SelectBlock(int topRow, int leftCol,
	int bottomRow, int rightCol,
	const wxKeyboardState& kbd,
	wxEventType eventType)
{
	// Fix the coordinates of the block if needed.
	int allowed = -1;
	switch (m_selectionMode)
	{
	case ibGrid::ibGridSelectCells:
		// In this mode arbitrary blocks can be selected.
		allowed = 1;
		break;

	case ibGrid::ibGridSelectRows:
		leftCol = 0;
		rightCol = m_grid->GetNumberCols() - 1;
		allowed = 1;
		break;

	case ibGrid::ibGridSelectColumns:
		topRow = 0;
		bottomRow = m_grid->GetNumberRows() - 1;
		allowed = 1;
		break;

	case ibGrid::ibGridSelectRowsOrColumns:
		// Arbitrary block selection doesn't make sense for this mode, as
		// we could only select the entire grid, which wouldn't be useful,
		// but we do allow selecting blocks that are already composed of
		// only rows or only columns.
		if (topRow == 0 && bottomRow == m_grid->GetNumberRows() - 1)
			allowed = 1;
		else if (leftCol == 0 && rightCol == m_grid->GetNumberCols() - 1)
			allowed = 1;
		else
			allowed = 0;
		break;

	case ibGrid::ibGridSelectNone:
		allowed = 0;
		break;
	}

	wxASSERT_MSG(allowed != -1, "unknown selection mode?");
	if (!allowed)
		return;

	Select(ibGridBlockCoords(topRow, leftCol, bottomRow, rightCol).Canonicalize(),
		kbd, eventType);
}

void
ibGridSelection::SelectAll()
{
	// There is no need to refresh anything, as Select() will do it anyhow, and
	// no need to generate any events, so do not call ClearSelection() here.
	m_selection.clear();

	const int numRows = m_grid->GetNumberRows();
	const int numCols = m_grid->GetNumberCols();

	if (numRows && numCols)
	{
		Select(ibGridBlockCoords(0, 0, numRows - 1, numCols - 1),
			wxKeyboardState(), wxEVT_GRID_RANGE_SELECTED);
	}
}

void
ibGridSelection::DeselectBlock(const ibGridBlockCoords& block,
	const wxKeyboardState& kbd,
	wxEventType eventType)
{
	// In ibGridSelectNone mode, all blocks should already be deselected.
	if (m_selectionMode == ibGrid::ibGridSelectNone)
		return;

	const ibGridBlockCoords canonicalizedBlock = block.Canonicalize();

	size_t count, n;

	// If the selected block intersects with the deselection block, split it
	// in up to 4 new parts, that don't contain the block to be selected, like
	// this (for rows):
	// |---------------------------|
	// |                           |
	// |           part 1          |
	// |                           |
	// |---------------------------|
	// | part 3 |    x    | part 4 |
	// |---------------------------|
	// |                           |
	// |           part 2          |
	// |                           |
	// |---------------------------|
	// And for columns:
	// |---------------------------|
	// |        |         |        |
	// |        | part 3  |        |
	// |        |         |        |
	// |        |---------|        |
	// | part 1 |    x    | part 2 |
	// |        |---------|        |
	// |        |         |        |
	// |        | part 4  |        |
	// |        |         |        |
	// |---------------------------|
	//   (The x marks the newly deselected block).
	// Note: in row/column selection mode, we only need part1 and part2

	// Blocks to refresh.
	wxVectorGridBlockCoords refreshBlocks;
	// Add the deselected block.
	refreshBlocks.push_back(canonicalizedBlock);

	count = m_selection.size();
	for (n = 0; n < count; n++)
	{
		const ibGridBlockCoords& selBlock = m_selection[n];

		// Whether blocks intersect.
		if (!m_selection[n].Intersects(canonicalizedBlock))
			continue;

		int splitOrientation = -1;
		switch (m_selectionMode)
		{
		case ibGrid::ibGridSelectRows:
			splitOrientation = wxHORIZONTAL;
			break;

		case ibGrid::ibGridSelectColumns:
			splitOrientation = wxVERTICAL;
			break;

		case ibGrid::ibGridSelectCells:
		case ibGrid::ibGridSelectRowsOrColumns:
			if (selBlock.GetLeftCol() == 0 &&
				selBlock.GetRightCol() == m_grid->GetNumberCols() - 1)
				splitOrientation = wxHORIZONTAL;
			else
				splitOrientation = wxVERTICAL;
			break;

		case ibGrid::ibGridSelectNone:
			wxFAIL_MSG("unreachable");
			break;
		}

		wxASSERT_MSG(splitOrientation != -1, "unknown selection mode");

		const ibGridBlockDiffResult result =
			selBlock.Difference(canonicalizedBlock, splitOrientation);

		// remove the block (note that selBlock, being a reference, is
		// invalidated here and can't be used any more below)
		m_selection.erase(m_selection.begin() + n);
		n--;
		count--;

		for (int i = 0; i < 2; ++i)
		{
			const ibGridBlockCoords& part = result.m_parts[i];
			if (part != ibGridNoBlockCoords)
				SelectBlockNoEvent(part);
		}

		for (int i = 2; i < 4; ++i)
		{
			const ibGridBlockCoords& part = result.m_parts[i];
			if (part != ibGridNoBlockCoords)
			{
				// Add part[2] and part[3] only in the cells selection mode.
				if (m_selectionMode == ibGrid::ibGridSelectCells)
					SelectBlockNoEvent(part);
				else
					MergeOrAddBlock(refreshBlocks, part);
			}
		}
	}

	// Refresh the screen and send events.
	count = refreshBlocks.size();
	for (n = 0; n < count; n++)
	{
		const ibGridBlockCoords& refBlock = refreshBlocks[n];

		if (!m_grid->GetBatchCount())
		{
			m_grid->RefreshBlock(refBlock.GetTopRow(), refBlock.GetLeftCol(),
				refBlock.GetBottomRow(), refBlock.GetRightCol());
		}

		if (eventType != wxEVT_NULL)
		{
			ibGridRangeSelectEvent gridEvt(m_grid->GetId(),
				eventType,
				m_grid,
				refBlock.GetTopLeft(),
				refBlock.GetBottomRight(),
				false,
				kbd);
			m_grid->GetEventHandler()->ProcessEvent(gridEvt);
		}
	}
}

void ibGridSelection::ClearSelection()
{
	size_t n;
	ibGridCellCoords coords1, coords2;

	// deselect all blocks and update the screen
	while ((n = m_selection.size()) > 0)
	{
		n--;
		const ibGridBlockCoords& block = m_selection[n];
		coords1 = block.GetTopLeft();
		coords2 = block.GetBottomRight();
		m_selection.erase(m_selection.begin() + n);
		if (!m_grid->GetBatchCount())
		{
			m_grid->RefreshBlock(coords1, coords2);

#ifdef __WXOSX__
			m_grid->UpdateGridWindows();
#endif
		}
	}

	// One deselection event, indicating deselection of _all_ cells.
	// (No finer grained events for each of the smaller regions
	//  deselected above!)
	ibGridRangeSelectEvent gridEvt(m_grid->GetId(),
		wxEVT_GRID_RANGE_SELECTED,
		m_grid,
		ibGridCellCoords(0, 0),
		ibGridCellCoords(
			m_grid->GetNumberRows() - 1,
			m_grid->GetNumberCols() - 1),
		false);

	m_grid->GetEventHandler()->ProcessEvent(gridEvt);
}


void ibGridSelection::UpdateRows(size_t pos, int numRows)
{
	size_t count = m_selection.size();
	size_t n;

	for (n = 0; n < count; n++)
	{
		ibGridBlockCoords& block = m_selection[n];
		wxCoord row1 = block.GetTopRow();
		wxCoord row2 = block.GetBottomRow();

		if ((size_t)row2 >= pos)
		{
			if (numRows > 0)
			{
				// If rows inserted, increase row counter where necessary
				block.SetBottomRow(row2 + numRows);
				if ((size_t)row1 >= pos)
					block.SetTopRow(row1 + numRows);
			}
			else if (numRows < 0)
			{
				// If rows deleted ...
				if ((size_t)row2 >= pos - numRows)
				{
					// ...either decrement row counter (if row still exists)...
					block.SetBottomRow(row2 + numRows);
					if ((size_t)row1 >= pos)
						block.SetTopRow(wxMax(row1 + numRows, (int)pos));

				}
				else
				{
					if ((size_t)row1 >= pos)
					{
						// ...or remove the attribute
						m_selection.erase(m_selection.begin() + n);
						n--;
						count--;
					}
					else
						block.SetBottomRow(pos - 1);
				}
			}
		}
	}
}


void ibGridSelection::UpdateCols(size_t pos, int numCols)
{
	size_t count = m_selection.size();
	size_t n;

	for (n = 0; n < count; n++)
	{
		ibGridBlockCoords& block = m_selection[n];
		wxCoord col1 = block.GetLeftCol();
		wxCoord col2 = block.GetRightCol();

		if ((size_t)col2 >= pos)
		{
			if (numCols > 0)
			{
				// If rows inserted, increase row counter where necessary
				block.SetRightCol(col2 + numCols);
				if ((size_t)col1 >= pos)
					block.SetLeftCol(col1 + numCols);
			}
			else if (numCols < 0)
			{
				// If cols deleted ...
				if ((size_t)col2 >= pos - numCols)
				{
					// ...either decrement col counter (if col still exists)...
					block.SetRightCol(col2 + numCols);
					if ((size_t)col1 >= pos)
						block.SetLeftCol(wxMax(col1 + numCols, (int)pos));

				}
				else
				{
					if ((size_t)col1 >= pos)
					{
						// ...or remove the attribute
						m_selection.erase(m_selection.begin() + n);
						n--;
						count--;
					}
					else
						block.SetRightCol(pos - 1);
				}
			}
		}
	}
}

bool ibGridSelection::ExtendCurrentBlock(const ibGridCellCoords& blockStart,
	const ibGridCellCoords& blockEnd,
	const wxKeyboardState& kbd,
	wxEventType eventType)
{
	wxASSERT(blockStart.GetRow() != -1 && blockStart.GetCol() != -1 &&
		blockEnd.GetRow() != -1 && blockEnd.GetCol() != -1);

	if (m_selectionMode == ibGrid::ibGridSelectNone)
		return false;

	// If selection doesn't contain the current cell (which also covers the
	// special case of nothing being selected yet), we have to create a new
	// block containing it because it doesn't make sense to extend any existing
	// block to non-selected current cell.

	if (!IsInSelection(m_grid->GetGridCursorCoords()))
	{
		SelectBlock(blockStart, blockEnd, kbd, eventType);
		return true;
	}

	const ibGridBlockCoords& block = *m_selection.rbegin();
	ibGridBlockCoords newBlock = block;

	// Determine if we should try to extend the current block rows and/or
	// columns at all.
	bool canChangeRow = false,
		canChangeCol = false;

	switch (m_selectionMode)
	{
	case ibGrid::ibGridSelectCells:
		// Nothing prevents us from doing it in this case.
		canChangeRow =
			canChangeCol = true;
		break;

	case ibGrid::ibGridSelectColumns:
		// Rows are always fixed, so prevent us from ever selecting only
		// part of a column in this case by leaving canChangeRow false.
		canChangeCol = true;
		break;

	case ibGrid::ibGridSelectRows:
		// Same as above but mirrored.
		canChangeRow = true;
		break;

	case ibGrid::ibGridSelectRowsOrColumns:
		// In this case we may only change component which is not fixed.
		if (block.GetTopRow() != 0 ||
			block.GetBottomRow() != m_grid->GetNumberRows() - 1)
		{
			// This is a row block, so we can extend it in row direction.
			canChangeRow = true;
		}
		else if (block.GetLeftCol() != 0 ||
			block.GetRightCol() != m_grid->GetNumberCols() - 1)
		{
			canChangeCol = true;
		}
		else // The entire grid is selected.
		{
			// In this case we can shrink it in either direction.
			canChangeRow =
				canChangeCol = true;
		}
		break;

	case ibGrid::ibGridSelectNone:
		wxFAIL_MSG("unreachable");
		break;
	}

	if (canChangeRow)
	{
		// If the new block starts at the same top row as the current one, the
		// end block coordinates must correspond to the new bottom row -- and
		// vice versa, if the new block starts at the bottom, its other end
		// must correspond to the top.
		if (blockStart.GetRow() == block.GetTopRow())
		{
			newBlock.SetBottomRow(blockEnd.GetRow());
		}
		else if (blockStart.GetRow() == block.GetBottomRow())
		{
			newBlock.SetTopRow(blockEnd.GetRow());
		}
		else // current and new block don't have common row boundary
		{
			// This can happen when mixing entire column and cell selection, e.g.
			// by Shift-clicking on the column header. In this case, the right
			// thing to do is to just expand the current block to the new one
			// boundaries, extending the selection to the entire column height when
			// a column is selected. However notice that we should not shrink the
			// current block here, in order to allow Shift-Left/Right (which don't
			// know anything about the column selection and so just use single row
			// blocks) to keep the full column selection.
			int top = blockStart.GetRow(),
				bottom = blockEnd.GetRow();
			if (top > bottom)
				wxSwap(top, bottom);

			if (top < newBlock.GetTopRow())
				newBlock.SetTopRow(top);
			if (bottom > newBlock.GetBottomRow())
				newBlock.SetBottomRow(bottom);
		}
	}

	// Same as above but mirrored for columns.
	if (canChangeCol)
	{
		if (blockStart.GetCol() == block.GetLeftCol())
		{
			newBlock.SetRightCol(blockEnd.GetCol());
		}
		else if (blockStart.GetCol() == block.GetRightCol())
		{
			newBlock.SetLeftCol(blockEnd.GetCol());
		}
		else
		{
			int left = blockStart.GetCol(),
				right = blockEnd.GetCol();
			if (left > right)
				wxSwap(left, right);

			if (left < newBlock.GetLeftCol())
				newBlock.SetLeftCol(left);
			if (right > newBlock.GetRightCol())
				newBlock.SetRightCol(right);
		}
	}

	newBlock = newBlock.Canonicalize();

	if (newBlock == block)
		return false;

	// Update View.
	if (!m_grid->GetBatchCount())
	{
		ibGridBlockDiffResult refreshBlocks = block.SymDifference(newBlock);
		for (int i = 0; i < 4; ++i)
		{
			const ibGridBlockCoords& refreshBlock = refreshBlocks.m_parts[i];

			if (!refreshBlock)
				continue;

			m_grid->RefreshBlock(wxMax(refreshBlock.GetTopRow() - 1, 0), wxMax(refreshBlock.GetLeftCol() - 1, 0),
				wxMin(refreshBlock.GetBottomRow() + 1, m_grid->GetNumberRows() - 1), wxMin(refreshBlock.GetRightCol() + 1, m_grid->GetNumberCols() - 1));
		}
	}

	// Update the current block in place.
	*m_selection.rbegin() = newBlock;

	// Send Event.
	ibGridRangeSelectEvent gridEvt(m_grid->GetId(),
		eventType,
		m_grid,
		newBlock.GetTopLeft(),
		newBlock.GetBottomRight(),
		true,
		kbd);
	m_grid->GetEventHandler()->ProcessEvent(gridEvt);

	return true;
}

ibGridCellCoords ibGridSelection::GetExtensionAnchor() const
{
	ibGridCellCoords coords = m_grid->m_currentCellCoords;

	// If the current cell isn't selected (which also covers the special case
	// of nothing being selected yet), we have to use it as anchor as we need
	// to ensure that it will get selected.
	if (!IsInSelection(coords))
		return coords;

	const ibGridBlockCoords& block = *m_selection.rbegin();
	if (block.GetTopRow() == coords.GetRow())
		coords.SetRow(block.GetBottomRow());
	else if (block.GetBottomRow() == coords.GetRow())
		coords.SetRow(block.GetTopRow());

	if (block.GetLeftCol() == coords.GetCol())
		coords.SetCol(block.GetRightCol());
	else if (block.GetRightCol() == coords.GetCol())
		coords.SetCol(block.GetLeftCol());

	return coords;
}

ibGridCellCoordsArray ibGridSelection::GetCellSelection() const
{
	if (m_selectionMode != ibGrid::ibGridSelectCells)
		return ibGridCellCoordsArray();

	ibGridCellCoordsArray cells;
	const size_t count = m_selection.size();
	cells.reserve(count);
	for (size_t n = 0; n < count; n++)
	{
		const ibGridBlockCoords& block = m_selection[n];
		if (block.GetTopRow() == block.GetBottomRow() &&
			block.GetLeftCol() == block.GetRightCol())
		{
			cells.push_back(block.GetTopLeft());
		}
	}
	return cells;
}

ibGridCellCoordsArray ibGridSelection::GetBlockSelectionTopLeft() const
{
	ibGridCellCoordsArray coords;
	const size_t count = m_selection.size();
	coords.reserve(count);
	for (size_t n = 0; n < count; n++)
	{
		coords.push_back(m_selection[n].GetTopLeft());
	}
	return coords;
}

ibGridCellCoordsArray ibGridSelection::GetBlockSelectionBottomRight() const
{
	ibGridCellCoordsArray coords;
	const size_t count = m_selection.size();
	coords.reserve(count);
	for (size_t n = 0; n < count; n++)
	{
		coords.push_back(m_selection[n].GetBottomRight());
	}
	return coords;
}

// For compatibility with the existing code, try reconstructing the selected
// rows/columns from the selected blocks we store internally. Of course, this
// only works well in the corresponding selection mode in which the user can
// only select the entire lines in the first place, as otherwise it's difficult to
// efficiently determine that a line is selected because all of its cells
// were selected one by one. But this should work well enough in practice and
// is, anyhow, the best we can do.
wxArrayInt ibGridSelection::GetRowSelection() const
{
	if (m_selectionMode == ibGrid::ibGridSelectColumns ||
		m_selectionMode == ibGrid::ibGridSelectNone)
		return wxArrayInt();

	wxIntSortedArray uniqueRows;
	const size_t count = m_selection.size();
	for (size_t n = 0; n < count; ++n)
	{
		const ibGridBlockCoords& block = m_selection[n];
		if (block.GetLeftCol() == 0 &&
			block.GetRightCol() == m_grid->GetNumberCols() - 1)
		{
			for (int r = block.GetTopRow(); r <= block.GetBottomRow(); ++r)
			{
				if (uniqueRows.Index(r) == wxNOT_FOUND)
					uniqueRows.Add(r);
			}
		}
	}

	wxArrayInt result;
	result.reserve(uniqueRows.size());
	for (size_t i = 0; i < uniqueRows.size(); ++i)
	{
		result.push_back(uniqueRows[i]);
	}
	return result;
}

// See comments for GetRowSelection().
wxArrayInt ibGridSelection::GetColSelection() const
{
	if (m_selectionMode == ibGrid::ibGridSelectRows ||
		m_selectionMode == ibGrid::ibGridSelectNone)
		return wxArrayInt();

	wxIntSortedArray uniqueCols;
	const size_t count = m_selection.size();
	for (size_t n = 0; n < count; ++n)
	{
		const ibGridBlockCoords& block = m_selection[n];
		if (block.GetTopRow() == 0 &&
			block.GetBottomRow() == m_grid->GetNumberRows() - 1)
		{
			for (int c = block.GetLeftCol(); c <= block.GetRightCol(); ++c)
			{
				if (uniqueCols.Index(c) == wxNOT_FOUND)
					uniqueCols.Add(c);
			}
		}
	}

	wxArrayInt result;
	result.reserve(uniqueCols.size());
	for (size_t i = 0; i < uniqueCols.size(); ++i)
	{
		result.push_back(uniqueCols[i]);
	}
	return result;
}

void
ibGridSelection::Select(const ibGridBlockCoords& block,
	const wxKeyboardState& kbd,
	wxEventType eventType)
{
	if (m_grid->GetNumberRows() == 0 || m_grid->GetNumberCols() == 0)
		return;

	m_selection.push_back(block);

	// Update View:
	if (!m_grid->GetBatchCount())
	{
		m_grid->RefreshBlock(block.GetTopLeft(), block.GetBottomRight());
	}

	// Send Event, if not disabled.
	if (eventType != wxEVT_NULL)
	{
		ibGridRangeSelectEvent gridEvt(m_grid->GetId(),
			eventType,
			m_grid,
			block.GetTopLeft(),
			block.GetBottomRight(),
			true,
			kbd);
		m_grid->GetEventHandler()->ProcessEvent(gridEvt);
	}
}

void ibGridSelection::MergeOrAddBlock(wxVectorGridBlockCoords& blocks,
	const ibGridBlockCoords& newBlock)
{
	size_t count = blocks.size();
	for (size_t n = 0; n < count; n++)
	{
		const ibGridBlockCoords& block = blocks[n];

		if (block.Contains(newBlock))
			return;

		if (newBlock.Contains(block))
		{
			blocks.erase(blocks.begin() + n);
			n--;
			count--;
		}
	}

	blocks.push_back(newBlock);
}

#endif
