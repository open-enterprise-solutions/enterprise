#ifndef _FEATURE_OPTION_DOC_H__
#define _FEATURE_OPTION_DOC_H__

// The document/view pair behind a FEATURE OPTION — opening one shows its MEMBERS, what is not shown while
// it is off. Mirrors the shared attribute's pair next door (docViewSharedAttribute.h), because it is the
// same gesture: click the thing, tick what it applies to. THE DESKTOP'S
// (designer/docManager/templates/docViewFeatureOption.h) IN A TAB THE SERVER RAISED (Kind FeatureOption): the
// document is the server's (frmserver ibFeatureOptionMembersDocument).

#include "frmclient/docView/tabDocument.h"

class ibFeatureOptionEditView : public ibFrontendView {
	class ibFeatureOptionEditor* m_membersEditor = nullptr;
public:

	ibFeatureOptionEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibFeatureOptionEditor* GetMembersEditor() const { return m_membersEditor; }
};

class ibFeatureOptionEditDocument : public ibTabDocument {
public:

	explicit ibFeatureOptionEditDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE EDITOR'S ROWS — the view's Rows and State, drawn whole whenever the answer changed them.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibFeatureOptionEditor* GetMembersEditor() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibFeatureOptionEditDocument);
};

#endif
