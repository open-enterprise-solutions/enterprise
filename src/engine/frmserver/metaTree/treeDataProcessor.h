#ifndef __SERVER_TREE_DATA_PROCESSOR_H__
#define __SERVER_TREE_DATA_PROCESSOR_H__

// THE EXTERNAL DATA PROCESSOR'S TREE, AS THE SERVER HOLDS IT — the desktop's ibDataProcessorTree (designer/mainFrame/
// metaTree), its fill and its watch: the rows of the file being edited, drawn into the view of its tab. The rows are
// those of the configuration's tree (treeConfiguration.h); the node drawn into is the processor's own row.

#include "treeConfiguration.h"

#include "backend/metadataDataProcessor.h"

class FRMSERVER_API ibDataProcessorTree : public ibMetaTreeBase {
public:

	// Drawn for `frame`, whose client it shows to — the tree of `docParent`'s file.
	ibDataProcessorTree(ibClientFrame* frame, ibMetaDocument* docParent);

	virtual ibMetaDataDataProcessor* GetMetaData() const override { return m_metaData.get(); }

	// RENAMED, the file's tab takes the name (the desktop's OnEditCaptionName) — and the rest is the base's.
	virtual void MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object) override;

	// What it shows, held while it shows it — and watched from now on.
	bool Load(std::shared_ptr<ibMetaDataDataProcessor> metaData);

	// THE ROWS, into `root` — the processor's row, its groups and what they hold, read from the metadata as it stands.
	void OnDraw(ibDataNode& root);

private:

	// The fill — the desktop's InitTree and FillData in one walk: a group written whole before the next.
	void FillData(ibDataNode& root);

	ibMetaDocument* const m_docParent;   // borrowed: the file's document, which holds the tree
	std::shared_ptr<ibMetaDataDataProcessor> m_metaData;   // what it shows, held while it shows it
};

#endif
