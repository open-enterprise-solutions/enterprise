#include "docViewComposer.h"

#include "frmclient/backend/query/queryConstructorModel.h"   // ibQueryFieldsKnown — the fields the view said of its text
#include "frmclient/backend/serialize/dataProtocol.h"         // the composition, as the view carries it
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/dlgs/settings/composer/composerSettings.h"

#include <wx/sizer.h>

// ----------------------------------------------------------------------------
// THE PANEL ON A TAB — the desktop's ibComposerEditor, whose document answered the configuration and took the dirty
// bit: here the configuration is the tab's (its fields asked of the server) and an edit is the composition sent.
// ----------------------------------------------------------------------------
class ibComposerTabEditor : public ibComposerSettingsPanel {
public:
	ibComposerTabEditor(wxWindow* parent, ibCompositionDescription& edited, const ibMetaData* metaData,
		const ibTabDocument* document)
		: ibComposerSettingsPanel(parent, edited, metaData), m_edited(edited), m_document(document), m_built(true) {}

	// WHAT THE PANEL HOLDS, TO THE SERVER — at once, not posted: the answer brings the fields of the text it now has
	// (the view's Fields), which the panel reads right after an edit of the query. What the server already has is not
	// sent again: the panel marks itself modified on a page change too, and that is no edit.
	void Send()
	{
		ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
		if (frame == nullptr || m_document == nullptr || IsReadOnly())
			return;
		const ibProtocolNode args = Held();
		const ibString held = args.Write();
		if (held == m_sent)
			return;
		m_sent = held;
		ibProtocolNode params, answer;
		params.SetValue(ibProtocol::ibEvent::Tab, m_document->GetTabId())
			.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(ibProtocol::ibEvent::ibKind::Change))
			.SetValue(ibProtocol::ibEvent::Args, args);
		frame->Call(ibProtocol::ibMethod::Event, params, answer);
	}

	// What the server has — the composition the tab was built of.
	void Remember() { m_sent = Held().Write(); }

protected:
	// THE DIRTY BIT IS THE SERVER'S DOCUMENT'S — the edit is sent, and the configuration is modified there.
	virtual void MarkModified() override
	{
		if (m_built)
			Send();
	}

private:
	ibProtocolNode Held() const
	{
		ibDataNode composition;
		ibCompositionDescriptionMemory::WriteNode(composition, m_edited);
		ibProtocolNode args;
		ibWriteProtocolNode(composition, args.Child(ibProtocol::ibView::ibComposer::Composition));
		return args;
	}

	ibCompositionDescription& m_edited;
	ibString m_sent;   // what the server has, as the wire wrote it
	const ibTabDocument* m_document = nullptr;
	bool m_built = false;
};

// ----------------------------------------------------------------------------
// ibComposerEditView implementation
// ----------------------------------------------------------------------------

bool ibComposerEditView::OnCreate(ibFrontendDocument* docBase, long flags)
{
	if (const ibTabDocument* const document = dynamic_cast<const ibTabDocument*>(docBase))
		m_metaData = document->GetMetaData();
	return ibFrontendView::OnCreate(docBase, flags);
}

void ibComposerEditView::Build(const ibProtocolNode& view)
{
	if (m_composerPanel != nullptr || m_viewFrame == nullptr)
		return;

	ibDataNode composition;
	ibReadProtocolNode(view.FindChild(ibProtocol::ibView::ibComposer::Composition), composition);
	ibCompositionDescriptionMemory::ReadNode(composition, m_composition);

	m_composerPanel = new ibComposerTabEditor(m_viewFrame, m_composition, &m_metaData,
		dynamic_cast<const ibTabDocument*>(GetDocument()));
	m_composerPanel->SetReadOnly(view.FindChild(ibProtocol::ibView::State).GetBool(ibProtocol::ibView::ibComposer::ReadOnly));
	m_composerPanel->Remember();

	wxSizer* sizer = m_viewFrame->GetSizer();
	if (sizer == nullptr) {
		sizer = new wxBoxSizer(wxVERTICAL);
		m_viewFrame->SetSizer(sizer);
	}
	sizer->Add(m_composerPanel, 1, wxEXPAND);
	m_viewFrame->Layout();
}

bool ibComposerEditView::Commit()
{
	if (m_composerPanel == nullptr || m_composerPanel->IsReadOnly())
		return true;
	if (!m_composerPanel->Commit())
		return false;
	m_composerPanel->Send();
	return true;
}

void ibComposerEditView::OnDraw(wxDC* WXUNUSED(dc))
{
	// nothing to do here — the panel draws itself
}

bool ibComposerEditView::OnClose(bool deleteWindow)
{
	// ⚠ THE TAB IS THE SERVER'S: closing it asks there (ibTabDocument::Close, after the panel was accepted), and its
	// window goes with the document when the frame no longer has the tab.
	if (ibFrontendView::OnClose(deleteWindow)) {
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
// ibComposerEditDocument implementation
// ----------------------------------------------------------------------------

ibComposerEditDocument::~ibComposerEditDocument()
{
	ibQueryFieldsForget(GetTabId());
}

void ibComposerEditDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& WXUNUSED(patch))
{
	// The fields the view said, for the text it said them of — read before the panel is built, which reads them.
	ibDataNode node;
	ibReadProtocolNode(view.FindChild(ibProtocol::ibView::ibComposer::Composition), node);
	ibCompositionDescription composition;
	ibCompositionDescriptionMemory::ReadNode(node, composition);
	ibQueryFieldsKnown(GetTabId(), composition.m_query, view);

	if (ibComposerEditView* const composerView = dynamic_cast<ibComposerEditView*>(GetFirstView()))
		composerView->Build(view);
}

bool ibComposerEditDocument::Save()
{
	ibComposerEditView* const view = dynamic_cast<ibComposerEditView*>(GetFirstView());
	if (view != nullptr && !view->Commit())
		return false;
	return ibTabDocument::Save();
}

bool ibComposerEditDocument::Close()
{
	ibComposerEditView* const view = dynamic_cast<ibComposerEditView*>(GetFirstView());
	if (view != nullptr && !view->Commit())
		return false;
	return ibTabDocument::Close();
}

ibFrontendView* ibComposerEditDocument::DoCreateView()
{
	return new ibComposerEditView();
}
