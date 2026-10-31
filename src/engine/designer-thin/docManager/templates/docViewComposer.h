#ifndef _COMPOSER_DOC_H__
#define _COMPOSER_DOC_H__

// A COMPOSER OPENS ON A TAB OF ITS OWN, like a form or a template — THE DESKTOP'S
// (designer/docManager/templates/docViewComposer.h) IN A TAB THE SERVER RAISED (Kind Composer): the document is the
// server's (frmserver ibComposerCompositionDocument). What it declares — the query, the resources, the parameters, the
// output structure — is edited by the same panel a composition on a form is edited by (ibComposerSettingsPanel), over
// the composition the tab's view brought; what the panel holds goes back whole (Change {Composition}), on each edit and
// when the tab is saved or closed — accepting the tab is Commit() on the panel, as the desktop's.

#include "frmclient/docView/tabDocument.h"
#include "core/description/compositionDescription.h"   // the description the panel edits

class ibComposerEditView : public ibFrontendView {
	class ibComposerTabEditor* m_composerPanel = nullptr;
	ibCompositionDescription m_composition;   // what the panel edits in place — the view's, built once
	ibMetaData m_metaData;                    // the tab's configuration: the fields of a text are asked of it
public:
	ibComposerEditView() : ibFrontendView() {}

	// The panel over the composition the view brought, built on its first draw; read-only as the view's State says.
	void Build(const ibProtocolNode& view);
	// What is on screen onto the composition, and the composition to the server. False: the panel objected and the tab
	// stays open on it.
	bool Commit();

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibComposerTabEditor* GetComposerPanel() const { return m_composerPanel; }
};

class ibComposerEditDocument : public ibTabDocument {
public:
	explicit ibComposerEditDocument(long long tabId) : ibTabDocument(tabId) {}
	virtual ~ibComposerEditDocument();

	// THE VIEW — the panel built on the first draw; the fields of its query kept for the panel's read of that text
	// (ibQueryFieldsKnown), on every draw. What the panel holds is not drawn over: it is the panel's, and the server's
	// copy is what it was sent.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	// Accepting the tab — the panel committed and sent before the server saves or closes it.
	virtual bool Save() override;
	virtual bool Close() override;

protected:
	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibComposerEditDocument);
};

#endif
