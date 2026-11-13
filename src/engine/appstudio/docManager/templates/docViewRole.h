#ifndef _ROLE_H__
#define _ROLE_H__

#include "frmclient/docView/tabDocument.h"

// THE DESKTOP'S (designer/docManager/templates/docViewRole.h) IN A TAB THE SERVER RAISED (Kind Role): the role is the
// server's document (frmserver ibRoleRightsDocument) — its rights kept in the objects, saved with the configuration
// there; this one shows them in the editor, the rows its view brings (DrawView).

class ibRoleEditView : public ibFrontendView {
	class ibRoleEditor* m_roleEditor = nullptr;
public:

	ibRoleEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibRoleEditor* GetRoleEditor() const { return m_roleEditor; }
};

// ----------------------------------------------------------------------------
// ibRoleEditDocument: the server's role tab and the editor married
// ----------------------------------------------------------------------------

class ibRoleEditDocument : public ibTabDocument {
public:

	explicit ibRoleEditDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE EDITOR'S ROWS — the view's Rows and State, drawn whole whenever the answer changed them.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibRoleEditor* GetRoleEditor() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibRoleEditDocument);
};

#endif
