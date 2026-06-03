#ifndef _DATA_PROC_H__
#define _DATA_PROC_H__

#include "frontend/docView/docView.h"
#include "mainFrame/metaTree/treeDataProcessor.h"

// The view using a standard wxTextCtrl to show its contents
class ibDataProcessorEditView : public ibMetaView
{
	ibDataProcessorTree* m_metaTree;

public:

	ibDataProcessorEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	ibDataProcessorTree* GetMetaTree() const { return m_metaTree; }

protected:

	wxDECLARE_DYNAMIC_CLASS(ibDataProcessorEditView);
};

static int s_defaultDataProcessorNameCounter = 1;

class ibDataProcessorFileDocument : public ibMetaDataDocument {
	ibMetaDataDataProcessor* m_metaData;
public:

	virtual wxIcon GetIcon() const {
		if (m_metaData != nullptr) {
			ibValueMetaObject* metaObject = m_metaData->GetCommonMetaObject();
			wxASSERT(metaObject);
			return metaObject->GetIcon();
		}
		return wxNullIcon;
	}

	ibDataProcessorFileDocument() : ibMetaDataDocument() { m_childDoc = false; }
	virtual ~ibDataProcessorFileDocument() {
		wxDELETE(m_metaData);
	}

	virtual ibMetaDataDataProcessor* GetMetaData() const {
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
			wxString::Format(commonObject->GetClassName() + wxT("%d"), s_defaultDataProcessorNameCounter++);

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

	virtual ibDataProcessorTree* GetMetaTree() const;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;
	virtual bool DoSaveDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibDataProcessorFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibDataProcessorFileDocument);
};

#endif 