#ifndef __HELP_H__
#define __HELP_H__

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

#include "frmserver/docView/docView.h"

// The view of a help document: the text as its document holds it, drawn into the frame.
class FRMSERVER_API ibHelpEditView : public ibView {
public:

	ibHelpEditView() : ibView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnDraw(ibDataNode& frame) override;

private:

	bool m_readOnly = false;

	wxDECLARE_DYNAMIC_CLASS(ibHelpEditView);
};

// ----------------------------------------------------------------------------
// ibHelpDocument: ibDocument and its text married
// ----------------------------------------------------------------------------

// metaModuleObject.h is included for g_metaCommonModuleCLSID (icon lookup
// only — not for metadata binding; ibHelpDocument is a plain file document).
#include "backend/metaCollection/metaModuleObject.h"

class FRMSERVER_API ibHelpDocument : public ibDocument
{
public:

	virtual ibServerPicture GetIcon() const {
		return ibBackendPicture::GetServerPicture(g_metaCommonModuleCLSID);
	}

	ibHelpDocument() : ibDocument() {}

	// The text — the document's own: it is what the desktop's editor held.
	const wxString& GetText() const { return m_text; }

protected:

	wxString m_text;

	wxDECLARE_NO_COPY_CLASS(ibHelpDocument);
	wxDECLARE_ABSTRACT_CLASS(ibHelpDocument);
};

// ----------------------------------------------------------------------------
// A very simple text document class
// ----------------------------------------------------------------------------

class FRMSERVER_API ibHelpFileDocument : public ibHelpDocument
{
public:

	ibHelpFileDocument() : ibHelpDocument() {}

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

	wxDECLARE_NO_COPY_CLASS(ibHelpFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibHelpFileDocument);
};

#endif
