////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : grid window
////////////////////////////////////////////////////////////////////////////

#include "gridCommon.h"

#include <wx/generic/gridsel.h>
#include <wx/generic/private/grid.h>
#include <wx/renderer.h>

enum
{
	wxID_ROW_HEIGHT = wxID_HIGHEST + 1,
	wxID_COL_WIDTH,

	wxID_HIDE_CELL,
	wxID_SHOW_CELL
};

wxBEGIN_EVENT_TABLE(CGrid, wxGrid)
EVT_GRID_CELL_LEFT_CLICK(CGrid::OnMouseLeftDown)
EVT_GRID_LABEL_LEFT_CLICK(CGrid::OnMouseLeftDown)
EVT_GRID_CELL_LEFT_DCLICK(CGrid::OnMouseLeftDClick)
EVT_GRID_LABEL_LEFT_DCLICK(CGrid::OnMouseLeftDClick)
EVT_GRID_CELL_RIGHT_CLICK(CGrid::OnMouseRightDown)
EVT_GRID_LABEL_RIGHT_CLICK(CGrid::OnMouseRightDown)
EVT_KEY_DOWN(CGrid::OnKeyDown)
EVT_KEY_UP(CGrid::OnKeyUp)
EVT_GRID_SELECT_CELL(CGrid::OnSelectCell)
EVT_GRID_RANGE_SELECT(CGrid::OnSelectCells)
EVT_GRID_COL_SIZE(CGrid::OnGridColSize)
EVT_GRID_ROW_SIZE(CGrid::OnGridRowSize)
EVT_GRID_EDITOR_HIDDEN(CGrid::OnGridEditorHidden)
EVT_SCROLLWIN(CGrid::OnScroll)
EVT_SIZE(CGrid::OnSize)
EVT_MENU(wxID_COPY, CGrid::OnCopy)
EVT_MENU(wxID_PASTE, CGrid::OnPaste)
EVT_MENU(wxID_DELETE, CGrid::OnDelete)
EVT_MENU(wxID_ROW_HEIGHT, CGrid::OnRowHeight)
EVT_MENU(wxID_COL_WIDTH, CGrid::OnColWidth)
EVT_MENU(wxID_HIDE_CELL, CGrid::OnHideCell)
EVT_MENU(wxID_SHOW_CELL, CGrid::OnShowCell)
EVT_MENU(wxID_PROPERTIES, CGrid::OnProperties)
wxEND_EVENT_TABLE()

////////////////////////////////////////////////////////////////////

class CGridSectionTable : public wxGridStringTable {
	CSectionCtrl* m_sectionLeft;
	CSectionCtrl* m_sectionUpper;
public:

	CGridSectionTable(CSectionCtrl* sectionLeft, CSectionCtrl* sectionUpper) :
		wxGridStringTable(50, 20), m_sectionLeft(sectionLeft), m_sectionUpper(sectionUpper)
	{
	}

	// overridden functions from wxGridStringTable
	//
	virtual void Clear() override {
		m_sectionLeft->Clear();
		m_sectionUpper->Clear();
		wxGridStringTable::Clear();
	}

	virtual bool InsertRows(size_t pos = 0, size_t numRows = 1) override {
		for (size_t i = 0; i < numRows; i++) {
			m_sectionLeft->InsertRow(pos + i);
		}
		return wxGridStringTable::InsertRows(pos, numRows);
	}

	virtual bool AppendRows(size_t numRows = 1) override {
		m_sectionLeft->InsertRow(GetNumberRows() + numRows);
		return wxGridStringTable::AppendRows(numRows);
	}

	virtual bool DeleteRows(size_t pos = 0, size_t numRows = 1) override {
		m_sectionLeft->RemoveRow(numRows);
		return wxGridStringTable::DeleteRows(pos, numRows);
	}

	virtual bool AppendCols(size_t numCols = 1) override {
		m_sectionUpper->InsertRow(GetNumberCols() + numCols);
		return wxGridStringTable::AppendCols(numCols);
	}

	virtual bool InsertCols(size_t pos = 0, size_t numCols = 1) override {
		for (size_t i = 0; i < numCols; i++)
			m_sectionUpper->InsertRow(pos + i);
		return wxGridStringTable::InsertCols(pos, numCols);
	}

	virtual bool DeleteCols(size_t pos = 0, size_t numCols = 1) override {
		m_sectionUpper->RemoveRow(numCols);
		return wxGridStringTable::DeleteCols(pos, numCols);
	}

	virtual wxString GetColLabelValue(int col) override {
		wxString s;
		s << col + 1;
		return s;
	}
};

class CGridCellSectionRenderer : public wxGridCellRenderer {

	void DrawBorder(CGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rectCell,
		int row, int col,
		bool isSelected)
	{
		wxColour borderColour;
		wxPenStyle leftBorder, rightBorder,
			topBorder, bottomBorder;

		grid.GetCellBorder(row, col,
			&leftBorder, &rightBorder,
			&topBorder, &bottomBorder,
			&borderColour
		);

		if (leftBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
			dc.SetPen({ borderColour, 2, leftBorder });
			dc.DrawLine(rectCell.GetLeft(), rectCell.GetTop() - 1,
				rectCell.GetLeft(), rectCell.GetBottom() + 1);
		}

		if (rightBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
			dc.SetPen({ borderColour, 2, rightBorder });
			dc.DrawLine(rectCell.GetRight() + 1, rectCell.GetTop() - 1,
				rectCell.GetRight() + 1, rectCell.GetBottom() + 1);
		}

		if (topBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
			dc.SetPen({ borderColour, 2, topBorder });
			dc.DrawLine(rectCell.GetLeft() - 1, rectCell.GetTop(),
				rectCell.GetRight() + 1, rectCell.GetTop());
		}

		if (bottomBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
			dc.SetPen({ borderColour, 2, bottomBorder });
			dc.DrawLine(rectCell.GetLeft() - 1, rectCell.GetBottom(),
				rectCell.GetRight() + 1, rectCell.GetBottom());
		}
	}

	void DrawBackground(CGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rectCell,
		int row, int col,
		bool isSelected) {

		// erase only this cells background, overflow cells should have been erased
		dc.SetBackgroundMode(wxBRUSHSTYLE_SOLID);

		wxColour clr;
		if (grid.IsThisEnabled()) {
			if (isSelected) {
				if (grid.HasFocus())
					clr = grid.GetSelectionBackground();
				else
					clr = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
			}
			else {
				clr = attr.GetBackgroundColour();
			}
		}
		else {// grey out fields if the grid is disabled
			clr = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
		}

		dc.SetBrush(clr);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(rectCell);

		if (isSelected && !grid.IsInSelection(row, col - 1)) { //
			dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
			dc.DrawLine(rectCell.GetLeft(), rectCell.GetTop() - 1,
				rectCell.GetLeft(), rectCell.GetBottom());
		}

		if (isSelected && !grid.IsInSelection(row, col + 1)) { //
			dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
			dc.DrawLine(rectCell.GetRight() + 1, rectCell.GetTop() - 1,
				rectCell.GetRight() + 1, rectCell.GetBottom());
		}

		if (isSelected && !grid.IsInSelection(row - 1, col)) {
			dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
			dc.DrawLine(rectCell.GetLeft() - 1, rectCell.GetTop(),
				rectCell.GetRight() + 1, rectCell.GetTop());
		}

		if (isSelected && !grid.IsInSelection(row + 1, col)) {
			dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
			dc.DrawLine(rectCell.GetLeft() - 1, rectCell.GetBottom(),
				rectCell.GetRight() + 1, rectCell.GetBottom());
		}

		CGridCellSectionRenderer::DrawBorder(grid,
			attr, dc, rectCell,
			row, col,
			isSelected
		);

		for (auto block : grid.GetSelectedBlocks()) {
			grid.RefreshBlock(
				block.GetTopRow(),
				block.GetLeftCol(),
				block.GetBottomRow(),
				block.GetRightCol()
			);
		}
	}

	void DrawSection(CGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) {
		int num_rows = 0, num_cols = 0;
		attr.GetSize(&num_rows, &num_cols);

		for (int r = 0; r < num_rows; r++) {
			for (int c = 0; c < num_cols; c++) {
				wxRect rectCell = CGridCellSectionRenderer::CellToRect(grid, row + r, col + c);
				wxString sectionName;
				int flagSection = grid.GetSectionLeft()->FindInSection(row + r, sectionName);
				// top line
				if (flagSection & 2) {
					//dc.SetBrush(*wxTRANSPARENT_BRUSH);
					dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
					dc.DrawLine(rectCell.x - 1, rectCell.y - 1, rectCell.x + rectCell.width, rectCell.y - 1);
				}
				// bottom line
				if (flagSection & 4) {
					//dc.SetBrush(*wxTRANSPARENT_BRUSH);
					dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
					dc.DrawLine(rectCell.x - rectCell.width, rectCell.y + rectCell.height, rectCell.x + rectCell.width, rectCell.y + rectCell.height);
				}
				// left hand line
				flagSection = grid.GetSectionUpper()->FindInSection(col + c, sectionName);
				if (flagSection & 2) {
					//dc.SetBrush(*wxTRANSPARENT_BRUSH);
					dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
					dc.DrawLine(rectCell.x - 1, rectCell.y - 1, rectCell.x - 1, rectCell.y + rectCell.height + 1);
				}
				// right hand line
				if (flagSection & 4) {
					//dc.SetBrush(*wxTRANSPARENT_BRUSH);
					dc.SetPen(wxPen(*wxRED, 1, wxPENSTYLE_SOLID));
					dc.DrawLine(rectCell.x + rectCell.width, rectCell.y - rectCell.height - 1, rectCell.x + rectCell.width, rectCell.y + rectCell.height);
				}
			}
		}
	}

	void DrawPrintLine(CGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rectCell,
		int row, int col,
		bool isSelected) {
		if (grid.IsColBrake(col - 1)) {
			dc.SetPen(wxPen(*wxBLACK, 1, wxPENSTYLE_SHORT_DASH));
			dc.DrawLine(rectCell.x - 1, rectCell.y - 1, rectCell.x - 1, rectCell.y + rectCell.height + 1);
		}
		if (grid.IsRowBrake(row - 1)) {
			dc.SetPen(wxPen(*wxBLACK, 1, wxPENSTYLE_SHORT_DASH));
			dc.DrawLine(rectCell.x - 1, rectCell.y - 1, rectCell.x + rectCell.width, rectCell.y - 1);
		}
	}

	void DrawCell(CGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rectCell,
		int row, int col,
		bool isSelected) {
		wxRect rect = rectCell;
		wxSize bestSize = GetBestSize(grid, attr, dc, row, col);

		int best_width = bestSize.GetWidth(),
			best_height = bestSize.GetHeight() + 1;

		rect.Inflate(-1);

		if (attr.CanOverflow()) {

			int hAlign, vAlign;
			attr.GetAlignment(&hAlign, &vAlign);

			int overflowCols = 0, overflowRows = 0;
			int cols = grid.GetNumberCols(), rows = grid.GetNumberRows();
			int cell_rows, cell_cols;

			wxOrientation orientation = grid.GetCellTextOrientation(row, col);
			attr.GetSize(&cell_rows, &cell_cols); // shouldn't get here if <= 0

			if (orientation == wxOrientation::wxHORIZONTAL &&
				(best_width > rectCell.width) && (col < cols) && grid.GetTable()) {
				int i, c_cols, c_rows;
				for (i = col + cell_cols; i < cols; i++) {
					bool is_empty = true;
					for (int j = row; j < row + cell_rows; j++) {
						// check w/ anchor cell for multicell block
						grid.GetCellSize(j, i, &c_rows, &c_cols);
						if (c_rows > 0)
							c_rows = 0;
						if (!grid.GetTable()->IsEmptyCell(j + c_rows, i)) {
							is_empty = false;
							break;
						}
					}

					if (is_empty) {
						rect.width += grid.GetColSize(i);
					}
					else {
						i--;
						break;
					}

					if (rect.width >= best_width)
						break;
				}

				overflowCols = i - col - cell_cols + 1;
				if (overflowCols >= cols)
					overflowCols = cols - 1;
			}

			if (overflowCols > 0) { // redraw overflow cells w/ proper hilight
				hAlign = wxALIGN_BOTTOM; // if oveflowed then it's left aligned
				wxRect clip = rect;
				clip.x += rectCell.width;

				// draw each overflow cell individually
				int col_end = col + cell_cols + overflowCols;

				if (col_end >= grid.GetNumberCols())
					col_end = grid.GetNumberCols() - 1;

				for (int i = col + cell_cols; i <= col_end; i++) {
					// redraw the cell to update the background
					wxGridCellCoords coords(row, i);
					grid.DrawCell(dc, coords);

					clip.width = grid.GetColSize(i) - 1;
					wxDCClipper clipper(dc, clip);

					SetTextColoursAndFont(grid, attr, dc,
						grid.IsInSelection(row, i));

					grid.DrawTextRectangle(dc, grid.GetCellValue(row, col),
						rect, hAlign, vAlign, grid.GetCellTextOrientation(row, col));

					clip.x += grid.GetColSize(i) - 1;
				}

				rect = rectCell;
				rect.Inflate(-1);
				rect.width++;
			}
		}

		int hAlign, vAlign;
		attr.GetAlignment(&hAlign, &vAlign);

		// now we only have to draw the text
		SetTextColoursAndFont(grid, attr, dc, isSelected);

		//get size row 
		if (best_height != grid.GetRowHeight(row)) {
			//	grid.SetRowSize(row, best_height);
		}

		// draw text 
		grid.DrawTextRectangle(dc, grid.GetCellValue(row, col),
			rect, hAlign, vAlign, grid.GetCellTextOrientation(row, col));
	}

public:

	virtual void Draw(wxGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rectCell,
		int row, int col,
		bool isSelected) override {
		CGrid& gridOwner = static_cast<CGrid&>(grid);
		// erase only this cells background, overflow cells should have been erased
		DrawBackground(gridOwner, attr, dc, rectCell, row, col, isSelected);
		DrawCell(gridOwner, attr, dc, rectCell, row, col, isSelected);
		DrawPrintLine(gridOwner, attr, dc, rectCell, row, col, isSelected);
		DrawSection(gridOwner, attr, dc, rectCell, row, col, isSelected);
	}

	// return the string extent
	virtual wxSize GetBestSize(wxGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		int row, int col) override {
		return DoGetBestSize(attr, dc, grid.GetCellValue(row, col));
	}

	virtual wxGridCellRenderer* Clone() const override {
		return new CGridCellSectionRenderer;
	}

protected:

	wxRect CellToRect(CGrid& grid, int row, int col) const
	{
		wxRect rect(-1, -1, -1, -1);

		if (row >= 0 && row < grid.GetNumberRows() &&
			col >= 0 && col < grid.GetNumberCols()) {
			int cell_rows, cell_cols;
			rect.width = rect.height = 0;
			if (grid.GetCellSize(row, col, &cell_rows, &cell_cols) == wxGrid::CellSpan_Inside) {
				cell_rows = cell_cols = 1;
			}
			rect.x = grid.GetColLeft(col);
			rect.y = grid.GetRowTop(row);

			for (int i = col; i < col + cell_cols; i++)
				rect.width += grid.GetColWidth(i);

			for (int i = row; i < row + cell_rows; i++)
				rect.height += grid.GetRowHeight(i);

#ifndef __WXQT__
			// if grid lines are enabled, then the area of the cell is a bit smaller
			if (grid.GridLinesEnabled()) {
				rect.width -= 1;
				rect.height -= 1;
			}
#endif
		}

		return rect;
	}

	// calc the string extent for given string/font
	wxSize DoGetBestSize(const wxGridCellAttr& attr,
		wxDC& dc,
		const wxString& text) {
		dc.SetFont(attr.GetFont());
		return dc.GetMultiLineTextExtent(text);
	}

	int DoGetCharHeight(wxDC& dc) const {
		return dc.GetCharHeight();
	}

	int DoGetCharWidth(wxDC& dc) const {
		return dc.GetCharWidth();
	}
};

class CGridCellTextEditor : public wxGridCellEditor {

	void OnKeyDown(wxKeyEvent& event) {
		switch (event.GetKeyCode())
		{
		case WXK_RETURN:
		case WXK_NUMPAD_ENTER:
			if (event.ShiftDown()) {
				wxTextCtrl* textCtrl = GetTextCtrl();
				if (textCtrl != nullptr) {
					textCtrl->WriteText('\n');
				}
			}
			else {
				event.Skip();
			}
			break;

		default:
			event.Skip();
			break;
		}
	}

	void OnText(wxCommandEvent& event) {
		wxTextCtrl* textCtrl = GetTextCtrl();
		if (textCtrl != nullptr) {
			wxString buffer = textCtrl->GetValue();
			wxSize sizeCtrl = GetBestSize(textCtrl, buffer);
			textCtrl->SetSize(sizeCtrl);
		}
		event.Skip();
	}

public:
	explicit CGridCellTextEditor(size_t maxChars = 0)
		: wxGridCellEditor(),
		m_maxChars(maxChars)
	{
	}

	CGridCellTextEditor(const CGridCellTextEditor& other) : wxGridCellEditor(other),
		m_maxChars(other.m_maxChars),
		m_value(other.m_value)
	{
#if wxUSE_VALIDATORS
		if (other.m_validator) {
			SetValidator(*other.m_validator);
		}
#endif
	}

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) override {
		DoCreate(parent, id, evtHandler);
		evtHandler->Bind(wxEVT_KEY_DOWN, &CGridCellTextEditor::OnKeyDown, this);
		evtHandler->Bind(wxEVT_COMMAND_TEXT_UPDATED, &CGridCellTextEditor::OnText, this);
	}

	virtual void SetSize(const wxRect& rectOrig) override {

		wxRect rect(rectOrig);
		// Make the edit control large enough to allow for internal margins
		//
		// TODO: remove this if the text ctrl sizing is improved
		//
#if defined(__WXMSW__)
		rect.x--; 
		rect.y--;
#elif !defined(__WXGTK__)
		int extra_x = 2; ()
			int extra_y = 2;

#if defined(__WXMOTIF__)
		extra_x *= 2;
		extra_y *= 2;
#endif

		rect.SetLeft(wxMax(0, rect.x - extra_x));
		rect.SetTop(wxMax(0, rect.y - extra_y));
		rect.SetRight(rect.GetRight() + 2 * extra_x);
		rect.SetBottom(rect.GetBottom() + 2 * extra_y);
#endif

		wxGridCellEditor::SetSize(rect);
	}

	virtual bool IsAcceptedKey(wxKeyEvent& event) override {
		switch (event.GetKeyCode())
		{
		case WXK_DELETE:
		case WXK_BACK:
			return true;
		default:
			return wxGridCellEditor::IsAcceptedKey(event);
		}
	}

	virtual void BeginEdit(int row, int col, wxGrid* grid) override {
		wxASSERT_MSG(m_control, wxT("The wxGridCellEditor must be created first!"));
		m_value = grid->GetTable()->GetValue(row, col);
		DoBeginEdit(m_value);
	}

	virtual bool EndEdit(int row, int col, const wxGrid* grid,
		const wxString& oldval, wxString* newval) override {
		wxCHECK_MSG(m_control, false,
			"wxGridCellTextEditor must be created first!");

		const wxString value = GetTextCtrl()->GetValue();
		if (value == m_value)
			return false;

		m_value = value;

		if (newval)
			*newval = m_value;

		return true;
	}

	virtual void ApplyEdit(int row, int col, wxGrid* grid) override {
		grid->GetTable()->SetValue(row, col, m_value);
		m_value.clear();
	}

	virtual void Reset() override {
		wxASSERT_MSG(m_control, "wxGridCellTextEditor must be created first!");
		DoReset(m_value);
	}

	virtual void StartingKey(wxKeyEvent& event) override {

		// Since this is now happening in the EVT_CHAR event EmulateKeyPress is no
		// longer an appropriate way to get the character into the text control.
		// Do it ourselves instead.  We know that if we get this far that we have
		// a valid character, so not a whole lot of testing needs to be done.

		wxTextCtrl* tc = GetTextCtrl();
		int ch;

		bool isPrintable;

#if wxUSE_UNICODE
		ch = event.GetUnicodeKey();
		if (ch != WXK_NONE)
			isPrintable = true;
		else
#endif // wxUSE_UNICODE
		{
			ch = event.GetKeyCode();
			isPrintable = ch >= WXK_SPACE && ch < WXK_START;
		}

		switch (ch)
		{
		case WXK_DELETE:
			// Delete the initial character when starting to edit with DELETE.
			tc->Remove(0, 1);
			break;

		case WXK_BACK:
			// Delete the last character when starting to edit with BACKSPACE.
		{
			const long pos = tc->GetLastPosition();
			tc->Remove(pos - 1, pos);
		}
		break;

		default:
			if (isPrintable)
				tc->WriteText(static_cast<wxChar>(ch));
			break;
		}
	}

	virtual void HandleReturn(wxKeyEvent& event) override {
#if defined(__WXMOTIF__) || defined(__WXGTK__)
		// wxMotif needs a little extra help...
		size_t pos = (size_t)(GetTextCtrl()->GetInsertionPoint());
		wxString s(GetTextCtrl()->GetValue());
		s = s.Left(pos) + wxT("\n") + s.Mid(pos);
		GetTextCtrl()->SetValue(s);
		GetTextCtrl()->SetInsertionPoint(pos);
#else
		// the other ports can handle a Return key press
		//
		event.Skip();
#endif
	}

	// parameters string format is "max_width"
	virtual void SetParameters(const wxString& params) override {
		if (!params)
		{
			// reset to default
			m_maxChars = 0;
		}
		else
		{
			long tmp;
			if (params.ToLong(&tmp))
			{
				m_maxChars = (size_t)tmp;
			}
			else
			{
				wxLogDebug(wxT("Invalid wxGridCellTextEditor parameter string '%s' ignored"), params);
			}
		}
	}

#if wxUSE_VALIDATORS
	virtual void SetValidator(const wxValidator& validator) {
		m_validator.reset(static_cast<wxValidator*>(validator.Clone()));
		if (m_validator && IsCreated())
			GetTextCtrl()->SetValidator(*m_validator);
	}
#endif

	virtual wxGridCellEditor* Clone() const override {
		return new CGridCellTextEditor(*this);
	}

	// added GetValue so we can get the value which is in the control
	virtual wxString GetValue() const override {
		return GetTextCtrl()->GetValue();
	}

protected:

	wxSize GetBestSize(wxTextCtrl* textCtrl, const wxString& startValue) const {
		wxASSERT(textCtrl);
		wxString buffer; wxSize sizeText = { textCtrl->GetCharWidth(), textCtrl->GetCharHeight()};
		for (auto c : startValue) {
			if (c == '\n') {
				int intStringX, intStringY;
				textCtrl->GetTextExtent(buffer, &intStringX, &intStringY);
				buffer = "<initalValue>";
				if (sizeText.x < intStringX) {
					sizeText.x = intStringX;
				}
				sizeText.y += intStringY;
				continue;
			}
			buffer += c;
		}
		if (!buffer.empty()) {
			int intStringX, intStringY;
			textCtrl->GetTextExtent(buffer, &intStringX, &intStringY);
			if (sizeText.x < intStringX) {
				sizeText.x = intStringX;
			}
			sizeText.y += intStringY;
		}

		wxSize defSize = textCtrl->GetSize();

		if (sizeText.x == textCtrl->GetCharWidth() &&
			sizeText.y == textCtrl->GetCharHeight()) {
			return defSize;
		}

		wxSize newSize = textCtrl->GetSizeFromTextSize(sizeText);

		if (newSize.x < defSize.x &&
			newSize.y < defSize.y)
			return defSize;
		else if (newSize.x < defSize.x)
			return wxSize(defSize.x, newSize.y);
		else if (newSize.y < defSize.y)
			return wxSize(newSize.x, defSize.y);

		return newSize;
	}

	wxTextCtrl* GetTextCtrl() const {
		return (wxTextCtrl*)m_control;
	}

	// parts of our virtual functions reused by the derived classes
	void DoCreate(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler,
		long style = 0) {

		style |= wxTE_MULTILINE | wxTE_NO_VSCROLL | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB | wxNO_BORDER;

		wxTextCtrl* const text = new wxTextCtrl(parent, id, wxEmptyString,
			wxDefaultPosition, wxDefaultSize,
			style);

		text->SetMargins(0, 0);

		// set max length allowed in the textctrl, if the parameter was set
		if (m_maxChars != 0) {
			text->SetMaxLength(m_maxChars);
		}

		m_control = text;

#if wxUSE_VALIDATORS
		// validate text in textctrl, if validator is set
		if (m_validator) {
			text->SetValidator(*m_validator);
		}
#endif

		wxGridCellEditor::Create(parent, id, evtHandler);
	}

	void DoBeginEdit(const wxString& startValue) {

		wxTextCtrl* const textCtrl = GetTextCtrl();
		wxASSERT(textCtrl);
		textCtrl->SetValue(startValue);
		textCtrl->SetInsertionPointEnd();
		textCtrl->SelectAll();
		textCtrl->SetFocus();
	}

	void DoReset(const wxString& startValue) {
		wxTextCtrl* const textCtrl = GetTextCtrl();
		wxASSERT(textCtrl);
		textCtrl->SetValue(startValue);
		textCtrl->SetInsertionPointEnd();
	}

private:
	size_t                   m_maxChars;        // max number of chars allowed
#if wxUSE_VALIDATORS
	wxScopedPtr<wxValidator> m_validator;
#endif
	wxString                 m_value;
};

bool CGrid::GetWorkSection(int& rangeFrom, int& rangeTo, CSectionCtrl*& sectionCtrl)
{
	wxGridBlockCoords selection = GetSelectedCellRange();

	if (selection.GetRightCol() == m_numCols - 1
		&& selection.GetBottomRow() != m_numRows - 1) { //гориз. секции
		sectionCtrl = m_sectionLeft;
		rangeFrom = selection.GetTopRow();
		rangeTo = selection.GetBottomRow();
		return true;
	}
	else if (selection.GetRightCol() != m_numCols - 1
		&& selection.GetBottomRow() == m_numRows - 1) { //верт. секции
		sectionCtrl = m_sectionUpper;
		rangeFrom = selection.GetLeftCol();
		rangeTo = selection.GetRightCol();
		return true;
	}

	return false;
}

void CGrid::CalcSection()
{
	if (m_sectionEnabled) {
		m_rowLabelWidth = WXGRID_DEFAULT_ROW_LABEL_WIDTH - 42 + m_sectionLeft->CellSize();
		m_colLabelHeight = WXGRID_MIN_COL_WIDTH + m_sectionUpper->CellSize();
	}
	else {
		m_rowLabelWidth = WXGRID_DEFAULT_ROW_LABEL_WIDTH - 42;
		m_colLabelHeight = WXGRID_MIN_COL_WIDTH;
	}

	wxGrid::CalcDimensions();
}

// ctor and Create() create the grid window, as with the other controls
CGrid::CGrid() : wxGrid()
{
	//set double buffered
	wxGrid::SetDoubleBuffered(true);
}

CGrid::CGrid(wxWindow* parent,
	wxWindowID id, const wxPoint& pos,
	const wxSize& size) : wxGrid(parent, id, pos, size),
	m_gridProperty(new CPropertyObjectGrid(this)),
	m_sectionLeft(new CSectionCtrl(this, eSectionMode::LEFT_MODE)), m_sectionUpper(new CSectionCtrl(this, eSectionMode::UPPER_MODE)),
	m_sectionEnabled(true)
{
	m_rowLabelWidth = WXGRID_DEFAULT_ROW_LABEL_WIDTH - 42;
	m_colLabelHeight = WXGRID_MIN_COL_WIDTH;
	m_defaultColWidth = WXGRID_DEFAULT_ROW_LABEL_WIDTH - 12;
	m_defaultRowHeight = WXGRID_MIN_ROW_HEIGHT;

	// Grid
	wxGrid::EnableEditing(true);
	wxGrid::EnableGridLines(true);
	wxGrid::EnableDragGridSize(false);
	wxGrid::SetScrollRate(15, 15);
	wxGrid::SetMargins(0, 0);

	// Native col 
	wxGrid::UseNativeColHeader(false);

	//set double buffered
	wxGrid::SetDoubleBuffered(true);

	// Cell Defaults
	wxGrid::SetDefaultCellAlignment(wxAlignment::wxALIGN_LEFT, wxAlignment::wxALIGN_BOTTOM);

	wxGrid::SetTable(
		new CGridSectionTable(m_sectionLeft, m_sectionUpper),
		true, wxGridSelectCells
	);

	m_selectionBackground.Set(211, 217, 239);
	m_selectionForeground.Set(0, 0, 0);

	m_gridWin->Bind(wxEVT_PAINT, &CGrid::OnPaint, this);

	wxGrid::SetDefaultRenderer(new CGridCellSectionRenderer);
	wxGrid::SetDefaultEditor(new CGridCellTextEditor);
}

CGrid::~CGrid()
{
	//m_gridWin->Unbind(wxEVT_PAINT, &CGrid::OnPaint, this);

	if (m_frozenRowGridWin != nullptr) {
		m_frozenRowGridWin->Unbind(wxEVT_PAINT, &CGrid::OnPaint, this);
	}
	if (m_frozenColGridWin != nullptr) {
		m_frozenColGridWin->Unbind(wxEVT_PAINT, &CGrid::OnPaint, this);
	}
	if (m_frozenCornerGridWin != nullptr) {
		m_frozenCornerGridWin->Unbind(wxEVT_PAINT, &CGrid::OnPaint, this);
	}

	wxDELETE(m_sectionLeft);
	wxDELETE(m_sectionUpper);

	//delete property
	wxDELETE(m_gridProperty);
}

void CGrid::AddSection()
{
	if (!wxGrid::IsEditable())
		return;

	int rangeFrom, rangeTo;
	CSectionCtrl* sectionCtrl = nullptr;

	if (!GetWorkSection(rangeFrom, rangeTo, sectionCtrl))
		return;

	m_sectionEnabled = true;

	if (sectionCtrl->Add(rangeFrom, rangeTo)) {

		int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
			rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;

		if (sectionCtrl->GetSectionMode() == eSectionMode::LEFT_MODE) {
			EraseRowBrake();
			AddRowBrake(
				rowBrake > rangeTo ? rowBrake : rangeTo
			);
		}
		else if (sectionCtrl->GetSectionMode() == eSectionMode::UPPER_MODE) {
			EraseColBrake();
			AddColBrake(
				colBrake > rangeTo ? colBrake : rangeTo
			);
		}

		CalcSection();
	}

	wxGrid::ForceRefresh();
}

void CGrid::RemoveSection()
{
	if (!wxGrid::IsEditable())
		return;

	int rangeFrom, rangeTo;
	CSectionCtrl* sectionCtrl = nullptr;

	if (!GetWorkSection(rangeFrom, rangeTo, sectionCtrl))
		return;

	m_sectionEnabled = sectionCtrl->Count() > 0;

	if (sectionCtrl->Remove(rangeFrom, rangeTo)) {
		CalcSection();
	}

	wxGrid::ForceRefresh();
}

void CGrid::MergeCells()
{
	if (!wxGrid::IsEditable())
		return;

	wxGridBlockCoords cellRange = GetSelectedCellRange();

	wxGrid::SetCellSize(
		cellRange.GetTopRow(),
		cellRange.GetLeftCol(),
		cellRange.GetBottomRow() - cellRange.GetTopRow() + 1,
		cellRange.GetRightCol() - cellRange.GetLeftCol() + 1
	);

	wxGrid::SendEvent(wxEVT_GRID_EDITOR_HIDDEN);
	wxGrid::ForceRefresh();
}

void CGrid::DockTable()
{
	if (!wxGrid::IsEditable())
		return;

	wxGridBlockCoords cellRange = GetSelectedCellRange();

	CGrid::FreezeTo(
		cellRange.GetBottomRow(),
		cellRange.GetRightCol()
	);
}

#include <wx/clipbrd.h>

void CGrid::Copy()
{
	wxString copy_data;
	bool something_in_this_line;

	copy_data.Clear();

	for (int i = 0; i < m_numRows; i++) {
		something_in_this_line = false;
		for (int j = 0; j < m_numCols; j++) {
			if (IsInSelection(i, j)) {
				if (something_in_this_line == false) {
					if (copy_data.IsEmpty() == false) {
						copy_data.Append(wxT("\n"));
					}
					something_in_this_line = true;
				}
				else {
					copy_data.Append(wxT("\t"));
				}
				copy_data = copy_data + GetCellValue(i, j);
			}
		}
	}

	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(copy_data));
		wxTheClipboard->Close();
	}
}

void CGrid::Paste()
{
	wxString copy_data, cur_field, cur_line;

	if (wxTheClipboard->Open()
		&& wxTheClipboard->IsSupported(wxDF_TEXT)) {
		wxTextDataObject textObj;
		if (wxTheClipboard->GetData(textObj)) {
			copy_data = textObj.GetText();
		}
		wxTheClipboard->Close();
	}

	int i = GetGridCursorRow(), j = GetGridCursorCol();
	int k = j;

	int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
		rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;

	while (!copy_data.IsEmpty()) {
		cur_line = copy_data.BeforeFirst('\n');
		while (!cur_line.IsEmpty()) {
			cur_field = cur_line.BeforeFirst('\t');
			AddBrake(
				colBrake > j ? colBrake : j,
				rowBrake > i ? rowBrake : i
			);
			SetCellValue(i, j, cur_field);
			j++;
			cur_line = cur_line.AfterFirst('\t');
		}
		i++;
		j = k;
		copy_data = copy_data.AfterFirst('\n');
	}

	wxGrid::ForceRefresh();
}

#pragma region draw 
// this struct simply combines together the default header renderers
//
// as the renderers ctors are trivial, there is no problem with making them
// globals
struct DefaultHeaderRenderers
{
	wxGridColumnHeaderRendererDefault colRenderer;
	wxGridRowHeaderRendererDefault rowRenderer;
	wxGridCornerHeaderRendererDefault cornerRenderer;
} gs_defaultHeaderRenderers;

void CGrid::DrawCellHighlight(wxDC& dc, const wxGridCellAttr* attr)
{
	int row = m_currentCellCoords.GetRow();
	int col = m_currentCellCoords.GetCol();

	if (GetColWidth(col) <= 0 ||
		GetRowHeight(row) <= 0)
		return;

	wxRect rectCell = CellToRect(row, col);

	bool needDrawHighlight = !wxGrid::IsInSelection(row, col - 1)
		&& !wxGrid::IsInSelection(row, col + 1)
		&& !wxGrid::IsInSelection(row - 1, col)
		&& !wxGrid::IsInSelection(row + 1, col);

	if (needDrawHighlight) {

		// hmmm... what could we do here to show that the cell is disabled?
		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rectCell.GetLeft(), rectCell.GetTop() - 1,
			rectCell.GetLeft(), rectCell.GetBottom());

		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rectCell.GetRight(), rectCell.GetTop(),
			rectCell.GetRight(), rectCell.GetBottom());

		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rectCell.GetLeft(), rectCell.GetTop(),
			rectCell.GetRight(), rectCell.GetTop());

		dc.SetPen({ *wxBLACK, 2, wxPENSTYLE_SOLID });
		dc.DrawLine(rectCell.GetLeft(), rectCell.GetBottom(),
			rectCell.GetRight(), rectCell.GetBottom());
	}

	for (auto block : wxGrid::GetSelectedBlocks()) {
		wxGrid::RefreshBlock(block.GetTopRow(), block.GetLeftCol(), block.GetBottomRow(), block.GetRightCol());
	}
}

void CGrid::DrawCornerLabel(wxDC& dc)
{
	wxRect rect;

	if (m_sectionEnabled) {
		rect.x = m_sectionLeft->CellSize();
		rect.y = m_sectionUpper->CellSize();
		rect.width = m_rowLabelWidth;
		rect.height = m_colLabelHeight;
	}
	else {
		rect.x = 0;
		rect.y = 0;
		rect.width = m_rowLabelWidth;
		rect.height = m_colLabelHeight;
	}

	wxGridCellAttrProvider* const
		attrProvider = m_table ? m_table->GetAttrProvider() : nullptr;
	const wxGridCornerHeaderRenderer&
		rend = attrProvider ? attrProvider->GetCornerRenderer()
		: static_cast<wxGridCornerHeaderRenderer&>
		(gs_defaultHeaderRenderers.cornerRenderer);

	if (m_nativeColumnLabels) {
		rect.Deflate(1);
		wxRendererNative::Get().DrawHeaderButton(m_cornerLabelWin, dc, rect, 0);
	}
	else {
		rect.width++; rect.height++;
		rend.DrawBorder(*this, dc, rect);
	}

	wxString label = GetCornerLabelValue();
	if (!label.IsEmpty()) {
		int hAlign, vAlign;
		GetCornerLabelAlignment(&hAlign, &vAlign);
		rend.DrawLabel(*this, dc, label, rect, hAlign, vAlign, GetCornerLabelTextOrientation());
	}
}

void CGrid::DrawColLabels(wxDC& dc, const wxArrayInt& cols)
{
	if (!m_numCols)
		return;

	const size_t numLabels = cols.GetCount();
	for (size_t i = 0; i < numLabels; i++) {
		DrawColLabel(dc, cols[i]);
	}

	if (m_sectionEnabled) {

		wxGridCellAttrProvider* const
			attrProvider = m_table ? m_table->GetAttrProvider() : nullptr;

		int hAlign, vAlign;
		wxGrid::GetColLabelAlignment(&hAlign, &vAlign);

		for (unsigned int s = 0; s < m_sectionUpper->Count(); s++) {
			section_t& section = m_sectionUpper->GetSection(s);
			size_t colWidth = 0;
			for (unsigned int i = section.m_rangeFrom; i <= section.m_rangeTo; i++) {
				colWidth += CGrid::GetColWidth(i);
			}

			const wxGridColumnHeaderRenderer&
				rend = attrProvider ? attrProvider->GetColumnHeaderRenderer(section.m_rangeFrom)
				: static_cast<wxGridColumnHeaderRenderer&>
				(gs_defaultHeaderRenderers.colRenderer);

			int colLeft = GetColLeft(section.m_rangeFrom);
			wxRect rectSection(colLeft > 0 ? colLeft - 1 : 0, 0, colWidth, m_sectionUpper->CellSize());

			rend.DrawLabel(*this, dc, section.m_sectionName,
				rectSection, hAlign, vAlign, wxHORIZONTAL);
		}
	}
}

void CGrid::DrawColLabel(wxDC& dc, int col)
{
	if (GetColWidth(col) <= 0 || m_colLabelHeight <= 0)
		return;

	int colLeft = GetColLeft(col);

	wxRect rect;

	if (m_sectionEnabled) {
		rect.x = colLeft;
		rect.y = m_sectionUpper->CellSize();
		rect.width = GetColWidth(col);
		rect.height = m_colLabelHeight - m_sectionUpper->CellSize();
	}
	else {
		rect.x = colLeft;
		rect.y = 0;
		rect.width = GetColWidth(col);
		rect.height = m_colLabelHeight;
	}

	wxGridCellAttrProvider* const
		attrProvider = m_table ? m_table->GetAttrProvider() : nullptr;
	const wxGridColumnHeaderRenderer&
		rend = attrProvider ? attrProvider->GetColumnHeaderRenderer(col)
		: static_cast<wxGridColumnHeaderRenderer&>
		(gs_defaultHeaderRenderers.colRenderer);

	if (m_nativeColumnLabels) {
		wxRendererNative::Get().DrawHeaderButton
		(
			GetColLabelWindow(),
			dc,
			rect,
			0,
			IsSortingBy(col)
			? IsSortOrderAscending()
			? wxHDR_SORT_ICON_UP
			: wxHDR_SORT_ICON_DOWN
			: wxHDR_SORT_ICON_NONE
		);
		rect.Deflate(2);
	}
	else
	{
		// It is reported that we need to erase the background to avoid display
		// artefacts, see #12055.
		wxDCBrushChanger setBrush(dc, m_colLabelWin->GetBackgroundColour());
		dc.DrawRectangle(rect);

		rend.DrawBorder(*this, dc, rect);
	}

	if (m_sectionEnabled) {

		wxRect rectSection(colLeft, 0, GetColWidth(col), m_sectionUpper->CellSize());

		wxString sectionName;
		int flagSection = m_sectionUpper->FindInSection(col, sectionName);
		if (flagSection & 2) {
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
			dc.DrawLine(rectSection.GetLeft(), rectSection.GetBottom(), rectSection.GetLeft(), rectSection.GetTop());
		}

		if (flagSection & 3) {
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
			dc.DrawLine(rectSection.GetLeft(), rectSection.GetTop(), rectSection.GetRight(), rectSection.GetTop());
		}

		if (flagSection & 4) {
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
			dc.DrawLine(rectSection.GetRight(), rectSection.GetTop(), rectSection.GetRight(), rectSection.GetBottom());
		}
	}

	int hAlign, vAlign;
	wxGrid::GetColLabelAlignment(&hAlign, &vAlign);

	rend.DrawLabel(*this, dc, GetColLabelValue(col), rect, hAlign, vAlign, GetColLabelTextOrientation());
}

void CGrid::DrawRowLabels(wxDC& dc, const wxArrayInt& rows)
{
	if (!m_numRows)
		return;

	const size_t numLabels = rows.GetCount();
	for (size_t i = 0; i < numLabels; i++) {
		DrawRowLabel(dc, rows[i]);
	}

	if (m_sectionEnabled) {
		wxGridCellAttrProvider* const
			attrProvider = m_table ? m_table->GetAttrProvider() : nullptr;

		int hAlign, vAlign;
		wxGrid::GetColLabelAlignment(&hAlign, &vAlign);

		for (unsigned int s = 0; s < m_sectionLeft->Count(); s++) {

			section_t& section = m_sectionLeft->GetSection(s);

			size_t rowHeight = 0;
			for (unsigned int i = section.m_rangeFrom; i <= section.m_rangeTo; i++) {
				rowHeight += CGrid::GetRowHeight(i);
			}

			const wxGridRowHeaderRenderer&
				rend = attrProvider ? attrProvider->GetRowHeaderRenderer(section.m_rangeFrom)
				: static_cast<const wxGridRowHeaderRenderer&>
				(gs_defaultHeaderRenderers.rowRenderer);

			wxRect rectSection(0, GetRowTop(section.m_rangeFrom), m_sectionLeft->CellSize(), rowHeight + 1);

			rend.DrawLabel(*this, dc, section.m_sectionName,
				rectSection, hAlign, vAlign, wxHORIZONTAL);
		}
	}
}

void CGrid::DrawRowLabel(wxDC& dc, int row)
{
	if (GetRowHeight(row) <= 0 || m_rowLabelWidth <= 0)
		return;

	wxGridCellAttrProvider* const
		attrProvider = m_table ? m_table->GetAttrProvider() : nullptr;

	// notice that an explicit static_cast is needed to avoid a compilation
	// error with VC7.1 which, for some reason, tries to instantiate (abstract)
	// wxGridRowHeaderRenderer class without it
	const wxGridRowHeaderRenderer&
		rend = attrProvider ? attrProvider->GetRowHeaderRenderer(row)
		: static_cast<const wxGridRowHeaderRenderer&>
		(gs_defaultHeaderRenderers.rowRenderer);

	wxRect rect;

	if (m_sectionEnabled) {
		rect.x = m_sectionLeft->CellSize();
		rect.y = GetRowTop(row);
		rect.width = m_rowLabelWidth - m_sectionLeft->CellSize();
		rect.height = GetRowHeight(row);
	}
	else {
		rect.x = 0;
		rect.y = GetRowTop(row);
		rect.width = m_rowLabelWidth;
		rect.height = GetRowHeight(row);
	}

	if (m_nativeColumnLabels) {
		wxRendererNative::Get().DrawHeaderButton
		(
			GetGridRowLabelWindow(),
			dc,
			rect,
			0,
			IsSortingBy(row)
			? IsSortOrderAscending()
			? wxHDR_SORT_ICON_UP
			: wxHDR_SORT_ICON_DOWN
			: wxHDR_SORT_ICON_NONE
		);
		rect.Deflate(2);
	}
	else {
		rend.DrawBorder(*this, dc, rect);
	}

	if (m_sectionEnabled) {

		wxRect rectSection(0, GetRowTop(row), m_sectionLeft->CellSize(), GetRowHeight(row));

		wxString sectionName;
		int flagSection = m_sectionLeft->FindInSection(row, sectionName);

		if (flagSection & 2) {
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
			dc.DrawLine(rectSection.GetLeft(), rectSection.GetTop(), rectSection.GetRight(), rectSection.GetTop());
		}

		if (flagSection & 3) {
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
			dc.DrawLine(rectSection.GetLeft(), rectSection.GetTop(), rectSection.GetLeft(), rectSection.GetBottom());
		}

		if (flagSection & 4) {
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
			dc.DrawLine(rectSection.GetLeft(), rectSection.GetBottom(), rectSection.GetRight(), rectSection.GetBottom());
		}
	}

	int hAlign, vAlign;
	GetRowLabelAlignment(&hAlign, &vAlign);

	rend.DrawLabel(*this, dc, GetRowLabelValue(row),
		rect, hAlign, vAlign, wxHORIZONTAL);
}

#pragma endregion

bool CGrid::FreezeTo(int row, int col)
{
	bool hasFrozenRowGridWin = m_frozenRowGridWin != nullptr;
	bool hasFrozenColGridWin = m_frozenColGridWin != nullptr;
	bool hasFrozenCornerGridWin = m_frozenCornerGridWin != nullptr;

	if (wxGrid::FreezeTo(row, col)) {
		if (!hasFrozenRowGridWin && m_frozenRowGridWin != nullptr) {
			m_frozenRowGridWin->SetDoubleBuffered(true);
			m_frozenRowGridWin->Bind(wxEVT_PAINT, &CGrid::OnPaint, this);
			m_rowFrozenLabelWin->SetDoubleBuffered(true);
		}
		if (!hasFrozenColGridWin && m_frozenColGridWin != nullptr) {
			m_frozenColGridWin->SetDoubleBuffered(true);
			m_frozenColGridWin->Bind(wxEVT_PAINT, &CGrid::OnPaint, this);
			m_colFrozenLabelWin->SetDoubleBuffered(true);
		}
		if (!hasFrozenCornerGridWin && m_frozenCornerGridWin != nullptr) {
			m_frozenCornerGridWin->SetDoubleBuffered(true);
			m_frozenCornerGridWin->Bind(wxEVT_PAINT, &CGrid::OnPaint, this);
		}
		return true;
	}

	return false;
}

void CGrid::SetCellTextOrientation(int row, int col, const wxOrientation& orient)
{
	gridAttrData_t* gridAttr = CreateAndGetClientAttr(row, col);
	wxASSERT(gridAttr);
	gridAttr->m_textOrient = orient;
}

wxOrientation CGrid::GetCellTextOrientation(int row, int col) const
{
	gridAttrData_t* gridAttr = CreateAndGetClientAttr(row, col);
	wxASSERT(gridAttr);
	return gridAttr->m_textOrient;
}

void CGrid::SetCellBorder(int row, int col,
	const wxPenStyle& left, const wxPenStyle& right,
	const wxPenStyle& top, const wxPenStyle& bottom,
	const wxColour& borderColour)
{
	gridAttrData_t* gridAttr = CreateAndGetClientAttr(row, col);
	wxASSERT(gridAttr);
	gridAttr->m_leftBorder = left;
	gridAttr->m_rightBorder = right;
	gridAttr->m_topBorder = top;
	gridAttr->m_bottomBorder = bottom;
	gridAttr->m_borderColour = borderColour;
}

void CGrid::GetCellBorder(int row, int col,
	wxPenStyle* left, wxPenStyle* right,
	wxPenStyle* top, wxPenStyle* bottom,
	wxColour* colour) const
{
	gridAttrData_t* gridAttr = CreateAndGetClientAttr(row, col);
	wxASSERT(gridAttr);
	if (left) *left = gridAttr->m_leftBorder;
	if (right) *right = gridAttr->m_rightBorder;
	if (top) *top = gridAttr->m_topBorder;
	if (bottom) *bottom = gridAttr->m_bottomBorder;
	if (colour) *colour = gridAttr->m_borderColour;
}

wxGridCellCoords CGrid::GetCellFromPoint(const wxPoint& point) const
{
	wxGridCellCoords cellID; // return value 

	int ux, uy,
		sx, sy;

	wxGrid::GetScrollPixelsPerUnit(&ux, &uy);
	wxGrid::GetViewStart(&sx, &sy);

	sx *= ux; sy *= uy;

	int w, h;
	m_gridWin->GetClientSize(&w, &h);

	int x0 = wxGrid::XToCol(sx);
	int y0 = wxGrid::YToRow(sy);
	int x1 = wxGrid::XToCol(sx + w, true);
	int y1 = wxGrid::YToRow(sy + h, true);

	// calculate column index
	int xpos = 0, col = x0 + 1;
	while (col < m_numCols) {
		xpos += wxGrid::GetColWidth(col);
		if (xpos > point.x)
			break;
		col++;
	}

	if (col >= m_numCols)
		cellID.SetCol(-1);
	else
		cellID.SetCol(col);

	// calculate row index
	int ypos = 0, row = y0 + 1;
	while (row < m_numRows) {
		ypos += wxGrid::GetRowHeight(row);
		if (ypos > point.y)
			break;
		row++;
	}

	if (row >= m_numRows)
		cellID.SetRow(-1);
	else
		cellID.SetRow(row);

	return cellID;
}

#pragma region events
void CGrid::OnMouseLeftDown(wxGridEvent& event)
{
	if (!wxGrid::IsEditable())
		return;

	wxPoint pos = event.GetPosition();

	if (event.GetCol() == wxNOT_FOUND) {
		wxString secName;
		if (pos.x < (int)m_sectionLeft->CellSize()) {
			int posLeft = m_sectionLeft->FindInSection(event.GetRow(), secName);
			if (posLeft > 0) {
				section_t* section = m_sectionLeft->FindSectionByPos(event.GetRow());
				if (section != nullptr) {
					m_currentCellCoords.Set(section->m_rangeFrom, 0);
					wxGrid::SelectBlock(section->m_rangeFrom, 0, section->m_rangeTo, m_numCols - 1);
				}
			}
			if (posLeft != 0) {
				return;
			}
		}
	}
	else if (event.GetRow() == wxNOT_FOUND) {
		wxString secName;
		if (pos.y < (int)m_sectionUpper->CellSize()) {
			int posUpper = m_sectionUpper->FindInSection(event.GetCol(), secName);
			if (posUpper > 0) {
				section_t* section = m_sectionUpper->FindSectionByPos(event.GetCol());
				if (section != nullptr) {
					m_currentCellCoords.Set(0, section->m_rangeFrom);
					wxGrid::SelectBlock(0, section->m_rangeFrom, m_numRows - 1, section->m_rangeTo);
				}
			}
			if (posUpper != 0) {
				return;
			}
		}
	}

	event.Skip();
}

void CGrid::OnMouseLeftDClick(wxGridEvent& event)
{
	if (!wxGrid::IsEditable())
		return;

	wxPoint pos = event.GetPosition();

	if (event.GetCol() == wxNOT_FOUND) {
		wxString secName;
		if (pos.x < (int)m_sectionLeft->CellSize()) {
			int posLeft = m_sectionLeft->FindInSection(event.GetRow(), secName);
			if (posLeft > 0) {
				section_t* section = m_sectionLeft->FindSectionByPos(event.GetRow());
				if (section != nullptr) {
					if (m_sectionLeft->EditName(*section)) {
						m_rowLabelWin->Refresh();
						return;
					}
				}
			}
		}
	}
	else if (event.GetRow() == wxNOT_FOUND) {
		wxString secName;
		if (pos.y < (int)m_sectionUpper->CellSize()) {
			int posUpper = m_sectionUpper->FindInSection(event.GetCol(), secName);
			if (posUpper > 0) {
				section_t* section = m_sectionUpper->FindSectionByPos(event.GetCol());
				if (section != nullptr) {
					if (m_sectionUpper->EditName(*section)) {
						m_colLabelWin->Refresh();
						return;
					}
				}
			}
		}
	}

	event.Skip();
}

#include <wx/artprov.h>

void CGrid::OnMouseRightDown(wxGridEvent& event)
{
	if (event.GetRow() == wxNOT_FOUND &&
		event.GetCol() == wxNOT_FOUND) {
	}
	else if (event.GetRow() == wxNOT_FOUND) {
		wxGrid::SetCurrentCell(0, event.GetCol()); bool foundedCol = false;
		for (auto block : wxGrid::GetSelectedBlocks()) {
			if (block.GetLeftCol() <= event.GetCol() && block.GetTopRow() == 0
				&& block.GetRightCol() >= event.GetCol() && block.GetBottomRow() == m_numRows - 1) {
				foundedCol = true; break;
			}
		}
		if (!foundedCol) {
			wxGrid::SelectBlock(0, event.GetCol(), m_numRows - 1, event.GetCol());
		}
	}
	else if (event.GetCol() == wxNOT_FOUND) {
		wxGrid::SetCurrentCell(event.GetRow(), 0); bool foundedRow = false;
		for (auto block : wxGrid::GetSelectedBlocks()) {
			if (block.GetLeftCol() == 0 && block.GetTopRow() <= event.GetRow() &&
				block.GetRightCol() == m_numCols - 1 && block.GetBottomRow() >= event.GetRow()) {
				foundedRow = true; break;
			}
		}
		if (!foundedRow) {
			wxGrid::SelectBlock(event.GetRow(), 0, event.GetRow(), m_numCols - 1);
		}
	}
	else {
		wxGrid::SetCurrentCell(event.GetRow(), event.GetCol());
	}

	wxMenuItem* item = nullptr; wxMenu menuPopup;
	item = menuPopup.Append(wxID_COPY, _("Copy"));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_COPY, wxART_MENU));
	item = menuPopup.Append(wxID_PASTE, _("Paste"));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_MENU));

	if (!wxGrid::CanEnableCellControl())
		menuPopup.Enable(wxID_PASTE, false);

	item = menuPopup.Append(wxID_DELETE, _("Delete"));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU));

	if (event.GetRow() == wxNOT_FOUND &&
		event.GetCol() != wxNOT_FOUND) {
		menuPopup.AppendSeparator();
		item = menuPopup.Append(wxID_HIDE_CELL, _("Hide"));
		item->SetBitmap(wxArtProvider::GetBitmap(wxART_MINUS, wxART_MENU));
		item = menuPopup.Append(wxID_SHOW_CELL, _("Display"));
		item->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU));
		item = menuPopup.Append(wxID_COL_WIDTH, _("Column width..."));
		item->SetBitmap(wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_MENU));
	}
	else if (event.GetRow() != wxNOT_FOUND &&
		event.GetCol() == wxNOT_FOUND) {
		menuPopup.AppendSeparator();
		item = menuPopup.Append(wxID_HIDE_CELL, _("Hide"));
		item->SetBitmap(wxArtProvider::GetBitmap(wxART_MINUS, wxART_MENU));
		item = menuPopup.Append(wxID_SHOW_CELL, _("Display"));
		item->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU));
		item = menuPopup.Append(wxID_ROW_HEIGHT, _("Row height..."));
		item->SetBitmap(wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_MENU));
	}

	menuPopup.AppendSeparator();
	item = menuPopup.Append(wxID_PROPERTIES, _("Properties"));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_MENU));

	if (wxGrid::PopupMenu(&menuPopup, event.GetPosition())) {
		event.Skip();
	}
}

// This seems to be required for wxMotif/wxGTK otherwise the mouse
// cursor must be in the cell edit control to get key events
//
void CGrid::OnKeyDown(wxKeyEvent& event)
{
	int maxCol = 0, maxRow = 0;

	if (maxCol < m_currentCellCoords.GetCol())
		maxCol = m_currentCellCoords.GetCol();

	if (maxRow < m_currentCellCoords.GetRow())
		maxRow = m_currentCellCoords.GetRow();

	switch (event.GetKeyCode())
	{
	case WXK_DOWN: if (maxRow == wxGrid::GetNumberRows() - 1)
		wxGrid::AppendRows(5);
		break;
	case WXK_RIGHT: if (maxCol == wxGrid::GetNumberCols() - 1)
		wxGrid::AppendCols(2);
		break;
	}

	if ((event.GetUnicodeKey() == 'C') && event.ControlDown()) {
		wxMenuEvent event(wxEVT_MENU, wxID_COPY);
		wxPostEvent(this, event);
	}
	else if ((event.GetUnicodeKey() == 'V') && event.ControlDown()) {
		wxMenuEvent event(wxEVT_MENU, wxID_PASTE);
		wxPostEvent(this, event);
	}

	event.Skip();
}

void CGrid::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void CGrid::OnSelectCell(wxGridEvent& event)
{
	m_gridProperty->SetReadOnly(wxGrid::IsEditable());

	if (event.Selecting()) {
		m_gridProperty->AddSelectedCell(
			{
			 event.GetRow(), event.GetCol(),
			 event.GetRow(), event.GetCol()
			}
		);
	}
	else {
		m_gridProperty->ClearSelectedCell();
	}

	event.Skip();
}

void CGrid::OnSelectCells(wxGridRangeSelectEvent& event)
{
	m_gridProperty->SetReadOnly(wxGrid::IsEditable());

	if (event.Selecting()) {
		m_gridProperty->AddSelectedCell({
			event.GetTopRow(), event.GetLeftCol(),
			event.GetBottomRow(), event.GetRightCol()
			}
		);
	}
	else {
		m_gridProperty->ClearSelectedCell();
	}

	event.Skip();
}

void CGrid::OnGridColSize(wxGridSizeEvent& event)
{
	CalcSection();
	wxGrid::SendEvent(wxEVT_GRID_EDITOR_HIDDEN, wxGridCellCoords{ m_currentCellCoords.GetCol(), event.GetRowOrCol() });
	wxGrid::ForceRefresh();
	event.Skip();
}

void CGrid::OnGridRowSize(wxGridSizeEvent& event)
{
	CalcSection();
	wxGrid::SendEvent(wxEVT_GRID_EDITOR_HIDDEN, wxGridCellCoords{ event.GetRowOrCol(), m_currentCellCoords.GetCol() });
	wxGrid::ForceRefresh();
	event.Skip();
}

void CGrid::OnGridEditorHidden(wxGridEvent& event)
{
	int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
		rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;

	int col = event.GetCol(),
		row = event.GetRow();

	int cell_rows = 0, cell_cols = 0;
	if (wxGrid::GetCellSize(row, col, &cell_rows, &cell_cols) == wxGrid::CellSpan_Main) {
		row += cell_rows - 1; col += cell_cols - 1;
	}
	else if (wxGrid::GetCellSize(row, col, &cell_rows, &cell_cols) == wxGrid::CellSpan_Inside) {
		row += cell_rows; col += cell_cols;
		if (GetCellSize(row, col, &cell_rows, &cell_cols) == wxGrid::CellSpan_Main) {
			row += cell_rows; col += cell_cols;
		}
	}

	AddBrake(
		colBrake > col ? colBrake : col,
		rowBrake > row ? rowBrake : row
	);

	event.Skip();
}

#include <wx/dcbuffer.h>

void CGrid::OnPaint(wxPaintEvent& event)
{
	wxGridWindow* gridWin = dynamic_cast<wxGridWindow*>(event.GetEventObject());

	if (gridWin == nullptr)
		return;

	wxAutoBufferedPaintDC dc(gridWin);
	wxGrid::PrepareDCFor(dc, gridWin);
	wxRegion reg = gridWin->GetUpdateRegion();

	wxGridCellCoordsArray dirtyCells =
		wxGrid::CalcCellsExposed(reg, gridWin);

	wxGrid::DrawAllGridWindowLines(dc, reg, gridWin);
	wxGrid::DrawGridSpace(dc, gridWin);
	wxGrid::DrawGridCellArea(dc, dirtyCells);

	wxGrid::DrawHighlight(dc, dirtyCells);

	if (gridWin->GetType() != wxGridWindow::wxGridWindowNormal)
		wxGrid::DrawFrozenBorder(dc, gridWin);
}

void CGrid::OnScroll(wxScrollWinEvent& event)
{
	int ux, uy,
		sx, sy;

	wxGrid::GetScrollPixelsPerUnit(&ux, &uy);
	wxGrid::GetViewStart(&sx, &sy);

	sx *= ux; sy *= uy;

	int w, h;
	m_gridWin->GetClientSize(&w, &h);

	if (m_frozenColGridWin) {
		int w1, h1;
		m_frozenColGridWin->GetClientSize(&w1, &h1);
		//w += w1; h += h1;
	}

	if (m_frozenRowGridWin) {
		int w2, h2;
		m_frozenRowGridWin->GetClientSize(&w2, &h2);
		//w += w2; h += h2;
	}
	if (m_frozenCornerGridWin) {
		int w3, h3;
		m_frozenCornerGridWin->GetClientSize(&w3, &h3);
		//w += w3; h += h3;
	}

	int x0 = wxGrid::XToCol(sx);
	int y0 = wxGrid::YToRow(sy);
	int x1 = wxGrid::XToCol(sx + w, true);
	int y1 = wxGrid::YToRow(sy + h, true);

	switch (event.GetOrientation())
	{
	case wxOrientation::wxVERTICAL:
		if (y1 == wxGrid::GetNumberRows() - 1) {
			wxGrid::AppendRows();
		}
		break;
	case wxOrientation::wxHORIZONTAL:
		if (x1 == wxGrid::GetNumberCols() - 1) {
			wxGrid::AppendCols(2);
		}
		break;
	}

	event.Skip();
}

void CGrid::OnSize(wxSizeEvent& event)
{
	CGrid::CalcSection();
	event.Skip();
}
#pragma endregion

#pragma region context_menu
void CGrid::OnCopy(wxCommandEvent& event)
{
	Copy();
}

void CGrid::OnPaste(wxCommandEvent& event)
{
	Paste();
}

void CGrid::OnDelete(wxCommandEvent& event)
{
	int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
		rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;

	for (auto cell : wxGrid::GetSelectedBlocks()) {
		for (int col = cell.GetLeftCol(); col <= cell.GetRightCol(); col++) {
			for (int row = cell.GetTopRow(); row <= cell.GetBottomRow(); row++) {
				wxGrid::SetCellBackgroundColour(row, col, m_defaultCellAttr->GetBackgroundColour());
				wxGrid::SetCellFont(row, col, m_defaultCellAttr->GetFont());
				wxGrid::SetCellTextColour(row, col, m_defaultCellAttr->GetTextColour());
				wxGrid::SetCellAlignment(row, col, wxAlignment::wxALIGN_LEFT, wxAlignment::wxALIGN_BOTTOM);
				int num_rows, num_cols;
				if (wxGrid::GetCellSize(row, col, &num_rows, &num_cols) == CellSpan::CellSpan_Main) {
					wxGrid::SetCellSize(row, col, 1, 1);
				}
				else if (wxGrid::GetCellSize(row, col, &num_rows, &num_cols) == CellSpan::CellSpan_Inside) {
					int insideRow = row + num_rows,
						insideCol = col + num_cols;
					wxGrid::GetCellSize(insideRow, insideCol, &num_rows, &num_cols);
					wxGrid::SetCellSize(insideRow, insideCol,
						1,
						1
					);
				}
				else {
					wxGrid::SetCellSize(row, col, 1, 1);
				}

				CGrid::SetCellTextOrientation(row, col, wxOrientation::wxHORIZONTAL);
				CGrid::SetCellBorder(row, col,
					wxPenStyle::wxPENSTYLE_TRANSPARENT,
					wxPenStyle::wxPENSTYLE_TRANSPARENT,
					wxPenStyle::wxPENSTYLE_TRANSPARENT,
					wxPenStyle::wxPENSTYLE_TRANSPARENT,
					*wxBLACK
				);

				wxGrid::SetCellValue(row, col, wxEmptyString);
			}
		}

		int rangeFrom = 0, rangeTo = 0; CSectionCtrl* sectionCtrl = nullptr;
		if (GetWorkSection(rangeFrom, rangeTo, sectionCtrl)) {
			sectionCtrl->Remove(rangeFrom, rangeTo);
		}

		if (cell.GetLeftCol() <= colBrake &&
			cell.GetRightCol() >= colBrake &&
			cell.GetLeftCol() > 0) {
			for (int col = cell.GetRightCol(); col >= cell.GetLeftCol(); col--) {
				wxGrid::SetColSize(col, WXGRID_DEFAULT_ROW_LABEL_WIDTH - 12);
			}
			m_colBrakes.clear();
			m_colBrakes.push_back(cell.GetLeftCol() - 1);
			rowBrake = cell.GetLeftCol();
		}

		if (cell.GetTopRow() <= rowBrake &&
			cell.GetBottomRow() >= rowBrake &&
			cell.GetTopRow() > 0) {
			for (int row = cell.GetBottomRow(); row >= cell.GetTopRow(); row--) {
				wxGrid::SetRowSize(row, WXGRID_MIN_ROW_HEIGHT);
			}
			m_rowBrakes.clear();
			m_rowBrakes.push_back(cell.GetTopRow() - 1);
			colBrake = cell.GetTopRow();
		}

		if (!cell.GetLeftCol() &&
			!cell.GetTopRow()) {
			for (int col = cell.GetRightCol(); col >= 0; col--) {
				wxGrid::SetColSize(col, WXGRID_DEFAULT_ROW_LABEL_WIDTH - 12);
			}
			for (int row = cell.GetBottomRow(); row >= 0; row--) {
				wxGrid::SetRowSize(row, WXGRID_MIN_ROW_HEIGHT);
			}
			m_colBrakes.clear(); m_rowBrakes.clear();
			colBrake = 0; rowBrake = 0;
		}
	}
}

#include "frontend/win/dlgs/rowHeight.h"
#include "frontend/win/dlgs/colHeight.h"

void CGrid::OnRowHeight(wxCommandEvent& event)
{
	CRowHeightWnd *rowHeight = new CRowHeightWnd(this);
	int result = rowHeight->ShowModal();
	if (result == wxID_OK) {
		int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
			rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;
		for (auto cell : wxGrid::GetSelectedBlocks()) {
			for (int row = cell.GetTopRow(); row <= cell.GetBottomRow(); row++) {
				AddBrake(
					colBrake,
					rowBrake > row ? rowBrake : row
				);
				wxGrid::SetRowSize(row, rowHeight->GetHeight());
			}
		}
	}
	rowHeight->Destroy();
}

void CGrid::OnColWidth(wxCommandEvent& event)
{
	CColWidthWnd *colWidth = new CColWidthWnd(this);
	int result = colWidth->ShowModal();
	if (result == wxID_OK) {
		int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
			rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;
		for (auto cell : wxGrid::GetSelectedBlocks()) {
			for (int col = cell.GetLeftCol(); col <= cell.GetRightCol(); col++) {
				AddBrake(
					colBrake > col ? colBrake : col,
					rowBrake
				);
				wxGrid::SetColSize(col, colWidth->GetWidth());
			}
		}
	}
	colWidth->Destroy();
}

void CGrid::OnHideCell(wxCommandEvent& event)
{
	int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
		rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;
	for (auto cell : wxGrid::GetSelectedBlocks()) {
		if (cell.GetLeftCol() > 0 &&
			cell.GetTopRow() == 0) {
			for (int col = cell.GetLeftCol(); col <= cell.GetRightCol(); col++) {
				AddBrake(
					colBrake > col ? colBrake : col,
					rowBrake
				);
				wxGrid::SetColSize(col, 0);
			}
		}
		else if (cell.GetTopRow() > 0 &&
			cell.GetLeftCol() == 0) {
			for (int row = cell.GetTopRow(); row <= cell.GetBottomRow(); row++) {
				AddBrake(
					colBrake,
					rowBrake > row ? rowBrake : row
				);
				wxGrid::SetRowSize(row, 0);
			}
		}
		else if (cell.GetLeftCol() == 0 &&
			cell.GetTopRow() == 0) {
			if (cell.GetBottomRow() == m_numRows - 1) {
				for (int col = cell.GetLeftCol(); col <= cell.GetRightCol(); col++) {
					AddBrake(
						colBrake > col ? colBrake : col,
						rowBrake
					);
					wxGrid::SetColSize(col, 0);
				}
			}
			else if (cell.GetRightCol() == m_numCols - 1) {
				for (int row = cell.GetTopRow(); row <= cell.GetBottomRow(); row++) {
					AddBrake(
						colBrake,
						rowBrake > row ? rowBrake : row
					);
					wxGrid::SetRowSize(row, 0);
				}
			}
		}
	}
}

void CGrid::OnShowCell(wxCommandEvent& event)
{
	int colBrake = m_colBrakes.Count() > 0 ? m_colBrakes[0] : 0,
		rowBrake = m_rowBrakes.Count() > 0 ? m_rowBrakes[0] : 0;
	for (auto cell : wxGrid::GetSelectedBlocks()) {
		if (cell.GetLeftCol() > 0 &&
			cell.GetTopRow() == 0) {
			bool hiddenCol = false;
			for (int col = 0; col < cell.GetLeftCol(); col++) {
				int size = wxGrid::GetColSize(col);
				hiddenCol = size == 0;
			}
			for (int col = hiddenCol ? 0 : cell.GetLeftCol(); col <= cell.GetRightCol(); col++) {
				AddBrake(
					colBrake > col ? colBrake : col,
					rowBrake
				);
				wxGrid::SetColSize(col, wxNOT_FOUND);
			}
		}
		else if (cell.GetTopRow() > 0 &&
			cell.GetLeftCol() == 0) {
			bool hiddenRow = false;
			for (int row = 0; row < cell.GetTopRow(); row++) {
				int size = wxGrid::GetRowSize(row);
				hiddenRow = size == 0;
			}
			for (int row = hiddenRow ? 0 : cell.GetTopRow(); row <= cell.GetBottomRow(); row++) {
				AddBrake(
					colBrake,
					rowBrake > row ? rowBrake : row
				);
				wxGrid::SetRowSize(row, wxNOT_FOUND);
			}
		}
		else if (cell.GetLeftCol() == 0 &&
			cell.GetTopRow() == 0) {
			if (cell.GetBottomRow() == m_numRows - 1) {
				for (int col = cell.GetLeftCol(); col <= cell.GetRightCol(); col++) {
					AddBrake(
						colBrake > col ? colBrake : col,
						rowBrake
					);
					wxGrid::SetColSize(col, wxNOT_FOUND);
				}
			}
			else if (cell.GetRightCol() == m_numCols - 1) {
				for (int row = cell.GetTopRow(); row <= cell.GetBottomRow(); row++) {
					AddBrake(
						colBrake,
						rowBrake > row ? rowBrake : row
					);
					wxGrid::SetRowSize(row, wxNOT_FOUND);
				}
			}
		}
	}
}

void CGrid::OnProperties(wxCommandEvent& event)
{
	m_gridProperty->ShowProperty();
}
#pragma endregion  