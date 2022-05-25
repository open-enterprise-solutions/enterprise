#ifndef _DATA_REPORT_H__
#define _DATA_REPORT_H__

#include "common/docInfo.h"
#include "common/codeproc.h"

#include "frontend/metatree/external/dataReportWnd.h"

// The view using a standard wxTextCtrl to show its contents
class CReportView : public CView
{
public:

	CReportView() : CView() {}

	virtual bool OnCreate(CDocument *doc, long flags) override;
	virtual void OnDraw(wxDC *dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

protected:

	wxDECLARE_DYNAMIC_CLASS(CReportEditView);
};

// The view using a standard wxTextCtrl to show its contents
class CReportEditView : public CView
{
	CDataReportTree *m_metaTree;

public:

	CReportEditView() : CView() {}

	virtual bool OnCreate(CDocument *doc, long flags) override;
	virtual void OnDraw(wxDC *dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	CDataReportTree *GetMetaTree() const { return m_metaTree; }

protected:

	wxDECLARE_DYNAMIC_CLASS(CReportEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CReportDocument : public CDocument,
	public IMetaDocument
{
	CMetadataReport *m_metaData;

public:

	CReportDocument() : CDocument() {}
	virtual ~CReportDocument() { /*wxDELETE(m_metaData);*/ }

	virtual IMetadata *GetMetadata() const { return m_metaData; }

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override
	{
		// notice that there is no need to neither reset nor even check the
		// modified flag here as the document itself is a new object (this is only
		// called from CreateDocument()) and so it shouldn't be saved anyhow even
		// if it is modified -- this could happen if the user code creates
		// documents pre-filled with some user-entered (and which hence must not be
		// lost) information

		SetDocumentSaved(false);

		const wxString name = 
			GetDocumentManager()->MakeNewDocumentName();

		SetTitle(name);
		SetFilename(name, true);

		IMetaObject *commonObject = m_metaData->GetCommonMetaObject();
		wxASSERT(commonObject);
		commonObject->SetName(name);

		if (!m_metaData->RunMetadata())
			return false;

		return true;
	}

	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CReportDocument);
	wxDECLARE_DYNAMIC_CLASS(CReportDocument);
};

class CReportEditDocument : public CDocument,
	public IMetaDocument
{
	CMetadataReport *m_metaData;

public:

	CReportEditDocument() : CDocument() { m_childDoc = false; }
	virtual ~CReportEditDocument() { wxDELETE(m_metaData); }

	virtual IMetadata *GetMetadata() const { return m_metaData; }

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override
	{
		// notice that there is no need to neither reset nor even check the
		// modified flag here as the document itself is a new object (this is only
		// called from CreateDocument()) and so it shouldn't be saved anyhow even
		// if it is modified -- this could happen if the user code creates
		// documents pre-filled with some user-entered (and which hence must not be
		// lost) information

		SetDocumentSaved(false);

		const wxString name = 
			GetDocumentManager()->MakeNewDocumentName();

		SetTitle(name);
		SetFilename(name, true);

		IMetaObject *commonObject = m_metaData->GetCommonMetaObject();
		wxASSERT(commonObject);
		commonObject->SetName(name);

		m_metaData->Modify(true);

		if (!m_metaData->RunMetadata())
			return false;

		if (!GetMetaTree()->Load(m_metaData))
			return false;

		return true;
	}

	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual CDataReportTree *GetMetaTree() const;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CReportEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CReportEditDocument);
};

#endif 