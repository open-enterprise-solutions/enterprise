#ifndef _FRMCLIENT_DOC_VIEW_FILE_H__
#define _FRMCLIENT_DOC_VIEW_FILE_H__

#include "frmclient/docView/docView.h"

// ----------------------------------------------------------------------------
// A FILE THE SERVER OPENS — the document of every template the server lists (the frame's Templates): the file goes up,
// the server makes the document of it by its id, and it comes as a tab of its own; a new one the server makes of the
// template's extension. That tab holds the document, so nothing here keeps where it is open, and it has no view.
// ----------------------------------------------------------------------------

class FRMCLIENT_API ibFrontendFileDocument : public ibFrontendDocument {
public:

	ibFrontendFileDocument() : ibFrontendDocument() {}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnNewDocument() override;

protected:

	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibFrontendFileDocument);
	wxDECLARE_DYNAMIC_CLASS(ibFrontendFileDocument);
};

#endif
