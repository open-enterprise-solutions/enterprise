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

void CAutoComplete::Append(short type, const wxString& name, const wxString& desc)
{
	if (name.IsEmpty())
		return;

	if (!sCurrentWord.IsEmpty()) {
		wxString nameUpper = name.Upper().Trim(true).Trim(false);
		if (nameUpper.Find(sCurrentWord.Upper().Trim(true).Trim(false)) < 0)
			return;
	}

	const wxBitmap* bitmap = m_visualData->GetImage(type);
	if (bitmap == NULL)
		m_visualData->RegisterImage(type, GetImageByType(type));

	m_aKeywords.push_back(
		{ type, name, desc }
	);
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
	if (!m_aKeywords.size()) {
		Cancel(); return;
	}

	COESListBox* m_listBox = lb->GetListBox();

	std::sort(m_aKeywords.begin(), m_aKeywords.end(),
		[](keywordElement_t a, keywordElement_t b) {
			return a.m_name.CompareTo(b.m_name, wxString::caseCompare::ignoreCase) < 0;
		}
	);

	lb->SetPosition(position);

	for (auto keyword : m_aKeywords)
		m_listBox->Append(keyword.m_name, keyword.m_type);

	if (m_aKeywords.size() == 1) Select(0);
	else if (lb->Show()) m_listBox->Select(0);
}

void CAutoComplete::Cancel()
{
	active = false;
	sCurrentWord = wxEmptyString;
	m_aKeywords.clear();

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

	std::vector< keywordElement_t>::iterator m_selectedKeyword = m_aKeywords.begin() + index;

	if (m_selectedKeyword != m_aKeywords.end())
	{
		wxString sTextComplete = m_selectedKeyword->m_name;
		wxString sShortDescription = m_selectedKeyword->m_shortDescription;

		if (m_selectedKeyword->m_type != eVariable && m_selectedKeyword->m_type != eExportVariable)
		{
			if (sShortDescription.IsEmpty()) {
				sTextComplete += "()";
			}
			else {
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

#include "core/art/artProvider.h"

wxBitmap CAutoComplete::GetImageByType(short type) const
{
	if (type == eProcedure || type == eExportProcedure)
		return wxArtProvider::GetBitmap(wxART_PROCEDURE_RED, wxART_AUTOCOMPLETE);
	else if (type == eFunction || type == eExportFunction)
		return wxArtProvider::GetBitmap(wxART_FUNCTION_RED, wxART_AUTOCOMPLETE);
	else if (type == eVariable || type == eExportVariable)
		return wxArtProvider::GetBitmap(wxART_VARIABLE_ALTERNATIVE, wxART_AUTOCOMPLETE);
	return wxNullBitmap;
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
	COESListBox* listBox = lb->GetListBox();
	int currentRow = listBox->VirtualHitTest(event.GetY());
	if (currentRow != wxNOT_FOUND) {
		std::vector< keywordElement_t>::iterator selectedKeyword = m_aKeywords.begin() + currentRow;

		if (selectedKeyword->m_shortDescription.IsEmpty())
			listBox->SetToolTip(NULL);
		else
			listBox->SetToolTip(selectedKeyword->m_shortDescription);
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
