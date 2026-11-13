#ifndef _DATA_REPORT_H__
#define _DATA_REPORT_H__

#include "frmclient/docView/tabDocument.h"

// THE DESKTOP'S (designer/docManager/templates/docViewDataReportFile.h) IN A TAB THE SERVER RAISED (Kind
// Report): the file is the server's document (frmserver ibReportEditDocument) — made, opened, saved and
// closed there; this one shows its tree, the rows its view brings (DrawView).

class ibReportEditView : public ibFrontendView {
public:

	ibReportEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;

	class ibDataReportTree* GetMetaTree() const { return m_metaTree; }

private:

	class ibDataReportTree* m_metaTree = nullptr;
};

class ibReportFileDocument : public ibTabDocument {
public:

	explicit ibReportFileDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE TREE'S ROWS — the view's MetaData, drawn over what is shown (the tree matches them by their ids): a tab just
	// opened, its view the one the client kept, is drawn all the same.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibDataReportTree* GetMetaTree() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibReportFileDocument);
};

#endif
