#include "textEditor.h"

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
}

#include "frmclient/docView/docView.h"

void ibTextEditor::OnTextChange(wxCommandEvent& event)
{
	if (m_document != nullptr)
		m_document->Modify(true);
	
	event.Skip();
}
