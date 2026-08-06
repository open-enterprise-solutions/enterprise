#include "docViewCommonAttribute.h"

// ----------------------------------------------------------------------------
// ibCommonAttributeEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibCommonAttributeEditView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibCommonAttributeEditView, ibMetaView)
wxEND_EVENT_TABLE()

#include "win/editor/commonAttributeEditor/commonAttributeEditor.h"

bool ibCommonAttributeEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();
	m_compositionEditor = new ibCommonAttributeCompositionEditor(m_viewFrame, wxID_ANY, doc->GetMetaObject());
	m_compositionEditor->SetReadOnly(flags == ibDOC_READONLY);

	m_compositionEditor->RefreshComposition();
	return ibView::OnCreate(docBase, flags);
}

void ibCommonAttributeEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_compositionEditor != nullptr)
		m_compositionEditor->RefreshComposition();
}

void ibCommonAttributeEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the tree draws itself
}

bool ibCommonAttributeEditView::OnClose(bool deleteWindow)
{
	Activate(false);

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {

		m_compositionEditor->Freeze();

		m_compositionEditor->Destroy();
		m_compositionEditor = nullptr;

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibCommonAttributeDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibCommonAttributeDocument, ibMetaDocument);

bool ibCommonAttributeDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;

	return true;
}

// The composition lives in the metadata, not in a file of its own — the metaobject is
// saved with the configuration, so there is nothing to write here.
bool ibCommonAttributeDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool ibCommonAttributeDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool ibCommonAttributeDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibCommonAttributeDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// ibCommonAttributeEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibCommonAttributeEditDocument, ibMetaDocument);
