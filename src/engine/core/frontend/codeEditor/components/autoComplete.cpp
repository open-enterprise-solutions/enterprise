#include "autoComplete.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

extern wxImageList* GetImageList();

CAutoComplete::CAutoComplete(wxStyledTextCtrl* textCtrl) :
	active(false),
	lb(NULL),
	m_visualData(new ÑListBoxVisualData(5)),
	m_evtHandler(NULL),
	m_owner(textCtrl),
	posStart(0),
	startLen(0),
	cancelAtStartPos(true),
	dropRestOfWord(false)
{
}

CAutoComplete::~CAutoComplete()
{
	if (lb)
	{
		lb->GetListBox()->Destroy();
		lb->Destroy();
	}
}

bool CAutoComplete::Active() const
{
	return active;
}

void CAutoComplete::Start(const wxString& sCurWord,
	int position, int startLen_,
	int lineHeight)
{
	if (active) Cancel();

	lb = new COESListBoxWin(m_owner, m_visualData, m_owner->TextHeight(m_owner->GetCurrentLine()));
	lb->SetSize(225, 200);

	active = true;
	startLen = startLen_;
	posStart = position;

	sCurrentWord = sCurWord;

	COESListBox* m_listBox = lb->GetListBox();
	m_listBox->SetListBoxFont(m_owner->StyleGetFont(wxSTC_STYLE_DEFAULT));

	m_listBox->Bind(wxEVT_LISTBOX, &CAutoComplete::OnSelection, this);
	m_listBox->Bind(wxEVT_LISTBOX_DCLICK, &CAutoComplete::OnSelection, this);

	m_listBox->Bind(wxEVT_MOTION, &CAutoComplete::OnMouseMotion, this);

	m_evtHandler = new wxEvtHandler;
	m_evtHandler->Bind(wxEVT_KEY_DOWN, &CAutoComplete::OnKeyDown, this);

	//focus kill/set
	m_evtHandler->Bind(wxEVT_SET_FOCUS, &CAutoComplete::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_KILL_FOCUS, &CAutoComplete::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_CHILD_FOCUS, &CAutoComplete::OnProcessChildFocus, this);

	//on sizing 
	m_evtHandler->Bind(wxEVT_SIZE, &CAutoComplete::OnProcessSize, this);
	m_evtHandler->Bind(wxEVT_SIZING, &CAutoComplete::OnProcessSize, this);

	//on mouse event
	m_evtHandler->Bind(wxEVT_LEFT_DCLICK, &CAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_DCLICK, &CAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_DCLICK, &CAutoComplete::OnProcessMouse, this);

	m_evtHandler->Bind(wxEVT_LEFT_UP, &CAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_UP, &CAutoComplete::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_UP, &CAutoComplete::OnProcessMouse, this);
}

void CAutoComplete::Append(short type, const wxString& sName, const wxString& sDescription, int image)
{
	if (sName.IsEmpty())
		return;

	if (!sCurrentWord.IsEmpty()) {
		wxString sNameUpper = sName.Upper().Trim(true).Trim(false);
		if (sNameUpper.Find(sCurrentWord.Upper().Trim(true).Trim(false)) < 0)
			return;
	}

	if (image != wxNOT_FOUND) {
		const wxBitmap* m_bmp = m_visualData->GetImage(image);
		if (!m_bmp) m_visualData->RegisterImage(image, GetImageList()->GetBitmap(image));
	}

	aKeywords.push_back({ type, sName, sDescription, image });
}

int CAutoComplete::GetSelection() const
{
	return lb->GetListBox()->GetSelection();
}

wxString CAutoComplete::GetValue(int item) const
{
	return lb->GetListBox()->GetValue(item);
}

void CAutoComplete::Show(const wxPoint& position)
{
	if (!active) return;
	if (!aKeywords.size()) { Cancel(); return; }

	COESListBox* m_listBox = lb->GetListBox();

	std::sort(aKeywords.begin(), aKeywords.end(),
		[](keywordElement_t a, keywordElement_t b)
		{
			return a.name.CompareTo(b.name, wxString::caseCompare::ignoreCase) < 0;
		});

	lb->SetPosition(position);

	for (auto keyword : aKeywords) m_listBox->Append(keyword.name, keyword.image);

	if (aKeywords.size() == 1) Select(0);
	else if (lb->Show()) m_listBox->Select(0);
}

void CAutoComplete::Cancel()
{
	active = false;
	sCurrentWord = wxEmptyString;
	aKeywords.clear();

	if (lb) {

		lb->GetListBox()->Clear();
		lb->GetListBox()->Destroy();

		wxDELETE(lb);
	}

	wxDELETE(m_evtHandler);
}

void CAutoComplete::MoveUp()
{
	int count = lb->GetListBox()->Length();
	int current = lb->GetListBox()->GetSelection();
	current -= 1;

	if (current >= count)
		current = count - 1;

	if (current < 0)
		current = 0;

	lb->GetListBox()->Select(current);
}

void CAutoComplete::MoveDown()
{
	int count = lb->GetListBox()->Length();
	int current = lb->GetListBox()->GetSelection();
	current += 1;

	if (current >= count)
		current = count - 1;

	if (current < 0)
		current = 0;

	lb->GetListBox()->Select(current);
}

#include "../codeEditorCtrl.h"

void CAutoComplete::Select(int index)
{
	wxString sDescription; bool m_bNeedCallTip = false;

	std::vector< keywordElement_t>::iterator m_selectedKeyword = aKeywords.begin() + index;

	if (m_selectedKeyword != aKeywords.end())
	{
		wxString sTextComplete = m_selectedKeyword->name;
		wxString sShortDescription = m_selectedKeyword->shortDescription;

		if (m_selectedKeyword->type != eVariable && m_selectedKeyword->type != eExportVariable)
		{
			if (sShortDescription.IsEmpty())
			{
				sTextComplete += "()";
			}
			else
			{
				sTextComplete += "(";
				if (sShortDescription.Find("()") > 0) sTextComplete += ")";
				else m_bNeedCallTip = true;
			}
		}

		m_owner->Replace(posStart - startLen, posStart, sTextComplete);
		m_owner->SetEmptySelection(posStart - startLen + sTextComplete.ToUTF8().length());

		sDescription = sShortDescription;
	}

	Cancel();

	if (m_bNeedCallTip)
	{
		CCodeEditorCtrl* m_autoComplete = dynamic_cast<CCodeEditorCtrl*>(m_owner);
		if (m_autoComplete) m_autoComplete->ShowCallTip(sDescription);
	}
}

bool CAutoComplete::CallEvent(wxEvent& event)
{
	if (!active) return false;
	bool result = m_evtHandler->ProcessEvent(event);
	if (m_evtHandler) return result;

	if (event.GetEventType() == wxEVT_KEY_DOWN)
	{
		return ((wxKeyEvent&)event).GetKeyCode() == WXK_RETURN ||
			((wxKeyEvent&)event).GetKeyCode() == WXK_NUMPAD_ENTER;
	}

	return false;
}

void CAutoComplete::OnSelection(wxCommandEvent& event)
{
	Select(event.GetSelection());
}

void CAutoComplete::OnKeyDown(wxKeyEvent& event)
{
	COESListBox* m_listBox = lb->GetListBox();

	switch (event.GetKeyCode())
	{
	case WXK_UP: MoveUp(); break;
	case WXK_DOWN: MoveDown(); break;
	case WXK_NUMPAD_ENTER:
	case WXK_RETURN:
		Select(m_listBox->GetSelection());
		break;
	default: Cancel(); break;
	}
}

void CAutoComplete::OnMouseMotion(wxMouseEvent& event)
{
	COESListBox* m_listBox = lb->GetListBox();
	int m_currentRow = m_listBox->VirtualHitTest(event.GetY());
	if (m_currentRow != wxNOT_FOUND)
	{
		std::vector< keywordElement_t>::iterator m_selectedKeyword = aKeywords.begin() + m_currentRow;

		if (m_selectedKeyword->shortDescription.IsEmpty()) m_listBox->SetToolTip(NULL);
		else m_listBox->SetToolTip(m_selectedKeyword->shortDescription);
	}

	event.Skip();
}

void CAutoComplete::OnProcessFocus(wxFocusEvent& event)
{
	Cancel();
}

void CAutoComplete::OnProcessChildFocus(wxChildFocusEvent& event)
{
	Cancel();
}

void CAutoComplete::OnProcessSize(wxSizeEvent& event)
{
	Cancel();
}

void CAutoComplete::OnProcessMouse(wxMouseEvent& event)
{
	Cancel();
}
