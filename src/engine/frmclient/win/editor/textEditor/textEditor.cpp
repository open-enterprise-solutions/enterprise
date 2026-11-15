#include "textEditor.h"

#include <wx/fdrepdlg.h>
#include "deskgui/univ/messageDialog.h"

ibTextEditor::ibTextEditor(ibFrontendDocument* document, wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: ibStyledTextCtrl(parent, id, pos, size, style, name), m_document(document)
{
	//Set margin cursor
	for (int margin = 0; margin < ibStyledTextCtrl::GetMarginCount(); margin++)
		ibStyledTextCtrl::SetMarginCursor(margin, ibSTC_CURSORARROW);

	// subscribe to changes in the text control to update the document state
	// when it's modified
	ibStyledTextCtrl::Connect
	(
		ibEVT_STC_CHANGE,
		wxCommandEventHandler(ibTextEditor::OnTextChange),
		nullptr,
		this
	);
	Bind(wxEVT_FIND, &ibTextEditor::OnFind, this);
	Bind(wxEVT_FIND_NEXT, &ibTextEditor::OnFind, this);
}

void ibTextEditor::FindText(const wxString& findString, int wxflags)
{
	int sciflags = 0;
	if ((wxflags & wxFR_WHOLEWORD) != 0)
		sciflags |= ibSTC_FIND_WHOLEWORD;
	if ((wxflags & wxFR_MATCHCASE) != 0)
		sciflags |= ibSTC_FIND_MATCHCASE;
	int result = 0;
	if ((wxflags & wxFR_DOWN) != 0) {
		SetSelectionStart(GetSelectionEnd());
		SearchAnchor();
		result = SearchNext(sciflags, findString);
	}
	else {
		SetSelectionEnd(GetSelectionStart());
		SearchAnchor();
		result = SearchPrev(sciflags, findString);
	}
	if (ibSTC_INVALID_POSITION == result)
		ibMessageBox(ibString::Format(_("\"%s\" not found!"), findString.c_str()), _("Not Found!"), wxICON_ERROR, this);
	else
		EnsureCaretVisible();
}

void ibTextEditor::OnFind(wxFindDialogEvent& event)
{
	FindText(event.GetFindString(), event.GetFlags());
}

#include "frmclient/docView/docView.h"

void ibTextEditor::OnTextChange(wxCommandEvent& event)
{
	if (m_document != nullptr)
		m_document->Modify(true);
	
	event.Skip();
}
