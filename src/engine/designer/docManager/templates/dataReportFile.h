#ifndef _DATA_REPORT_H__
#define _DATA_REPORT_H__

#include "frontend/docView/docView.h"
#include "mainFrame/metatree/external/dataReportWnd.h"

// The view using a standard wxTextCtrl to show its contents
class CReportEditView : public CMetaView
{
	CDataReportTree* m_metaTree;

public:

	CReportEditView() : CMetaView() {}

	virtual bool OnCreate(CMetaDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	CDataReportTree* GetMetaTree() const { return m_metaTree; }

protected:

	wxDECLARE_DYNAMIC_CLASS(CReportEditView);
};

static int s_defaultReportNameCounter = 1;

class CReportEditDocument : public IMetaDataDocument {
	CMetaDataReport* m_metaData;
public:

	virtual wxIcon GetIcon() const {
		if (m_metaData != nullptr) {
			IMetaObject* metaObject = m_metaData->GetCommonMetaObject();
			wxASSERT(metaObject);
			return metaObject->GetIcon();
		}
		return wxNullIcon;
	}

	CReportEditDocument() : IMetaDataDocument() { m_childDoc = false; }
	virtual ~CReportEditDocument() { 
		wxDELETE(m_metaData); 
	}

	virtual CMetaDataReport* GetMetaData() const {
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
			wxString::Format(commonObject->GetClassName() + wxT("%d"), s_defaultReportNameCounter++);

		SetTitle(name);
		SetFilename(name, true);

		commonObject->SetName(name);

		m_metaData->Modify(true);

		if (!m_metaData->RunConfiguration())
			return false;

		if (!GetMetaTree()->Load(m_metaData))
			return false;

		return true;
	}

	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual CDataReportTree* GetMetaTree() const;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(CReportEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CReportEditDocument);
};

#endif 