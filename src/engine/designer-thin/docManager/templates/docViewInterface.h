#ifndef _INTERFACE_H__
#define _INTERFACE_H__

#include "frmclient/docView/tabDocument.h"

// THE DESKTOP'S (designer/docManager/templates/docViewInterface.h) IN A TAB THE SERVER RAISED (Kind Interface): the
// section is the server's document (frmserver ibInterfaceCompositionDocument) — its composition kept in the objects,
// saved with the configuration there; this one shows it in the editor, the rows its view brings (DrawView).

class ibInterfaceEditView : public ibFrontendView {
	class ibInterfaceEditor* m_interfaceEditor = nullptr;
public:

	ibInterfaceEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibInterfaceEditor* GetInterfaceEditor() const { return m_interfaceEditor; }
};

// ----------------------------------------------------------------------------
// ibInterfaceEditDocument: the server's section tab and the editor married
// ----------------------------------------------------------------------------

class ibInterfaceEditDocument : public ibTabDocument {
public:

	explicit ibInterfaceEditDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE EDITOR'S ROWS — the view's Rows and State, drawn whole whenever the answer changed them.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibInterfaceEditor* GetInterfaceEditor() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibInterfaceEditDocument);
};

#endif
