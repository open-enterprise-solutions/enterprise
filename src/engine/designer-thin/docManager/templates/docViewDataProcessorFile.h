#ifndef _DATA_PROC_H__
#define _DATA_PROC_H__

#include "frmclient/docView/tabDocument.h"

// THE DESKTOP'S (designer/docManager/templates/docViewDataProcessorFile.h) IN A TAB THE SERVER RAISED (Kind
// DataProcessor): the file is the server's document (frmserver ibDataProcessorEditDocument) — made, opened, saved and
// closed there; this one shows its tree, the rows its view brings (DrawView).

class ibDataProcessorEditView : public ibFrontendView {
public:

	ibDataProcessorEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;

	class ibDataProcessorTree* GetMetaTree() const { return m_metaTree; }

private:

	class ibDataProcessorTree* m_metaTree = nullptr;
};

class ibDataProcessorFileDocument : public ibTabDocument {
public:

	explicit ibDataProcessorFileDocument(long long tabId) : ibTabDocument(tabId) {}

	// THE TREE'S ROWS — the view's MetaData, drawn over what is shown (the tree matches them by their ids): a tab just
	// opened, its view the one the client kept, is drawn all the same.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibDataProcessorTree* GetMetaTree() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibDataProcessorFileDocument);
};

#endif
