#ifndef _DESIGNER_DATA_PROC_H__
#define _DESIGNER_DATA_PROC_H__

// THE EXTERNAL DATA PROCESSOR AS THE DESIGNER EDITS IT — the desktop designer's ibDataProcessorFileDocument and
// ibDataProcessorEditView (designer/docManager/templates/docViewDataProcessorFile.h): opened from a file, kept open,
// its tree drawn for the client (View.MetaData), saved back to a file. The runtime's pair of the same file
// (../docViewDataProcessorFile.h) runs it instead.

#include "frmserver/docView/docView.h"
#include "frmserver/metaTree/treeDataProcessor.h"

#include <memory>

// The tree of the file, as the client draws it — the view of its own (the tab's Kind).
class FRMSERVER_API ibDataProcessorTreeView : public ibMetaView {
public:

	ibDataProcessorTreeView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	// THE ROWS, into the frame's MetaData — the tree as it stands.
	virtual void OnDraw(ibDataNode& frame) override;

	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::DataProcessor; }

	ibDataProcessorTree* GetMetaTree() const { return m_metaTree.get(); }

private:

	std::unique_ptr<ibDataProcessorTree> m_metaTree;

	wxDECLARE_DYNAMIC_CLASS(ibDataProcessorTreeView);
};

class FRMSERVER_API ibDataProcessorEditDocument : public ibMetaDataDocument {
	std::shared_ptr<ibMetaDataDataProcessor> m_metaData;   // what it shows, held while it is open
public:

	// Its object's picture — its tab's (the desktop's).
	virtual ibPictureDescription GetIcon() const override;

	ibDataProcessorEditDocument() : ibMetaDataDocument() { m_childDoc = false; }

	virtual ibMetaDataDataProcessor* GetMetaData() const override {
		return m_metaData.get();
	}

	virtual bool OnCreate(const ibString& path, long flags) override;
	virtual bool OnNewDocument() override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	ibDataProcessorTree* GetMetaTree() const;

protected:

	// Its file is the session's temporary one (by its id): what the client uploaded, what it takes down.
	virtual bool DoOpenDocument(const ibString& filename) override;
	virtual bool DoSaveDocument(const ibString& filename) override;

	ibDataProcessorEditDocument(const ibDataProcessorEditDocument&) = delete;
	ibDataProcessorEditDocument& operator=(const ibDataProcessorEditDocument&) = delete;
	wxDECLARE_DYNAMIC_CLASS(ibDataProcessorEditDocument);
};

#endif
