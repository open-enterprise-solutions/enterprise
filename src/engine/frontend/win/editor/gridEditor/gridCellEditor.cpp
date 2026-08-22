#include "gridEditor.h"

#if wxUSE_TEXTCTRL

// ----------------------------------------------------------------------------
// ibGridEditor::ibGridEditorCellTextEditor
// ----------------------------------------------------------------------------

ibGridEditor::ibGridEditorCellTextEditor::ibGridEditorCellTextEditor(const ibGridEditorCellTextEditor& other)
	: ibGridCellEditor(other),
	m_maxChars(other.m_maxChars),
	m_value(other.m_value)
{
#if wxUSE_VALIDATORS
	if (other.m_validator)
	{
		SetValidator(*other.m_validator);
	}
#endif
}

void ibGridEditor::ibGridEditorCellTextEditor::Create(wxWindow* parent,
	wxWindowID id,
	wxEvtHandler* evtHandler)
{
	DoCreate(parent, id, evtHandler);
}

void ibGridEditor::ibGridEditorCellTextEditor::DoCreate(wxWindow* parent,
	wxWindowID id,
	wxEvtHandler* evtHandler,
	long style)
{
	wxTextCtrl* const text = new wxTextCtrl(parent, id, wxEmptyString,
		wxDefaultPosition, wxDefaultSize,
		style | wxTE_MULTILINE | wxTE_NO_VSCROLL | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB | wxNO_BORDER);

	text->SetMargins(0, 0);

	// set max length allowed in the textctrl, if the parameter was set
	if (m_maxChars != 0)
	{
		Text()->SetMaxLength(m_maxChars);
	}

	m_control = text;

	evtHandler->Bind(wxEVT_KEY_DOWN,
		[text](wxKeyEvent& e) {

			static wxMemoryDC dc;

			static int intStringX, intStringY, intLineY;
			static int intSizeX, intSizeY;

			const int insertion = text->GetInsertionPoint();

			switch (e.GetKeyCode())
			{
			case WXK_RETURN:
			case WXK_NUMPAD_ENTER:
				if (e.ShiftDown()) {

					wxString strValue = text->GetValue();
					if (insertion < (int)strValue.size())
						strValue.insert(insertion, wxT('\n'));
					else
						strValue.append(wxT('\n'));

					dc.SetFont(text->GetFont());
					dc.GetMultiLineTextExtent(strValue, &intStringX, &intStringY, &intLineY);

					text->GetSize(&intSizeX, &intSizeY);
					text->SetSize(intSizeX, intStringY > intSizeY ? intStringY : intSizeY);

					text->WriteText(wxT('\n'));
				}
				else e.Skip();
				break;
			default:

				wxString strValue = text->GetValue();
				if (insertion < (int)strValue.size())
					strValue.insert(insertion, e.GetUnicodeKey());
				else
					strValue.append(e.GetUnicodeKey());

				dc.SetFont(text->GetFont());
				dc.GetMultiLineTextExtent(strValue, &intStringX, &intStringY, &intLineY);

				text->GetSize(&intSizeX, &intSizeY);

				if (intStringX + dc.GetCharWidth() > intSizeX)
					text->SetSize(intStringX + dc.GetCharWidth() + 1, intSizeY);

				e.Skip();
				break;
			}
		}
	);

#if wxUSE_VALIDATORS
	// validate text in textctrl, if validator is set
	if (m_validator)
	{
		Text()->SetValidator(*m_validator);
	}
#endif

	ibGridCellEditor::Create(parent, id, evtHandler);
}

void ibGridEditor::ibGridEditorCellTextEditor::SetSize(const wxRect& rectOrig)
{
	wxRect rect(rectOrig);

	// Make the edit control large enough to allow for internal margins
	//
	// TODO: remove this if the text ctrl sizing is improved
	//
#if defined(__WXMSW__)

	rect.x--;
	rect.y--;

#elif !defined(__WXGTK__)
	int extra_x = 2;
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

	ibGridCellEditor::SetSize(rect);
}

void ibGridEditor::ibGridEditorCellTextEditor::BeginEdit(int row, int col, ibGrid* grid)
{
	wxASSERT_MSG(m_control, wxT("The ibGridCellEditor must be created first!"));

	m_value = grid->GetTable()->GetValue(row, col);

	DoBeginEdit(m_value);
}

void ibGridEditor::ibGridEditorCellTextEditor::DoBeginEdit(const wxString& startValue)
{
	Text()->SetValue(startValue);
	Text()->SetInsertionPointEnd();
	Text()->SelectAll();
	Text()->SetFocus();
}

bool ibGridEditor::ibGridEditorCellTextEditor::EndEdit(int WXUNUSED(row),
	int WXUNUSED(col),
	const ibGrid* WXUNUSED(grid),
	const wxString& WXUNUSED(oldval),
	wxString* newval)
{
	wxCHECK_MSG(m_control, false,
		"ibGridEditor::ibGridEditorCellTextEditor must be created first!");

	const wxString value = Text()->GetValue();
	if (value == m_value)
		return false;

	m_value = value;

	if (newval)
		*newval = m_value;

	return true;
}

void ibGridEditor::ibGridEditorCellTextEditor::ApplyEdit(int row, int col, ibGrid* grid)
{
	if (!m_value.IsEmpty()) {

		wxCoord height = 0;

		static wxMemoryDC dc;
		dc.SetFont(grid->GetCellFont(row, col, grid->GetGridZoom()));
		dc.GetMultiLineTextExtent(m_value, NULL, &height);

		int cell_rows, cell_cols;
		CellSpan span = grid->GetCellSize(row, col, &cell_rows, &cell_cols);

		if (span == CellSpan::CellSpan_Main) {
			int rowSize = 0;
			for (int i = row; i < row + cell_rows; i++)
				rowSize += grid->GetRowSize(i, grid->GetGridZoom());
			if (height > rowSize){
				int rowOldSize = grid->GetRowSize(row, grid->GetGridZoom()); 
				grid->SetRowSize(row, height - (rowSize - rowOldSize) + 2, grid->GetGridZoom());
			}
		}
		else if (span == CellSpan::CellSpan_None) {
			if (height > grid->GetRowSize(row, grid->GetGridZoom()))
				grid->SetRowSize(row, height + 2, grid->GetGridZoom());
		}
	}

	grid->SetCellValue(row, col, m_value);

	m_value.clear();
}

void ibGridEditor::ibGridEditorCellTextEditor::Reset()
{
	wxASSERT_MSG(m_control, "ibGridEditor::ibGridEditorCellTextEditor must be created first!");

	DoReset(m_value);
}

void ibGridEditor::ibGridEditorCellTextEditor::DoReset(const wxString& startValue)
{
	Text()->SetValue(startValue);
	Text()->SetInsertionPointEnd();
}

bool ibGridEditor::ibGridEditorCellTextEditor::IsAcceptedKey(wxKeyEvent& event)
{
	switch (event.GetKeyCode())
	{
	case WXK_BACK:
		return true;

	default:
		return ibGridCellEditor::IsAcceptedKey(event);
	}
}

void ibGridEditor::ibGridEditorCellTextEditor::StartingKey(wxKeyEvent& event)
{
	// Since this is now happening in the EVT_CHAR event EmulateKeyPress is no
	// longer an appropriate way to get the character into the text control.
	// Do it ourselves instead.  We know that if we get this far that we have
	// a valid character, so not a whole lot of testing needs to be done.

	wxTextCtrl* tc = Text();
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

void ibGridEditor::ibGridEditorCellTextEditor::HandleReturn(wxKeyEvent& event)
{
#if defined(__WXMOTIF__) || defined(__WXGTK__)
	// wxMotif needs a little extra help...
	size_t pos = (size_t)(Text()->GetInsertionPoint());
	wxString s(Text()->GetValue());
	s = s.Left(pos) + wxT("\n") + s.Mid(pos);
	Text()->SetValue(s);
	Text()->SetInsertionPoint(pos);
#else
	// the other ports can handle a Return key press
	//
	event.Skip();
#endif
}

void ibGridEditor::ibGridEditorCellTextEditor::SetParameters(const wxString& params)
{
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
			ibJournalInfo(wxT("ui"), wxT("Invalid ibGridEditor::ibGridEditorCellTextEditor parameter string '%s' ignored"), params);
		}
	}
}

#if wxUSE_VALIDATORS
void ibGridEditor::ibGridEditorCellTextEditor::SetValidator(const wxValidator& validator)
{
	m_validator.reset(static_cast<wxValidator*>(validator.Clone()));
	if (m_validator && IsCreated())
		Text()->SetValidator(*m_validator);
}
#endif

// return the value in the text control
wxString ibGridEditor::ibGridEditorCellTextEditor::GetValue() const
{
	return Text()->GetValue();
}

#endif // wxUSE_TEXTCTRL
