#ifndef _DATA_PROC_H__
#define _DATA_PROC_H__

#include "frmserver/docView/docView.h"
#include "backend/metadataDataProcessor.h"

// The view using a standard wxTextCtrl to show its contents
class ibDataProcessorEditView : public ibMetaView
{
public:

	ibDataProcessorEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnDraw(ibDataNode& frame) override;
	virtual bool OnClose(bool deleteWindow = true) override;

protected:

	wxDECLARE_DYNAMIC_CLASS(ibDataProcessorEditView);
};

class ibDataProcessorFileDocument : public ibMetaDocument {
	std::shared_ptr<ibMetaDataDataProcessor> m_metaData;   // held while it is open — and by its object, from its load on
public:

	ibDataProcessorFileDocument() : ibMetaDocument() {}

	ibMetaDataDataProcessor* GetMetaData() const {
		return m_metaData.get();
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

	wxDECLARE_NO_COPY_CLASS(ibDataProcessorFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibDataProcessorFileDocument);
};

#endif 