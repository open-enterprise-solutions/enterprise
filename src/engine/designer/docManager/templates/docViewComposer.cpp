#include "docViewComposer.h"

#include "backend/metaCollection/metaComposerObject.h"
#include "frontend/win/dlgs/settings/composer/composerSettings.h"

// ----------------------------------------------------------------------------
// ibComposerEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComposerEditView, ibMetaView);

wxBEGIN_EVENT_TABLE(ibComposerEditView, ibMetaView)
wxEND_EVENT_TABLE()

bool ibComposerEditView::OnCreate(ibDocument* docBase, long flags)
{
	ibMetaDocument* doc = GetDocument();

	// THE PANEL IS THE EDITOR. A composer declares a composition, and a composition is edited by the
	// platform's own settings panel — the same one the gridbox opens modally. Nothing here knows what
	// a filter or a level is; it only decides WHERE the panel lives.
	auto* metaComposer = dynamic_cast<ibValueMetaObjectComposer*>(doc->GetMetaObject());
	ibValueDataComposition* composition = metaComposer != nullptr ? metaComposer->GetComposition() : nullptr;

	m_composerPanel = new ibComposerSettingsPanel(m_viewFrame, composition);
	// THE HOUSE CALL, the one every other designer editor makes (role, interface, module, visual,
	// help). Enable(false) on the whole panel was the shape here, and it greys the very text a
	// person opened the tab to READ — view-only means the verbs are closed, not that the window
	// goes dim (Max, 2026-08-20: "open the tabs in view mode so they cannot be changed afterwards —
	// no copying, no adding, only looking").
	m_composerPanel->SetReadOnly(flags == ibDOC_READONLY);

	return ibView::OnCreate(docBase, flags);
}

void ibComposerEditView::OnUpdate(ibView* sender, wxObject* hint)
{
}

void ibComposerEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the panel draws itself
}

bool ibComposerEditView::OnClose(bool deleteWindow)
{
	// ⭐ CLOSING THE TAB IS ACCEPTING IT. The embedded filter / sort live in a transactional buffer
	// until Commit, so a tab that closed without it would lose exactly the settings the person came
	// here to write. A panel that objects (a half-written condition, an expression that does not
	// compile and was not confirmed) keeps the tab open on what it objected to.
	if (m_composerPanel != nullptr && !m_composerPanel->Commit())
		return false;

	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}

	if (ibMetaView::OnClose(deleteWindow)) {

		if (m_composerPanel != nullptr) {
			m_composerPanel->Freeze();
			m_composerPanel->Destroy();
			m_composerPanel = nullptr;
		}

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------------
// ibComposerDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_CLASS(ibComposerDocument, ibMetaDocument);

bool ibComposerDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

bool ibComposerDocument::DoSaveDocument(const wxString& filename)
{
	return true;
}

bool ibComposerDocument::DoOpenDocument(const wxString& filename)
{
	return true;
}

bool ibComposerDocument::IsModified() const
{
	return ibMetaDocument::IsModified();
}

void ibComposerDocument::Modify(bool modified)
{
	ibMetaDocument::Modify(modified);
}

// ----------------------------------------------------------------------------
// ibComposerEditDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComposerEditDocument, ibComposerDocument);
