#ifndef __SERVER_TREE_DATA_TOOL_H__
#define __SERVER_TREE_DATA_TOOL_H__

// THE EXTERNAL DATA TOOL'S TREE, AS THE SERVER HOLDS IT — the desktop's ibDataToolTree (designer/mainFrame/
// metaTree), its fill and its watch: the rows of the file being edited, drawn into the view of its tab. The rows are
// those of the application's tree (treeApplication.h); the node drawn into is the data tool's own row.

#include "treeApplication.h"

#include "backend/metadataDataTool.h"

class FRMSERVER_API ibDataToolTree : public ibMetaTreeBase {
public:

	// Drawn for `frame`, whose client it shows to — the tree of `docParent`'s file.
	ibDataToolTree(ibClientFrame* frame, ibMetaDocument* docParent);

	virtual ibMetaDataDataTool* GetMetaData() const override { return m_metaData.get(); }

	// RENAMED, the file's tab takes the name (the desktop's OnEditCaptionName) — and the rest is the base's.
	virtual void MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object) override;

	// What it shows, held while it shows it — and watched from now on.
	bool Load(std::shared_ptr<ibMetaDataDataTool> metaData);

	// THE ROWS, into `root` — the data tool's row, its groups and what they hold, read from the metadata as it stands.
	void OnDraw(ibDataNode& root);

private:

	// The fill — the desktop's InitTree and FillData in one walk: a group written whole before the next.
	void FillData(ibDataNode& root);

	ibMetaDocument* const m_docParent;   // borrowed: the file's document, which holds the tree
	std::shared_ptr<ibMetaDataDataTool> m_metaData;   // what it shows, held while it shows it
};

#endif
