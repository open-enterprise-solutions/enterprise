#include "docViewText.h"

#include "backend/temp/tempStorage.h"   // ibTempFile — the document's file

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

	frame.SetValue(wxT("Text"), document->GetText());
	if (m_readOnly)
		frame.SetValue(wxT("ReadOnly"), true);
	if (document->IsModified())
		frame.SetValue(wxT("Modified"), true);
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
