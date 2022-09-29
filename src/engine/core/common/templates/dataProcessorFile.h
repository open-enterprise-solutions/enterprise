#ifndef _DATA_PROC_H__
#define _DATA_PROC_H__

#include "common/docInfo.h"
#include "frontend/metatree/external/dataProcessorWnd.h"

// The view using a standard wxTextCtrl to show its contents
class CDataProcessorView : public CView
{
public:

	CDataProcessorView() : CView() {}

	virtual bool OnCreate(CDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

protected:

	wxDECLARE_DYNAMIC_CLASS(CDataProcessorEditView);
};

// The view using a standard wxTextCtrl to show its contents
class CDataProcessorEditView : public CView
{
	CDataProcessorTree* m_metaTree;

public:

	CDataProcessorEditView() : CView() {}

	virtual bool OnCreate(CDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	CDataProcessorTree* GetMetaTree() const { return m_metaTree; }

protected:

	wxDECLARE_DYNAMIC_CLASS(CDataProcessorEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CDataProcessorDocument : public CDocument,
	public IMetaDocument {
	CMetadataDataProcessor* m_metaData;
public:

	CDataProcessorDocument() : CDocument() {}
	virtual ~CDataProcessorDocument() { /*wxDELETE(m_metaData);*/ }

	virtual CMetadataDataProcessor* GetMetadata() const { 
		return m_metaData;
	}

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

		IMetaObject* commonObject = m_metaData->GetCommonMetaObject();
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

	wxDECLARE_NO_COPY_CLASS(CDataProcessorDocument);
	wxDECLARE_DYNAMIC_CLASS(CDataProcessorDocument);
};

static int s_defaultDataProcessorNameCounter = 1;

class CDataProcessorEditDocument : public CDocument,
	public IMetaDocument {
	CMetadataDataProcessor* m_metaData;
public:

	virtual wxIcon GetIcon() const {
		if (m_metaData != NULL) {
			IMetaObject* metaObject = m_metaData->GetCommonMetaObject();
			wxASSERT(metaObject);
			return metaObject->GetIcon();
		}
		return wxNullIcon;
	}

	CDataProcessorEditDocument() : CDocument() { m_childDoc = false; }
	virtual ~CDataProcessorEditDocument() { wxDELETE(m_metaData); }

	virtual CMetadataDataProcessor* GetMetadata() const {
		return m_metaData;
	}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override
	{
		IMetaObject* commonObject = m_metaData->GetCommonMetaObject();
		wxASSERT(commonObject);

		// notice that there is no need to neither reset nor even check the
		// modified flag here as the document itself is a new object (this is only
		// called from CreateDocument()) and so it shouldn't be saved anyhow even
		// if it is modified -- this could happen if the user code creates
		// documents pre-filled with some user-entered (and which hence must not be
		// lost) information

		SetDocumentSaved(false);

		const wxString name =
			wxString::Format(commonObject->GetClassName() + wxT("%d"), s_defaultDataProcessorNameCounter++);

		SetTitle(name);
		SetFilename(name, true);

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

	virtual CDataProcessorTree* GetMetaTree() const;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CDataProcessorEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CDataProcessorEditDocument);
};

#endif 