#ifndef _METADATA_DOC_H__
#define _METADATA_DOC_H__

#include "frontend/docView/docView.h"
#include "mainFrame/metaTree/treeConfiguration.h"

// The view using a standard wxTextCtrl to show its contents
class ibMetadataEditView : public ibMetaView {
public:

	ibMetadataEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	ibMetadataTree* GetMetaTree() const {  return m_metaTree; }

protected:

	ibMetadataTree* m_metaTree;

	wxDECLARE_DYNAMIC_CLASS(ibMetadataEditView);
};

// ----------------------------------------------------------------------------
// ibMetadataDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class ibMetadataBrowserDocument : public ibMetaDocument {
	
	virtual ibView* DoCreateView() {
		return new ibMetadataEditView();
	}

public:
	
	ibMetadataBrowserDocument(ibMetaDataConfigurationBase* metaData = nullptr) :
		ibMetaDocument(), m_metaData(metaData) { m_childDoc = false; }
	
	virtual wxIcon GetIcon() const {
		if (m_metaData != nullptr) {
			ibValueMetaObject* metaObject = m_metaData->GetCommonMetaObject();
			wxASSERT(metaObject);
			return metaObject->GetIcon();
		}
		return wxNullIcon;
	}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnCloseDocument() override;

	virtual void Modify(bool mod) override {}

	virtual ibMetadataTree* GetMetaTree() const;

protected:

	ibMetaDataConfigurationBase* m_metaData;

	wxDECLARE_NO_COPY_CLASS(ibMetadataBrowserDocument);
	wxDECLARE_DYNAMIC_CLASS(ibMetadataBrowserDocument);
};

class ibMetadataFileDocument : public ibMetadataBrowserDocument {

	virtual ibView* DoCreateView() {
		// Bypass the parent's ibMetadataEditView override and use the default
		// template-driven factory on ibDocument.
		return ibDocument::DoCreateView();
	}

public:

	ibMetadataFileDocument() : ibMetadataBrowserDocument() {}
	virtual ~ibMetadataFileDocument() { wxDELETE(m_metaData); }

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibMetadataFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibMetadataFileDocument);
};


#endif 