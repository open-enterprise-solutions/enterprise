#include "docViewFile.h"

#include "frmclient/docView/docManager.h"   // full ibFrontendDocTemplate type
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/visualView/visualHostClient.h"   // ibFormVisualDocument — the tab a file opened in

// ----------------------------------------------------------------------------
// ibFrontendFileDocument
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibFrontendFileDocument, ibFrontendDocument);

bool ibFrontendFileDocument::OnCreate(const wxString& WXUNUSED(path), long WXUNUSED(flags))
{
	return true;
}

bool ibFrontendFileDocument::OnNewDocument()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// Its first format: the first the template names.
	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Extension, GetDocumentTemplate()->GetDefaultExtension().BeforeFirst(wxT(';')));
	frame->Call(ibProtocolMethod::Open, params, answer);

	//We must delete document after initialization
	return false;
}

bool ibFrontendFileDocument::DoOpenDocument(const wxString& filename)
{
	// The server opens it: the file goes up, and the server makes the document of it by its id — it shows in a tab of
	// its own.
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	wxString uploaded;
	if (frame == nullptr || !frame->UploadFile(filename, uploaded))
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::File, uploaded);
	// …and the tab it shows in keeps where it came from: Save puts it back there.
	if (frame->Call(ibProtocolMethod::Open, params, answer)) {
		if (ibFormVisualDocument* const opened = frame->FindDocument(frame->GetActiveTab()))
			opened->SetFilename(filename);
	}

	//We must delete document after initialization
	return false;
}
