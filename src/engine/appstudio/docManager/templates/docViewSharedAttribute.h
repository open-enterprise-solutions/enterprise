#ifndef _SHARED_ATTRIBUTE_DOC_H__
#define _SHARED_ATTRIBUTE_DOC_H__

// The document/view pair behind a SHARED ATTRIBUTE — opening one shows its
// COMPOSITION, the objects that carry it. Mirrors the section pair next door
// (docViewInterface.h), because it is the same gesture: click the thing, check the
// objects it applies to. THE DESKTOP'S (designer/docManager/templates/docViewSharedAttribute.h) IN A TAB THE SERVER
// RAISED (Kind SharedAttribute): the document is the server's (frmserver ibSharedAttributeCompositionDocument).

#include "frmclient/docView/tabDocument.h"

class ibSharedAttributeEditView : public ibFrontendView {
	class ibSharedAttributeCompositionEditor* m_compositionEditor = nullptr;
public:

	ibSharedAttributeEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibSharedAttributeCompositionEditor* GetCompositionEditor() const { return m_compositionEditor; }
};

class ibSharedAttributeEditDocument : public ibTabDocument {
public:

	explicit ibSharedAttributeEditDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE EDITOR'S ROWS — the view's Rows and State, drawn whole whenever the answer changed them.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibSharedAttributeCompositionEditor* GetCompositionEditor() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibSharedAttributeEditDocument);
};

#endif
