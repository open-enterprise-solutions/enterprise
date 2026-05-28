#ifndef _DATA_REPORT_H__
#define _DATA_REPORT_H__

#include "frontend/docView/docView.h"
#include "backend/metadataReport.h"

// The view using a standard wxTextCtrl to show its contents
class ibReportEditView : public ibMetaView
{
public:

	ibReportEditView() : ibMetaView() {}

	virtual bool OnCreate(ibMetaDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

protected:

	wxDECLARE_DYNAMIC_CLASS(ibReportEditView);
};

class ibReportFileDocument : public ibMetaDataDocument {
	ibMetaDataReport* m_metaData;
public:

	ibReportFileDocument() : ibMetaDataDocument() {}
	virtual ~ibReportFileDocument() { 
		/*wxDELETE(m_metaData);*/ 
	}

	virtual ibMetaDataReport* GetMetaData() const {
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

		ibValueMetaObject* commonObject = m_metaData->GetCommonMetaObject();
		wxASSERT(commonObject);
		commonObject->SetName(name);

		if (!m_metaData->RunDatabase())
			return false;

		return true;
	}

	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibReportFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibReportFileDocument);
};

#endif 