#ifndef _DATA_REPORT_H__
#define _DATA_REPORT_H__

#include "frontend/docView/docView.h"
#include "mainFrame/metaTree/treeDataReport.h"

// The view using a standard wxTextCtrl to show its contents
class ibReportEditView : public ibMetaView
{
	ibDataReportTree* m_metaTree;

public:

	ibReportEditView() : ibMetaView() {}

	virtual bool OnCreate(ibMetaDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	ibDataReportTree* GetMetaTree() const { return m_metaTree; }

protected:

	wxDECLARE_DYNAMIC_CLASS(ibReportEditView);
};

static int s_defaultReportNameCounter = 1;

class ibReportFileDocument : public ibMetaDataDocument {
	ibMetaDataReport* m_metaData;
public:

	virtual wxIcon GetIcon() const {
		if (m_metaData != nullptr) {
			ibValueMetaObject* metaObject = m_metaData->GetCommonMetaObject();
			wxASSERT(metaObject);
			return metaObject->GetIcon();
		}
		return wxNullIcon;
	}

	ibReportFileDocument() : ibMetaDataDocument() { m_childDoc = false; }
	virtual ~ibReportFileDocument() { 
		wxDELETE(m_metaData); 
	}

	virtual ibMetaDataReport* GetMetaData() const {
		return m_metaData;
	}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override
	{
		ibValueMetaObject* commonObject = m_metaData->GetCommonMetaObject();
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

		if (!m_metaData->RunDatabase())
			return false;

		if (!GetMetaTree()->Load(m_metaData))
			return false;

		return true;
	}

	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual ibDataReportTree* GetMetaTree() const;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibReportFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibReportFileDocument);
};

#endif 