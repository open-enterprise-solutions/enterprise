#include "textEditor.h"

ibTextEditor::ibTextEditor(ibDocument* document, wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxStyledTextCtrl(parent, id, pos, size, style, name), m_document(document)
{
	//Set margin cursor
	for (int margin = 0; margin < wxStyledTextCtrl::GetMarginCount(); margin++)
		wxStyledTextCtrl::SetMarginCursor(margin, wxSTC_CURSORARROW);

	// subscribe to changes in the text control to update the document state
	// when it's modified
	wxStyledTextCtrl::Connect
	(
		wxEVT_STC_CHANGE,
		wxCommandEventHandler(ibTextEditor::OnTextChange),
		nullptr,
		this
	);
}

#include "frontend/docView/docView.h"

void ibTextEditor::OnTextChange(wxCommandEvent& event)
{
	if (m_document != nullptr)
		m_document->Modify(true);
	
	event.Skip();
}
