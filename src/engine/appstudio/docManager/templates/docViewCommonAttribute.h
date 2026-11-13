#ifndef _COMMON_ATTRIBUTE_DOC_H__
#define _COMMON_ATTRIBUTE_DOC_H__

// The document/view pair behind a COMMON ATTRIBUTE — opening one shows its
// COMPOSITION, the objects that carry it. Mirrors the section pair next door
// (docViewInterface.h), because it is the same gesture: click the thing, check the
// objects it applies to. THE DESKTOP'S (designer/docManager/templates/docViewCommonAttribute.h) IN A TAB THE SERVER
// RAISED (Kind CommonAttribute): the document is the server's (frmserver ibCommonAttributeCompositionDocument).

#include "frmclient/docView/tabDocument.h"

class ibCommonAttributeEditView : public ibFrontendView {
	class ibCommonAttributeCompositionEditor* m_compositionEditor = nullptr;
public:

	ibCommonAttributeEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibCommonAttributeCompositionEditor* GetCompositionEditor() const { return m_compositionEditor; }
};

class ibCommonAttributeEditDocument : public ibTabDocument {
public:

	explicit ibCommonAttributeEditDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE EDITOR'S ROWS — the view's Rows and State, drawn whole whenever the answer changed them.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibCommonAttributeCompositionEditor* GetCompositionEditor() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibCommonAttributeEditDocument);
};

#endif
