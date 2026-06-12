#include "autoComplete.h"
#include "frontend/mainFrame/mainFrame.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

ibAutoComplete::ibAutoComplete(wxStyledTextCtrl* textCtrl)
	: m_visualData(new ibListBoxVisualData(5)),
	  m_owner(textCtrl)
{
}

ibAutoComplete::~ibAutoComplete()
{
	if (m_listBoxWin)
	{
		m_listBoxWin->GetListBox()->Destroy();
		m_listBoxWin->Destroy();
	}

	delete m_visualData;
}

bool ibAutoComplete::Active() const
{
	return m_active;
}

void ibAutoComplete::Start(const wxString& currentWord,
	int position, int startLen,
	int lineHeight)
{
	if (m_active) Cancel();

	m_listBoxWin = new ibCodeEditorListBoxWin(m_owner, m_visualData, m_owner->TextHeight(m_owner->GetCurrentLine()));

	// Scale the dropdown box together with the editor's zoom so the
	// listbox stays proportional to the font — at default zoom the box
	// is 225x200; zooming in/out grows or shrinks it by the same factor
	// the font does (clamped against a 1pt floor).
	const int basePts = std::max(1, m_owner->StyleGetFont(wxSTC_STYLE_DEFAULT).GetPointSize());
	const int actualPts = std::max(1, basePts + m_owner->GetZoom());
	const double scale = double(actualPts) / double(basePts);
	m_listBoxWin->SetSize(int(225 * scale), int(200 * scale));

	m_active = true;
	m_startLen = startLen;
	m_posStart = position;

	m_currentWord = currentWord;

	ibCodeEditorListBox* listBox = m_listBoxWin->GetListBox();

	// Match the editor's currently displayed font size — STC stores the
	// base font in StyleGetFont and renders it scaled by GetZoom(). Without
	// applying the zoom delta, the dropdown stays at the unzoomed size and
	// looks tiny next to a zoomed-in editor (or oversized when zoomed out).
	wxFont font = m_owner->StyleGetFont(wxSTC_STYLE_DEFAULT);
	font.SetPointSize(std::max(1, font.GetPointSize() + m_owner->GetZoom()));
	listBox->SetListBoxFont(font);

	listBox->Bind(wxEVT_LISTBOX, &ibAutoComplete::OnSelection, this);
	listBox->Bind(wxEVT_LISTBOX_DCLICK, &ibAutoComplete::OnSelection, this);

	listBox->Bind(wxEVT_MOTION, &ibAutoComplete::OnMouseMotion, this);

	m_evtHandler = new wxEvtHandler;
	m_evtHandler->Bind(wxEVT_KEY_DOWN, &ibAutoComplete::OnKeyDown, this);

	//focus kill/set
	m_evtHandler->Bind(wxEVT_SET_FOCUS, &ibAutoComplete::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_KILL_FOCUS, &ibAutoComplete::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_CHILD_FOCUS, &ibAutoComplete::OnProcessChildFocus, this);

	//on sizing
	m_evtHandler->Bind(wxEVT_SIZE, &ibAutoComplete::OnProcessSize, this);
	m_evtHandler->Bind(wxEVT_SIZING, &ibAutoComplete::OnProcessSize, this);

	//on mouse event
	m_evtHandler->Bind(wxEVT_LEFT_DCLICK, &ibAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_DCLICK, &ibAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_DCLICK, &ibAutoComplete::OnProcessMouse, this);

	m_evtHandler->Bind(wxEVT_LEFT_UP, &ibAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_UP, &ibAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_UP, &ibAutoComplete::OnProcessMouse, this);
}

void ibAutoComplete::Append(short type, const wxString& strName, const wxString& desc)
{
	if (strName.IsEmpty())
		return;

	// Filter only against the visible portion of the typed word — strip
	// control / whitespace chars (`\r`, `\n`, `\t`, spaces). Without the
	// trim a stray newline / CR that PrepareExpression leaves in
	// m_currentWord when the caret is on a blank line slips past
	// IsEmpty() (length > 0), the Upper'd Find sees no match against
	// real identifiers, and the dropdown ends up empty.
	wxString filterWord = m_currentWord;
	filterWord.Trim(true).Trim(false);

	if (!filterWord.IsEmpty()) {
		const wxString& nameUpper = stringUtils::MakeUpper(strName);
		if (nameUpper.Find(stringUtils::MakeUpper(filterWord)) < 0)
			return;
	}

	const wxBitmap* bitmap = m_visualData->GetImage(type);
	if (bitmap == nullptr)
		m_visualData->RegisterImage(type, GetImageByType(type));

	m_aKeywords.push_back(
		{ type, strName, desc }
	);
}

int ibAutoComplete::GetSelection() const
{
	return m_listBoxWin->GetListBox()->GetSelection();
}

wxString ibAutoComplete::GetValue(int item) const
{
	return m_listBoxWin->GetListBox()->GetValue(item);
}

void ibAutoComplete::Show(const wxPoint& position)
{
	if (!m_active) return;
	if (!m_aKeywords.size()) {
		Cancel(); return;
	}

	ibCodeEditorListBox* listBox = m_listBoxWin->GetListBox();

	std::sort(m_aKeywords.begin(), m_aKeywords.end(),
		[](ibKeywordElement a, ibKeywordElement b) {
			return a.m_name.CompareTo(b.m_name, wxString::caseCompare::ignoreCase) < 0;
		}
	);

	m_listBoxWin->SetPosition(position);

	for (auto keyword : m_aKeywords)
		listBox->Append(keyword.m_name, keyword.m_type);

	if (m_aKeywords.size() == 1) Select(0);
	else if (m_listBoxWin->Show()) listBox->Select(0);
}

void ibAutoComplete::Cancel()
{
	m_active = false;
	m_currentWord = wxEmptyString;
	m_aKeywords.clear();

	if (m_listBoxWin) {
		m_listBoxWin->GetListBox()->Clear();
		m_listBoxWin->GetListBox()->Destroy();
		wxDELETE(m_listBoxWin);
	}
	wxDELETE(m_evtHandler);
}

void ibAutoComplete::MoveUp()
{
	int count = m_listBoxWin->GetListBox()->Length();
	int current = m_listBoxWin->GetListBox()->GetSelection();
	current -= 1;

	if (current >= count)
		current = count - 1;

	if (current < 0)
		current = 0;

	m_listBoxWin->GetListBox()->Select(current);
}

void ibAutoComplete::MoveDown()
{
	int count = m_listBoxWin->GetListBox()->Length();
	int current = m_listBoxWin->GetListBox()->GetSelection();
	current += 1;

	if (current >= count)
		current = count - 1;

	if (current < 0)
		current = 0;

	m_listBoxWin->GetListBox()->Select(current);
}

#include "../codeEditor.h"

void ibAutoComplete::Select(int index)
{
	wxString sDescription;
	bool needCallTip = false;

	std::vector<ibKeywordElement>::iterator selectedKeyword = m_aKeywords.begin() + index;

	if (selectedKeyword != m_aKeywords.end())
	{
		wxString textComplete = selectedKeyword->m_name;
		wxString shortDescription = selectedKeyword->m_shortDescription;

		if (selectedKeyword->m_type != eVariable && selectedKeyword->m_type != eExportVariable)
		{
			if (shortDescription.IsEmpty()) {
				textComplete += "()";
			}
			else {
				textComplete += "(";
				if (shortDescription.Find("()") > 0) textComplete += ")";
				else needCallTip = true;
			}
		}

		m_owner->Replace(m_posStart - m_startLen, m_posStart, textComplete);
		m_owner->SetEmptySelection(m_posStart - m_startLen + textComplete.ToUTF8().length());

		sDescription = shortDescription;
	}

	Cancel();

	if (needCallTip)
	{
		ibCodeEditor* autoComplete = dynamic_cast<ibCodeEditor*>(m_owner);
		if (autoComplete) autoComplete->ShowCallTip(sDescription);
	}
}

bool ibAutoComplete::CallEvent(wxEvent& event)
{
	if (!m_active) return false;
	bool result = m_evtHandler->ProcessEvent(event);
	if (m_evtHandler) return result;

	if (event.GetEventType() == wxEVT_KEY_DOWN)
	{
		return ((wxKeyEvent&)event).GetKeyCode() == WXK_RETURN ||
			((wxKeyEvent&)event).GetKeyCode() == WXK_NUMPAD_ENTER;
	}

	return false;
}

#include "frontend/artProvider/artProvider.h"

wxBitmap ibAutoComplete::GetImageByType(short type) const
{
	if (type == eProcedure || type == eExportProcedure)
		return wxArtProvider::GetBitmapBundle(wxART_PROCEDURE_RED, wxART_AUTOCOMPLETE).GetBitmap(wxDefaultSize);
	else if (type == eFunction || type == eExportFunction)
		return wxArtProvider::GetBitmapBundle(wxART_FUNCTION_RED, wxART_AUTOCOMPLETE).GetBitmap(wxDefaultSize);
	else if (type == eVariable || type == eExportVariable)
		return wxArtProvider::GetBitmapBundle(wxART_VARIABLE_ALTERNATIVE, wxART_AUTOCOMPLETE).GetBitmap(wxDefaultSize);
	return wxNullBitmap;
}

void ibAutoComplete::OnSelection(wxCommandEvent& event)
{
	Select(event.GetSelection());
}

void ibAutoComplete::OnKeyDown(wxKeyEvent& event)
{
	ibCodeEditorListBox* listBox = m_listBoxWin->GetListBox();

	switch (event.GetKeyCode())
	{
	case WXK_UP: MoveUp(); break;
	case WXK_DOWN: MoveDown(); break;
	case WXK_NUMPAD_ENTER:
	case WXK_RETURN:
		Select(listBox->GetSelection());
		break;
	default: Cancel(); break;
	}
}

void ibAutoComplete::OnMouseMotion(wxMouseEvent& event)
{
	ibCodeEditorListBox* listBox = m_listBoxWin->GetListBox();
	int currentRow = listBox->VirtualHitTest(event.GetY());
	if (currentRow != wxNOT_FOUND) {
		std::vector<ibKeywordElement>::iterator selectedKeyword = m_aKeywords.begin() + currentRow;

		if (selectedKeyword->m_shortDescription.IsEmpty())
			listBox->SetToolTip(nullptr);
		else
			listBox->SetToolTip(selectedKeyword->m_shortDescription);
	}

	event.Skip();
}

void ibAutoComplete::OnProcessFocus(wxFocusEvent& event)
{
	Cancel();
}

void ibAutoComplete::OnProcessChildFocus(wxChildFocusEvent& event)
{
	Cancel();
}

void ibAutoComplete::OnProcessSize(wxSizeEvent& event)
{
	Cancel();
}

void ibAutoComplete::OnProcessMouse(wxMouseEvent& event)
{
	Cancel();
}
