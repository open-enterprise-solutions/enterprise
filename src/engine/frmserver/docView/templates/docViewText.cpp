#include "docViewText.h"

#include "backend/temp/tempStorage.h"   // ibTempFile — the document's file
#include "frmserver/visualView/ctrl/textBox.h"   // the text drawn as a textbox's

// ----------------------------------------------------------------------------
// ibTextEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibTextEditView, ibView);

bool ibTextEditView::OnCreate(ibDocument* doc, long flags)
{
	m_readOnly = flags == ibDOC_READONLY;

	return ibView::OnCreate(doc, flags);
}

void ibTextEditView::OnDraw(ibDataNode& frame)
{
	const ibTextDocument* const document = dynamic_cast<const ibTextDocument*>(GetDocument());
	if (document == nullptr)
		return;

	// THE TAB'S VIEW IS A TEXTBOX'S NODE — a client draws it with the one it draws a form's text box with, as a sheet's
	// is a gridbox's. The text it shows, and whether it takes typing; what is typed comes back as Change {Text}.
	frame.SetClsid(g_controlTextBoxCLSID);
	ibDataNode& state = frame.Child(wxT("State"));
	state.SetValue(wxT("Text"), document->GetText());
	if (m_readOnly)
		state.SetValue(wxT("ReadOnly"), true);
}

bool ibTextEditView::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	ibTextDocument* const document = dynamic_cast<ibTextDocument*>(GetDocument());
	if (document == nullptr || event != ibProtocolEvent::Change || m_readOnly)
		return false;

	document->SetText(args.GetValue<wxString>(wxT("Text")));
	return true;
}

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and its text married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibTextDocument, ibDocument);

// ----------------------------------------------------------------------------
// ibTextFileDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibTextFileDocument, ibTextDocument);

bool ibTextFileDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibDocument::OnCreate(path, flags))
		return false;

	return true;
}

// The text is the document's own, so it saves and loads it itself rather than through Save/LoadObject.
// Its file is the session's temporary one (ibTempFile), where the desktop's was on disk.
bool ibTextFileDocument::DoSaveDocument(const wxString& filename)
{
	ibTempFile file;
	if (!file.Create(filename, true) || !file.Write(m_text, wxConvUTF8))
		return false;

	return true;
}

bool ibTextFileDocument::DoOpenDocument(const wxString& filename)
{
	ibTempFile file(filename);
	if (!file.IsOpened() || !file.ReadAll(&m_text, wxConvUTF8))
		return false;

	Modify(false);
	return true;
}
