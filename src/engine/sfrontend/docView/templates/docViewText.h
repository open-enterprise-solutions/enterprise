#ifndef __TEXT_H__
#define __TEXT_H__

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

#include "sfrontend/docView/docView.h"

// The view of a text document: the text as its document holds it, drawn into the frame.
class SFRONTEND_API ibTextEditView : public ibView {
public:

	ibTextEditView() : ibView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnDraw(ibDataNode& frame) override;

protected:

	bool m_readOnly = false;

	wxDECLARE_DYNAMIC_CLASS(ibTextEditView);
};

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and its text married
// ----------------------------------------------------------------------------

// metaModuleObject.h is included for g_metaCommonModuleCLSID (icon lookup
// only — not for metadata binding; ibTextDocument is a plain file document).
#include "backend/metaCollection/metaModuleObject.h"

class SFRONTEND_API ibTextDocument : public ibDocument
{
public:

	virtual wxIcon GetIcon() const {
		return ibBackendPicture::GetPictureAsIcon(g_metaCommonModuleCLSID);
	}

	ibTextDocument() : ibDocument() {}

	// The text — the document's own: it is what the desktop's editor held.
	const wxString& GetText() const { return m_text; }

protected:

	wxString m_text;

	wxDECLARE_NO_COPY_CLASS(ibTextDocument);
	wxDECLARE_ABSTRACT_CLASS(ibTextDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class SFRONTEND_API ibTextFileDocument : public ibTextDocument
{
public:

	ibTextFileDocument() : ibTextDocument() {}

	// THE TEXT FORMAT, said once: the text template is registered with it (the ibDocManager ctor).
	static wxString FileMask() { return wxT("*.txt;*.text"); }
	static wxString FileExtensions() { return wxT("txt;text"); }

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override {

		// notice that there is no need to either reset nor even check the
		// modified flag here as the document itself is a new object (this is only
		// called from CreateDocument()) and so it shouldn't be saved anyhow even
		// if it is modified -- this could happen if the user code creates
		// documents pre-filled with some user-entered (and which hence must not be
		// lost) information

		SetDocumentSaved(false);

		const wxString name = GetDocumentManager()->MakeNewDocumentName();

		SetTitle(name);
		SetFilename(name, true);
		Modify(true);

		return true;
	}

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibTextFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibTextFileDocument);
};

#endif
