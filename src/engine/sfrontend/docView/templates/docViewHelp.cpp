#include "docViewHelp.h"

#include "backend/temp/tempStorage.h"   // ibTempFile — the document's file

// ----------------------------------------------------------------------------
// ibHelpEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibHelpEditView, ibView);

bool ibHelpEditView::OnCreate(ibDocument* doc, long flags)
{
	m_readOnly = flags == ibDOC_READONLY;

	return ibView::OnCreate(doc, flags);
}

void ibHelpEditView::OnDraw(ibDataNode& frame)
{
	const ibHelpDocument* const document = dynamic_cast<const ibHelpDocument*>(GetDocument());
	if (document == nullptr)
		return;

	frame.SetValue(wxT("Text"), document->GetText());
	if (m_readOnly)
		frame.SetValue(wxT("ReadOnly"), true);
	if (document->IsModified())
		frame.SetValue(wxT("Modified"), true);
}

// ----------------------------------------------------------------------------
// ibHelpDocument: ibDocument and its text married
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibHelpDocument, ibDocument);

// ----------------------------------------------------------------------------
// ibHelpFileDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibHelpFileDocument, ibHelpDocument);

bool ibHelpFileDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibDocument::OnCreate(path, flags))
		return false;

	return true;
}

// The text is the document's own, so it saves and loads it itself rather than through Save/LoadObject.
// Its file is the session's temporary one (ibTempFile), where the desktop's was on disk.
bool ibHelpFileDocument::DoSaveDocument(const wxString& filename)
{
	ibTempFile file;
	if (!file.Create(filename, true) || !file.Write(m_text, wxConvUTF8))
		return false;

	return true;
}

bool ibHelpFileDocument::DoOpenDocument(const wxString& filename)
{
	ibTempFile file(filename);
	if (!file.IsOpened() || !file.ReadAll(&m_text, wxConvUTF8))
		return false;

	Modify(false);
	return true;
}
