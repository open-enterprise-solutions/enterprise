#ifndef _DESIGNER_DATA_REPORT_H__
#define _DESIGNER_DATA_REPORT_H__

// THE EXTERNAL REPORT AS THE DESIGNER EDITS IT — the desktop designer's ibReportFileDocument and ibReportEditView
// (designer/docManager/templates/docViewDataReportFile.h): the twin of the data processor's
// (docViewDataProcessorEdit.h). The runtime's pair of the same file (../docViewDataReportFile.h) runs it instead.

#include "frmserver/docView/docView.h"
#include "frmserver/metaTree/treeDataReport.h"

#include <memory>

// The tree of the file, as the client draws it — the view of its own (the tab's Kind).
class FRMSERVER_API ibReportTreeView : public ibMetaView {
public:

	ibReportTreeView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	// THE ROWS, into the frame's MetaData — the tree as it stands.
	virtual void OnDraw(ibDataNode& frame) override;

	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::Report; }

	ibDataReportTree* GetMetaTree() const { return m_metaTree.get(); }

private:

	std::unique_ptr<ibDataReportTree> m_metaTree;

	wxDECLARE_DYNAMIC_CLASS(ibReportTreeView);
};

class FRMSERVER_API ibReportEditDocument : public ibMetaDataDocument {
	std::shared_ptr<ibMetaDataReport> m_metaData;   // what it shows, held while it is open
public:

	// Its object's picture — its tab's (the desktop's).
	virtual ibPictureDescription GetIcon() const override;

	ibReportEditDocument() : ibMetaDataDocument() { m_childDoc = false; }

	virtual ibMetaDataReport* GetMetaData() const override {
		return m_metaData.get();
	}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	ibDataReportTree* GetMetaTree() const;

protected:

	// Its file is the session's temporary one (by its id): what the client uploaded, what it takes down.
	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	ibReportEditDocument(const ibReportEditDocument&) = delete;
	ibReportEditDocument& operator=(const ibReportEditDocument&) = delete;
	wxDECLARE_DYNAMIC_CLASS(ibReportEditDocument);
};

#endif
