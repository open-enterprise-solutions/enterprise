#ifndef _METADATA_DOC_H__
#define _METADATA_DOC_H__

#include "frontend/docView/docView.h"
#include "mainFrame/metatree/metatreeWnd.h"

// The view using a standard wxTextCtrl to show its contents
class CMetadataView : public CMetaView
{
	CMetadataTree* m_metaTree;

public:

	CMetadataView() : CMetaView() {}

	virtual bool OnCreate(CMetaDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	CMetadataTree* GetMetaTree() const { 
		return m_metaTree;
	}

protected:

	wxDECLARE_DYNAMIC_CLASS(CMetadataView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CMetadataDocument : public CMetaDocument {
	CMetaDataConfigurationFile* m_metaData;
public:

	virtual wxIcon GetIcon() const {
		if (m_metaData != nullptr) {
			IMetaObject* metaObject = m_metaData->GetCommonMetaObject();
			wxASSERT(metaObject);
			return metaObject->GetIcon();
		}
		return wxNullIcon;
	}

	CMetadataDocument() : CMetaDocument(), m_metaData(nullptr) {}
	virtual ~CMetadataDocument() { wxDELETE(m_metaData); }

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual CMetadataTree* GetMetaTree() const = 0;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CMetadataDocument);
	wxDECLARE_ABSTRACT_CLASS(CMetadataDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class CMetataEditDocument : public CMetadataDocument
{
public:

	CMetataEditDocument() : CMetadataDocument() { m_childDoc = false; }
	virtual CMetadataTree* GetMetaTree() const;

	wxDECLARE_NO_COPY_CLASS(CMetataEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CMetataEditDocument);
};

#endif 