#ifndef __SERVER_TREE_DATA_REPORT_H__
#define __SERVER_TREE_DATA_REPORT_H__

// THE EXTERNAL REPORT'S TREE, AS THE SERVER HOLDS IT — the desktop's ibDataReportTree (designer/mainFrame/metaTree),
// its fill and its watch: the twin of the data processor's (treeDataProcessor.h), plus the report's composers.

#include "treeConfiguration.h"

#include "backend/metadataReport.h"

class FRMSERVER_API ibDataReportTree : public ibMetaTreeBase {
public:

	// Drawn for `frame`, whose client it shows to — the tree of `docParent`'s file.
	ibDataReportTree(ibClientFrame* frame, ibMetaDocument* docParent);

	virtual ibMetaDataReport* GetMetaData() const override { return m_metaData.get(); }

	// RENAMED, the file's tab takes the name (the desktop's OnEditCaptionName) — and the rest is the base's.
	virtual void MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object) override;

	// What it shows, held while it shows it — and watched from now on.
	bool Load(std::shared_ptr<ibMetaDataReport> metaData);

	// THE ROWS, into `root` — the report's row, its groups and what they hold, read from the metadata as it stands.
	void OnDraw(ibDataNode& root);

private:

	// The fill — the desktop's InitTree and FillData in one walk: a group written whole before the next.
	void FillData(ibDataNode& root);

	ibMetaDocument* const m_docParent;   // borrowed: the file's document, which holds the tree
	std::shared_ptr<ibMetaDataReport> m_metaData;   // what it shows, held while it shows it
};

#endif
