#ifndef __FRMSERVER_DOC_VIEW_META_FILE_H__
#define __FRMSERVER_DOC_VIEW_META_FILE_H__

////////////////////////////////////////////////////////////////////////////
// A CONFIGURATION BROWSED READ-ONLY — File → Open (.oap) or Open database configuration
////////////////////////////////////////////////////////////////////////////
//
// The desktop's ibMetadataFileDocument / ibMetadataBrowserDocument
// (designer/docManager/templates/docViewMetaFile.h): a tab over a configuration that is not the
// one being edited — a file's (ConfigurationFile, RunDatabase onlyLoadFlag) or the database's copy.
// Its tree is drawn into the view's MetaData (ibView::ibKind::Configuration); the client's tab shows
// it read-only.
////////////////////////////////////////////////////////////////////////////

#include "frmserver/docView/docView.h"
#include "frmserver/metaTree/treeConfiguration.h"
#include "backend/metadataConfiguration.h"

#include <memory>

class FRMSERVER_API ibConfigurationBrowserView : public ibMetaView {
public:

	ibConfigurationBrowserView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocol::ibView::ibKind GetViewKind() const override {
		return ibProtocol::ibView::ibKind::Configuration;
	}

	ibConfigurationTree* GetMetaTree() const { return m_metaTree.get(); }

private:

	std::unique_ptr<ibConfigurationTree> m_metaTree;

	wxDECLARE_DYNAMIC_CLASS(ibConfigurationBrowserView);
};

// A file's configuration, or the database's copy held for browsing — both are ConfigurationFile-shaped.
class FRMSERVER_API ibMetadataBrowserDocument : public ibMetaDataDocument {
public:

	ibMetadataBrowserDocument() : ibMetaDataDocument() { m_childDoc = false; }
	explicit ibMetadataBrowserDocument(std::shared_ptr<ibMetaDataConfigurationBase> metaData)
		: ibMetaDataDocument(), m_metaData(std::move(metaData)) { m_childDoc = false; }

	virtual ibMetaData* GetMetaData() const override { return m_metaData.get(); }
	virtual ibPictureDescription GetIcon() const override;

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnCloseDocument() override;

	ibConfigurationTree* GetMetaTree() const;

protected:

	std::shared_ptr<ibMetaDataConfigurationBase> m_metaData;

	wxDECLARE_NO_COPY_CLASS(ibMetadataBrowserDocument);
	wxDECLARE_DYNAMIC_CLASS(ibMetadataBrowserDocument);
};

class FRMSERVER_API ibMetadataFileDocument : public ibMetadataBrowserDocument {
public:

	ibMetadataFileDocument() : ibMetadataBrowserDocument() {}

	virtual bool OnCreate(const wxString& path, long flags) override;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibMetadataFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibMetadataFileDocument);
};

#endif
