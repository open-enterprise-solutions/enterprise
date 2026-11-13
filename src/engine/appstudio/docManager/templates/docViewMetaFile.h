#ifndef _METADATA_DOC_H__
#define _METADATA_DOC_H__

#include "frmclient/docView/tabDocument.h"

// THE DESKTOP'S (designer/docManager/templates/docViewMetaFile.h) IN A TAB THE SERVER WOULD RAISE: the configuration it
// browses — the database's (Configuration ▸ Open database configuration: ibMetadataBrowserDocument) or a file's (File ▸
// Open: ibMetadataFileDocument, the desktop's template g_metaCommonMetadataCLSID) — is the server's to load, run
// read-only and close; this one shows its tree (ibView::ibKind::Configuration, MetaData rows). The tree is a tab's
// ibConfigurationTree (read-only), not the frame's (tab 0).

// The view using a standard wxTextCtrl to show its contents
class ibMetadataEditView : public ibFrontendView {
public:

	ibMetadataEditView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibFrontendView* activeView, ibFrontendView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	class ibConfigurationTree* GetMetaTree() const {  return m_metaTree; }

protected:

	class ibConfigurationTree* m_metaTree = nullptr;
};

// ----------------------------------------------------------------------------
// ibMetadataDocument: the server's configuration tab and the tree married
// ----------------------------------------------------------------------------

class ibMetadataBrowserDocument : public ibTabDocument {
public:

	explicit ibMetadataBrowserDocument(long long tabId) : ibTabDocument(tabId) {}

	// (The desktop's GetIcon — the root metaobject's — and its OnCreate, the tree loaded with the configuration it held:
	// the tab's and its rows'. Its OnCloseDocument gave the inspector back the active configuration's root; the thin's
	// tab is closed by the server, and the inspector follows the tree that has the focus.)

	// THE TREE'S ROWS — the view's MetaData, drawn over what is shown, as an external file's tree draws them.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	class ibConfigurationTree* GetMetaTree() const;

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibMetadataBrowserDocument);
};

// (The desktop's file document made a file's configuration (ibMetaDataConfigurationFile), loaded it (LoadConfigFromFile),
// ran it read-only (RunDatabase, onlyLoadFlag) and closed it with the tab (CloseDatabase) — the server's, its tab's. Its
// view is the browser's: the desktop's template made the same one.)
class ibMetadataFileDocument : public ibMetadataBrowserDocument {
public:

	explicit ibMetadataFileDocument(long long tabId) : ibMetadataBrowserDocument(tabId) {}

	wxDECLARE_NO_COPY_CLASS(ibMetadataFileDocument);
};


#endif
