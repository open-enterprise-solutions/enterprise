#ifndef _METADATA_DOC_H__
#define _METADATA_DOC_H__

#include "common/docInfo.h"
#include "common/codeproc.h"

#include "frontend/metatree/metatreeWnd.h"

// The view using a standard wxTextCtrl to show its contents
class CMetadataView : public CView
{
	CMetadataTree *m_metaTree;

public:

	CMetadataView() : CView() {}

	virtual bool OnCreate(CDocument *doc, long flags) override;
	virtual void OnDraw(wxDC *dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	CMetadataTree *GetMetaTree() const { return m_metaTree; }

protected:

	wxDECLARE_DYNAMIC_CLASS(CMetadataView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CMetadataDocument : public CDocument
{
	CConfigFileMetadata *m_metaData;

public:

	CMetadataDocument() : CDocument() {}
	virtual ~CMetadataDocument() { wxDELETE(m_metaData); }

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual CMetadataTree *GetMetaTree() const = 0;

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
	virtual CMetadataTree *GetMetaTree() const;

	wxDECLARE_NO_COPY_CLASS(CMetataEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CMetataEditDocument);
};

#endif 