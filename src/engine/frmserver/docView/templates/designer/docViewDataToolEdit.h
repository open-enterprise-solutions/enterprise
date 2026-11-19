#ifndef _DESIGNER_DATA_PROC_H__
#define _DESIGNER_DATA_PROC_H__

// THE EXTERNAL DATA TOOL AS THE DESIGNER EDITS IT — the desktop designer's ibDataToolFileDocument and
// ibDataToolEditView (designer/docManager/templates/docViewDataToolFile.h): opened from a file, kept open,
// its tree drawn for the client (View.MetaData), saved back to a file. The runtime's pair of the same file
// (../docViewDataToolFile.h) runs it instead.

#include "frmserver/docView/docView.h"
#include "frmserver/metaTree/treeDataTool.h"

#include <memory>

// The tree of the file, as the client draws it — the view of its own (the tab's Kind).
class FRMSERVER_API ibDataToolTreeView : public ibMetaView {
public:

	ibDataToolTreeView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	// THE ROWS, into the frame's MetaData — the tree as it stands.
	virtual void OnDraw(ibDataNode& frame) override;

	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::DataTool; }

	ibDataToolTree* GetMetaTree() const { return m_metaTree.get(); }

private:

	std::unique_ptr<ibDataToolTree> m_metaTree;
};

class FRMSERVER_API ibDataToolEditDocument : public ibMetaDataDocument {
	std::shared_ptr<ibMetaDataDataTool> m_metaData;   // what it shows, held while it is open
public:

	// Its object's picture — its tab's (the desktop's).
	virtual ibPictureDescription GetIcon() const override;

	ibDataToolEditDocument() : ibMetaDataDocument() { m_childDoc = false; }

	virtual ibMetaDataDataTool* GetMetaData() const override {
		return m_metaData.get();
	}

	virtual bool OnCreate(const ibString& path, long flags) override;
	virtual bool OnNewDocument() override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	ibDataToolTree* GetMetaTree() const;

protected:

	// Its file is the session's temporary one (by its id): what the client uploaded, what it takes down.
	virtual bool DoOpenDocument(const ibString& filename) override;
	virtual bool DoSaveDocument(const ibString& filename) override;

	ibDataToolEditDocument(const ibDataToolEditDocument&) = delete;
	ibDataToolEditDocument& operator=(const ibDataToolEditDocument&) = delete;
};

#endif
