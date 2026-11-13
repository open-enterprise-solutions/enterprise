#ifndef _FUNCTIONAL_OPTION_DOC_H__
#define _FUNCTIONAL_OPTION_DOC_H__

// The document/view pair behind a FUNCTIONAL OPTION — opening one shows its MEMBERS, what is not shown while
// it is off. Mirrors the common attribute's pair next door (docViewCommonAttribute.h), because it is the
// same gesture: click the thing, tick what it applies to. THE DESKTOP'S
// (designer/docManager/templates/docViewFunctionalOption.h) IN A TAB THE SERVER RAISED (Kind FunctionalOption): the
// document is the server's (frmserver ibFunctionalOptionMembersDocument).

#include "frmclient/docView/tabDocument.h"

class ibFunctionalOptionEditView : public ibFrontendView {
	class ibFunctionalOptionEditor* m_membersEditor = nullptr;
public:

	ibFunctionalOptionEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibFunctionalOptionEditor* GetMembersEditor() const { return m_membersEditor; }
};

class ibFunctionalOptionEditDocument : public ibTabDocument {
public:

	explicit ibFunctionalOptionEditDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE EDITOR'S ROWS — the view's Rows and State, drawn whole whenever the answer changed them.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibFunctionalOptionEditor* GetMembersEditor() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibFunctionalOptionEditDocument);
};

#endif
