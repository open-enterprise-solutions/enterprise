#ifndef _DATA_PROC_H__
#define _DATA_PROC_H__

#include "frmserver/docView/docView.h"
#include "backend/metadataDataTool.h"

// The view using a standard wxTextCtrl to show its contents
class ibDataToolEditView : public ibMetaView
{
public:

	ibDataToolEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnDraw(ibDataNode& frame) override;
	virtual bool OnClose(bool deleteWindow = true) override;
};

class ibDataToolFileDocument : public ibMetaDocument {
	std::shared_ptr<ibMetaDataDataTool> m_metaData;   // held while it is open — and by its object, from its load on
public:

	ibDataToolFileDocument() : ibMetaDocument() {}

	ibMetaDataDataTool* GetMetaData() const {
		return m_metaData.get();
	}

	virtual bool OnCreate(const ibString& path, long flags) override;
	virtual bool OnNewDocument() override
	{
		// notice that there is no need to neither reset nor even check the
		// modified flag here as the document itself is a new object (this is only
		// called from CreateDocument()) and so it shouldn't be saved anyhow even
		// if it is modified -- this could happen if the user code creates
		// documents pre-filled with some user-entered (and which hence must not be
		// lost) information

		SetDocumentSaved(false);

		const ibString name =
			GetDocumentManager()->MakeNewDocumentName();

		SetTitle(name);
		SetFilename(name, true);

		ibValueMetaObject* rootObject = m_metaData->GetRootMetaObject();
		ibASSERT(rootObject);
		rootObject->SetName(name);

		if (!m_metaData->RunDatabase())
			return false;

		return true;
	}

	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoOpenDocument(const ibString& filename) override;
	virtual bool DoSaveDocument(const ibString& filename) override;

	ibDataToolFileDocument(const ibDataToolFileDocument&) = delete;
	ibDataToolFileDocument& operator=(const ibDataToolFileDocument&) = delete;
};

#endif 